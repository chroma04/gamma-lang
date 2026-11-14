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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint16_t f0; uint16_t f1; } gm_tuqvtbj0p3v9c;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
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
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_tuqvtbj0p3v9c) == 4 && _Alignof(gm_tuqvtbj0p3v9c) == 2 && offsetof(gm_tuqvtbj0p3v9c, f0) == 0 && offsetof(gm_tuqvtbj0p3v9c, f1) == 2, "layout of `(u16, u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; uint64_t f6; uint8_t f7; uint8_t f8; uint8_t pe[6]; } gm_t4j35k4k4pvmb;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tiilmfd16v3fc v1; } f1; } gm_t716e5hdn64ba;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
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
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_t0mglksrkfa5e f0; uint8_t f1; uint8_t pe[7]; } gm_tnpj2jtv1r8pa;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4j35k4k4pvmb) == 152 && _Alignof(gm_t4j35k4k4pvmb) == 8 && offsetof(gm_t4j35k4k4pvmb, f0) == 0 && offsetof(gm_t4j35k4k4pvmb, f1) == 16 && offsetof(gm_t4j35k4k4pvmb, f2) == 32 && offsetof(gm_t4j35k4k4pvmb, f3) == 48 && offsetof(gm_t4j35k4k4pvmb, f4) == 56 && offsetof(gm_t4j35k4k4pvmb, f5) == 96 && offsetof(gm_t4j35k4k4pvmb, f6) == 136 && offsetof(gm_t4j35k4k4pvmb, f7) == 144 && offsetof(gm_t4j35k4k4pvmb, f8) == 145, "layout of `ArgBinder`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t716e5hdn64ba) == 32 && _Alignof(gm_t716e5hdn64ba) == 8 && offsetof(gm_t716e5hdn64ba, f0) == 0 && offsetof(gm_t716e5hdn64ba, f1) == 8, "layout of `EffectNamed`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t716e5hdn64ba f0; uint8_t f1; uint8_t pe[7]; } gm_tlbrdhah49ci5;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnpj2jtv1r8pa) == 48 && _Alignof(gm_tnpj2jtv1r8pa) == 8 && offsetof(gm_tnpj2jtv1r8pa, f0) == 0 && offsetof(gm_tnpj2jtv1r8pa, f1) == 40, "layout of `?FieldDef`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tlbrdhah49ci5) == 40 && _Alignof(gm_tlbrdhah49ci5) == 8 && offsetof(gm_tlbrdhah49ci5, f0) == 0 && offsetof(gm_tlbrdhah49ci5, f1) == 32, "layout of `?EffectNamed`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_arg_value_gavegdu {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* lit */
    gm_trqfvmjtt4sij l4; /* pre */
    void * ret;
};
struct gm_fr_argument_order_jhg33u8 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* order */
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_assert__dllohqd {
    gm_frame h;
    gm_t7te2tdpebbt4 l5; /* a */
    gm_tc3oht4ksth9d l6; /* c */
    gm_tc3oht4ksth9d l8; /* x */
    gm_trqfvmjtt4sij l10; /* both */
    void * ret;
};
struct gm_fr_bind_args_dnih8ih {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* named_here */
    gm_t7te2tdpebbt4 l9; /* a */
    gm_trqfvmjtt4sij l11; /* positional */
    gm_t5v0c8ia6a5mo l15; /* p */
    gm_ts5t31j0bugq1 l18; /* packed */
    gm_trqfvmjtt4sij l22; /* out */
    gm_t4j35k4k4pvmb l28; /* b */
    gm_trqfvmjtt4sij l31; /* stmts */
    gm_trqfvmjtt4sij l32; /* written */
    gm_t7te2tdpebbt4 l35; /* a */
    gm_tc3oht4ksth9d l36; /* e */
    gm_tc3oht4ksth9d l40; /* e */
    gm_tc3oht4ksth9d l43; /* e */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_bind_spread_parts_lmknlmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_call_decl_sj6f05u {
    gm_frame h;
    void * ret;
};
struct gm_fr_call_fn_2prsqv9 {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_trqfvmjtt4sij l10; /* vals */
    gm_t7te2tdpebbt4 l13; /* a */
    void * ret;
};
struct gm_fr_call_l10ebge {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* a */
    gm_t5vbkdq81qfs7 l8; /* r */
    gm_t0ialelq3ql1k l9; /* v */
    gm_t930q5l38hdap l22; /* b */
    void * ret;
};
struct gm_fr_call_value_t2nv21i {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* f */
    gm_tddtjfggdat0d l6; /* f */
    gm_tddtjfggdat0d l7; /* sig */
    gm_trqfvmjtt4sij l8; /* params */
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_construct_optional_cffo1ol {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* x */
    void * ret;
};
struct gm_fr_conversion_2qjq323 {
    gm_frame h;
    gm_tpautr4br7h3l l6; /* def */
    gm_tc3oht4ksth9d l10; /* x */
    gm_tnpj2jtv1r8pa l11; /* found */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t0mglksrkfa5e l15; /* f */
    gm_trqfvmjtt4sij l17; /* one */
    gm_tc3oht4ksth9d l26; /* x */
    void * ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
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
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_moftl2f {
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
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_expect_eq_iu4ojmi {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* a */
    gm_tc3oht4ksth9d l4; /* b */
    gm_trqfvmjtt4sij l15; /* one */
    gm_trqfvmjtt4sij l17; /* pieces */
    gm_trqfvmjtt4sij l18; /* specs */
    gm_trqfvmjtt4sij l19; /* vals */
    gm_trqfvmjtt4sij l23; /* stop_args */
    gm_trqfvmjtt4sij l25; /* fail_stmts */
    gm_trqfvmjtt4sij l27; /* cond */
    gm_trqfvmjtt4sij l29; /* stmts */
    void * ret;
};
struct gm_fr_fill_defaults_3fq5ut6 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_const_ref_arg_s0f37he {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_literal_tree_om58rum {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qtk22f3 {
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
struct gm_fr_list_v5d1o9b_2 {
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
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_next_positional_t5fspc1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_param_index_78k4iut {
    gm_frame h;
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_print_bhebp9d {
    gm_frame h;
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tc3oht4ksth9d l7; /* template */
    gm_trqfvmjtt4sij l10; /* one */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_recursive_without_ret_48l31rm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_tir_literal_oh960hv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trap_t21p26s {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* m */
    void * ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variadic_arg_dfdnlo3 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_equalize_ni1r5ra(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8fudcr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9ijbt7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ivju6qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mq9dnpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0);
__attribute__((aligned(4))) void gm_f_note_fixed_by_ev84og7(void * a0, void * a1, gm_csguankko4lmhd a2, gm_cseahvg8v7p0vs a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0);
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_untyped_literal_pfu1ft4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_krbc9rueul4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1ekjot5tdpje[67];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s3231a310mran[10];
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_s8vts15kskc6n[11];
extern const uint8_t gm_s98peh781o3et[51];
extern const uint8_t gm_s9ur7sj9r0g63[7];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sbs1lj6q8rfiu[44];
extern const uint8_t gm_scgahod5hue9b[56];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgnvdmoebn4i7[16];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sijukd7vsocnt[68];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sj4n6nvsgq2jn[43];
extern const uint8_t gm_smadkict0htlf[38];
extern const uint8_t gm_sn937bfucthvg[4];
extern const uint8_t gm_so4gnucc3fl4v[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srch6h8rdicu443[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgp1m0q2ut8m[12];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_ss6g2koqn63bf[5];
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
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = gm_l2;
        void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = 0;
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK_SHALLOW(&F, 10u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 5, 0, 3);
    memset((uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 21, 0, 3);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 0, ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 1, t1);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 2, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 3, ((uint8_t)0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 4, ((uint8_t)0));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 8, ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 12, t2);
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&(*(gm_t7g88i52rrtm5 *)gm_out) + 20, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_moftl2f F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 68u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = gm_f_hoists_13k2a2s(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        void * t10 = gm_f_prefix_operand_3n5jchh(t9);
        void * t11 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = t3;
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t5;
    }
    {
        void * t9 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t10 != ((uint8_t)49ULL)) goto ie8;
        gm_tet9guhev5r4u t11 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t9) + 8) + 8));
        F.l2 = t11;
        {
            uint8_t t13 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l2) + 0) + 0) + 8) == 15) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l2) + 0) + 0)), gm_sa9e77lq9q04p, 15) == 0));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix8:;
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qtk22f3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_moftl2f();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcg82mujgottmf, 140, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t8 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_recursive_without_ret_48l31rm F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l2;
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_c8qq9n9((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcl43af23eher3, 280, 20);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_b0_1 t11 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t10);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_arg_value_gavegdu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l5; /* v */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 905u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t0mfir12leg2g t6;
        gm_f_iterator_arg_r0if28f((void *)&t6, (void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0mfir12leg2g t7 = t6;
        if (!((((uint8_t *)&t7))[88] != 0)) goto ie2;
        gm_tc3oht4ksth9d t8 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&t7));
        F.l3 = t8;
        {
            void * t10 = gm_l0;
            gm_trqfvmjtt4sij t11 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t10) + 1160));
            F.l4 = t11;
        }
        {
            void * t12 = gm_l0;
            uint8_t *t13 = (((uint8_t *)t12) + 1160);
            gm_f_list_ir0h3tc_2((void *)&(*(gm_trqfvmjtt4sij *)t13), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t14 = gm_l0;
            void * t15 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t16 = gm_l2;
            void * t17 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t17;
        }
        {
            void * t18 = gm_l0;
            gm_trqfvmjtt4sij t19 = F.l4;
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t18) + 1160), t19);
        }
        {
            void * t20 = gm_l5;
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t23 = gm_l1;
        uint8_t t24 = ((GM_LD(uint8_t, (((uint8_t *)t23) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t24) goto ie22;
        {
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l2;
            uint8_t t29 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = (uint8_t)!t29;
            if (!t30) goto ie26;
            {
                void * t32 = gm_l0;
                void * t33 = gm_l1;
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
                void * t35 = gm_l0;
                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                gm_t88rcucg1b7uk t37 = gm_l2;
                gm_b16_8 t38 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t39 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t32, t34, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            gm_t88rcucg1b7uk t40 = gm_l2;
            gm_teti411eujvf4 t41;
            memset(&t41, 0, sizeof t41);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)8ULL));
            void * t42 = gm_l1;
            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
            void * t44 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t41, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t46 = gm_l0;
        uint8_t *t47 = (((uint8_t *)t46) + 472);
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
        void * t50 = gm_l1;
        gm_t88rcucg1b7uk t51 = gm_l2;
        uint8_t t52 = gm_f_is_const_ref_arg_s0f37he((void *)NULL, cx, NULL, t49, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, t47, t52);
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l1;
        gm_t88rcucg1b7uk t55 = gm_l2;
        gm_tkn42u7na3t81 t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56), t55);
        ((uint8_t *)&t56)[4] = 1;
        void * t57 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t53, t54, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t57;
    }
    {
        void * t58 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t58) + 472), ((uint8_t)0));
    }
    {
        void * t60 = gm_l0;
        void * t61 = gm_l1;
        void * t62 = gm_l6;
        gm_t88rcucg1b7uk t63 = gm_l2;
        uint8_t t64 = gm_f_iterator_call_as_value_00i5mch((void *)NULL, cx, NULL, t60, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t64) goto ie59;
        {
            void * t66 = gm_l0;
            void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
            void * t68 = gm_l1;
            gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
            void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t67, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t72 = gm_l0;
        void * t73 = gm_l6;
        gm_t88rcucg1b7uk t74 = gm_l2;
        void * t75 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t72, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t75;
    }
    {
        void * t76 = gm_l0;
        void * t77 = gm_l6;
        gm_t88rcucg1b7uk t78 = gm_l2;
        void * t79 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t76, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_argument_order_jhg33u8 F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* args */
    uint8_t gm_l2; /* has_spread */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* spreading */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    GM_STACK_CHECK(&F, 643u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 144));
                    F.l6 = t15;
                    {
                        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        switch (t22) { case ((uint8_t)0ULL): goto ma18; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma20; default: goto ma21; }
ma18:;
                        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t24 != ((uint8_t)0ULL)) goto mn23;
                        void * t25 = (void *)((uint8_t *)&F.l3);
                        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        gm_tftb1odc739t5 t27 = gm_f_push_ucgmlo2(t25, t26);
                        gm_tftb1odc739t5 t28 = t27;
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srch6h8rdicu443, 289, 31);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        goto me17;
mn23:;
ma19:;
                        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t31 != ((uint8_t)1ULL)) goto mn30;
                        gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l6) + 0) + 8) + 0));
                        gm_l7 = t32;
                        {
                            gm_b16_8 t35 = gm_l0;
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                            gm_tf5srh4mp4b3i t37 = gm_f_param_index_78k4iut(t35, t36);
                            gm_tf5srh4mp4b3i t38 = t37;
                            if (!((((uint8_t *)&t38))[8] != 0)) goto ie34;
                            uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t38));
                            gm_l8 = t39;
                            {
                                void * t41 = (void *)((uint8_t *)&F.l3);
                                uint64_t t42 = gm_l8;
                                gm_tftb1odc739t5 t43 = gm_f_push_ucgmlo2(t41, t42);
                                gm_tftb1odc739t5 t44 = t43;
                                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srch6h8rdicu443, 291, 55);
                                gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                            }
                            goto ix34;
ie34:;
ix34:;
                        }
                        goto me17;
mn30:;
ma20:;
                        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t47 != ((uint8_t)2ULL)) goto mn46;
                        {
                            gm_l2 = ((uint8_t)1);
                        }
                        goto me17;
mn46:;
ma21:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 288, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 287, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t50 = gm_l5;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l5 = t53;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t54 = gm_l2;
        gm_l9 = t54;
    }
    {
        {
            gm_l11 = ((uint64_t)1ULL);
        }
        {
            uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
            uint64_t t57 = (t56 > ((uint64_t)1ULL) ? t56 : ((uint64_t)1ULL));
            gm_l10 = t57;
        }
        {
            for (;;) {
                uint64_t t59 = gm_l11;
                uint64_t t60 = gm_l10;
                uint8_t t61 = (t59 < t60);
                if (!t61) goto lb58;
                {
                    uint64_t t64 = gm_l11;
                    uint64_t t65 = t64;
                    uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&t65));
                    gm_l12 = t68;
                    {
                        uint64_t t71 = gm_l12;
                        uint64_t r72; bool o73 = __builtin_sub_overflow(t71, ((uint64_t)1ULL), &r72);
                        if (o73) gm_trap("integer overflow in `-`", gm_srch6h8rdicu443, 298, 24);
                        uint64_t t74 = r72;
                        uint64_t t75 = (uintptr_t)t74;
                        uint64_t t76 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srch6h8rdicu443, 298, 12);
                        uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t75 * 8));
                        uint64_t t78 = gm_l12;
                        uint64_t t79 = (uintptr_t)t78;
                        uint64_t t80 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t79 >= (uintptr_t)(t80)) gm_trap_index(t79, t80, gm_srch6h8rdicu443, 298, 33);
                        uint64_t t81 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t79 * 8));
                        uint8_t t82 = (t77 > t81);
                        if (!t82) goto ie70;
                        {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto ix70;
ie70:;
ix70:;
                    }
                    goto me66;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 297, 5);
me66:;
                }
lc58:;
                {
                    uint64_t t85 = gm_l11;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l11 = t88;
                }
            }
lb58:;
        }
    }
    {
        uint8_t t89 = gm_l2;
        uint8_t t90 = gm_l9;
        gm_tqs5p4n2tq19v t91;
        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), t89);
        GM_ST(uint8_t, ((uint8_t *)&t91 + 1), t90);
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
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_assert__dllohqd F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l7; /* cond */
    void * gm_l9; /* m */
    GM_STACK_CHECK(&F, 1348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 144));
                    F.l5 = t15;
                    {
                        uint8_t t18 = 0;
                        uint8_t t19 = 0;
                        uint8_t t20;
                        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 0) + 0));
                        if (t23 != ((uint8_t)0ULL)) goto mn22;
                        t20 = ((uint8_t)0);
                        goto me21;
mn22:;
                        t20 = ((uint8_t)1);
                        goto me21;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 527, 12);
me21:;
                        if (!t20) {
                            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint8_t t26 = (t25 > ((uint64_t)2ULL));
                            t19 = t26;
                        } else t19 = 1;
                        if (!t19) {
                            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                            t18 = t28;
                        } else t18 = 1;
                        if (!t18) goto ie17;
                        {
                            void * t30 = gm_l0;
                            gm_tl874t2atnlh3 t31 = gm_l2;
                            gm_b16_8 t32;
                            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s98peh781o3et));
                            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(50));
                            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t34 = gm_l0;
                            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                            gm_tl874t2atnlh3 t36 = gm_l2;
                            void * t37 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint8_t t38 = 0;
                            F.ret = t37;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 526, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
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
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
        if (!t46) goto ie44;
        {
            void * t48 = gm_l0;
            gm_tl874t2atnlh3 t49 = gm_l2;
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s98peh781o3et));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(50));
            gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t48, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l2;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srch6h8rdicu443, 536, 23);
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t58 * 144) + 0) + 0));
        if (t60 != ((uint8_t)0ULL)) goto ie57;
        gm_tc3oht4ksth9d t61 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t58 * 144) + 0) + 8));
        F.l6 = t61;
        {
            void * t63 = gm_l0;
            void * t64 = (void *)((uint8_t *)&F.l6);
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t88rcucg1b7uk t68 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t69), t68);
            ((uint8_t *)&t69)[4] = 1;
            void * t70 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t63, t64, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t70;
        }
        {
            void * t71 = gm_l0;
            void * t72 = gm_l7;
            void * t73 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t73;
        }
        {
            void * t74;
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t77 = ((t76 == ((uint64_t)2ULL)));
            if (!t77) goto ie75;
            void * t79;
            uint64_t t81 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_srch6h8rdicu443, 540, 27);
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t81 * 144) + 0) + 0));
            if (t83 != ((uint8_t)0ULL)) goto ie80;
            gm_tc3oht4ksth9d t84 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t81 * 144) + 0) + 8));
            F.l8 = t84;
            void * t86 = gm_l0;
            void * t87 = gm_l0;
            void * t88 = (void *)((uint8_t *)&F.l8);
            void * t89 = gm_l0;
            void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
            void * t91 = (void *)(((uint8_t *)t90) + 8);
            gm_t88rcucg1b7uk t92 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t93;
            GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93), t92);
            ((uint8_t *)&t93)[4] = 1;
            void * t94 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t87, t88, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t95 = gm_l0;
            void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
            void * t97 = (void *)(((uint8_t *)t96) + 8);
            gm_t88rcucg1b7uk t98 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t99 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t86, t94, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t100 = t99;
            t79 = t100;
            goto ix80;
ie80:;
            void * t102 = gm_l0;
            void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
            gm_tl874t2atnlh3 t104 = gm_l2;
            void * t105 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t106 = 0;
            F.ret = t105;
            goto gm_fret;
            t79 = ((void *)0);
ix80:;
            void * t107 = t79;
            t74 = t107;
            goto ix75;
ie75:;
            void * t109 = gm_l0;
            void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
            void * t111 = (void *)(((uint8_t *)t110) + 8);
            gm_t88rcucg1b7uk t112 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_spi8n2atkg603));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(16));
            gm_teti411eujvf4 t114;
            memset(&t114, 0, sizeof t114);
            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)5ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
            gm_tl874t2atnlh3 t115 = gm_l2;
            void * t116 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t112, &t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t117 = t116;
            t74 = t117;
ix75:;
            gm_l9 = t74;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t118 = (void *)((uint8_t *)&F.l10);
            void * t119 = gm_l7;
            gm_tftb1odc739t5 t120 = gm_f_push_d943vgp_2(t118, &(*(gm_t69m56meqefs8 *)((uint8_t *)t119)));
            gm_tftb1odc739t5 t121 = t120;
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srch6h8rdicu443, 546, 5);
            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        }
        {
            void * t123 = (void *)((uint8_t *)&F.l10);
            void * t124 = gm_l9;
            gm_tftb1odc739t5 t125 = gm_f_push_d943vgp_2(t123, &(*(gm_t69m56meqefs8 *)((uint8_t *)t124)));
            gm_tftb1odc739t5 t126 = t125;
            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srch6h8rdicu443, 547, 5);
            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
        }
        {
            void * t128 = gm_l0;
            void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
            void * t130 = (void *)(((uint8_t *)t129) + 8);
            gm_t88rcucg1b7uk t131 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt8o5kebou62r t132;
            memset((uint8_t *)&t132 + 12, 0, 4);
            gm_tvdg94bk7aa22 t133;
            GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t133 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)61ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t132 + 0, t133);
            gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t132 + 16, t134);
            gm_teti411eujvf4 t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t135 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t135 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t135 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t135 + 8), t132);
            gm_tl874t2atnlh3 t136 = gm_l2;
            void * t137 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t131, &t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t138 = 0;
            F.ret = t137;
            goto gm_fret;
        }
        goto ix57;
ie57:;
        {
            void * t140 = gm_l0;
            void * t141 = GM_LD(void *, (((uint8_t *)t140) + 0));
            gm_tl874t2atnlh3 t142 = gm_l2;
            void * t143 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t144 = 0;
            F.ret = t143;
            goto gm_fret;
        }
ix57:;
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
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5) {
    struct gm_fr_bind_args_dnih8ih F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* params */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* args0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* what */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l10; /* n */
    uint64_t gm_l12; /* max */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l16; /* ok */
    gm_b16_8 gm_l17; /* args */
    gm_b16_8 gm_l19; /* a */
    gm_tl874t2atnlh3 gm_l20; /* sp */
    gm_b16_8 gm_l21; /* msg */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tqs5p4n2tq19v gm_l25; /* order */
    uint8_t gm_l26; /* has_spread */
    uint8_t gm_l27; /* spreading */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l37; /* i */
    void * gm_l38; /* v */
    gm_t08olq97rscn3 gm_l39; /* n */
    uint64_t gm_l41; /* i */
    void * gm_l42; /* v */
    gm_b16_8 gm_l44; /* parts */
    GM_STACK_CHECK(&F, 3691u);
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
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l7 = t3;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l8;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l8;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t7te2tdpebbt4 t15 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t11 * 144));
                    F.l9 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 0) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l9) + 0) + 8) + 0));
                        gm_l10 = t19;
                        {
                            void * t21 = (void *)((uint8_t *)&F.l6);
                            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                            gm_tftb1odc739t5 t23 = gm_f_push_3ef746b(t21, t22);
                            gm_tftb1odc739t5 t24 = t23;
                            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srch6h8rdicu443, 207, 37);
                            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 206, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t27 = gm_l8;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l8 = t30;
                }
            }
lb4:;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l12 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t32 = gm_l1;
            gm_l13 = t32;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t34 = gm_l14;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t36 = (t34 < t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = gm_l14;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_t5v0c8ia6a5mo t44 = GM_LD(gm_t5v0c8ia6a5mo, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t40 * 56));
                    F.l15 = t44;
                    {
                        uint8_t t46 = 0;
                        void * t47 = (void *)((uint8_t *)&F.l15);
                        uint8_t t48 = gm_f_is_site_8sr7vam(t47);
                        uint8_t t49 = (uint8_t)!t48;
                        if (t49) {
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            gm_b16_8 t52 = t51;
                            uint8_t t53 = 0;
                            gm_b16_8 t54 = t50;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t52) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t52))) + i * 16, ((uint8_t *)&t54))) { t53 = 1; break; }
                            uint8_t t55 = (uint8_t)(!t53);
                            t46 = t55;
                        } else t46 = 0;
                        gm_l16 = t46;
                    }
                    {
                        void * t56 = (void *)((uint8_t *)&F.l11);
                        uint8_t t57 = gm_l16;
                        gm_tftb1odc739t5 t58 = gm_f_push_vd1290p(t56, t57);
                        gm_tftb1odc739t5 t59 = t58;
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srch6h8rdicu443, 213, 9);
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                    }
                    {
                        uint8_t t62 = gm_l16;
                        if (!t62) goto ie61;
                        {
                            uint64_t t64 = gm_l12;
                            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                            if (o66) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 214, 17);
                            uint64_t t67 = r65;
                            gm_l12 = t67;
                        }
                        goto ix61;
ie61:;
ix61:;
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 211, 5);
me42:;
                }
lc33:;
                {
                    uint64_t t69 = gm_l14;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l14 = t72;
                }
            }
lb33:;
        }
    }
    {
        gm_b16_8 t73 = gm_l3;
        gm_l17 = t73;
    }
    {
        gm_b16_8 t75 = gm_l1;
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t77 = t76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b16_8 t79 = gm_l3;
        gm_tl874t2atnlh3 t80 = gm_l4;
        gm_tck5nas0nf5v8 t81;
        gm_f_pack_rest_args_cknfrfl((void *)&t81, (void *)NULL, cx, NULL, t75, t78, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tck5nas0nf5v8 t82 = t81;
        if (!((((uint8_t *)&t82))[40] != 0)) goto ie74;
        gm_ts5t31j0bugq1 t83 = GM_LD(gm_ts5t31j0bugq1, ((uint8_t *)&t82));
        F.l18 = t83;
        {
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t87 != ((uint8_t)0ULL)) goto mn86;
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 8));
            gm_l19 = t88;
            {
                gm_b16_8 t90 = gm_l19;
                gm_b16_8 t91 = t90;
                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                gm_l17 = t92;
            }
            goto me85;
mn86:;
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t94 != ((uint8_t)1ULL)) goto mn93;
            gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l18) + 8) + 0));
            gm_l20 = t95;
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 8) + 16));
            gm_l21 = t96;
            {
                void * t98 = gm_l0;
                gm_tl874t2atnlh3 t99 = gm_l20;
                gm_b16_8 t100 = gm_l21;
                gm_b0_1 t101 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t98, t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l22, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t103 = gm_l1;
                    gm_l23 = t103;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t105 = gm_l24;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t107 = (t105 < t106);
                        if (!t107) goto lb104;
                        {
                            uint64_t t110 = gm_l24;
                            uint64_t t111 = (uintptr_t)t110;
                            uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            {
                                void * t116 = (void *)((uint8_t *)&F.l22);
                                void * t117 = gm_l0;
                                void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
                                gm_tl874t2atnlh3 t119 = gm_l4;
                                void * t120 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t119);
                                if (gm_unw.kind) {
                                    goto gm_ll108;
                                }
                                gm_tftb1odc739t5 t121 = gm_f_push_d943vgp_2(t116, &(*(gm_t69m56meqefs8 *)((uint8_t *)t120)));
                                gm_tftb1odc739t5 t122 = t121;
                                if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 1, gm_srch6h8rdicu443, 223, 35);
                                gm_b0_1 t123 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 1));
                            }
                            goto me113;
mn114:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 223, 17);
me113:;
                        }
lc104:;
                        {
                            uint64_t t125 = gm_l24;
                            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                            uint64_t t128 = r126;
                            gm_l24 = t128;
                        }
                    }
gm_ll108:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
                    }
                    goto gm_lad;
lb104:;
                }
            }
            {
                gm_t6j6smj7qoctu t129;
                gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t129 + 0, t130);
                gm_b16_8 t131 = gm_f_empty_slice_vj3ckdj_2();
                GM_ST(gm_b16_8, (uint8_t *)&t129 + 16, t131);
                gm_ttu09lij36f8c t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 36, 0);
                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t132), t129);
                ((uint8_t *)&t132)[32] = 1;
                uint8_t t133 = 0;
                F.ret = t132;
                goto gm_fret;
            }
            goto me85;
mn93:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 218, 9);
me85:;
        }
        goto ix74;
ie74:;
ix74:;
    }
    {
        gm_b16_8 t134 = gm_l1;
        gm_b16_8 t135 = gm_l17;
        gm_tqs5p4n2tq19v t136 = gm_f_argument_order_jhg33u8((void *)NULL, cx, NULL, t134, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t136;
    }
    {
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
        gm_l26 = t137;
    }
    {
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 1));
        gm_l27 = t138;
    }
    {
        memset((uint8_t *)&F.l28 + 146, 0, 6);
        gm_b16_8 t139 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 0, t139);
        gm_b16_8 t140 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 16, t140);
        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t142 = t141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
        GM_ST(gm_b16_8, (uint8_t *)&F.l28 + 32, t143);
        uint64_t t144 = gm_l12;
        GM_ST(uint64_t, (uint8_t *)&F.l28 + 48, t144);
        gm_f_list_pmp06kq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28 + 56)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_s83jn2q((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l28 + 96)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&F.l28 + 136, ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&F.l28 + 144, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&F.l28 + 145, ((uint8_t)0));
    }
    {
        {
            gm_b16_8 t146 = gm_l1;
            gm_l29 = t146;
        }
        {
            gm_l30 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t148 = gm_l30;
                uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                uint8_t t150 = (t148 < t149);
                if (!t150) goto lb147;
                {
                    uint64_t t153 = gm_l30;
                    uint64_t t154 = (uintptr_t)t153;
                    uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    {
                        void * t159 = (void *)(((uint8_t *)&F.l28) + 56);
                        gm_tftb1odc739t5 t160 = gm_f_push_r2k6rjf(t159, ((void *)0));
                        gm_tftb1odc739t5 t161 = t160;
                        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srch6h8rdicu443, 233, 9);
                        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                    }
                    {
                        void * t163 = (void *)(((uint8_t *)&F.l28) + 96);
                        gm_tftb1odc739t5 t164 = gm_f_push_vd1290p(t163, ((uint8_t)0));
                        gm_tftb1odc739t5 t165 = t164;
                        if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srch6h8rdicu443, 234, 9);
                        gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
                    }
                    goto me156;
mn157:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 232, 5);
me156:;
                }
lc147:;
                {
                    uint64_t t168 = gm_l30;
                    uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                    uint64_t t171 = r169;
                    gm_l30 = t171;
                }
            }
lb147:;
        }
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t173 = gm_l17;
            gm_l33 = t173;
        }
        {
            gm_l34 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t175 = gm_l34;
                uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                uint8_t t177 = (t175 < t176);
                if (!t177) goto lb174;
                {
                    uint64_t t180 = gm_l34;
                    uint64_t t181 = (uintptr_t)t180;
                    uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    gm_t7te2tdpebbt4 t185 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t181 * 144));
                    F.l35 = t185;
                    {
                        uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        switch (t192) { case ((uint8_t)0ULL): goto ma188; case ((uint8_t)1ULL): goto ma189; case ((uint8_t)2ULL): goto ma190; default: goto ma191; }
ma188:;
                        uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t194 != ((uint8_t)0ULL)) goto mn193;
                        gm_tc3oht4ksth9d t195 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l35) + 0) + 8));
                        F.l36 = t195;
                        {
                            uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&F.l28) + 144));
                            if (!t198) goto ie197;
                            {
                                void * t200 = gm_l0;
                                gm_tl874t2atnlh3 t201 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                gm_b16_8 t202;
                                GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s1ekjot5tdpje));
                                GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(66));
                                gm_b0_1 t203 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t200, t201, t202);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                            }
                            goto ix197;
ie197:;
ix197:;
                        }
                        {
                            void * t204 = (void *)((uint8_t *)&F.l28);
                            gm_tf5srh4mp4b3i t205 = gm_f_next_positional_t5fspc1(t204);
                            gm_tf5srh4mp4b3i t206 = t205;
                            uint64_t t207;
                            if (((((uint8_t *)&t206))[8] != 0)) {
                                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&t206) + 0));
                                t207 = t208;
                            } else {
                                {
                                    void * t210 = gm_l0;
                                    gm_tl874t2atnlh3 t211 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                    gm_t3877pk7lv748 t212;
                                    gm_b16_8 t213 = gm_l5;
                                    GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
                                    uint64_t t214 = gm_l12;
                                    GM_ST(uint64_t, (uint8_t *)&t212 + 16, t214);
                                    gm_tabb710d9mq2e t215;
                                    gm_f_format_b4oiuht((void *)&t215, (void *)NULL, cx, NULL, &t212);
                                    if (gm_unw.kind) {
                                        goto gm_ll178;
                                    }
                                    gm_tabb710d9mq2e t216 = t215;
                                    if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 8, gm_srch6h8rdicu443, 243, 38);
                                    gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
                                    gm_b16_8 t218 = t217;
                                    gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                                    gm_b0_1 t220 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t210, t211, t219);
                                    if (gm_unw.kind) {
                                        goto gm_ll178;
                                    }
                                }
                                {
                                    {
                                        void * t222 = gm_l0;
                                        void * t223 = (void *)((uint8_t *)&F.l36);
                                        gm_tkn42u7na3t81 t224;
                                        memset(&t224, 0, sizeof t224);
                                        void * t225 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t222, t223, t224);
                                        if (gm_unw.kind) {
                                            goto gm_ll178;
                                        }
                                    }
                                }
                                goto lc174;
                            }
                            gm_l37 = t207;
                        }
                        {
                            void * t226 = gm_l0;
                            void * t227 = (void *)((uint8_t *)&F.l36);
                            uint64_t t228 = gm_l37;
                            uint64_t t229 = (uintptr_t)t228;
                            uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t229 >= (uintptr_t)(t230)) gm_trap_index(t229, t230, gm_srch6h8rdicu443, 247, 39);
                            gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t229 * 56) + 16));
                            void * t232 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t226, t227, t231);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_l38 = t232;
                        }
                        {
                            uint8_t t234 = gm_l27;
                            if (!t234) goto ie233;
                            {
                                void * t236 = gm_l0;
                                void * t237 = gm_l38;
                                void * t238 = (void *)((uint8_t *)&F.l31);
                                void * t239 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t236, t237, t238);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                                gm_l38 = t239;
                            }
                            goto ix233;
ie233:;
ix233:;
                        }
                        {
                            void * t240 = gm_l0;
                            void * t241 = (void *)(((uint8_t *)&F.l28) + 56);
                            void * t242 = (void *)((uint8_t *)&F.l32);
                            uint64_t t243 = gm_l37;
                            void * t244 = (void *)((uint8_t *)&F.l31);
                            gm_b0_1 t245 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t240, t241, t242, t243, t244);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                        }
                        {
                            uint64_t t246 = gm_l37;
                            uint64_t t247 = (uintptr_t)t246;
                            uint64_t t248 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                            if (t247 >= (uintptr_t)(t248)) gm_trap_index(t247, t248, gm_srch6h8rdicu443, 250, 17);
                            void * t249 = gm_l38;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t247 * 8), t249);
                        }
                        {
                            uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 136));
                            uint64_t r251; bool o252 = __builtin_add_overflow(t250, ((uint64_t)1ULL), &r251);
                            if (o252) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 251, 17);
                            uint64_t t253 = r251;
                            GM_ST(uint64_t, (((uint8_t *)&F.l28) + 136), t253);
                        }
                        goto me187;
mn193:;
ma189:;
                        uint8_t t255 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t255 != ((uint8_t)1ULL)) goto mn254;
                        gm_t08olq97rscn3 t256 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l35) + 0) + 8) + 0));
                        gm_l39 = t256;
                        gm_tc3oht4ksth9d t257 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l35) + 0) + 8) + 32));
                        F.l40 = t257;
                        {
                            gm_b16_8 t259 = gm_l1;
                            gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                            gm_tf5srh4mp4b3i t261 = gm_f_param_index_78k4iut(t259, t260);
                            gm_tf5srh4mp4b3i t262 = t261;
                            uint64_t t263;
                            if (((((uint8_t *)&t262))[8] != 0)) {
                                uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&t262) + 0));
                                t263 = t264;
                            } else {
                                {
                                    void * t266 = gm_l0;
                                    gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                    gm_t6j6smj7qoctu t268;
                                    gm_b16_8 t269 = gm_l5;
                                    GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t269);
                                    gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t268 + 16, t270);
                                    gm_tabb710d9mq2e t271;
                                    gm_f_format_ivju6qo((void *)&t271, (void *)NULL, cx, NULL, &t268);
                                    if (gm_unw.kind) {
                                        goto gm_ll178;
                                    }
                                    gm_tabb710d9mq2e t272 = t271;
                                    if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 8, gm_srch6h8rdicu443, 255, 38);
                                    gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&t272) + 8));
                                    gm_b16_8 t274 = t273;
                                    gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
                                    gm_b0_1 t276 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t266, t267, t275);
                                    if (gm_unw.kind) {
                                        goto gm_ll178;
                                    }
                                }
                                {
                                    {
                                        void * t278 = gm_l0;
                                        void * t279 = (void *)((uint8_t *)&F.l40);
                                        gm_tkn42u7na3t81 t280;
                                        memset(&t280, 0, sizeof t280);
                                        void * t281 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t278, t279, t280);
                                        if (gm_unw.kind) {
                                            goto gm_ll178;
                                        }
                                    }
                                }
                                goto lc174;
                            }
                            gm_l41 = t263;
                        }
                        {
                            uint8_t t283 = 0;
                            uint64_t t284 = gm_l41;
                            uint64_t t285 = (uintptr_t)t284;
                            uint64_t t286 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 96) + 0) + 8);
                            if (t285 >= (uintptr_t)(t286)) gm_trap_index(t285, t286, gm_srch6h8rdicu443, 259, 20);
                            uint8_t t287 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 96) + 0)) + t285 * 1));
                            if (!t287) {
                                uint8_t t288 = 0;
                                uint64_t t289 = gm_l41;
                                uint64_t t290 = (uintptr_t)t289;
                                uint64_t t291 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                                if (t290 >= (uintptr_t)(t291)) gm_trap_index(t290, t291, gm_srch6h8rdicu443, 259, 44);
                                void * t292 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t290 * 8));
                                void * t293 = t292;
                                uint8_t t294;
                                if ((GM_LD(void *, ((uint8_t *)&t293)) != 0)) goto mn296;
                                t294 = ((uint8_t)0);
                                goto me295;
mn296:;
                                t294 = ((uint8_t)1);
                                goto me295;
mn297:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 259, 44);
me295:;
                                if (t294) {
                                    uint8_t t298 = gm_l26;
                                    uint8_t t299 = (uint8_t)!t298;
                                    t288 = t299;
                                } else t288 = 0;
                                t283 = t288;
                            } else t283 = 1;
                            if (!t283) goto ie282;
                            {
                                void * t301 = gm_l0;
                                gm_tl874t2atnlh3 t302 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                gm_b16_8 t303 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 0));
                                gm_tnpifv20irnph t304;
                                GM_ST(gm_b16_8, ((uint8_t *)&t304 + 0), t303);
                                gm_tabb710d9mq2e t305;
                                gm_f_format_hqkpc5s((void *)&t305, (void *)NULL, cx, NULL, t304);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                                gm_tabb710d9mq2e t306 = t305;
                                if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 8, gm_srch6h8rdicu443, 260, 38);
                                gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&t306) + 8));
                                gm_b16_8 t308 = t307;
                                gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                                gm_b0_1 t310 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t301, t302, t309);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                            }
                            goto ix282;
ie282:;
ix282:;
                        }
                        {
                            uint64_t t311 = gm_l41;
                            uint64_t t312 = (uintptr_t)t311;
                            uint64_t t313 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 96) + 0) + 8);
                            if (t312 >= (uintptr_t)(t313)) gm_trap_index(t312, t313, gm_srch6h8rdicu443, 262, 17);
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 96) + 0)) + t312 * 1), ((uint8_t)1));
                        }
                        {
                            void * t314 = gm_l0;
                            void * t315 = (void *)((uint8_t *)&F.l40);
                            uint64_t t316 = gm_l41;
                            uint64_t t317 = (uintptr_t)t316;
                            uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                            if (t317 >= (uintptr_t)(t318)) gm_trap_index(t317, t318, gm_srch6h8rdicu443, 264, 39);
                            gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t317 * 56) + 16));
                            void * t320 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t314, t315, t319);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_l42 = t320;
                        }
                        {
                            uint8_t t322 = gm_l27;
                            if (!t322) goto ie321;
                            {
                                void * t324 = gm_l0;
                                void * t325 = gm_l42;
                                void * t326 = (void *)((uint8_t *)&F.l31);
                                void * t327 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t324, t325, t326);
                                if (gm_unw.kind) {
                                    goto gm_ll178;
                                }
                                gm_l42 = t327;
                            }
                            goto ix321;
ie321:;
ix321:;
                        }
                        {
                            void * t328 = gm_l0;
                            void * t329 = (void *)(((uint8_t *)&F.l28) + 56);
                            void * t330 = (void *)((uint8_t *)&F.l32);
                            uint64_t t331 = gm_l41;
                            void * t332 = (void *)((uint8_t *)&F.l31);
                            gm_b0_1 t333 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t328, t329, t330, t331, t332);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                        }
                        {
                            uint64_t t334 = gm_l41;
                            uint64_t t335 = (uintptr_t)t334;
                            uint64_t t336 = GM_LD(uintptr_t, ((((uint8_t *)&F.l28) + 56) + 0) + 8);
                            if (t335 >= (uintptr_t)(t336)) gm_trap_index(t335, t336, gm_srch6h8rdicu443, 267, 17);
                            void * t337 = gm_l42;
                            GM_ST(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l28) + 56) + 0)) + t335 * 8), t337);
                        }
                        goto me187;
mn254:;
ma190:;
                        uint8_t t339 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 0) + 0));
                        if (t339 != ((uint8_t)2ULL)) goto mn338;
                        gm_tc3oht4ksth9d t340 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l35) + 0) + 8));
                        F.l43 = t340;
                        {
                            void * t342 = gm_l0;
                            void * t343 = (void *)((uint8_t *)&F.l43);
                            void * t344 = (void *)((uint8_t *)&F.l31);
                            gm_th2lqbo77pvsc t345;
                            gm_f_spread_parts_e62q2l2((void *)&t345, (void *)NULL, cx, NULL, t342, t343, t344);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                            gm_th2lqbo77pvsc t346 = t345;
                            gm_b16_8 t347;
                            if (((((uint8_t *)&t346))[16] != 0)) {
                                gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t346) + 0));
                                t347 = t348;
                            } else {
                                {
                                    GM_ST(uint8_t, (((uint8_t *)&F.l28) + 145), ((uint8_t)1));
                                }
                                goto lc174;
                            }
                            gm_l44 = t347;
                        }
                        {
                            void * t350 = gm_l0;
                            void * t351 = (void *)((uint8_t *)&F.l28);
                            gm_b16_8 t352 = gm_l44;
                            gm_b16_8 t353 = t352;
                            gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
                            gm_tl874t2atnlh3 t355 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 72));
                            gm_b0_1 t356 = gm_f_bind_spread_parts_lmknlmc((void *)NULL, cx, NULL, t350, t351, t354, t355);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                        }
                        goto me187;
mn338:;
ma191:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 239, 9);
me187:;
                    }
                    goto me183;
mn184:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 238, 5);
me183:;
                }
lc174:;
                {
                    uint64_t t358 = gm_l34;
                    uint64_t r359; bool o360 = __builtin_add_overflow(t358, ((uint64_t)1ULL), &r359);
                    uint64_t t361 = r359;
                    gm_l34 = t361;
                }
            }
gm_ll178:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb174; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc174; }
            }
            goto gm_lad;
lb174:;
        }
    }
    {
        void * t362 = gm_l0;
        void * t363 = (void *)((uint8_t *)&F.l28);
        uint64_t t364 = gm_l2;
        gm_b16_8 t365 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
        gm_tl874t2atnlh3 t366 = gm_l4;
        gm_t6j6smj7qoctu t367;
        gm_f_fill_defaults_3fq5ut6((void *)&t367, (void *)NULL, cx, NULL, t362, t363, t364, t365, t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t368;
        GM_ST(uint8_t, (uint8_t *)&t368 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t368 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t368 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t368), t367);
        ((uint8_t *)&t368)[32] = 1;
        uint8_t t369 = 0;
        F.ret = t368;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bind_spread_parts_lmknlmc F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* parts */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l6; /* part */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* n */
    uint64_t gm_l9; /* j */
    uint64_t gm_l10; /* j */
    GM_STACK_CHECK(&F, 1237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tpouv9r7lrri2 t15 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 32));
                    gm_l6 = t15;
                    {
                        gm_l7 = ((uint64_t)0ULL);
                    }
                    {
                        if (!(((((uint8_t *)&gm_l6) + 0))[16] != 0)) goto ie17;
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                        gm_l8 = t18;
                        {
                            void * t20 = gm_l1;
                            GM_ST(uint8_t, (((uint8_t *)t20) + 144), ((uint8_t)1));
                        }
                        {
                            void * t21 = gm_l1;
                            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
                            gm_b16_8 t23 = gm_l8;
                            gm_tf5srh4mp4b3i t24 = gm_f_param_index_78k4iut(t22, t23);
                            gm_tf5srh4mp4b3i t25 = t24;
                            uint64_t t26;
                            if (((((uint8_t *)&t25))[8] != 0)) {
                                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&t25) + 0));
                                t26 = t27;
                            } else {
                                {
                                    void * t29 = gm_l0;
                                    gm_tl874t2atnlh3 t30 = gm_l3;
                                    gm_t6j6smj7qoctu t31;
                                    void * t32 = gm_l1;
                                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t33);
                                    gm_b16_8 t34 = gm_l8;
                                    GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t34);
                                    gm_tabb710d9mq2e t35;
                                    gm_f_format_10ctmcq((void *)&t35, (void *)NULL, cx, NULL, &t31);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    gm_tabb710d9mq2e t36 = t35;
                                    if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srch6h8rdicu443, 311, 30);
                                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                                    gm_b16_8 t38 = t37;
                                    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                                    gm_b0_1 t40 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t29, t30, t39);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                }
                                {
                                    void * t41 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t41) + 145), ((uint8_t)1));
                                }
                                goto lb4;
                            }
                            gm_l9 = t26;
                        }
                        {
                            uint8_t t43 = 0;
                            uint64_t t44 = gm_l9;
                            uint64_t t45 = (uintptr_t)t44;
                            void * t46 = gm_l1;
                            uint64_t t47 = GM_LD(uintptr_t, ((((uint8_t *)t46) + 96) + 0) + 8);
                            if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_srch6h8rdicu443, 315, 16);
                            uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t46) + 96) + 0)) + t45 * 1));
                            if (!t48) {
                                uint64_t t49 = gm_l9;
                                uint64_t t50 = (uintptr_t)t49;
                                void * t51 = gm_l1;
                                uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 56) + 0) + 8);
                                if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_srch6h8rdicu443, 315, 39);
                                void * t53 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 56) + 0)) + t50 * 8));
                                void * t54 = t53;
                                uint8_t t55;
                                if ((GM_LD(void *, ((uint8_t *)&t54)) != 0)) goto mn57;
                                t55 = ((uint8_t)0);
                                goto me56;
mn57:;
                                t55 = ((uint8_t)1);
                                goto me56;
mn58:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 315, 39);
me56:;
                                t43 = t55;
                            } else t43 = 1;
                            if (!t43) goto ie42;
                            {
                                void * t60 = gm_l0;
                                gm_tl874t2atnlh3 t61 = gm_l3;
                                gm_b16_8 t62 = gm_l8;
                                gm_tnpifv20irnph t63;
                                GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                                gm_tabb710d9mq2e t64;
                                gm_f_format_hqkpc5s((void *)&t64, (void *)NULL, cx, NULL, t63);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                gm_tabb710d9mq2e t65 = t64;
                                if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srch6h8rdicu443, 316, 30);
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                                gm_b16_8 t67 = t66;
                                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                                gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t60, t61, t68);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            {
                                void * t70 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t70) + 145), ((uint8_t)1));
                            }
                            {
                                goto lb4;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        {
                            uint64_t t71 = gm_l9;
                            gm_l7 = t71;
                        }
                        goto ix17;
ie17:;
                        {
                            void * t74 = gm_l1;
                            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)t74) + 144));
                            if (!t75) goto ie73;
                            {
                                void * t77 = gm_l0;
                                gm_tl874t2atnlh3 t78 = gm_l3;
                                gm_b16_8 t79;
                                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s1ekjot5tdpje));
                                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(66));
                                gm_b0_1 t80 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t77, t78, t79);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            {
                                void * t81 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t81) + 145), ((uint8_t)1));
                            }
                            {
                                goto lb4;
                            }
                            goto ix73;
ie73:;
ix73:;
                        }
                        {
                            void * t82 = gm_l1;
                            gm_tf5srh4mp4b3i t83 = gm_f_next_positional_t5fspc1(t82);
                            gm_tf5srh4mp4b3i t84 = t83;
                            uint64_t t85;
                            if (((((uint8_t *)&t84))[8] != 0)) {
                                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&t84) + 0));
                                t85 = t86;
                            } else {
                                {
                                    void * t88 = gm_l0;
                                    gm_tl874t2atnlh3 t89 = gm_l3;
                                    gm_t3877pk7lv748 t90;
                                    void * t91 = gm_l1;
                                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t91) + 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t92);
                                    void * t93 = gm_l1;
                                    uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 48));
                                    GM_ST(uint64_t, (uint8_t *)&t90 + 16, t94);
                                    gm_tabb710d9mq2e t95;
                                    gm_f_format_b4oiuht((void *)&t95, (void *)NULL, cx, NULL, &t90);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                    gm_tabb710d9mq2e t96 = t95;
                                    if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 8, gm_srch6h8rdicu443, 329, 30);
                                    gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
                                    gm_b16_8 t98 = t97;
                                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                                    gm_b0_1 t100 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t88, t89, t99);
                                    if (gm_unw.kind) {
                                        goto gm_ll8;
                                    }
                                }
                                {
                                    void * t101 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t101) + 145), ((uint8_t)1));
                                }
                                goto lb4;
                            }
                            gm_l10 = t85;
                        }
                        {
                            uint64_t t102 = gm_l10;
                            gm_l7 = t102;
                        }
                        {
                            void * t103 = gm_l1;
                            void * t104 = gm_l1;
                            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)t104) + 136));
                            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                            if (o107) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 334, 13);
                            uint64_t t108 = r106;
                            GM_ST(uint64_t, (((uint8_t *)t103) + 136), t108);
                        }
ix17:;
                    }
                    {
                        uint64_t t109 = gm_l7;
                        uint64_t t110 = (uintptr_t)t109;
                        void * t111 = gm_l1;
                        uint64_t t112 = GM_LD(uintptr_t, ((((uint8_t *)t111) + 56) + 0) + 8);
                        if (t110 >= (uintptr_t)(t112)) gm_trap_index(t110, t112, gm_srch6h8rdicu443, 336, 9);
                        uint8_t *t113 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t111) + 56) + 0)) + t110 * 8);
                        void * t114 = gm_l0;
                        void * t115 = GM_LD(void *, (((uint8_t *)&gm_l6) + 24));
                        uint64_t t116 = gm_l7;
                        uint64_t t117 = (uintptr_t)t116;
                        void * t118 = gm_l1;
                        uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)t118) + 0) + 8);
                        if (t117 >= (uintptr_t)(t119)) gm_trap_index(t117, t119, gm_srch6h8rdicu443, 336, 50);
                        gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t118) + 0)) + t117 * 56) + 16));
                        void * t121 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t114, t115, t120);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(void *, t113, t121);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 306, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t123 = gm_l5;
                    uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                    uint64_t t126 = r124;
                    gm_l5 = t126;
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
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_call_decl_sj6f05u F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* callee */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* f */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* eff */
    uint32_t gm_l9; /* op */
    GM_STACK_CHECK(&F, 1020u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l1;
        gm_t6qooom8qa2do t5;
        gm_f_resolve_decl_pus042v((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t6 = t5;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma10; default: goto ma13; }
ma8:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t17 != ((uint8_t)5ULL)) goto mn16;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&t6) + 8));
        gm_l6 = t18;
        void * t19 = gm_l0;
        uint32_t t20 = gm_l6;
        gm_b16_8 t21 = gm_l4;
        gm_tl874t2atnlh3 t22 = gm_l5;
        void * t23 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t19, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me7;
mn16:;
ma9:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t26 != ((uint8_t)0ULL)) goto mn25;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l7 = t27;
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l7;
        gm_b16_8 t30 = gm_l4;
        gm_tl874t2atnlh3 t31 = gm_l5;
        void * t32 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t28, t29, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me7;
mn25:;
ma10:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t35 != ((uint8_t)11ULL)) goto mn34;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        gm_tl874t2atnlh3 t38 = gm_l5;
        void * t39 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me7;
mn34:;
ma11:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t42 != ((uint8_t)6ULL)) goto mn41;
        uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l8 = t43;
        uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 4));
        gm_l9 = t44;
        void * t45 = gm_l0;
        uint32_t t46 = gm_l8;
        uint32_t t47 = gm_l9;
        gm_b16_8 t48 = gm_l4;
        gm_tl874t2atnlh3 t49 = gm_l5;
        void * t50 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t45, t46, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = 0;
        F.ret = t50;
        goto gm_fret;
        goto me7;
mn41:;
ma12:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t53 != ((uint8_t)7ULL)) goto mn52;
        void * t54 = gm_l0;
        void * t55 = gm_l0;
        void * t56 = gm_l3;
        gm_tkn42u7na3t81 t57;
        memset(&t57, 0, sizeof t57);
        void * t58 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t55, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t59 = gm_l4;
        gm_tl874t2atnlh3 t60 = gm_l5;
        void * t61 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t54, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t62 = 0;
        F.ret = t61;
        goto gm_fret;
        goto me7;
mn52:;
ma13:;
        {
            void * t65 = gm_l0;
            void * t66 = gm_l3;
            gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
            gm_b16_8 t68 = gm_l2;
            gm_tnpifv20irnph t69;
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
            gm_tabb710d9mq2e t70;
            gm_f_format_8fudcr8((void *)&t70, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t71 = t70;
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srch6h8rdicu443, 136, 35);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
            gm_b16_8 t73 = t72;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_b0_1 t75 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t65, t67, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t76 = gm_l0;
            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
            gm_tl874t2atnlh3 t78 = gm_l5;
            void * t79 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = 0;
            F.ret = t79;
            goto gm_fret;
        }
        goto me7;
mn63:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 129, 5);
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
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_fn_2prsqv9 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* variadic */
    gm_b16_8 gm_l7; /* fixed */
    gm_t6j6smj7qoctu gm_l8; /* bound */
    gm_b16_8 gm_l9; /* all */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1570u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 344) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_srch6h8rdicu443, 144, 12);
        gm_tqqhoivahirr0 t8 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 344) + 0)) + t4 * 136));
        F.l4 = t8;
    }
    {
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 80) + 8);
        uint8_t t11 = (t10 > ((uint64_t)0ULL));
        if (!t11) goto ie9;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_l1;
            gm_b16_8 t15 = gm_l2;
            gm_tl874t2atnlh3 t16 = gm_l3;
            void * t17 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t13, t14, ((void *)0), t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t19 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 40) + 8);
        gm_l5 = t19;
    }
    {
        uint8_t t20 = 0;
        uint32_t t21 = gm_l1;
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t23) + 1368) + 0));
        gm_b16_8 t25 = t24;
        uint8_t t26 = 0;
        uint32_t t27 = t21;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t25) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t25))) + i * 4, ((uint8_t *)&t27))) { t26 = 1; break; }
        uint8_t t28 = (uint8_t)(t26);
        if (t28) {
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t30 = gm_l5;
            uint8_t t31 = (t29 > t30);
            t20 = t31;
        } else t20 = 0;
        gm_l6 = t20;
    }
    {
        gm_b16_8 t32;
        uint8_t t34 = gm_l6;
        if (!t34) goto ie33;
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t37 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t38 = gm_l5;
        uint64_t t39 = (uintptr_t)t38;
        if ((uintptr_t)0 > t39 || t39 > (uintptr_t)t36) gm_trap_slice((uintptr_t)0, t39, t36, gm_srch6h8rdicu443, 148, 28);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t37 + (uintptr_t)0 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - (uintptr_t)0));
        gm_b16_8 t41 = t40;
        t32 = t41;
        goto ix33;
ie33:;
        gm_b16_8 t43 = gm_l2;
        gm_b16_8 t44 = t43;
        t32 = t44;
ix33:;
        gm_l7 = t32;
    }
    {
        void * t45 = gm_l0;
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
        gm_b16_8 t50 = gm_l7;
        gm_tl874t2atnlh3 t51 = gm_l3;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sq0dqiq83b924));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(8));
        gm_ttu09lij36f8c t53;
        gm_f_bind_args_dnih8ih((void *)&t53, (void *)NULL, cx, NULL, t45, t48, t49, t50, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t54 = t53;
        gm_t6j6smj7qoctu t55;
        if (((((uint8_t *)&t54))[32] != 0)) {
            gm_t6j6smj7qoctu t56 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t54) + 0));
            t55 = t56;
        } else {
            void * t57 = gm_l0;
            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
            gm_tl874t2atnlh3 t59 = gm_l3;
            void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        gm_l8 = t55;
    }
    {
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
        gm_l9 = t62;
    }
    {
        uint8_t t64 = gm_l6;
        if (!t64) goto ie63;
        {
            gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t66 = (void *)((uint8_t *)&F.l10);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_tftb1odc739t5 t70 = gm_f_extend_gig5o6g(t66, t69);
            gm_tftb1odc739t5 t71 = t70;
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srch6h8rdicu443, 153, 9);
            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
        }
        {
            {
                uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t75 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t76 = gm_l5;
                uint64_t t77 = (uintptr_t)t76;
                if (t77 > (uintptr_t)t74 || (uintptr_t)t74 > (uintptr_t)t74) gm_trap_slice(t77, (uintptr_t)t74, t74, gm_srch6h8rdicu443, 154, 18);
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t75 + t77 * 144)));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)((uintptr_t)t74 - t77));
                gm_l11 = t78;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t80 = gm_l12;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t82 = (t80 < t81);
                    if (!t82) goto lb79;
                    {
                        uint64_t t85 = gm_l12;
                        uint64_t t86 = (uintptr_t)t85;
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t7te2tdpebbt4 t90 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t86 * 144));
                        F.l13 = t90;
                        {
                            void * t92 = (void *)((uint8_t *)&F.l10);
                            void * t93 = gm_l0;
                            void * t94 = (void *)((uint8_t *)&F.l13);
                            void * t95 = gm_f_variadic_arg_dfdnlo3((void *)NULL, cx, NULL, t93, t94);
                            if (gm_unw.kind) {
                                goto gm_ll83;
                            }
                            gm_tftb1odc739t5 t96 = gm_f_push_d943vgp_2(t92, &(*(gm_t69m56meqefs8 *)((uint8_t *)t95)));
                            gm_tftb1odc739t5 t97 = t96;
                            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srch6h8rdicu443, 154, 30);
                            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                        }
                        goto me88;
mn89:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 154, 9);
me88:;
                    }
lc79:;
                    {
                        uint64_t t100 = gm_l12;
                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                        uint64_t t103 = r101;
                        gm_l12 = t103;
                    }
                }
gm_ll83:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
                }
                goto gm_lad;
lb79:;
            }
        }
        {
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_l9 = t104;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        void * t105 = gm_l0;
        void * t106 = (void *)(((uint8_t *)&F.l4) + 64);
        gm_tl874t2atnlh3 t107 = gm_l3;
        gm_b0_1 t108 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t105, t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
        gm_to76jli3bqddq t110;
        uint32_t t111 = gm_l1;
        gm_tsth9qu1dv0g3 t112;
        memset(&t112, 0, sizeof t112);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t112 + 8), t111);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t110 + 0, t112);
        gm_b16_8 t113 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t110 + 40, t113);
        gm_teti411eujvf4 t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t114 + 8), t110);
        gm_tl874t2atnlh3 t115 = gm_l3;
        void * t116 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t109, &t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t116;
    }
    {
        uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)&gm_l8) + 16) + 8);
        uint8_t t119 = ((t118 == ((uint64_t)0ULL)));
        if (!t119) goto ie117;
        {
            void * t121 = gm_l14;
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto ix117;
ie117:;
ix117:;
    }
    {
        void * t123 = gm_l14;
        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t123) + 0));
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
        void * t126 = gm_l14;
        gm_tl874t2atnlh3 t127 = gm_l3;
        void * t128 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t124, t125, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t129 = 0;
        F.ret = t128;
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
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_call_l10ebge F;
    void * gm_l0; /* cx */
    void * gm_l1; /* callee */
    gm_b16_8 gm_l2; /* args */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t08olq97rscn3 gm_l5; /* id */
    gm_t88rcucg1b7uk gm_l7; /* t */
    void * gm_l10; /* c */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    gm_t716e5hdn64ba gm_l13; /* named */
    gm_t08olq97rscn3 gm_l14; /* name */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* base */
    gm_t08olq97rscn3 gm_l17; /* name */
    uint64_t gm_l18; /* m */
    uint64_t gm_l19; /* d */
    gm_t88rcucg1b7uk gm_l20; /* t */
    gm_t08olq97rscn3 gm_l21; /* id */
    uint32_t gm_l23; /* f */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l25; /* n */
    uint64_t gm_l26; /* d */
    gm_t88rcucg1b7uk gm_l27; /* t */
    uint32_t gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* t */
    gm_b16_8 gm_l30; /* es */
    gm_t88rcucg1b7uk gm_l31; /* t */
    GM_STACK_CHECK(&F, 6298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l5 = t5;
        void * t6 = gm_l0;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        gm_tqvu78rpi6q1h t8;
        gm_f_lookup_local_6f38t7p((void *)&t8, t6, t7);
        gm_tqvu78rpi6q1h t9 = t8;
        uint8_t t10;
        if (((((uint8_t *)&t9))[208] != 0)) goto mn12;
        t10 = ((uint8_t)1);
        goto me11;
mn12:;
        t10 = ((uint8_t)0);
        goto me11;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 6, 38);
me11:;
        if (!t10) goto ie2;
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
        if (!t15) goto ie2;
        uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srch6h8rdicu443, 6, 111);
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 144) + 0) + 0));
        if (t18 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t19 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 144) + 0) + 8));
        F.l6 = t19;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_tkn42u7na3t81 t25 = gm_f_type_param_3av4r8i(t23, t24);
            gm_tkn42u7na3t81 t26 = t25;
            if (!((((uint8_t *)&t26))[4] != 0)) goto ie21;
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t26));
            gm_l7 = t27;
            {
                void * t30 = gm_l0;
                void * t31 = (void *)((uint8_t *)&F.l6);
                gm_tkv1vap90vht2 t32;
                gm_f_try_untyped_8j8up35((void *)&t32, (void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkv1vap90vht2 t33 = t32;
                if (!((((uint8_t *)&t33))[208] != 0)) goto ie29;
                gm_t5vbkdq81qfs7 t34 = GM_LD(gm_t5vbkdq81qfs7, ((uint8_t *)&t33));
                F.l8 = t34;
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                    if (t37 != ((uint8_t)0ULL)) goto ie36;
                    gm_t0ialelq3ql1k t38 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l8) + 8));
                    F.l9 = t38;
                    {
                        uint8_t t41;
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                        if (t44 != ((uint8_t)3ULL)) goto mn43;
                        t41 = ((uint8_t)1);
                        goto me42;
mn43:;
                        t41 = ((uint8_t)0);
                        goto me42;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 10, 20);
me42:;
                        if (!t41) goto ie40;
                        {
                            void * t47 = gm_l0;
                            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                            gm_tnpifv20irnph t50;
                            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
                            gm_tabb710d9mq2e t51;
                            gm_f_format_t3u5vlm((void *)&t51, (void *)NULL, cx, NULL, t50);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tabb710d9mq2e t52 = t51;
                            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srch6h8rdicu443, 11, 38);
                            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                            gm_b16_8 t54 = t53;
                            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                            gm_b0_1 t56 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t47, t48, t55);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t57 = gm_l0;
                            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                            gm_tl874t2atnlh3 t59 = gm_l4;
                            void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t58, t59);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t61 = 0;
                            F.ret = t60;
                            goto gm_fret;
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    {
                        void * t62 = gm_l0;
                        void * t63 = gm_l0;
                        void * t64 = (void *)((uint8_t *)&F.l9);
                        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                        void * t66 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t63, t64, t65);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t88rcucg1b7uk t67 = gm_l7;
                        void * t68 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t62, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t68;
                    }
                    {
                        void * t69 = gm_l10;
                        gm_tl874t2atnlh3 t70 = gm_l4;
                        GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72), t70);
                    }
                    {
                        void * t71 = gm_l10;
                        uint8_t t72 = 0;
                        F.ret = t71;
                        goto gm_fret;
                    }
                    goto ix36;
ie36:;
                    {
                        void * t74 = gm_l0;
                        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
                        gm_tl874t2atnlh3 t76 = gm_l4;
                        void * t77 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t78 = 0;
                        F.ret = t77;
                        goto gm_fret;
                    }
ix36:;
                }
                goto ix29;
ie29:;
ix29:;
            }
            goto ix21;
ie21:;
ix21:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t80 = gm_l1;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t80) + 8) + 0));
        if (t81 != ((uint8_t)22ULL)) goto ie79;
        void * t82 = GM_LD(void *, (((((uint8_t *)t80) + 8) + 8) + 0));
        gm_l11 = t82;
        gm_t08olq97rscn3 t83 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t80) + 8) + 8) + 8));
        gm_l12 = t83;
        {
            void * t86 = gm_l0;
            void * t87 = gm_l11;
            gm_tlbrdhah49ci5 t88;
            gm_f_effect_named_b8afdbp((void *)&t88, (void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tlbrdhah49ci5 t89 = t88;
            if (!((((uint8_t *)&t89))[32] != 0)) goto ie85;
            gm_t716e5hdn64ba t90 = GM_LD(gm_t716e5hdn64ba, ((uint8_t *)&t89));
            gm_l13 = t90;
            {
                void * t92 = gm_l0;
                gm_b16_8 t93 = gm_l2;
                gm_tl874t2atnlh3 t94 = gm_l4;
                void * t95 = gm_f_perform_named_h7gr9vi((void *)NULL, cx, NULL, t92, &(*(gm_t716e5hdn64ba *)((uint8_t *)&gm_l13)), &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t96 = 0;
                F.ret = t95;
                goto gm_fret;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            void * t98 = gm_l0;
            void * t99 = gm_l11;
            gm_tf5srh4mp4b3i t100 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t101 = t100;
            uint8_t t102;
            if (((((uint8_t *)&t101))[8] != 0)) goto mn104;
            t102 = ((uint8_t)1);
            goto me103;
mn104:;
            t102 = ((uint8_t)0);
            goto me103;
mn105:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 23, 12);
me103:;
            if (!t102) goto ie97;
            {
                void * t107 = gm_l0;
                void * t108 = gm_l11;
                gm_b16_8 t109 = gm_l2;
                gm_tl874t2atnlh3 t110 = gm_l4;
                void * t111 = gm_f_method_call_hebo20o((void *)NULL, cx, NULL, t107, t108, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t109, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t112 = 0;
                F.ret = t111;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        void * t113 = gm_l1;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        switch (t122) { case ((uint8_t)0ULL): goto ma120; case ((uint8_t)1ULL): goto ma120; case ((uint8_t)2ULL): goto ma120; case ((uint8_t)3ULL): goto ma120; case ((uint8_t)4ULL): goto ma120; case ((uint8_t)5ULL): goto ma120; case ((uint8_t)6ULL): goto ma117; case ((uint8_t)7ULL): goto ma120; case ((uint8_t)8ULL): goto ma115; case ((uint8_t)9ULL): goto ma120; case ((uint8_t)10ULL): goto ma119; case ((uint8_t)11ULL): goto ma120; case ((uint8_t)12ULL): goto ma120; case ((uint8_t)13ULL): goto ma120; case ((uint8_t)14ULL): goto ma120; case ((uint8_t)15ULL): goto ma120; case ((uint8_t)16ULL): goto ma120; case ((uint8_t)17ULL): goto ma120; case ((uint8_t)18ULL): goto ma120; case ((uint8_t)19ULL): goto ma118; case ((uint8_t)20ULL): goto ma120; case ((uint8_t)21ULL): goto ma120; case ((uint8_t)22ULL): goto ma116; case ((uint8_t)23ULL): goto ma120; case ((uint8_t)24ULL): goto ma120; case ((uint8_t)25ULL): goto ma120; case ((uint8_t)26ULL): goto ma120; case ((uint8_t)27ULL): goto ma120; case ((uint8_t)28ULL): goto ma120; case ((uint8_t)29ULL): goto ma120; case ((uint8_t)30ULL): goto ma120; case ((uint8_t)31ULL): goto ma120; case ((uint8_t)32ULL): goto ma120; case ((uint8_t)33ULL): goto ma120; case ((uint8_t)34ULL): goto ma120; case ((uint8_t)35ULL): goto ma120; case ((uint8_t)36ULL): goto ma120; case ((uint8_t)37ULL): goto ma120; case ((uint8_t)38ULL): goto ma120; case ((uint8_t)39ULL): goto ma120; case ((uint8_t)40ULL): goto ma120; case ((uint8_t)41ULL): goto ma120; case ((uint8_t)42ULL): goto ma120; case ((uint8_t)43ULL): goto ma120; case ((uint8_t)44ULL): goto ma120; case ((uint8_t)45ULL): goto ma120; case ((uint8_t)46ULL): goto ma120; case ((uint8_t)47ULL): goto ma120; case ((uint8_t)48ULL): goto ma120; case ((uint8_t)49ULL): goto ma120; case ((uint8_t)50ULL): goto ma120; default: goto ma120; }
ma115:;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        if (t124 != ((uint8_t)8ULL)) goto mn123;
        gm_t08olq97rscn3 t125 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t113) + 8) + 8));
        gm_l14 = t125;
        {
            gm_tkn42u7na3t81 t127 = gm_l3;
            gm_tkn42u7na3t81 t128 = t127;
            gm_t88rcucg1b7uk t129;
            if (((((uint8_t *)&t128))[4] != 0)) {
                gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t128) + 0));
                t129 = t130;
            } else {
                {
                    void * t132 = gm_l0;
                    void * t133 = gm_l1;
                    gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t133) + 72));
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                    gm_tnpifv20irnph t136;
                    GM_ST(gm_b16_8, ((uint8_t *)&t136 + 0), t135);
                    gm_tabb710d9mq2e t137;
                    gm_f_format_bvitrsu((void *)&t137, (void *)NULL, cx, NULL, t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t138 = t137;
                    if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 8, gm_srch6h8rdicu443, 28, 39);
                    gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&t138) + 8));
                    gm_b16_8 t140 = t139;
                    gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                    gm_b0_1 t142 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t132, t134, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t143 = gm_l0;
                void * t144 = GM_LD(void *, (((uint8_t *)t143) + 0));
                gm_tl874t2atnlh3 t145 = gm_l4;
                void * t146 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t144, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t147 = 0;
                F.ret = t146;
                goto gm_fret;
            }
            gm_l15 = t129;
        }
        {
            void * t148 = gm_l0;
            void * t149 = gm_l0;
            void * t150 = gm_l0;
            void * t151 = (void *)(((uint8_t *)t150) + 96);
            void * t152 = gm_l0;
            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
            gm_t88rcucg1b7uk t154 = gm_l15;
            gm_t88rcucg1b7uk t155 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t151, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t156 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t149, GM_CONV(gm_csguankko4lmhd, t155)));
            gm_b16_8 t157 = gm_l2;
            gm_th2lqbo77pvsc t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t158), t157);
            ((uint8_t *)&t158)[16] = 1;
            gm_tl874t2atnlh3 t159 = gm_l4;
            void * t160 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t148, t156, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l14)), &t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
        goto me114;
mn123:;
ma116:;
        uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        if (t163 != ((uint8_t)22ULL)) goto mn162;
        void * t164 = GM_LD(void *, (((((uint8_t *)t113) + 8) + 8) + 0));
        gm_l16 = t164;
        gm_t08olq97rscn3 t165 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t113) + 8) + 8) + 8));
        gm_l17 = t165;
        {
            void * t168 = gm_l0;
            void * t169 = gm_l16;
            gm_tf5srh4mp4b3i t170 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t171 = t170;
            if (!((((uint8_t *)&t171))[8] != 0)) goto ie167;
            uint64_t t172 = GM_LD(uint64_t, ((uint8_t *)&t171));
            gm_l18 = t172;
            {
                void * t174 = gm_l0;
                void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
                uint64_t t176 = gm_l18;
                void * t177 = gm_l0;
                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)t177) + 8));
                gm_tf5srh4mp4b3i t179 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t175, t176, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t180 = t179;
                uint64_t t181;
                if (((((uint8_t *)&t180))[8] != 0)) {
                    uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&t180) + 0));
                    t181 = t182;
                } else {
                    void * t183 = gm_l0;
                    void * t184 = GM_LD(void *, (((uint8_t *)t183) + 0));
                    gm_tl874t2atnlh3 t185 = gm_l4;
                    void * t186 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t184, t185);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t187 = 0;
                    F.ret = t186;
                    goto gm_fret;
                }
                gm_l19 = t181;
            }
            {
                void * t188 = gm_l0;
                uint64_t t189 = gm_l19;
                gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                void * t191 = gm_l1;
                gm_b16_8 t192 = gm_l2;
                gm_tl874t2atnlh3 t193 = gm_l4;
                void * t194 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t188, t189, t190, t191, t192, t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t195 = 0;
                F.ret = t194;
                goto gm_fret;
            }
            goto ix167;
ie167:;
ix167:;
        }
        {
            void * t197 = gm_l0;
            void * t198 = gm_l16;
            gm_tkn42u7na3t81 t199 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t197, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t200 = t199;
            if (!((((uint8_t *)&t200))[4] != 0)) goto ie196;
            gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t200));
            gm_l20 = t201;
            {
                void * t203 = gm_l0;
                gm_t88rcucg1b7uk t204 = gm_l20;
                gm_b16_8 t205 = gm_l2;
                gm_th2lqbo77pvsc t206;
                GM_ST(uint8_t, (uint8_t *)&t206 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t206 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t206 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t206), t205);
                ((uint8_t *)&t206)[16] = 1;
                gm_tl874t2atnlh3 t207 = gm_l4;
                void * t208 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t203, t204, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), &t206, t207);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t209 = 0;
                F.ret = t208;
                goto gm_fret;
            }
            goto ix196;
ie196:;
ix196:;
        }
        {
            void * t210 = gm_l0;
            gm_tl874t2atnlh3 t211 = gm_l4;
            gm_b16_8 t212;
            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sgnvdmoebn4i7));
            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(15));
            void * t213 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t210, t211, t212);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t214 = 0;
            F.ret = t213;
            goto gm_fret;
        }
        goto me114;
mn162:;
ma117:;
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        if (t216 != ((uint8_t)6ULL)) goto mn215;
        gm_t08olq97rscn3 t217 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t113) + 8) + 8));
        gm_l21 = t217;
        {
            void * t220 = gm_l0;
            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tqvu78rpi6q1h t222;
            gm_f_lookup_local_6f38t7p((void *)&t222, t220, t221);
            gm_tqvu78rpi6q1h t223 = t222;
            if (!((((uint8_t *)&t223))[208] != 0)) goto ie219;
            gm_t930q5l38hdap t224 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&t223));
            F.l22 = t224;
            {
                uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                if (t227 != ((uint8_t)3ULL)) goto ie226;
                uint32_t t228 = GM_LD(uint32_t, (((uint8_t *)&F.l22) + 8));
                gm_l23 = t228;
                {
                    uint32_t t231 = gm_l23;
                    void * t232 = gm_l0;
                    gm_b16_8 t233 = GM_LD(gm_b16_8, ((((uint8_t *)t232) + 760) + 0));
                    gm_b16_8 t234 = t233;
                    uint8_t t235 = 0;
                    uint32_t t236 = t231;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t234) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t234))) + i * 4, ((uint8_t *)&t236))) { t235 = 1; break; }
                    uint8_t t237 = (uint8_t)(t235);
                    if (!t237) goto ie230;
                    {
                        void * t239 = gm_l0;
                        gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                        gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                        gm_b0_1 t242 = gm_f_recursive_without_ret_48l31rm((void *)NULL, cx, NULL, t239, t240, t241);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t243 = gm_l0;
                        void * t244 = GM_LD(void *, (((uint8_t *)t243) + 0));
                        gm_tl874t2atnlh3 t245 = gm_l4;
                        void * t246 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t244, t245);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t247 = 0;
                        F.ret = t246;
                        goto gm_fret;
                    }
                    goto ix230;
ie230:;
ix230:;
                }
                {
                    void * t248 = gm_l0;
                    uint32_t t249 = gm_l23;
                    gm_b16_8 t250 = gm_l2;
                    gm_tl874t2atnlh3 t251 = gm_l4;
                    void * t252 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t248, t249, t250, t251);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t253 = 0;
                    F.ret = t252;
                    goto gm_fret;
                }
                goto ix226;
ie226:;
ix226:;
            }
            {
                uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                if (t255 != ((uint8_t)5ULL)) goto ie254;
                gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 8));
                gm_l24 = t256;
                {
                    void * t259 = gm_l0;
                    void * t260 = GM_LD(void *, (((uint8_t *)t259) + 0));
                    void * t261 = (void *)(((uint8_t *)t260) + 8);
                    gm_t88rcucg1b7uk t262 = gm_l24;
                    uint8_t t263 = gm_f_is_error_8k27gd1(t261, GM_CONV(gm_csguankko4lmhd, t262));
                    if (!t263) goto ie258;
                    {
                        void * t265 = gm_l0;
                        void * t266 = GM_LD(void *, (((uint8_t *)t265) + 0));
                        gm_tl874t2atnlh3 t267 = gm_l4;
                        void * t268 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t266, t267);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t269 = 0;
                        F.ret = t268;
                        goto gm_fret;
                    }
                    goto ix258;
ie258:;
ix258:;
                }
                {
                    void * t270 = gm_l0;
                    gm_t88rcucg1b7uk t271 = gm_l24;
                    gm_b16_8 t272 = gm_l2;
                    gm_tl874t2atnlh3 t273 = gm_l4;
                    void * t274 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t270, t271, t272, t273);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t275 = 0;
                    F.ret = t274;
                    goto gm_fret;
                }
                goto ix254;
ie254:;
ix254:;
            }
            {
                uint8_t t277 = ((GM_LD(uint8_t, ((uint8_t *)&F.l22) + 0) == ((uint8_t)2ULL)));
                if (!t277) goto ie276;
                {
                    void * t279 = gm_l0;
                    void * t280 = gm_l1;
                    gm_tl874t2atnlh3 t281 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t280) + 72));
                    gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                    gm_tnpifv20irnph t283;
                    GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t282);
                    gm_tabb710d9mq2e t284;
                    gm_f_format_q8r21un((void *)&t284, (void *)NULL, cx, NULL, t283);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t285 = t284;
                    if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 8, gm_srch6h8rdicu443, 57, 43);
                    gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&t285) + 8));
                    gm_b16_8 t287 = t286;
                    gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                    gm_b0_1 t289 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t279, t281, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t290 = gm_l0;
                    void * t291 = GM_LD(void *, (((uint8_t *)t290) + 0));
                    gm_tl874t2atnlh3 t292 = gm_l4;
                    void * t293 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t291, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t294 = 0;
                    F.ret = t293;
                    goto gm_fret;
                }
                goto ix276;
ie276:;
ix276:;
            }
            {
                void * t295 = gm_l0;
                void * t296 = gm_l0;
                void * t297 = gm_l1;
                gm_tkn42u7na3t81 t298;
                memset(&t298, 0, sizeof t298);
                void * t299 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t296, t297, t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t300 = gm_l2;
                gm_tl874t2atnlh3 t301 = gm_l4;
                void * t302 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t295, t299, t300, t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t303 = 0;
                F.ret = t302;
                goto gm_fret;
            }
            goto ix219;
ie219:;
ix219:;
        }
        {
            void * t305 = gm_l0;
            void * t306 = GM_LD(void *, (((uint8_t *)t305) + 0));
            gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tkn42u7na3t81 t308 = gm_f_type_param_3av4r8i(t306, t307);
            gm_tkn42u7na3t81 t309 = t308;
            uint8_t t310;
            if (((((uint8_t *)&t309))[4] != 0)) goto mn312;
            t310 = ((uint8_t)0);
            goto me311;
mn312:;
            t310 = ((uint8_t)1);
            goto me311;
mn313:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 62, 16);
me311:;
            if (!t310) goto ie304;
            {
                gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                gm_l25 = t315;
            }
            {
                void * t316 = gm_l0;
                void * t317 = gm_l1;
                gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t317) + 72));
                gm_t0uuhaljsf410 t319;
                gm_b16_8 t320 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t319 + 0, t320);
                gm_b16_8 t321 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t319 + 16, t321);
                gm_b16_8 t322 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t319 + 32, t322);
                gm_b16_8 t323 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t319 + 48, t323);
                gm_tabb710d9mq2e t324;
                gm_f_format_9ijbt7j((void *)&t324, (void *)NULL, cx, NULL, &t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t325 = t324;
                if (((((uint8_t *)&t325))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t325) + 8, gm_srch6h8rdicu443, 64, 39);
                gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&t325) + 8));
                gm_b16_8 t327 = t326;
                gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
                gm_b0_1 t329 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t316, t318, t328);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t330 = gm_l0;
                void * t331 = GM_LD(void *, (((uint8_t *)t330) + 0));
                gm_tl874t2atnlh3 t332 = gm_l4;
                void * t333 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t331, t332);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t334 = 0;
                F.ret = t333;
                goto gm_fret;
            }
            goto ix304;
ie304:;
ix304:;
        }
        {
            void * t336 = gm_l0;
            void * t337 = GM_LD(void *, (((uint8_t *)t336) + 0));
            void * t338 = gm_l0;
            uint64_t t339 = GM_LD(uint64_t, (((uint8_t *)t338) + 8));
            gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tf5srh4mp4b3i t341 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t337, t339, t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t342 = t341;
            if (!((((uint8_t *)&t342))[8] != 0)) goto ie335;
            uint64_t t343 = GM_LD(uint64_t, ((uint8_t *)&t342));
            gm_l26 = t343;
            {
                void * t345 = gm_l0;
                void * t346 = GM_LD(void *, (((uint8_t *)t345) + 0));
                uint64_t t347 = gm_l26;
                gm_tl874t2atnlh3 t348 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                gm_b0_1 t349 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t346, t347, t348);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t350 = gm_l0;
                uint64_t t351 = gm_l26;
                gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                void * t353 = gm_l1;
                gm_b16_8 t354 = gm_l2;
                gm_tl874t2atnlh3 t355 = gm_l4;
                void * t356 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t350, t351, t352, t353, t354, t355);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t357 = 0;
                F.ret = t356;
                goto gm_fret;
            }
            goto ix335;
ie335:;
ix335:;
        }
        {
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s2r7keqsravo0, 5) == 0)) goto mn359;
            void * t360 = gm_l0;
            gm_b16_8 t361 = gm_l2;
            gm_tl874t2atnlh3 t362 = gm_l4;
            void * t363 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t360, ((uint8_t)1), t361, t362);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t364 = 0;
            F.ret = t363;
            goto gm_fret;
            goto me358;
mn359:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_so4gnucc3fl4v, 6) == 0)) goto mn365;
            void * t366 = gm_l0;
            gm_b16_8 t367 = gm_l2;
            gm_tl874t2atnlh3 t368 = gm_l4;
            void * t369 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t366, ((uint8_t)0), t367, t368);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t370 = 0;
            F.ret = t369;
            goto gm_fret;
            goto me358;
mn365:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_sinmpfqtbo9in, 4) == 0)) goto mn371;
            void * t372 = gm_l0;
            gm_b16_8 t373 = gm_l2;
            gm_tl874t2atnlh3 t374 = gm_l4;
            void * t375 = gm_f_trap_t21p26s((void *)NULL, cx, NULL, t372, t373, t374);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t376 = 0;
            F.ret = t375;
            goto gm_fret;
            goto me358;
mn371:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s9ur7sj9r0g63, 6) == 0)) goto mn377;
            void * t378 = gm_l0;
            gm_b16_8 t379 = gm_l2;
            gm_tl874t2atnlh3 t380 = gm_l4;
            void * t381 = gm_f_assert__dllohqd((void *)NULL, cx, NULL, t378, t379, t380);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t382 = 0;
            F.ret = t381;
            goto gm_fret;
            goto me358;
mn377:;
            if (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 9) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s3231a310mran, 9) == 0)) goto mn383;
            void * t384 = gm_l0;
            gm_b16_8 t385 = gm_l2;
            gm_tl874t2atnlh3 t386 = gm_l4;
            void * t387 = gm_f_expect_eq_iu4ojmi((void *)NULL, cx, NULL, t384, t385, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t388 = 0;
            F.ret = t387;
            goto gm_fret;
            goto me358;
mn383:;
            goto me358;
mn389:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 71, 13);
me358:;
        }
        {
            void * t392 = gm_l0;
            void * t393 = GM_LD(void *, (((uint8_t *)t392) + 0));
            gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
            gm_tkn42u7na3t81 t395 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t393, t394);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t396 = t395;
            if (!((((uint8_t *)&t396))[4] != 0)) goto ie391;
            gm_t88rcucg1b7uk t397 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t396));
            gm_l27 = t397;
            {
                void * t399 = gm_l0;
                gm_t88rcucg1b7uk t400 = gm_l27;
                gm_b16_8 t401 = gm_l2;
                gm_tl874t2atnlh3 t402 = gm_l4;
                void * t403 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t399, t400, t401, t402);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t404 = 0;
                F.ret = t403;
                goto gm_fret;
            }
            goto ix391;
ie391:;
ix391:;
        }
        {
            uint8_t t406 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l21) + 0) + 8) == 5) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l21) + 0)), gm_s4g2k7ups3ui6, 5) == 0));
            if (!t406) goto ie405;
            void * t407 = gm_l0;
            void * t408 = GM_LD(void *, (((uint8_t *)t407) + 0));
            if (!(((((uint8_t *)t408) + 1956))[4] != 0)) goto ie405;
            uint32_t t409 = GM_LD(uint32_t, (((uint8_t *)t408) + 1956));
            gm_l28 = t409;
            {
                void * t411 = gm_l0;
                uint32_t t412 = gm_l28;
                gm_b16_8 t413 = gm_l2;
                gm_tl874t2atnlh3 t414 = gm_l4;
                void * t415 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t411, t412, ((uint32_t)0ULL), t413, t414);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t416 = 0;
                F.ret = t415;
                goto gm_fret;
            }
            goto ix405;
ie405:;
ix405:;
        }
        {
            void * t417 = gm_l0;
            void * t418 = gm_l0;
            void * t419 = gm_l1;
            gm_tkn42u7na3t81 t420;
            memset(&t420, 0, sizeof t420);
            void * t421 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t418, t419, t420);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t422 = gm_l2;
            gm_tl874t2atnlh3 t423 = gm_l4;
            void * t424 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t417, t421, t422, t423);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t425 = 0;
            F.ret = t424;
            goto gm_fret;
        }
        goto me114;
mn215:;
ma118:;
        uint8_t t427 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        if (t427 != ((uint8_t)19ULL)) goto mn426;
        {
            void * t430 = gm_l0;
            void * t431 = gm_l1;
            gm_tkn42u7na3t81 t432 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t430, t431);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t433 = t432;
            if (!((((uint8_t *)&t433))[4] != 0)) goto ie429;
            gm_t88rcucg1b7uk t434 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t433));
            gm_l29 = t434;
            {
                void * t436 = gm_l0;
                gm_t88rcucg1b7uk t437 = gm_l29;
                gm_b16_8 t438 = gm_l2;
                gm_tl874t2atnlh3 t439 = gm_l4;
                void * t440 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t436, t437, t438, t439);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t441 = 0;
                F.ret = t440;
                goto gm_fret;
            }
            goto ix429;
ie429:;
ix429:;
        }
        {
            void * t443 = gm_l0;
            void * t444 = GM_LD(void *, (((uint8_t *)t443) + 0));
            void * t445 = gm_l0;
            uint64_t t446 = GM_LD(uint64_t, (((uint8_t *)t445) + 8));
            void * t447 = gm_l1;
            uint8_t t448 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t444, t446, t447);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t448) goto ie442;
            void * t449 = gm_l0;
            void * t450 = GM_LD(void *, (((uint8_t *)t449) + 0));
            void * t451 = (void *)(((uint8_t *)t450) + 8);
            void * t452 = gm_l0;
            void * t453 = gm_l1;
            gm_t88rcucg1b7uk t454 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t452, t453);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t455 = gm_f_is_error_8k27gd1(t451, GM_CONV(gm_csguankko4lmhd, t454));
            if (!t455) goto ie442;
            {
                void * t457 = gm_l0;
                void * t458 = GM_LD(void *, (((uint8_t *)t457) + 0));
                gm_tl874t2atnlh3 t459 = gm_l4;
                void * t460 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t458, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t461 = 0;
                F.ret = t460;
                goto gm_fret;
            }
            goto ix442;
ie442:;
ix442:;
        }
        {
            void * t462 = gm_l0;
            void * t463 = gm_l0;
            void * t464 = gm_l1;
            gm_tkn42u7na3t81 t465;
            memset(&t465, 0, sizeof t465);
            void * t466 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t463, t464, t465);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t467 = gm_l2;
            gm_tl874t2atnlh3 t468 = gm_l4;
            void * t469 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t462, t466, t467, t468);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t470 = 0;
            F.ret = t469;
            goto gm_fret;
        }
        goto me114;
mn426:;
ma119:;
        uint8_t t472 = GM_LD(uint8_t, ((((uint8_t *)t113) + 8) + 0));
        if (t472 != ((uint8_t)10ULL)) goto mn471;
        gm_b16_8 t473 = GM_LD(gm_b16_8, ((((uint8_t *)t113) + 8) + 8));
        gm_l30 = t473;
        {
            uint64_t t476 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            uint8_t t477 = (t476 > ((uint64_t)0ULL));
            if (!t477) goto ie475;
            void * t478 = gm_l0;
            void * t479 = GM_LD(void *, (((uint8_t *)t478) + 0));
            void * t480 = gm_l0;
            uint64_t t481 = GM_LD(uint64_t, (((uint8_t *)t480) + 8));
            void * t482 = gm_l1;
            uint8_t t483 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t479, t481, t482);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t483) goto ie475;
            {
                void * t485 = gm_l0;
                void * t486 = gm_l1;
                gm_t88rcucg1b7uk t487 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t485, t486);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l31 = t487;
            }
            {
                void * t489 = gm_l0;
                void * t490 = GM_LD(void *, (((uint8_t *)t489) + 0));
                void * t491 = (void *)(((uint8_t *)t490) + 8);
                gm_t88rcucg1b7uk t492 = gm_l31;
                uint8_t t493 = gm_f_is_error_8k27gd1(t491, GM_CONV(gm_csguankko4lmhd, t492));
                if (!t493) goto ie488;
                {
                    void * t495 = gm_l0;
                    void * t496 = GM_LD(void *, (((uint8_t *)t495) + 0));
                    gm_tl874t2atnlh3 t497 = gm_l4;
                    void * t498 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t496, t497);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t499 = 0;
                    F.ret = t498;
                    goto gm_fret;
                }
                goto ix488;
ie488:;
ix488:;
            }
            {
                void * t500 = gm_l0;
                gm_t88rcucg1b7uk t501 = gm_l31;
                gm_b16_8 t502 = gm_l2;
                gm_tl874t2atnlh3 t503 = gm_l4;
                void * t504 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t500, t501, t502, t503);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t505 = 0;
                F.ret = t504;
                goto gm_fret;
            }
            goto ix475;
ie475:;
ix475:;
        }
        {
            void * t506 = gm_l0;
            void * t507 = gm_l0;
            void * t508 = gm_l1;
            gm_tkn42u7na3t81 t509;
            memset(&t509, 0, sizeof t509);
            void * t510 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t507, t508, t509);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t511 = gm_l2;
            gm_tl874t2atnlh3 t512 = gm_l4;
            void * t513 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t506, t510, t511, t512);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t514 = 0;
            F.ret = t513;
            goto gm_fret;
        }
        goto me114;
mn471:;
ma120:;
        void * t516 = gm_l0;
        void * t517 = gm_l0;
        void * t518 = gm_l1;
        gm_tkn42u7na3t81 t519;
        memset(&t519, 0, sizeof t519);
        void * t520 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t517, t518, t519);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t521 = gm_l2;
        gm_tl874t2atnlh3 t522 = gm_l4;
        void * t523 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t516, t520, t521, t522);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t524 = 0;
        F.ret = t523;
        goto gm_fret;
        goto me114;
mn515:;
ma121:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 25, 5);
me114:;
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
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_value_t2nv21i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* p */
    gm_t6j6smj7qoctu gm_l12; /* bound */
    void * gm_l13; /* callee */
    void * gm_l15; /* call */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_trcmmd12cefb1 gm_l18; /* st */
    GM_STACK_CHECK(&F, 1772u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 96);
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        gm_t88rcucg1b7uk t8 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t3, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11 = (void *)(((uint8_t *)t10) + 8);
        gm_t88rcucg1b7uk t12 = gm_l4;
        gm_t53at80pfdus3 t13;
        gm_f_kind_jcnt4hm((void *)&t13, t11, GM_CONV(gm_csguankko4lmhd, t12));
        gm_t53at80pfdus3 t14 = t13;
        gm_tddtjfggdat0d t15;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t20 != ((uint8_t)16ULL)) goto pa19;
        gm_tddtjfggdat0d t21 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t14) + 8));
        F.l5 = t21;
        goto po18;
pa19:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t23 != ((uint8_t)15ULL)) goto pa22;
        gm_tddtjfggdat0d t24 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t14) + 8));
        F.l5 = t24;
        goto po18;
pa22:;
        goto mn17;
po18:;
        gm_tddtjfggdat0d t25 = F.l5;
        t15 = t25;
        goto me16;
mn17:;
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_t88rcucg1b7uk t32 = gm_l4;
            uint8_t t33 = gm_f_is_error_8k27gd1(t31, GM_CONV(gm_csguankko4lmhd, t32));
            uint8_t t34 = (uint8_t)!t33;
            if (!t34) goto ie28;
            {
                void * t36 = gm_l0;
                void * t37 = gm_l1;
                gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
                void * t39 = gm_l0;
                gm_t88rcucg1b7uk t40 = gm_l4;
                gm_b16_8 t41 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t42;
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                gm_tabb710d9mq2e t43;
                gm_f_format_jaikcua((void *)&t43, (void *)NULL, cx, NULL, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t44 = t43;
                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srch6h8rdicu443, 110, 60);
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                gm_b16_8 t46 = t45;
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                gm_b0_1 t48 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t36, t38, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix28;
ie28:;
ix28:;
        }
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        gm_tl874t2atnlh3 t51 = gm_l3;
        void * t52 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t53 = 0;
        F.ret = t52;
        goto gm_fret;
        goto me16;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 107, 12);
me16:;
        F.l7 = t15;
    }
    {
        gm_f_list_af74s0q((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_l9 = t55;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t57 = gm_l10;
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t59 = (t57 < t58);
                if (!t59) goto lb56;
                {
                    uint64_t t62 = gm_l10;
                    uint64_t t63 = (uintptr_t)t62;
                    uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t63 * 4));
                    gm_l11 = t67;
                    {
                        void * t69 = (void *)((uint8_t *)&F.l8);
                        gm_t5v0c8ia6a5mo t70;
                        memset((uint8_t *)&t70 + 49, 0, 7);
                        gm_b16_8 t71;
                        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sfdbfrv46lihd));
                        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(1));
                        GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
                        gm_t88rcucg1b7uk t72 = gm_l11;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t70 + 16, t72);
                        gm_tl874t2atnlh3 t73 = gm_l3;
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t70 + 20, t73);
                        GM_ST(void *, (uint8_t *)&t70 + 32, ((void *)0));
                        gm_t9805qtonbsag t74;
                        memset(&t74, 0, sizeof t74);
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t70 + 40, t74);
                        GM_ST(uint8_t, (uint8_t *)&t70 + 48, ((uint8_t)0));
                        gm_tftb1odc739t5 t75 = gm_f_push_o8u01sh(t69, &t70);
                        gm_tftb1odc739t5 t76 = t75;
                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srch6h8rdicu443, 115, 27);
                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 115, 5);
me65:;
                }
lc56:;
                {
                    uint64_t t79 = gm_l10;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l10 = t82;
                }
            }
lb56:;
        }
    }
    {
        void * t83 = gm_l0;
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t85 = t84;
        gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
        void * t87 = gm_l0;
        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 8));
        gm_b16_8 t89 = gm_l2;
        gm_tl874t2atnlh3 t90 = gm_l3;
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sq0dqiq83b924));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(8));
        gm_ttu09lij36f8c t92;
        gm_f_bind_args_dnih8ih((void *)&t92, (void *)NULL, cx, NULL, t83, t86, t88, t89, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t93 = t92;
        gm_t6j6smj7qoctu t94;
        if (((((uint8_t *)&t93))[32] != 0)) {
            gm_t6j6smj7qoctu t95 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t93) + 0));
            t94 = t95;
        } else {
            void * t96 = gm_l0;
            void * t97 = GM_LD(void *, (((uint8_t *)t96) + 0));
            gm_tl874t2atnlh3 t98 = gm_l3;
            void * t99 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
        }
        gm_l12 = t94;
    }
    {
        void * t101 = gm_l0;
        void * t102 = (void *)(((uint8_t *)&F.l7) + 24);
        gm_tl874t2atnlh3 t103 = gm_l3;
        gm_b0_1 t104 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t101, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t105 = gm_l1;
        gm_l13 = t105;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t107 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 16) + 8);
        uint8_t t108 = (t107 > ((uint64_t)0ULL));
        if (!t108) goto ie106;
        {
            void * t110 = gm_l0;
            void * t111 = gm_l1;
            void * t112 = (void *)((uint8_t *)&F.l14);
            void * t113 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t110, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t113;
        }
        goto ix106;
ie106:;
ix106:;
    }
    {
        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
        gm_to76jli3bqddq t115;
        void * t116 = gm_l13;
        gm_tsth9qu1dv0g3 t117;
        GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t117 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t117 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t117 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t117 + 8), t116);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t115 + 0, t117);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t115 + 40, t118);
        gm_teti411eujvf4 t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t119 + 8), t115);
        gm_tl874t2atnlh3 t120 = gm_l3;
        void * t121 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t114, &t119, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t121;
    }
    {
        uint64_t t123 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 16) + 8);
        uint8_t t124 = ((t123 == ((uint64_t)0ULL)));
        if (!t124) goto ie122;
        {
            void * t126 = gm_l15;
            uint8_t t127 = 0;
            F.ret = t126;
            goto gm_fret;
        }
        goto ix122;
ie122:;
ix122:;
    }
    {
        {
            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
            gm_l16 = t129;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t131 = gm_l17;
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t133 = (t131 < t132);
                if (!t133) goto lb130;
                {
                    uint64_t t136 = gm_l17;
                    uint64_t t137 = (uintptr_t)t136;
                    uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_trcmmd12cefb1 t141 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t137 * 32));
                    gm_l18 = t141;
                    {
                        void * t143 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t144 = gm_f_push_d6c76ud_2(t143, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l18)));
                        gm_tftb1odc739t5 t145 = t144;
                        if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srch6h8rdicu443, 123, 27);
                        gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
                    }
                    goto me139;
mn140:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 123, 5);
me139:;
                }
lc130:;
                {
                    uint64_t t148 = gm_l17;
                    uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                    uint64_t t151 = r149;
                    gm_l17 = t151;
                }
            }
lb130:;
        }
    }
    {
        void * t152 = gm_l15;
        gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t152) + 0));
        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t155 = gm_l15;
        gm_tl874t2atnlh3 t156 = gm_l3;
        void * t157 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t153, t154, t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t158 = 0;
        F.ret = t157;
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
__attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_optional_cffo1ol F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t5 = (!(t4 == ((uint64_t)1ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srch6h8rdicu443, 483, 29);
            uint8_t t8;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t6 * 144) + 0) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            t8 = ((uint8_t)0);
            goto me9;
mn10:;
            t8 = ((uint8_t)1);
            goto me9;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 483, 29);
me9:;
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l4;
            void * t16 = gm_l0;
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tabb710d9mq2e t20;
            gm_f_format_mq9dnpb((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t21 = t20;
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srch6h8rdicu443, 484, 24);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            gm_tl874t2atnlh3 t28 = gm_l4;
            void * t29 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srch6h8rdicu443, 487, 23);
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t32 * 144) + 0) + 0));
        if (t34 != ((uint8_t)0ULL)) goto ie31;
        gm_tc3oht4ksth9d t35 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t32 * 144) + 0) + 8));
        F.l5 = t35;
        {
            uint8_t t38 = ((GM_LD(uint8_t, (((uint8_t *)&F.l5) + 8) + 0) == ((uint8_t)4ULL)));
            if (!t38) goto ie37;
            {
                gm_t88rcucg1b7uk t40 = gm_l1;
                gm_teti411eujvf4 t41;
                memset(&t41, 0, sizeof t41);
                GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)7ULL));
                gm_tl874t2atnlh3 t42 = gm_l4;
                void * t43 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            void * t45 = gm_l0;
            void * t46 = gm_l0;
            void * t47 = (void *)((uint8_t *)&F.l5);
            gm_t88rcucg1b7uk t48 = gm_l2;
            gm_tkn42u7na3t81 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t49), t48);
            ((uint8_t *)&t49)[4] = 1;
            void * t50 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t46, t47, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t51 = gm_l2;
            void * t52 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t45, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t52;
        }
        {
            gm_t88rcucg1b7uk t53 = gm_l1;
            gm_tmn7n0n9raou5 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)2ULL));
            void * t55 = gm_l6;
            gm_t8jsc4n9vp2ib t56;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t56 + 0), t54);
            GM_ST(void *, ((uint8_t *)&t56 + 8), t55);
            gm_teti411eujvf4 t57;
            memset(&t57, 0, sizeof t57);
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t57 + 8), t56);
            gm_tl874t2atnlh3 t58 = gm_l4;
            void * t59 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t53, &t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix31;
ie31:;
        {
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
            gm_tl874t2atnlh3 t64 = gm_l4;
            void * t65 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
ix31:;
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
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_conversion_2qjq323 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    uint32_t gm_l5; /* id */
    uint8_t gm_l7; /* one_positional */
    gm_b16_8 gm_l8; /* fields */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l16; /* v */
    gm_b16_8 gm_l18; /* elems */
    gm_b16_8 gm_l19; /* names */
    uint64_t gm_l20; /* len */
    gm_t88rcucg1b7uk gm_l21; /* elem */
    gm_tnnjeerolos0a gm_l22; /* kind */
    gm_t88rcucg1b7uk gm_l23; /* index */
    gm_t88rcucg1b7uk gm_l24; /* inner */
    gm_t88rcucg1b7uk gm_l25; /* elem */
    gm_t88rcucg1b7uk gm_l27; /* u */
    void * gm_l28; /* v */
    void * gm_l29; /* v */
    gm_t88rcucg1b7uk gm_l30; /* vt */
    GM_STACK_CHECK(&F, 4675u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l4;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t53at80pfdus3 t9 = t8;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)17ULL)) goto ie5;
        uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)&t9) + 8) + 0));
        gm_l5 = t11;
        {
            uint32_t t13 = gm_l5;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1088) + 0) + 8);
            if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srch6h8rdicu443, 406, 15);
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            switch (t24) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma21; default: goto ma22; }
ma20:;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t26 != ((uint8_t)0ULL)) goto mn25;
            gm_tpautr4br7h3l t27 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8));
            F.l6 = t27;
            {
                uint8_t t29 = 0;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t31 = ((t30 == ((uint64_t)1ULL)));
                if (t31) {
                    uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srch6h8rdicu443, 409, 53);
                    uint8_t t34;
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t32 * 144) + 0) + 0));
                    if (t37 != ((uint8_t)0ULL)) goto mn36;
                    t34 = ((uint8_t)1);
                    goto me35;
mn36:;
                    t34 = ((uint8_t)0);
                    goto me35;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 409, 53);
me35:;
                    t29 = t34;
                } else t29 = 0;
                gm_l7 = t29;
            }
            {
                uint8_t t40 = 0;
                uint8_t t41 = gm_l7;
                if (t41) {
                    uint8_t t42;
                    if ((((((uint8_t *)&F.l6) + 40))[4] != 0)) goto mn44;
                    t42 = ((uint8_t)0);
                    goto me43;
mn44:;
                    t42 = ((uint8_t)1);
                    goto me43;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 410, 44);
me43:;
                    t40 = t42;
                } else t40 = 0;
                uint8_t t46 = (uint8_t)!t40;
                if (!t46) goto ie39;
                {
                    void * t48 = gm_l0;
                    gm_t88rcucg1b7uk t49 = gm_l1;
                    gm_b16_8 t50 = gm_l2;
                    gm_tl874t2atnlh3 t51 = gm_l3;
                    void * t52 = gm_f_construct_struct_ve6tb88((void *)NULL, cx, NULL, t48, t49, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t53 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix39;
ie39:;
ix39:;
            }
            goto me19;
mn25:;
ma21:;
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t55 != ((uint8_t)2ULL)) goto mn54;
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8));
            gm_l8 = t56;
            {
                uint8_t t59 = 0;
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t61 = (!(t60 == ((uint64_t)1ULL)));
                if (!t61) {
                    uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srch6h8rdicu443, 414, 41);
                    uint8_t t64;
                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t62 * 144) + 0) + 0));
                    if (t67 != ((uint8_t)1ULL)) goto mn66;
                    t64 = ((uint8_t)0);
                    goto me65;
mn66:;
                    t64 = ((uint8_t)1);
                    goto me65;
mn68:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 414, 41);
me65:;
                    t59 = t64;
                } else t59 = 1;
                if (!t59) goto ie58;
                {
                    void * t70 = gm_l0;
                    gm_tl874t2atnlh3 t71 = gm_l3;
                    void * t72 = gm_l0;
                    gm_t88rcucg1b7uk t73 = gm_l1;
                    gm_b16_8 t74 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t72, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t75;
                    GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
                    gm_tabb710d9mq2e t76;
                    gm_f_format_cqv4884((void *)&t76, (void *)NULL, cx, NULL, t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t77 = t76;
                    if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srch6h8rdicu443, 415, 36);
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                    gm_b16_8 t79 = t78;
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                    gm_b0_1 t81 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t70, t71, t80);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t82 = gm_l0;
                    void * t83 = GM_LD(void *, (((uint8_t *)t82) + 0));
                    gm_tl874t2atnlh3 t84 = gm_l3;
                    void * t85 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t83, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t86 = 0;
                    F.ret = t85;
                    goto gm_fret;
                }
                goto ix58;
ie58:;
ix58:;
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srch6h8rdicu443, 418, 33);
                uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t88 * 144) + 0) + 0));
                if (t90 != ((uint8_t)1ULL)) goto ie87;
                gm_t08olq97rscn3 t91 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t88 * 144) + 0) + 8) + 0));
                gm_l9 = t91;
                gm_tc3oht4ksth9d t92 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t88 * 144) + 0) + 8) + 32));
                F.l10 = t92;
                {
                    gm_tnpj2jtv1r8pa t94;
                    memset(&t94, 0, sizeof t94);
                    F.l11 = t94;
                }
                {
                    {
                        gm_b16_8 t96 = gm_l8;
                        gm_l12 = t96;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t98 = gm_l13;
                            uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            uint8_t t100 = (t98 < t99);
                            if (!t100) goto lb97;
                            {
                                uint64_t t103 = gm_l13;
                                uint64_t t104 = (uintptr_t)t103;
                                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                gm_t0mglksrkfa5e t108 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t104 * 40));
                                F.l14 = t108;
                                {
                                    uint8_t t111;
                                    if (((((uint8_t *)&F.l11))[40] != 0)) goto mn113;
                                    t111 = ((uint8_t)1);
                                    goto me112;
mn113:;
                                    t111 = ((uint8_t)0);
                                    goto me112;
mn114:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 421, 24);
me112:;
                                    if (!t111) goto ie110;
                                    gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                    gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                                    gm_b16_8 t117 = t115;
                                    gm_b16_8 t118 = t116;
                                    uint8_t t119 = (gm_eq0iqiubfm7e(((uint8_t *)&t117), ((uint8_t *)&t118)));
                                    if (!t119) goto ie110;
                                    {
                                        gm_t0mglksrkfa5e t121 = F.l14;
                                        gm_tnpj2jtv1r8pa t122;
                                        GM_ST(uint8_t, (uint8_t *)&t122 + 41, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t122 + 42, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t122 + 44, 0);
                                        GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t122), t121);
                                        ((uint8_t *)&t122)[40] = 1;
                                        F.l11 = t122;
                                    }
                                    goto ix110;
ie110:;
ix110:;
                                }
                                goto me106;
mn107:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 420, 17);
me106:;
                            }
lc97:;
                            {
                                uint64_t t124 = gm_l13;
                                uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                                uint64_t t127 = r125;
                                gm_l13 = t127;
                            }
                        }
lb97:;
                    }
                }
                {
                    gm_tnpj2jtv1r8pa t128 = F.l11;
                    gm_tnpj2jtv1r8pa t129 = t128;
                    gm_t0mglksrkfa5e t130;
                    if (((((uint8_t *)&t129))[40] != 0)) {
                        gm_t0mglksrkfa5e t131 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t129) + 0));
                        t130 = t131;
                    } else {
                        {
                            void * t133 = gm_l0;
                            gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                            gm_t6j6smj7qoctu t135;
                            void * t136 = gm_l0;
                            gm_t88rcucg1b7uk t137 = gm_l1;
                            gm_b16_8 t138 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t136, t137);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t138);
                            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t139);
                            gm_tabb710d9mq2e t140;
                            gm_f_format_lc07prd((void *)&t140, (void *)NULL, cx, NULL, &t135);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tabb710d9mq2e t141 = t140;
                            if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_srch6h8rdicu443, 424, 38);
                            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
                            gm_b16_8 t143 = t142;
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            gm_b0_1 t145 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t133, t134, t144);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t146 = gm_l0;
                        void * t147 = GM_LD(void *, (((uint8_t *)t146) + 0));
                        gm_tl874t2atnlh3 t148 = gm_l3;
                        void * t149 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t147, t148);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t150 = 0;
                        F.ret = t149;
                        goto gm_fret;
                    }
                    F.l15 = t130;
                }
                {
                    void * t151 = gm_l0;
                    void * t152 = gm_l0;
                    void * t153 = (void *)((uint8_t *)&F.l10);
                    gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                    gm_tkn42u7na3t81 t155;
                    GM_ST(uint8_t, (uint8_t *)&t155 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t155 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t155), t154);
                    ((uint8_t *)&t155)[4] = 1;
                    void * t156 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t152, t153, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                    void * t158 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t151, t156, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t158;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t159 = (void *)((uint8_t *)&F.l17);
                    void * t160 = gm_l16;
                    gm_tftb1odc739t5 t161 = gm_f_push_d943vgp_2(t159, &(*(gm_t69m56meqefs8 *)((uint8_t *)t160)));
                    gm_tftb1odc739t5 t162 = t161;
                    if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srch6h8rdicu443, 429, 17);
                    gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
                }
                {
                    gm_t88rcucg1b7uk t164 = gm_l1;
                    gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                    gm_teti411eujvf4 t166;
                    memset(&t166, 0, sizeof t166);
                    GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)35ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t165);
                    gm_tl874t2atnlh3 t167 = gm_l3;
                    void * t168 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t164, &t166, t167);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t169 = 0;
                    F.ret = t168;
                    goto gm_fret;
                }
                goto ix87;
ie87:;
                {
                    void * t171 = gm_l0;
                    void * t172 = GM_LD(void *, (((uint8_t *)t171) + 0));
                    gm_tl874t2atnlh3 t173 = gm_l3;
                    void * t174 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t172, t173);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t175 = 0;
                    F.ret = t174;
                    goto gm_fret;
                }
ix87:;
            }
            goto me19;
mn54:;
ma22:;
            goto me19;
mn176:;
ma23:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 406, 9);
me19:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t178 = gm_l4;
        gm_t88rcucg1b7uk t179 = gm_l1;
        gm_t53at80pfdus3 t180;
        gm_f_kind_jcnt4hm((void *)&t180, t178, GM_CONV(gm_csguankko4lmhd, t179));
        gm_t53at80pfdus3 t181 = t180;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
        switch (t189) { case ((uint8_t)0ULL): goto ma187; case ((uint8_t)1ULL): goto ma187; case ((uint8_t)2ULL): goto ma187; case ((uint8_t)3ULL): goto ma187; case ((uint8_t)4ULL): goto ma187; case ((uint8_t)5ULL): goto ma187; case ((uint8_t)6ULL): goto ma187; case ((uint8_t)7ULL): goto ma187; case ((uint8_t)8ULL): goto ma187; case ((uint8_t)9ULL): goto ma187; case ((uint8_t)10ULL): goto ma187; case ((uint8_t)11ULL): goto ma184; case ((uint8_t)12ULL): goto ma183; case ((uint8_t)13ULL): goto ma185; case ((uint8_t)14ULL): goto ma187; case ((uint8_t)15ULL): goto ma187; case ((uint8_t)16ULL): goto ma187; case ((uint8_t)17ULL): goto ma187; case ((uint8_t)18ULL): goto ma187; case ((uint8_t)19ULL): goto ma187; case ((uint8_t)20ULL): goto ma186; case ((uint8_t)21ULL): goto ma187; case ((uint8_t)22ULL): goto ma187; case ((uint8_t)23ULL): goto ma187; case ((uint8_t)24ULL): goto ma187; case ((uint8_t)25ULL): goto ma187; case ((uint8_t)26ULL): goto ma187; default: goto ma187; }
ma183:;
        uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
        if (t191 != ((uint8_t)12ULL)) goto mn190;
        gm_b16_8 t192 = GM_LD(gm_b16_8, ((((uint8_t *)&t181) + 8) + 0));
        gm_l18 = t192;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((((uint8_t *)&t181) + 8) + 16));
        gm_l19 = t193;
        void * t194 = gm_l0;
        gm_t88rcucg1b7uk t195 = gm_l1;
        gm_b16_8 t196 = gm_l18;
        gm_b16_8 t197 = gm_l19;
        gm_b16_8 t198 = gm_l2;
        gm_tl874t2atnlh3 t199 = gm_l3;
        void * t200 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t194, t195, t196, t197, t198, t199);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t201 = 0;
        F.ret = t200;
        goto gm_fret;
        goto me182;
mn190:;
ma184:;
        uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
        if (t203 != ((uint8_t)11ULL)) goto mn202;
        uint64_t t204 = GM_LD(uint64_t, ((((uint8_t *)&t181) + 8) + 0));
        gm_l20 = t204;
        gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t181) + 8) + 8));
        gm_l21 = t205;
        gm_tnnjeerolos0a t206 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t181) + 8) + 12));
        gm_l22 = t206;
        {
            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t209 != ((uint8_t)1ULL)) goto ie208;
            gm_t88rcucg1b7uk t210 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 4));
            gm_l23 = t210;
            {
                void * t212 = gm_l0;
                gm_t88rcucg1b7uk t213 = gm_l1;
                gm_t88rcucg1b7uk t214 = gm_l23;
                gm_t88rcucg1b7uk t215 = gm_l21;
                gm_b16_8 t216 = gm_l2;
                gm_tl874t2atnlh3 t217 = gm_l3;
                void * t218 = gm_f_construct_enum_array_d7cisuu((void *)NULL, cx, NULL, t212, t213, t214, t215, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t219 = 0;
                F.ret = t218;
                goto gm_fret;
            }
            goto ix208;
ie208:;
ix208:;
        }
        {
            void * t220 = gm_l0;
            gm_t88rcucg1b7uk t221 = gm_l1;
            gm_tkn42u7na3t81 t222;
            GM_ST(uint8_t, (uint8_t *)&t222 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t222 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t222), t221);
            ((uint8_t *)&t222)[4] = 1;
            uint64_t t223 = gm_l20;
            gm_tf5srh4mp4b3i t224;
            GM_ST(uint8_t, (uint8_t *)&t224 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t224 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t224 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t224), t223);
            ((uint8_t *)&t224)[8] = 1;
            gm_t88rcucg1b7uk t225 = gm_l21;
            gm_b16_8 t226 = gm_l2;
            gm_tl874t2atnlh3 t227 = gm_l3;
            void * t228 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t220, t222, t224, t225, t226, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t229 = 0;
            F.ret = t228;
            goto gm_fret;
        }
        goto me182;
mn202:;
ma185:;
        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
        if (t231 != ((uint8_t)13ULL)) goto mn230;
        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t181) + 8));
        gm_l24 = t232;
        void * t233 = gm_l0;
        gm_t88rcucg1b7uk t234 = gm_l1;
        gm_t88rcucg1b7uk t235 = gm_l24;
        gm_b16_8 t236 = gm_l2;
        gm_tl874t2atnlh3 t237 = gm_l3;
        void * t238 = gm_f_construct_optional_cffo1ol((void *)NULL, cx, NULL, t233, t234, t235, t236, t237);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t239 = 0;
        F.ret = t238;
        goto gm_fret;
        goto me182;
mn230:;
ma186:;
        uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&t181) + 0));
        if (t241 != ((uint8_t)20ULL)) goto mn240;
        gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t181) + 8) + 4));
        gm_l25 = t242;
        void * t243 = gm_l0;
        gm_t88rcucg1b7uk t244 = gm_l1;
        gm_t88rcucg1b7uk t245 = gm_l25;
        gm_b16_8 t246 = gm_l2;
        gm_tl874t2atnlh3 t247 = gm_l3;
        void * t248 = gm_f_construct_param_array_btk60nl((void *)NULL, cx, NULL, t243, t244, t245, t246, t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t249 = 0;
        F.ret = t248;
        goto gm_fret;
        goto me182;
mn240:;
ma187:;
        goto me182;
mn250:;
ma188:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 435, 5);
me182:;
    }
    {
        uint8_t t253 = 0;
        uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t255 = (!(t254 == ((uint64_t)1ULL)));
        if (!t255) {
            uint64_t t256 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_srch6h8rdicu443, 445, 29);
            uint8_t t258;
            uint8_t t261 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t256 * 144) + 0) + 0));
            if (t261 != ((uint8_t)0ULL)) goto mn260;
            t258 = ((uint8_t)0);
            goto me259;
mn260:;
            t258 = ((uint8_t)1);
            goto me259;
mn262:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 445, 29);
me259:;
            t253 = t258;
        } else t253 = 1;
        if (!t253) goto ie252;
        {
            void * t264 = gm_l0;
            gm_tl874t2atnlh3 t265 = gm_l3;
            void * t266 = gm_l0;
            gm_t88rcucg1b7uk t267 = gm_l1;
            gm_b16_8 t268 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t269;
            GM_ST(gm_b16_8, ((uint8_t *)&t269 + 0), t268);
            gm_tabb710d9mq2e t270;
            gm_f_format_458q1b3((void *)&t270, (void *)NULL, cx, NULL, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t271 = t270;
            if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 8, gm_srch6h8rdicu443, 446, 24);
            gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&t271) + 8));
            gm_b16_8 t273 = t272;
            gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&t273));
            gm_b0_1 t275 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t264, t265, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t276 = gm_l0;
            void * t277 = GM_LD(void *, (((uint8_t *)t276) + 0));
            gm_tl874t2atnlh3 t278 = gm_l3;
            void * t279 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t277, t278);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t280 = 0;
            F.ret = t279;
            goto gm_fret;
        }
        goto ix252;
ie252:;
ix252:;
    }
    {
        uint64_t t282 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t282 >= (uintptr_t)(t283)) gm_trap_index(t282, t283, gm_srch6h8rdicu443, 449, 23);
        uint8_t t284 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t282 * 144) + 0) + 0));
        if (t284 != ((uint8_t)0ULL)) goto ie281;
        gm_tc3oht4ksth9d t285 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t282 * 144) + 0) + 8));
        F.l26 = t285;
        {
            void * t288 = gm_l0;
            void * t289 = GM_LD(void *, (((uint8_t *)t288) + 0));
            gm_t88rcucg1b7uk t290 = gm_l1;
            gm_tkn42u7na3t81 t291 = gm_f_bits_struct_ty_l1kjtap(t289, GM_CONV(gm_csguankko4lmhd, t290));
            gm_tkn42u7na3t81 t292 = t291;
            if (!((((uint8_t *)&t292))[4] != 0)) goto ie287;
            gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t292));
            gm_l27 = t293;
            {
                void * t295 = gm_l0;
                void * t296 = (void *)((uint8_t *)&F.l26);
                gm_t88rcucg1b7uk t297 = gm_l27;
                gm_tkn42u7na3t81 t298;
                GM_ST(uint8_t, (uint8_t *)&t298 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t298 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t298), t297);
                ((uint8_t *)&t298)[4] = 1;
                void * t299 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t295, t296, t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t299;
            }
            {
                void * t301 = gm_l0;
                void * t302 = (void *)(((uint8_t *)t301) + 96);
                void * t303 = gm_l4;
                void * t304 = gm_l28;
                gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
                gm_taari1s0m51a7 t306 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t302, t303, t305);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_taari1s0m51a7 t307 = t306;
                uint8_t t308;
                if (((((uint8_t *)&t307))[1] != 0)) goto mn310;
                t308 = ((uint8_t)0);
                goto me309;
mn310:;
                t308 = ((uint8_t)1);
                goto me309;
mn311:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 453, 12);
me309:;
                if (!t308) goto ie300;
                {
                    void * t313 = gm_l0;
                    void * t314 = gm_l28;
                    gm_t88rcucg1b7uk t315 = gm_l27;
                    void * t316 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t313, t314, t315);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t316;
                }
                goto ix300;
ie300:;
ix300:;
            }
            {
                void * t317 = gm_l0;
                void * t318 = (void *)(((uint8_t *)t317) + 320);
                gm_thsh8t5u55gps t319;
                void * t320 = gm_l28;
                gm_t88rcucg1b7uk t321 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t320) + 0));
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t319 + 0, t321);
                gm_t88rcucg1b7uk t322 = gm_l1;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t319 + 4, t322);
                gm_tl874t2atnlh3 t323 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t319 + 8, t323);
                gm_tpm220j5qhjke t324;
                GM_ST(uint8_t, (uint8_t *)&t324 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t324 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t324 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t324 + 28, 0);
                GM_ST(uint64_t, (uint8_t *)&t324 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t324 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t324 + 0), ((uint8_t)1ULL));
                GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t324 + 8), t319);
                gm_tftb1odc739t5 t325 = gm_f_push_7koe9lk(t318, &t324);
                gm_tftb1odc739t5 t326 = t325;
                if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 1, gm_srch6h8rdicu443, 455, 9);
                gm_b0_1 t327 = GM_LD(gm_b0_1, (((uint8_t *)&t326) + 1));
            }
            {
                gm_t88rcucg1b7uk t328 = gm_l1;
                gm_tt8o5kebou62r t329;
                memset((uint8_t *)&t329 + 12, 0, 4);
                gm_tvdg94bk7aa22 t330;
                GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t330 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t329 + 0, t330);
                void * t331 = gm_l28;
                gm_b16_8 t332 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t331);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t329 + 16, t332);
                gm_teti411eujvf4 t333;
                GM_ST(uint8_t, (uint8_t *)&t333 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t333 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t333 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t333 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t333 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t333 + 0), ((uint8_t)33ULL));
                GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t333 + 8), t329);
                gm_tl874t2atnlh3 t334 = gm_l3;
                void * t335 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t328, &t333, t334);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t336 = 0;
                F.ret = t335;
                goto gm_fret;
            }
            goto ix287;
ie287:;
ix287:;
        }
        {
            void * t337 = gm_l0;
            void * t338 = (void *)((uint8_t *)&F.l26);
            gm_tkn42u7na3t81 t339;
            void * t341 = (void *)((uint8_t *)&F.l26);
            uint8_t t342 = gm_f_is_literal_tree_om58rum(t341);
            if (!t342) goto ie340;
            gm_t88rcucg1b7uk t344 = gm_l1;
            gm_t88rcucg1b7uk t345 = t344;
            gm_tkn42u7na3t81 t346;
            GM_ST(uint8_t, (uint8_t *)&t346 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t346 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t346), t345);
            ((uint8_t *)&t346)[4] = 1;
            t339 = t346;
            goto ix340;
ie340:;
            gm_tkn42u7na3t81 t348;
            memset(&t348, 0, sizeof t348);
            gm_tkn42u7na3t81 t349 = t348;
            t339 = t349;
ix340:;
            void * t350 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t337, t338, t339);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t350;
        }
        {
            void * t352 = gm_l0;
            void * t353 = (void *)(((uint8_t *)t352) + 96);
            void * t354 = gm_l4;
            void * t355 = gm_l29;
            gm_t88rcucg1b7uk t356 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t355) + 0));
            gm_taari1s0m51a7 t357 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t353, t354, t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_taari1s0m51a7 t358 = t357;
            uint8_t t359;
            if (((((uint8_t *)&t358))[1] != 0)) goto mn361;
            t359 = ((uint8_t)0);
            goto me360;
mn361:;
            t359 = ((uint8_t)1);
            goto me360;
mn362:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 8);
me360:;
            if (!t359) goto ie351;
            uint8_t t363 = 0;
            void * t364 = gm_l4;
            gm_t88rcucg1b7uk t365 = gm_l1;
            uint8_t t366 = gm_f_is_integer_8d4f9ek(t364, GM_CONV(gm_csguankko4lmhd, t365));
            if (!t366) {
                uint8_t t367 = 0;
                void * t368 = gm_l4;
                gm_t88rcucg1b7uk t369 = gm_l1;
                gm_t53at80pfdus3 t370;
                gm_f_kind_jcnt4hm((void *)&t370, t368, GM_CONV(gm_csguankko4lmhd, t369));
                gm_t53at80pfdus3 t371 = t370;
                uint8_t t372;
                uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&t371) + 0));
                if (t375 != ((uint8_t)3ULL)) goto mn374;
                t372 = ((uint8_t)1);
                goto me373;
mn374:;
                t372 = ((uint8_t)0);
                goto me373;
mn376:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 76);
me373:;
                if (t372) {
                    void * t377 = gm_l29;
                    uint8_t t378 = gm_f_tir_literal_oh960hv(t377);
                    t367 = t378;
                } else t367 = 0;
                t363 = t367;
            } else t363 = 1;
            if (!t363) goto ie351;
            {
                void * t381 = gm_l0;
                void * t382 = (void *)(((uint8_t *)t381) + 96);
                void * t383 = gm_l4;
                void * t384 = gm_l29;
                gm_t88rcucg1b7uk t385 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t384) + 0));
                gm_t88rcucg1b7uk t386 = gm_l1;
                gm_t9qkh0rsooc8d t387 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t382, t383, t385, t386);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9qkh0rsooc8d t388 = t387;
                uint8_t t389;
                if (!((((uint8_t *)&t388))[0] != 0)) goto mn391;
                t389 = ((uint8_t)0);
                goto me390;
mn391:;
                t389 = ((uint8_t)1);
                goto me390;
mn392:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 461, 12);
me390:;
                if (!t389) goto ie380;
                {
                    void * t395 = gm_l29;
                    uint8_t t396 = gm_f_tir_literal_oh960hv(t395);
                    uint8_t t397 = (uint8_t)!t396;
                    if (!t397) goto ie394;
                    {
                        void * t399 = gm_l0;
                        void * t400 = (void *)(((uint8_t *)t399) + 96);
                        void * t401 = gm_l4;
                        void * t402 = gm_l29;
                        gm_t88rcucg1b7uk t403 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t402) + 0));
                        gm_tl874t2atnlh3 t404 = gm_l3;
                        gm_f_note_fixed_by_ev84og7(t400, t401, GM_CONV(gm_csguankko4lmhd, t403), GM_CONV(gm_cseahvg8v7p0vs, t404));
                    }
                    goto ix394;
ie394:;
ix394:;
                }
                {
                    void * t405 = gm_l29;
                    uint8_t t406 = 0;
                    F.ret = t405;
                    goto gm_fret;
                }
                goto ix380;
ie380:;
ix380:;
            }
            {
                uint8_t t408 = 0;
                void * t409 = gm_l29;
                uint8_t t410 = gm_f_tir_literal_oh960hv(t409);
                uint8_t t411 = (uint8_t)!t410;
                if (!t411) {
                    void * t412 = gm_l0;
                    void * t413 = (void *)(((uint8_t *)t412) + 96);
                    void * t414 = gm_l4;
                    void * t415 = gm_l29;
                    gm_t88rcucg1b7uk t416 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t415) + 0));
                    gm_taari1s0m51a7 t417 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t413, t414, t416);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t9qkh0rsooc8d t418;
                    GM_ST(uint8_t, ((uint8_t *)&t418 + 0), ((uint8_t)1ULL));
                    gm_taari1s0m51a7 t419;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t419), t418);
                    ((uint8_t *)&t419)[1] = 1;
                    gm_taari1s0m51a7 t420 = t417;
                    gm_taari1s0m51a7 t421 = t419;
                    uint8_t t422 = (!gm_eq5307j5k9ms(((uint8_t *)&t420), ((uint8_t *)&t421)));
                    t408 = t422;
                } else t408 = 1;
                if (!t408) goto ie407;
                {
                    void * t424 = gm_l0;
                    void * t425 = (void *)(((uint8_t *)t424) + 320);
                    gm_thsh8t5u55gps t426;
                    void * t427 = gm_l29;
                    gm_t88rcucg1b7uk t428 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t427) + 0));
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t426 + 0, t428);
                    gm_t88rcucg1b7uk t429 = gm_l1;
                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t426 + 4, t429);
                    gm_tl874t2atnlh3 t430 = gm_l3;
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t426 + 8, t430);
                    gm_tpm220j5qhjke t431;
                    GM_ST(uint8_t, (uint8_t *)&t431 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t431 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t431 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t431 + 28, 0);
                    GM_ST(uint64_t, (uint8_t *)&t431 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t431 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t431 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t431 + 8), t426);
                    gm_tftb1odc739t5 t432 = gm_f_push_7koe9lk(t425, &t431);
                    gm_tftb1odc739t5 t433 = t432;
                    if (((((uint8_t *)&t433))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t433) + 1, gm_srch6h8rdicu443, 466, 13);
                    gm_b0_1 t434 = GM_LD(gm_b0_1, (((uint8_t *)&t433) + 1));
                }
                {
                    gm_t88rcucg1b7uk t435 = gm_l1;
                    gm_tt8o5kebou62r t436;
                    memset((uint8_t *)&t436 + 12, 0, 4);
                    gm_tvdg94bk7aa22 t437;
                    GM_ST(uint8_t, (uint8_t *)&t437 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t437 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t437 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t437 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t437 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t436 + 0, t437);
                    void * t438 = gm_l29;
                    gm_b16_8 t439 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t438);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t436 + 16, t439);
                    gm_teti411eujvf4 t440;
                    GM_ST(uint8_t, (uint8_t *)&t440 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t440 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t440 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t440 + 0), ((uint8_t)33ULL));
                    GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t440 + 8), t436);
                    gm_tl874t2atnlh3 t441 = gm_l3;
                    void * t442 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t435, &t440, t441);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t443 = 0;
                    F.ret = t442;
                    goto gm_fret;
                }
                goto ix407;
ie407:;
ix407:;
            }
            {
                void * t444 = gm_l0;
                gm_tl874t2atnlh3 t445 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                void * t446 = gm_l0;
                gm_t88rcucg1b7uk t447 = gm_l1;
                gm_b16_8 t448 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t446, t447);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t449;
                GM_ST(gm_b16_8, ((uint8_t *)&t449 + 0), t448);
                gm_tabb710d9mq2e t450;
                gm_f_format_i1v5tqv((void *)&t450, (void *)NULL, cx, NULL, t449);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t451 = t450;
                if (((((uint8_t *)&t451))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t451) + 8, gm_srch6h8rdicu443, 469, 26);
                gm_b16_8 t452 = GM_LD(gm_b16_8, (((uint8_t *)&t451) + 8));
                gm_b16_8 t453 = t452;
                gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&t453));
                gm_b0_1 t455 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t444, t445, t454);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t456 = gm_l29;
                uint8_t t457 = 0;
                F.ret = t456;
                goto gm_fret;
            }
            goto ix351;
ie351:;
ix351:;
        }
        {
            void * t458 = gm_l0;
            void * t459 = (void *)(((uint8_t *)t458) + 96);
            void * t460 = gm_l0;
            void * t461 = GM_LD(void *, (((uint8_t *)t460) + 0));
            void * t462 = gm_l29;
            gm_t88rcucg1b7uk t463 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t462) + 0));
            gm_t88rcucg1b7uk t464 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t459, t461, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t464;
        }
        {
            void * t466 = gm_l4;
            gm_t88rcucg1b7uk t467 = gm_l30;
            uint8_t t468 = gm_f_has_infer_3o82a0b(t466, GM_CONV(gm_csguankko4lmhd, t467));
            uint8_t t469 = (uint8_t)!t468;
            if (!t469) goto ie465;
            gm_t88rcucg1b7uk t470 = gm_l30;
            gm_t88rcucg1b7uk t471 = gm_l1;
            gm_t88rcucg1b7uk t472 = t470;
            gm_t88rcucg1b7uk t473 = t471;
            uint8_t t474 = (!gm_eq9m6mvtmgde(((uint8_t *)&t472), ((uint8_t *)&t473)));
            if (!t474) goto ie465;
            void * t475 = gm_l0;
            gm_t88rcucg1b7uk t476 = gm_l30;
            gm_t88rcucg1b7uk t477 = gm_l1;
            gm_th2lqbo77pvsc t478;
            gm_f_coercion_steps_ibqvhta((void *)&t478, (void *)NULL, cx, NULL, t475, t476, t477);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t479 = t478;
            uint8_t t480;
            if (((((uint8_t *)&t479))[16] != 0)) goto mn482;
            t480 = ((uint8_t)0);
            goto me481;
mn482:;
            t480 = ((uint8_t)1);
            goto me481;
mn483:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 474, 50);
me481:;
            if (!t480) goto ie465;
            {
                void * t485 = gm_l0;
                void * t486 = gm_l29;
                gm_t88rcucg1b7uk t487 = gm_l1;
                void * t488 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t485, t486, t487);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t489 = 0;
                F.ret = t488;
                goto gm_fret;
            }
            goto ix465;
ie465:;
ix465:;
        }
        {
            void * t491 = gm_l4;
            gm_t88rcucg1b7uk t492 = gm_l30;
            uint8_t t493 = gm_f_has_infer_3o82a0b(t491, GM_CONV(gm_csguankko4lmhd, t492));
            uint8_t t494 = (uint8_t)!t493;
            if (!t494) goto ie490;
            gm_t88rcucg1b7uk t495 = gm_l30;
            gm_t88rcucg1b7uk t496 = gm_l1;
            gm_t88rcucg1b7uk t497 = t495;
            gm_t88rcucg1b7uk t498 = t496;
            uint8_t t499 = (gm_eq9m6mvtmgde(((uint8_t *)&t497), ((uint8_t *)&t498)));
            if (!t499) goto ie490;
            void * t500 = gm_l4;
            gm_t88rcucg1b7uk t501 = gm_l30;
            uint8_t t502 = gm_f_is_error_8k27gd1(t500, GM_CONV(gm_csguankko4lmhd, t501));
            uint8_t t503 = (uint8_t)!t502;
            if (!t503) goto ie490;
            {
                gm_t88rcucg1b7uk t505 = gm_l1;
                gm_tmn7n0n9raou5 t506;
                GM_ST(uint8_t, (uint8_t *)&t506 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t506 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t506 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)8ULL));
                void * t507 = gm_l29;
                gm_t8jsc4n9vp2ib t508;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t508 + 0), t506);
                GM_ST(void *, ((uint8_t *)&t508 + 8), t507);
                gm_teti411eujvf4 t509;
                memset(&t509, 0, sizeof t509);
                GM_ST(uint8_t, ((uint8_t *)&t509 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t509 + 8), t508);
                gm_tl874t2atnlh3 t510 = gm_l3;
                void * t511 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t505, &t509, t510);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t512 = 0;
                F.ret = t511;
                goto gm_fret;
            }
            goto ix490;
ie490:;
ix490:;
        }
        {
            void * t513 = gm_l0;
            void * t514 = (void *)(((uint8_t *)t513) + 320);
            gm_thsh8t5u55gps t515;
            void * t516 = gm_l29;
            gm_t88rcucg1b7uk t517 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t516) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t515 + 0, t517);
            gm_t88rcucg1b7uk t518 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t515 + 4, t518);
            gm_tl874t2atnlh3 t519 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t515 + 8, t519);
            gm_tpm220j5qhjke t520;
            GM_ST(uint8_t, (uint8_t *)&t520 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t520 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t520 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t520 + 28, 0);
            GM_ST(uint64_t, (uint8_t *)&t520 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t520 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t520 + 0), ((uint8_t)1ULL));
            GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t520 + 8), t515);
            gm_tftb1odc739t5 t521 = gm_f_push_7koe9lk(t514, &t520);
            gm_tftb1odc739t5 t522 = t521;
            if (((((uint8_t *)&t522))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t522) + 1, gm_srch6h8rdicu443, 477, 5);
            gm_b0_1 t523 = GM_LD(gm_b0_1, (((uint8_t *)&t522) + 1));
        }
        {
            gm_t88rcucg1b7uk t524 = gm_l1;
            gm_tt8o5kebou62r t525;
            memset((uint8_t *)&t525 + 12, 0, 4);
            gm_tvdg94bk7aa22 t526;
            GM_ST(uint8_t, (uint8_t *)&t526 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t526 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t526 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t526 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t526 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t525 + 0, t526);
            void * t527 = gm_l29;
            gm_b16_8 t528 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t527);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t525 + 16, t528);
            gm_teti411eujvf4 t529;
            GM_ST(uint8_t, (uint8_t *)&t529 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t529 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t529 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t529 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t529 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t529 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t529 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t529 + 8), t525);
            gm_tl874t2atnlh3 t530 = gm_l3;
            void * t531 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t524, &t529, t530);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t532 = 0;
            F.ret = t531;
            goto gm_fret;
        }
        goto ix281;
ie281:;
        {
            void * t534 = gm_l0;
            void * t535 = GM_LD(void *, (((uint8_t *)t534) + 0));
            gm_tl874t2atnlh3 t536 = gm_l3;
            void * t537 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t535, t536);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t538 = 0;
            F.ret = t537;
            goto gm_fret;
        }
ix281:;
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
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_expect_eq_iu4ojmi F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tiilmfd16v3fc gm_l5; /* eref */
    void * gm_l6; /* tys */
    void * gm_l7; /* l0 */
    void * gm_l8; /* r0 */
    gm_t3mf58gv9kf3n gm_l9; /* eq */
    gm_t88rcucg1b7uk gm_l10; /* lt */
    gm_t88rcucg1b7uk gm_l11; /* rt */
    uint32_t gm_l12; /* x */
    uint32_t gm_l13; /* y */
    void * gm_l14; /* differ */
    gm_tnpifv20irnph gm_l16; /* set */
    void * gm_l20; /* write */
    void * gm_l21; /* msg */
    void * gm_l22; /* loc */
    void * gm_l24; /* stop */
    void * gm_l26; /* fail */
    void * gm_l28; /* check */
    GM_STACK_CHECK(&F, 3222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t6 = (!(t5 == ((uint64_t)2ULL)));
        if (!t6) {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_srch6h8rdicu443, 554, 29);
            uint8_t t9;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t7 * 144) + 0) + 0));
            if (t12 != ((uint8_t)0ULL)) goto mn11;
            t9 = ((uint8_t)0);
            goto me10;
mn11:;
            t9 = ((uint8_t)1);
            goto me10;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 29);
me10:;
            t4 = t9;
        } else t4 = 1;
        if (!t4) {
            uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srch6h8rdicu443, 554, 67);
            uint8_t t16;
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 144) + 0) + 0));
            if (t19 != ((uint8_t)0ULL)) goto mn18;
            t16 = ((uint8_t)0);
            goto me17;
mn18:;
            t16 = ((uint8_t)1);
            goto me17;
mn20:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 67);
me17:;
            t3 = t16;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t22 = gm_l0;
            gm_tl874t2atnlh3 t23 = gm_l2;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_scgahod5hue9b));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(55));
            gm_b0_1 t25 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            gm_tl874t2atnlh3 t28 = gm_l2;
            void * t29 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srch6h8rdicu443, 558, 23);
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 144) + 0) + 0));
        if (t34 != ((uint8_t)0ULL)) goto ie31;
        gm_tc3oht4ksth9d t35 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 144) + 0) + 8));
        F.l3 = t35;
        {
            uint64_t t38 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srch6h8rdicu443, 559, 23);
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 144) + 0) + 0));
            if (t40 != ((uint8_t)0ULL)) goto ie37;
            gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 144) + 0) + 8));
            F.l4 = t41;
            {
                void * t43 = gm_l0;
                void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_ss6g2koqn63bf));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(4));
                gm_tqu3m2l3n8mnc t46;
                gm_f_prelude_effect_gilrmm5((void *)&t46, (void *)NULL, cx, NULL, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqu3m2l3n8mnc t47 = t46;
                gm_tiilmfd16v3fc t48;
                if (((((uint8_t *)&t47))[24] != 0)) {
                    gm_tiilmfd16v3fc t49 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t47) + 0));
                    t48 = t49;
                } else {
                    void * t50 = gm_l0;
                    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                    gm_tl874t2atnlh3 t52 = gm_l2;
                    void * t53 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t54 = 0;
                    F.ret = t53;
                    goto gm_fret;
                }
                gm_l5 = t48;
            }
            {
                void * t55 = gm_l0;
                void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
                void * t57 = (void *)(((uint8_t *)t56) + 8);
                gm_l6 = t57;
            }
            {
                void * t58 = gm_l0;
                void * t59 = (void *)((uint8_t *)&F.l3);
                gm_tkn42u7na3t81 t60;
                memset(&t60, 0, sizeof t60);
                void * t61 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t58, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t61;
            }
            {
                void * t62 = gm_l0;
                void * t63 = (void *)((uint8_t *)&F.l4);
                void * t64 = gm_l7;
                gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 0));
                gm_tkn42u7na3t81 t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t66), t65);
                ((uint8_t *)&t66)[4] = 1;
                void * t67 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t62, t63, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t67;
            }
            {
                void * t68 = gm_l0;
                void * t69 = gm_l7;
                void * t70 = gm_l8;
                gm_tl874t2atnlh3 t71 = gm_l2;
                gm_t3mf58gv9kf3n t72 = gm_f_equalize_ni1r5ra((void *)NULL, cx, NULL, t68, t69, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t72;
            }
            {
                void * t73 = gm_l0;
                void * t74 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                gm_t88rcucg1b7uk t77 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t73, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t77;
            }
            {
                void * t78 = gm_l0;
                void * t79 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t79) + 0));
                gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                gm_t88rcucg1b7uk t82 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t78, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t82;
            }
            {
                void * t83 = gm_l0;
                gm_t88rcucg1b7uk t84 = gm_l10;
                gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                uint32_t t86 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t83, t84, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t86;
            }
            {
                void * t87 = gm_l0;
                gm_t88rcucg1b7uk t88 = gm_l11;
                gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                uint32_t t90 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t90;
            }
            {
                void * t91 = gm_l6;
                gm_t88rcucg1b7uk t92 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tlcl4u558l6p5 t93;
                memset((uint8_t *)&t93 + 17, 0, 7);
                gm_t88rcucg1b7uk t94 = gm_l10;
                uint32_t t95 = gm_l12;
                gm_teti411eujvf4 t96;
                memset(&t96, 0, sizeof t96);
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t96 + 8), t95);
                gm_tl874t2atnlh3 t97 = gm_l2;
                void * t98 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t93 + 0, t98);
                gm_t88rcucg1b7uk t99 = gm_l11;
                uint32_t t100 = gm_l13;
                gm_teti411eujvf4 t101;
                memset(&t101, 0, sizeof t101);
                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t101 + 8), t100);
                gm_tl874t2atnlh3 t102 = gm_l2;
                void * t103 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t99, &t101, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, (uint8_t *)&t93 + 8, t103);
                GM_ST(uint8_t, (uint8_t *)&t93 + 16, ((uint8_t)1));
                gm_teti411eujvf4 t104;
                memset(&t104, 0, sizeof t104);
                GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)43ULL));
                GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t104 + 8), t93);
                gm_tl874t2atnlh3 t105 = gm_l2;
                void * t106 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t92, &t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t106;
            }
            {
                gm_f_list_084fcd6((void *)&F.l15, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t107 = (void *)((uint8_t *)&F.l15);
                gm_tftb1odc739t5 t108 = gm_f_push_d1r4h19(t107, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l5)));
                gm_tftb1odc739t5 t109 = t108;
                if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srch6h8rdicu443, 571, 5);
                gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
            }
            {
                gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                gm_b16_8 t112 = t111;
                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                gm_tnpifv20irnph t114 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t114;
            }
            {
                void * t115 = gm_l0;
                void * t116 = (void *)((uint8_t *)&gm_l16);
                gm_tl874t2atnlh3 t117 = gm_l2;
                gm_b0_1 t118 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t115, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_f_list_epvducg((void *)&F.l17, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t119 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s8vts15kskc6n));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(10));
                gm_tftb1odc739t5 t121 = gm_f_push_3ef746b(t119, t120);
                gm_tftb1odc739t5 t122 = t121;
                if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 1, gm_srch6h8rdicu443, 576, 5);
                gm_b0_1 t123 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 1));
            }
            {
                void * t124 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t125;
                GM_ST(void *, (uint8_t *)&t125, (void *)(gm_srgp1m0q2ut8m));
                GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(11));
                gm_tftb1odc739t5 t126 = gm_f_push_3ef746b(t124, t125);
                gm_tftb1odc739t5 t127 = t126;
                if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srch6h8rdicu443, 577, 5);
                gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
            }
            {
                void * t129 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t130;
                GM_ST(void *, (uint8_t *)&t130, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(1));
                gm_tftb1odc739t5 t131 = gm_f_push_3ef746b(t129, t130);
                gm_tftb1odc739t5 t132 = t131;
                if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srch6h8rdicu443, 578, 5);
                gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
            }
            {
                gm_f_list_qtk22f3((void *)&F.l18, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t134 = (void *)((uint8_t *)&F.l18);
                gm_t7g88i52rrtm5 t135;
                gm_f_default_spec_9ntjq2s((void *)&t135);
                gm_tftb1odc739t5 t136 = gm_f_push_nbmosph(t134, &t135);
                gm_tftb1odc739t5 t137 = t136;
                if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srch6h8rdicu443, 580, 5);
                gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
            }
            {
                void * t139 = (void *)((uint8_t *)&F.l18);
                gm_t7g88i52rrtm5 t140;
                gm_f_default_spec_9ntjq2s((void *)&t140);
                gm_tftb1odc739t5 t141 = gm_f_push_nbmosph(t139, &t140);
                gm_tftb1odc739t5 t142 = t141;
                if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srch6h8rdicu443, 581, 5);
                gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l19, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t144 = (void *)((uint8_t *)&F.l19);
                gm_t88rcucg1b7uk t145 = gm_l11;
                uint32_t t146 = gm_l13;
                gm_teti411eujvf4 t147;
                memset(&t147, 0, sizeof t147);
                GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t147 + 8), t146);
                gm_tl874t2atnlh3 t148 = gm_l2;
                void * t149 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t150 = gm_f_push_d943vgp_2(t144, &(*(gm_t69m56meqefs8 *)((uint8_t *)t149)));
                gm_tftb1odc739t5 t151 = t150;
                if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srch6h8rdicu443, 583, 5);
                gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
            }
            {
                void * t153 = (void *)((uint8_t *)&F.l19);
                gm_t88rcucg1b7uk t154 = gm_l10;
                uint32_t t155 = gm_l12;
                gm_teti411eujvf4 t156;
                memset(&t156, 0, sizeof t156);
                GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t156 + 8), t155);
                gm_tl874t2atnlh3 t157 = gm_l2;
                void * t158 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t154, &t156, t157);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t159 = gm_f_push_d943vgp_2(t153, &(*(gm_t69m56meqefs8 *)((uint8_t *)t158)));
                gm_tftb1odc739t5 t160 = t159;
                if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_srch6h8rdicu443, 584, 5);
                gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
            }
            {
                void * t162 = gm_l6;
                gm_t88rcucg1b7uk t163 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tntn2tf15puib t164;
                gm_t419mo45g7j0a t165;
                memset((uint8_t *)&t165 + 28, 0, 4);
                memcpy((uint8_t *)&t165 + 0, ((uint8_t *)&gm_l5), 24);
                GM_ST(uint32_t, (uint8_t *)&t165 + 24, ((uint32_t)0ULL));
                gm_th1g69d7vpipg t166;
                GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t166 + 8), t165);
                GM_ST(gm_th1g69d7vpipg, (uint8_t *)&t164 + 0, t166);
                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                gm_b16_8 t168 = t167;
                gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                GM_ST(gm_b16_8, (uint8_t *)&t164 + 40, t169);
                gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                gm_b16_8 t171 = t170;
                gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                GM_ST(gm_b16_8, (uint8_t *)&t164 + 56, t172);
                gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t164 + 72, t173);
                gm_tq7s98ihonkju t174 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t164, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krbc9rueul4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t175 = t174;
                if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 8, gm_srch6h8rdicu443, 585, 40);
                void * t176 = GM_LD(void *, (((uint8_t *)&t175) + 8));
                gm_teti411eujvf4 t177;
                memset(&t177, 0, sizeof t177);
                GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)55ULL));
                GM_ST(void *, ((uint8_t *)&t177 + 8), t176);
                gm_tl874t2atnlh3 t178 = gm_l2;
                void * t179 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t163, &t177, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t179;
            }
            {
                void * t180 = gm_l6;
                gm_t88rcucg1b7uk t181 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t180);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t182;
                GM_ST(void *, (uint8_t *)&t182, (void *)(gm_smadkict0htlf));
                GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(37));
                gm_teti411eujvf4 t183;
                memset(&t183, 0, sizeof t183);
                GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t183 + 8), t182);
                gm_tl874t2atnlh3 t184 = gm_l2;
                void * t185 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t181, &t183, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t185;
            }
            {
                void * t186 = gm_l0;
                gm_tl874t2atnlh3 t187 = gm_l2;
                void * t188 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t188;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l23, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t189 = (void *)((uint8_t *)&F.l23);
                void * t190 = gm_l21;
                gm_tftb1odc739t5 t191 = gm_f_push_d943vgp_2(t189, &(*(gm_t69m56meqefs8 *)((uint8_t *)t190)));
                gm_tftb1odc739t5 t192 = t191;
                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srch6h8rdicu443, 589, 5);
                gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
            }
            {
                void * t194 = (void *)((uint8_t *)&F.l23);
                void * t195 = gm_l22;
                gm_tftb1odc739t5 t196 = gm_f_push_d943vgp_2(t194, &(*(gm_t69m56meqefs8 *)((uint8_t *)t195)));
                gm_tftb1odc739t5 t197 = t196;
                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srch6h8rdicu443, 590, 5);
                gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
            }
            {
                void * t199 = gm_l6;
                gm_t88rcucg1b7uk t200 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_to76jli3bqddq t201;
                gm_t419mo45g7j0a t202;
                memset((uint8_t *)&t202 + 28, 0, 4);
                memcpy((uint8_t *)&t202 + 0, ((uint8_t *)&gm_l5), 24);
                GM_ST(uint32_t, (uint8_t *)&t202 + 24, ((uint32_t)1ULL));
                gm_tsth9qu1dv0g3 t203;
                GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t203 + 8), t202);
                GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t201 + 0, t203);
                gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 40, t204);
                gm_teti411eujvf4 t205;
                GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)34ULL));
                GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t205 + 8), t201);
                gm_tl874t2atnlh3 t206 = gm_l2;
                void * t207 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t200, &t205, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t207;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l25, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t208 = (void *)((uint8_t *)&F.l25);
                void * t209 = gm_l20;
                gm_trcmmd12cefb1 t210;
                GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t210 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t210 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t210 + 8), t209);
                gm_tftb1odc739t5 t211 = gm_f_push_d6c76ud_2(t208, &t210);
                gm_tftb1odc739t5 t212 = t211;
                if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_srch6h8rdicu443, 593, 5);
                gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
            }
            {
                void * t214 = gm_l6;
                gm_t88rcucg1b7uk t215 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t214);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                void * t217 = gm_l24;
                gm_tl874t2atnlh3 t218 = gm_l2;
                void * t219 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t215, t216, t217, t218);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t219;
            }
            {
                gm_f_list_v5d1o9b_2((void *)&F.l27, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t220 = (void *)((uint8_t *)&F.l27);
                void * t221 = gm_l14;
                gm_tlr9dp305htm7 t222;
                GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t222 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)0ULL));
                GM_ST(void *, ((uint8_t *)&t222 + 8), t221);
                gm_tftb1odc739t5 t223 = gm_f_push_eb7kbhp_2(t220, &t222);
                gm_tftb1odc739t5 t224 = t223;
                if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 1, gm_srch6h8rdicu443, 596, 5);
                gm_b0_1 t225 = GM_LD(gm_b0_1, (((uint8_t *)&t224) + 1));
            }
            {
                void * t226 = gm_l6;
                gm_t88rcucg1b7uk t227 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t226);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trl2khmuphncd t228;
                gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
                void * t230 = gm_l26;
                GM_ST(void *, (uint8_t *)&t228 + 16, t230);
                GM_ST(void *, (uint8_t *)&t228 + 24, ((void *)0));
                gm_teti411eujvf4 t231;
                GM_ST(uint8_t, (uint8_t *)&t231 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t231 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t231 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t231 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t231 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t231 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)47ULL));
                GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t231 + 8), t228);
                gm_tl874t2atnlh3 t232 = gm_l2;
                void * t233 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t227, &t231, t232);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t233;
            }
            {
                gm_f_list_ir0h3tc_2((void *)&F.l29, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t234 = (void *)((uint8_t *)&F.l29);
                uint32_t t235 = gm_l12;
                void * t236 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                gm_ttggq7ng4kp3p t237;
                GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t237 + 0), t235);
                GM_ST(void *, ((uint8_t *)&t237 + 8), t236);
                gm_trcmmd12cefb1 t238;
                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t238 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t238 + 8), t237);
                gm_tftb1odc739t5 t239 = gm_f_push_d6c76ud_2(t234, &t238);
                gm_tftb1odc739t5 t240 = t239;
                if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_srch6h8rdicu443, 599, 5);
                gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
            }
            {
                void * t242 = (void *)((uint8_t *)&F.l29);
                uint32_t t243 = gm_l13;
                void * t244 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                gm_ttggq7ng4kp3p t245;
                GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t245 + 0), t243);
                GM_ST(void *, ((uint8_t *)&t245 + 8), t244);
                gm_trcmmd12cefb1 t246;
                GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t246 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t246 + 8), t245);
                gm_tftb1odc739t5 t247 = gm_f_push_d6c76ud_2(t242, &t246);
                gm_tftb1odc739t5 t248 = t247;
                if (((((uint8_t *)&t248))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t248) + 1, gm_srch6h8rdicu443, 600, 5);
                gm_b0_1 t249 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 1));
            }
            {
                void * t250 = (void *)((uint8_t *)&F.l29);
                void * t251 = gm_l28;
                gm_trcmmd12cefb1 t252;
                GM_ST(uint8_t, (uint8_t *)&t252 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t252 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t252 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t252 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t252 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t252 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t252 + 8), t251);
                gm_tftb1odc739t5 t253 = gm_f_push_d6c76ud_2(t250, &t252);
                gm_tftb1odc739t5 t254 = t253;
                if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srch6h8rdicu443, 601, 5);
                gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
            }
            {
                void * t256 = gm_l0;
                void * t257 = GM_LD(void *, (((uint8_t *)t256) + 0));
                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&F.l29) + 0));
                gm_tl874t2atnlh3 t259 = gm_l2;
                void * t260 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t257, t258, t259);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t261 = 0;
                F.ret = t260;
                goto gm_fret;
            }
            goto ix37;
ie37:;
            {
                void * t263 = gm_l0;
                void * t264 = GM_LD(void *, (((uint8_t *)t263) + 0));
                gm_tl874t2atnlh3 t265 = gm_l2;
                void * t266 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t264, t265);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t267 = 0;
                F.ret = t266;
                goto gm_fret;
            }
ix37:;
        }
        goto ix31;
ie31:;
        {
            void * t269 = gm_l0;
            void * t270 = GM_LD(void *, (((uint8_t *)t269) + 0));
            gm_tl874t2atnlh3 t271 = gm_l2;
            void * t272 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t270, t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t273 = 0;
            F.ret = t272;
            goto gm_fret;
        }
ix31:;
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
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_fill_defaults_3fq5ut6 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* pre */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* params */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* slot */
    void * gm_l11; /* v */
    void * gm_l12; /* p */
    void * gm_l13; /* d */
    GM_STACK_CHECK(&F, 944u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t5 = gm_l1;
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 56) + 0));
            gm_b16_8 t7 = t6;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_l7 = t8;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l8;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l8;
                    uint64_t t16 = gm_l8;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t19 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t17 * 8));
                    gm_t22qobfhrnoif t20;
                    GM_ST(uint64_t, ((uint8_t *)&t20 + 0), t15);
                    GM_ST(void *, ((uint8_t *)&t20 + 8), t19);
                    gm_t22qobfhrnoif t21 = t20;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
                    gm_l9 = t24;
                    void * t25 = GM_LD(void *, (((uint8_t *)&t21) + 8));
                    gm_l10 = t25;
                    {
                        void * t28 = gm_l10;
                        void * t29 = t28;
                        if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto ie27;
                        void * t30 = GM_LD(void *, ((uint8_t *)&t29));
                        gm_l11 = t30;
                        {
                            void * t32 = (void *)((uint8_t *)&F.l6);
                            void * t33 = gm_l11;
                            gm_tftb1odc739t5 t34 = gm_f_push_d943vgp_2(t32, &(*(gm_t69m56meqefs8 *)((uint8_t *)t33)));
                            gm_tftb1odc739t5 t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srch6h8rdicu443, 347, 13);
                            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                        }
                        {
                            goto lc9;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        uint64_t t37 = gm_l9;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srch6h8rdicu443, 350, 15);
                        void * t40 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t38 * 56);
                        gm_l12 = t40;
                    }
                    {
                        void * t42 = gm_l12;
                        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 32));
                        void * t44 = t43;
                        if (!(GM_LD(void *, ((uint8_t *)&t44)) != 0)) goto ie41;
                        void * t45 = GM_LD(void *, ((uint8_t *)&t44));
                        gm_l13 = t45;
                        {
                            void * t47 = (void *)((uint8_t *)&F.l6);
                            void * t48 = gm_l0;
                            void * t49 = gm_l12;
                            uint64_t t50 = gm_l2;
                            void * t51 = gm_l13;
                            void * t52 = gm_l12;
                            gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 16));
                            gm_tl874t2atnlh3 t54 = gm_l4;
                            void * t55 = gm_f_param_default_h93ci1m((void *)NULL, cx, NULL, t48, t49, t50, t51, t53, t54);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_tftb1odc739t5 t56 = gm_f_push_d943vgp_2(t47, &(*(gm_t69m56meqefs8 *)((uint8_t *)t55)));
                            gm_tftb1odc739t5 t57 = t56;
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srch6h8rdicu443, 352, 13);
                            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                        }
                        {
                            goto lc9;
                        }
                        goto ix41;
ie41:;
ix41:;
                    }
                    {
                        void * t60 = gm_l1;
                        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t60) + 145));
                        uint8_t t62 = (uint8_t)!t61;
                        if (!t62) goto ie59;
                        {
                            void * t65 = gm_l12;
                            uint8_t t66 = (((GM_LD(uintptr_t, (((uint8_t *)t65) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)t65) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                            if (!t66) goto ie64;
                            {
                                void * t68 = gm_l0;
                                gm_tl874t2atnlh3 t69 = gm_l4;
                                gm_t3877pk7lv748 t70;
                                void * t71 = gm_l1;
                                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)t71) + 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t72);
                                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                GM_ST(uint64_t, (uint8_t *)&t70 + 16, t73);
                                gm_tabb710d9mq2e t74;
                                gm_f_format_qjdh7n5((void *)&t74, (void *)NULL, cx, NULL, &t70);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                gm_tabb710d9mq2e t75 = t74;
                                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srch6h8rdicu443, 357, 32);
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                                gm_b16_8 t77 = t76;
                                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                                gm_b0_1 t79 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t68, t69, t78);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                            }
                            goto ix64;
ie64:;
                            {
                                void * t81 = gm_l0;
                                gm_tl874t2atnlh3 t82 = gm_l4;
                                void * t83 = gm_l12;
                                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 0));
                                gm_tnpifv20irnph t85;
                                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
                                gm_tabb710d9mq2e t86;
                                gm_f_format_qk20moi((void *)&t86, (void *)NULL, cx, NULL, t85);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                                gm_tabb710d9mq2e t87 = t86;
                                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srch6h8rdicu443, 359, 32);
                                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
                                gm_b16_8 t89 = t88;
                                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                                gm_b0_1 t91 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t81, t82, t90);
                                if (gm_unw.kind) {
                                    goto gm_ll13;
                                }
                            }
ix64:;
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    {
                        void * t92 = (void *)((uint8_t *)&F.l6);
                        void * t93 = gm_l0;
                        void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
                        gm_tl874t2atnlh3 t95 = gm_l4;
                        void * t96 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t94, t95);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                        gm_tftb1odc739t5 t97 = gm_f_push_d943vgp_2(t92, &(*(gm_t69m56meqefs8 *)((uint8_t *)t96)));
                        gm_tftb1odc739t5 t98 = t97;
                        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srch6h8rdicu443, 362, 9);
                        gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 345, 5);
me22:;
                }
lc9:;
                {
                    uint64_t t101 = gm_l8;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l8 = t104;
                }
            }
gm_ll13:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
            }
            goto gm_lad;
lb9:;
        }
    }
    {
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 0, t105);
        gm_b16_8 t106 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 16, t106);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_is_const_ref_arg_s0f37he F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* x */
    void * gm_l4; /* inner */
    void * gm_l5; /* h */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_l3 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l3;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            if (t5 != ((uint8_t)9ULL)) goto lb3;
            void * t6 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
            gm_l4 = t6;
            {
                void * t9 = gm_l4;
                gm_l3 = t9;
            }
lc3:;
        }
lb3:;
    }
    {
        void * t11 = gm_l3;
        void * t12 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t13 = t12;
        if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie10;
        void * t14 = GM_LD(void *, ((uint8_t *)&t13));
        gm_l5 = t14;
        {
            void * t16 = gm_l5;
            gm_l3 = t16;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t18 = gm_l3;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        if (t19 != ((uint8_t)12ULL)) goto ie17;
        uint8_t t20 = GM_LD(uint8_t, ((((((uint8_t *)t18) + 8) + 8) + 0) + 0));
        if (t20 != ((uint8_t)2ULL)) goto ie17;
        {
            void * t23 = gm_l0;
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l2;
            gm_t53at80pfdus3 t26;
            gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
            gm_t53at80pfdus3 t27 = t26;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            if (t28 != ((uint8_t)8ULL)) goto ie22;
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)&t27) + 8) + 0));
            gm_l6 = t29;
            {
                uint8_t t31 = gm_l6;
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t33 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_tree_om58rum F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* operand */
    void * gm_l3; /* lhs */
    void * gm_l4; /* rhs */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)0ULL)) goto pa13;
    goto po12;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    goto po12;
pa15:;
    goto mn11;
po12:;
    t2 = ((uint8_t)1);
    goto me3;
mn11:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t18 != ((uint8_t)9ULL)) goto mn17;
    void * t19 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l1 = t19;
    void * t20 = gm_l1;
    uint8_t t21 = gm_f_is_literal_tree_om58rum(t20);
    t2 = t21;
    goto me3;
mn17:;
ma6:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)12ULL)) goto mn22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l2 = t24;
    void * t25 = gm_l2;
    uint8_t t26 = gm_f_is_literal_tree_om58rum(t25);
    t2 = t26;
    goto me3;
mn22:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t28 != ((uint8_t)13ULL)) goto mn27;
    void * t29 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l3 = t29;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 16));
    gm_l4 = t30;
    uint8_t t31 = 0;
    void * t32 = gm_l3;
    uint8_t t33 = gm_f_is_literal_tree_om58rum(t32);
    if (t33) {
        void * t34 = gm_l4;
        uint8_t t35 = gm_f_is_literal_tree_om58rum(t34);
        t31 = t35;
    } else t31 = 0;
    t2 = t31;
    goto me3;
mn27:;
ma8:;
    t2 = ((uint8_t)0);
    goto me3;
mn36:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 494, 54);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_positional_t5fspc1 F;
    void * gm_l0; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb2;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 136));
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 32) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srch6h8rdicu443, 196, 41);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 32)) + t10 * 1));
            uint8_t t14 = (uint8_t)!t13;
            if (!t14) goto lb2;
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 136));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 196, 64);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 136), t22);
            }
lc2:;
        }
lb2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 136));
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
        uint8_t t28 = (t25 >= t27);
        if (!t28) goto ie23;
        {
            gm_tf5srh4mp4b3i t30;
            memset(&t30, 0, sizeof t30);
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t32 = gm_l0;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 136));
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
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_index_78k4iut F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 476u);
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
                    gm_tgsvmddj3e85e t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 56), 56);
                    gm_tgsvmddj3e85e t15 = t10;
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l4 = t18;
                    gm_t5v0c8ia6a5mo t19 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)&t15) + 8));
                    F.l5 = t19;
                    {
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie21;
                        {
                            uint64_t t28 = gm_l4;
                            gm_tf5srh4mp4b3i t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t29), t28);
                            ((uint8_t *)&t29)[8] = 1;
                            uint8_t t30 = 0;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 171, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb4:;
        }
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
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_print_bhebp9d F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* to_out */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* effect_name */
    gm_tiilmfd16v3fc gm_l9; /* eref */
    gm_tnpifv20irnph gm_l11; /* set */
    GM_STACK_CHECK(&F, 1183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_srch6h8rdicu443, 607, 25);
            uint8_t t8;
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 144) + 0) + 0));
            if (t11 != ((uint8_t)0ULL)) goto mn10;
            t8 = ((uint8_t)0);
            goto me9;
mn10:;
            t8 = ((uint8_t)1);
            goto me9;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 607, 25);
me9:;
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t14 = gm_l0;
            gm_tl874t2atnlh3 t15 = gm_l3;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sijukd7vsocnt));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(67));
            gm_b0_1 t17 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l3;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t24 = gm_l2;
            gm_l4 = t24;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t26 = gm_l5;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto lb25;
                {
                    uint64_t t31 = gm_l5;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t7te2tdpebbt4 t36 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 144));
                    F.l6 = t36;
                    {
                        uint8_t t39;
                        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 0) + 0));
                        if (t42 != ((uint8_t)1ULL)) goto mn41;
                        t39 = ((uint8_t)1);
                        goto me40;
mn41:;
                        t39 = ((uint8_t)0);
                        goto me40;
mn43:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 612, 12);
me40:;
                        if (!t39) goto ie38;
                        {
                            void * t45 = gm_l0;
                            gm_tl874t2atnlh3 t46 = gm_l3;
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sijukd7vsocnt));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(67));
                            gm_b0_1 t48 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                        }
                        {
                            void * t49 = gm_l0;
                            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                            gm_tl874t2atnlh3 t51 = gm_l3;
                            void * t52 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t50, t51);
                            if (gm_unw.kind) {
                                goto gm_ll29;
                            }
                            uint8_t t53 = 0;
                            F.ret = t52;
                            goto gm_fret;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 611, 5);
me34:;
                }
lc25:;
                {
                    uint64_t t55 = gm_l5;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l5 = t58;
                }
            }
gm_ll29:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
            }
            goto gm_lad;
lb25:;
        }
    }
    {
        uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srch6h8rdicu443, 617, 30);
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t60 * 144) + 0) + 0));
        if (t62 != ((uint8_t)0ULL)) goto ie59;
        gm_tc3oht4ksth9d t63 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t60 * 144) + 0) + 8));
        F.l7 = t63;
        {
            gm_b16_8 t65;
            uint8_t t67 = gm_l1;
            if (!t67) goto ie66;
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sn937bfucthvg));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(3));
            gm_b16_8 t70 = t69;
            t65 = t70;
            goto ix66;
ie66:;
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_srqehfttj6ngk));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(3));
            gm_b16_8 t73 = t72;
            t65 = t73;
ix66:;
            gm_l8 = t65;
        }
        {
            void * t74 = gm_l0;
            void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
            gm_b16_8 t76 = gm_l8;
            gm_tqu3m2l3n8mnc t77;
            gm_f_prelude_effect_gilrmm5((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqu3m2l3n8mnc t78 = t77;
            gm_tiilmfd16v3fc t79;
            if (((((uint8_t *)&t78))[24] != 0)) {
                gm_tiilmfd16v3fc t80 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t78) + 0));
                t79 = t80;
            } else {
                void * t81 = gm_l0;
                void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                gm_tl874t2atnlh3 t83 = gm_l3;
                void * t84 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t85 = 0;
                F.ret = t84;
                goto gm_fret;
            }
            gm_l9 = t79;
        }
        {
            gm_f_list_084fcd6((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t86 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t87 = gm_f_push_d1r4h19(t86, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
            gm_tftb1odc739t5 t88 = t87;
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_srch6h8rdicu443, 621, 5);
            gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
        }
        {
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t91 = t90;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            gm_tnpifv20irnph t93 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t93;
        }
        {
            void * t94 = gm_l0;
            void * t95 = (void *)((uint8_t *)&gm_l11);
            gm_tl874t2atnlh3 t96 = gm_l3;
            gm_b0_1 t97 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t94, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t98 = gm_l0;
            gm_t419mo45g7j0a t99;
            memset((uint8_t *)&t99 + 28, 0, 4);
            memcpy((uint8_t *)&t99 + 0, ((uint8_t *)&gm_l9), 24);
            GM_ST(uint32_t, (uint8_t *)&t99 + 24, ((uint32_t)0ULL));
            gm_th1g69d7vpipg t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t100 + 8), t99);
            void * t101 = (void *)((uint8_t *)&F.l7);
            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t103 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t104 = (uintptr_t)((uint64_t)1ULL);
            if (t104 > (uintptr_t)t102 || (uintptr_t)t102 > (uintptr_t)t102) gm_trap_slice(t104, (uintptr_t)t102, t102, gm_srch6h8rdicu443, 624, 57);
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(((uint8_t *)t103 + t104 * 144)));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)((uintptr_t)t102 - t104));
            gm_tl874t2atnlh3 t106 = gm_l3;
            void * t107 = gm_f_build_write_k6dcf8r((void *)NULL, cx, NULL, t98, &t100, t101, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t108 = 0;
            F.ret = t107;
            goto gm_fret;
        }
        goto ix59;
ie59:;
        {
            void * t110 = gm_l0;
            void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
            gm_tl874t2atnlh3 t112 = gm_l3;
            void * t113 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t114 = 0;
            F.ret = t113;
            goto gm_fret;
        }
ix59:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tir_literal_oh960hv F;
    void * gm_l0; /* e */
    void * gm_l1; /* operand */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)0ULL)) goto pa12;
    goto po11;
pa12:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)1ULL)) goto pa14;
    goto po11;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    goto po11;
pa16:;
    goto mn10;
po11:;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t19 != ((uint8_t)29ULL)) goto mn18;
    void * t20 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l1 = t20;
    void * t21 = gm_l1;
    uint8_t t22 = gm_f_tir_literal_oh960hv(t21);
    t2 = t22;
    goto me3;
mn18:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t24 != ((uint8_t)30ULL)) goto mn23;
    void * t25 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l2 = t25;
    void * t26 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 16));
    gm_l3 = t26;
    uint8_t t27 = 0;
    void * t28 = gm_l2;
    uint8_t t29 = gm_f_tir_literal_oh960hv(t28);
    if (t29) {
        void * t30 = gm_l3;
        uint8_t t31 = gm_f_tir_literal_oh960hv(t30);
        t27 = t31;
    } else t27 = 0;
    t2 = t27;
    goto me3;
mn23:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn32:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 503, 40);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_trap_t21p26s F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l4; /* msg */
    GM_STACK_CHECK(&F, 712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie3;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_t88rcucg1b7uk t10 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_teti411eujvf4 t12;
        memset(&t12, 0, sizeof t12);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
        gm_tl874t2atnlh3 t13 = gm_l2;
        void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t15 = t14;
        t2 = t15;
        goto ix3;
ie3:;
        void * t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)1ULL)));
        if (!t19) goto ie17;
        uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srch6h8rdicu443, 514, 33);
        uint8_t t22;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 144) + 0) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        t22 = ((uint8_t)1);
        goto me23;
mn24:;
        t22 = ((uint8_t)0);
        goto me23;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 514, 33);
me23:;
        if (!t22) goto ie17;
        void * t28;
        uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srch6h8rdicu443, 515, 27);
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t30 * 144) + 0) + 0));
        if (t32 != ((uint8_t)0ULL)) goto ie29;
        gm_tc3oht4ksth9d t33 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t30 * 144) + 0) + 8));
        F.l3 = t33;
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = (void *)((uint8_t *)&F.l3);
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_t88rcucg1b7uk t41 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[4] = 1;
        void * t43 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t36, t37, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        gm_t88rcucg1b7uk t47 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t48 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t35, t43, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t49 = t48;
        t28 = t49;
        goto ix29;
ie29:;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        gm_tl874t2atnlh3 t53 = gm_l2;
        void * t54 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55 = 0;
        F.ret = t54;
        goto gm_fret;
        t28 = ((void *)0);
ix29:;
        void * t56 = t28;
        t16 = t56;
        goto ix17;
ie17:;
        {
            void * t58 = gm_l0;
            gm_tl874t2atnlh3 t59 = gm_l2;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sj4n6nvsgq2jn));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(42));
            gm_b0_1 t61 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t58, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        gm_tl874t2atnlh3 t64 = gm_l2;
        void * t65 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t66 = 0;
        F.ret = t65;
        goto gm_fret;
        t16 = ((void *)0);
ix17:;
        t2 = t16;
ix3:;
        gm_l4 = t2;
    }
    {
        void * t67 = gm_l0;
        void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
        void * t69 = (void *)(((uint8_t *)t68) + 8);
        gm_t88rcucg1b7uk t70 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = gm_l4;
        gm_teti411eujvf4 t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)52ULL));
        GM_ST(void *, ((uint8_t *)&t72 + 8), t71);
        gm_tl874t2atnlh3 t73 = gm_l2;
        void * t74 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t70, &t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t75 = 0;
        F.ret = t74;
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
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_variadic_arg_dfdnlo3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* a */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint16_t gm_l5; /* bits */
    uint16_t gm_l6; /* bits */
    gm_t88rcucg1b7uk gm_l7; /* inner */
    uint8_t gm_l8; /* ok */
    GM_STACK_CHECK(&F, 688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
        if (t4 != ((uint8_t)0ULL)) goto ie2;
        gm_tc3oht4ksth9d t5 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t3) + 0) + 8));
        F.l2 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)((uint8_t *)&F.l2);
            gm_tkn42u7na3t81 t9;
            memset(&t9, 0, sizeof t9);
            void * t10 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t10;
        }
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 96);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = gm_l3;
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 0));
            gm_t88rcucg1b7uk t17 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t12, t14, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            gm_t88rcucg1b7uk t21 = gm_l4;
            gm_t53at80pfdus3 t22;
            gm_f_kind_jcnt4hm((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
            gm_t53at80pfdus3 t23 = t22;
            uint8_t t24;
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            switch (t32) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma27; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma30; case ((uint8_t)13ULL): goto ma29; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma27; default: goto ma30; }
ma26:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t34 != ((uint8_t)0ULL)) goto mn33;
            uint16_t t35 = GM_LD(uint16_t, ((((uint8_t *)&t23) + 8) + 2));
            gm_l5 = t35;
            uint16_t t36 = gm_l5;
            gm_tuqvtbj0p3v9c t37;
            GM_ST(uint16_t, ((uint8_t *)&t37 + 0), ((uint16_t)32ULL));
            GM_ST(uint16_t, ((uint8_t *)&t37 + 2), ((uint16_t)64ULL));
            gm_tuqvtbj0p3v9c t38 = t37;
            uint8_t t39 = 0;
            uint16_t t40 = t36;
            if (gm_eq9oqmmiu8qc(((uint8_t *)&t38) + 0, ((uint8_t *)&t40))) t39 = 1;
            if (gm_eq9oqmmiu8qc(((uint8_t *)&t38) + 2, ((uint8_t *)&t40))) t39 = 1;
            uint8_t t41 = (uint8_t)(t39);
            t24 = t41;
            goto me25;
mn33:;
ma27:;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t45 != ((uint8_t)1ULL)) goto pa44;
            goto po43;
pa44:;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t47 != ((uint8_t)2ULL)) goto pa46;
            goto po43;
pa46:;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t49 != ((uint8_t)7ULL)) goto pa48;
            goto po43;
pa48:;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t51 != ((uint8_t)8ULL)) goto pa50;
            goto po43;
pa50:;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t53 != ((uint8_t)9ULL)) goto pa52;
            goto po43;
pa52:;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t55 != ((uint8_t)23ULL)) goto pa54;
            goto po43;
pa54:;
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t57 != ((uint8_t)26ULL)) goto pa56;
            goto po43;
pa56:;
            goto mn42;
po43:;
            t24 = ((uint8_t)1);
            goto me25;
mn42:;
ma28:;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t59 != ((uint8_t)3ULL)) goto mn58;
            uint16_t t60 = GM_LD(uint16_t, (((uint8_t *)&t23) + 8));
            gm_l6 = t60;
            uint16_t t61 = gm_l6;
            uint8_t t62 = ((t61 == ((uint16_t)64ULL)));
            t24 = t62;
            goto me25;
mn58:;
ma29:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t64 != ((uint8_t)13ULL)) goto mn63;
            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t23) + 8));
            gm_l7 = t65;
            void * t66 = gm_l0;
            void * t67 = GM_LD(void *, (((uint8_t *)t66) + 0));
            gm_t88rcucg1b7uk t68 = gm_l7;
            uint8_t t69 = gm_f_is_nullable_pointer_nkie06s(t67, GM_CONV(gm_csguankko4lmhd, t68));
            t24 = t69;
            goto me25;
mn63:;
ma30:;
            t24 = ((uint8_t)0);
            goto me25;
mn70:;
ma31:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 636, 11);
me25:;
            gm_l8 = t24;
        }
        {
            uint8_t t72 = gm_l8;
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) goto ie71;
            {
                void * t75 = gm_l0;
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
                void * t77 = gm_l0;
                gm_t88rcucg1b7uk t78 = gm_l4;
                gm_b16_8 t79 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t80;
                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
                gm_tabb710d9mq2e t81;
                gm_f_format_kub2tep((void *)&t81, (void *)NULL, cx, NULL, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t82 = t81;
                if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_srch6h8rdicu443, 643, 34);
                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                gm_b16_8 t84 = t83;
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                gm_b0_1 t86 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t75, t76, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
            void * t87 = gm_l3;
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            {
                void * t91 = gm_l0;
                void * t92 = gm_l1;
                gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 128));
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sbs1lj6q8rfiu));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(43));
                gm_b0_1 t95 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t91, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t96 = gm_l0;
                void * t97 = GM_LD(void *, (((uint8_t *)t96) + 0));
                void * t98 = gm_l1;
                gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t98) + 128));
                void * t100 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t97, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t101 = 0;
                F.ret = t100;
                goto gm_fret;
            }
        }
ix2:;
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
