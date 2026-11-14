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
typedef struct gm_cs32e03j420uj1 { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_cs32e03j420uj1;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[40]; } gm_t6kvj62qe5fp2;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
_Static_assert(sizeof(gm_cs32e03j420uj1) == 24 && _Alignof(gm_cs32e03j420uj1) == 8 && offsetof(gm_cs32e03j420uj1, f0) == 0 && offsetof(gm_cs32e03j420uj1, f1) == 8 && offsetof(gm_cs32e03j420uj1, f2) == 16 && offsetof(gm_cs32e03j420uj1, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t6kvj62qe5fp2) == 40 && _Alignof(gm_t6kvj62qe5fp2) == 1 && offsetof(gm_t6kvj62qe5fp2, a) == 0, "layout of `[40]u8`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_args_881ap90 {
    gm_frame h;
    gm_toa2vhe3l4hfr l6; /* a */
    gm_b0_1 ret;
};
struct gm_fr_components_dvsj2j9 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* sig */
    gm_tddtjfggdat0d l26; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_ref_6sjtehp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_effects_mg3vf2a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_is_empty_set_3vntabf {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* a */
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_key_fr74f0n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parse_exponent_4uekfcv {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
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
struct gm_fr_sig_uguln8a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ty_vka2r8h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_debug_string_pns0400 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_float_7p0icl6 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* digits */
    gm_b0_1 ret;
};
struct gm_fr_write_int_lq1ktof {
    gm_frame h;
    gm_t6kvj62qe5fp2 l8; /* digits */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_gu67n99(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_len_op_symbol_2qoiciu(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0f0dp07(void * a0, gm_tu0pangbbaeec a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2q6gmep(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g9nufph(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m28h1lv(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n5e95le(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1);
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0lrfrtvffhu7[4];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s15tjb1oda5hu[6];
extern const uint8_t gm_s1rqgg2ibsis6[10];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4imhc4v1tf2p[10];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5j8t1cg2erp9[8];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s7mhpbh399e5s[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sb7t8ia5pj1l5[19];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfif278eeflhm[4];
extern const uint8_t gm_sfqmvjqk1sdmt[6];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm337p11h7spc[8];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcp2qdr7uqsmui[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
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
        gm_b0_1 t6 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t4, t5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_args_881ap90 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t428ue2u3hf5g gm_l8; /* k */
    GM_STACK_CHECK(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            void * t6 = gm_l0;
            uint64_t t7 = gm_l2;
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 41);
            uint64_t t10 = r8;
            uint64_t t11 = (uintptr_t)t10;
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srcp2qdr7uqsmui, 203, 36);
            uint8_t t13 = gm_f_is_empty_set_3vntabf(t6, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 40)));
            if (!t13) goto lb3;
            {
                uint64_t t16 = gm_l2;
                uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 51);
                uint64_t t19 = r17;
                gm_l2 = t19;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t21 = gm_l2;
        uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
        if (!t22) goto ie20;
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
        gm_tftb1odc739t5 t28 = gm_f_push_t83rg2m(t27, ((uint8_t)40ULL));
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcp2qdr7uqsmui, 205, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        {
            uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t32 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t34 = gm_l2;
            uint64_t t35 = (uintptr_t)t34;
            if (t33 > t35 || t35 > (uintptr_t)t31) gm_trap_slice(t33, t35, t31, gm_srcp2qdr7uqsmui, 206, 27);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t32 + t33 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t33));
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
                    gm_tdr8bh2bm3fc8 t43;
                    uint64_t t44 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t43 + 0, t44);
                    uint64_t t45 = gm_l4;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t43 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t46 * 40), 40);
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
                    gm_l5 = t50;
                    F.l6 = (*(gm_toa2vhe3l4hfr *)(((uint8_t *)&t43) + 8));
                    {
                        uint64_t t53 = gm_l5;
                        uint8_t t54 = (t53 > ((uint64_t)0ULL));
                        if (!t54) goto ie52;
                        {
                            void * t56 = gm_l0;
                            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 8));
                            gm_b16_8 t58;
                            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t59 = gm_f_extend_0cs85gs(t57, t58);
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcp2qdr7uqsmui, 207, 20);
                            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        }
                        goto ix52;
ie52:;
ix52:;
                    }
                    {
                        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t63 != ((uint8_t)0ULL)) goto mn62;
                        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                        gm_l7 = t64;
                        void * t65 = gm_l0;
                        gm_t88rcucg1b7uk t66 = gm_l7;
                        gm_b0_1 t67 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t65, t66);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        goto me61;
mn62:;
                        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t69 != ((uint8_t)1ULL)) goto mn68;
                        gm_t428ue2u3hf5g t70 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l6) + 8));
                        gm_l8 = t70;
                        void * t71 = gm_l0;
                        void * t72 = (void *)((uint8_t *)&gm_l8);
                        gm_b0_1 t73 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t71, t72);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        goto me61;
mn68:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 208, 9);
me61:;
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 206, 5);
me48:;
                }
lc37:;
                {
                    uint64_t t75 = gm_l4;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l4 = t78;
                }
            }
gm_ll41:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
            }
            goto gm_lad;
lb37:;
        }
    }
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 8));
        gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)41ULL));
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcp2qdr7uqsmui, 213, 5);
        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_components_dvsj2j9 F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* out */
    uint8_t gm_l3; /* signed */
    uint16_t gm_l4; /* bits */
    uint16_t gm_l5; /* bits */
    uint8_t gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint8_t gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint8_t gm_l10; /* c */
    gm_t88rcucg1b7uk gm_l11; /* t */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_tnnjeerolos0a gm_l14; /* kind */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* t */
    gm_t88rcucg1b7uk gm_l22; /* t */
    gm_t88rcucg1b7uk gm_l23; /* err */
    gm_t88rcucg1b7uk gm_l24; /* ok */
    uint32_t gm_l27; /* id */
    gm_b16_8 gm_l28; /* args */
    gm_b16_8 gm_l29; /* name */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_t88rcucg1b7uk gm_l31; /* t */
    uint32_t gm_l32; /* id */
    gm_t88rcucg1b7uk gm_l33; /* len */
    gm_t88rcucg1b7uk gm_l34; /* elem */
    gm_t9qkh0rsooc8d gm_l35; /* op */
    gm_t88rcucg1b7uk gm_l36; /* lhs */
    gm_t88rcucg1b7uk gm_l37; /* rhs */
    gm_tqsk8dt6ib3qq gm_l38; /* <temporary array> */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_t88rcucg1b7uk gm_l42; /* t */
    gm_t428ue2u3hf5g gm_l43; /* k */
    gm_tnpifv20irnph gm_l44; /* set */
    GM_STACK_CHECK(&F, 2570u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma23; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma25; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma27; case ((uint8_t)19ULL): goto ma28; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma31; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma32; case ((uint8_t)25ULL): goto ma33; case ((uint8_t)26ULL): goto ma35; default: goto ma36; }
ma9:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t39 != ((uint8_t)0ULL)) goto mn38;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l3 = t40;
        uint16_t t41 = GM_LD(uint16_t, ((((uint8_t *)&t7) + 8) + 2));
        gm_l4 = t41;
        void * t42 = gm_l2;
        gm_tvaas22dl64e0 t43;
        memset((uint8_t *)&t43 + 18, 0, 6);
        gm_b16_8 t44;
        uint8_t t46 = gm_l3;
        if (!t46) goto ie45;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sitlbs33jqqal));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
        gm_b16_8 t49 = t48;
        t44 = t49;
        goto ix45;
ie45:;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s0m9d22aen6f8));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
        gm_b16_8 t52 = t51;
        t44 = t52;
ix45:;
        GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t44);
        uint16_t t53 = gm_l4;
        GM_ST(uint16_t, (uint8_t *)&t43 + 16, t53);
        gm_tftb1odc739t5 t54 = gm_f_write_8gmb252(t42, &t43);
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcp2qdr7uqsmui, 56, 31);
        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        goto me8;
mn38:;
ma10:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t57 != ((uint8_t)1ULL)) goto mn56;
        void * t58 = gm_l2;
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t60 = gm_f_extend_0cs85gs(t58, t59);
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srcp2qdr7uqsmui, 57, 19);
        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        goto me8;
mn56:;
ma11:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t63 != ((uint8_t)2ULL)) goto mn62;
        void * t64 = gm_l2;
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t66 = gm_f_extend_0cs85gs(t64, t65);
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcp2qdr7uqsmui, 58, 19);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
        goto me8;
mn62:;
ma12:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t69 != ((uint8_t)3ULL)) goto mn68;
        uint16_t t70 = GM_LD(uint16_t, (((uint8_t *)&t7) + 8));
        gm_l5 = t70;
        void * t71 = gm_l2;
        uint16_t t72 = gm_l5;
        gm_tu0pangbbaeec t73;
        GM_ST(uint16_t, ((uint8_t *)&t73 + 0), t72);
        gm_tftb1odc739t5 t74 = gm_f_write_0f0dp07(t71, t73);
        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcp2qdr7uqsmui, 59, 25);
        gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
        goto me8;
mn68:;
ma13:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t77 != ((uint8_t)4ULL)) goto mn76;
        void * t78 = gm_l2;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t80 = gm_f_extend_0cs85gs(t78, t79);
        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcp2qdr7uqsmui, 60, 18);
        gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        goto me8;
mn76:;
ma14:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t83 != ((uint8_t)5ULL)) goto mn82;
        void * t84 = gm_l2;
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s3cv29r83pgn4));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t86 = gm_f_extend_0cs85gs(t84, t85);
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcp2qdr7uqsmui, 61, 19);
        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        goto me8;
mn82:;
ma15:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t89 != ((uint8_t)6ULL)) goto mn88;
        void * t90 = gm_l2;
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t92 = gm_f_extend_0cs85gs(t90, t91);
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcp2qdr7uqsmui, 62, 18);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
        goto me8;
mn88:;
ma16:;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t95 != ((uint8_t)7ULL)) goto mn94;
        void * t96 = gm_l2;
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_slolcr6ikvg33));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(6));
        gm_tftb1odc739t5 t98 = gm_f_extend_0cs85gs(t96, t97);
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcp2qdr7uqsmui, 63, 20);
        gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
        goto me8;
mn94:;
ma17:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t101 != ((uint8_t)8ULL)) goto mn100;
        uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l6 = t102;
        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l7 = t103;
        {
            void * t105 = gm_l2;
            gm_b16_8 t106;
            uint8_t t108 = gm_l6;
            if (!t108) goto ie107;
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sm337p11h7spc));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(7));
            gm_b16_8 t111 = t110;
            t106 = t111;
            goto ix107;
ie107:;
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_srs1vrn2uaams));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(1));
            gm_b16_8 t114 = t113;
            t106 = t114;
ix107:;
            gm_tftb1odc739t5 t115 = gm_f_extend_0cs85gs(t105, t106);
            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcp2qdr7uqsmui, 65, 13);
            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
        }
        {
            void * t117 = gm_l0;
            gm_t88rcucg1b7uk t118 = gm_l7;
            gm_b0_1 t119 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn100:;
ma18:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t121 != ((uint8_t)9ULL)) goto mn120;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l8 = t122;
        gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l9 = t123;
        {
            void * t125 = gm_l2;
            gm_b16_8 t126;
            uint8_t t128 = gm_l8;
            if (!t128) goto ie127;
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s1rqgg2ibsis6));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(9));
            gm_b16_8 t131 = t130;
            t126 = t131;
            goto ix127;
ie127:;
            gm_b16_8 t133;
            GM_ST(void *, (uint8_t *)&t133, (void *)(gm_sfif278eeflhm));
            GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(3));
            gm_b16_8 t134 = t133;
            t126 = t134;
ix127:;
            gm_tftb1odc739t5 t135 = gm_f_extend_0cs85gs(t125, t126);
            if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srcp2qdr7uqsmui, 69, 13);
            gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
        }
        {
            void * t137 = gm_l0;
            gm_t88rcucg1b7uk t138 = gm_l9;
            gm_b0_1 t139 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t137, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn120:;
ma19:;
        uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t141 != ((uint8_t)10ULL)) goto mn140;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l10 = t142;
        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l11 = t143;
        {
            uint8_t t146 = gm_l10;
            if (!t146) goto ie145;
            void * t147 = gm_l0;
            void * t148 = GM_LD(void *, (((uint8_t *)t147) + 0));
            gm_t88rcucg1b7uk t149 = gm_l11;
            gm_t53at80pfdus3 t150;
            gm_f_kind_jcnt4hm((void *)&t150, t148, GM_CONV(gm_csguankko4lmhd, t149));
            gm_tjcj4j2gbh6c8 t151;
            GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)0));
            GM_ST(uint16_t, ((uint8_t *)&t151 + 2), ((uint16_t)8ULL));
            uint8_t t152 = (((GM_LD(uint8_t, ((uint8_t *)&t150) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t150) + 8), ((uint8_t *)&t151))));
            if (!t152) goto ie145;
            {
                void * t154 = gm_l2;
                gm_b16_8 t155;
                GM_ST(void *, (uint8_t *)&t155, (void *)(gm_s0iqiubfm7e7e));
                GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t156 = gm_f_extend_0cs85gs(t154, t155);
                if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srcp2qdr7uqsmui, 74, 17);
                gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
            }
            goto ix145;
ie145:;
            {
                void * t159 = gm_l2;
                gm_b16_8 t160;
                uint8_t t162 = gm_l10;
                if (!t162) goto ie161;
                gm_b16_8 t164;
                GM_ST(void *, (uint8_t *)&t164, (void *)(gm_shsv7bl83q04d));
                GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(8));
                gm_b16_8 t165 = t164;
                t160 = t165;
                goto ix161;
ie161:;
                gm_b16_8 t167;
                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_s3ndsddjcvskd));
                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(2));
                gm_b16_8 t168 = t167;
                t160 = t168;
ix161:;
                gm_tftb1odc739t5 t169 = gm_f_extend_0cs85gs(t159, t160);
                if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srcp2qdr7uqsmui, 76, 17);
                gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
            }
            {
                void * t171 = gm_l0;
                gm_t88rcucg1b7uk t172 = gm_l11;
                gm_b0_1 t173 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix145:;
        }
        goto me8;
mn140:;
ma20:;
        uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t175 != ((uint8_t)11ULL)) goto mn174;
        uint64_t t176 = GM_LD(uint64_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l12 = t176;
        gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l13 = t177;
        gm_tnnjeerolos0a t178 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t7) + 8) + 12));
        gm_l14 = t178;
        {
            uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            switch (t186) { case ((uint8_t)0ULL): goto ma184; case ((uint8_t)1ULL): goto ma181; case ((uint8_t)2ULL): goto ma182; case ((uint8_t)3ULL): goto ma183; default: goto ma185; }
ma181:;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t188 != ((uint8_t)1ULL)) goto mn187;
            gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 4));
            gm_l15 = t189;
            {
                void * t191 = gm_l2;
                gm_tftb1odc739t5 t192 = gm_f_push_t83rg2m(t191, ((uint8_t)91ULL));
                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srcp2qdr7uqsmui, 83, 21);
                gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
            }
            {
                void * t194 = gm_l0;
                gm_t88rcucg1b7uk t195 = gm_l15;
                gm_b0_1 t196 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t194, t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t197 = gm_l2;
                gm_tftb1odc739t5 t198 = gm_f_push_t83rg2m(t197, ((uint8_t)93ULL));
                if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srcp2qdr7uqsmui, 85, 21);
                gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
            }
            goto me180;
mn187:;
ma182:;
            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t201 != ((uint8_t)2ULL)) goto mn200;
            void * t202 = gm_l2;
            uint64_t t203 = gm_l12;
            gm_t4ce0jd3tdro4 t204;
            GM_ST(uint64_t, ((uint8_t *)&t204 + 0), t203);
            gm_tftb1odc739t5 t205 = gm_f_write_n5e95le(t202, t204);
            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_srcp2qdr7uqsmui, 87, 26);
            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
            goto me180;
mn200:;
ma183:;
            uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t208 != ((uint8_t)3ULL)) goto mn207;
            void * t209 = gm_l2;
            uint64_t t210 = gm_l12;
            gm_t4ce0jd3tdro4 t211;
            GM_ST(uint64_t, ((uint8_t *)&t211 + 0), t210);
            gm_tftb1odc739t5 t212 = gm_f_write_2q6gmep(t209, t211);
            if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_srcp2qdr7uqsmui, 88, 26);
            gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
            goto me180;
mn207:;
ma184:;
            uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t215 != ((uint8_t)0ULL)) goto mn214;
            void * t216 = gm_l2;
            uint64_t t217 = gm_l12;
            gm_t4ce0jd3tdro4 t218;
            GM_ST(uint64_t, ((uint8_t *)&t218 + 0), t217);
            gm_tftb1odc739t5 t219 = gm_f_write_u50pgu2(t216, t218);
            if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_srcp2qdr7uqsmui, 89, 27);
            gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
            goto me180;
mn214:;
ma185:;
            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 81, 13);
me180:;
        }
        {
            void * t221 = gm_l0;
            gm_t88rcucg1b7uk t222 = gm_l13;
            gm_b0_1 t223 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t221, t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn174:;
ma21:;
        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t225 != ((uint8_t)12ULL)) goto mn224;
        gm_b16_8 t226 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 0));
        gm_l16 = t226;
        gm_b16_8 t227 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 16));
        gm_l17 = t227;
        {
            void * t229 = gm_l2;
            gm_tftb1odc739t5 t230 = gm_f_push_t83rg2m(t229, ((uint8_t)40ULL));
            if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 1, gm_srcp2qdr7uqsmui, 94, 13);
            gm_b0_1 t231 = GM_LD(gm_b0_1, (((uint8_t *)&t230) + 1));
        }
        {
            {
                gm_b16_8 t233 = gm_l16;
                gm_l18 = t233;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t235 = gm_l19;
                    uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t237 = (t235 < t236);
                    if (!t237) goto lb234;
                    {
                        uint64_t t240 = gm_l19;
                        uint64_t t241 = gm_l19;
                        uint64_t t242 = (uintptr_t)t241;
                        uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t242 * 4));
                        gm_tlcm4lvu3g1a8 t245;
                        GM_ST(uint32_t, (uint8_t *)&t245 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t245 + 0), t240);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t245 + 8), t244);
                        uint64_t t248 = GM_LD(uint64_t, (((uint8_t *)&t245) + 0));
                        gm_l20 = t248;
                        gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t245) + 8));
                        gm_l21 = t249;
                        {
                            uint64_t t252 = gm_l20;
                            uint8_t t253 = (t252 > ((uint64_t)0ULL));
                            if (!t253) goto ie251;
                            {
                                void * t255 = gm_l2;
                                gm_b16_8 t256;
                                GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t257 = gm_f_extend_0cs85gs(t255, t256);
                                if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 1, gm_srcp2qdr7uqsmui, 96, 28);
                                gm_b0_1 t258 = GM_LD(gm_b0_1, (((uint8_t *)&t257) + 1));
                            }
                            goto ix251;
ie251:;
ix251:;
                        }
                        {
                            uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            uint8_t t261 = (t260 > ((uint64_t)0ULL));
                            if (!t261) goto ie259;
                            {
                                void * t263 = gm_l2;
                                uint64_t t264 = gm_l20;
                                uint64_t t265 = (uintptr_t)t264;
                                uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                if (t265 >= (uintptr_t)(t266)) gm_trap_index(t265, t266, gm_srcp2qdr7uqsmui, 97, 54);
                                gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t265 * 16));
                                gm_tnpifv20irnph t268;
                                GM_ST(gm_b16_8, ((uint8_t *)&t268 + 0), t267);
                                gm_tftb1odc739t5 t269 = gm_f_write_g9nufph(t263, t268);
                                if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 1, gm_srcp2qdr7uqsmui, 97, 36);
                                gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t269) + 1));
                            }
                            goto ix259;
ie259:;
ix259:;
                        }
                        {
                            void * t271 = gm_l0;
                            gm_t88rcucg1b7uk t272 = gm_l21;
                            gm_b0_1 t273 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t271, t272);
                            if (gm_unw.kind) {
                                goto gm_ll238;
                            }
                        }
                        goto me246;
mn247:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 95, 13);
me246:;
                    }
lc234:;
                    {
                        uint64_t t275 = gm_l19;
                        uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                        uint64_t t278 = r276;
                        gm_l19 = t278;
                    }
                }
gm_ll238:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb234; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc234; }
                }
                goto gm_lad;
lb234:;
            }
        }
        {
            uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t281 = ((t280 == ((uint64_t)1ULL)));
            if (!t281) goto ie279;
            uint64_t t282 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t283 = ((t282 == ((uint64_t)0ULL)));
            if (!t283) goto ie279;
            {
                void * t285 = gm_l2;
                gm_tftb1odc739t5 t286 = gm_f_push_t83rg2m(t285, ((uint8_t)44ULL));
                if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 1, gm_srcp2qdr7uqsmui, 100, 52);
                gm_b0_1 t287 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 1));
            }
            goto ix279;
ie279:;
ix279:;
        }
        {
            void * t288 = gm_l2;
            gm_tftb1odc739t5 t289 = gm_f_push_t83rg2m(t288, ((uint8_t)41ULL));
            if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 1, gm_srcp2qdr7uqsmui, 101, 13);
            gm_b0_1 t290 = GM_LD(gm_b0_1, (((uint8_t *)&t289) + 1));
        }
        goto me8;
mn224:;
ma22:;
        uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t292 != ((uint8_t)13ULL)) goto mn291;
        gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l22 = t293;
        {
            void * t295 = gm_l2;
            gm_tftb1odc739t5 t296 = gm_f_push_t83rg2m(t295, ((uint8_t)63ULL));
            if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 1, gm_srcp2qdr7uqsmui, 104, 13);
            gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t296) + 1));
        }
        {
            void * t298 = gm_l0;
            gm_t88rcucg1b7uk t299 = gm_l22;
            gm_b0_1 t300 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t298, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn291:;
ma23:;
        uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t302 != ((uint8_t)14ULL)) goto mn301;
        gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 0));
        gm_l23 = t303;
        gm_t88rcucg1b7uk t304 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l24 = t304;
        {
            void * t306 = gm_l0;
            gm_t88rcucg1b7uk t307 = gm_l23;
            gm_b0_1 t308 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t306, t307);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t309 = gm_l2;
            gm_tftb1odc739t5 t310 = gm_f_push_t83rg2m(t309, ((uint8_t)33ULL));
            if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_srcp2qdr7uqsmui, 109, 13);
            gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
        }
        {
            void * t312 = gm_l0;
            gm_t88rcucg1b7uk t313 = gm_l24;
            gm_b0_1 t314 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t312, t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn301:;
ma24:;
        uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t316 != ((uint8_t)15ULL)) goto mn315;
        F.l25 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t7) + 8));
        void * t317 = gm_l0;
        gm_b16_8 t318;
        GM_ST(void *, (uint8_t *)&t318, (void *)(gm_s579fb7l5l6df));
        GM_ST(uintptr_t, (uint8_t *)&t318 + 8, (uintptr_t)(2));
        void * t319 = (void *)((uint8_t *)&F.l25);
        gm_b0_1 t320 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t317, t318, t319);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn315:;
ma25:;
        uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t322 != ((uint8_t)16ULL)) goto mn321;
        F.l26 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t7) + 8));
        void * t323 = gm_l0;
        gm_b16_8 t324;
        GM_ST(void *, (uint8_t *)&t324, (void *)(gm_s7mhpbh399e5s));
        GM_ST(uintptr_t, (uint8_t *)&t324 + 8, (uintptr_t)(3));
        void * t325 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t326 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t323, t324, t325);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn321:;
ma26:;
        uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t328 != ((uint8_t)17ULL)) goto mn327;
        uint32_t t329 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l27 = t329;
        gm_b16_8 t330 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 8));
        gm_l28 = t330;
        {
            void * t332 = gm_l0;
            void * t333 = GM_LD(void *, (((uint8_t *)t332) + 0));
            uint32_t t334 = gm_l27;
            gm_b16_8 t335 = gm_f_adt_name_h7mjik7(t333, t334);
            gm_l29 = t335;
        }
        {
            void * t336 = gm_l2;
            gm_b16_8 t337 = gm_l29;
            gm_tftb1odc739t5 t338 = gm_f_extend_0cs85gs(t336, t337);
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srcp2qdr7uqsmui, 116, 13);
            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
        }
        {
            uint64_t t341 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
            uint8_t t342 = ((t341 == ((uint64_t)1ULL)));
            if (!t342) goto ie340;
            gm_b16_8 t343 = gm_l29;
            gm_b16_8 t344;
            GM_ST(void *, (uint8_t *)&t344, (void *)(gm_sj21lfdl9q0t4));
            GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(5));
            uint8_t t345 = gm_f_starts_with_6vvt01e(t343, t344);
            if (!t345) goto ie340;
            {
                uint64_t t348 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t349 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                if (t348 >= (uintptr_t)(t349)) gm_trap_index(t348, t349, gm_srcp2qdr7uqsmui, 120, 32);
                uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t348 * 40) + 0));
                if (t350 != ((uint8_t)0ULL)) goto ie347;
                gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t348 * 40) + 8));
                gm_l30 = t351;
                void * t352 = gm_l0;
                void * t353 = GM_LD(void *, (((uint8_t *)t352) + 0));
                uint32_t t354 = gm_l27;
                uint32_t t355 = gm_f_template_9o92cvv(t353, t354);
                uint32_t t356 = gm_l27;
                uint8_t t357 = ((t355 == t356));
                if (!t357) goto ie347;
                {
                    void * t359 = gm_l0;
                    gm_t88rcucg1b7uk t360 = gm_l30;
                    gm_b0_1 t361 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t359, t360);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix347;
ie347:;
ix347:;
            }
            {
                gm_b0_1 t362;
                uint8_t t363 = 0;
                F.ret = t362;
                goto gm_fret;
            }
            goto ix340;
ie340:;
ix340:;
        }
        {
            void * t364 = gm_l0;
            gm_b16_8 t365 = gm_l28;
            gm_b0_1 t366 = gm_f_args_881ap90((void *)NULL, cx, NULL, t364, t365);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn327:;
ma27:;
        uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t368 != ((uint8_t)18ULL)) goto mn367;
        gm_t88rcucg1b7uk t369 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l31 = t369;
        {
            void * t371 = gm_l2;
            gm_b16_8 t372;
            GM_ST(void *, (uint8_t *)&t372, (void *)(gm_sfqmvjqk1sdmt));
            GM_ST(uintptr_t, (uint8_t *)&t372 + 8, (uintptr_t)(5));
            gm_tftb1odc739t5 t373 = gm_f_extend_0cs85gs(t371, t372);
            if (((((uint8_t *)&t373))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t373) + 1, gm_srcp2qdr7uqsmui, 126, 13);
            gm_b0_1 t374 = GM_LD(gm_b0_1, (((uint8_t *)&t373) + 1));
        }
        {
            void * t375 = gm_l0;
            gm_t88rcucg1b7uk t376 = gm_l31;
            gm_b0_1 t377 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t375, t376);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t378 = gm_l2;
            gm_tftb1odc739t5 t379 = gm_f_push_t83rg2m(t378, ((uint8_t)41ULL));
            if (((((uint8_t *)&t379))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t379) + 1, gm_srcp2qdr7uqsmui, 128, 13);
            gm_b0_1 t380 = GM_LD(gm_b0_1, (((uint8_t *)&t379) + 1));
        }
        goto me8;
mn367:;
ma28:;
        uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t382 != ((uint8_t)19ULL)) goto mn381;
        uint32_t t383 = GM_LD(uint32_t, (((uint8_t *)&t7) + 8));
        gm_l32 = t383;
        void * t384 = gm_l2;
        void * t385 = gm_l0;
        void * t386 = GM_LD(void *, (((uint8_t *)t385) + 0));
        uint32_t t387 = gm_l32;
        gm_b16_8 t388 = gm_f_param_name_rbnop5l(t386, t387);
        gm_tftb1odc739t5 t389 = gm_f_extend_0cs85gs(t384, t388);
        if (((((uint8_t *)&t389))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t389) + 1, gm_srcp2qdr7uqsmui, 130, 23);
        gm_b0_1 t390 = GM_LD(gm_b0_1, (((uint8_t *)&t389) + 1));
        goto me8;
mn381:;
ma29:;
        uint8_t t392 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t392 != ((uint8_t)20ULL)) goto mn391;
        gm_t88rcucg1b7uk t393 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 0));
        gm_l33 = t393;
        gm_t88rcucg1b7uk t394 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l34 = t394;
        {
            void * t396 = gm_l2;
            gm_tftb1odc739t5 t397 = gm_f_push_t83rg2m(t396, ((uint8_t)91ULL));
            if (((((uint8_t *)&t397))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t397) + 1, gm_srcp2qdr7uqsmui, 132, 13);
            gm_b0_1 t398 = GM_LD(gm_b0_1, (((uint8_t *)&t397) + 1));
        }
        {
            void * t399 = gm_l0;
            gm_t88rcucg1b7uk t400 = gm_l33;
            gm_b0_1 t401 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t402 = gm_l2;
            gm_tftb1odc739t5 t403 = gm_f_push_t83rg2m(t402, ((uint8_t)93ULL));
            if (((((uint8_t *)&t403))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t403) + 1, gm_srcp2qdr7uqsmui, 134, 13);
            gm_b0_1 t404 = GM_LD(gm_b0_1, (((uint8_t *)&t403) + 1));
        }
        {
            void * t405 = gm_l0;
            gm_t88rcucg1b7uk t406 = gm_l34;
            gm_b0_1 t407 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t405, t406);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me8;
mn391:;
ma30:;
        uint8_t t409 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t409 != ((uint8_t)22ULL)) goto mn408;
        gm_t9qkh0rsooc8d t410 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t7) + 8) + 0));
        gm_l35 = t410;
        gm_t88rcucg1b7uk t411 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l36 = t411;
        gm_t88rcucg1b7uk t412 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 8));
        gm_l37 = t412;
        {
        }
        {
            {
                {
                    gm_t88rcucg1b7uk t416 = gm_l36;
                    gm_t88rcucg1b7uk t417 = gm_l37;
                    gm_tqsk8dt6ib3qq t418;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t418 + 0), t416);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t418 + 4), t417);
                    gm_l38 = t418;
                }
                void * t419 = ((uint8_t *)&gm_l38);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcp2qdr7uqsmui, 138, 35);
                gm_b16_8 t420;
                GM_ST(void *, (uint8_t *)&t420, (void *)(((uint8_t *)t419 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t421 = t420;
                gm_l39 = t421;
            }
            {
                gm_l40 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t423 = gm_l40;
                    uint64_t t424 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                    uint8_t t425 = (t423 < t424);
                    if (!t425) goto lb422;
                    {
                        uint64_t t428 = gm_l40;
                        uint64_t t429 = gm_l40;
                        uint64_t t430 = (uintptr_t)t429;
                        uint64_t t431 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        gm_t88rcucg1b7uk t432 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t430 * 4));
                        gm_tlcm4lvu3g1a8 t433;
                        GM_ST(uint32_t, (uint8_t *)&t433 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t433 + 0), t428);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t433 + 8), t432);
                        uint64_t t436 = GM_LD(uint64_t, (((uint8_t *)&t433) + 0));
                        gm_l41 = t436;
                        gm_t88rcucg1b7uk t437 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t433) + 8));
                        gm_l42 = t437;
                        {
                            uint64_t t440 = gm_l41;
                            uint8_t t441 = ((t440 == ((uint64_t)1ULL)));
                            if (!t441) goto ie439;
                            {
                                void * t443 = gm_l2;
                                gm_t9qkh0rsooc8d t444 = gm_l35;
                                gm_b16_8 t445 = gm_f_len_op_symbol_2qoiciu(t444);
                                gm_tnpifv20irnph t446;
                                GM_ST(gm_b16_8, ((uint8_t *)&t446 + 0), t445);
                                gm_tftb1odc739t5 t447 = gm_f_write_m28h1lv(t443, t446);
                                if (((((uint8_t *)&t447))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t447) + 1, gm_srcp2qdr7uqsmui, 139, 29);
                                gm_b0_1 t448 = GM_LD(gm_b0_1, (((uint8_t *)&t447) + 1));
                            }
                            goto ix439;
ie439:;
ix439:;
                        }
                        {
                            void * t450 = gm_l0;
                            void * t451 = GM_LD(void *, (((uint8_t *)t450) + 0));
                            gm_t88rcucg1b7uk t452 = gm_l42;
                            gm_t53at80pfdus3 t453;
                            gm_f_kind_jcnt4hm((void *)&t453, t451, GM_CONV(gm_csguankko4lmhd, t452));
                            uint8_t t454;
                            uint8_t t457 = GM_LD(uint8_t, (((uint8_t *)&t453) + 0));
                            if (t457 != ((uint8_t)22ULL)) goto mn456;
                            t454 = ((uint8_t)1);
                            goto me455;
mn456:;
                            t454 = ((uint8_t)0);
                            goto me455;
mn458:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 140, 20);
me455:;
                            if (!t454) goto ie449;
                            {
                                void * t460 = gm_l2;
                                gm_tftb1odc739t5 t461 = gm_f_push_t83rg2m(t460, ((uint8_t)40ULL));
                                if (((((uint8_t *)&t461))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t461) + 1, gm_srcp2qdr7uqsmui, 141, 21);
                                gm_b0_1 t462 = GM_LD(gm_b0_1, (((uint8_t *)&t461) + 1));
                            }
                            {
                                void * t463 = gm_l0;
                                gm_t88rcucg1b7uk t464 = gm_l42;
                                gm_b0_1 t465 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t463, t464);
                                if (gm_unw.kind) {
                                    goto gm_ll426;
                                }
                            }
                            {
                                void * t466 = gm_l2;
                                gm_tftb1odc739t5 t467 = gm_f_push_t83rg2m(t466, ((uint8_t)41ULL));
                                if (((((uint8_t *)&t467))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t467) + 1, gm_srcp2qdr7uqsmui, 143, 21);
                                gm_b0_1 t468 = GM_LD(gm_b0_1, (((uint8_t *)&t467) + 1));
                            }
                            goto ix449;
ie449:;
                            {
                                void * t470 = gm_l0;
                                gm_t88rcucg1b7uk t471 = gm_l42;
                                gm_b0_1 t472 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t470, t471);
                                if (gm_unw.kind) {
                                    goto gm_ll426;
                                }
                            }
ix449:;
                        }
                        goto me434;
mn435:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 138, 13);
me434:;
                    }
lc422:;
                    {
                        uint64_t t474 = gm_l40;
                        uint64_t r475; bool o476 = __builtin_add_overflow(t474, ((uint64_t)1ULL), &r475);
                        uint64_t t477 = r475;
                        gm_l40 = t477;
                    }
                }
gm_ll426:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb422; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc422; }
                }
                goto gm_lad;
lb422:;
            }
        }
        goto me8;
mn408:;
ma31:;
        uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t479 != ((uint8_t)21ULL)) goto mn478;
        gm_l43 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t7) + 8));
        void * t480 = gm_l0;
        void * t481 = (void *)((uint8_t *)&gm_l43);
        gm_b0_1 t482 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t480, t481);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn478:;
ma32:;
        uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t484 != ((uint8_t)24ULL)) goto mn483;
        gm_tnpifv20irnph t485 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t7) + 8));
        gm_l44 = t485;
        void * t486 = gm_l0;
        void * t487 = (void *)((uint8_t *)&gm_l44);
        gm_b0_1 t488 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t486, t487);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn483:;
ma33:;
        uint8_t t490 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t490 != ((uint8_t)25ULL)) goto mn489;
        void * t491 = gm_l2;
        gm_b16_8 t492;
        GM_ST(void *, (uint8_t *)&t492, (void *)(gm_sic5vsema0r36));
        GM_ST(uintptr_t, (uint8_t *)&t492 + 8, (uintptr_t)(7));
        gm_tftb1odc739t5 t493 = gm_f_extend_0cs85gs(t491, t492);
        if (((((uint8_t *)&t493))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t493) + 1, gm_srcp2qdr7uqsmui, 151, 24);
        gm_b0_1 t494 = GM_LD(gm_b0_1, (((uint8_t *)&t493) + 1));
        goto me8;
mn489:;
ma34:;
        uint8_t t496 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t496 != ((uint8_t)23ULL)) goto mn495;
        void * t497 = gm_l2;
        gm_b16_8 t498;
        GM_ST(void *, (uint8_t *)&t498, (void *)(gm_s4imhc4v1tf2p));
        GM_ST(uintptr_t, (uint8_t *)&t498 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t499 = gm_f_extend_0cs85gs(t497, t498);
        if (((((uint8_t *)&t499))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t499) + 1, gm_srcp2qdr7uqsmui, 152, 22);
        gm_b0_1 t500 = GM_LD(gm_b0_1, (((uint8_t *)&t499) + 1));
        goto me8;
mn495:;
ma35:;
        uint8_t t502 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t502 != ((uint8_t)26ULL)) goto mn501;
        void * t503 = gm_l2;
        gm_b16_8 t504;
        GM_ST(void *, (uint8_t *)&t504, (void *)(gm_scp1b2sk2nlmr));
        GM_ST(uintptr_t, (uint8_t *)&t504 + 8, (uintptr_t)(7));
        gm_tftb1odc739t5 t505 = gm_f_extend_0cs85gs(t503, t504);
        if (((((uint8_t *)&t505))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t505) + 1, gm_srcp2qdr7uqsmui, 153, 19);
        gm_b0_1 t506 = GM_LD(gm_b0_1, (((uint8_t *)&t505) + 1));
        goto me8;
mn501:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 55, 5);
me8:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_ref_6sjtehp F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 0));
        uint8_t t5 = ((t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l1;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie2;
        {
            uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
            void * t12 = gm_l1;
            uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 8) + 8);
            if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcp2qdr7uqsmui, 190, 24);
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 8)) + t11 * 40) + 0));
            if (t14 != ((uint8_t)0ULL)) goto ie10;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 8)) + t11 * 40) + 8));
            gm_l2 = t15;
            {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l2;
                gm_b0_1 t19 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t20;
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 8));
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l1;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 0));
        gm_b16_8 t28 = gm_f_effect_name_prpp0ip(t25, t27);
        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t23, t28);
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcp2qdr7uqsmui, 195, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
        gm_b0_1 t34 = gm_f_args_881ap90((void *)NULL, cx, NULL, t31, t33);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effects_mg3vf2a F;
    void * gm_l0; /* p */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_tiilmfd16v3fc gm_l5; /* e */
    GM_STACK_CHECK(&F, 312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 8));
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sbvutvpa8pe3d));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcp2qdr7uqsmui, 179, 9);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        {
            gm_b0_1 t12;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t15 = gm_l1;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 0));
            gm_l2 = t16;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l3;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    gm_t4f5fr60fo50r t23;
                    uint64_t t24 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t23 + 0, t24);
                    uint64_t t25 = gm_l3;
                    uint64_t t26 = (uintptr_t)t25;
                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t23 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t26 * 24), 24);
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t23) + 0));
                    gm_l4 = t30;
                    gm_l5 = (*(gm_tiilmfd16v3fc *)(((uint8_t *)&t23) + 8));
                    {
                        uint64_t t33 = gm_l4;
                        uint8_t t34 = (t33 > ((uint64_t)0ULL));
                        if (!t34) goto ie32;
                        {
                            void * t36 = gm_l0;
                            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 8));
                            gm_b16_8 t38;
                            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s0m0qaokk51g0));
                            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
                            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcp2qdr7uqsmui, 183, 20);
                            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    {
                        void * t41 = gm_l0;
                        void * t42 = (void *)((uint8_t *)&gm_l5);
                        gm_b0_1 t43 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t41, t42);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 182, 5);
me28:;
                }
lc17:;
                {
                    uint64_t t45 = gm_l3;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l3 = t48;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_set_3vntabf F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tnpifv20irnph gm_l3; /* set */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
        gm_l2 = t4;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            gm_t88rcucg1b7uk t9 = gm_l2;
            gm_t53at80pfdus3 t10;
            gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
            if (t11 != ((uint8_t)24ULL)) goto ie6;
            gm_tnpifv20irnph t12 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t10) + 8));
            gm_l3 = t12;
            {
                uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8);
                uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            goto ix6;
ie6:;
            {
                uint8_t t18 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix6:;
        }
        goto ix2;
ie2:;
        {
            uint8_t t20 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1) { return gm_f_is_empty_set_3vntabf(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_key_fr74f0n F;
    void * gm_l0; /* p */
    void * gm_l1; /* k */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* b */
    uint8_t gm_l7; /* signed */
    uint16_t gm_l8; /* bits */
    uint64_t gm_l9; /* raw */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint8_t gm_l13; /* c */
    GM_STACK_CHECK(&F, 872u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l3 = t14;
        void * t15 = gm_l2;
        gm_b16_8 t16 = gm_l3;
        gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcp2qdr7uqsmui, 225, 25);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        goto me5;
mn12:;
ma7:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t20 != ((uint8_t)2ULL)) goto mn19;
        void * t21 = gm_l2;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcp2qdr7uqsmui, 226, 18);
        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        goto me5;
mn19:;
ma8:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t26 != ((uint8_t)3ULL)) goto mn25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 8));
        gm_l4 = t27;
        gm_b16_8 t28 = gm_l4;
        void * t29 = gm_l2;
        gm_b0_1 t30 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn25:;
ma9:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t32 != ((uint8_t)0ULL)) goto mn31;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t4) + 8) + 0));
        gm_l5 = t33;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t34;
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            gm_t88rcucg1b7uk t38 = gm_l5;
            gm_t53at80pfdus3 t39;
            gm_f_kind_jcnt4hm((void *)&t39, t37, GM_CONV(gm_csguankko4lmhd, t38));
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            switch (t48) { case ((uint8_t)0ULL): goto ma41; case ((uint8_t)1ULL): goto ma42; case ((uint8_t)2ULL): goto ma43; case ((uint8_t)3ULL): goto ma45; case ((uint8_t)4ULL): goto ma44; case ((uint8_t)5ULL): goto ma46; case ((uint8_t)6ULL): goto ma46; case ((uint8_t)7ULL): goto ma46; case ((uint8_t)8ULL): goto ma46; case ((uint8_t)9ULL): goto ma46; case ((uint8_t)10ULL): goto ma46; case ((uint8_t)11ULL): goto ma46; case ((uint8_t)12ULL): goto ma46; case ((uint8_t)13ULL): goto ma46; case ((uint8_t)14ULL): goto ma46; case ((uint8_t)15ULL): goto ma46; case ((uint8_t)16ULL): goto ma46; case ((uint8_t)17ULL): goto ma46; case ((uint8_t)18ULL): goto ma46; case ((uint8_t)19ULL): goto ma46; case ((uint8_t)20ULL): goto ma46; case ((uint8_t)21ULL): goto ma46; case ((uint8_t)22ULL): goto ma46; case ((uint8_t)23ULL): goto ma46; case ((uint8_t)24ULL): goto ma46; case ((uint8_t)25ULL): goto ma46; case ((uint8_t)26ULL): goto ma46; default: goto ma46; }
ma41:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t50 != ((uint8_t)0ULL)) goto mn49;
            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&t39) + 8) + 0));
            gm_l7 = t51;
            gm_b16_8 t52 = gm_l6;
            uint8_t t53 = gm_l7;
            void * t54 = gm_l2;
            gm_b0_1 t55 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t52, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me40;
mn49:;
ma42:;
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t57 != ((uint8_t)1ULL)) goto mn56;
            gm_b16_8 t58 = gm_l6;
            void * t59 = gm_l2;
            gm_b0_1 t60 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t58, ((uint8_t)0), t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me40;
mn56:;
ma43:;
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t62 != ((uint8_t)2ULL)) goto mn61;
            gm_b16_8 t63 = gm_l6;
            void * t64 = gm_l2;
            gm_b0_1 t65 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t63, ((uint8_t)1), t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me40;
mn61:;
ma44:;
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t67 != ((uint8_t)4ULL)) goto mn66;
            void * t68 = gm_l2;
            gm_b16_8 t69;
            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t72 = (t71 > ((uint64_t)0ULL));
            if (!t72) goto ie70;
            uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t73 >= (uintptr_t)(t74)) gm_trap_index(t73, t74, gm_srcp2qdr7uqsmui, 233, 54);
            uint8_t t75 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t73 * 1));
            uint8_t t76 = ((t75 == ((uint8_t)1ULL)));
            if (!t76) goto ie70;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(4));
            gm_b16_8 t79 = t78;
            t69 = t79;
            goto ix70;
ie70:;
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(5));
            gm_b16_8 t82 = t81;
            t69 = t82;
ix70:;
            gm_tftb1odc739t5 t83 = gm_f_extend_0cs85gs(t68, t69);
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcp2qdr7uqsmui, 233, 26);
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
            goto me40;
mn66:;
ma45:;
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t86 != ((uint8_t)3ULL)) goto mn85;
            uint16_t t87 = GM_LD(uint16_t, (((uint8_t *)&t39) + 8));
            gm_l8 = t87;
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_b16_8 t90 = gm_l6;
                    gm_l10 = t90;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t92 = gm_l11;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t94 = (t92 < t93);
                        if (!t94) goto lb91;
                        {
                            uint64_t t97 = gm_l11;
                            uint64_t t98 = gm_l11;
                            uint64_t t99 = (uintptr_t)t98;
                            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            uint8_t t101 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t99 * 1));
                            gm_tf5srh4mp4b3i t102;
                            GM_ST(uint8_t, (uint8_t *)&t102 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t102 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t102 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t102 + 0), t97);
                            GM_ST(uint8_t, ((uint8_t *)&t102 + 8), t101);
                            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&t102) + 0));
                            gm_l12 = t105;
                            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&t102) + 8));
                            gm_l13 = t106;
                            {
                                uint64_t t108 = gm_l9;
                                uint8_t t109 = gm_l13;
                                uint64_t t110 = (uint64_t)t109;
                                uint64_t t111 = gm_l12;
                                uint64_t r112; bool o113 = __builtin_mul_overflow(t111, ((uint64_t)8ULL), &r112);
                                if (o113) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 236, 69);
                                uint64_t t114 = r112;
                                uint64_t t115 = (uint64_t)t114;
                                unsigned __int128 n116 = (unsigned __int128)t115;
                                if (n116 >= 64) gm_trap_shift((uint64_t)n116, "u64", gm_srcp2qdr7uqsmui, 236, 55);
                                uint64_t r117 = (uint64_t)((uint64_t)t110 << (int)n116);
                                bool o118 = (uint64_t)(r117 >> (int)n116) != t110;
                                if (o118) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 236, 55);
                                uint64_t t119 = r117;
                                uint64_t t120 = (uint64_t)(t108 | t119);
                                gm_l9 = t120;
                            }
                            goto me103;
mn104:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 236, 21);
me103:;
                        }
lc91:;
                        {
                            uint64_t t122 = gm_l11;
                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                            uint64_t t125 = r123;
                            gm_l11 = t125;
                        }
                    }
lb91:;
                }
            }
            {
                uint16_t t127 = gm_l8;
                uint8_t t128 = ((t127 == ((uint16_t)64ULL)));
                if (!t128) goto ie126;
                uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t130 = ((t129 == ((uint64_t)8ULL)));
                if (!t130) goto ie126;
                {
                    uint64_t t132 = gm_l9;
                    double t133;
                    memset(&t133, 0, sizeof t133); memcpy(&t133, ((uint8_t *)&t132), sizeof t133 < 8 ? sizeof t133 : 8);
                    void * t134 = gm_l2;
                    gm_b0_1 t135 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t133, t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix126;
ie126:;
                uint16_t t137 = gm_l8;
                uint8_t t138 = ((t137 == ((uint16_t)32ULL)));
                if (!t138) goto ie136;
                uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t140 = ((t139 == ((uint64_t)4ULL)));
                if (!t140) goto ie136;
                {
                    uint64_t t142 = gm_l9;
                    uint32_t t143 = (uint32_t)t142;
                    float t144;
                    memset(&t144, 0, sizeof t144); memcpy(&t144, ((uint8_t *)&t143), sizeof t144 < 4 ? sizeof t144 : 4);
                    double t145 = (double)t144;
                    void * t146 = gm_l2;
                    gm_b0_1 t147 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t145, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix136;
ie136:;
                {
                    void * t149 = gm_l0;
                    gm_t88rcucg1b7uk t150 = gm_l5;
                    gm_b0_1 t151 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t149, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t152 = gm_l2;
                    gm_b16_8 t153;
                    GM_ST(void *, (uint8_t *)&t153, (void *)(gm_s15tjb1oda5hu));
                    GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t154 = gm_f_extend_0cs85gs(t152, t153);
                    if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srcp2qdr7uqsmui, 243, 25);
                    gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
                }
ix136:;
ix126:;
            }
            goto me40;
mn85:;
ma46:;
            {
                void * t158 = gm_l0;
                gm_t88rcucg1b7uk t159 = gm_l5;
                gm_b0_1 t160 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t158, t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t161 = gm_l2;
                gm_b16_8 t162;
                GM_ST(void *, (uint8_t *)&t162, (void *)(gm_s15tjb1oda5hu));
                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(5));
                gm_tftb1odc739t5 t163 = gm_f_extend_0cs85gs(t161, t162);
                if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srcp2qdr7uqsmui, 248, 21);
                gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
            }
            goto me40;
mn156:;
ma47:;
            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 229, 13);
me40:;
        }
        goto me5;
mn31:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 224, 5);
me5:;
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
inline __attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_exponent_4uekfcv F;
    gm_b16_8 gm_l0; /* t */
    uint8_t gm_l1; /* negative */
    int64_t gm_l2; /* v */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (t4) {
            uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srcp2qdr7uqsmui, 303, 31);
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t5 * 1));
            uint8_t t8 = ((t7 == ((uint8_t)45ULL)));
            t2 = t8;
        } else t2 = 0;
        gm_l1 = t2;
    }
    {
        gm_l2 = ((int64_t)0LL);
    }
    {
        {
            gm_b16_8 t10 = gm_l0;
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
                    uint64_t t17 = gm_l4;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t18 * 1));
                    uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t20));
                    gm_l5 = t23;
                    {
                        uint8_t t26 = gm_l5;
                        uint8_t t27 = (t26 >= ((uint8_t)48ULL));
                        if (!t27) goto ie25;
                        uint8_t t28 = gm_l5;
                        uint8_t t29 = (t28 <= ((uint8_t)57ULL));
                        if (!t29) goto ie25;
                        {
                            int64_t t31 = gm_l2;
                            int64_t r32; bool o33 = __builtin_mul_overflow(t31, ((int64_t)10LL), &r32);
                            if (o33) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 306, 40);
                            int64_t t34 = r32;
                            uint8_t t35 = gm_l5;
                            uint8_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint8_t)48ULL), &r36);
                            if (o37) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 306, 53);
                            uint8_t t38 = r36;
                            int64_t t39 = (int64_t)t38;
                            int64_t r40; bool o41 = __builtin_add_overflow(t34, t39, &r40);
                            if (o41) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 306, 40);
                            int64_t t42 = r40;
                            gm_l2 = t42;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 305, 5);
me21:;
                }
lc11:;
                {
                    uint64_t t44 = gm_l4;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l4 = t47;
                }
            }
lb11:;
        }
    }
    {
        int64_t t48;
        uint8_t t50 = gm_l1;
        if (!t50) goto ie49;
        int64_t t52 = gm_l2;
        if (t52 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 308, 26);
        int64_t t53 = (int64_t)((uint64_t)0 - (uint64_t)t52);
        int64_t t54 = t53;
        t48 = t54;
        goto ix49;
ie49:;
        int64_t t56 = gm_l2;
        int64_t t57 = t56;
        t48 = t57;
ix49:;
        uint8_t t58 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1) { gm_tmm67r2c94d5j gm_r; gm_f_printer_h7jigd7(&gm_r, a0, a1); return GM_CONV(gm_cs32e03j420uj1, gm_r); }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_sig_uguln8a F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* head */
    void * gm_l2; /* sig */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 490u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l2;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 40));
        if (!t6) goto ie4;
        {
            void * t8 = gm_l3;
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sb7t8ia5pj1l5));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcp2qdr7uqsmui, 159, 20);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t12 = gm_l3;
        gm_b16_8 t13 = gm_l1;
        gm_tnpifv20irnph t14;
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
        gm_tftb1odc739t5 t15 = gm_f_write_e9824eu(t12, t14);
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcp2qdr7uqsmui, 160, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        {
            void * t18 = gm_l2;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 0));
            gm_l4 = t19;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t21 = gm_l5;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t23 = (t21 < t22);
                if (!t23) goto lb20;
                {
                    uint64_t t26 = gm_l5;
                    uint64_t t27 = gm_l5;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t28 * 4));
                    gm_tlcm4lvu3g1a8 t31;
                    GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t26);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31 + 8), t30);
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
                    gm_l6 = t34;
                    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t31) + 8));
                    gm_l7 = t35;
                    {
                        uint64_t t38 = gm_l6;
                        uint8_t t39 = (t38 > ((uint64_t)0ULL));
                        if (!t39) goto ie37;
                        {
                            void * t41 = gm_l3;
                            gm_b16_8 t42;
                            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t43 = gm_f_extend_0cs85gs(t41, t42);
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcp2qdr7uqsmui, 162, 20);
                            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        void * t45 = gm_l0;
                        gm_t88rcucg1b7uk t46 = gm_l7;
                        gm_b0_1 t47 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll24;
                        }
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 161, 5);
me32:;
                }
lc20:;
                {
                    uint64_t t49 = gm_l5;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l5 = t52;
                }
            }
gm_ll24:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
            }
            goto gm_lad;
lb20:;
        }
    }
    {
        void * t53 = gm_l3;
        gm_tftb1odc739t5 t54 = gm_f_push_t83rg2m(t53, ((uint8_t)41ULL));
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcp2qdr7uqsmui, 165, 5);
        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
    }
    {
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
        void * t59 = gm_l2;
        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + 16));
        uint8_t t61 = gm_f_is_unit_n83ttnb(t58, GM_CONV(gm_csguankko4lmhd, t60));
        uint8_t t62 = (uint8_t)!t61;
        if (!t62) goto ie56;
        {
            void * t64 = gm_l3;
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sdbvaa960ee0f));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t66 = gm_f_extend_0cs85gs(t64, t65);
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcp2qdr7uqsmui, 167, 9);
            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
        }
        {
            void * t68 = gm_l0;
            void * t69 = gm_l2;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 16));
            gm_b0_1 t71 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t68, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        void * t73 = gm_l2;
        uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 24) + 0) + 8);
        uint8_t t75 = (t74 > ((uint64_t)0ULL));
        if (!t75) goto ie72;
        {
            void * t77 = gm_l3;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s5j8t1cg2erp9));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(7));
            gm_tftb1odc739t5 t79 = gm_f_extend_0cs85gs(t77, t78);
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srcp2qdr7uqsmui, 171, 9);
            gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        }
        {
            void * t81 = gm_l0;
            void * t82 = gm_l2;
            void * t83 = (void *)(((uint8_t *)t82) + 24);
            gm_b0_1 t84 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t81, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix72;
ie72:;
ix72:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ty_vka2r8h F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* left */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2;
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 16));
        uint8_t t6 = ((t5 == ((uint32_t)0ULL)));
        if (!t6) goto ie3;
        uint32_t t8 = ((uint32_t)300ULL);
        t2 = t8;
        goto ix3;
ie3:;
        void * t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 20));
        uint32_t t12 = t11;
        t2 = t12;
ix3:;
        gm_l2 = t2;
    }
    {
        uint32_t t14 = gm_l2;
        uint8_t t15 = ((t14 == ((uint32_t)0ULL)));
        if (!t15) goto ie13;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_soqt3jnmovq5l));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcp2qdr7uqsmui, 44, 9);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        {
            gm_b0_1 t22;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l0;
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 16));
        uint32_t r27; bool o28 = __builtin_add_overflow(t26, ((uint32_t)1ULL), &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 47, 5);
        uint32_t t29 = r27;
        GM_ST(uint32_t, (((uint8_t *)t24) + 16), t29);
    }
    {
        void * t30 = gm_l0;
        uint32_t t31 = gm_l2;
        uint32_t r32; bool o33 = __builtin_sub_overflow(t31, ((uint32_t)1ULL), &r32);
        if (o33) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 48, 14);
        uint32_t t34 = r32;
        GM_ST(uint32_t, (((uint8_t *)t30) + 20), t34);
    }
    {
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l1;
        gm_b0_1 t37 = gm_f_components_dvsj2j9((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)t39) + 16));
        uint32_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint32_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 50, 5);
        uint32_t t43 = r41;
        GM_ST(uint32_t, (((uint8_t *)t38) + 16), t43);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_write_debug_string_pns0400 F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srcp2qdr7uqsmui, 340, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
    }
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 1));
                    uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t16));
                    gm_l4 = t19;
                    {
                        uint8_t t21 = gm_l4;
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t24 == ((uint8_t)34ULL))) goto mn23;
                        void * t25 = gm_l1;
                        gm_b16_8 t26;
                        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t25, t26);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcp2qdr7uqsmui, 343, 20);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        goto me22;
mn23:;
                        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t30 == ((uint8_t)92ULL))) goto mn29;
                        void * t31 = gm_l1;
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcp2qdr7uqsmui, 344, 21);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                        goto me22;
mn29:;
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t36 == ((uint8_t)10ULL))) goto mn35;
                        void * t37 = gm_l1;
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcp2qdr7uqsmui, 345, 21);
                        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                        goto me22;
mn35:;
                        uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t42 == ((uint8_t)13ULL))) goto mn41;
                        void * t43 = gm_l1;
                        gm_b16_8 t44;
                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t45 = gm_f_extend_0cs85gs(t43, t44);
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcp2qdr7uqsmui, 346, 21);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                        goto me22;
mn41:;
                        uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t48 == ((uint8_t)9ULL))) goto mn47;
                        void * t49 = gm_l1;
                        gm_b16_8 t50;
                        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcp2qdr7uqsmui, 347, 21);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                        goto me22;
mn47:;
                        uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t54 == ((uint8_t)0ULL))) goto mn53;
                        void * t55 = gm_l1;
                        gm_b16_8 t56;
                        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s2v5amvfkfoll));
                        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcp2qdr7uqsmui, 348, 18);
                        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        goto me22;
mn53:;
                        {
                            uint8_t t62 = 0;
                            uint8_t t63 = gm_l4;
                            uint8_t t64 = (t63 < ((uint8_t)32ULL));
                            if (!t64) {
                                uint8_t t65 = gm_l4;
                                uint8_t t66 = ((t65 == ((uint8_t)127ULL)));
                                t62 = t66;
                            } else t62 = 1;
                            if (!t62) goto ie61;
                            {
                                void * t68 = gm_l1;
                                uint8_t t69 = gm_l4;
                                gm_t9qkh0rsooc8d t70;
                                GM_ST(uint8_t, ((uint8_t *)&t70 + 0), t69);
                                gm_tftb1odc739t5 t71 = gm_f_write_kki8q0t(t68, t70);
                                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcp2qdr7uqsmui, 351, 21);
                                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                            }
                            goto ix61;
ie61:;
                            {
                                void * t74 = gm_l1;
                                uint8_t t75 = gm_l4;
                                gm_tftb1odc739t5 t76 = gm_f_push_t83rg2m(t74, t75);
                                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcp2qdr7uqsmui, 353, 21);
                                gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                            }
ix61:;
                        }
                        goto me22;
mn59:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 342, 9);
me22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 341, 5);
me17:;
                }
lc7:;
                {
                    uint64_t t79 = gm_l3;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l3 = t82;
                }
            }
lb7:;
        }
    }
    {
        void * t83 = gm_l1;
        gm_tftb1odc739t5 t84 = gm_f_push_t83rg2m(t83, ((uint8_t)34ULL));
        if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srcp2qdr7uqsmui, 358, 5);
        gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1) {
    struct gm_fr_write_float_7p0icl6 F;
    double gm_l0; /* x */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* mantissa */
    int64_t gm_l5; /* exp */
    uint8_t gm_l6; /* negative */
    int64_t gm_l8; /* point */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint8_t gm_l12; /* c */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* k */
    GM_STACK_CHECK(&F, 1403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t3 = gm_l0;
        double t4 = gm_l0;
        double t5 = (t3 - t4);
        uint8_t t6 = (!(t5 == ((double)0e0)));
        if (!t6) goto ie2;
        double t7 = gm_l0;
        double t8 = gm_l0;
        uint8_t t9 = ((t7 == t8));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l1;
            gm_b16_8 t12;
            double t14 = gm_l0;
            uint8_t t15 = (t14 < ((double)0e0));
            if (!t15) goto ie13;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_segch6a5dme9u));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            gm_b16_8 t18 = t17;
            t12 = t18;
            goto ix13;
ie13:;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_b16_8 t21 = t20;
            t12 = t21;
ix13:;
            gm_tftb1odc739t5 t22 = gm_f_extend_0cs85gs(t11, t12);
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcp2qdr7uqsmui, 258, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t27 = gm_l0;
        double t28 = gm_l0;
        uint8_t t29 = (!(t27 == t28));
        if (!t29) goto ie26;
        {
            void * t31 = gm_l1;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s0lrfrtvffhu7));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcp2qdr7uqsmui, 262, 9);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            gm_b0_1 t35;
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        double t37 = gm_l0;
        gm_tidu2er0d7h95 t38;
        GM_ST(double, ((uint8_t *)&t38 + 0), t37);
        gm_tabb710d9mq2e t39;
        gm_f_format_gu67n99((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcp2qdr7uqsmui, 265, 13);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_l2 = t40;
    }
    {
        gm_b16_8 t41 = gm_l2;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_tf5srh4mp4b3i t43 = gm_f_index_of_hdeg351(t42, ((uint8_t)101ULL));
        uint64_t t44;
        if (((((uint8_t *)&t43))[8] != 0)) {
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t43) + 0));
            t44 = t45;
        } else {
            {
                void * t47 = gm_l1;
                gm_b16_8 t48 = gm_l2;
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t47, t49);
                if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srcp2qdr7uqsmui, 267, 9);
                gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
            }
            gm_b0_1 t52;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        gm_l3 = t44;
    }
    {
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t55 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t56 = gm_l3;
        uint64_t t57 = (uintptr_t)t56;
        if ((uintptr_t)0 > t57 || t57 > (uintptr_t)t54) gm_trap_slice((uintptr_t)0, t57, t54, gm_srcp2qdr7uqsmui, 271, 17);
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t55 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(t57 - (uintptr_t)0));
        gm_l4 = t58;
    }
    {
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t61 = gm_l3;
        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 272, 32);
        uint64_t t64 = r62;
        uint64_t t65 = (uintptr_t)t64;
        if (t65 > (uintptr_t)t59 || (uintptr_t)t59 > (uintptr_t)t59) gm_trap_slice(t65, (uintptr_t)t59, t59, gm_srcp2qdr7uqsmui, 272, 27);
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t60 + t65 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)((uintptr_t)t59 - t65));
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        int64_t t68 = gm_f_parse_exponent_4uekfcv(t67);
        gm_l5 = t68;
    }
    {
        uint8_t t69 = 0;
        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t71 = (t70 > ((uint64_t)0ULL));
        if (t71) {
            uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcp2qdr7uqsmui, 273, 38);
            uint8_t t74 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t72 * 1));
            uint8_t t75 = ((t74 == ((uint8_t)45ULL)));
            t69 = t75;
        } else t69 = 0;
        gm_l6 = t69;
    }
    {
        uint8_t t77 = gm_l6;
        if (!t77) goto ie76;
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t81 = (uintptr_t)((uint64_t)1ULL);
            if (t81 > (uintptr_t)t79 || (uintptr_t)t79 > (uintptr_t)t79) gm_trap_slice(t81, (uintptr_t)t79, t79, gm_srcp2qdr7uqsmui, 274, 30);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t80 + t81 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)((uintptr_t)t79 - t81));
            gm_l4 = t82;
        }
        goto ix76;
ie76:;
ix76:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        int64_t t84 = (int64_t)t83;
        gm_l8 = t84;
    }
    {
        {
            gm_b16_8 t86 = gm_l4;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_l9 = t87;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t89 = gm_l10;
                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t91 = (t89 < t90);
                if (!t91) goto lb88;
                {
                    uint64_t t94 = gm_l10;
                    uint64_t t95 = gm_l10;
                    uint64_t t96 = (uintptr_t)t95;
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t96 * 1));
                    gm_tf5srh4mp4b3i t99;
                    GM_ST(uint8_t, (uint8_t *)&t99 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t99 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t94);
                    GM_ST(uint8_t, ((uint8_t *)&t99 + 8), t98);
                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&t99) + 0));
                    gm_l11 = t102;
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t99) + 8));
                    gm_l12 = t103;
                    {
                        uint8_t t106 = gm_l12;
                        uint8_t t107 = ((t106 == ((uint8_t)46ULL)));
                        if (!t107) goto ie105;
                        {
                            uint64_t t109 = gm_l11;
                            int64_t t110 = (int64_t)t109;
                            gm_l8 = t110;
                        }
                        goto ix105;
ie105:;
                        {
                            void * t112 = (void *)((uint8_t *)&F.l7);
                            uint8_t t113 = gm_l12;
                            gm_tftb1odc739t5 t114 = gm_f_push_t83rg2m(t112, t113);
                            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcp2qdr7uqsmui, 281, 13);
                            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                        }
ix105:;
                    }
                    goto me100;
mn101:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 277, 5);
me100:;
                }
lc88:;
                {
                    uint64_t t117 = gm_l10;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l10 = t120;
                }
            }
lb88:;
        }
    }
    {
        int64_t t121 = gm_l8;
        int64_t t122 = gm_l5;
        int64_t r123; bool o124 = __builtin_add_overflow(t121, t122, &r123);
        if (o124) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 284, 5);
        int64_t t125 = r123;
        gm_l8 = t125;
    }
    {
        uint8_t t127 = gm_l6;
        if (!t127) goto ie126;
        {
            void * t129 = gm_l1;
            gm_tftb1odc739t5 t130 = gm_f_push_t83rg2m(t129, ((uint8_t)45ULL));
            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srcp2qdr7uqsmui, 285, 19);
            gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
        }
        goto ix126;
ie126:;
ix126:;
    }
    {
        int64_t t133 = gm_l8;
        uint8_t t134 = (t133 <= ((int64_t)0LL));
        if (!t134) goto ie132;
        {
            void * t136 = gm_l1;
            gm_b16_8 t137;
            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sjq55q61p743j));
            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t138 = gm_f_extend_0cs85gs(t136, t137);
            if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 1, gm_srcp2qdr7uqsmui, 287, 9);
            gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 1));
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                int64_t t141 = gm_l8;
                if (t141 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 288, 22);
                int64_t t142 = (int64_t)((uint64_t)0 - (uint64_t)t141);
                uint64_t t143;
                memset(&t143, 0, sizeof t143); memcpy(&t143, ((uint8_t *)&t142), sizeof t143 < 8 ? sizeof t143 : 8);
                uint64_t t144 = (uint64_t)t143;
                gm_l13 = t144;
            }
            {
                for (;;) {
                    uint64_t t146 = gm_l14;
                    uint64_t t147 = gm_l13;
                    uint8_t t148 = (t146 < t147);
                    if (!t148) goto lb145;
                    {
                        uint64_t t151 = gm_l14;
                        {
                            void * t155 = gm_l1;
                            gm_tftb1odc739t5 t156 = gm_f_push_t83rg2m(t155, ((uint8_t)48ULL));
                            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srcp2qdr7uqsmui, 288, 55);
                            gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                        }
                        goto me152;
mn153:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 288, 9);
me152:;
                    }
lc145:;
                    {
                        uint64_t t159 = gm_l14;
                        uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                        uint64_t t162 = r160;
                        gm_l14 = t162;
                    }
                }
lb145:;
            }
        }
        {
            void * t163 = gm_l1;
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
            gm_tftb1odc739t5 t166 = gm_f_extend_0cs85gs(t163, t165);
            if (((((uint8_t *)&t166))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t166) + 1, gm_srcp2qdr7uqsmui, 289, 9);
            gm_b0_1 t167 = GM_LD(gm_b0_1, (((uint8_t *)&t166) + 1));
        }
        goto ix132;
ie132:;
        int64_t t169 = gm_l8;
        uint64_t t170 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        int64_t t171 = (int64_t)t170;
        uint8_t t172 = (t169 >= t171);
        if (!t172) goto ie168;
        {
            void * t174 = gm_l1;
            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
            gm_tftb1odc739t5 t177 = gm_f_extend_0cs85gs(t174, t176);
            if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_srcp2qdr7uqsmui, 291, 9);
            gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
        }
        {
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                int64_t t180 = gm_l8;
                uint64_t t181 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                int64_t t182 = (int64_t)t181;
                int64_t r183; bool o184 = __builtin_sub_overflow(t180, t182, &r183);
                if (o184) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 292, 22);
                int64_t t185 = r183;
                uint64_t t186;
                memset(&t186, 0, sizeof t186); memcpy(&t186, ((uint8_t *)&t185), sizeof t186 < 8 ? sizeof t186 : 8);
                uint64_t t187 = (uint64_t)t186;
                gm_l15 = t187;
            }
            {
                for (;;) {
                    uint64_t t189 = gm_l16;
                    uint64_t t190 = gm_l15;
                    uint8_t t191 = (t189 < t190);
                    if (!t191) goto lb188;
                    {
                        uint64_t t194 = gm_l16;
                        {
                            void * t198 = gm_l1;
                            gm_tftb1odc739t5 t199 = gm_f_push_t83rg2m(t198, ((uint8_t)48ULL));
                            if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_srcp2qdr7uqsmui, 292, 84);
                            gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                        }
                        goto me195;
mn196:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 292, 9);
me195:;
                    }
lc188:;
                    {
                        uint64_t t202 = gm_l16;
                        uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                        uint64_t t205 = r203;
                        gm_l16 = t205;
                    }
                }
lb188:;
            }
        }
        goto ix168;
ie168:;
        {
            int64_t t207 = gm_l8;
            uint64_t t208;
            memset(&t208, 0, sizeof t208); memcpy(&t208, ((uint8_t *)&t207), sizeof t208 < 8 ? sizeof t208 : 8);
            uint64_t t209 = (uint64_t)t208;
            gm_l17 = t209;
        }
        {
            void * t210 = gm_l1;
            uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            void * t212 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)));
            uint64_t t213 = gm_l17;
            uint64_t t214 = (uintptr_t)t213;
            if ((uintptr_t)0 > t214 || t214 > (uintptr_t)t211) gm_trap_slice((uintptr_t)0, t214, t211, gm_srcp2qdr7uqsmui, 295, 20);
            gm_b16_8 t215;
            GM_ST(void *, (uint8_t *)&t215, (void *)(((uint8_t *)t212 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(t214 - (uintptr_t)0));
            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
            gm_tftb1odc739t5 t217 = gm_f_extend_0cs85gs(t210, t216);
            if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srcp2qdr7uqsmui, 295, 9);
            gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
        }
        {
            void * t219 = gm_l1;
            gm_tftb1odc739t5 t220 = gm_f_push_t83rg2m(t219, ((uint8_t)46ULL));
            if (((((uint8_t *)&t220))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t220) + 1, gm_srcp2qdr7uqsmui, 296, 9);
            gm_b0_1 t221 = GM_LD(gm_b0_1, (((uint8_t *)&t220) + 1));
        }
        {
            void * t222 = gm_l1;
            uint64_t t223 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            void * t224 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)));
            uint64_t t225 = gm_l17;
            uint64_t t226 = (uintptr_t)t225;
            if (t226 > (uintptr_t)t223 || (uintptr_t)t223 > (uintptr_t)t223) gm_trap_slice(t226, (uintptr_t)t223, t223, gm_srcp2qdr7uqsmui, 297, 20);
            gm_b16_8 t227;
            GM_ST(void *, (uint8_t *)&t227, (void *)(((uint8_t *)t224 + t226 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)((uintptr_t)t223 - t226));
            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
            gm_tftb1odc739t5 t229 = gm_f_extend_0cs85gs(t222, t228);
            if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcp2qdr7uqsmui, 297, 9);
            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
        }
ix168:;
ix132:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2) {
    struct gm_fr_write_int_lq1ktof F;
    gm_b16_8 gm_l0; /* b */
    uint8_t gm_l1; /* signed */
    void * gm_l2; /* out */
    unsigned __int128 gm_l3; /* v */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    uint64_t gm_l9; /* n */
    GM_STACK_CHECK(&F, 694u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 1));
                    gm_tf5srh4mp4b3i t15;
                    GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l6 = t18;
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t15) + 8));
                    gm_l7 = t19;
                    {
                        unsigned __int128 t21 = gm_l3;
                        uint8_t t22 = gm_l7;
                        unsigned __int128 t23 = (unsigned __int128)t22;
                        uint64_t t24 = gm_l6;
                        uint64_t r25; bool o26 = __builtin_mul_overflow(t24, ((uint64_t)8ULL), &r25);
                        if (o26) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 314, 53);
                        uint64_t t27 = r25;
                        unsigned __int128 t28 = (unsigned __int128)t27;
                        unsigned __int128 n29 = (unsigned __int128)t28;
                        if (n29 >= 128) gm_trap_shift((uint64_t)n29, "u128", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 r30 = (unsigned __int128)((unsigned __int128)t23 << (int)n29);
                        bool o31 = (unsigned __int128)(r30 >> (int)n29) != t23;
                        if (o31) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 t32 = r30;
                        unsigned __int128 t33 = (unsigned __int128)(t21 | t32);
                        gm_l3 = t33;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 314, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l5 = t38;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t40 = gm_l1;
        if (!t40) goto ie39;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t42 = (t41 > ((uint64_t)0ULL));
        if (!t42) goto ie39;
        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t44 = (t43 < ((uint64_t)16ULL));
        if (!t44) goto ie39;
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
        if (o47) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 315, 50);
        uint64_t t48 = r46;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcp2qdr7uqsmui, 315, 48);
        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t49 * 1));
        uint8_t t52 = (t51 >= ((uint8_t)128ULL));
        if (!t52) goto ie39;
        {
            unsigned __int128 t54 = gm_l3;
            unsigned __int128 t55 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r57; bool o58 = __builtin_mul_overflow(t56, ((uint64_t)8ULL), &r57);
            if (o58) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 317, 31);
            uint64_t t59 = r57;
            unsigned __int128 t60 = (unsigned __int128)t59;
            unsigned __int128 n61 = (unsigned __int128)t60;
            if (n61 >= 128) gm_trap_shift((uint64_t)n61, "u128", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 r62 = (unsigned __int128)((unsigned __int128)t55 << (int)n61);
            bool o63 = (unsigned __int128)(r62 >> (int)n61) != t55;
            if (o63) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 t64 = r62;
            unsigned __int128 t65 = (unsigned __int128)(t54 | t64);
            gm_l3 = t65;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint8_t t67 = gm_l1;
        if (!t67) goto ie66;
        unsigned __int128 t68 = gm_l3;
        unsigned __int128 n69 = (unsigned __int128)((uint64_t)127ULL);
        if (n69 >= 128) gm_trap_shift((uint64_t)n69, "u128", gm_srcp2qdr7uqsmui, 319, 19);
        unsigned __int128 t70 = (unsigned __int128)(t68 >> (int)n69);
        uint8_t t71 = ((t70 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t71) goto ie66;
        {
            void * t73 = gm_l2;
            gm_tftb1odc739t5 t74 = gm_f_push_t83rg2m(t73, ((uint8_t)45ULL));
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcp2qdr7uqsmui, 320, 9);
            gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
        }
        {
            unsigned __int128 t76 = gm_l3;
            unsigned __int128 t77 = (unsigned __int128)~t76;
            unsigned __int128 r78; bool o79 = __builtin_add_overflow(t77, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r78);
            unsigned __int128 t80 = r78;
            gm_l3 = t80;
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
    }
    {
        gm_l9 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb81;
            {
                uint64_t t84 = gm_l9;
                uint64_t t85 = (uintptr_t)t84;
                if (t85 >= (uintptr_t)(40)) gm_trap_index(t85, 40, gm_srcp2qdr7uqsmui, 326, 9);
                unsigned __int128 t86 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 326, 28);
                unsigned __int128 t87 = (unsigned __int128)(t86 % ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)));
                uint8_t t88 = (uint8_t)t87;
                uint8_t r89; bool o90 = __builtin_add_overflow(((uint8_t)48ULL), t88, &r89);
                if (o90) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 326, 21);
                uint8_t t91 = r89;
                GM_ST(uint8_t, (((uint8_t *)&F.l8) + t85 * 1), t91);
            }
            {
                uint64_t t92 = gm_l9;
                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                if (o94) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 327, 9);
                uint64_t t95 = r93;
                gm_l9 = t95;
            }
            {
                unsigned __int128 t96 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 328, 9);
                unsigned __int128 t97 = (unsigned __int128)(t96 / ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)));
                gm_l3 = t97;
            }
            {
                unsigned __int128 t99 = gm_l3;
                uint8_t t100 = ((t99 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t100) goto ie98;
                {
                    goto lb81;
                }
                goto ix98;
ie98:;
ix98:;
            }
lc81:;
        }
lb81:;
    }
    {
        for (;;) {
            uint64_t t103 = gm_l9;
            uint8_t t104 = (t103 > ((uint64_t)0ULL));
            if (!t104) goto lb102;
            {
                uint64_t t107 = gm_l9;
                uint64_t r108; bool o109 = __builtin_sub_overflow(t107, ((uint64_t)1ULL), &r108);
                if (o109) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 332, 9);
                uint64_t t110 = r108;
                gm_l9 = t110;
            }
            {
                void * t111 = gm_l2;
                uint64_t t112 = gm_l9;
                uint64_t t113 = (uintptr_t)t112;
                if (t113 >= (uintptr_t)(40)) gm_trap_index(t113, 40, gm_srcp2qdr7uqsmui, 333, 18);
                uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + t113 * 1));
                gm_tftb1odc739t5 t115 = gm_f_push_t83rg2m(t111, t114);
                if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcp2qdr7uqsmui, 333, 9);
                gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
            }
lc102:;
        }
lb102:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
