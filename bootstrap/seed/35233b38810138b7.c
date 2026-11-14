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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; } gm_t8d5d2ep1fo6t;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; gm_t9qkh0rsooc8d f14; gm_t9qkh0rsooc8d f15; gm_t9qkh0rsooc8d f16; gm_t9qkh0rsooc8d f17; } gm_ta8d7579em356;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_tbgd14esf0jvi a[2]; } gm_t0vh9i8m50f41;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8d5d2ep1fo6t) == 4 && _Alignof(gm_t8d5d2ep1fo6t) == 1 && offsetof(gm_t8d5d2ep1fo6t, f0) == 0 && offsetof(gm_t8d5d2ep1fo6t, f1) == 1 && offsetof(gm_t8d5d2ep1fo6t, f2) == 2 && offsetof(gm_t8d5d2ep1fo6t, f3) == 3, "layout of `(Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_ta8d7579em356) == 18 && _Alignof(gm_ta8d7579em356) == 1 && offsetof(gm_ta8d7579em356, f0) == 0 && offsetof(gm_ta8d7579em356, f1) == 1 && offsetof(gm_ta8d7579em356, f2) == 2 && offsetof(gm_ta8d7579em356, f3) == 3 && offsetof(gm_ta8d7579em356, f4) == 4 && offsetof(gm_ta8d7579em356, f5) == 5 && offsetof(gm_ta8d7579em356, f6) == 6 && offsetof(gm_ta8d7579em356, f7) == 7 && offsetof(gm_ta8d7579em356, f8) == 8 && offsetof(gm_ta8d7579em356, f9) == 9 && offsetof(gm_ta8d7579em356, f10) == 10 && offsetof(gm_ta8d7579em356, f11) == 11 && offsetof(gm_ta8d7579em356, f12) == 12 && offsetof(gm_ta8d7579em356, f13) == 13 && offsetof(gm_ta8d7579em356, f14) == 14 && offsetof(gm_ta8d7579em356, f15) == 15 && offsetof(gm_ta8d7579em356, f16) == 16 && offsetof(gm_ta8d7579em356, f17) == 17, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; } gm_tt7vn9ehj0acr;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t0vh9i8m50f41) == 64 && _Alignof(gm_t0vh9i8m50f41) == 8 && offsetof(gm_t0vh9i8m50f41, a) == 0, "layout of `[2]Edit`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; } gm_t41cj6gvba3r8;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; } gm_t72hb7duiv9b4;
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; gm_t3fcbuehh3bs2 f4; } gm_tak1k6bk5qcen;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tt7vn9ehj0acr) == 3 && _Alignof(gm_tt7vn9ehj0acr) == 1 && offsetof(gm_tt7vn9ehj0acr, f0) == 0 && offsetof(gm_tt7vn9ehj0acr, f1) == 2, "layout of `?BinOp`");
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t41cj6gvba3r8) == 96 && _Alignof(gm_t41cj6gvba3r8) == 8 && offsetof(gm_t41cj6gvba3r8, f0) == 0 && offsetof(gm_t41cj6gvba3r8, f1) == 32 && offsetof(gm_t41cj6gvba3r8, f2) == 64, "layout of `(TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_t72hb7duiv9b4) == 128 && _Alignof(gm_t72hb7duiv9b4) == 8 && offsetof(gm_t72hb7duiv9b4, f0) == 0 && offsetof(gm_t72hb7duiv9b4, f1) == 32 && offsetof(gm_t72hb7duiv9b4, f2) == 64 && offsetof(gm_t72hb7duiv9b4, f3) == 96, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tak1k6bk5qcen) == 160 && _Alignof(gm_tak1k6bk5qcen) == 8 && offsetof(gm_tak1k6bk5qcen, f0) == 0 && offsetof(gm_tak1k6bk5qcen, f1) == 32 && offsetof(gm_tak1k6bk5qcen, f2) == 64 && offsetof(gm_tak1k6bk5qcen, f3) == 96 && offsetof(gm_tak1k6bk5qcen, f4) == 128, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_tao63pk3r18v5 v1; } f1; } gm_t1vei8bd9e9m7;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1vei8bd9e9m7) == 96 && _Alignof(gm_t1vei8bd9e9m7) == 8 && offsetof(gm_t1vei8bd9e9m7, f0) == 0 && offsetof(gm_t1vei8bd9e9m7, f1) == 8, "layout of `Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t1vei8bd9e9m7 v1; } f1; } gm_tb1kjfin6jnc2;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tb1kjfin6jnc2) == 104 && _Alignof(gm_tb1kjfin6jnc2) == 8 && offsetof(gm_tb1kjfin6jnc2, f0) == 0 && offsetof(gm_tb1kjfin6jnc2, f1) == 8, "layout of `Abort!Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7te2tdpebbt4 v1; } f1; } gm_t3q68r4ne5jv8;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_t3q68r4ne5jv8) == 152 && _Alignof(gm_t3q68r4ne5jv8) == 8 && offsetof(gm_t3q68r4ne5jv8, f0) == 0 && offsetof(gm_t3q68r4ne5jv8, f1) == 8, "layout of `Abort!Arg`");
struct gm_fr_add_from_7os0rvi {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_add_op_99op354 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_ambiguous_try_else_0v14t0i {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_t0vh9i8m50f41 l5; /* <temporary array> */
    gm_t0vh9i8m50f41 l6; /* <temporary array> */
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
struct gm_fr_at_reported_kw_name_it872te {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binary_41fr5r5 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_binary_follows_jeb42bi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_from_pu2bq19 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tm4pc0emhrv1g l5; /* t */
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tc3oht4ksth9d l12; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_bit_op_p81j9u3 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_can_begin_expr_rmia9bs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_can_begin_type_a3rjrm7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_9gj7im4 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_deny_in_head_06d9o22 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_eat_label_7eokqmm {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ends_expr_after_name_5t7p9lg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_error_expr_dhdi86k {
    gm_frame h;
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1s091rv {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_is_arith_l6b4tpf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_q2f0b93 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_relational_n3goics {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
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
struct gm_fr_mul_from_mlskgvm {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_mul_op_ab6loj8 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
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
struct gm_fr_op_text_3o86dat {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parens_0pje0fd {
    gm_frame h;
    gm_t0vh9i8m50f41 ret;
};
struct gm_fr_parse_add_dh9nvbt {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_arg_t6fq0nq {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tc3oht4ksth9d l5; /* e */
    gm_t3q68r4ne5jv8 ret;
};
struct gm_fr_parse_bang_irmkots {
    gm_frame h;
    void * l0; /* p */
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t0mfir12leg2g l3; /* rhs */
    gm_tc3oht4ksth9d l4; /* r */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_bit_h83vs72 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_block_expr_0j6b78q {
    gm_frame h;
    gm_t79rbdv4rafe5 l1; /* block */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_break_hehputf {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_call_args_32e205h {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_capture_body_fgf89k2 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_cmp_q5clpig {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* first */
    gm_tc3oht4ksth9d l3; /* lhs */
    gm_tc3oht4ksth9d l6; /* rhs */
    gm_tc3oht4ksth9d l7; /* e */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_continue_ertnf67 {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_dot_suffix_1fk204i {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_else_smoqgg4 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t1s75i08qtptd l2; /* capture */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_expr_bkb2n6e {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fallback_g1vpkra {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_head_expr_6sofdhn {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_import_s1hv231 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_intrinsic_4ou54h3 {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* t */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_logic_ajejref {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_mul_35e1t49 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_not_a8b3gjf {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_paren_sq1guoi {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* elems */
    gm_tc3oht4ksth9d l8; /* value */
    gm_t1s75i08qtptd l9; /* name */
    gm_tc3oht4ksth9d l10; /* value */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ao4kstr {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ops_2p1csn3 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_tc3oht4ksth9d l3; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_prefix_671fi6d {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_primary_1vvr4ji {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_range_o9o2al7 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_resume_u919e4d {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_tc3oht4ksth9d l7; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_return_fjkmc1q {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_shift_jium2c6 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_1hujuhl {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_t1vei8bd9e9m7 l3; /* s */
    gm_tc3oht4ksth9d l5; /* index */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_inner_dhgpovb {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* first */
    gm_tb1kjfin6jnc2 ret;
};
struct gm_fr_prefix_op_japctlh {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_range_from_5v6ko9m {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l5; /* rhs */
    gm_tc3oht4ksth9d l6; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_report_diag_big1nsf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
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
struct gm_fr_shift_from_3cnaq25 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_tc3oht4ksth9d l5; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_shift_op_sro3o9l {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_shrink_to_lo_iok07dh {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b);
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0);
__attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_bump_reported_kw_name_gs5eq1i(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gqrkfjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o5o20fv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_pb6f6q3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_keyword_as_name_ipm23p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_for_q8vk3da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_foreign_l4091na(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_handler_lit_1nkojsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_if_shktfou(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_match_1lcgsrh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_with_hlmj8pt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_speculate_djd8kc6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1cqejvllajb6[3];
extern const uint8_t gm_s1llcn0p4qesk[20];
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s1q0n6t6d7g6n[34];
extern const uint8_t gm_s27pt4vcvtscl[24];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3tpa8q2q7llg[17];
extern const uint8_t gm_s42atdm90l4do[14];
extern const uint8_t gm_s4niue72anpss[37];
extern const uint8_t gm_s4puodol85qqo[55];
extern const uint8_t gm_s541gi95epov5[25];
extern const uint8_t gm_s5lrfo6g7933d[50];
extern const uint8_t gm_s5us2hgd173vu[27];
extern const uint8_t gm_s5vb1b1mnli2b[72];
extern const uint8_t gm_s8ki76moa277n[23];
extern const uint8_t gm_s8sl52ot187i2[50];
extern const uint8_t gm_s8vhc5kn5i8tv[38];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_sa3k87epai61e[25];
extern const uint8_t gm_sam3pd3469drv[55];
extern const uint8_t gm_saojtqmfeak72[8];
extern const uint8_t gm_sb455a3lakuom[60];
extern const uint8_t gm_sbckhsd8r0nr0[20];
extern const uint8_t gm_sh0is7j09prt2[23];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shsfl3e9phpsi[23];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sjrimf03ml0ct[13];
extern const uint8_t gm_skefd3dps40es[40];
extern const uint8_t gm_skh04d6p3n402[79];
extern const uint8_t gm_sl7a8f8pe8le9[21];
extern const uint8_t gm_slp04vsc9cddi[49];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_snemlnnsbdb13[46];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_sqm6u7rqmi1il[19];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0dtioet3p4f6[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_ssd99k9slj99i[3];
extern const uint8_t gm_ssf8mqtfpipac[45];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_sujvknhnh5am8[19];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svhvj3h6g939d[66];
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
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
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
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b) {
    if (a[2] != b[2]) return false;
    return !a[2] || gm_eqbqb4gb1d92(a, b);
}
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eq929lennfd3(a, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_reported_kw_name_it872te F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)2ULL)) goto mn6;
    gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t3) + 8));
    gm_l1 = t8;
    gm_t9qkh0rsooc8d t9 = gm_l1;
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 176) + 0));
    gm_b16_8 t12 = t11;
    uint8_t t13 = 0;
    gm_t9qkh0rsooc8d t14 = t9;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t12) + 8)); i++) if (gm_eq2ceic3f8rp(((uint8_t *)GM_LD(void *, ((uint8_t *)&t12))) + i * 1, ((uint8_t *)&t14))) { t13 = 1; break; }
    uint8_t t15 = (uint8_t)(t13);
    t4 = t15;
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn16:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 199, 56);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 144)));
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
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_expr_dhdi86k F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    gm_t2gtbh39ptekd t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)50ULL));
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t1, &t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1s091rv F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
            gm_tm4pc0emhrv1g t4;
            gm_f_bump_q2i40re((void *)&t4, t3);
        }
    }
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
        gm_f_parse_type_2m93dtq((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_q2f0b93 F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t1 == t2));
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_capture_body_fgf89k2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)65ULL));
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
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
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_mul_35e1t49 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_f_mul_from_mlskgvm((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_report_diag_big1nsf F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 259, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
        gm_f_push_o4ckdr1(t9, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shrink_to_lo_iok07dh F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_tl874t2atnlh3 t4;
    GM_ST(uint32_t, ((uint8_t *)&t4 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 4), t2);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_from_7os0rvi F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 1173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_mul_from_mlskgvm((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        for (;;) {
            void * t9 = gm_l0;
            gm_t3fcbuehh3bs2 t10;
            gm_f_kind_k239kpg((void *)&t10, t9);
            gm_tt7vn9ehj0acr t11 = gm_f_add_op_99op354(&t10);
            gm_tt7vn9ehj0acr t12 = t11;
            if (!((((uint8_t *)&t12))[2] != 0)) goto lb8;
            gm_ts6oqppjh0ba3 t13 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t12));
            gm_l3 = t13;
            {
                {
                    void * t17 = gm_l0;
                    gm_tm4pc0emhrv1g t18;
                    gm_f_bump_q2i40re((void *)&t18, t17);
                }
            }
            {
                void * t19 = gm_l0;
                gm_to1cssm8dfiv5 t20;
                gm_f_parse_mul_35e1t49((void *)&t20, (void *)NULL, cx, NULL, t19);
                if (gm_unw.kind) {
                    goto gm_ll14;
                }
                gm_to1cssm8dfiv5 t21 = t20;
                if (((((uint8_t *)&t21))[0] != 0)) {
                    gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 8));
                    gm_to1cssm8dfiv5 t23;
                    memset(&t23, 0, sizeof t23);
                    ((uint8_t *)&t23)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
                    F.ret = t23;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t24 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t21) + 8));
                F.l4 = t24;
            }
            {
                void * t25 = gm_l0;
                gm_ts6oqppjh0ba3 t26 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t25, t26, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll14;
                }
            }
lc8:;
        }
gm_ll14:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        gm_tc3oht4ksth9d t27 = F.l2;
        gm_to1cssm8dfiv5 t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        ((uint8_t *)&t28)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t28 + 8), t27);
        gm_to1cssm8dfiv5 t29 = t28;
        uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
        F.ret = t28;
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_op_99op354 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; default: goto ma11; }
ma3:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)8ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn14;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t18;
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
    gm_tt7vn9ehj0acr t19;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[2] = 1;
    t1 = t19;
    goto me2;
mn14:;
ma4:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t21 != ((uint8_t)8ULL)) goto mn20;
    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t22 != ((uint8_t)10ULL)) goto mn20;
    gm_t9qkh0rsooc8d t23;
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 1), t23);
    gm_tt7vn9ehj0acr t25;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[2] = 1;
    t1 = t25;
    goto me2;
mn20:;
ma5:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)8ULL)) goto mn26;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t28 != ((uint8_t)14ULL)) goto mn26;
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 1), t29);
    gm_tt7vn9ehj0acr t31;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t31), t30);
    ((uint8_t *)&t31)[2] = 1;
    t1 = t31;
    goto me2;
mn26:;
ma6:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t33 != ((uint8_t)8ULL)) goto mn32;
    uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t34 != ((uint8_t)18ULL)) goto mn32;
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t35);
    gm_tt7vn9ehj0acr t37;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[2] = 1;
    t1 = t37;
    goto me2;
mn32:;
ma7:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t39 != ((uint8_t)8ULL)) goto mn38;
    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t40 != ((uint8_t)1ULL)) goto mn38;
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t42 + 1), t41);
    gm_tt7vn9ehj0acr t43;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43), t42);
    ((uint8_t *)&t43)[2] = 1;
    t1 = t43;
    goto me2;
mn38:;
ma8:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)8ULL)) goto mn44;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t46 != ((uint8_t)11ULL)) goto mn44;
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t48;
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 1), t47);
    gm_tt7vn9ehj0acr t49;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t49), t48);
    ((uint8_t *)&t49)[2] = 1;
    t1 = t49;
    goto me2;
mn44:;
ma9:;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t51 != ((uint8_t)8ULL)) goto mn50;
    uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t52 != ((uint8_t)15ULL)) goto mn50;
    gm_t9qkh0rsooc8d t53;
    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t54;
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 1), t53);
    gm_tt7vn9ehj0acr t55;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t55), t54);
    ((uint8_t *)&t55)[2] = 1;
    t1 = t55;
    goto me2;
mn50:;
ma10:;
    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t57 != ((uint8_t)8ULL)) goto mn56;
    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t58 != ((uint8_t)19ULL)) goto mn56;
    gm_t9qkh0rsooc8d t59;
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t60;
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t60 + 1), t59);
    gm_tt7vn9ehj0acr t61;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t61), t60);
    ((uint8_t *)&t61)[2] = 1;
    t1 = t61;
    goto me2;
mn56:;
ma11:;
    gm_tt7vn9ehj0acr t63;
    memset(&t63, 0, sizeof t63);
    t1 = t63;
    goto me2;
mn62:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 61, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0) { return gm_f_add_op_99op354(&a0); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_ambiguous_try_else_0v14t0i F;
    void * gm_l0; /* p */
    void * gm_l1; /* lhs */
    void * gm_l2; /* rhs */
    void * gm_l3; /* operand */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)12ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 0));
        if (t5 != ((uint8_t)4ULL)) goto ie2;
        void * t6 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        {
            void * t8 = gm_l1;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s5vb1b1mnli2b));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(71));
            gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t11 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_shsfl3e9phpsi));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(22));
            {
                void * t14 = gm_l0;
                uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 0));
                void * t16 = gm_l3;
                uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t16) + 72) + 4));
                void * t18 = gm_l2;
                uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t18) + 72) + 8));
                gm_f_parens_0pje0fd((void *)&(*(gm_t0vh9i8m50f41 *)((uint8_t *)&F.l5)), t15, t17, t19);
            }
            void * t20 = ((uint8_t *)&F.l5);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0dtioet3p4f6, 154, 47);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t20 + (uintptr_t)0 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t22 = t21;
            gm_b0_1 t23 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t11, t12, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t24 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s8ki76moa277n));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(22));
            {
                void * t27 = gm_l0;
                uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)t27) + 0));
                void * t29 = gm_l1;
                uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)t29) + 72) + 4));
                void * t31 = gm_l1;
                uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t31) + 72) + 8));
                gm_f_parens_0pje0fd((void *)&(*(gm_t0vh9i8m50f41 *)((uint8_t *)&F.l6)), t28, t30, t32);
            }
            void * t33 = ((uint8_t *)&F.l6);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0dtioet3p4f6, 155, 47);
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t33 + (uintptr_t)0 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t35 = t34;
            gm_b0_1 t36 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t24, t25, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t37 = gm_l0;
            gm_f_report_diag_big1nsf(t37, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t39;
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
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
inline __attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3) {
    struct gm_fr_binary_41fr5r5 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    memcpy(&F.l3, a3, sizeof F.l3);
    {
        void * t2 = gm_l0;
        gm_trl9d7m1k5lbd t3;
        memset((uint8_t *)&t3 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t4 = gm_l1;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t3 + 0, t4);
        void * t5 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t3 + 8, t5);
        void * t6 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t3 + 16, t6);
        gm_t2gtbh39ptekd t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)13ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t7 + 8), t3);
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
        gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t8), GM_CONV(gm_cseahvg8v7p0vs, t9)));
        gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t2, &t7, GM_CONV(gm_cseahvg8v7p0vs, t10));
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
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binary_follows_jeb42bi F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
    gm_t0am46iohlpol t14;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 0), t12);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
    gm_t0am46iohlpol t15 = t14;
    uint8_t t16 = 0;
    gm_t9qkh0rsooc8d t17 = t11;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 0, ((uint8_t *)&t17))) t16 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 1, ((uint8_t *)&t17))) t16 = 1;
    uint8_t t18 = (uint8_t)(t16);
    t1 = t18;
    goto me2;
mn8:;
ma4:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)8ULL)) goto mn19;
    gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t21;
    uint8_t t22;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    switch (t30) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma25; case ((uint8_t)6ULL): goto ma25; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma27; case ((uint8_t)13ULL): goto ma25; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma25; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma25; case ((uint8_t)22ULL): goto ma28; case ((uint8_t)23ULL): goto ma28; case ((uint8_t)24ULL): goto ma28; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma28; case ((uint8_t)27ULL): goto ma28; case ((uint8_t)28ULL): goto ma28; case ((uint8_t)29ULL): goto ma28; case ((uint8_t)30ULL): goto ma28; case ((uint8_t)31ULL): goto ma28; case ((uint8_t)32ULL): goto ma28; case ((uint8_t)33ULL): goto ma28; case ((uint8_t)34ULL): goto ma28; case ((uint8_t)35ULL): goto ma28; case ((uint8_t)36ULL): goto ma28; case ((uint8_t)37ULL): goto ma28; case ((uint8_t)38ULL): goto ma28; case ((uint8_t)39ULL): goto ma28; case ((uint8_t)40ULL): goto ma28; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma28; case ((uint8_t)46ULL): goto ma28; case ((uint8_t)47ULL): goto ma28; case ((uint8_t)48ULL): goto ma28; case ((uint8_t)49ULL): goto ma28; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma24; case ((uint8_t)52ULL): goto ma28; case ((uint8_t)53ULL): goto ma28; case ((uint8_t)54ULL): goto ma28; case ((uint8_t)55ULL): goto ma28; case ((uint8_t)56ULL): goto ma28; case ((uint8_t)57ULL): goto ma28; case ((uint8_t)58ULL): goto ma28; case ((uint8_t)59ULL): goto ma28; case ((uint8_t)60ULL): goto ma28; case ((uint8_t)61ULL): goto ma28; case ((uint8_t)62ULL): goto ma28; case ((uint8_t)63ULL): goto ma28; case ((uint8_t)64ULL): goto ma28; case ((uint8_t)65ULL): goto ma28; case ((uint8_t)66ULL): goto ma28; case ((uint8_t)67ULL): goto ma24; case ((uint8_t)68ULL): goto ma24; default: goto ma28; }
ma24:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t34 != ((uint8_t)67ULL)) goto pa33;
    goto po32;
pa33:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t36 != ((uint8_t)68ULL)) goto pa35;
    goto po32;
pa35:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t38 != ((uint8_t)41ULL)) goto pa37;
    goto po32;
pa37:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t40 != ((uint8_t)42ULL)) goto pa39;
    goto po32;
pa39:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t42 != ((uint8_t)43ULL)) goto pa41;
    goto po32;
pa41:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t44 != ((uint8_t)44ULL)) goto pa43;
    goto po32;
pa43:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t46 != ((uint8_t)50ULL)) goto pa45;
    goto po32;
pa45:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t48 != ((uint8_t)51ULL)) goto pa47;
    goto po32;
pa47:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t50 != ((uint8_t)7ULL)) goto pa49;
    goto po32;
pa49:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t52 != ((uint8_t)8ULL)) goto pa51;
    goto po32;
pa51:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t54 != ((uint8_t)9ULL)) goto pa53;
    goto po32;
pa53:;
    goto mn31;
po32:;
    t22 = ((uint8_t)1);
    goto me23;
mn31:;
ma25:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t58 != ((uint8_t)5ULL)) goto pa57;
    goto po56;
pa57:;
    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t60 != ((uint8_t)13ULL)) goto pa59;
    goto po56;
pa59:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t62 != ((uint8_t)17ULL)) goto pa61;
    goto po56;
pa61:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t64 != ((uint8_t)21ULL)) goto pa63;
    goto po56;
pa63:;
    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t66 != ((uint8_t)6ULL)) goto pa65;
    goto po56;
pa65:;
    goto mn55;
po56:;
    t22 = ((uint8_t)1);
    goto me23;
mn55:;
ma26:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t70 != ((uint8_t)0ULL)) goto pa69;
    goto po68;
pa69:;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t72 != ((uint8_t)10ULL)) goto pa71;
    goto po68;
pa71:;
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t74 != ((uint8_t)14ULL)) goto pa73;
    goto po68;
pa73:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t76 != ((uint8_t)18ULL)) goto pa75;
    goto po68;
pa75:;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t78 != ((uint8_t)1ULL)) goto pa77;
    goto po68;
pa77:;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t80 != ((uint8_t)11ULL)) goto pa79;
    goto po68;
pa79:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t82 != ((uint8_t)15ULL)) goto pa81;
    goto po68;
pa81:;
    uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t84 != ((uint8_t)19ULL)) goto pa83;
    goto po68;
pa83:;
    goto mn67;
po68:;
    t22 = ((uint8_t)1);
    goto me23;
mn67:;
ma27:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t88 != ((uint8_t)2ULL)) goto pa87;
    goto po86;
pa87:;
    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t90 != ((uint8_t)12ULL)) goto pa89;
    goto po86;
pa89:;
    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t92 != ((uint8_t)16ULL)) goto pa91;
    goto po86;
pa91:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t94 != ((uint8_t)20ULL)) goto pa93;
    goto po86;
pa93:;
    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t96 != ((uint8_t)3ULL)) goto pa95;
    goto po86;
pa95:;
    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t98 != ((uint8_t)4ULL)) goto pa97;
    goto po86;
pa97:;
    goto mn85;
po86:;
    t22 = ((uint8_t)1);
    goto me23;
mn85:;
ma28:;
    t22 = ((uint8_t)0);
    goto me23;
mn99:;
ma29:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 88, 18);
me23:;
    t1 = t22;
    goto me2;
mn19:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn100:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 86, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0) { return gm_f_binary_follows_jeb42bi(&a0); }
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_bit_from_pu2bq19 F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l3; /* chain */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* word */
    gm_b16_8 gm_l9; /* before */
    gm_b16_8 gm_l10; /* after */
    GM_STACK_CHECK(&F, 2515u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_shift_from_3cnaq25((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        gm_tt7vn9ehj0acr t8;
        memset(&t8, 0, sizeof t8);
        gm_l3 = t8;
    }
    {
        for (;;) {
            void * t10 = gm_l0;
            gm_t3fcbuehh3bs2 t11;
            gm_f_kind_k239kpg((void *)&t11, t10);
            gm_tt7vn9ehj0acr t12 = gm_f_bit_op_p81j9u3(&t11);
            gm_tt7vn9ehj0acr t13 = t12;
            if (!((((uint8_t *)&t13))[2] != 0)) goto lb9;
            gm_ts6oqppjh0ba3 t14 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t13));
            gm_l4 = t14;
            {
                uint8_t t18;
                if (((((uint8_t *)&gm_l3))[2] != 0)) goto mn20;
                t18 = ((uint8_t)1);
                goto me19;
mn20:;
                t18 = ((uint8_t)0);
                goto me19;
mn21:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 302, 12);
me19:;
                if (!t18) goto ie17;
                {
                    gm_ts6oqppjh0ba3 t23 = gm_l4;
                    gm_tt7vn9ehj0acr t24;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t24), t23);
                    ((uint8_t *)&t24)[2] = 1;
                    gm_l3 = t24;
                }
                goto ix17;
ie17:;
                gm_tt7vn9ehj0acr t26 = gm_l3;
                gm_ts6oqppjh0ba3 t27 = gm_l4;
                gm_tt7vn9ehj0acr t28;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28), t27);
                ((uint8_t *)&t28)[2] = 1;
                gm_tt7vn9ehj0acr t29 = t26;
                gm_tt7vn9ehj0acr t30 = t28;
                uint8_t t31 = (!gm_eqcdhc1ipuua(((uint8_t *)&t29), ((uint8_t *)&t30)));
                if (!t31) goto ie25;
                {
                    void * t33 = gm_l0;
                    void * t34 = gm_l0;
                    gm_tl874t2atnlh3 t35 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t34));
                    gm_b16_8 t36;
                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_svhvj3h6g939d));
                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(65));
                    gm_b0_1 t37 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t33, t35, t36);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                }
                goto ix25;
ie25:;
ix25:;
ix17:;
            }
            {
                void * t38 = gm_l0;
                gm_f_bump_q2i40re((void *)&F.l5, t38);
            }
            {
                gm_ts6oqppjh0ba3 t40 = gm_l4;
                gm_ts6oqppjh0ba3 t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)7ULL));
                gm_ts6oqppjh0ba3 t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)8ULL));
                gm_t9ldr1q5hn9fq t43;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43 + 0), t41);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43 + 2), t42);
                gm_t9ldr1q5hn9fq t44 = t43;
                uint8_t t45 = 0;
                gm_ts6oqppjh0ba3 t46 = t40;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t44) + 0, ((uint8_t *)&t46))) t45 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t44) + 2, ((uint8_t *)&t46))) t45 = 1;
                uint8_t t47 = (uint8_t)(t45);
                if (!t47) goto ie39;
                void * t48 = gm_l0;
                gm_t9qkh0rsooc8d t49;
                uint8_t t51 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                if (!t51) goto ie50;
                gm_t9qkh0rsooc8d t53;
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)7ULL));
                gm_t9qkh0rsooc8d t54 = t53;
                t49 = t54;
                goto ix50;
ie50:;
                gm_t9qkh0rsooc8d t56;
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)8ULL));
                gm_t9qkh0rsooc8d t57 = t56;
                t49 = t57;
ix50:;
                uint8_t t58 = gm_f_at_54906rm(t48, t49);
                if (!t58) goto ie39;
                void * t59 = gm_l0;
                uint32_t t60 = gm_f_lo_fp82gt4(t59);
                uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 8));
                uint8_t t62 = ((t60 == t61));
                if (!t62) goto ie39;
                {
                    gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 32));
                    void * t65 = gm_l0;
                    gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t65));
                    gm_tl874t2atnlh3 t67 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t64), GM_CONV(gm_cseahvg8v7p0vs, t66)));
                    gm_l6 = t67;
                }
                {
                    gm_b16_8 t68;
                    uint8_t t70 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                    if (!t70) goto ie69;
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s1cqejvllajb6));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(2));
                    gm_b16_8 t73 = t72;
                    t68 = t73;
                    goto ix69;
ie69:;
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_ssd99k9slj99i));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(2));
                    gm_b16_8 t76 = t75;
                    t68 = t76;
ix69:;
                    gm_l7 = t68;
                }
                {
                    gm_b16_8 t77;
                    uint8_t t79 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                    if (!t79) goto ie78;
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_s920dpvvv6cej));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(3));
                    gm_b16_8 t82 = t81;
                    t77 = t82;
                    goto ix78;
ie78:;
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_spi47lg4tr80j));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(2));
                    gm_b16_8 t85 = t84;
                    t77 = t85;
ix78:;
                    gm_l8 = t77;
                }
                {
                    gm_b16_8 t86;
                    uint32_t t88 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 72) + 8));
                    uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                    uint8_t t90 = ((t88 == t89));
                    if (!t90) goto ie87;
                    gm_b16_8 t92;
                    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(1));
                    gm_b16_8 t93 = t92;
                    t86 = t93;
                    goto ix87;
ie87:;
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(0));
                    gm_b16_8 t96 = t95;
                    t86 = t96;
ix87:;
                    gm_l9 = t86;
                }
                {
                    {
                        void * t98 = gm_l0;
                        gm_tm4pc0emhrv1g t99;
                        gm_f_bump_q2i40re((void *)&t99, t98);
                    }
                }
                {
                    gm_b16_8 t100;
                    void * t102 = gm_l0;
                    uint32_t t103 = gm_f_lo_fp82gt4(t102);
                    uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t105 = ((t103 == t104));
                    if (!t105) goto ie101;
                    gm_b16_8 t107;
                    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(1));
                    gm_b16_8 t108 = t107;
                    t100 = t108;
                    goto ix101;
ie101:;
                    gm_b16_8 t110;
                    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(0));
                    gm_b16_8 t111 = t110;
                    t100 = t111;
ix101:;
                    gm_l10 = t100;
                }
                {
                    gm_tl874t2atnlh3 t112 = gm_l6;
                    gm_t6j6smj7qoctu t113;
                    gm_b16_8 t114 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
                    gm_b16_8 t115 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
                    gm_tabb710d9mq2e t116;
                    gm_f_format_5nk8afu((void *)&t116, (void *)NULL, cx, NULL, &t113);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                    gm_tabb710d9mq2e t117 = t116;
                    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src0dtioet3p4f6, 317, 33);
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                    gm_b16_8 t119 = t118;
                    gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                    gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t112, t120);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                }
                {
                    void * t121 = (void *)((uint8_t *)&F.l11);
                    gm_t6j6smj7qoctu t122;
                    gm_b16_8 t123 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t122 + 0, t123);
                    gm_b16_8 t124 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t122 + 16, t124);
                    gm_tabb710d9mq2e t125;
                    gm_f_format_gqrkfjb((void *)&t125, (void *)NULL, cx, NULL, &t122);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                    gm_tabb710d9mq2e t126 = t125;
                    if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 8, gm_src0dtioet3p4f6, 318, 23);
                    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
                    gm_b16_8 t128 = t127;
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                    gm_tl874t2atnlh3 t130 = gm_l6;
                    gm_tfeab279c16rr t131;
                    gm_b16_8 t132 = gm_l9;
                    GM_ST(gm_b16_8, (uint8_t *)&t131 + 0, t132);
                    gm_b16_8 t133 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t131 + 16, t133);
                    gm_b16_8 t134 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t131 + 32, t134);
                    gm_tabb710d9mq2e t135;
                    gm_f_format_dsta2ql((void *)&t135, (void *)NULL, cx, NULL, &t131);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                    gm_tabb710d9mq2e t136 = t135;
                    if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_src0dtioet3p4f6, 318, 75);
                    gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
                    gm_b16_8 t138 = t137;
                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                    gm_b0_1 t140 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t121, t129, t130, t139);
                    if (gm_unw.kind) {
                        goto gm_ll15;
                    }
                }
                {
                    void * t141 = gm_l0;
                    gm_b0_1 t142 = gm_f_fail_diag_u0q4kp3(t141, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                    gm_to1cssm8dfiv5 t143;
                    memset(&t143, 0, sizeof t143);
                    ((uint8_t *)&t143)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t143 + 8), t142);
                    gm_to1cssm8dfiv5 t144 = t143;
                    uint8_t t145 = ((((uint8_t *)&t144))[0] != 0);
                    F.ret = t143;
                    goto gm_fret;
                }
                goto ix39;
ie39:;
ix39:;
            }
            {
                void * t146 = gm_l0;
                gm_to1cssm8dfiv5 t147;
                gm_f_parse_shift_jium2c6((void *)&t147, (void *)NULL, cx, NULL, t146);
                if (gm_unw.kind) {
                    goto gm_ll15;
                }
                gm_to1cssm8dfiv5 t148 = t147;
                if (((((uint8_t *)&t148))[0] != 0)) {
                    gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 8));
                    gm_to1cssm8dfiv5 t150;
                    memset(&t150, 0, sizeof t150);
                    ((uint8_t *)&t150)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t150 + 8), t149);
                    F.ret = t150;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t151 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t148) + 8));
                F.l12 = t151;
            }
            {
                void * t152 = gm_l0;
                gm_ts6oqppjh0ba3 t153 = gm_l4;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t152, t153, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_ll15;
                }
            }
lc9:;
        }
gm_ll15:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
        }
        goto gm_lad;
lb9:;
    }
    {
        gm_tc3oht4ksth9d t154 = F.l2;
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_op_p81j9u3 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)8ULL)) goto mn9;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t11 != ((uint8_t)7ULL)) goto mn9;
    gm_ts6oqppjh0ba3 t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)7ULL));
    gm_tt7vn9ehj0acr t13;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[2] = 1;
    t1 = t13;
    goto me2;
mn9:;
ma4:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)8ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)8ULL)) goto mn14;
    gm_ts6oqppjh0ba3 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
    gm_tt7vn9ehj0acr t18;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[2] = 1;
    t1 = t18;
    goto me2;
mn14:;
ma5:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)8ULL)) goto mn19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)9ULL)) goto mn19;
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)9ULL));
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn19:;
ma6:;
    gm_tt7vn9ehj0acr t25;
    memset(&t25, 0, sizeof t25);
    t1 = t25;
    goto me2;
mn24:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 45, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0) { return gm_f_bit_op_p81j9u3(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_expr_rmia9bs F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma3; case ((uint8_t)6ULL): goto ma3; case ((uint8_t)7ULL): goto ma3; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)0ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto pa13;
    goto po10;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto pa15;
    goto po10;
pa15:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)4ULL)) goto pa17;
    goto po10;
pa17:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)5ULL)) goto pa19;
    goto po10;
pa19:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t22 != ((uint8_t)6ULL)) goto pa21;
    goto po10;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t24 != ((uint8_t)7ULL)) goto pa23;
    goto po10;
pa23:;
    goto mn9;
po10:;
    t1 = ((uint8_t)1);
    goto me2;
mn9:;
ma4:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t27;
    gm_t9qkh0rsooc8d t28 = gm_l1;
    gm_ta8d7579em356 t29;
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 0, t30);
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)10ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 1, t31);
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)19ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 2, t32);
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)11ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 3, t33);
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)24ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 4, t34);
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)27ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 5, t35);
    gm_t9qkh0rsooc8d t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)6ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 6, t36);
    gm_t9qkh0rsooc8d t37;
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)14ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 7, t37);
    gm_t9qkh0rsooc8d t38;
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 8, t38);
    gm_t9qkh0rsooc8d t39;
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)17ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 9, t39);
    gm_t9qkh0rsooc8d t40;
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)31ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 10, t40);
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 11, t41);
    gm_t9qkh0rsooc8d t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)23ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 12, t42);
    gm_t9qkh0rsooc8d t43;
    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 13, t43);
    gm_t9qkh0rsooc8d t44;
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 14, t44);
    gm_t9qkh0rsooc8d t45;
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)22ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 15, t45);
    gm_t9qkh0rsooc8d t46;
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)26ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 16, t46);
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 17, t47);
    gm_ta8d7579em356 t48 = t29;
    uint8_t t49 = 0;
    gm_t9qkh0rsooc8d t50 = t28;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 0, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 1, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 2, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 3, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 4, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 5, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 6, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 7, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 8, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 9, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 10, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 11, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 12, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 13, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 14, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 15, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 16, ((uint8_t *)&t50))) t49 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t48) + 17, ((uint8_t *)&t50))) t49 = 1;
    uint8_t t51 = (uint8_t)(t49);
    t1 = t51;
    goto me2;
mn25:;
ma5:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t53 != ((uint8_t)8ULL)) goto mn52;
    gm_t9qkh0rsooc8d t54 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t54;
    gm_t9qkh0rsooc8d t55 = gm_l2;
    gm_t9qkh0rsooc8d t56;
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)61ULL));
    gm_t9qkh0rsooc8d t57;
    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)63ULL));
    gm_t9qkh0rsooc8d t58;
    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)58ULL));
    gm_t9qkh0rsooc8d t59;
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t60;
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)9ULL));
    gm_t9qkh0rsooc8d t61;
    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)7ULL));
    gm_t9qkh0rsooc8d t62;
    GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)46ULL));
    gm_t9qkh0rsooc8d t63;
    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t64;
    GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t65;
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)53ULL));
    gm_tfh78knfg7urq t66;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 0), t56);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 1), t57);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 2), t58);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 3), t59);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 4), t60);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 5), t61);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 6), t62);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 7), t63);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 8), t64);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 9), t65);
    gm_tfh78knfg7urq t67 = t66;
    uint8_t t68 = 0;
    gm_t9qkh0rsooc8d t69 = t55;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 0, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 1, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 2, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 3, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 4, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 5, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 6, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 7, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 8, ((uint8_t *)&t69))) t68 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t67) + 9, ((uint8_t *)&t69))) t68 = 1;
    uint8_t t70 = (uint8_t)(t68);
    t1 = t70;
    goto me2;
mn52:;
ma6:;
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t74 != ((uint8_t)9ULL)) goto pa73;
    goto po72;
pa73:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t76 != ((uint8_t)10ULL)) goto pa75;
    goto po72;
pa75:;
    goto mn71;
po72:;
    t1 = ((uint8_t)0);
    goto me2;
mn71:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 5, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_expr_rmia9bs(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_type_a3rjrm7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma3; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)0ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)7ULL)) goto pa13;
    goto po10;
pa13:;
    goto mn9;
po10:;
    t1 = ((uint8_t)1);
    goto me2;
mn9:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t17;
    gm_t9qkh0rsooc8d t18 = gm_l1;
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)27ULL));
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)6ULL));
    gm_t8d5d2ep1fo6t t23;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 0), t19);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t20);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 2), t21);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 3), t22);
    gm_t8d5d2ep1fo6t t24 = t23;
    uint8_t t25 = 0;
    gm_t9qkh0rsooc8d t26 = t18;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t24) + 0, ((uint8_t *)&t26))) t25 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t24) + 1, ((uint8_t *)&t26))) t25 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t24) + 2, ((uint8_t *)&t26))) t25 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t24) + 3, ((uint8_t *)&t26))) t25 = 1;
    uint8_t t27 = (uint8_t)(t25);
    t1 = t27;
    goto me2;
mn15:;
ma5:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t29 != ((uint8_t)8ULL)) goto mn28;
    gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t30;
    gm_t9qkh0rsooc8d t31 = gm_l2;
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)63ULL));
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)61ULL));
    gm_t8d5d2ep1fo6t t36;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 0), t32);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t33);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 2), t34);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 3), t35);
    gm_t8d5d2ep1fo6t t37 = t36;
    uint8_t t38 = 0;
    gm_t9qkh0rsooc8d t39 = t31;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 0, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 1, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 2, ((uint8_t *)&t39))) t38 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t37) + 3, ((uint8_t *)&t39))) t38 = 1;
    uint8_t t40 = (uint8_t)(t38);
    t1 = t40;
    goto me2;
mn28:;
ma6:;
    t1 = ((uint8_t)0);
    goto me2;
mn41:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 23, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_type_a3rjrm7(&a0); }
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_op_9gj7im4 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; default: goto ma11; }
ma3:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)16ULL)) goto mn14;
    gm_ts6oqppjh0ba3 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)12ULL));
    gm_tt7vn9ehj0acr t18;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[2] = 1;
    t1 = t18;
    goto me2;
mn14:;
ma4:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)2ULL)) goto mn19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)15ULL)) goto mn19;
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)14ULL));
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn19:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)8ULL)) goto mn24;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t26 != ((uint8_t)67ULL)) goto mn24;
    gm_ts6oqppjh0ba3 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)13ULL));
    gm_tt7vn9ehj0acr t28;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[2] = 1;
    t1 = t28;
    goto me2;
mn24:;
ma6:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t30 != ((uint8_t)8ULL)) goto mn29;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t31 != ((uint8_t)68ULL)) goto mn29;
    gm_ts6oqppjh0ba3 t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)15ULL));
    gm_tt7vn9ehj0acr t33;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t33), t32);
    ((uint8_t *)&t33)[2] = 1;
    t1 = t33;
    goto me2;
mn29:;
ma7:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t35 != ((uint8_t)8ULL)) goto mn34;
    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t36 != ((uint8_t)41ULL)) goto mn34;
    gm_ts6oqppjh0ba3 t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)16ULL));
    gm_tt7vn9ehj0acr t38;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t38), t37);
    ((uint8_t *)&t38)[2] = 1;
    t1 = t38;
    goto me2;
mn34:;
ma8:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t40 != ((uint8_t)8ULL)) goto mn39;
    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t41 != ((uint8_t)42ULL)) goto mn39;
    gm_ts6oqppjh0ba3 t42;
    GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)17ULL));
    gm_tt7vn9ehj0acr t43;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43), t42);
    ((uint8_t *)&t43)[2] = 1;
    t1 = t43;
    goto me2;
mn39:;
ma9:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)8ULL)) goto mn44;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t46 != ((uint8_t)43ULL)) goto mn44;
    gm_ts6oqppjh0ba3 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
    gm_tt7vn9ehj0acr t48;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t48), t47);
    ((uint8_t *)&t48)[2] = 1;
    t1 = t48;
    goto me2;
mn44:;
ma10:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t50 != ((uint8_t)8ULL)) goto mn49;
    uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t51 != ((uint8_t)44ULL)) goto mn49;
    gm_ts6oqppjh0ba3 t52;
    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)19ULL));
    gm_tt7vn9ehj0acr t53;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t53), t52);
    ((uint8_t *)&t53)[2] = 1;
    t1 = t53;
    goto me2;
mn49:;
ma11:;
    gm_tt7vn9ehj0acr t55;
    memset(&t55, 0, sizeof t55);
    t1 = t55;
    goto me2;
mn54:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 33, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0) { return gm_f_cmp_op_9gj7im4(&a0); }
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_deny_in_head_06d9o22 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 128) + 0));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t7));
            gm_b16_8 t9 = gm_l1;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_3nkc0o4((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_src0dtioet3p4f6, 589, 52);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_b0_1 t16 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t6, t8, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
            gm_t9qkh0rsooc8d t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b0_1 t20;
        gm_t9qkh0rsooc8d t21;
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t21 + 1), t20);
        gm_t9qkh0rsooc8d t22 = t21;
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
inline __attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_label_7eokqmm F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* sym */
    GM_STACK_CHECK_SHALLOW(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t3fcbuehh3bs2 t4;
        gm_f_kind_k239kpg((void *)&t4, t3);
        gm_t3fcbuehh3bs2 t5 = t4;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l1 = t7;
        {
            gm_b16_8 t10 = gm_l1;
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 88) + 0));
            gm_b16_8 t13 = t12;
            uint8_t t14 = 0;
            gm_b16_8 t15 = t10;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t13) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t13))) + i * 16, ((uint8_t *)&t15))) { t14 = 1; break; }
            uint8_t t16 = (uint8_t)(!t14);
            if (!t16) goto ie9;
            {
                gm_t1s75i08qtptd t18;
                memset(&t18, 0, sizeof t18);
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix9;
ie9:;
ix9:;
        }
        {
            gm_t08olq97rscn3 t20;
            memset((uint8_t *)&t20 + 28, 0, 4);
            gm_b16_8 t21 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
            void * t22 = gm_l0;
            gm_tm4pc0emhrv1g t23;
            gm_f_bump_q2i40re((void *)&t23, t22);
            gm_tm4pc0emhrv1g t24 = t23;
            gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t24) + 32));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t20 + 16, t25);
            gm_t1s75i08qtptd t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t26), t20);
            ((uint8_t *)&t26)[32] = 1;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t1s75i08qtptd t29;
            memset(&t29, 0, sizeof t29);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0) { gm_t1s75i08qtptd gm_r; gm_f_eat_label_7eokqmm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ends_expr_after_name_5t7p9lg F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t6 != ((uint8_t)0ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t8 != ((uint8_t)3ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)4ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)5ULL)) goto pa11;
    goto po4;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t15 != ((uint8_t)19ULL)) goto pa13;
    goto po4;
pa13:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t18 != ((uint8_t)25ULL)) goto pa16;
    goto po4;
pa16:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)2ULL)) goto pa19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)10ULL)) goto pa19;
    goto po4;
pa19:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn22:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 787, 60);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0) { return gm_f_ends_expr_after_name_5t7p9lg(&a0); }
extern __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_arith_l6b4tpf F;
    void * gm_l0; /* e */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t5 != ((uint8_t)13ULL)) goto mn4;
    gm_ts6oqppjh0ba3 t6 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t6;
    uint8_t t7 = 0;
    uint8_t t8 = 0;
    uint8_t t9 = 0;
    uint8_t t10;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    t10 = ((uint8_t)1);
    goto me11;
mn12:;
    t10 = ((uint8_t)0);
    goto me11;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 26);
me11:;
    if (!t10) {
        uint8_t t15;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t18 != ((uint8_t)1ULL)) goto mn17;
        t15 = ((uint8_t)1);
        goto me16;
mn17:;
        t15 = ((uint8_t)0);
        goto me16;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 43);
me16:;
        t9 = t15;
    } else t9 = 1;
    if (!t9) {
        uint8_t t20;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t23 != ((uint8_t)2ULL)) goto mn22;
        t20 = ((uint8_t)1);
        goto me21;
mn22:;
        t20 = ((uint8_t)0);
        goto me21;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 60);
me21:;
        t8 = t20;
    } else t8 = 1;
    if (!t8) {
        gm_ts6oqppjh0ba3 t25 = gm_l1;
        gm_ts6oqppjh0ba3 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)4ULL));
        gm_ts6oqppjh0ba3 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        gm_t9ldr1q5hn9fq t28;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 0), t26);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 2), t27);
        gm_t9ldr1q5hn9fq t29 = t28;
        uint8_t t30 = 0;
        gm_ts6oqppjh0ba3 t31 = t25;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t29) + 0, ((uint8_t *)&t31))) t30 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t29) + 2, ((uint8_t *)&t31))) t30 = 1;
        uint8_t t32 = (uint8_t)(t30);
        t7 = t32;
    } else t7 = 1;
    t2 = t7;
    goto me3;
mn4:;
    t2 = ((uint8_t)0);
    goto me3;
mn33:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 99, 47);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_relational_n3goics F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 488u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t3fcbuehh3bs2 t1 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)a0));
    gm_t72hb7duiv9b4 t2;
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)41ULL));
    gm_t3fcbuehh3bs2 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t4 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t4 + 8), t3);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 0, t4);
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)42ULL));
    gm_t3fcbuehh3bs2 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t6 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 8), t5);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 32, t6);
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)43ULL));
    gm_t3fcbuehh3bs2 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t8 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8 + 8), t7);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 64, t8);
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)44ULL));
    gm_t3fcbuehh3bs2 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 8), t9);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 96, t10);
    gm_t72hb7duiv9b4 t11 = t2;
    uint8_t t12 = 0;
    gm_t3fcbuehh3bs2 t13 = t1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 0, ((uint8_t *)&t13))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 32, ((uint8_t *)&t13))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 64, ((uint8_t *)&t13))) t12 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 96, ((uint8_t *)&t13))) t12 = 1;
    uint8_t t14 = (uint8_t)(t12);
    F.ret = t14;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0) { return gm_f_is_relational_n3goics(&a0); }
inline __attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_mul_from_mlskgvm F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 877u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tc3oht4ksth9d t2 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
        F.l2 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l0;
            gm_t3fcbuehh3bs2 t5;
            gm_f_kind_k239kpg((void *)&t5, t4);
            gm_tt7vn9ehj0acr t6 = gm_f_mul_op_ab6loj8(&t5);
            gm_tt7vn9ehj0acr t7 = t6;
            if (!((((uint8_t *)&t7))[2] != 0)) goto lb3;
            gm_ts6oqppjh0ba3 t8 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t7));
            gm_l3 = t8;
            {
                {
                    void * t12 = gm_l0;
                    gm_tm4pc0emhrv1g t13;
                    gm_f_bump_q2i40re((void *)&t13, t12);
                }
            }
            {
                void * t14 = gm_l0;
                gm_to1cssm8dfiv5 t15;
                gm_f_parse_prefix_671fi6d((void *)&t15, (void *)NULL, cx, NULL, t14);
                if (gm_unw.kind) {
                    goto gm_ll9;
                }
                gm_to1cssm8dfiv5 t16 = t15;
                if (((((uint8_t *)&t16))[0] != 0)) {
                    gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
                    gm_to1cssm8dfiv5 t18;
                    memset(&t18, 0, sizeof t18);
                    ((uint8_t *)&t18)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
                    F.ret = t18;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t19 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t16) + 8));
                F.l4 = t19;
            }
            {
                void * t20 = gm_l0;
                gm_ts6oqppjh0ba3 t21 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t20, t21, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll9;
                }
            }
lc3:;
        }
gm_ll9:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
    }
    {
        gm_tc3oht4ksth9d t22 = F.l2;
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mul_op_ab6loj8 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; default: goto ma9; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t13 != ((uint8_t)8ULL)) goto mn12;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn12;
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16 + 1), t15);
    gm_tt7vn9ehj0acr t17;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t17), t16);
    ((uint8_t *)&t17)[2] = 1;
    t1 = t17;
    goto me2;
mn12:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t19 != ((uint8_t)8ULL)) goto mn18;
    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t20 != ((uint8_t)12ULL)) goto mn18;
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t21);
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn18:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)8ULL)) goto mn24;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t26 != ((uint8_t)16ULL)) goto mn24;
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 1), t27);
    gm_tt7vn9ehj0acr t29;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[2] = 1;
    t1 = t29;
    goto me2;
mn24:;
ma6:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t31 != ((uint8_t)8ULL)) goto mn30;
    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t32 != ((uint8_t)20ULL)) goto mn30;
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 1), t33);
    gm_tt7vn9ehj0acr t35;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[2] = 1;
    t1 = t35;
    goto me2;
mn30:;
ma7:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t37 != ((uint8_t)8ULL)) goto mn36;
    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t38 != ((uint8_t)3ULL)) goto mn36;
    gm_ts6oqppjh0ba3 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)4ULL));
    gm_tt7vn9ehj0acr t40;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t40), t39);
    ((uint8_t *)&t40)[2] = 1;
    t1 = t40;
    goto me2;
mn36:;
ma8:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t42 != ((uint8_t)8ULL)) goto mn41;
    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t43 != ((uint8_t)4ULL)) goto mn41;
    gm_ts6oqppjh0ba3 t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)5ULL));
    gm_tt7vn9ehj0acr t45;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t45), t44);
    ((uint8_t *)&t45)[2] = 1;
    t1 = t45;
    goto me2;
mn41:;
ma9:;
    gm_tt7vn9ehj0acr t47;
    memset(&t47, 0, sizeof t47);
    t1 = t47;
    goto me2;
mn46:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 73, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0) { return gm_f_mul_op_ab6loj8(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_text_3o86dat F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    gm_t9qkh0rsooc8d gm_l2; /* kw */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)8ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_b16_8 t12 = gm_f_punct_text_ov4r40u(t11);
    t1 = t12;
    goto me2;
mn8:;
ma4:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t15;
    gm_t9qkh0rsooc8d t16 = gm_l2;
    gm_b16_8 t17 = gm_f_keyword_text_149oqn4(t16);
    t1 = t17;
    goto me2;
mn13:;
ma5:;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
    t1 = t19;
    goto me2;
mn18:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 252, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0) { return gm_f_op_text_3o86dat(&a0); }
inline __attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parens_0pje0fd F;
    uint32_t gm_l0; /* file */
    uint32_t gm_l1; /* lo */
    uint32_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 12, 0, 4);
        uint32_t t2 = gm_l0;
        uint32_t t3 = gm_l1;
        uint32_t t4 = gm_l1;
        gm_tl874t2atnlh3 t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t3);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 0, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_smooaouqmm8dj));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 16, t6);
        memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 12, 0, 4);
        uint32_t t7 = gm_l0;
        uint32_t t8 = gm_l2;
        uint32_t t9 = gm_l2;
        gm_tl874t2atnlh3 t10;
        GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t7);
        GM_ST(uint32_t, ((uint8_t *)&t10 + 4), t8);
        GM_ST(uint32_t, ((uint8_t *)&t10 + 8), t9);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 0, t10);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s1asl1e1tvb0k));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 16, t11);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2) { gm_t0vh9i8m50f41 gm_r; gm_f_parens_0pje0fd(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_add_dh9nvbt F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        uint8_t t11 = gm_f_binary_follows_jeb42bi(&t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            gm_tc3oht4ksth9d t14 = F.l1;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t18 = gm_l0;
        gm_f_add_from_7os0rvi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t18, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_arg_t6fq0nq F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    GM_STACK_CHECK(&F, 4021u);
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
        void * t5 = gm_l0;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)50ULL));
        uint8_t t7 = gm_f_at_54906rm(t5, t6);
        if (!t7) goto ie4;
        {
            {
                void * t10 = gm_l0;
                gm_tm4pc0emhrv1g t11;
                gm_f_bump_q2i40re((void *)&t11, t10);
            }
        }
        {
            void * t12 = gm_l0;
            gm_to1cssm8dfiv5 t13;
            gm_f_parse_expr_bkb2n6e((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) {
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
                gm_t3q68r4ne5jv8 t16;
                memset(&t16, 0, sizeof t16);
                ((uint8_t *)&t16)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
                F.ret = t16;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t17 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t14) + 8));
            F.l2 = t17;
        }
        {
            gm_t7te2tdpebbt4 t18;
            memset((uint8_t *)&t18 + 140, 0, 4);
            gm_tc3oht4ksth9d t19 = F.l2;
            gm_t22mtv3sd9prf t20;
            memset(&t20, 0, sizeof t20);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t18 + 0, t20);
            void * t21 = gm_l0;
            uint32_t t22 = gm_l1;
            gm_tl874t2atnlh3 t23 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t21, t22));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t18 + 128, t23);
            gm_t3q68r4ne5jv8 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            ((uint8_t *)&t24)[0] = 0;
            GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t24 + 8), t18);
            gm_t3q68r4ne5jv8 t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t28 = gm_l0;
        uint8_t t29 = gm_f_nth_is_name_oqhoobd(t28, ((uint64_t)0ULL));
        if (!t29) goto ie27;
        void * t30 = gm_l0;
        gm_t9qkh0rsooc8d t31;
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)56ULL));
        uint8_t t32 = gm_f_nth_is_3fgqmcb(t30, ((uint64_t)1ULL), t31);
        if (!t32) goto ie27;
        {
            void * t35 = gm_l0;
            gm_t3fcbuehh3bs2 t36;
            gm_f_kind_k239kpg((void *)&t36, t35);
            gm_t3fcbuehh3bs2 t37 = t36;
            uint8_t t38 = ((GM_LD(uint8_t, ((uint8_t *)&t37) + 0) == ((uint8_t)1ULL)));
            if (!t38) goto ie34;
            {
                void * t40 = gm_l0;
                void * t41 = gm_l0;
                gm_tl874t2atnlh3 t42 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t41));
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s4niue72anpss));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(36));
                gm_b0_1 t44 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t40, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            void * t45 = gm_l0;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s3tpa8q2q7llg));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(16));
            gm_t4jn8fl3lmk5k t47;
            gm_f_parse_name_figjvbe((void *)&t47, (void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) {
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 8));
                gm_t3q68r4ne5jv8 t50;
                memset(&t50, 0, sizeof t50);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t50 + 8), t49);
                F.ret = t50;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t51 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t48) + 8));
            gm_l3 = t51;
        }
        {
            {
                void * t53 = gm_l0;
                gm_tm4pc0emhrv1g t54;
                gm_f_bump_q2i40re((void *)&t54, t53);
            }
        }
        {
            void * t55 = gm_l0;
            gm_to1cssm8dfiv5 t56;
            gm_f_parse_expr_bkb2n6e((void *)&t56, (void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) {
                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 8));
                gm_t3q68r4ne5jv8 t59;
                memset(&t59, 0, sizeof t59);
                ((uint8_t *)&t59)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t59 + 8), t58);
                F.ret = t59;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t60 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t57) + 8));
            F.l4 = t60;
        }
        {
            gm_t7te2tdpebbt4 t61;
            memset((uint8_t *)&t61 + 140, 0, 4);
            gm_t56mj5hnm2uc6 t62;
            memcpy((uint8_t *)&t62 + 0, ((uint8_t *)&gm_l3), 32);
            memcpy((uint8_t *)&t62 + 32, ((uint8_t *)&F.l4), 88);
            gm_t22mtv3sd9prf t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t63 + 8), t62);
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t61 + 0, t63);
            void * t64 = gm_l0;
            uint32_t t65 = gm_l1;
            gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t64, t65));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t61 + 128, t66);
            gm_t3q68r4ne5jv8 t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            ((uint8_t *)&t67)[0] = 0;
            GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t67 + 8), t61);
            gm_t3q68r4ne5jv8 t68 = t67;
            uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
            F.ret = t67;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t70 = gm_l0;
        gm_to1cssm8dfiv5 t71;
        gm_f_parse_expr_bkb2n6e((void *)&t71, (void *)NULL, cx, NULL, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t72 = t71;
        if (((((uint8_t *)&t72))[0] != 0)) {
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 8));
            gm_t3q68r4ne5jv8 t74;
            memset(&t74, 0, sizeof t74);
            ((uint8_t *)&t74)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t74 + 8), t73);
            F.ret = t74;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t75 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t72) + 8));
        F.l5 = t75;
    }
    {
        gm_t7te2tdpebbt4 t76;
        memset((uint8_t *)&t76 + 140, 0, 4);
        gm_tc3oht4ksth9d t77 = F.l5;
        gm_t22mtv3sd9prf t78;
        memset(&t78, 0, sizeof t78);
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t78 + 8), t77);
        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t76 + 0, t78);
        void * t79 = gm_l0;
        uint32_t t80 = gm_l1;
        gm_tl874t2atnlh3 t81 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t79, t80));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t76 + 128, t81);
        gm_t3q68r4ne5jv8 t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        ((uint8_t *)&t82)[0] = 0;
        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&t82 + 8), t76);
        gm_t3q68r4ne5jv8 t83 = t82;
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
inline __attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_bang_irmkots F;
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 1019u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        void * t4 = F.l0;
        gm_t3fcbuehh3bs2 t5;
        gm_f_nth_c0r2nvs((void *)&t5, t4, ((uint64_t)1ULL));
        uint8_t t6 = gm_f_can_begin_type_a3rjrm7(&t5);
        if (!t6) goto ie3;
        {
            void * t8 = F.l0;
            gm_closure t9 = ((gm_closure){(void *)gm_f_fn_1s091rv, (void *)&F.h, cx});
            gm_f_speculate_djd8kc6((void *)&F.l3, (void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&F.l3))[88] != 0)) goto ie10;
            gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
            F.l4 = t11;
            {
                void * t13 = F.l0;
                void * t14 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t15 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3mf58gv9kf3n t16;
                GM_ST(void *, ((uint8_t *)&t16 + 0), t14);
                GM_ST(void *, ((uint8_t *)&t16 + 8), t15);
                gm_t2gtbh39ptekd t17;
                memset(&t17, 0, sizeof t17);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)17ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t17 + 8), t16);
                void * t18 = F.l0;
                uint32_t t19 = gm_l2;
                gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
                gm_tc3oht4ksth9d t21;
                gm_f_mk_expr_53qj7nd((void *)&t21, t13, &t17, GM_CONV(gm_cseahvg8v7p0vs, t20));
                gm_to1cssm8dfiv5 t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                ((uint8_t *)&t22)[0] = 0;
                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t22 + 8), t21);
                gm_to1cssm8dfiv5 t23 = t22;
                uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
                F.ret = t22;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        {
            void * t26 = F.l0;
            gm_tm4pc0emhrv1g t27;
            gm_f_bump_q2i40re((void *)&t27, t26);
        }
    }
    {
        void * t28 = F.l0;
        void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
        void * t31 = F.l0;
        uint32_t t32 = gm_l2;
        gm_tl874t2atnlh3 t33 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t31, t32));
        gm_tc3oht4ksth9d t34;
        gm_f_mk_expr_53qj7nd((void *)&t34, t28, &t30, GM_CONV(gm_cseahvg8v7p0vs, t33));
        gm_to1cssm8dfiv5 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t35 + 8), t34);
        gm_to1cssm8dfiv5 t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
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
inline __attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_bit_h83vs72 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        uint8_t t11 = gm_f_binary_follows_jeb42bi(&t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            gm_tc3oht4ksth9d t14 = F.l1;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t18 = gm_l0;
        gm_f_bit_from_pu2bq19((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t18, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_block_expr_0j6b78q F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 775u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saojtqmfeak72));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t9 = gm_l0;
        gm_toc4mrkn2e7o9 t10;
        gm_f_parse_block_tpg3f1i((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toc4mrkn2e7o9 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 8));
            gm_to1cssm8dfiv5 t13;
            memset(&t13, 0, sizeof t13);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), t12);
            F.ret = t13;
            goto gm_fret;
        }
        gm_t79rbdv4rafe5 t14 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&t11) + 8));
        F.l1 = t14;
    }
    {
        void * t15 = gm_l0;
        gm_t79rbdv4rafe5 t16 = F.l1;
        gm_t2gtbh39ptekd t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)44ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t17 + 8), t16);
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
        gm_tc3oht4ksth9d t19;
        gm_f_mk_expr_53qj7nd((void *)&t19, t15, &t17, GM_CONV(gm_cseahvg8v7p0vs, t18));
        gm_to1cssm8dfiv5 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        ((uint8_t *)&t20)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t20 + 8), t19);
        gm_to1cssm8dfiv5 t21 = t20;
        uint8_t t22 = ((((uint8_t *)&t21))[0] != 0);
        F.ret = t20;
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
inline __attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_break_hehputf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    void * gm_l4; /* value */
    GM_STACK_CHECK(&F, 1458u);
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
        void * t7 = gm_l0;
        gm_f_eat_label_7eokqmm((void *)&F.l2, t7);
    }
    {
        uint8_t t9;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 773, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t15 = gm_l0;
            gm_t3fcbuehh3bs2 t16;
            gm_f_kind_k239kpg((void *)&t16, t15);
            gm_t3fcbuehh3bs2 t17 = t16;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
            if (t18 != ((uint8_t)0ULL)) goto ie14;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_l3 = t19;
            {
                void * t22 = gm_l0;
                gm_t3fcbuehh3bs2 t23;
                gm_f_nth_c0r2nvs((void *)&t23, t22, ((uint64_t)1ULL));
                uint8_t t24 = gm_f_ends_expr_after_name_5t7p9lg(&t23);
                if (!t24) goto ie21;
                {
                    void * t26 = gm_l0;
                    void * t27 = gm_l0;
                    gm_tl874t2atnlh3 t28 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t27));
                    gm_b16_8 t29 = gm_l3;
                    gm_tnpifv20irnph t30;
                    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                    gm_tabb710d9mq2e t31;
                    gm_f_format_pb6f6q3((void *)&t31, (void *)NULL, cx, NULL, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t32 = t31;
                    if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_src0dtioet3p4f6, 776, 42);
                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
                    gm_b16_8 t34 = t33;
                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                    gm_b0_1 t36 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t26, t28, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_to1cssm8dfiv5 t37;
                    memset(&t37, 0, sizeof t37);
                    ((uint8_t *)&t37)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), t36);
                    gm_to1cssm8dfiv5 t38 = t37;
                    uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
                    F.ret = t37;
                    goto gm_fret;
                }
                goto ix21;
ie21:;
ix21:;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_l4 = ((void *)0);
    }
    {
        void * t41 = gm_l0;
        gm_t3fcbuehh3bs2 t42;
        gm_f_kind_k239kpg((void *)&t42, t41);
        uint8_t t43 = gm_f_can_begin_expr_rmia9bs(&t42);
        if (!t43) goto ie40;
        {
            void * t45 = gm_l0;
            gm_to1cssm8dfiv5 t46;
            gm_f_parse_expr_bkb2n6e((void *)&t46, (void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) {
                gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
                gm_to1cssm8dfiv5 t49;
                memset(&t49, 0, sizeof t49);
                ((uint8_t *)&t49)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t49 + 8), t48);
                F.ret = t49;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t50 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t47) + 8));
            void * t51 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t51;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        void * t52 = gm_l0;
        gm_tchr52g0ema53 t53;
        memcpy((uint8_t *)&t53 + 0, ((uint8_t *)&F.l2), 40);
        void * t54 = gm_l4;
        GM_ST(void *, (uint8_t *)&t53 + 40, t54);
        gm_t2gtbh39ptekd t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)46ULL));
        GM_ST(gm_tchr52g0ema53, ((uint8_t *)&t55 + 8), t53);
        void * t56 = gm_l0;
        uint32_t t57 = gm_l1;
        gm_tl874t2atnlh3 t58 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t56, t57));
        gm_tc3oht4ksth9d t59;
        gm_f_mk_expr_53qj7nd((void *)&t59, t52, &t55, GM_CONV(gm_cseahvg8v7p0vs, t58));
        gm_to1cssm8dfiv5 t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        ((uint8_t *)&t60)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t60 + 8), t59);
        gm_to1cssm8dfiv5 t61 = t60;
        uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
        F.ret = t60;
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
inline __attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_call_args_32e205h F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    uint8_t gm_l3; /* trailing */
    GM_STACK_CHECK(&F, 889u);
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
                gm_thk954m1ol1ig t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
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
        gm_f_list_6oos54v((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_l3 = ((uint8_t)0);
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
                gm_t3q68r4ne5jv8 t26;
                gm_f_parse_arg_t6fq0nq((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
                gm_t3q68r4ne5jv8 t27 = t26;
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
                    goto gm_ll22;
                }
                gm_t7te2tdpebbt4 t30 = GM_LD(gm_t7te2tdpebbt4, (((uint8_t *)&t27) + 8));
                gm_tftb1odc739t5 t31 = gm_f_push_jis5lhu(t24, &t30);
                gm_tftb1odc739t5 t32 = t31;
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src0dtioet3p4f6, 558, 9);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            {
                gm_l3 = ((uint8_t)0);
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
            {
                gm_l3 = ((uint8_t)1);
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
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s5us2hgd173vu));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(26));
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
extern __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cmp_q5clpig F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* start_pos */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    uint8_t gm_l5; /* first_rel */
    gm_tl874t2atnlh3 gm_l8; /* at */
    uint8_t gm_l10; /* negated */
    gm_th2lqbo77pvsc gm_l11; /* fix */
    gm_t08olq97rscn3 gm_l12; /* mid */
    int32_t gm_l13; /* count */
    gm_b16_8 gm_l14; /* text */
    GM_STACK_CHECK(&F, 3712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 72));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_to1cssm8dfiv5 t5;
        gm_f_parse_prefix_671fi6d((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_to1cssm8dfiv5 t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        void * t11 = gm_l0;
        gm_t3fcbuehh3bs2 t12;
        gm_f_kind_k239kpg((void *)&t12, t11);
        uint8_t t13 = gm_f_binary_follows_jeb42bi(&t12);
        uint8_t t14 = (uint8_t)!t13;
        if (!t14) goto ie10;
        {
            gm_tc3oht4ksth9d t16 = F.l2;
            gm_to1cssm8dfiv5 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t17 + 8), t16);
            gm_to1cssm8dfiv5 t18 = t17;
            uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t20 = gm_l0;
        gm_to1cssm8dfiv5 t21;
        gm_f_range_from_5v6ko9m((void *)&t21, (void *)NULL, cx, NULL, t20, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t22 = t21;
        if (((((uint8_t *)&t22))[0] != 0)) {
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 8));
            gm_to1cssm8dfiv5 t24;
            memset(&t24, 0, sizeof t24);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), t23);
            F.ret = t24;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t25 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t22) + 8));
        F.l3 = t25;
    }
    {
        void * t26 = gm_l0;
        gm_t3fcbuehh3bs2 t27;
        gm_f_kind_k239kpg((void *)&t27, t26);
        gm_tt7vn9ehj0acr t28 = gm_f_cmp_op_9gj7im4(&t27);
        gm_tt7vn9ehj0acr t29 = t28;
        gm_ts6oqppjh0ba3 t30;
        if (((((uint8_t *)&t29))[2] != 0)) {
            gm_ts6oqppjh0ba3 t31 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&t29) + 0));
            t30 = t31;
        } else {
            gm_tc3oht4ksth9d t32 = F.l3;
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
        gm_l4 = t30;
    }
    {
        void * t36 = gm_l0;
        gm_t3fcbuehh3bs2 t37;
        gm_f_kind_k239kpg((void *)&t37, t36);
        uint8_t t38 = gm_f_is_relational_n3goics(&t37);
        gm_l5 = t38;
    }
    {
        {
            void * t40 = gm_l0;
            gm_tm4pc0emhrv1g t41;
            gm_f_bump_q2i40re((void *)&t41, t40);
        }
    }
    {
        void * t42 = gm_l0;
        gm_to1cssm8dfiv5 t43;
        gm_f_parse_range_o9o2al7((void *)&t43, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t44 = t43;
        if (((((uint8_t *)&t44))[0] != 0)) {
            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 8));
            gm_to1cssm8dfiv5 t46;
            memset(&t46, 0, sizeof t46);
            ((uint8_t *)&t46)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t46 + 8), t45);
            F.ret = t46;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t47 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t44) + 8));
        F.l6 = t47;
    }
    {
        void * t48 = gm_l0;
        gm_ts6oqppjh0ba3 t49 = gm_l4;
        gm_f_binary_41fr5r5((void *)&F.l7, (void *)NULL, cx, NULL, t48, t49, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t51 = gm_l0;
        gm_t3fcbuehh3bs2 t52;
        gm_f_kind_k239kpg((void *)&t52, t51);
        gm_tt7vn9ehj0acr t53 = gm_f_cmp_op_9gj7im4(&t52);
        gm_tt7vn9ehj0acr t54 = t53;
        uint8_t t55;
        if (((((uint8_t *)&t54))[2] != 0)) goto mn57;
        t55 = ((uint8_t)1);
        goto me56;
mn57:;
        t55 = ((uint8_t)0);
        goto me56;
mn58:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 225, 8);
me56:;
        if (!t55) goto ie50;
        {
            gm_tc3oht4ksth9d t60 = F.l7;
            gm_to1cssm8dfiv5 t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            ((uint8_t *)&t61)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t61 + 8), t60);
            gm_to1cssm8dfiv5 t62 = t61;
            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
            F.ret = t61;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        void * t64 = gm_l0;
        gm_tl874t2atnlh3 t65 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t64));
        gm_l8 = t65;
    }
    {
        gm_tl874t2atnlh3 t66 = gm_l8;
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s5lrfo6g7933d));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(49));
        gm_f_error_s6hn871((void *)&F.l9, (void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t68 = 0;
        uint64_t t69 = gm_l1;
        uint8_t t70 = (t69 > ((uint64_t)0ULL));
        if (t70) {
            uint64_t t71 = gm_l1;
            uint64_t r72; bool o73 = __builtin_sub_overflow(t71, ((uint64_t)1ULL), &r72);
            if (o73) gm_trap("integer overflow in `-`", gm_src0dtioet3p4f6, 231, 41);
            uint64_t t74 = r72;
            uint64_t t75 = (uintptr_t)t74;
            void * t76 = gm_l0;
            uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)t76) + 8) + 8);
            if (t75 >= (uintptr_t)(t77)) gm_trap_index(t75, t77, gm_src0dtioet3p4f6, 231, 34);
            gm_t3fcbuehh3bs2 t78 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, (((uint8_t *)t76) + 8)) + t75 * 48) + 0));
            gm_t3fcbuehh3bs2 t79 = t78;
            gm_t9qkh0rsooc8d t80;
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)18ULL));
            gm_t9qkh0rsooc8d t81 = t80;
            uint8_t t82 = (((GM_LD(uint8_t, ((uint8_t *)&t79) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t79) + 8), ((uint8_t *)&t81))));
            t68 = t82;
        } else t68 = 0;
        gm_l10 = t68;
    }
    {
        gm_th2lqbo77pvsc t83;
        memset(&t83, 0, sizeof t83);
        gm_l11 = t83;
    }
    {
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
        if (t85 != ((uint8_t)6ULL)) goto ie84;
        gm_t08olq97rscn3 t86 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l6) + 8) + 8));
        gm_l12 = t86;
        {
            uint8_t t89 = gm_l5;
            if (!t89) goto ie88;
            void * t90 = gm_l0;
            gm_t3fcbuehh3bs2 t91;
            gm_f_kind_k239kpg((void *)&t91, t90);
            uint8_t t92 = gm_f_is_relational_n3goics(&t91);
            if (!t92) goto ie88;
            uint8_t t93 = gm_l10;
            uint8_t t94 = (uint8_t)!t93;
            if (!t94) goto ie88;
            {
                gm_t6j6smj7qoctu t96;
                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t96 + 0, t97);
                void * t98 = gm_l0;
                gm_t3fcbuehh3bs2 t99;
                gm_f_kind_k239kpg((void *)&t99, t98);
                gm_b16_8 t100 = gm_f_op_text_3o86dat(&t99);
                GM_ST(gm_b16_8, (uint8_t *)&t96 + 16, t100);
                gm_tabb710d9mq2e t101;
                gm_f_format_5k9iv40((void *)&t101, (void *)NULL, cx, NULL, &t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t102 = t101;
                if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 8, gm_src0dtioet3p4f6, 235, 19);
                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&t102) + 8));
                gm_b16_8 t104 = t103;
                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                gm_th2lqbo77pvsc t106;
                GM_ST(uint8_t, (uint8_t *)&t106 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t106 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t106 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t106), t105);
                ((uint8_t *)&t106)[16] = 1;
                gm_l11 = t106;
            }
            goto ix88;
ie88:;
ix88:;
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        gm_l13 = ((int32_t)0LL);
    }
    {
        for (;;) {
            void * t108 = gm_l0;
            gm_t3fcbuehh3bs2 t109;
            gm_f_kind_k239kpg((void *)&t109, t108);
            gm_tt7vn9ehj0acr t110 = gm_f_cmp_op_9gj7im4(&t109);
            gm_tt7vn9ehj0acr t111 = t110;
            uint8_t t112;
            if (((((uint8_t *)&t111))[2] != 0)) goto mn114;
            t112 = ((uint8_t)0);
            goto me113;
mn114:;
            t112 = ((uint8_t)1);
            goto me113;
mn115:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 239, 11);
me113:;
            if (!t112) goto lb107;
            {
                {
                    void * t119 = gm_l0;
                    gm_tm4pc0emhrv1g t120;
                    gm_f_bump_q2i40re((void *)&t120, t119);
                }
            }
            {
                {
                    void * t122 = gm_l0;
                    gm_to1cssm8dfiv5 t123;
                    gm_f_parse_range_o9o2al7((void *)&t123, (void *)NULL, cx, NULL, t122);
                    if (gm_unw.kind) {
                        goto gm_ll116;
                    }
                    gm_to1cssm8dfiv5 t124 = t123;
                    if (((((uint8_t *)&t124))[0] != 0)) {
                        gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 8));
                        gm_to1cssm8dfiv5 t126;
                        memset(&t126, 0, sizeof t126);
                        ((uint8_t *)&t126)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t126 + 8), t125);
                        F.ret = t126;
                        goto gm_fret;
                    }
                    gm_tc3oht4ksth9d t127 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t124) + 8));
                }
            }
            {
                int32_t t128 = gm_l13;
                int32_t r129; bool o130 = __builtin_add_overflow(t128, ((int32_t)1LL), &r129);
                if (o130) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 242, 9);
                int32_t t131 = r129;
                gm_l13 = t131;
            }
lc107:;
        }
gm_ll116:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb107; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc107; }
        }
        goto gm_lad;
lb107:;
    }
    {
        if (!((((uint8_t *)&gm_l11))[16] != 0)) goto ie132;
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l14 = t133;
        {
            int32_t t136 = gm_l13;
            uint8_t t137 = ((t136 == ((int32_t)1LL)));
            if (!t137) goto ie135;
            {
                void * t139 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t140;
                GM_ST(void *, (uint8_t *)&t140, (void *)(gm_s27pt4vcvtscl));
                GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(23));
                gm_tl874t2atnlh3 t141 = gm_l8;
                gm_b16_8 t142 = gm_l14;
                gm_b0_1 t143 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t139, t140, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix135;
ie135:;
ix135:;
        }
        goto ix132;
ie132:;
ix132:;
    }
    {
        void * t144 = gm_l0;
        gm_f_report_diag_big1nsf(t144, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
    }
    {
        gm_tc3oht4ksth9d t145 = F.l7;
        gm_to1cssm8dfiv5 t146;
        GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
        ((uint8_t *)&t146)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t146 + 8), t145);
        gm_to1cssm8dfiv5 t147 = t146;
        uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
        F.ret = t146;
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
inline __attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_continue_ertnf67 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    GM_STACK_CHECK(&F, 960u);
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
        void * t7 = gm_l0;
        gm_f_eat_label_7eokqmm((void *)&F.l2, t7);
    }
    {
        uint8_t t9;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 796, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t15 = gm_l0;
            gm_t3fcbuehh3bs2 t16;
            gm_f_kind_k239kpg((void *)&t16, t15);
            gm_t3fcbuehh3bs2 t17 = t16;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
            if (t18 != ((uint8_t)0ULL)) goto ie14;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_l3 = t19;
            {
                void * t21 = gm_l0;
                void * t22 = gm_l0;
                gm_tl874t2atnlh3 t23 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t22));
                gm_b16_8 t24 = gm_l3;
                gm_tnpifv20irnph t25;
                GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                gm_tabb710d9mq2e t26;
                gm_f_format_pb6f6q3((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src0dtioet3p4f6, 798, 38);
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
                gm_b16_8 t29 = t28;
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_b0_1 t31 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t21, t23, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to1cssm8dfiv5 t32;
                memset(&t32, 0, sizeof t32);
                ((uint8_t *)&t32)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t32 + 8), t31);
                gm_to1cssm8dfiv5 t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t35 = gm_l0;
        gm_t1s75i08qtptd t36 = F.l2;
        gm_t2gtbh39ptekd t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)47ULL));
        GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t37 + 8), t36);
        void * t38 = gm_l0;
        uint32_t t39 = gm_l1;
        gm_tl874t2atnlh3 t40 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t38, t39));
        gm_tc3oht4ksth9d t41;
        gm_f_mk_expr_53qj7nd((void *)&t41, t35, &t37, GM_CONV(gm_cseahvg8v7p0vs, t40));
        gm_to1cssm8dfiv5 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        ((uint8_t *)&t42)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t42 + 8), t41);
        gm_to1cssm8dfiv5 t43 = t42;
        uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
        F.ret = t42;
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
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_dot_suffix_1fk204i F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* n */
    gm_t9qkh0rsooc8d gm_l5; /* k */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l7; /* l */
    uint32_t gm_l8; /* index */
    uint64_t gm_l9; /* v */
    GM_STACK_CHECK(&F, 2029u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        {
            void * t4 = gm_l0;
            gm_tm4pc0emhrv1g t5;
            gm_f_bump_q2i40re((void *)&t5, t4);
        }
    }
    {
        void * t6 = gm_l0;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t6));
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        gm_t3fcbuehh3bs2 t10 = t9;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; default: goto ma15; }
ma12:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l4 = t20;
        {
            {
                void * t23 = gm_l0;
                gm_tm4pc0emhrv1g t24;
                gm_f_bump_q2i40re((void *)&t24, t23);
            }
        }
        {
            void * t25 = gm_l0;
            gm_tioie2ju3q4ul t26;
            void * t27 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t26 + 0, t27);
            memset((uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t26 + 8)) + 28, 0, 4);
            gm_b16_8 t28 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t26 + 8)) + 0, t28);
            gm_tl874t2atnlh3 t29 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t26 + 8)) + 16, t29);
            gm_t2gtbh39ptekd t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t30 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t30 + 8), t26);
            void * t31 = gm_l0;
            uint32_t t32 = gm_l2;
            gm_tl874t2atnlh3 t33 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t31, t32));
            gm_tc3oht4ksth9d t34;
            gm_f_mk_expr_53qj7nd((void *)&t34, t25, &t30, GM_CONV(gm_cseahvg8v7p0vs, t33));
            gm_to1cssm8dfiv5 t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t35 + 8), t34);
            gm_to1cssm8dfiv5 t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto me11;
mn18:;
ma13:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t39 != ((uint8_t)2ULL)) goto mn38;
        gm_t9qkh0rsooc8d t40 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t10) + 8));
        gm_l5 = t40;
        {
            void * t42 = gm_l0;
            gm_t9qkh0rsooc8d t43 = gm_l5;
            gm_f_keyword_as_name_ipm23p9((void *)&gm_l6, (void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t44 = gm_l0;
            gm_tioie2ju3q4ul t45;
            void * t46 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t45 + 0, t46);
            memcpy((uint8_t *)&t45 + 8, ((uint8_t *)&gm_l6), 32);
            gm_t2gtbh39ptekd t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t47 + 8), t45);
            void * t48 = gm_l0;
            uint32_t t49 = gm_l2;
            gm_tl874t2atnlh3 t50 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t48, t49));
            gm_tc3oht4ksth9d t51;
            gm_f_mk_expr_53qj7nd((void *)&t51, t44, &t47, GM_CONV(gm_cseahvg8v7p0vs, t50));
            gm_to1cssm8dfiv5 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t52 + 8), t51);
            gm_to1cssm8dfiv5 t53 = t52;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t52;
            goto gm_fret;
        }
        goto me11;
mn38:;
ma14:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t56 != ((uint8_t)3ULL)) goto mn55;
        void * t57 = GM_LD(void *, (((uint8_t *)&t10) + 8));
        gm_l7 = t57;
        {
            {
                void * t60 = gm_l0;
                gm_tm4pc0emhrv1g t61;
                gm_f_bump_q2i40re((void *)&t61, t60);
            }
        }
        {
            gm_l8 = ((uint32_t)0ULL);
        }
        {
            void * t63 = gm_l7;
            gm_tf5srh4mp4b3i t64 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)t63) + 0)));
            gm_tf5srh4mp4b3i t65 = t64;
            if (!((((uint8_t *)&t65))[8] != 0)) goto ie62;
            uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)&t65));
            gm_l9 = t66;
            uint64_t t67 = gm_l9;
            uint8_t t68 = (t67 <= ((uint64_t)4294967295ULL));
            if (!t68) goto ie62;
            void * t69 = gm_l7;
            uint8_t t70;
            if ((((((uint8_t *)t69) + 48))[8] != 0)) goto mn72;
            t70 = ((uint8_t)1);
            goto me71;
mn72:;
            t70 = ((uint8_t)0);
            goto me71;
mn73:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 470, 63);
me71:;
            if (!t70) goto ie62;
            {
                uint64_t t75 = gm_l9;
                uint32_t t76 = (uint32_t)t75;
                gm_l8 = t76;
            }
            goto ix62;
ie62:;
            {
                void * t78 = gm_l0;
                gm_tl874t2atnlh3 t79 = gm_l3;
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sbckhsd8r0nr0));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(19));
                gm_b0_1 t81 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t78, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix62:;
        }
        {
            void * t82 = gm_l0;
            gm_tupk62cr6lbdu t83;
            void * t84 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t83 + 0, t84);
            uint32_t t85 = gm_l8;
            GM_ST(uint32_t, (uint8_t *)&t83 + 8, t85);
            gm_tl874t2atnlh3 t86 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t83 + 12, t86);
            gm_t2gtbh39ptekd t87;
            memset(&t87, 0, sizeof t87);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t87 + 8), t83);
            void * t88 = gm_l0;
            uint32_t t89 = gm_l2;
            gm_tl874t2atnlh3 t90 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t88, t89));
            gm_tc3oht4ksth9d t91;
            gm_f_mk_expr_53qj7nd((void *)&t91, t82, &t87, GM_CONV(gm_cseahvg8v7p0vs, t90));
            gm_to1cssm8dfiv5 t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
            ((uint8_t *)&t92)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t92 + 8), t91);
            gm_to1cssm8dfiv5 t93 = t92;
            uint8_t t94 = ((((uint8_t *)&t93))[0] != 0);
            F.ret = t92;
            goto gm_fret;
        }
        goto me11;
mn55:;
ma15:;
        void * t96 = gm_l0;
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s8vhc5kn5i8tv));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(37));
        gm_b0_1 t98 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t96, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t99;
        memset(&t99, 0, sizeof t99);
        ((uint8_t *)&t99)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t99 + 8), t98);
        gm_to1cssm8dfiv5 t100 = t99;
        uint8_t t101 = ((((uint8_t *)&t100))[0] != 0);
        F.ret = t99;
        goto gm_fret;
        goto me11;
mn95:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 458, 5);
me11:;
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
__attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_else_smoqgg4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1997u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_logic_ajejref((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t9 = gm_l0;
        gm_t9qkh0rsooc8d t10;
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)7ULL));
        uint8_t t11 = gm_f_at_kw_p0r3sr9(t9, t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            gm_tc3oht4ksth9d t14 = F.l1;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        {
            void * t19 = gm_l0;
            gm_tm4pc0emhrv1g t20;
            gm_f_bump_q2i40re((void *)&t20, t19);
        }
    }
    {
        gm_t1s75i08qtptd t21;
        memset(&t21, 0, sizeof t21);
        F.l2 = t21;
    }
    {
        void * t23 = gm_l0;
        uint8_t t24 = gm_f_nth_is_name_oqhoobd(t23, ((uint64_t)0ULL));
        if (!t24) goto ie22;
        void * t25 = gm_l0;
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)59ULL));
        uint8_t t27 = gm_f_nth_is_3fgqmcb(t25, ((uint64_t)1ULL), t26);
        if (!t27) goto ie22;
        {
            void * t29 = gm_l0;
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s1pvh8pfrs3ir));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(6));
            gm_t4jn8fl3lmk5k t31;
            gm_f_parse_name_figjvbe((void *)&t31, (void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) {
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 8));
                gm_to1cssm8dfiv5 t34;
                memset(&t34, 0, sizeof t34);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t34 + 8), t33);
                F.ret = t34;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t32) + 8));
            gm_t1s75i08qtptd t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t36), t35);
            ((uint8_t *)&t36)[32] = 1;
            F.l2 = t36;
        }
        {
            {
                void * t38 = gm_l0;
                gm_tm4pc0emhrv1g t39;
                gm_f_bump_q2i40re((void *)&t39, t38);
            }
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t40 = gm_l0;
        uint8_t t41;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn43;
        t41 = ((uint8_t)0);
        goto me42;
mn43:;
        t41 = ((uint8_t)1);
        goto me42;
mn44:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 135, 33);
me42:;
        gm_to1cssm8dfiv5 t45;
        gm_f_parse_fallback_g1vpkra((void *)&t45, (void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) {
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 8));
            gm_to1cssm8dfiv5 t48;
            memset(&t48, 0, sizeof t48);
            ((uint8_t *)&t48)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), t47);
            F.ret = t48;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t49 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t46) + 8));
        F.l3 = t49;
    }
    {
        void * t50 = gm_l0;
        void * t51 = (void *)((uint8_t *)&F.l1);
        void * t52 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t53 = gm_f_ambiguous_try_else_0v14t0i((void *)NULL, cx, NULL, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t54 = gm_l0;
        gm_tioi3jh9v22g5 t55;
        void * t56 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t55 + 0, t56);
        memcpy((uint8_t *)&t55 + 8, ((uint8_t *)&F.l2), 40);
        void * t57 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t55 + 48, t57);
        gm_t2gtbh39ptekd t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)14ULL));
        GM_ST(gm_tioi3jh9v22g5, ((uint8_t *)&t58 + 8), t55);
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 72));
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
        gm_tl874t2atnlh3 t61 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t59), GM_CONV(gm_cseahvg8v7p0vs, t60)));
        gm_tc3oht4ksth9d t62;
        gm_f_mk_expr_53qj7nd((void *)&t62, t54, &t58, GM_CONV(gm_cseahvg8v7p0vs, t61));
        gm_to1cssm8dfiv5 t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
        ((uint8_t *)&t63)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t63 + 8), t62);
        gm_to1cssm8dfiv5 t64 = t63;
        uint8_t t65 = ((((uint8_t *)&t64))[0] != 0);
        F.ret = t63;
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
inline __attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_expr_bkb2n6e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 147u);
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
        gm_f_parse_else_smoqgg4((void *)&F.ret, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t10 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t10;
        goto gm_ld1_1;
    }
    {
        void * t11 = gm_l0;
        gm_f_leave_rb20u32(t11);
    }
    goto gm_ls12;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t13 = gm_l0;
        gm_f_leave_rb20u32(t13);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls12:;
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
inline __attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fallback_g1vpkra F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* captured */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_f_parse_capture_body_fgf89k2((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t7 = gm_l0;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)65ULL));
        uint8_t t9 = gm_f_at_54906rm(t7, t8);
        if (!t9) goto ie6;
        void * t10 = gm_l0;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 128) + 0));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie6;
        {
            void * t14 = gm_l0;
            gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) {
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
            gm_to1cssm8dfiv5 t19;
            memset(&t19, 0, sizeof t19);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), t18);
            F.ret = t19;
            goto gm_fret;
        }
        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
    }
    {
        void * t22 = gm_l0;
        gm_f_parse_else_smoqgg4((void *)&F.ret, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t23 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t23;
        goto gm_ld1_1;
    }
    {
        void * t24 = gm_l0;
        gm_f_leave_rb20u32(t24);
    }
    goto gm_ls25;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t26 = gm_l0;
        gm_f_leave_rb20u32(t26);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls25:;
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
inline __attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_head_expr_6sofdhn F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
    }
    {
    }
    {
        void * t8 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&F.ret, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t9 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t9;
        goto gm_ld1_1;
    }
    {
        {
            void * t11 = gm_l0;
            gm_toh0ootodhjue t12 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t11) + 128), t12);
        }
    }
    goto gm_ls13;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t15 = gm_l0;
            gm_toh0ootodhjue t16 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t15) + 128), t16);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls13:;
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
inline __attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_import_s1hv231 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tibupt4dmcqai gm_l2; /* l */
    GM_STACK_CHECK(&F, 875u);
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
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        gm_t3fcbuehh3bs2 t10 = t9;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t11 != ((uint8_t)5ULL)) goto ie7;
        gm_tibupt4dmcqai t12 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t10) + 8));
        gm_l2 = t12;
        {
            {
                void * t15 = gm_l0;
                gm_tm4pc0emhrv1g t16;
                gm_f_bump_q2i40re((void *)&t16, t15);
            }
        }
        {
            void * t17 = gm_l0;
            gm_tibupt4dmcqai t18 = gm_l2;
            gm_t2gtbh39ptekd t19;
            memset(&t19, 0, sizeof t19);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)38ULL));
            GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t19 + 8), t18);
            void * t20 = gm_l0;
            uint32_t t21 = gm_l1;
            gm_tl874t2atnlh3 t22 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t20, t21));
            gm_tc3oht4ksth9d t23;
            gm_f_mk_expr_53qj7nd((void *)&t23, t17, &t19, GM_CONV(gm_cseahvg8v7p0vs, t22));
            gm_to1cssm8dfiv5 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            ((uint8_t *)&t24)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t24 + 8), t23);
            gm_to1cssm8dfiv5 t25 = t24;
            uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix7;
ie7:;
        {
            void * t28 = gm_l0;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s8sl52ot187i2));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(49));
            gm_b0_1 t30 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t31;
            memset(&t31, 0, sizeof t31);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
            gm_to1cssm8dfiv5 t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
ix7:;
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
inline __attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_intrinsic_4ou54h3 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* sym */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_th2lqbo77pvsc gm_l4; /* args */
    GM_STACK_CHECK(&F, 1073u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_f_bump_q2i40re((void *)&F.l1, t2);
    }
    {
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)&F.l1) + 0) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 0) + 8));
        gm_l2 = t5;
        {
            memset((uint8_t *)&gm_l3 + 28, 0, 4);
            gm_b16_8 t7 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&gm_l3 + 0, t7);
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 32));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l3 + 16, t8);
        }
        {
            gm_th2lqbo77pvsc t9;
            memset(&t9, 0, sizeof t9);
            gm_l4 = t9;
        }
        {
            void * t11 = gm_l0;
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)61ULL));
            uint8_t t13 = gm_f_at_54906rm(t11, t12);
            if (!t13) goto ie10;
            {
                void * t15 = gm_l0;
                gm_thk954m1ol1ig t16;
                gm_f_parse_call_args_32e205h((void *)&t16, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_thk954m1ol1ig t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) {
                    gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
                    gm_to1cssm8dfiv5 t19;
                    memset(&t19, 0, sizeof t19);
                    ((uint8_t *)&t19)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), t18);
                    F.ret = t19;
                    goto gm_fret;
                }
                gm_th2lqbo77pvsc t20 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t17) + 8));
                gm_th2lqbo77pvsc t21 = t20;
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 0));
                gm_th2lqbo77pvsc t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t23), t22);
                ((uint8_t *)&t23)[16] = 1;
                gm_l4 = t23;
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            void * t24 = gm_l0;
            gm_tet9guhev5r4u t25;
            memcpy((uint8_t *)&t25 + 0, ((uint8_t *)&gm_l3), 32);
            memcpy((uint8_t *)&t25 + 32, ((uint8_t *)&gm_l4), 24);
            gm_t2gtbh39ptekd t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)49ULL));
            GM_ST(gm_tet9guhev5r4u, ((uint8_t *)&t26 + 8), t25);
            void * t27 = gm_l0;
            uint32_t t28 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 32) + 4));
            gm_tl874t2atnlh3 t29 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t27, t28));
            gm_tc3oht4ksth9d t30;
            gm_f_mk_expr_53qj7nd((void *)&t30, t24, &t26, GM_CONV(gm_cseahvg8v7p0vs, t29));
            gm_to1cssm8dfiv5 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            ((uint8_t *)&t31)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t31 + 8), t30);
            gm_to1cssm8dfiv5 t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix3;
ie3:;
        {
            void * t35 = gm_l0;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sjrimf03ml0ct));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(12));
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
ix3:;
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
inline __attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_logic_ajejref F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l2; /* chain */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 1231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_not_a8b3gjf((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        gm_tt7vn9ehj0acr t8;
        memset(&t8, 0, sizeof t8);
        gm_l2 = t8;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb9;
            {
                gm_ts6oqppjh0ba3 t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)20ULL));
                gm_l3 = t12;
            }
            {
                void * t14 = gm_l0;
                gm_t9qkh0rsooc8d t15;
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)20ULL));
                uint8_t t16 = gm_f_at_kw_p0r3sr9(t14, t15);
                if (!t16) goto ie13;
                {
                    gm_ts6oqppjh0ba3 t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)21ULL));
                    gm_l3 = t18;
                }
                goto ix13;
ie13:;
                uint8_t t20 = 0;
                void * t21 = gm_l0;
                gm_t9qkh0rsooc8d t22;
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
                uint8_t t23 = gm_f_at_kw_p0r3sr9(t21, t22);
                uint8_t t24 = (uint8_t)!t23;
                if (!t24) {
                    void * t25 = gm_l0;
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 128) + 1));
                    t20 = t26;
                } else t20 = 1;
                if (!t20) goto ie19;
                {
                    goto lb9;
                }
                goto ix19;
ie19:;
ix19:;
ix13:;
            }
            {
                uint8_t t29;
                if (((((uint8_t *)&gm_l2))[2] != 0)) goto mn31;
                t29 = ((uint8_t)1);
                goto me30;
mn31:;
                t29 = ((uint8_t)0);
                goto me30;
mn32:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 185, 12);
me30:;
                if (!t29) goto ie28;
                {
                    gm_ts6oqppjh0ba3 t34 = gm_l3;
                    gm_tt7vn9ehj0acr t35;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35), t34);
                    ((uint8_t *)&t35)[2] = 1;
                    gm_l2 = t35;
                }
                goto ix28;
ie28:;
                gm_tt7vn9ehj0acr t37 = gm_l2;
                gm_ts6oqppjh0ba3 t38 = gm_l3;
                gm_tt7vn9ehj0acr t39;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t39), t38);
                ((uint8_t *)&t39)[2] = 1;
                gm_tt7vn9ehj0acr t40 = t37;
                gm_tt7vn9ehj0acr t41 = t39;
                uint8_t t42 = (!gm_eqcdhc1ipuua(((uint8_t *)&t40), ((uint8_t *)&t41)));
                if (!t42) goto ie36;
                {
                    void * t44 = gm_l0;
                    void * t45 = gm_l0;
                    gm_tl874t2atnlh3 t46 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t45));
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_snemlnnsbdb13));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(45));
                    gm_b0_1 t48 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t44, t46, t47);
                    if (gm_unw.kind) {
                        goto gm_ll10;
                    }
                }
                goto ix36;
ie36:;
ix36:;
ix28:;
            }
            {
                {
                    void * t50 = gm_l0;
                    gm_tm4pc0emhrv1g t51;
                    gm_f_bump_q2i40re((void *)&t51, t50);
                }
            }
            {
                void * t52 = gm_l0;
                gm_to1cssm8dfiv5 t53;
                gm_f_parse_not_a8b3gjf((void *)&t53, (void *)NULL, cx, NULL, t52);
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
                gm_to1cssm8dfiv5 t54 = t53;
                if (((((uint8_t *)&t54))[0] != 0)) {
                    gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 8));
                    gm_to1cssm8dfiv5 t56;
                    memset(&t56, 0, sizeof t56);
                    ((uint8_t *)&t56)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), t55);
                    F.ret = t56;
                    goto gm_fret;
                }
                gm_tc3oht4ksth9d t57 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t54) + 8));
                F.l4 = t57;
            }
            {
                void * t58 = gm_l0;
                gm_ts6oqppjh0ba3 t59 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)), (void *)NULL, cx, NULL, t58, t59, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll10;
                }
            }
lc9:;
        }
gm_ll10:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
        }
        goto gm_lad;
lb9:;
    }
    {
        gm_tc3oht4ksth9d t60 = F.l1;
        gm_to1cssm8dfiv5 t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
        ((uint8_t *)&t61)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t61 + 8), t60);
        gm_to1cssm8dfiv5 t62 = t61;
        uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
        F.ret = t61;
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
extern __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_not_a8b3gjf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 1015u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)18ULL));
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            gm_f_parse_cmp_q5clpig((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
    }
    {
        void * t21 = gm_l0;
        gm_to1cssm8dfiv5 t22;
        gm_f_parse_not_a8b3gjf((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_to1cssm8dfiv5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            gm_to1cssm8dfiv5 t25;
            memset(&t25, 0, sizeof t25);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tc3oht4ksth9d t26 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t23) + 8));
        F.l2 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)5ULL));
        void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t8ghgl2jj0qha t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 0), t28);
        GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
        gm_t2gtbh39ptekd t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t31 + 8), t30);
        void * t32 = gm_l0;
        uint32_t t33 = gm_l1;
        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t32, t33));
        gm_tc3oht4ksth9d t35;
        gm_f_mk_expr_53qj7nd((void *)&t35, t27, &t31, GM_CONV(gm_cseahvg8v7p0vs, t34));
        gm_to1cssm8dfiv5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t36 + 8), t35);
        gm_to1cssm8dfiv5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t38;
        goto gm_ld1_1;
    }
    {
        void * t39 = gm_l0;
        gm_f_leave_rb20u32(t39);
    }
    goto gm_ls40;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t41 = gm_l0;
        gm_f_leave_rb20u32(t41);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls40:;
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
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_paren_sq1guoi F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    gm_toh0ootodhjue gm_l3; /* old */
    uint8_t gm_l5; /* comma */
    gm_tqs5p4n2tq19v gm_l6; /* named */
    uint32_t gm_l7; /* elem_start */
    gm_tl874t2atnlh3 gm_l11; /* whole */
    GM_STACK_CHECK(&F, 3847u);
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
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)61ULL));
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t8 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 4));
                gm_to1cssm8dfiv5 t11;
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
        void * t13 = gm_l0;
        gm_toh0ootodhjue t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t14 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t14 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t15 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t13, GM_CONV(gm_cs3cuou245tr5f, t14)));
        gm_l3 = t15;
    }
    {
    }
    {
        gm_f_list_fvvkq82((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        gm_tqs5p4n2tq19v t19;
        memset(&t19, 0, sizeof t19);
        gm_l6 = t19;
    }
    {
        for (;;) {
            void * t21 = gm_l0;
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)62ULL));
            uint8_t t23 = gm_f_at_54906rm(t21, t22);
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto lb20;
            {
                void * t27 = gm_l0;
                uint32_t t28 = gm_f_lo_fp82gt4(t27);
                gm_l7 = t28;
            }
            {
                void * t30 = gm_l0;
                gm_t9qkh0rsooc8d t31;
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)50ULL));
                uint8_t t32 = gm_f_at_54906rm(t30, t31);
                if (!t32) goto ie29;
                {
                    {
                        void * t35 = gm_l0;
                        gm_tm4pc0emhrv1g t36;
                        gm_f_bump_q2i40re((void *)&t36, t35);
                    }
                }
                {
                    uint8_t t38 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                    if (!t38) goto ie37;
                    {
                        void * t40 = gm_l0;
                        void * t41 = gm_l0;
                        uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)t41) + 0));
                        uint32_t t43 = gm_l7;
                        void * t44 = gm_l0;
                        gm_tl874t2atnlh3 t45 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t44));
                        gm_tl874t2atnlh3 t46 = t45;
                        uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&t46) + 8));
                        gm_tl874t2atnlh3 t48;
                        GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t42);
                        GM_ST(uint32_t, ((uint8_t *)&t48 + 4), t43);
                        GM_ST(uint32_t, ((uint8_t *)&t48 + 8), t47);
                        gm_b16_8 t49;
                        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_shbaullahutpe));
                        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(36));
                        gm_b0_1 t50 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t40, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_ll25;
                        }
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                {
                    gm_to1cssm8dfiv5 t51;
                    uint8_t t53 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                    if (!t53) goto ie52;
                    void * t55 = gm_l0;
                    gm_to1cssm8dfiv5 t56;
                    gm_f_parse_expr_bkb2n6e((void *)&t56, (void *)NULL, cx, NULL, t55);
                    if (gm_unw.kind) {
                        goto gm_ll25;
                    }
                    gm_to1cssm8dfiv5 t57 = t56;
                    t51 = t57;
                    goto ix52;
ie52:;
                    void * t59 = gm_l0;
                    gm_to1cssm8dfiv5 t60;
                    gm_f_parse_type_2m93dtq((void *)&t60, (void *)NULL, cx, NULL, t59);
                    if (gm_unw.kind) {
                        goto gm_ll25;
                    }
                    gm_to1cssm8dfiv5 t61 = t60;
                    t51 = t61;
ix52:;
                    gm_to1cssm8dfiv5 t62 = t51;
                    if (((((uint8_t *)&t62))[0] != 0)) {
                        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 8));
                        gm_to1cssm8dfiv5 t64;
                        memset(&t64, 0, sizeof t64);
                        ((uint8_t *)&t64)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t64 + 8), t63);
                        F.ret = t64;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                        goto gm_ll25;
                    }
                    gm_tc3oht4ksth9d t65 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t62) + 8));
                    F.l8 = t65;
                }
                {
                    void * t66 = (void *)((uint8_t *)&F.l4);
                    gm_tv1o1j44ii4db t67;
                    memset((uint8_t *)&t67 + 129, 0, 7);
                    gm_t1s75i08qtptd t68;
                    memset(&t68, 0, sizeof t68);
                    GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t67 + 0, t68);
                    memcpy((uint8_t *)&t67 + 40, ((uint8_t *)&F.l8), 88);
                    GM_ST(uint8_t, (uint8_t *)&t67 + 128, ((uint8_t)1));
                    gm_tftb1odc739t5 t69 = gm_f_push_l8mrumu(t66, &t67);
                    gm_tftb1odc739t5 t70 = t69;
                    if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_src0dtioet3p4f6, 704, 13);
                    gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
                }
                {
                    void * t72 = gm_l0;
                    gm_t9qkh0rsooc8d t73;
                    GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)57ULL));
                    uint8_t t74 = gm_f_eat_qmock9g(t72, t73);
                    gm_l5 = t74;
                }
                {
                    uint8_t t76 = gm_l5;
                    uint8_t t77 = (uint8_t)!t76;
                    if (!t77) goto ie75;
                    {
                        goto lb20;
                    }
                    goto ix75;
ie75:;
ix75:;
                }
                {
                    goto lc20;
                }
                goto ix29;
ie29:;
ix29:;
            }
            {
                gm_t1s75i08qtptd t79;
                memset(&t79, 0, sizeof t79);
                F.l9 = t79;
            }
            {
                void * t81 = gm_l0;
                uint8_t t82 = gm_f_nth_is_name_oqhoobd(t81, ((uint64_t)0ULL));
                if (!t82) goto ie80;
                void * t83 = gm_l0;
                gm_t9qkh0rsooc8d t84;
                GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)56ULL));
                uint8_t t85 = gm_f_nth_is_3fgqmcb(t83, ((uint64_t)1ULL), t84);
                if (!t85) goto ie80;
                {
                    void * t87 = gm_l0;
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s1pvh8pfrs3ir));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                    gm_t4jn8fl3lmk5k t89;
                    gm_f_parse_name_figjvbe((void *)&t89, (void *)NULL, cx, NULL, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_ll25;
                    }
                    gm_t4jn8fl3lmk5k t90 = t89;
                    if (((((uint8_t *)&t90))[0] != 0)) {
                        gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 8));
                        gm_to1cssm8dfiv5 t92;
                        memset(&t92, 0, sizeof t92);
                        ((uint8_t *)&t92)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t92 + 8), t91);
                        F.ret = t92;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                        goto gm_ll25;
                    }
                    gm_t08olq97rscn3 t93 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t90) + 8));
                    gm_t1s75i08qtptd t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 36, 0);
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t94), t93);
                    ((uint8_t *)&t94)[32] = 1;
                    F.l9 = t94;
                }
                {
                    {
                        void * t96 = gm_l0;
                        gm_tm4pc0emhrv1g t97;
                        gm_f_bump_q2i40re((void *)&t97, t96);
                    }
                }
                goto ix80;
ie80:;
ix80:;
            }
            {
                uint8_t t99;
                if (((((uint8_t *)&gm_l6))[1] != 0)) goto mn101;
                t99 = ((uint8_t)1);
                goto me100;
mn101:;
                t99 = ((uint8_t)0);
                goto me100;
mn102:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 714, 12);
me100:;
                if (!t99) goto ie98;
                {
                    uint8_t t104;
                    if (((((uint8_t *)&F.l9))[32] != 0)) goto mn106;
                    t104 = ((uint8_t)0);
                    goto me105;
mn106:;
                    t104 = ((uint8_t)1);
                    goto me105;
mn107:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 715, 21);
me105:;
                    gm_tqs5p4n2tq19v t108;
                    GM_ST(uint8_t, ((uint8_t *)&t108), t104);
                    ((uint8_t *)&t108)[1] = 1;
                    gm_l6 = t108;
                }
                goto ix98;
ie98:;
                gm_tqs5p4n2tq19v t110 = gm_l6;
                uint8_t t111;
                if (((((uint8_t *)&F.l9))[32] != 0)) goto mn113;
                t111 = ((uint8_t)0);
                goto me112;
mn113:;
                t111 = ((uint8_t)1);
                goto me112;
mn114:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 716, 33);
me112:;
                gm_tqs5p4n2tq19v t115;
                GM_ST(uint8_t, ((uint8_t *)&t115), t111);
                ((uint8_t *)&t115)[1] = 1;
                gm_tqs5p4n2tq19v t116 = t110;
                gm_tqs5p4n2tq19v t117 = t115;
                uint8_t t118 = (!gm_eqebiqthsuot(((uint8_t *)&t116), ((uint8_t *)&t117)));
                if (!t118) goto ie109;
                {
                    void * t120 = gm_l0;
                    void * t121 = gm_l0;
                    uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)t121) + 0));
                    uint32_t t123 = gm_l7;
                    void * t124 = gm_l0;
                    gm_tl874t2atnlh3 t125 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t124));
                    gm_tl874t2atnlh3 t126 = t125;
                    uint32_t t127 = GM_LD(uint32_t, (((uint8_t *)&t126) + 8));
                    gm_tl874t2atnlh3 t128;
                    GM_ST(uint32_t, ((uint8_t *)&t128 + 0), t122);
                    GM_ST(uint32_t, ((uint8_t *)&t128 + 4), t123);
                    GM_ST(uint32_t, ((uint8_t *)&t128 + 8), t127);
                    gm_b16_8 t129;
                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sam3pd3469drv));
                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(54));
                    gm_b0_1 t130 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t120, t128, t129);
                    if (gm_unw.kind) {
                        goto gm_ll25;
                    }
                }
                goto ix109;
ie109:;
ix109:;
ix98:;
            }
            {
                gm_to1cssm8dfiv5 t131;
                uint8_t t133 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                if (!t133) goto ie132;
                void * t135 = gm_l0;
                gm_to1cssm8dfiv5 t136;
                gm_f_parse_expr_bkb2n6e((void *)&t136, (void *)NULL, cx, NULL, t135);
                if (gm_unw.kind) {
                    goto gm_ll25;
                }
                gm_to1cssm8dfiv5 t137 = t136;
                t131 = t137;
                goto ix132;
ie132:;
                void * t139 = gm_l0;
                gm_to1cssm8dfiv5 t140;
                gm_f_parse_type_2m93dtq((void *)&t140, (void *)NULL, cx, NULL, t139);
                if (gm_unw.kind) {
                    goto gm_ll25;
                }
                gm_to1cssm8dfiv5 t141 = t140;
                t131 = t141;
ix132:;
                gm_to1cssm8dfiv5 t142 = t131;
                if (((((uint8_t *)&t142))[0] != 0)) {
                    gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 8));
                    gm_to1cssm8dfiv5 t144;
                    memset(&t144, 0, sizeof t144);
                    ((uint8_t *)&t144)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t144 + 8), t143);
                    F.ret = t144;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll25;
                }
                gm_tc3oht4ksth9d t145 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t142) + 8));
                F.l10 = t145;
            }
            {
                void * t146 = (void *)((uint8_t *)&F.l4);
                gm_tv1o1j44ii4db t147;
                memset((uint8_t *)&t147 + 129, 0, 7);
                memcpy((uint8_t *)&t147 + 0, ((uint8_t *)&F.l9), 40);
                memcpy((uint8_t *)&t147 + 40, ((uint8_t *)&F.l10), 88);
                GM_ST(uint8_t, (uint8_t *)&t147 + 128, ((uint8_t)0));
                gm_tftb1odc739t5 t148 = gm_f_push_l8mrumu(t146, &t147);
                gm_tftb1odc739t5 t149 = t148;
                if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_src0dtioet3p4f6, 720, 9);
                gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
            }
            {
                void * t151 = gm_l0;
                gm_t9qkh0rsooc8d t152;
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)57ULL));
                uint8_t t153 = gm_f_eat_qmock9g(t151, t152);
                gm_l5 = t153;
            }
            {
                uint8_t t155 = gm_l5;
                uint8_t t156 = (uint8_t)!t155;
                if (!t156) goto ie154;
                {
                    goto lb20;
                }
                goto ix154;
ie154:;
ix154:;
            }
lc20:;
        }
gm_ll25:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
        }
        goto gm_ld1_1;
lb20:;
    }
    {
        {
            void * t159 = gm_l0;
            gm_t9qkh0rsooc8d t160;
            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)62ULL));
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sa3k87epai61e));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(24));
            gm_t0u0j6voh3loe t162 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t0u0j6voh3loe t163 = t162;
            if (((((uint8_t *)&t163))[0] != 0)) {
                gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 4));
                gm_to1cssm8dfiv5 t165;
                memset(&t165, 0, sizeof t165);
                ((uint8_t *)&t165)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t165 + 8), t164);
                F.ret = t165;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t163) + 4));
        }
    }
    {
        void * t167 = gm_l0;
        uint32_t t168 = gm_l2;
        gm_tl874t2atnlh3 t169 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t167, t168));
        gm_l11 = t169;
    }
    {
        uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t172 = ((t171 == ((uint64_t)1ULL)));
        if (!t172) goto ie170;
        uint8_t t173 = gm_l5;
        uint8_t t174 = (uint8_t)!t173;
        if (!t174) goto ie170;
        uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t176 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_src0dtioet3p4f6, 726, 47);
        uint8_t t177;
        if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t175 * 136) + 0))[32] != 0)) goto mn179;
        t177 = ((uint8_t)1);
        goto me178;
mn179:;
        t177 = ((uint8_t)0);
        goto me178;
mn180:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 726, 47);
me178:;
        if (!t177) goto ie170;
        uint64_t t181 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t182 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_src0dtioet3p4f6, 726, 83);
        uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t181 * 136) + 128));
        uint8_t t184 = (uint8_t)!t183;
        if (!t184) goto ie170;
        {
            void * t186 = gm_l0;
            uint64_t t187 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
            if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_src0dtioet3p4f6, 727, 37);
            void * t189 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t187 * 136) + 40)));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t2gtbh39ptekd t190;
            memset(&t190, 0, sizeof t190);
            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)9ULL));
            GM_ST(void *, ((uint8_t *)&t190 + 8), t189);
            gm_tl874t2atnlh3 t191 = gm_l11;
            gm_tc3oht4ksth9d t192;
            gm_f_mk_expr_53qj7nd((void *)&t192, t186, &t190, GM_CONV(gm_cseahvg8v7p0vs, t191));
            gm_to1cssm8dfiv5 t193;
            GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
            ((uint8_t *)&t193)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t193 + 8), t192);
            gm_to1cssm8dfiv5 t194 = t193;
            uint8_t t195 = ((((uint8_t *)&t194))[0] != 0);
            F.ret = t193;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t195;
            goto gm_ld1_1;
        }
        goto ix170;
ie170:;
ix170:;
    }
    {
        void * t196 = gm_l0;
        gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_t2gtbh39ptekd t198;
        memset(&t198, 0, sizeof t198);
        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t198 + 8), t197);
        gm_tl874t2atnlh3 t199 = gm_l11;
        gm_tc3oht4ksth9d t200;
        gm_f_mk_expr_53qj7nd((void *)&t200, t196, &t198, GM_CONV(gm_cseahvg8v7p0vs, t199));
        gm_to1cssm8dfiv5 t201;
        GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
        ((uint8_t *)&t201)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t201 + 8), t200);
        gm_to1cssm8dfiv5 t202 = t201;
        uint8_t t203 = ((((uint8_t *)&t202))[0] != 0);
        F.ret = t201;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t203;
        goto gm_ld1_1;
    }
    {
        {
            void * t205 = gm_l0;
            gm_toh0ootodhjue t206 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t205) + 128), t206);
        }
    }
    goto gm_ls207;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t209 = gm_l0;
            gm_toh0ootodhjue t210 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t209) + 128), t210);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls207:;
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
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_postfix_ao4kstr F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* allow_bang */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_primary_1vvr4ji((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        void * t8 = gm_l0;
        uint8_t t9 = gm_l1;
        gm_f_parse_postfix_ops_2p1csn3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t9);
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
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_parse_postfix_ops_2p1csn3 F;
    void * gm_l0; /* p */
    uint8_t gm_l2; /* allow_bang */
    uint32_t gm_l4; /* links */
    uint32_t gm_l5; /* start */
    gm_t3fcbuehh3bs2 gm_l6; /* k */
    gm_th2lqbo77pvsc gm_l7; /* args */
    GM_STACK_CHECK(&F, 3312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        gm_tc3oht4ksth9d t2 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
        F.l3 = t2;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 72) + 4));
                gm_l5 = t6;
            }
            {
                void * t7 = gm_l0;
                gm_f_kind_k239kpg((void *)&gm_l6, t7);
            }
            {
                uint8_t t9 = 0;
                gm_t3fcbuehh3bs2 t10 = gm_l6;
                gm_tak1k6bk5qcen t11;
                gm_t9qkh0rsooc8d t12;
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)45ULL));
                gm_t3fcbuehh3bs2 t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 0, t13);
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)47ULL));
                gm_t3fcbuehh3bs2 t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 8), t14);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 32, t15);
                gm_t9qkh0rsooc8d t16;
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)61ULL));
                gm_t3fcbuehh3bs2 t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 8), t16);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 64, t17);
                gm_t9qkh0rsooc8d t18;
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)63ULL));
                gm_t3fcbuehh3bs2 t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 8), t18);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 96, t19);
                gm_t9qkh0rsooc8d t20;
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)58ULL));
                gm_t3fcbuehh3bs2 t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 8), t20);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 128, t21);
                gm_tak1k6bk5qcen t22 = t11;
                uint8_t t23 = 0;
                gm_t3fcbuehh3bs2 t24 = t10;
                if (gm_eqt7ftn1841u(((uint8_t *)&t22) + 0, ((uint8_t *)&t24))) t23 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t22) + 32, ((uint8_t *)&t24))) t23 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t22) + 64, ((uint8_t *)&t24))) t23 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t22) + 96, ((uint8_t *)&t24))) t23 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t22) + 128, ((uint8_t *)&t24))) t23 = 1;
                uint8_t t25 = (uint8_t)(t23);
                if (!t25) {
                    uint8_t t26 = 0;
                    uint8_t t27 = gm_l2;
                    if (t27) {
                        gm_t3fcbuehh3bs2 t28 = gm_l6;
                        gm_t3fcbuehh3bs2 t29 = t28;
                        gm_t9qkh0rsooc8d t30;
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)46ULL));
                        gm_t9qkh0rsooc8d t31 = t30;
                        uint8_t t32 = (((GM_LD(uint8_t, ((uint8_t *)&t29) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t29) + 8), ((uint8_t *)&t31))));
                        t26 = t32;
                    } else t26 = 0;
                    t9 = t26;
                } else t9 = 1;
                if (!t9) goto ie8;
                {
                    uint32_t t34 = gm_l4;
                    uint32_t r35; bool o36 = __builtin_add_overflow(t34, ((uint32_t)1ULL), &r35);
                    if (o36) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 426, 13);
                    uint32_t t37 = r35;
                    gm_l4 = t37;
                }
                {
                    uint32_t t39 = gm_l4;
                    uint32_t t40 = GM_LD(uint32_t, gm_cpvard348ed);
                    uint8_t t41 = (t39 > t40);
                    if (!t41) goto ie38;
                    {
                        void * t43 = gm_l0;
                        void * t44 = gm_l0;
                        gm_tl874t2atnlh3 t45 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t44));
                        uint32_t t46 = GM_LD(uint32_t, gm_cpvard348ed);
                        gm_t88rcucg1b7uk t47;
                        GM_ST(uint32_t, ((uint8_t *)&t47 + 0), t46);
                        gm_tabb710d9mq2e t48;
                        gm_f_format_o5o20fv((void *)&t48, (void *)NULL, cx, NULL, t47);
                        if (gm_unw.kind) {
                            goto gm_ll4;
                        }
                        gm_tabb710d9mq2e t49 = t48;
                        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_src0dtioet3p4f6, 428, 42);
                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
                        gm_b16_8 t51 = t50;
                        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                        gm_b0_1 t53 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t43, t45, t52);
                        if (gm_unw.kind) {
                            goto gm_ll4;
                        }
                        gm_to1cssm8dfiv5 t54;
                        memset(&t54, 0, sizeof t54);
                        ((uint8_t *)&t54)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t54 + 8), t53);
                        gm_to1cssm8dfiv5 t55 = t54;
                        uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
                        F.ret = t54;
                        goto gm_fret;
                    }
                    goto ix38;
ie38:;
ix38:;
                }
                goto ix8;
ie8:;
ix8:;
            }
            {
                gm_t3fcbuehh3bs2 t57 = gm_l6;
                gm_t3fcbuehh3bs2 t58 = t57;
                uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                switch (t68) { case ((uint8_t)0ULL): goto ma66; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma66; case ((uint8_t)4ULL): goto ma66; case ((uint8_t)5ULL): goto ma66; case ((uint8_t)6ULL): goto ma66; case ((uint8_t)7ULL): goto ma66; case ((uint8_t)8ULL): goto ma60; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma66; default: goto ma66; }
ma60:;
                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t70 != ((uint8_t)8ULL)) goto mn69;
                uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t71 != ((uint8_t)45ULL)) goto mn69;
                {
                    {
                        void * t74 = gm_l0;
                        gm_tm4pc0emhrv1g t75;
                        gm_f_bump_q2i40re((void *)&t75, t74);
                    }
                }
                {
                    void * t76 = gm_l0;
                    void * t77 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_t2gtbh39ptekd t78;
                    memset(&t78, 0, sizeof t78);
                    GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)15ULL));
                    GM_ST(void *, ((uint8_t *)&t78 + 8), t77);
                    void * t79 = gm_l0;
                    uint32_t t80 = gm_l5;
                    gm_tl874t2atnlh3 t81 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t79, t80));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t76, &t78, GM_CONV(gm_cseahvg8v7p0vs, t81));
                }
                goto me59;
mn69:;
ma61:;
                uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t83 != ((uint8_t)8ULL)) goto mn82;
                uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t84 != ((uint8_t)46ULL)) goto mn82;
                uint8_t t85 = gm_l2;
                if (!t85) goto mn82;
                {
                    void * t87 = gm_l0;
                    gm_to1cssm8dfiv5 t88;
                    gm_f_parse_bang_irmkots((void *)&t88, (void *)NULL, cx, NULL, t87, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_to1cssm8dfiv5 t89 = t88;
                    if (((((uint8_t *)&t89))[0] != 0)) {
                        gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 8));
                        gm_to1cssm8dfiv5 t91;
                        memset(&t91, 0, sizeof t91);
                        ((uint8_t *)&t91)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t91 + 8), t90);
                        F.ret = t91;
                        goto gm_fret;
                    }
                    gm_tc3oht4ksth9d t92 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t89) + 8));
                    F.l3 = t92;
                }
                goto me59;
mn82:;
ma62:;
                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t94 != ((uint8_t)8ULL)) goto mn93;
                uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t95 != ((uint8_t)47ULL)) goto mn93;
                {
                    {
                        void * t98 = gm_l0;
                        gm_tm4pc0emhrv1g t99;
                        gm_f_bump_q2i40re((void *)&t99, t98);
                    }
                }
                {
                    void * t100 = gm_l0;
                    void * t101 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_t2gtbh39ptekd t102;
                    memset(&t102, 0, sizeof t102);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&t102 + 8), t101);
                    void * t103 = gm_l0;
                    uint32_t t104 = gm_l5;
                    gm_tl874t2atnlh3 t105 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t103, t104));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t100, &t102, GM_CONV(gm_cseahvg8v7p0vs, t105));
                }
                goto me59;
mn93:;
ma63:;
                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t107 != ((uint8_t)8ULL)) goto mn106;
                uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t108 != ((uint8_t)61ULL)) goto mn106;
                {
                    void * t110 = gm_l0;
                    gm_thk954m1ol1ig t111;
                    gm_f_parse_call_args_32e205h((void *)&t111, (void *)NULL, cx, NULL, t110);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_thk954m1ol1ig t112 = t111;
                    if (((((uint8_t *)&t112))[0] != 0)) {
                        gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 8));
                        gm_to1cssm8dfiv5 t114;
                        memset(&t114, 0, sizeof t114);
                        ((uint8_t *)&t114)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t114 + 8), t113);
                        F.ret = t114;
                        goto gm_fret;
                    }
                    gm_th2lqbo77pvsc t115 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t112) + 8));
                    gm_l7 = t115;
                }
                {
                    void * t116 = gm_l0;
                    gm_tlljg61a2ctgq t117;
                    memset((uint8_t *)&t117 + 25, 0, 7);
                    void * t118 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    GM_ST(void *, (uint8_t *)&t117 + 0, t118);
                    gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t117 + 8, t119);
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
                    GM_ST(uint8_t, (uint8_t *)&t117 + 24, t120);
                    gm_t2gtbh39ptekd t121;
                    GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t121 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t121 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t121 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t121 + 8), t117);
                    void * t122 = gm_l0;
                    uint32_t t123 = gm_l5;
                    gm_tl874t2atnlh3 t124 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t122, t123));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t116, &t121, GM_CONV(gm_cseahvg8v7p0vs, t124));
                }
                goto me59;
mn106:;
ma64:;
                uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t126 != ((uint8_t)8ULL)) goto mn125;
                uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t127 != ((uint8_t)63ULL)) goto mn125;
                {
                    void * t129 = gm_l0;
                    gm_to1cssm8dfiv5 t130;
                    gm_f_parse_subscript_1hujuhl((void *)&t130, (void *)NULL, cx, NULL, t129, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_to1cssm8dfiv5 t131 = t130;
                    if (((((uint8_t *)&t131))[0] != 0)) {
                        gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 8));
                        gm_to1cssm8dfiv5 t133;
                        memset(&t133, 0, sizeof t133);
                        ((uint8_t *)&t133)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t133 + 8), t132);
                        F.ret = t133;
                        goto gm_fret;
                    }
                    gm_tc3oht4ksth9d t134 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t131) + 8));
                    F.l3 = t134;
                }
                goto me59;
mn125:;
ma65:;
                uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                if (t136 != ((uint8_t)8ULL)) goto mn135;
                uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)&t58) + 8) + 0));
                if (t137 != ((uint8_t)58ULL)) goto mn135;
                {
                    void * t139 = gm_l0;
                    gm_to1cssm8dfiv5 t140;
                    gm_f_parse_dot_suffix_1fk204i((void *)&t140, (void *)NULL, cx, NULL, t139, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_to1cssm8dfiv5 t141 = t140;
                    if (((((uint8_t *)&t141))[0] != 0)) {
                        gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 8));
                        gm_to1cssm8dfiv5 t143;
                        memset(&t143, 0, sizeof t143);
                        ((uint8_t *)&t143)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t143 + 8), t142);
                        F.ret = t143;
                        goto gm_fret;
                    }
                    gm_tc3oht4ksth9d t144 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t141) + 8));
                    F.l3 = t144;
                }
                goto me59;
mn135:;
ma66:;
                goto lb3;
                goto me59;
mn145:;
ma67:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 431, 9);
me59:;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
    }
    {
        gm_tc3oht4ksth9d t146 = F.l3;
        gm_to1cssm8dfiv5 t147;
        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
        ((uint8_t *)&t147)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t147 + 8), t146);
        gm_to1cssm8dfiv5 t148 = t147;
        uint8_t t149 = ((((uint8_t *)&t148))[0] != 0);
        F.ret = t147;
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
inline __attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_prefix_671fi6d F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 1051u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t3fcbuehh3bs2 t3;
        gm_f_kind_k239kpg((void *)&t3, t2);
        gm_taari1s0m51a7 t4 = gm_f_prefix_op_japctlh(&t3);
        gm_taari1s0m51a7 t5 = t4;
        gm_t9qkh0rsooc8d t6;
        if (((((uint8_t *)&t5))[1] != 0)) {
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l0;
            gm_f_parse_postfix_ao4kstr((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l1 = t6;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l2 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
    }
    {
        void * t21 = gm_l0;
        gm_to1cssm8dfiv5 t22;
        gm_f_parse_prefix_671fi6d((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_to1cssm8dfiv5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            gm_to1cssm8dfiv5 t25;
            memset(&t25, 0, sizeof t25);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tc3oht4ksth9d t26 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t23) + 8));
        F.l3 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_t9qkh0rsooc8d t28 = gm_l1;
        void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t8ghgl2jj0qha t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 0), t28);
        GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
        gm_t2gtbh39ptekd t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t31 + 8), t30);
        void * t32 = gm_l0;
        uint32_t t33 = gm_l2;
        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t32, t33));
        gm_tc3oht4ksth9d t35;
        gm_f_mk_expr_53qj7nd((void *)&t35, t27, &t31, GM_CONV(gm_cseahvg8v7p0vs, t34));
        gm_to1cssm8dfiv5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t36 + 8), t35);
        gm_to1cssm8dfiv5 t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t38;
        goto gm_ld1_1;
    }
    {
        void * t39 = gm_l0;
        gm_f_leave_rb20u32(t39);
    }
    goto gm_ls40;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t41 = gm_l0;
        gm_f_leave_rb20u32(t41);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls40:;
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
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_primary_1vvr4ji F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_t3fcbuehh3bs2 gm_l2; /* k */
    gm_t08olq97rscn3 gm_l3; /* label */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* l */
    void * gm_l6; /* l */
    gm_tibupt4dmcqai gm_l7; /* l */
    gm_b16_8 gm_l8; /* name */
    gm_t08olq97rscn3 gm_l9; /* name */
    GM_STACK_CHECK(&F, 7898u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_f_kind_k239kpg((void *)&gm_l2, t4);
    }
    {
        uint8_t t6;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 597, 8);
me7:;
        if (!t6) goto ie5;
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)56ULL));
        uint8_t t13 = gm_f_nth_is_3fgqmcb(t11, ((uint64_t)1ULL), t12);
        if (!t13) goto ie5;
        void * t14 = gm_l0;
        gm_t3fcbuehh3bs2 t15;
        gm_f_nth_c0r2nvs((void *)&t15, t14, ((uint64_t)2ULL));
        gm_t23tohc57ebhi t16;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)31ULL));
        gm_t3fcbuehh3bs2 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 8), t17);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 0, t18);
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)12ULL));
        gm_t3fcbuehh3bs2 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t20 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t19);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 32, t20);
        gm_t23tohc57ebhi t21 = t16;
        uint8_t t22 = 0;
        gm_t3fcbuehh3bs2 t23 = t15;
        if (gm_eqt7ftn1841u(((uint8_t *)&t21) + 0, ((uint8_t *)&t23))) t22 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t21) + 32, ((uint8_t *)&t23))) t22 = 1;
        uint8_t t24 = (uint8_t)(t22);
        if (!t24) goto ie5;
        {
            void * t26 = gm_l0;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_so7em1q8clnib));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(7));
            gm_t4jn8fl3lmk5k t28;
            gm_f_parse_ident_n2c6b75((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) {
                gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 8));
                gm_to1cssm8dfiv5 t31;
                memset(&t31, 0, sizeof t31);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
                F.ret = t31;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t29) + 8));
            gm_l3 = t32;
        }
        {
            {
                void * t34 = gm_l0;
                gm_tm4pc0emhrv1g t35;
                gm_f_bump_q2i40re((void *)&t35, t34);
            }
        }
        {
            void * t37 = gm_l0;
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)31ULL));
            uint8_t t39 = gm_f_at_kw_p0r3sr9(t37, t38);
            if (!t39) goto ie36;
            {
                void * t41 = gm_l0;
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s1llcn0p4qesk));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(19));
                gm_t9qkh0rsooc8d t43 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9qkh0rsooc8d t44 = t43;
                if (((((uint8_t *)&t44))[0] != 0)) {
                    gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                    gm_to1cssm8dfiv5 t46;
                    memset(&t46, 0, sizeof t46);
                    ((uint8_t *)&t46)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t46 + 8), t45);
                    F.ret = t46;
                    goto gm_fret;
                }
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
            }
            {
                void * t48 = gm_l0;
                gm_t08olq97rscn3 t49 = gm_l3;
                gm_t1s75i08qtptd t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 36, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t50), t49);
                ((uint8_t *)&t50)[32] = 1;
                gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t48, &t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            void * t51 = gm_l0;
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sqm6u7rqmi1il));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t53 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) {
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                gm_to1cssm8dfiv5 t56;
                memset(&t56, 0, sizeof t56);
                ((uint8_t *)&t56)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t56 + 8), t55);
                F.ret = t56;
                goto gm_fret;
            }
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            void * t58 = gm_l0;
            gm_t08olq97rscn3 t59 = gm_l3;
            gm_t1s75i08qtptd t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t60), t59);
            ((uint8_t *)&t60)[32] = 1;
            gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t58, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t62;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t65 != ((uint8_t)2ULL)) goto mn64;
        t62 = ((uint8_t)1);
        goto me63;
mn64:;
        t62 = ((uint8_t)0);
        goto me63;
mn66:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 607, 8);
me63:;
        if (!t62) goto ie61;
        gm_t3fcbuehh3bs2 t67 = gm_l2;
        gm_t41cj6gvba3r8 t68;
        gm_t9qkh0rsooc8d t69;
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)25ULL));
        gm_t3fcbuehh3bs2 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t70 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t70 + 8), t69);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t68 + 0, t70);
        gm_t9qkh0rsooc8d t71;
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)10ULL));
        gm_t3fcbuehh3bs2 t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t72 + 8), t71);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t68 + 32, t72);
        gm_t9qkh0rsooc8d t73;
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)19ULL));
        gm_t3fcbuehh3bs2 t74;
        GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t74 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t74 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t74 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t74 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t74 + 8), t73);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t68 + 64, t74);
        gm_t41cj6gvba3r8 t75 = t68;
        uint8_t t76 = 0;
        gm_t3fcbuehh3bs2 t77 = t67;
        if (gm_eqt7ftn1841u(((uint8_t *)&t75) + 0, ((uint8_t *)&t77))) t76 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t75) + 32, ((uint8_t *)&t77))) t76 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t75) + 64, ((uint8_t *)&t77))) t76 = 1;
        uint8_t t78 = (uint8_t)(!t76);
        if (!t78) goto ie61;
        void * t79 = gm_l0;
        uint8_t t80 = gm_f_at_reported_kw_name_it872te(t79);
        if (!t80) goto ie61;
        {
            void * t82 = gm_l0;
            gm_f_bump_reported_kw_name_gs5eq1i((void *)&gm_l4, t82);
        }
        {
            void * t83 = gm_l0;
            gm_t08olq97rscn3 t84 = gm_l4;
            gm_t2gtbh39ptekd t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t85 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)6ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t85 + 8), t84);
            gm_tl874t2atnlh3 t86 = gm_l1;
            gm_tc3oht4ksth9d t87;
            gm_f_mk_expr_53qj7nd((void *)&t87, t83, &t85, GM_CONV(gm_cseahvg8v7p0vs, t86));
            gm_to1cssm8dfiv5 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
            ((uint8_t *)&t88)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t88 + 8), t87);
            gm_to1cssm8dfiv5 t89 = t88;
            uint8_t t90 = ((((uint8_t *)&t89))[0] != 0);
            F.ret = t88;
            goto gm_fret;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t122) { case ((uint8_t)0ULL): goto ma99; case ((uint8_t)1ULL): goto ma97; case ((uint8_t)2ULL): goto ma95; case ((uint8_t)3ULL): goto ma92; case ((uint8_t)4ULL): goto ma93; case ((uint8_t)5ULL): goto ma94; case ((uint8_t)6ULL): goto ma116; case ((uint8_t)7ULL): goto ma117; case ((uint8_t)8ULL): goto ma98; case ((uint8_t)9ULL): goto ma120; case ((uint8_t)10ULL): goto ma120; default: goto ma120; }
ma92:;
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t124 != ((uint8_t)3ULL)) goto mn123;
        void * t125 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        gm_l5 = t125;
        {
            {
                void * t128 = gm_l0;
                gm_tm4pc0emhrv1g t129;
                gm_f_bump_q2i40re((void *)&t129, t128);
            }
        }
        {
            void * t130 = gm_l0;
            void * t131 = gm_l5;
            gm_t2gtbh39ptekd t132;
            memset(&t132, 0, sizeof t132);
            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)0ULL));
            GM_ST(void *, ((uint8_t *)&t132 + 8), t131);
            gm_tl874t2atnlh3 t133 = gm_l1;
            gm_tc3oht4ksth9d t134;
            gm_f_mk_expr_53qj7nd((void *)&t134, t130, &t132, GM_CONV(gm_cseahvg8v7p0vs, t133));
            gm_to1cssm8dfiv5 t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
            ((uint8_t *)&t135)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t135 + 8), t134);
            gm_to1cssm8dfiv5 t136 = t135;
            uint8_t t137 = ((((uint8_t *)&t136))[0] != 0);
            F.ret = t135;
            goto gm_fret;
        }
        goto me91;
mn123:;
ma93:;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t139 != ((uint8_t)4ULL)) goto mn138;
        void * t140 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        gm_l6 = t140;
        {
            {
                void * t143 = gm_l0;
                gm_tm4pc0emhrv1g t144;
                gm_f_bump_q2i40re((void *)&t144, t143);
            }
        }
        {
            void * t145 = gm_l0;
            void * t146 = gm_l6;
            gm_t2gtbh39ptekd t147;
            memset(&t147, 0, sizeof t147);
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t147 + 8), t146);
            gm_tl874t2atnlh3 t148 = gm_l1;
            gm_tc3oht4ksth9d t149;
            gm_f_mk_expr_53qj7nd((void *)&t149, t145, &t147, GM_CONV(gm_cseahvg8v7p0vs, t148));
            gm_to1cssm8dfiv5 t150;
            GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
            ((uint8_t *)&t150)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t150 + 8), t149);
            gm_to1cssm8dfiv5 t151 = t150;
            uint8_t t152 = ((((uint8_t *)&t151))[0] != 0);
            F.ret = t150;
            goto gm_fret;
        }
        goto me91;
mn138:;
ma94:;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t154 != ((uint8_t)5ULL)) goto mn153;
        gm_tibupt4dmcqai t155 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&gm_l2) + 8));
        gm_l7 = t155;
        {
            {
                void * t158 = gm_l0;
                gm_tm4pc0emhrv1g t159;
                gm_f_bump_q2i40re((void *)&t159, t158);
            }
        }
        {
            void * t160 = gm_l0;
            gm_tibupt4dmcqai t161 = gm_l7;
            gm_t2gtbh39ptekd t162;
            memset(&t162, 0, sizeof t162);
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t162 + 8), t161);
            gm_tl874t2atnlh3 t163 = gm_l1;
            gm_tc3oht4ksth9d t164;
            gm_f_mk_expr_53qj7nd((void *)&t164, t160, &t162, GM_CONV(gm_cseahvg8v7p0vs, t163));
            gm_to1cssm8dfiv5 t165;
            GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
            ((uint8_t *)&t165)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t165 + 8), t164);
            gm_to1cssm8dfiv5 t166 = t165;
            uint8_t t167 = ((((uint8_t *)&t166))[0] != 0);
            F.ret = t165;
            goto gm_fret;
        }
        goto me91;
mn153:;
ma95:;
        uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t171 != ((uint8_t)2ULL)) goto pa170;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t172 != ((uint8_t)25ULL)) goto pa170;
        goto po169;
pa170:;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t174 != ((uint8_t)2ULL)) goto pa173;
        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t175 != ((uint8_t)10ULL)) goto pa173;
        goto po169;
pa173:;
        goto mn168;
po169:;
        {
            {
                void * t178 = gm_l0;
                gm_tm4pc0emhrv1g t179;
                gm_f_bump_q2i40re((void *)&t179, t178);
            }
        }
        {
            void * t180 = gm_l0;
            gm_t3fcbuehh3bs2 t181 = gm_l2;
            gm_t3fcbuehh3bs2 t182 = t181;
            gm_t9qkh0rsooc8d t183;
            GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)25ULL));
            gm_t9qkh0rsooc8d t184 = t183;
            uint8_t t185 = (((GM_LD(uint8_t, ((uint8_t *)&t182) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t182) + 8), ((uint8_t *)&t184))));
            gm_t2gtbh39ptekd t186;
            memset(&t186, 0, sizeof t186);
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, ((uint8_t *)&t186 + 8), t185);
            gm_tl874t2atnlh3 t187 = gm_l1;
            gm_tc3oht4ksth9d t188;
            gm_f_mk_expr_53qj7nd((void *)&t188, t180, &t186, GM_CONV(gm_cseahvg8v7p0vs, t187));
            gm_to1cssm8dfiv5 t189;
            GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
            ((uint8_t *)&t189)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t189 + 8), t188);
            gm_to1cssm8dfiv5 t190 = t189;
            uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
            F.ret = t189;
            goto gm_fret;
        }
        goto me91;
mn168:;
ma96:;
        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t193 != ((uint8_t)2ULL)) goto mn192;
        uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t194 != ((uint8_t)19ULL)) goto mn192;
        {
            {
                void * t197 = gm_l0;
                gm_tm4pc0emhrv1g t198;
                gm_f_bump_q2i40re((void *)&t198, t197);
            }
        }
        {
            void * t199 = gm_l0;
            gm_t2gtbh39ptekd t200;
            memset(&t200, 0, sizeof t200);
            GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)4ULL));
            gm_tl874t2atnlh3 t201 = gm_l1;
            gm_tc3oht4ksth9d t202;
            gm_f_mk_expr_53qj7nd((void *)&t202, t199, &t200, GM_CONV(gm_cseahvg8v7p0vs, t201));
            gm_to1cssm8dfiv5 t203;
            GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
            ((uint8_t *)&t203)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t203 + 8), t202);
            gm_to1cssm8dfiv5 t204 = t203;
            uint8_t t205 = ((((uint8_t *)&t204))[0] != 0);
            F.ret = t203;
            goto gm_fret;
        }
        goto me91;
mn192:;
ma97:;
        uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t207 != ((uint8_t)1ULL)) goto mn206;
        {
            {
                void * t210 = gm_l0;
                gm_tm4pc0emhrv1g t211;
                gm_f_bump_q2i40re((void *)&t211, t210);
            }
        }
        {
            void * t212 = gm_l0;
            gm_t2gtbh39ptekd t213;
            memset(&t213, 0, sizeof t213);
            GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)7ULL));
            gm_tl874t2atnlh3 t214 = gm_l1;
            gm_tc3oht4ksth9d t215;
            gm_f_mk_expr_53qj7nd((void *)&t215, t212, &t213, GM_CONV(gm_cseahvg8v7p0vs, t214));
            gm_to1cssm8dfiv5 t216;
            GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
            ((uint8_t *)&t216)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t216 + 8), t215);
            gm_to1cssm8dfiv5 t217 = t216;
            uint8_t t218 = ((((uint8_t *)&t217))[0] != 0);
            F.ret = t216;
            goto gm_fret;
        }
        goto me91;
mn206:;
ma98:;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t220 != ((uint8_t)8ULL)) goto mn219;
        uint8_t t221 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t221 != ((uint8_t)53ULL)) goto mn219;
        {
            {
                void * t224 = gm_l0;
                gm_tm4pc0emhrv1g t225;
                gm_f_bump_q2i40re((void *)&t225, t224);
            }
        }
        {
            void * t226 = gm_l0;
            gm_t2gtbh39ptekd t227;
            memset(&t227, 0, sizeof t227);
            GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)5ULL));
            gm_tl874t2atnlh3 t228 = gm_l1;
            gm_tc3oht4ksth9d t229;
            gm_f_mk_expr_53qj7nd((void *)&t229, t226, &t227, GM_CONV(gm_cseahvg8v7p0vs, t228));
            gm_to1cssm8dfiv5 t230;
            GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
            ((uint8_t *)&t230)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t230 + 8), t229);
            gm_to1cssm8dfiv5 t231 = t230;
            uint8_t t232 = ((((uint8_t *)&t231))[0] != 0);
            F.ret = t230;
            goto gm_fret;
        }
        goto me91;
mn219:;
ma99:;
        uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t234 != ((uint8_t)0ULL)) goto mn233;
        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_l8 = t235;
        {
            uint8_t t238 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_st2dnokm1h0jd, 4) == 0));
            if (!t238) goto ie237;
            void * t239 = gm_l0;
            gm_t9qkh0rsooc8d t240;
            GM_ST(uint8_t, ((uint8_t *)&t240 + 0), ((uint8_t)61ULL));
            uint8_t t241 = gm_f_nth_is_3fgqmcb(t239, ((uint64_t)1ULL), t240);
            if (!t241) goto ie237;
            {
                void * t243 = gm_l0;
                gm_f_parse_cont_type_ljmco4q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t243);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix237;
ie237:;
ix237:;
        }
        {
            uint8_t t245 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sic5vsema0r36, 7) == 0));
            if (!t245) goto ie244;
            void * t246 = gm_l0;
            gm_t3fcbuehh3bs2 t247;
            gm_f_nth_c0r2nvs((void *)&t247, t246, ((uint64_t)1ULL));
            gm_t3fcbuehh3bs2 t248 = t247;
            uint8_t t249;
            uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
            if (t252 != ((uint8_t)0ULL)) goto mn251;
            t249 = ((uint8_t)1);
            goto me250;
mn251:;
            t249 = ((uint8_t)0);
            goto me250;
mn253:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 643, 38);
me250:;
            if (!t249) goto ie244;
            {
                void * t255 = gm_l0;
                gm_f_parse_handler_lit_1nkojsu((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix244;
ie244:;
ix244:;
        }
        {
            uint8_t t257 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sv0fbf330lmcd, 7) == 0));
            if (!t257) goto ie256;
            void * t258 = gm_l0;
            gm_t3fcbuehh3bs2 t259;
            gm_f_nth_c0r2nvs((void *)&t259, t258, ((uint64_t)1ULL));
            gm_t3fcbuehh3bs2 t260 = t259;
            uint8_t t261;
            uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&t260) + 0));
            if (t264 != ((uint8_t)5ULL)) goto mn263;
            t261 = ((uint8_t)1);
            goto me262;
mn263:;
            t261 = ((uint8_t)0);
            goto me262;
mn265:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 644, 38);
me262:;
            if (!t261) goto ie256;
            {
                void * t267 = gm_l0;
                gm_f_parse_foreign_l4091na((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix256;
ie256:;
ix256:;
        }
        {
            {
                void * t269 = gm_l0;
                gm_tm4pc0emhrv1g t270;
                gm_f_bump_q2i40re((void *)&t270, t269);
            }
        }
        {
            void * t271 = gm_l0;
            gm_t08olq97rscn3 t272;
            memset((uint8_t *)&t272 + 28, 0, 4);
            gm_b16_8 t273 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t272 + 0, t273);
            gm_tl874t2atnlh3 t274 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t272 + 16, t274);
            gm_t2gtbh39ptekd t275;
            GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t275 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t275 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t275 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)6ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t275 + 8), t272);
            gm_tl874t2atnlh3 t276 = gm_l1;
            gm_tc3oht4ksth9d t277;
            gm_f_mk_expr_53qj7nd((void *)&t277, t271, &t275, GM_CONV(gm_cseahvg8v7p0vs, t276));
            gm_to1cssm8dfiv5 t278;
            GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
            ((uint8_t *)&t278)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t278 + 8), t277);
            gm_to1cssm8dfiv5 t279 = t278;
            uint8_t t280 = ((((uint8_t *)&t279))[0] != 0);
            F.ret = t278;
            goto gm_fret;
        }
        goto me91;
mn233:;
ma100:;
        uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t282 != ((uint8_t)8ULL)) goto mn281;
        uint8_t t283 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t283 != ((uint8_t)61ULL)) goto mn281;
        void * t284 = gm_l0;
        gm_t9qkh0rsooc8d t285;
        GM_ST(uint8_t, ((uint8_t *)&t285 + 0), ((uint8_t)0ULL));
        gm_f_parse_paren_sq1guoi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t284, t285);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn281:;
ma101:;
        uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t287 != ((uint8_t)8ULL)) goto mn286;
        uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t288 != ((uint8_t)58ULL)) goto mn286;
        {
            {
                void * t291 = gm_l0;
                gm_tm4pc0emhrv1g t292;
                gm_f_bump_q2i40re((void *)&t292, t291);
            }
        }
        {
            void * t293 = gm_l0;
            gm_b16_8 t294;
            GM_ST(void *, (uint8_t *)&t294, (void *)(gm_s541gi95epov5));
            GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(24));
            gm_t4jn8fl3lmk5k t295;
            gm_f_parse_ident_n2c6b75((void *)&t295, (void *)NULL, cx, NULL, t293, t294);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t296 = t295;
            if (((((uint8_t *)&t296))[0] != 0)) {
                gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t296) + 8));
                gm_to1cssm8dfiv5 t298;
                memset(&t298, 0, sizeof t298);
                ((uint8_t *)&t298)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t298 + 8), t297);
                F.ret = t298;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t299 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t296) + 8));
            gm_l9 = t299;
        }
        {
            void * t300 = gm_l0;
            gm_t08olq97rscn3 t301 = gm_l9;
            gm_t2gtbh39ptekd t302;
            GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t302 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t302 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t302 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t302 + 8), t301);
            void * t303 = gm_l0;
            uint32_t t304 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_tl874t2atnlh3 t305 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t303, t304));
            gm_tc3oht4ksth9d t306;
            gm_f_mk_expr_53qj7nd((void *)&t306, t300, &t302, GM_CONV(gm_cseahvg8v7p0vs, t305));
            gm_to1cssm8dfiv5 t307;
            GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
            ((uint8_t *)&t307)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t307 + 8), t306);
            gm_to1cssm8dfiv5 t308 = t307;
            uint8_t t309 = ((((uint8_t *)&t308))[0] != 0);
            F.ret = t307;
            goto gm_fret;
        }
        goto me91;
mn286:;
ma102:;
        uint8_t t313 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t313 != ((uint8_t)8ULL)) goto pa312;
        uint8_t t314 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t314 != ((uint8_t)45ULL)) goto pa312;
        goto po311;
pa312:;
        uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t316 != ((uint8_t)8ULL)) goto pa315;
        uint8_t t317 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t317 != ((uint8_t)47ULL)) goto pa315;
        goto po311;
pa315:;
        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t319 != ((uint8_t)8ULL)) goto pa318;
        uint8_t t320 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t320 != ((uint8_t)63ULL)) goto pa318;
        goto po311;
pa318:;
        goto mn310;
po311:;
        void * t321 = gm_l0;
        gm_t9qkh0rsooc8d t322;
        GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)0ULL));
        gm_f_parse_type_prefix_kfrfcdd((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t321, t322);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn310:;
ma103:;
        uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t324 != ((uint8_t)2ULL)) goto mn323;
        uint8_t t325 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t325 != ((uint8_t)11ULL)) goto mn323;
        void * t326 = gm_l0;
        gm_f_parse_fn_expr_c9357a4((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t326);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn323:;
ma104:;
        uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t328 != ((uint8_t)2ULL)) goto mn327;
        uint8_t t329 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t329 != ((uint8_t)24ULL)) goto mn327;
        void * t330 = gm_l0;
        gm_f_parse_struct_lit_86pp62c((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t330);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn327:;
ma105:;
        uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t332 != ((uint8_t)2ULL)) goto mn331;
        uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t333 != ((uint8_t)27ULL)) goto mn331;
        void * t334 = gm_l0;
        gm_f_parse_union_lit_39ap2o5((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t334);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn331:;
ma106:;
        uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t336 != ((uint8_t)2ULL)) goto mn335;
        uint8_t t337 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t337 != ((uint8_t)6ULL)) goto mn335;
        void * t338 = gm_l0;
        gm_f_parse_effect_lit_cqei081((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t338);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn335:;
ma107:;
        uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t340 != ((uint8_t)2ULL)) goto mn339;
        uint8_t t341 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t341 != ((uint8_t)14ULL)) goto mn339;
        void * t342 = gm_l0;
        gm_f_parse_import_s1hv231((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t342);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn339:;
ma108:;
        uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t344 != ((uint8_t)2ULL)) goto mn343;
        uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t345 != ((uint8_t)13ULL)) goto mn343;
        {
            void * t347 = gm_l0;
            gm_b16_8 t348;
            GM_ST(void *, (uint8_t *)&t348, (void *)(gm_sujvknhnh5am8));
            GM_ST(uintptr_t, (uint8_t *)&t348 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t349 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t347, t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t350 = t349;
            if (((((uint8_t *)&t350))[0] != 0)) {
                gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t350) + 1));
                gm_to1cssm8dfiv5 t352;
                memset(&t352, 0, sizeof t352);
                ((uint8_t *)&t352)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t352 + 8), t351);
                F.ret = t352;
                goto gm_fret;
            }
            gm_b0_1 t353 = GM_LD(gm_b0_1, (((uint8_t *)&t350) + 1));
        }
        {
            void * t354 = gm_l0;
            gm_f_parse_if_shktfou((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t354);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me91;
mn343:;
ma109:;
        uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t356 != ((uint8_t)2ULL)) goto mn355;
        uint8_t t357 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t357 != ((uint8_t)17ULL)) goto mn355;
        {
            void * t359 = gm_l0;
            gm_b16_8 t360;
            GM_ST(void *, (uint8_t *)&t360, (void *)(gm_sl7a8f8pe8le9));
            GM_ST(uintptr_t, (uint8_t *)&t360 + 8, (uintptr_t)(20));
            gm_t9qkh0rsooc8d t361 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t359, t360);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t362 = t361;
            if (((((uint8_t *)&t362))[0] != 0)) {
                gm_b0_1 t363 = GM_LD(gm_b0_1, (((uint8_t *)&t362) + 1));
                gm_to1cssm8dfiv5 t364;
                memset(&t364, 0, sizeof t364);
                ((uint8_t *)&t364)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t364 + 8), t363);
                F.ret = t364;
                goto gm_fret;
            }
            gm_b0_1 t365 = GM_LD(gm_b0_1, (((uint8_t *)&t362) + 1));
        }
        {
            void * t366 = gm_l0;
            gm_f_parse_match_1lcgsrh((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me91;
mn355:;
ma110:;
        uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t368 != ((uint8_t)2ULL)) goto mn367;
        uint8_t t369 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t369 != ((uint8_t)31ULL)) goto mn367;
        {
            void * t371 = gm_l0;
            gm_b16_8 t372;
            GM_ST(void *, (uint8_t *)&t372, (void *)(gm_s1llcn0p4qesk));
            GM_ST(uintptr_t, (uint8_t *)&t372 + 8, (uintptr_t)(19));
            gm_t9qkh0rsooc8d t373 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t371, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t374 = t373;
            if (((((uint8_t *)&t374))[0] != 0)) {
                gm_b0_1 t375 = GM_LD(gm_b0_1, (((uint8_t *)&t374) + 1));
                gm_to1cssm8dfiv5 t376;
                memset(&t376, 0, sizeof t376);
                ((uint8_t *)&t376)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t376 + 8), t375);
                F.ret = t376;
                goto gm_fret;
            }
            gm_b0_1 t377 = GM_LD(gm_b0_1, (((uint8_t *)&t374) + 1));
        }
        {
            void * t378 = gm_l0;
            gm_t1s75i08qtptd t379;
            memset(&t379, 0, sizeof t379);
            gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t378, &t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me91;
mn367:;
ma111:;
        uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t381 != ((uint8_t)2ULL)) goto mn380;
        uint8_t t382 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t382 != ((uint8_t)12ULL)) goto mn380;
        {
            void * t384 = gm_l0;
            gm_b16_8 t385;
            GM_ST(void *, (uint8_t *)&t385, (void *)(gm_sqm6u7rqmi1il));
            GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t386 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t384, t385);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t387 = t386;
            if (((((uint8_t *)&t387))[0] != 0)) {
                gm_b0_1 t388 = GM_LD(gm_b0_1, (((uint8_t *)&t387) + 1));
                gm_to1cssm8dfiv5 t389;
                memset(&t389, 0, sizeof t389);
                ((uint8_t *)&t389)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t389 + 8), t388);
                F.ret = t389;
                goto gm_fret;
            }
            gm_b0_1 t390 = GM_LD(gm_b0_1, (((uint8_t *)&t387) + 1));
        }
        {
            void * t391 = gm_l0;
            gm_t1s75i08qtptd t392;
            memset(&t392, 0, sizeof t392);
            gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t391, &t392);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me91;
mn380:;
ma112:;
        uint8_t t394 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t394 != ((uint8_t)2ULL)) goto mn393;
        uint8_t t395 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t395 != ((uint8_t)23ULL)) goto mn393;
        void * t396 = gm_l0;
        gm_f_parse_return_fjkmc1q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t396);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn393:;
ma113:;
        uint8_t t398 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t398 != ((uint8_t)2ULL)) goto mn397;
        uint8_t t399 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t399 != ((uint8_t)1ULL)) goto mn397;
        void * t400 = gm_l0;
        gm_f_parse_break_hehputf((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t400);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn397:;
ma114:;
        uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t402 != ((uint8_t)2ULL)) goto mn401;
        uint8_t t403 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t403 != ((uint8_t)3ULL)) goto mn401;
        void * t404 = gm_l0;
        gm_f_parse_continue_ertnf67((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t404);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn401:;
ma115:;
        uint8_t t406 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t406 != ((uint8_t)2ULL)) goto mn405;
        uint8_t t407 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t407 != ((uint8_t)22ULL)) goto mn405;
        void * t408 = gm_l0;
        gm_f_parse_resume_u919e4d((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t408);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn405:;
ma116:;
        uint8_t t410 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t410 != ((uint8_t)6ULL)) goto mn409;
        void * t411 = gm_l0;
        gm_f_parse_intrinsic_4ou54h3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t411);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn409:;
ma117:;
        uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t413 != ((uint8_t)7ULL)) goto mn412;
        void * t414 = gm_l0;
        gm_t9qkh0rsooc8d t415;
        GM_ST(uint8_t, ((uint8_t *)&t415 + 0), ((uint8_t)0ULL));
        gm_f_parse_attributed_00u9rjp((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t414, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me91;
mn412:;
ma118:;
        uint8_t t417 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t417 != ((uint8_t)2ULL)) goto mn416;
        uint8_t t418 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t418 != ((uint8_t)18ULL)) goto mn416;
        void * t419 = gm_l0;
        gm_tl874t2atnlh3 t420 = gm_l1;
        gm_b16_8 t421;
        GM_ST(void *, (uint8_t *)&t421, (void *)(gm_sb455a3lakuom));
        GM_ST(uintptr_t, (uint8_t *)&t421 + 8, (uintptr_t)(59));
        gm_b0_1 t422 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t419, t420, t421);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t423;
        memset(&t423, 0, sizeof t423);
        ((uint8_t *)&t423)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t423 + 8), t422);
        gm_to1cssm8dfiv5 t424 = t423;
        uint8_t t425 = ((((uint8_t *)&t424))[0] != 0);
        F.ret = t423;
        goto gm_fret;
        goto me91;
mn416:;
ma119:;
        uint8_t t427 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t427 != ((uint8_t)2ULL)) goto mn426;
        uint8_t t428 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t428 != ((uint8_t)26ULL)) goto mn426;
        void * t429 = gm_l0;
        gm_tl874t2atnlh3 t430 = gm_l1;
        gm_b16_8 t431;
        GM_ST(void *, (uint8_t *)&t431, (void *)(gm_slp04vsc9cddi));
        GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(48));
        gm_b0_1 t432 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t429, t430, t431);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t433;
        memset(&t433, 0, sizeof t433);
        ((uint8_t *)&t433)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t433 + 8), t432);
        gm_to1cssm8dfiv5 t434 = t433;
        uint8_t t435 = ((((uint8_t *)&t434))[0] != 0);
        F.ret = t433;
        goto gm_fret;
        goto me91;
mn426:;
ma120:;
        void * t437 = gm_l0;
        gm_b16_8 t438;
        GM_ST(void *, (uint8_t *)&t438, (void *)(gm_s42atdm90l4do));
        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(13));
        gm_b0_1 t439 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t437, t438);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t440;
        memset(&t440, 0, sizeof t440);
        ((uint8_t *)&t440)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t440 + 8), t439);
        gm_to1cssm8dfiv5 t441 = t440;
        uint8_t t442 = ((((uint8_t *)&t441))[0] != 0);
        F.ret = t440;
        goto gm_fret;
        goto me91;
mn436:;
ma121:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 611, 5);
me91:;
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
inline __attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_range_o9o2al7 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        uint8_t t11 = gm_f_binary_follows_jeb42bi(&t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            gm_tc3oht4ksth9d t14 = F.l1;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t18 = gm_l0;
        gm_f_range_from_5v6ko9m((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t18, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_resume_u919e4d F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tl874t2atnlh3 gm_l3; /* whole */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    void * gm_l6; /* value */
    GM_STACK_CHECK(&F, 1342u);
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
        void * t7 = gm_l0;
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_postfix_ao4kstr((void *)&t8, (void *)NULL, cx, NULL, t7, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_to1cssm8dfiv5 t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t12 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t9) + 8));
        F.l2 = t12;
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_l1;
        gm_tl874t2atnlh3 t15 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t13, t14));
        gm_l3 = t15;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 8) + 0));
        if (t17 != ((uint8_t)19ULL)) goto ie16;
        void * t18 = GM_LD(void *, (((((uint8_t *)&F.l2) + 8) + 8) + 0));
        gm_l4 = t18;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l2) + 8) + 8) + 8));
        gm_l5 = t19;
        {
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t23 = (t22 <= ((uint64_t)1ULL));
            if (!t23) goto ie21;
            uint8_t t24 = 0;
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
            if (!t26) {
                uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_src0dtioet3p4f6, 811, 48);
                uint8_t t29;
                uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t27 * 144) + 0) + 0));
                if (t32 != ((uint8_t)0ULL)) goto mn31;
                t29 = ((uint8_t)1);
                goto me30;
mn31:;
                t29 = ((uint8_t)0);
                goto me30;
mn33:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 811, 48);
me30:;
                t24 = t29;
            } else t24 = 1;
            if (!t24) goto ie21;
            {
                gm_l6 = ((void *)0);
            }
            {
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t37 = ((t36 == ((uint64_t)1ULL)));
                if (!t37) goto ie35;
                {
                    uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src0dtioet3p4f6, 814, 38);
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t40 * 144) + 0) + 0));
                    if (t42 != ((uint8_t)0ULL)) goto ie39;
                    gm_tc3oht4ksth9d t43 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t40 * 144) + 0) + 8));
                    F.l7 = t43;
                    {
                        void * t45 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l6 = t45;
                    }
                    goto ix39;
ie39:;
ix39:;
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                void * t46 = gm_l0;
                void * t47 = gm_l4;
                void * t48 = gm_l6;
                gm_t3mf58gv9kf3n t49;
                GM_ST(void *, ((uint8_t *)&t49 + 0), t47);
                GM_ST(void *, ((uint8_t *)&t49 + 8), t48);
                gm_t2gtbh39ptekd t50;
                memset(&t50, 0, sizeof t50);
                GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)48ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t50 + 8), t49);
                gm_tl874t2atnlh3 t51 = gm_l3;
                gm_tc3oht4ksth9d t52;
                gm_f_mk_expr_53qj7nd((void *)&t52, t46, &t50, GM_CONV(gm_cseahvg8v7p0vs, t51));
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
            goto ix21;
ie21:;
ix21:;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t56 = gm_l0;
        gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_skh04d6p3n402));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(78));
        gm_b0_1 t59 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t56, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t60;
        memset(&t60, 0, sizeof t60);
        ((uint8_t *)&t60)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
        gm_to1cssm8dfiv5 t61 = t60;
        uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
        F.ret = t60;
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
inline __attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_return_fjkmc1q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* value */
    GM_STACK_CHECK(&F, 906u);
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
        gm_l2 = ((void *)0);
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        uint8_t t10 = gm_f_can_begin_expr_rmia9bs(&t9);
        if (!t10) goto ie7;
        {
            void * t12 = gm_l0;
            gm_to1cssm8dfiv5 t13;
            gm_f_parse_expr_bkb2n6e((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) {
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
                gm_to1cssm8dfiv5 t16;
                memset(&t16, 0, sizeof t16);
                ((uint8_t *)&t16)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
                F.ret = t16;
                goto gm_fret;
            }
            gm_tc3oht4ksth9d t17 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t14) + 8));
            void * t18 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t18;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l2;
        gm_t2gtbh39ptekd t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)45ULL));
        GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
        void * t22 = gm_l0;
        uint32_t t23 = gm_l1;
        gm_tl874t2atnlh3 t24 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t22, t23));
        gm_tc3oht4ksth9d t25;
        gm_f_mk_expr_53qj7nd((void *)&t25, t19, &t21, GM_CONV(gm_cseahvg8v7p0vs, t24));
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
inline __attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_shift_jium2c6 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l1 = t7;
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        uint8_t t11 = gm_f_binary_follows_jeb42bi(&t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            gm_tc3oht4ksth9d t14 = F.l1;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t18 = gm_l0;
        gm_f_shift_from_3cnaq25((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t18, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_subscript_1hujuhl F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    gm_tao63pk3r18v5 gm_l6; /* x */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    GM_STACK_CHECK(&F, 1701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        {
            void * t4 = gm_l0;
            gm_tm4pc0emhrv1g t5;
            gm_f_bump_q2i40re((void *)&t5, t4);
        }
    }
    {
        void * t6 = gm_l0;
        gm_tb1kjfin6jnc2 t7;
        gm_f_parse_subscript_inner_dhgpovb((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tb1kjfin6jnc2 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 8));
            gm_to1cssm8dfiv5 t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), t9);
            F.ret = t10;
            goto gm_fret;
        }
        gm_t1vei8bd9e9m7 t11 = GM_LD(gm_t1vei8bd9e9m7, (((uint8_t *)&t8) + 8));
        F.l3 = t11;
    }
    {
        {
            void * t13 = gm_l0;
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)64ULL));
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sh0is7j09prt2));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(22));
            gm_t0u0j6voh3loe t16 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 4));
                gm_to1cssm8dfiv5 t19;
                memset(&t19, 0, sizeof t19);
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t17) + 4));
        }
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = gm_l2;
        gm_tl874t2atnlh3 t23 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t21, t22));
        gm_l4 = t23;
    }
    {
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t26 != ((uint8_t)0ULL)) goto mn25;
        gm_tc3oht4ksth9d t27 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&F.l3) + 8));
        F.l5 = t27;
        void * t28 = gm_l0;
        void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t30 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t31;
        GM_ST(void *, ((uint8_t *)&t31 + 0), t29);
        GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
        gm_t2gtbh39ptekd t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)20ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t32 + 8), t31);
        gm_tl874t2atnlh3 t33 = gm_l4;
        gm_tc3oht4ksth9d t34;
        gm_f_mk_expr_53qj7nd((void *)&t34, t28, &t32, GM_CONV(gm_cseahvg8v7p0vs, t33));
        gm_to1cssm8dfiv5 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t35 + 8), t34);
        gm_to1cssm8dfiv5 t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
        goto gm_fret;
        goto me24;
mn25:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t39 != ((uint8_t)1ULL)) goto mn38;
        gm_tao63pk3r18v5 t40 = GM_LD(gm_tao63pk3r18v5, (((uint8_t *)&F.l3) + 8));
        gm_l6 = t40;
        {
            gm_tao63pk3r18v5 t42 = gm_l6;
            gm_l7 = t42;
        }
        {
            void * t43 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (((uint8_t *)&gm_l7) + 0), t43);
        }
        {
            void * t44 = gm_l0;
            gm_tao63pk3r18v5 t45 = gm_l7;
            gm_t2gtbh39ptekd t46;
            GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t46 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t46 + 8), t45);
            gm_tl874t2atnlh3 t47 = gm_l4;
            gm_tc3oht4ksth9d t48;
            gm_f_mk_expr_53qj7nd((void *)&t48, t44, &t46, GM_CONV(gm_cseahvg8v7p0vs, t47));
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
        goto me24;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 502, 5);
me24:;
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
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_subscript_inner_dhgpovb F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    void * gm_l2; /* placeholder */
    uint8_t gm_l3; /* inclusive */
    gm_tl874t2atnlh3 gm_l4; /* op_span */
    void * gm_l5; /* hi */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    uint8_t gm_l10; /* inclusive */
    uint8_t gm_l11; /* open */
    void * gm_l12; /* hi */
    GM_STACK_CHECK(&F, 2492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)1));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
    }
    {
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t9));
        gm_tc3oht4ksth9d t11;
        gm_f_error_expr_dhdi86k((void *)&t11, t8, GM_CONV(gm_cseahvg8v7p0vs, t10));
        void * t12 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l2 = t12;
    }
    {
        uint8_t t14 = 0;
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)50ULL));
        uint8_t t17 = gm_f_at_54906rm(t15, t16);
        if (!t17) {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)51ULL));
            uint8_t t20 = gm_f_at_54906rm(t18, t19);
            t14 = t20;
        } else t14 = 1;
        if (!t14) goto ie13;
        {
            void * t22 = gm_l0;
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)51ULL));
            uint8_t t24 = gm_f_at_54906rm(t22, t23);
            gm_l3 = t24;
        }
        {
            void * t25 = gm_l0;
            gm_tm4pc0emhrv1g t26;
            gm_f_bump_q2i40re((void *)&t26, t25);
            gm_tm4pc0emhrv1g t27 = t26;
            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t27) + 32));
            gm_l4 = t28;
        }
        {
            gm_l5 = ((void *)0);
        }
        {
            void * t30 = gm_l0;
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)64ULL));
            uint8_t t32 = gm_f_at_54906rm(t30, t31);
            uint8_t t33 = (uint8_t)!t32;
            if (!t33) goto ie29;
            {
                void * t35 = gm_l0;
                gm_to1cssm8dfiv5 t36;
                gm_f_parse_expr_bkb2n6e((void *)&t36, (void *)NULL, cx, NULL, t35);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_to1cssm8dfiv5 t37 = t36;
                if (((((uint8_t *)&t37))[0] != 0)) {
                    gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
                    gm_tb1kjfin6jnc2 t39;
                    memset(&t39, 0, sizeof t39);
                    ((uint8_t *)&t39)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
                    F.ret = t39;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld1_1;
                }
                gm_tc3oht4ksth9d t40 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t37) + 8));
                void * t41 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t40);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_l5 = t41;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            uint8_t t43 = gm_l3;
            if (!t43) goto ie42;
            void * t44 = gm_l5;
            void * t45 = t44;
            uint8_t t46;
            if ((GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto mn48;
            t46 = ((uint8_t)1);
            goto me47;
mn48:;
            t46 = ((uint8_t)0);
            goto me47;
mn49:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 527, 26);
me47:;
            if (!t46) goto ie42;
            {
                void * t51 = gm_l0;
                gm_tl874t2atnlh3 t52 = gm_l4;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_skefd3dps40es));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(39));
                gm_b0_1 t54 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t51, t52, t53);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            gm_tao63pk3r18v5 t55;
            memset((uint8_t *)&t55 + 25, 0, 7);
            void * t56 = gm_l2;
            GM_ST(void *, (uint8_t *)&t55 + 0, t56);
            GM_ST(void *, (uint8_t *)&t55 + 8, ((void *)0));
            void * t57 = gm_l5;
            GM_ST(void *, (uint8_t *)&t55 + 16, t57);
            uint8_t t58 = gm_l3;
            GM_ST(uint8_t, (uint8_t *)&t55 + 24, t58);
            gm_t1vei8bd9e9m7 t59;
            memset(&t59, 0, sizeof t59);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t59 + 8), t55);
            gm_tb1kjfin6jnc2 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
            ((uint8_t *)&t60)[0] = 0;
            GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t60 + 8), t59);
            gm_tb1kjfin6jnc2 t61 = t60;
            uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
            F.ret = t60;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t62;
            goto gm_ld1_1;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t63 = gm_l0;
        gm_to1cssm8dfiv5 t64;
        gm_f_parse_expr_bkb2n6e((void *)&t64, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_to1cssm8dfiv5 t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) {
            gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 8));
            gm_tb1kjfin6jnc2 t67;
            memset(&t67, 0, sizeof t67);
            ((uint8_t *)&t67)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t67 + 8), t66);
            F.ret = t67;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_tc3oht4ksth9d t68 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t65) + 8));
        F.l6 = t68;
    }
    {
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
        if (t70 != ((uint8_t)13ULL)) goto ie69;
        gm_ts6oqppjh0ba3 t71 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)&F.l6) + 8) + 8) + 0));
        gm_l7 = t71;
        void * t72 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 8));
        gm_l8 = t72;
        void * t73 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 16));
        gm_l9 = t73;
        {
            gm_ts6oqppjh0ba3 t76 = gm_l7;
            gm_ts6oqppjh0ba3 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)11ULL));
            gm_t9ldr1q5hn9fq t79;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t79 + 0), t77);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t79 + 2), t78);
            gm_t9ldr1q5hn9fq t80 = t79;
            uint8_t t81 = 0;
            gm_ts6oqppjh0ba3 t82 = t76;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t80) + 0, ((uint8_t *)&t82))) t81 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t80) + 2, ((uint8_t *)&t82))) t81 = 1;
            uint8_t t83 = (uint8_t)(t81);
            if (!t83) goto ie75;
            {
                uint8_t t85 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
                gm_l10 = t85;
            }
            {
                uint8_t t86 = 0;
                void * t87 = gm_l9;
                uint8_t t88 = ((GM_LD(uint8_t, (((uint8_t *)t87) + 8) + 0) == ((uint8_t)50ULL)));
                if (t88) {
                    void * t89 = gm_l9;
                    gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 72));
                    uint8_t t91 = gm_f_is_empty_q2f0b93(GM_CONV(gm_cseahvg8v7p0vs, t90));
                    t86 = t91;
                } else t86 = 0;
                gm_l11 = t86;
            }
            {
                uint8_t t93 = gm_l11;
                if (!t93) goto ie92;
                uint8_t t94 = gm_l10;
                if (!t94) goto ie92;
                {
                    void * t96 = gm_l0;
                    gm_tl874t2atnlh3 t97 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_skefd3dps40es));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(39));
                    gm_b0_1 t99 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t96, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_ld1_1;
                    }
                }
                goto ix92;
ie92:;
ix92:;
            }
            {
                void * t100;
                uint8_t t102 = gm_l11;
                if (!t102) goto ie101;
                void * t104 = ((void *)0);
                t100 = t104;
                goto ix101;
ie101:;
                void * t106 = gm_l9;
                void * t107 = t106;
                t100 = t107;
ix101:;
                gm_l12 = t100;
            }
            {
                gm_tao63pk3r18v5 t108;
                memset((uint8_t *)&t108 + 25, 0, 7);
                void * t109 = gm_l2;
                GM_ST(void *, (uint8_t *)&t108 + 0, t109);
                void * t110 = gm_l8;
                GM_ST(void *, (uint8_t *)&t108 + 8, t110);
                void * t111 = gm_l12;
                GM_ST(void *, (uint8_t *)&t108 + 16, t111);
                uint8_t t112 = gm_l10;
                GM_ST(uint8_t, (uint8_t *)&t108 + 24, t112);
                gm_t1vei8bd9e9m7 t113;
                memset(&t113, 0, sizeof t113);
                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t113 + 8), t108);
                gm_tb1kjfin6jnc2 t114;
                GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                ((uint8_t *)&t114)[0] = 0;
                GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t114 + 8), t113);
                gm_tb1kjfin6jnc2 t115 = t114;
                uint8_t t116 = ((((uint8_t *)&t115))[0] != 0);
                F.ret = t114;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t116;
                goto gm_ld1_1;
            }
            goto ix75;
ie75:;
ix75:;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        gm_tc3oht4ksth9d t117 = F.l6;
        gm_t1vei8bd9e9m7 t118;
        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t118 + 8), t117);
        gm_tb1kjfin6jnc2 t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
        ((uint8_t *)&t119)[0] = 0;
        GM_ST(gm_t1vei8bd9e9m7, ((uint8_t *)&t119 + 8), t118);
        gm_tb1kjfin6jnc2 t120 = t119;
        uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
        F.ret = t119;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t121;
        goto gm_ld1_1;
    }
    {
        {
            void * t123 = gm_l0;
            gm_toh0ootodhjue t124 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t123) + 128), t124);
        }
    }
    goto gm_ls125;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t127 = gm_l0;
            gm_toh0ootodhjue t128 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t127) + 128), t128);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls125:;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_op_japctlh F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_taari1s0m51a7 t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; default: goto ma8; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)8ULL)) goto mn11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t15;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
    t1 = t15;
    goto me2;
mn11:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)8ULL)) goto mn16;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t18 != ((uint8_t)9ULL)) goto mn16;
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t20;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
    ((uint8_t *)&t20)[1] = 1;
    t1 = t20;
    goto me2;
mn16:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t22 != ((uint8_t)8ULL)) goto mn21;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t23 != ((uint8_t)7ULL)) goto mn21;
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t25;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[1] = 1;
    t1 = t25;
    goto me2;
mn21:;
ma6:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)8ULL)) goto mn26;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t28 != ((uint8_t)46ULL)) goto mn26;
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t30;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30), t29);
    ((uint8_t *)&t30)[1] = 1;
    t1 = t30;
    goto me2;
mn26:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t33 != ((uint8_t)26ULL)) goto mn31;
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)4ULL));
    gm_taari1s0m51a7 t35;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[1] = 1;
    t1 = t35;
    goto me2;
mn31:;
ma8:;
    gm_taari1s0m51a7 t37;
    memset(&t37, 0, sizeof t37);
    t1 = t37;
    goto me2;
mn36:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 391, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0) { return gm_f_prefix_op_japctlh(&a0); }
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_range_from_5v6ko9m F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    uint8_t gm_l4; /* open */
    GM_STACK_CHECK(&F, 2371u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_bit_from_pu2bq19((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        gm_ts6oqppjh0ba3 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)10ULL));
        gm_l3 = t8;
    }
    {
        void * t10 = gm_l0;
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)51ULL));
        uint8_t t12 = gm_f_at_54906rm(t10, t11);
        if (!t12) goto ie9;
        {
            gm_ts6oqppjh0ba3 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)11ULL));
            gm_l3 = t14;
        }
        goto ix9;
ie9:;
        void * t16 = gm_l0;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)50ULL));
        uint8_t t18 = gm_f_at_54906rm(t16, t17);
        uint8_t t19 = (uint8_t)!t18;
        if (!t19) goto ie15;
        {
            gm_tc3oht4ksth9d t21 = F.l2;
            gm_to1cssm8dfiv5 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            ((uint8_t *)&t22)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t22 + 8), t21);
            gm_to1cssm8dfiv5 t23 = t22;
            uint8_t t24 = ((((uint8_t *)&t23))[0] != 0);
            F.ret = t22;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
ix9:;
    }
    {
        {
            void * t26 = gm_l0;
            gm_tm4pc0emhrv1g t27;
            gm_f_bump_q2i40re((void *)&t27, t26);
        }
    }
    {
        uint8_t t28 = 0;
        void * t29 = gm_l0;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 128) + 2));
        if (t30) {
            void * t31 = gm_l0;
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)64ULL));
            uint8_t t33 = gm_f_at_54906rm(t31, t32);
            t28 = t33;
        } else t28 = 0;
        gm_l4 = t28;
    }
    {
        gm_tc3oht4ksth9d t34;
        uint8_t t36 = gm_l4;
        if (!t36) goto ie35;
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        gm_tl874t2atnlh3 t40 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t39));
        gm_tl874t2atnlh3 t41 = GM_CONV(gm_tl874t2atnlh3, gm_f_shrink_to_lo_iok07dh(GM_CONV(gm_cseahvg8v7p0vs, t40)));
        gm_tc3oht4ksth9d t42;
        gm_f_error_expr_dhdi86k((void *)&t42, t38, GM_CONV(gm_cseahvg8v7p0vs, t41));
        gm_tc3oht4ksth9d t43 = t42;
        t34 = t43;
        goto ix35;
ie35:;
        void * t45 = gm_l0;
        gm_to1cssm8dfiv5 t46;
        gm_f_parse_bit_h83vs72((void *)&t46, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) {
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
            gm_to1cssm8dfiv5 t49;
            memset(&t49, 0, sizeof t49);
            ((uint8_t *)&t49)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t49 + 8), t48);
            F.ret = t49;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t50 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t47) + 8));
        gm_tc3oht4ksth9d t51 = t50;
        t34 = t51;
ix35:;
        F.l5 = t34;
    }
    {
        void * t52 = gm_l0;
        gm_ts6oqppjh0ba3 t53 = gm_l3;
        gm_f_binary_41fr5r5((void *)&F.l6, (void *)NULL, cx, NULL, t52, t53, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t55 = 0;
        void * t56 = gm_l0;
        gm_t9qkh0rsooc8d t57;
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)50ULL));
        uint8_t t58 = gm_f_at_54906rm(t56, t57);
        if (!t58) {
            void * t59 = gm_l0;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)51ULL));
            uint8_t t61 = gm_f_at_54906rm(t59, t60);
            t55 = t61;
        } else t55 = 1;
        if (!t55) goto ie54;
        {
            void * t63 = gm_l0;
            void * t64 = gm_l0;
            gm_tl874t2atnlh3 t65 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t64));
            gm_b16_8 t66;
            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s1q0n6t6d7g6n));
            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(33));
            gm_b0_1 t67 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t63, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            for (;;) {
                uint8_t t69 = 0;
                void * t70 = gm_l0;
                gm_t9qkh0rsooc8d t71;
                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)50ULL));
                uint8_t t72 = gm_f_at_54906rm(t70, t71);
                if (!t72) {
                    void * t73 = gm_l0;
                    gm_t9qkh0rsooc8d t74;
                    GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)51ULL));
                    uint8_t t75 = gm_f_at_54906rm(t73, t74);
                    t69 = t75;
                } else t69 = 1;
                if (!t69) goto lb68;
                {
                    {
                        void * t79 = gm_l0;
                        gm_tm4pc0emhrv1g t80;
                        gm_f_bump_q2i40re((void *)&t80, t79);
                    }
                }
                {
                    {
                        void * t82 = gm_l0;
                        gm_to1cssm8dfiv5 t83;
                        gm_f_parse_bit_h83vs72((void *)&t83, (void *)NULL, cx, NULL, t82);
                        if (gm_unw.kind) {
                            goto gm_ll76;
                        }
                        gm_to1cssm8dfiv5 t84 = t83;
                        if (((((uint8_t *)&t84))[0] != 0)) {
                            gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 8));
                            gm_to1cssm8dfiv5 t86;
                            memset(&t86, 0, sizeof t86);
                            ((uint8_t *)&t86)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t86 + 8), t85);
                            F.ret = t86;
                            goto gm_fret;
                        }
                        gm_tc3oht4ksth9d t87 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t84) + 8));
                    }
                }
lc68:;
            }
gm_ll76:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb68; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc68; }
            }
            goto gm_lad;
lb68:;
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        gm_tc3oht4ksth9d t88 = F.l6;
        gm_to1cssm8dfiv5 t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
        ((uint8_t *)&t89)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t89 + 8), t88);
        gm_to1cssm8dfiv5 t90 = t89;
        uint8_t t91 = ((((uint8_t *)&t90))[0] != 0);
        F.ret = t89;
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
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_shift_from_3cnaq25 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 2124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_add_from_7os0rvi((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 8));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t4) + 8));
        F.l2 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        gm_tt7vn9ehj0acr t10 = gm_f_shift_op_sro3o9l(&t9);
        gm_tt7vn9ehj0acr t11 = t10;
        gm_ts6oqppjh0ba3 t12;
        if (((((uint8_t *)&t11))[2] != 0)) {
            gm_ts6oqppjh0ba3 t13 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_tc3oht4ksth9d t14 = F.l2;
            gm_to1cssm8dfiv5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t15 + 8), t14);
            gm_to1cssm8dfiv5 t16 = t15;
            uint8_t t17 = ((((uint8_t *)&t16))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        gm_l3 = t12;
    }
    {
        {
            void * t19 = gm_l0;
            gm_tm4pc0emhrv1g t20;
            gm_f_bump_q2i40re((void *)&t20, t19);
        }
    }
    {
        void * t21 = gm_l0;
        gm_to1cssm8dfiv5 t22;
        gm_f_parse_add_dh9nvbt((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
            gm_to1cssm8dfiv5 t25;
            memset(&t25, 0, sizeof t25);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t26 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t23) + 8));
        F.l4 = t26;
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l2);
        uint8_t t29 = gm_f_is_arith_l6b4tpf(t28);
        if (!t29) goto ie27;
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s4puodol85qqo));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(54));
            gm_b0_1 t34 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix27;
ie27:;
        void * t36 = (void *)((uint8_t *)&F.l4);
        uint8_t t37 = gm_f_is_arith_l6b4tpf(t36);
        if (!t37) goto ie35;
        {
            void * t39 = gm_l0;
            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s4puodol85qqo));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(54));
            gm_b0_1 t42 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t39, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix35;
ie35:;
ix35:;
ix27:;
    }
    {
        void * t43 = gm_l0;
        gm_ts6oqppjh0ba3 t44 = gm_l3;
        gm_f_binary_41fr5r5((void *)&F.l5, (void *)NULL, cx, NULL, t43, t44, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t46 = gm_l0;
        gm_t3fcbuehh3bs2 t47;
        gm_f_kind_k239kpg((void *)&t47, t46);
        gm_tt7vn9ehj0acr t48 = gm_f_shift_op_sro3o9l(&t47);
        gm_tt7vn9ehj0acr t49 = t48;
        uint8_t t50;
        if (((((uint8_t *)&t49))[2] != 0)) goto mn52;
        t50 = ((uint8_t)0);
        goto me51;
mn52:;
        t50 = ((uint8_t)1);
        goto me51;
mn53:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 346, 8);
me51:;
        if (!t50) goto ie45;
        {
            void * t55 = gm_l0;
            void * t56 = gm_l0;
            gm_tl874t2atnlh3 t57 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t56));
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_ssf8mqtfpipac));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(44));
            gm_b0_1 t59 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t55, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            for (;;) {
                void * t61 = gm_l0;
                gm_t3fcbuehh3bs2 t62;
                gm_f_kind_k239kpg((void *)&t62, t61);
                gm_tt7vn9ehj0acr t63 = gm_f_shift_op_sro3o9l(&t62);
                gm_tt7vn9ehj0acr t64 = t63;
                uint8_t t65;
                if (((((uint8_t *)&t64))[2] != 0)) goto mn67;
                t65 = ((uint8_t)0);
                goto me66;
mn67:;
                t65 = ((uint8_t)1);
                goto me66;
mn68:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 348, 15);
me66:;
                if (!t65) goto lb60;
                {
                    {
                        void * t72 = gm_l0;
                        gm_tm4pc0emhrv1g t73;
                        gm_f_bump_q2i40re((void *)&t73, t72);
                    }
                }
                {
                    {
                        void * t75 = gm_l0;
                        gm_to1cssm8dfiv5 t76;
                        gm_f_parse_add_dh9nvbt((void *)&t76, (void *)NULL, cx, NULL, t75);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        gm_to1cssm8dfiv5 t77 = t76;
                        if (((((uint8_t *)&t77))[0] != 0)) {
                            gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 8));
                            gm_to1cssm8dfiv5 t79;
                            memset(&t79, 0, sizeof t79);
                            ((uint8_t *)&t79)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t79 + 8), t78);
                            F.ret = t79;
                            goto gm_fret;
                        }
                        gm_tc3oht4ksth9d t80 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t77) + 8));
                    }
                }
lc60:;
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
            }
            goto gm_lad;
lb60:;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        gm_tc3oht4ksth9d t81 = F.l5;
        gm_to1cssm8dfiv5 t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        ((uint8_t *)&t82)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t82 + 8), t81);
        gm_to1cssm8dfiv5 t83 = t82;
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shift_op_sro3o9l F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; default: goto ma8; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)8ULL)) goto mn11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t13 != ((uint8_t)5ULL)) goto mn11;
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
    gm_tt7vn9ehj0acr t16;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t16), t15);
    ((uint8_t *)&t16)[2] = 1;
    t1 = t16;
    goto me2;
mn11:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)8ULL)) goto mn17;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t19 != ((uint8_t)13ULL)) goto mn17;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
    gm_tt7vn9ehj0acr t22;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t22), t21);
    ((uint8_t *)&t22)[2] = 1;
    t1 = t22;
    goto me2;
mn17:;
ma5:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t24 != ((uint8_t)8ULL)) goto mn23;
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t25 != ((uint8_t)17ULL)) goto mn23;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 1), t26);
    gm_tt7vn9ehj0acr t28;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[2] = 1;
    t1 = t28;
    goto me2;
mn23:;
ma6:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t30 != ((uint8_t)8ULL)) goto mn29;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t31 != ((uint8_t)21ULL)) goto mn29;
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 1), t32);
    gm_tt7vn9ehj0acr t34;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t34), t33);
    ((uint8_t *)&t34)[2] = 1;
    t1 = t34;
    goto me2;
mn29:;
ma7:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t36 != ((uint8_t)8ULL)) goto mn35;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t37 != ((uint8_t)6ULL)) goto mn35;
    gm_ts6oqppjh0ba3 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)6ULL));
    gm_tt7vn9ehj0acr t39;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t39), t38);
    ((uint8_t *)&t39)[2] = 1;
    t1 = t39;
    goto me2;
mn35:;
ma8:;
    gm_tt7vn9ehj0acr t41;
    memset(&t41, 0, sizeof t41);
    t1 = t41;
    goto me2;
mn40:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 52, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0) { return gm_f_shift_op_sro3o9l(&a0); }
