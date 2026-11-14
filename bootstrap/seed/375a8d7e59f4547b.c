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
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_toceffb697p0u f2; } gm_tqs4ilbobv1qq;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqs4ilbobv1qq) == 56 && _Alignof(gm_tqs4ilbobv1qq) == 8 && offsetof(gm_tqs4ilbobv1qq, f0) == 0 && offsetof(gm_tqs4ilbobv1qq, f1) == 24 && offsetof(gm_tqs4ilbobv1qq, f2) == 40, "layout of `MatchReport`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; uint32_t v1; gm_tngtuiuskn938 v2; gm_b16_8 v3; } f1; } gm_taevtln836oa8;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
_Static_assert(sizeof(gm_taevtln836oa8) == 104 && _Alignof(gm_taevtln836oa8) == 8 && offsetof(gm_taevtln836oa8, f0) == 0 && offsetof(gm_taevtln836oa8, f1) == 8, "layout of `Ctor`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_taevtln836oa8 f1; gm_b16_8 f2; } gm_tbq1t03a53gpo;
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_tbq1t03a53gpo v1; } f1; } gm_t8oufsr65o5ha;
_Static_assert(sizeof(gm_tbq1t03a53gpo) == 128 && _Alignof(gm_tbq1t03a53gpo) == 8 && offsetof(gm_tbq1t03a53gpo, f0) == 0 && offsetof(gm_tbq1t03a53gpo, f1) == 8 && offsetof(gm_tbq1t03a53gpo, f2) == 112, "layout of `(ty: Ty, ctor: Ctor, subs: []DPat)`");
_Static_assert(sizeof(gm_t8oufsr65o5ha) == 136 && _Alignof(gm_t8oufsr65o5ha) == 8 && offsetof(gm_t8oufsr65o5ha, f0) == 0 && offsetof(gm_t8oufsr65o5ha, f1) == 8, "layout of `DPat`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_all_ctors_o7aijfe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tngtuiuskn938 l9; /* r */
    gm_tbci5ospk5nnn l10; /* min */
    gm_tbci5ospk5nnn l11; /* end */
    gm_trqfvmjtt4sij l12; /* cuts */
    gm_t8oufsr65o5ha l15; /* d */
    gm_taevtln836oa8 l16; /* c */
    gm_tbci5ospk5nnn l17; /* lo */
    gm_tbci5ospk5nnn l18; /* hi */
    gm_trqfvmjtt4sij l19; /* uniq */
    gm_tbci5ospk5nnn l22; /* c */
    gm_tbci5ospk5nnn l26; /* a */
    gm_tbci5ospk5nnn l27; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_alternatives_r0dto2k {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_check_match_mj577a4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* rows */
    gm_trqfvmjtt4sij l5; /* unreachable */
    gm_t8oufsr65o5ha l14; /* alt */
    gm_trqfvmjtt4sij l15; /* one */
    gm_t8oufsr65o5ha l18; /* alt */
    gm_trqfvmjtt4sij l19; /* row */
    gm_trqfvmjtt4sij l20; /* q */
    gm_tqs4ilbobv1qq ret;
};
struct gm_fr_column_covers_gn61qhr {
    gm_frame h;
    gm_t8oufsr65o5ha l4; /* d */
    gm_taevtln836oa8 l5; /* hc */
    uint8_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_covers_59m49eg {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* a */
    gm_tbci5ospk5nnn l3; /* b */
    gm_tbci5ospk5nnn l4; /* x */
    gm_tbci5ospk5nnn l5; /* y */
    uint8_t ret;
};
struct gm_fr_ctor_eq_fhbfduc {
    gm_frame h;
    gm_tbci5ospk5nnn l4; /* lo */
    gm_tbci5ospk5nnn l5; /* hi */
    gm_tbci5ospk5nnn l6; /* l2 */
    gm_tbci5ospk5nnn l7; /* h2 */
    uint8_t ret;
};
struct gm_fr_dpat_ty_2ipsk5e {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6rs1n5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c8h0sd2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kvdmouk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sv0bumj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_fn_j6p61la {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_word_n4uc7oo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_bounds_7npnb0b {
    gm_frame h;
    gm_tbci5ospk5nnn l7; /* half */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_irrefutable_lpkmeii {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* rows */
    gm_t8oufsr65o5ha l5; /* d */
    gm_trqfvmjtt4sij l6; /* row */
    gm_trqfvmjtt4sij l7; /* q */
    uint8_t ret;
};
struct gm_fr_is_name_char_eanps9t {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_99597qp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ahp090e {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_furncbs {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ghuba9i {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lower_product_5gakcnb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tbci5ospk5nnn l11; /* v */
    gm_tbci5ospk5nnn l13; /* lo */
    gm_tbci5ospk5nnn l14; /* hi */
    gm_tbci5ospk5nnn l16; /* h */
    gm_trqfvmjtt4sij l22; /* subs */
    gm_trqfvmjtt4sij l27; /* first */
    gm_trqfvmjtt4sij l31; /* combos */
    gm_trqfvmjtt4sij l36; /* next */
    gm_t8oufsr65o5ha l42; /* o */
    gm_trqfvmjtt4sij l43; /* c2 */
    gm_b16_8 ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_nested_present_binding_r48665i {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_rebuild_90secge {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_trqfvmjtt4sij l5; /* out */
    gm_b16_8 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_witness_fmsa425 {
    gm_frame h;
    gm_taevtln836oa8 l3; /* c */
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l13; /* inner */
    gm_trqfvmjtt4sij l18; /* parts */
    gm_t0mglksrkfa5e l23; /* f */
    gm_b16_8 ret;
};
struct gm_fr_sort_by_sbaob8l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_specialize_si0k4v5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_taevtln836oa8 l7; /* hc */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_sub_types_o2rpif3 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_useful_ge1iad5 {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* column */
    gm_taevtln836oa8 l9; /* c */
    gm_trqfvmjtt4sij l10; /* pieces */
    gm_tbci5ospk5nnn l11; /* lo */
    gm_tbci5ospk5nnn l12; /* hi */
    gm_trqfvmjtt4sij l13; /* with_head */
    gm_taevtln836oa8 l17; /* p */
    gm_tbci5ospk5nnn l18; /* x */
    gm_tbci5ospk5nnn l19; /* y */
    gm_taevtln836oa8 l22; /* piece */
    gm_trqfvmjtt4sij l24; /* spec_rows */
    gm_taevtln836oa8 l36; /* c */
    gm_taevtln836oa8 l39; /* c */
    gm_trqfvmjtt4sij l41; /* spec_rows */
    gm_trqfvmjtt4sij l48; /* default */
    gm_t8oufsr65o5ha l53; /* first */
    gm_taevtln836oa8 l56; /* c */
    gm_trqfvmjtt4sij l57; /* subs */
    gm_trqfvmjtt4sij l61; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_wrap_each_2nbqohg {
    gm_frame h;
    gm_taevtln836oa8 l1; /* c */
    gm_t8oufsr65o5ha l6; /* d */
    gm_trqfvmjtt4sij l7; /* subs */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_eht4gub(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_08505jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0gv6kss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hj7ahc4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i83sdhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_introsort_jhfnj7n(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_33sq608(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_95uqvlq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h9qsb4d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s94euqb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src8lmgjh8bsvgp[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dpat_ty_2ipsk5e F;
    void * gm_l0; /* d */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t5 != ((uint8_t)0ULL)) goto mn4;
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1) + 8));
    gm_l1 = t6;
    gm_t88rcucg1b7uk t7 = gm_l1;
    t2 = t7;
    goto me3;
mn4:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t1) + 8) + 0));
    gm_l2 = t10;
    gm_t88rcucg1b7uk t11 = gm_l2;
    t2 = t11;
    goto me3;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 27, 40);
me3:;
    F.ret = t2;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6rs1n5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6rs1n5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c8h0sd2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c8h0sd2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kvdmouk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kvdmouk F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sv0bumj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sv0bumj F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        int32_t t11 = gm_f_mag_cmp_s39veq6(t7, t10);
        uint8_t t12 = ((t11 == ((int32_t)0LL)));
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_j6p61la F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    int32_t t3 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)t1)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)t2)));
    uint8_t t4 = (t3 < ((int32_t)0LL));
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_99597qp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_99597qp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6rs1n5s();
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
inline __attribute__((aligned(4))) void gm_f_list_ahp090e(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ahp090e F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_kvdmouk();
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
inline __attribute__((aligned(4))) void gm_f_list_furncbs(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_furncbs F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_sv0bumj();
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
inline __attribute__((aligned(4))) void gm_f_list_ghuba9i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ghuba9i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c8h0sd2();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_sbaob8l(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_sbaob8l F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_jhfnj7n(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
__attribute__((aligned(4))) void gm_f_all_ctors_o7aijfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_all_ctors_o7aijfe F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* column */
    void * gm_l4; /* tys */
    gm_tep2drsl9408b gm_l5; /* uv */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* k */
    GM_STACK_CHECK(&F, 3623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_ahp090e((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l4;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; default: goto ma15; }
ma9:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t19 != ((uint8_t)4ULL)) goto mn18;
        {
            void * t21 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t22 + 8), ((uint8_t)0));
            gm_tftb1odc739t5 t23 = gm_f_push_95uqvlq(t21, &t22);
            gm_tftb1odc739t5 t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src8lmgjh8bsvgp, 244, 13);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
        }
        {
            void * t26 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t27;
            memset(&t27, 0, sizeof t27);
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t27 + 8), ((uint8_t)1));
            gm_tftb1odc739t5 t28 = gm_f_push_95uqvlq(t26, &t27);
            gm_tftb1odc739t5 t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_src8lmgjh8bsvgp, 245, 13);
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
        }
        goto me8;
mn18:;
ma10:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t32 != ((uint8_t)13ULL)) goto mn31;
        {
            void * t34 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t35;
            memset(&t35, 0, sizeof t35);
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)1ULL));
            gm_tftb1odc739t5 t36 = gm_f_push_95uqvlq(t34, &t35);
            gm_tftb1odc739t5 t37 = t36;
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src8lmgjh8bsvgp, 248, 13);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        {
            void * t39 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)2ULL));
            gm_tftb1odc739t5 t41 = gm_f_push_95uqvlq(t39, &t40);
            gm_tftb1odc739t5 t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src8lmgjh8bsvgp, 249, 13);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        goto me8;
mn31:;
ma11:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t45 != ((uint8_t)14ULL)) goto mn44;
        {
            void * t47 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t48;
            memset(&t48, 0, sizeof t48);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)3ULL));
            gm_tftb1odc739t5 t49 = gm_f_push_95uqvlq(t47, &t48);
            gm_tftb1odc739t5 t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src8lmgjh8bsvgp, 252, 13);
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
        }
        {
            void * t52 = (void *)((uint8_t *)&F.l3);
            gm_taevtln836oa8 t53;
            memset(&t53, 0, sizeof t53);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)4ULL));
            gm_tftb1odc739t5 t54 = gm_f_push_95uqvlq(t52, &t53);
            gm_tftb1odc739t5 t55 = t54;
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src8lmgjh8bsvgp, 253, 13);
            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
        }
        goto me8;
mn44:;
ma12:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t58 != ((uint8_t)12ULL)) goto mn57;
        void * t59 = (void *)((uint8_t *)&F.l3);
        gm_taevtln836oa8 t60;
        memset(&t60, 0, sizeof t60);
        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)6ULL));
        gm_tftb1odc739t5 t61 = gm_f_push_95uqvlq(t59, &t60);
        gm_tftb1odc739t5 t62 = t61;
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_src8lmgjh8bsvgp, 255, 22);
        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        goto me8;
mn57:;
ma13:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t65 != ((uint8_t)5ULL)) goto mn64;
        goto me8;
mn64:;
ma14:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t68 != ((uint8_t)17ULL)) goto mn67;
        {
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l1;
            gm_th2lqbo77pvsc t73;
            gm_f_struct_fields_chqu3it((void *)&t73, t71, GM_CONV(gm_csguankko4lmhd, t72));
            gm_th2lqbo77pvsc t74 = t73;
            uint8_t t75;
            if (((((uint8_t *)&t74))[16] != 0)) goto mn77;
            t75 = ((uint8_t)0);
            goto me76;
mn77:;
            t75 = ((uint8_t)1);
            goto me76;
mn78:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 258, 16);
me76:;
            if (!t75) goto ie70;
            {
                void * t80 = (void *)((uint8_t *)&F.l3);
                gm_taevtln836oa8 t81;
                memset(&t81, 0, sizeof t81);
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)6ULL));
                gm_tftb1odc739t5 t82 = gm_f_push_95uqvlq(t80, &t81);
                gm_tftb1odc739t5 t83 = t82;
                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_src8lmgjh8bsvgp, 259, 17);
                gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
            }
            goto ix70;
ie70:;
            void * t86 = gm_l0;
            gm_t88rcucg1b7uk t87 = gm_l1;
            gm_tdlpv0q69lov1 t88;
            gm_f_union_variants_a7khaqp((void *)&t88, t86, GM_CONV(gm_csguankko4lmhd, t87));
            gm_tdlpv0q69lov1 t89 = t88;
            if (!((((uint8_t *)&t89))[24] != 0)) goto ie85;
            gm_tep2drsl9408b t90 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&t89));
            gm_l5 = t90;
            {
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
                    gm_l6 = t93;
                }
                {
                    for (;;) {
                        uint64_t t95 = gm_l7;
                        uint64_t t96 = gm_l6;
                        uint8_t t97 = (t95 < t96);
                        if (!t97) goto lb94;
                        {
                            uint64_t t100 = gm_l7;
                            uint64_t t101 = t100;
                            uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&t101));
                            gm_l8 = t104;
                            {
                                void * t106 = (void *)((uint8_t *)&F.l3);
                                uint64_t t107 = gm_l8;
                                uint32_t t108 = (uint32_t)t107;
                                gm_taevtln836oa8 t109;
                                memset(&t109, 0, sizeof t109);
                                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)5ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t109 + 8), t108);
                                gm_tftb1odc739t5 t110 = gm_f_push_95uqvlq(t106, &t109);
                                gm_tftb1odc739t5 t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_src8lmgjh8bsvgp, 261, 47);
                                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
                            }
                            goto me102;
mn103:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 261, 17);
me102:;
                        }
lc94:;
                        {
                            uint64_t t114 = gm_l7;
                            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                            uint64_t t117 = r115;
                            gm_l7 = t117;
                        }
                    }
lb94:;
                }
            }
            goto ix85;
ie85:;
            {
                gm_th2lqbo77pvsc t119;
                memset(&t119, 0, sizeof t119);
                uint8_t t120 = 0;
                F.ret = t119;
                goto gm_fret;
            }
ix85:;
ix70:;
        }
        goto me8;
mn67:;
ma15:;
        {
            void * t123 = gm_l4;
            gm_t88rcucg1b7uk t124 = gm_l1;
            void * t125 = gm_l0;
            void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
            void * t127 = (void *)(((uint8_t *)t126) + 88);
            uint32_t t128 = gm_f_ptr_bits_glps5i7(t127);
            gm_tvegkb6ouf3m0 t129;
            gm_f_int_bounds_7npnb0b((void *)&t129, (void *)NULL, cx, NULL, t123, t124, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t130 = t129;
            gm_tngtuiuskn938 t131;
            if (((((uint8_t *)&t130))[96] != 0)) {
                gm_tngtuiuskn938 t132 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t130) + 0));
                t131 = t132;
            } else {
                gm_th2lqbo77pvsc t133;
                memset(&t133, 0, sizeof t133);
                uint8_t t134 = 0;
                F.ret = t133;
                goto gm_fret;
            }
            F.l9 = t131;
        }
        {
            gm_tbci5ospk5nnn t135 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 0));
            F.l10 = t135;
        }
        {
            gm_tbci5ospk5nnn t136 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 48));
            gm_tbci5ospk5nnn t137;
            gm_f_one_08qg1op((void *)&t137, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&F.l11, (void *)NULL, cx, NULL, &t136, &t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_ghuba9i((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t138 = (void *)((uint8_t *)&F.l12);
            gm_tftb1odc739t5 t139 = gm_f_push_33sq608(t138, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
            gm_tftb1odc739t5 t140 = t139;
            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_src8lmgjh8bsvgp, 271, 13);
            gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
        }
        {
            void * t142 = (void *)((uint8_t *)&F.l12);
            gm_tftb1odc739t5 t143 = gm_f_push_33sq608(t142, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
            gm_tftb1odc739t5 t144 = t143;
            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_src8lmgjh8bsvgp, 272, 13);
            gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
        }
        {
            {
                gm_b16_8 t147 = gm_l2;
                gm_l13 = t147;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t149 = gm_l14;
                    uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t151 = (t149 < t150);
                    if (!t151) goto lb148;
                    {
                        uint64_t t154 = gm_l14;
                        uint64_t t155 = (uintptr_t)t154;
                        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t8oufsr65o5ha t159 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t155 * 136));
                        F.l15 = t159;
                        {
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                            if (t162 != ((uint8_t)1ULL)) goto ie161;
                            gm_taevtln836oa8 t163 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)&F.l15) + 8) + 8));
                            F.l16 = t163;
                            uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                            if (t164 != ((uint8_t)7ULL)) goto ie161;
                            gm_tbci5ospk5nnn t165 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l16) + 8) + 0));
                            F.l17 = t165;
                            gm_tbci5ospk5nnn t166 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l16) + 8) + 48));
                            F.l18 = t166;
                            {
                                void * t168 = (void *)((uint8_t *)&F.l12);
                                gm_tftb1odc739t5 t169 = gm_f_push_33sq608(t168, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                gm_tftb1odc739t5 t170 = t169;
                                if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 1, gm_src8lmgjh8bsvgp, 275, 21);
                                gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 1));
                            }
                            {
                                void * t172 = (void *)((uint8_t *)&F.l12);
                                gm_tbci5ospk5nnn t173 = F.l18;
                                gm_tbci5ospk5nnn t174;
                                gm_f_one_08qg1op((void *)&t174, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll152;
                                }
                                gm_tbci5ospk5nnn t175;
                                gm_f_add_420lj6o((void *)&t175, (void *)NULL, cx, NULL, &t173, &t174);
                                if (gm_unw.kind) {
                                    goto gm_ll152;
                                }
                                gm_tftb1odc739t5 t176 = gm_f_push_33sq608(t172, &t175);
                                gm_tftb1odc739t5 t177 = t176;
                                if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_src8lmgjh8bsvgp, 276, 21);
                                gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
                            }
                            goto ix161;
ie161:;
ix161:;
                        }
                        goto me157;
mn158:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 273, 13);
me157:;
                    }
lc148:;
                    {
                        uint64_t t180 = gm_l14;
                        uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                        uint64_t t183 = r181;
                        gm_l14 = t183;
                    }
                }
gm_ll152:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb148; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc148; }
                }
                goto gm_lad;
lb148:;
            }
        }
        {
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_closure t185 = ((gm_closure){GM_PTR_TAG(gm_f_fn_j6p61la), NULL, cx});
            gm_f_sort_by_sbaob8l(t184, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_ghuba9i((void *)&F.l19, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                gm_l20 = t187;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t189 = gm_l21;
                    uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t191 = (t189 < t190);
                    if (!t191) goto lb188;
                    {
                        uint64_t t194 = gm_l21;
                        uint64_t t195 = (uintptr_t)t194;
                        uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_tbci5ospk5nnn t199 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t195 * 48));
                        F.l22 = t199;
                        {
                            uint8_t t202 = 0;
                            uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            uint8_t t204 = ((t203 == ((uint64_t)0ULL)));
                            if (!t204) {
                                uint64_t t205 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                                uint64_t r206; bool o207 = __builtin_sub_overflow(t205, ((uint64_t)1ULL), &r206);
                                if (o207) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 282, 54);
                                uint64_t t208 = r206;
                                uint64_t t209 = (uintptr_t)t208;
                                uint64_t t210 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                                if (t209 >= (uintptr_t)(t210)) gm_trap_index(t209, t210, gm_src8lmgjh8bsvgp, 282, 43);
                                int32_t t211 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t209 * 48)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                uint8_t t212 = (!(t211 == ((int32_t)0LL)));
                                t202 = t212;
                            } else t202 = 1;
                            if (!t202) goto ie201;
                            {
                                void * t214 = (void *)((uint8_t *)&F.l19);
                                gm_tftb1odc739t5 t215 = gm_f_push_33sq608(t214, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                                gm_tftb1odc739t5 t216 = t215;
                                if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_src8lmgjh8bsvgp, 282, 92);
                                gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
                            }
                            goto ix201;
ie201:;
ix201:;
                        }
                        goto me197;
mn198:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 281, 13);
me197:;
                    }
lc188:;
                    {
                        uint64_t t219 = gm_l21;
                        uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                        uint64_t t222 = r220;
                        gm_l21 = t222;
                    }
                }
lb188:;
            }
        }
        {
            {
                gm_l24 = ((uint64_t)1ULL);
            }
            {
                uint64_t t224 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                gm_l23 = t224;
            }
            {
                for (;;) {
                    uint64_t t226 = gm_l24;
                    uint64_t t227 = gm_l23;
                    uint8_t t228 = (t226 < t227);
                    if (!t228) goto lb225;
                    {
                        uint64_t t231 = gm_l24;
                        uint64_t t232 = t231;
                        uint64_t t235 = GM_LD(uint64_t, ((uint8_t *)&t232));
                        gm_l25 = t235;
                        {
                            uint64_t t237 = gm_l25;
                            uint64_t r238; bool o239 = __builtin_sub_overflow(t237, ((uint64_t)1ULL), &r238);
                            if (o239) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 285, 33);
                            uint64_t t240 = r238;
                            uint64_t t241 = (uintptr_t)t240;
                            uint64_t t242 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            if (t241 >= (uintptr_t)(t242)) gm_trap_index(t241, t242, gm_src8lmgjh8bsvgp, 285, 22);
                            gm_tbci5ospk5nnn t243 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t241 * 48));
                            F.l26 = t243;
                        }
                        {
                            uint64_t t244 = gm_l25;
                            uint64_t t245 = (uintptr_t)t244;
                            uint64_t t246 = GM_LD(uintptr_t, (((uint8_t *)&F.l19) + 0) + 8);
                            if (t245 >= (uintptr_t)(t246)) gm_trap_index(t245, t246, gm_src8lmgjh8bsvgp, 286, 22);
                            gm_tbci5ospk5nnn t247 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l19) + 0)) + t245 * 48));
                            F.l27 = t247;
                        }
                        {
                            int32_t t249 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                            uint8_t t250 = (t249 >= ((int32_t)0LL));
                            if (!t250) goto ie248;
                            int32_t t251 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                            uint8_t t252 = (t251 <= ((int32_t)0LL));
                            if (!t252) goto ie248;
                            int32_t t253 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)));
                            uint8_t t254 = (t253 < ((int32_t)0LL));
                            if (!t254) goto ie248;
                            {
                                void * t256 = (void *)((uint8_t *)&F.l3);
                                gm_tngtuiuskn938 t257;
                                memcpy((uint8_t *)&t257 + 0, ((uint8_t *)&F.l26), 48);
                                gm_tbci5ospk5nnn t258 = F.l27;
                                gm_tbci5ospk5nnn t259;
                                gm_f_one_08qg1op((void *)&t259, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll229;
                                }
                                gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t257 + 48)), (void *)NULL, cx, NULL, &t258, &t259);
                                if (gm_unw.kind) {
                                    goto gm_ll229;
                                }
                                gm_taevtln836oa8 t260;
                                GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)7ULL));
                                GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t260 + 8), t257);
                                gm_tftb1odc739t5 t261 = gm_f_push_95uqvlq(t256, &t260);
                                gm_tftb1odc739t5 t262 = t261;
                                if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 1, gm_src8lmgjh8bsvgp, 287, 75);
                                gm_b0_1 t263 = GM_LD(gm_b0_1, (((uint8_t *)&t262) + 1));
                            }
                            goto ix248;
ie248:;
ix248:;
                        }
                        goto me233;
mn234:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 284, 13);
me233:;
                    }
lc225:;
                    {
                        uint64_t t265 = gm_l24;
                        uint64_t r266; bool o267 = __builtin_add_overflow(t265, ((uint64_t)1ULL), &r266);
                        uint64_t t268 = r266;
                        gm_l24 = t268;
                    }
                }
gm_ll229:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb225; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc225; }
                }
                goto gm_lad;
lb225:;
            }
        }
        goto me8;
mn121:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 242, 5);
me8:;
    }
    {
        gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_th2lqbo77pvsc t270;
        GM_ST(uint8_t, (uint8_t *)&t270 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t270 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t270 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t270), t269);
        ((uint8_t *)&t270)[16] = 1;
        uint8_t t271 = 0;
        F.ret = t270;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_alternatives_r0dto2k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_alternatives_r0dto2k F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l2; /* ps */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* q */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)10ULL)) goto ie2;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t8 = gm_l2;
                gm_l4 = t8;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t12 = (t10 < t11);
                    if (!t12) goto lb9;
                    {
                        uint64_t t15 = gm_l5;
                        uint64_t t16 = (uintptr_t)t15;
                        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        void * t18 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t16 * 8));
                        void * t19 = t18;
                        void * t22 = GM_LD(void *, ((uint8_t *)&t19));
                        gm_l6 = t22;
                        {
                            void * t24 = (void *)((uint8_t *)&F.l3);
                            void * t25 = gm_l0;
                            void * t26 = gm_l6;
                            gm_b16_8 t27 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_ll13;
                            }
                            gm_b16_8 t28 = t27;
                            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                            gm_tftb1odc739t5 t30 = gm_f_extend_eht4gub(t24, t29);
                            gm_tftb1odc739t5 t31 = t30;
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_src8lmgjh8bsvgp, 143, 23);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        goto me20;
mn21:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 143, 9);
me20:;
                    }
lc9:;
                    {
                        uint64_t t34 = gm_l5;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        uint64_t t37 = r35;
                        gm_l5 = t37;
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
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t40 = gm_l0;
        void * t41 = gm_l1;
        gm_b16_8 t42 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = 0;
        F.ret = t42;
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
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_check_match_mj577a4 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* pats */
    gm_b16_8 gm_l3; /* guarded */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* p */
    gm_b16_8 gm_l10; /* alts */
    uint8_t gm_l11; /* any_useful */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_th2lqbo77pvsc gm_l21; /* missing */
    gm_toceffb697p0u gm_l22; /* present_only */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* m */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    void * gm_l27; /* p */
    gm_tl874t2atnlh3 gm_l28; /* at */
    GM_STACK_CHECK(&F, 1795u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_furncbs((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l6 = t3;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    uint64_t t11 = gm_l7;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t14 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t12 * 8));
                    gm_t22qobfhrnoif t15;
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(void *, ((uint8_t *)&t15 + 8), t14);
                    gm_t22qobfhrnoif t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l8 = t19;
                    void * t20 = GM_LD(void *, (((uint8_t *)&t16) + 8));
                    gm_l9 = t20;
                    {
                        void * t22 = gm_l0;
                        void * t23 = gm_l9;
                        gm_b16_8 t24 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l10 = t24;
                    }
                    {
                        gm_l11 = ((uint8_t)0);
                    }
                    {
                        {
                            gm_b16_8 t26 = gm_l10;
                            gm_l12 = t26;
                        }
                        {
                            gm_l13 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t28 = gm_l13;
                                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                uint8_t t30 = (t28 < t29);
                                if (!t30) goto lb27;
                                {
                                    uint64_t t33 = gm_l13;
                                    uint64_t t34 = (uintptr_t)t33;
                                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    gm_t8oufsr65o5ha t38 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t34 * 136));
                                    F.l14 = t38;
                                    {
                                        gm_f_list_99597qp((void *)&F.l15, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                    }
                                    {
                                        void * t40 = (void *)((uint8_t *)&F.l15);
                                        gm_tftb1odc739t5 t41 = gm_f_push_h9qsb4d(t40, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l14)));
                                        gm_tftb1odc739t5 t42 = t41;
                                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_src8lmgjh8bsvgp, 57, 13);
                                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                                    }
                                    {
                                        void * t45 = gm_l0;
                                        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                        gm_b16_8 t47 = t46;
                                        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                                        gm_b16_8 t50 = t49;
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                        gm_th2lqbo77pvsc t52;
                                        gm_f_useful_ge1iad5((void *)&t52, (void *)NULL, cx, NULL, t45, t48, t51);
                                        if (gm_unw.kind) {
                                            goto gm_ll31;
                                        }
                                        gm_th2lqbo77pvsc t53 = t52;
                                        uint8_t t54;
                                        if (((((uint8_t *)&t53))[16] != 0)) goto mn56;
                                        t54 = ((uint8_t)0);
                                        goto me55;
mn56:;
                                        t54 = ((uint8_t)1);
                                        goto me55;
mn57:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 58, 16);
me55:;
                                        if (!t54) goto ie44;
                                        {
                                            gm_l11 = ((uint8_t)1);
                                        }
                                        goto ix44;
ie44:;
ix44:;
                                    }
                                    goto me36;
mn37:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 55, 9);
me36:;
                                }
lc27:;
                                {
                                    uint64_t t60 = gm_l13;
                                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                                    uint64_t t63 = r61;
                                    gm_l13 = t63;
                                }
                            }
gm_ll31:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
                            }
                            goto gm_ll8;
lb27:;
                        }
                    }
                    {
                        uint8_t t65 = gm_l11;
                        uint8_t t66 = (uint8_t)!t65;
                        if (!t66) goto ie64;
                        {
                            void * t68 = (void *)((uint8_t *)&F.l5);
                            uint64_t t69 = gm_l8;
                            gm_tftb1odc739t5 t70 = gm_f_push_ucgmlo2(t68, t69);
                            gm_tftb1odc739t5 t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_src8lmgjh8bsvgp, 60, 29);
                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                        }
                        goto ix64;
ie64:;
ix64:;
                    }
                    {
                        uint64_t t74 = gm_l8;
                        uint64_t t75 = (uintptr_t)t74;
                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_src8lmgjh8bsvgp, 61, 16);
                        uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t75 * 1));
                        uint8_t t78 = (uint8_t)!t77;
                        if (!t78) goto ie73;
                        {
                            {
                                gm_b16_8 t81 = gm_l10;
                                gm_l16 = t81;
                            }
                            {
                                gm_l17 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t83 = gm_l17;
                                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    uint8_t t85 = (t83 < t84);
                                    if (!t85) goto lb82;
                                    {
                                        uint64_t t88 = gm_l17;
                                        uint64_t t89 = (uintptr_t)t88;
                                        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                        gm_t8oufsr65o5ha t93 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t89 * 136));
                                        F.l18 = t93;
                                        {
                                            gm_f_list_99597qp((void *)&F.l19, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll86;
                                            }
                                        }
                                        {
                                            void * t95 = (void *)((uint8_t *)&F.l19);
                                            gm_tftb1odc739t5 t96 = gm_f_push_h9qsb4d(t95, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l18)));
                                            gm_tftb1odc739t5 t97 = t96;
                                            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_src8lmgjh8bsvgp, 64, 17);
                                            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                                        }
                                        {
                                            void * t99 = (void *)((uint8_t *)&F.l4);
                                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                                            gm_tftb1odc739t5 t101 = gm_f_push_s94euqb(t99, t100);
                                            gm_tftb1odc739t5 t102 = t101;
                                            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_src8lmgjh8bsvgp, 65, 17);
                                            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
                                        }
                                        goto me91;
mn92:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 62, 13);
me91:;
                                    }
lc82:;
                                    {
                                        uint64_t t105 = gm_l17;
                                        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                                        uint64_t t108 = r106;
                                        gm_l17 = t108;
                                    }
                                }
gm_ll86:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                                }
                                goto gm_ll8;
lb82:;
                            }
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 52, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t110 = gm_l7;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l7 = t113;
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
        gm_f_list_99597qp((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t114 = (void *)((uint8_t *)&F.l20);
        gm_t88rcucg1b7uk t115 = gm_l1;
        gm_t8oufsr65o5ha t116;
        memset(&t116, 0, sizeof t116);
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t116 + 8), t115);
        gm_tftb1odc739t5 t117 = gm_f_push_h9qsb4d(t114, &t116);
        gm_tftb1odc739t5 t118 = t117;
        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_src8lmgjh8bsvgp, 70, 5);
        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
    }
    {
        gm_th2lqbo77pvsc t120;
        memset(&t120, 0, sizeof t120);
        gm_l21 = t120;
    }
    {
        gm_toceffb697p0u t121;
        memset(&t121, 0, sizeof t121);
        gm_l22 = t121;
    }
    {
        void * t123 = gm_l0;
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t125 = t124;
        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
        gm_b16_8 t128 = t127;
        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
        gm_th2lqbo77pvsc t130;
        gm_f_useful_ge1iad5((void *)&t130, (void *)NULL, cx, NULL, t123, t126, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t131 = t130;
        if (!((((uint8_t *)&t131))[16] != 0)) goto ie122;
        gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
        gm_l23 = t132;
        {
            void * t134 = gm_l0;
            uint64_t t135 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_src8lmgjh8bsvgp, 74, 31);
            void * t137 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t135 * 136);
            gm_b16_8 t138 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t134, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t138;
        }
        {
            gm_b16_8 t139 = gm_l24;
            gm_th2lqbo77pvsc t140;
            GM_ST(uint8_t, (uint8_t *)&t140 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t140 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t140 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t140), t139);
            ((uint8_t *)&t140)[16] = 1;
            gm_l21 = t140;
        }
        {
            gm_b16_8 t142 = gm_l24;
            gm_b16_8 t143;
            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(4));
            uint8_t t144 = gm_f_has_word_n4uc7oo(t142, t143);
            if (!t144) goto ie141;
            {
                {
                    gm_b16_8 t147 = gm_l2;
                    gm_l25 = t147;
                }
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t149 = gm_l26;
                        uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint8_t t151 = (t149 < t150);
                        if (!t151) goto lb148;
                        {
                            uint64_t t154 = gm_l26;
                            uint64_t t155 = (uintptr_t)t154;
                            uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                            void * t157 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t155 * 8));
                            void * t158 = t157;
                            void * t161 = GM_LD(void *, ((uint8_t *)&t158));
                            gm_l27 = t161;
                            {
                                void * t164 = gm_l27;
                                gm_toceffb697p0u t165 = gm_f_nested_present_binding_r48665i(t164, ((uint8_t)1));
                                gm_toceffb697p0u t166 = t165;
                                if (!((((uint8_t *)&t166))[12] != 0)) goto ie163;
                                gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t166));
                                gm_l28 = t167;
                                {
                                    gm_tl874t2atnlh3 t169 = gm_l28;
                                    gm_toceffb697p0u t170;
                                    GM_ST(uint8_t, (uint8_t *)&t170 + 13, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t170 + 14, 0);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t170), t169);
                                    ((uint8_t *)&t170)[12] = 1;
                                    gm_l22 = t170;
                                }
                                {
                                    goto lb148;
                                }
                                goto ix163;
ie163:;
ix163:;
                            }
                            goto me159;
mn160:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 77, 13);
me159:;
                        }
lc148:;
                        {
                            uint64_t t172 = gm_l26;
                            uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
                            uint64_t t175 = r173;
                            gm_l26 = t175;
                        }
                    }
lb148:;
                }
            }
            goto ix141;
ie141:;
ix141:;
        }
        goto ix122;
ie122:;
ix122:;
    }
    {
        memcpy((uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 0, ((uint8_t *)&gm_l21), 24);
        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 24, t176);
        gm_toceffb697p0u t177 = gm_l22;
        GM_ST(gm_toceffb697p0u, (uint8_t *)&(*(gm_tqs4ilbobv1qq *)gm_out) + 40, t177);
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
inline __attribute__((aligned(4))) uint8_t gm_f_column_covers_gn61qhr(gm_b16_8 a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_column_covers_gn61qhr F;
    gm_b16_8 gm_l0; /* column */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 383u);
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
                    gm_t8oufsr65o5ha t15 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 136));
                    F.l4 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        gm_taevtln836oa8 t19 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)&F.l4) + 8) + 8));
                        F.l5 = t19;
                        void * t20 = (void *)((uint8_t *)&F.l5);
                        void * t21 = gm_l1;
                        uint8_t t22 = gm_f_covers_59m49eg(t20, t21);
                        if (!t22) goto ie17;
                        {
                            uint8_t t24 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 434, 5);
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
lb4:;
        }
    }
    {
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_covers_59m49eg(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_covers_59m49eg F;
    void * gm_l0; /* head */
    void * gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)7ULL)) goto ie2;
        gm_tbci5ospk5nnn t5 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t3) + 8) + 0));
        F.l2 = t5;
        gm_tbci5ospk5nnn t6 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t3) + 8) + 48));
        F.l3 = t6;
        void * t7 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 0));
        if (t8 != ((uint8_t)7ULL)) goto ie2;
        gm_tbci5ospk5nnn t9 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t7) + 8) + 0));
        F.l4 = t9;
        gm_tbci5ospk5nnn t10 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t7) + 8) + 48));
        F.l5 = t10;
        {
            uint8_t t12 = 0;
            int32_t t13 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
            uint8_t t14 = (t13 <= ((int32_t)0LL));
            if (t14) {
                int32_t t15 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                uint8_t t16 = (t15 <= ((int32_t)0LL));
                t12 = t16;
            } else t12 = 0;
            uint8_t t17 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        uint8_t t20 = gm_f_ctor_eq_fhbfduc(t18, t19);
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ctor_eq_fhbfduc(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ctor_eq_fhbfduc F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* x */
    uint32_t gm_l3; /* x */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t1) + 8));
    gm_l2 = t17;
    void * t18 = gm_l1;
    uint8_t t19 = gm_l2;
    uint8_t t20 = (((GM_LD(uint8_t, ((uint8_t *)t18) + 0) == ((uint8_t)0ULL)) && ((GM_LD(uint8_t, (((uint8_t *)t18) + 8)) != 0) == (t19 != 0))));
    t2 = t20;
    goto me3;
mn15:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    void * t23 = gm_l1;
    uint8_t t24 = ((GM_LD(uint8_t, ((uint8_t *)t23) + 0) == ((uint8_t)1ULL)));
    t2 = t24;
    goto me3;
mn21:;
ma6:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    void * t27 = gm_l1;
    uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)t27) + 0) == ((uint8_t)2ULL)));
    t2 = t28;
    goto me3;
mn25:;
ma7:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t30 != ((uint8_t)3ULL)) goto mn29;
    void * t31 = gm_l1;
    uint8_t t32 = ((GM_LD(uint8_t, ((uint8_t *)t31) + 0) == ((uint8_t)3ULL)));
    t2 = t32;
    goto me3;
mn29:;
ma8:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t34 != ((uint8_t)4ULL)) goto mn33;
    void * t35 = gm_l1;
    uint8_t t36 = ((GM_LD(uint8_t, ((uint8_t *)t35) + 0) == ((uint8_t)4ULL)));
    t2 = t36;
    goto me3;
mn33:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t38 != ((uint8_t)5ULL)) goto mn37;
    uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)t1) + 8));
    gm_l3 = t39;
    void * t40 = gm_l1;
    uint32_t t41 = gm_l3;
    uint8_t t42 = (((GM_LD(uint8_t, ((uint8_t *)t40) + 0) == ((uint8_t)5ULL)) && (GM_LD(uint32_t, (((uint8_t *)t40) + 8)) == t41)));
    t2 = t42;
    goto me3;
mn37:;
ma10:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t44 != ((uint8_t)6ULL)) goto mn43;
    void * t45 = gm_l1;
    uint8_t t46 = ((GM_LD(uint8_t, ((uint8_t *)t45) + 0) == ((uint8_t)6ULL)));
    t2 = t46;
    goto me3;
mn43:;
ma11:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t48 != ((uint8_t)7ULL)) goto mn47;
    gm_tbci5ospk5nnn t49 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t1) + 8) + 0));
    F.l4 = t49;
    gm_tbci5ospk5nnn t50 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t1) + 8) + 48));
    F.l5 = t50;
    uint8_t t51;
    void * t53 = gm_l1;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + 0));
    if (t54 != ((uint8_t)7ULL)) goto ie52;
    gm_tbci5ospk5nnn t55 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t53) + 8) + 0));
    F.l6 = t55;
    gm_tbci5ospk5nnn t56 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t53) + 8) + 48));
    F.l7 = t56;
    uint8_t t58 = 0;
    uint8_t t59 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
    if (t59) {
        uint8_t t60 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
        t58 = t60;
    } else t58 = 0;
    uint8_t t61 = t58;
    t51 = t61;
    goto ix52;
ie52:;
    uint8_t t63 = ((uint8_t)0);
    t51 = t63;
ix52:;
    t2 = t51;
    goto me3;
mn47:;
ma12:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t65 != ((uint8_t)8ULL)) goto mn64;
    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_l8 = t66;
    uint8_t t67;
    void * t69 = gm_l1;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 0));
    if (t70 != ((uint8_t)8ULL)) goto ie68;
    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 8));
    gm_l9 = t71;
    gm_b16_8 t73 = gm_l8;
    gm_b16_8 t74 = gm_l9;
    gm_b16_8 t75 = t73;
    gm_b16_8 t76 = t74;
    uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
    uint8_t t78 = t77;
    t67 = t78;
    goto ix68;
ie68:;
    uint8_t t80 = ((uint8_t)0);
    t67 = t80;
ix68:;
    t2 = t67;
    goto me3;
mn64:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 322, 58);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_dpat_ty_2ipsk5e(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_j6p61la(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_word_n4uc7oo(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_word_n4uc7oo F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* word */
    uint64_t gm_l2; /* start */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t3;
        }
        {
            uint64_t t4 = gm_l4;
            uint64_t t5 = gm_l3;
            uint8_t t6 = (t4 <= t5);
            gm_l5 = t6;
        }
        {
            for (;;) {
                uint8_t t8 = gm_l5;
                if (!t8) goto lb7;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l6 = t15;
                    {
                        uint64_t t18 = gm_l6;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t20 = (t18 < t19);
                        if (!t20) goto ie17;
                        uint64_t t21 = gm_l6;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src8lmgjh8bsvgp, 92, 42);
                        uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t22 * 1));
                        uint8_t t25 = gm_f_is_name_char_eanps9t(t24);
                        if (!t25) goto ie17;
                        {
                            goto lc7;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                        uint64_t t30 = gm_l2;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = gm_l6;
                        uint64_t t33 = (uintptr_t)t32;
                        if (t31 > t33 || t33 > (uintptr_t)t28) gm_trap_slice(t31, t33, t28, gm_src8lmgjh8bsvgp, 93, 12);
                        gm_b16_8 t34;
                        GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t29 + t31 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t31));
                        gm_b16_8 t35 = gm_l1;
                        gm_b16_8 t36 = t34;
                        gm_b16_8 t37 = t35;
                        uint8_t t38 = (gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
                        if (!t38) goto ie27;
                        {
                            uint8_t t40 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        uint64_t t41 = gm_l6;
                        uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                        if (o43) gm_trap("integer overflow in `+`", gm_src8lmgjh8bsvgp, 94, 17);
                        uint64_t t44 = r42;
                        gm_l2 = t44;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 91, 5);
me13:;
                }
lc7:;
                uint64_t t46 = gm_l4;
                uint64_t t47 = gm_l3;
                uint8_t t48 = ((t46 == t47));
                if (!t48) goto ie45;
                {
                    gm_l5 = ((uint8_t)0);
                }
                goto ix45;
ie45:;
                {
                    uint64_t t51 = gm_l4;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l4 = t54;
                }
ix45:;
            }
lb7:;
        }
    }
    {
        uint8_t t55 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_bounds_7npnb0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2) {
    struct gm_fr_int_bounds_7npnb0b F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* ptr_bits */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 1014u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l5 = t15;
        uint16_t t16 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
        gm_l6 = t16;
        {
            uint8_t t18 = gm_l5;
            gm_l3 = t18;
        }
        {
            uint16_t t19 = gm_l6;
            uint64_t t20 = (uint64_t)t19;
            gm_l4 = t20;
        }
        goto me6;
mn13:;
ma8:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        {
            uint32_t t24 = gm_l2;
            uint64_t t25 = (uint64_t)t24;
            gm_l4 = t25;
        }
        goto me6;
mn21:;
ma9:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        {
            gm_l3 = ((uint8_t)1);
        }
        {
            uint32_t t29 = gm_l2;
            uint64_t t30 = (uint64_t)t29;
            gm_l4 = t30;
        }
        goto me6;
mn26:;
ma10:;
        gm_tvegkb6ouf3m0 t32;
        memset(&t32, 0, sizeof t32);
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me6;
mn31:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 298, 5);
me6:;
    }
    {
        uint8_t t35 = gm_l3;
        if (!t35) goto ie34;
        {
            gm_tbci5ospk5nnn t37;
            gm_f_one_08qg1op((void *)&t37, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t38 = gm_l4;
            uint64_t r39; bool o40 = __builtin_sub_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `-`", gm_src8lmgjh8bsvgp, 311, 34);
            uint64_t t41 = r39;
            gm_f_shl_qqecl81((void *)&F.l7, (void *)NULL, cx, NULL, &t37, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tngtuiuskn938 t42;
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t42 + 0)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t43 = F.l7;
            gm_tbci5ospk5nnn t44;
            gm_f_one_08qg1op((void *)&t44, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t42 + 48)), (void *)NULL, cx, NULL, &t43, &t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 97, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 98, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 100, 0);
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t45), t42);
            ((uint8_t *)&t45)[96] = 1;
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        gm_tngtuiuskn938 t47;
        gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t47 + 0)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t48;
        gm_f_one_08qg1op((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t49 = gm_l4;
        gm_tbci5ospk5nnn t50;
        gm_f_shl_qqecl81((void *)&t50, (void *)NULL, cx, NULL, &t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t51;
        gm_f_one_08qg1op((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_pktm5cr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&t47 + 48)), (void *)NULL, cx, NULL, &t50, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 100, 0);
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t52), t47);
        ((uint8_t *)&t52)[96] = 1;
        uint8_t t53 = 0;
        F.ret = t52;
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
inline __attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_irrefutable_lpkmeii F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 621u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_furncbs((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            void * t4 = gm_l1;
            gm_b16_8 t5 = gm_f_alternatives_r0dto2k((void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t8oufsr65o5ha t17 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 136));
                    F.l5 = t17;
                    {
                        gm_f_list_99597qp((void *)&F.l6, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t19 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t20 = gm_f_push_h9qsb4d(t19, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l5)));
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src8lmgjh8bsvgp, 132, 9);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    {
                        void * t23 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        gm_tftb1odc739t5 t25 = gm_f_push_s94euqb(t23, t24);
                        gm_tftb1odc739t5 t26 = t25;
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src8lmgjh8bsvgp, 133, 9);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 130, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
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
        gm_f_list_99597qp((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l7);
        void * t34 = gm_l1;
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
        gm_t8oufsr65o5ha t36;
        memset(&t36, 0, sizeof t36);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 8), t35);
        gm_tftb1odc739t5 t37 = gm_f_push_h9qsb4d(t33, &t36);
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src8lmgjh8bsvgp, 136, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        void * t40 = gm_l0;
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t42 = t41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_b16_8 t45 = t44;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_th2lqbo77pvsc t47;
        gm_f_useful_ge1iad5((void *)&t47, (void *)NULL, cx, NULL, t40, t43, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t48 = t47;
        uint8_t t49;
        if (((((uint8_t *)&t48))[16] != 0)) goto mn51;
        t49 = ((uint8_t)1);
        goto me50;
mn51:;
        t49 = ((uint8_t)0);
        goto me50;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 137, 12);
me50:;
        uint8_t t53 = 0;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_name_char_eanps9t(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_name_char_eanps9t F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    {
        uint8_t t6 = gm_l0;
        gm_l1 = t6;
    }
    uint8_t t7 = 0;
    uint8_t t8 = gm_l1;
    uint8_t t9 = (((uint8_t)97ULL) <= t8);
    if (t9) {
        uint8_t t10 = gm_l1;
        uint8_t t11 = (t10 <= ((uint8_t)122ULL));
        t7 = t11;
    } else t7 = 0;
    uint8_t t12 = t7;
    if (!t12) {
        {
            uint8_t t14 = gm_l0;
            gm_l2 = t14;
        }
        uint8_t t15 = 0;
        uint8_t t16 = gm_l2;
        uint8_t t17 = (((uint8_t)65ULL) <= t16);
        if (t17) {
            uint8_t t18 = gm_l2;
            uint8_t t19 = (t18 <= ((uint8_t)90ULL));
            t15 = t19;
        } else t15 = 0;
        uint8_t t20 = t15;
        t4 = t20;
    } else t4 = 1;
    if (!t4) {
        {
            uint8_t t22 = gm_l0;
            gm_l3 = t22;
        }
        uint8_t t23 = 0;
        uint8_t t24 = gm_l3;
        uint8_t t25 = (((uint8_t)48ULL) <= t24);
        if (t25) {
            uint8_t t26 = gm_l3;
            uint8_t t27 = (t26 <= ((uint8_t)57ULL));
            t23 = t27;
        } else t23 = 0;
        uint8_t t28 = t23;
        t3 = t28;
    } else t3 = 1;
    if (!t3) {
        uint8_t t29 = gm_l0;
        uint8_t t30 = ((t29 == ((uint8_t)95ULL)));
        t2 = t30;
    } else t2 = 1;
    if (!t2) {
        uint8_t t31 = gm_l0;
        uint8_t t32 = (t31 >= ((uint8_t)128ULL));
        t1 = t32;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_product_5gakcnb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_lower_product_5gakcnb F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* no_subs */
    gm_b16_8 gm_l5; /* ps */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    void * gm_l9; /* e */
    uint8_t gm_l10; /* b */
    gm_b16_8 gm_l12; /* b */
    uint8_t gm_l15; /* inclusive */
    void * gm_l17; /* q */
    void * gm_l18; /* q */
    void * gm_l19; /* q */
    uint32_t gm_l20; /* v */
    gm_b16_8 gm_l21; /* subs_tys */
    uint32_t gm_l23; /* v */
    void * gm_l24; /* q */
    gm_b16_8 gm_l25; /* fs */
    gm_b16_8 gm_l26; /* tys */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l34; /* f */
    gm_b16_8 gm_l35; /* options */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l39; /* c */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l46; /* subs */
    GM_STACK_CHECK(&F, 6908u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_f_empty_slice_6rs1n5s();
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l1;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma8; default: goto ma18; }
ma7:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t23 != ((uint8_t)0ULL)) goto pa22;
        goto po21;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        goto po21;
pa24:;
        goto mn20;
po21:;
        void * t26 = (void *)((uint8_t *)&F.l3);
        gm_t88rcucg1b7uk t27 = gm_l2;
        gm_t8oufsr65o5ha t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 8), t27);
        gm_tftb1odc739t5 t29 = gm_f_push_h9qsb4d(t26, &t28);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src8lmgjh8bsvgp, 155, 28);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        goto me6;
mn20:;
ma8:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t33 != ((uint8_t)10ULL)) goto mn32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
        gm_l5 = t34;
        {
            gm_b16_8 t36 = gm_l5;
            gm_l6 = t36;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l7;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l7;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t46 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t44 * 8));
                    void * t47 = t46;
                    void * t50 = GM_LD(void *, ((uint8_t *)&t47));
                    gm_l8 = t50;
                    {
                        void * t52 = (void *)((uint8_t *)&F.l3);
                        void * t53 = gm_l0;
                        void * t54 = gm_l8;
                        gm_b16_8 t55 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_b16_8 t56 = t55;
                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                        gm_tftb1odc739t5 t58 = gm_f_extend_eht4gub(t52, t57);
                        gm_tftb1odc739t5 t59 = t58;
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src8lmgjh8bsvgp, 156, 34);
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 156, 20);
me48:;
                }
lc37:;
                {
                    uint64_t t62 = gm_l7;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l7 = t65;
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
        goto me6;
mn32:;
ma9:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t67 != ((uint8_t)2ULL)) goto mn66;
        void * t68 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l9 = t68;
        void * t69 = gm_l9;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        switch (t76) { case ((uint8_t)0ULL): goto ma72; case ((uint8_t)1ULL): goto ma74; case ((uint8_t)2ULL): goto ma74; case ((uint8_t)3ULL): goto ma74; case ((uint8_t)4ULL): goto ma71; case ((uint8_t)5ULL): goto ma73; case ((uint8_t)6ULL): goto ma74; case ((uint8_t)7ULL): goto ma74; case ((uint8_t)8ULL): goto ma74; case ((uint8_t)9ULL): goto ma74; case ((uint8_t)10ULL): goto ma74; case ((uint8_t)11ULL): goto ma74; case ((uint8_t)12ULL): goto ma74; case ((uint8_t)13ULL): goto ma74; case ((uint8_t)14ULL): goto ma74; case ((uint8_t)15ULL): goto ma74; case ((uint8_t)16ULL): goto ma74; case ((uint8_t)17ULL): goto ma74; case ((uint8_t)18ULL): goto ma74; case ((uint8_t)19ULL): goto ma74; case ((uint8_t)20ULL): goto ma74; case ((uint8_t)21ULL): goto ma74; case ((uint8_t)22ULL): goto ma74; case ((uint8_t)23ULL): goto ma74; case ((uint8_t)24ULL): goto ma74; case ((uint8_t)25ULL): goto ma74; case ((uint8_t)26ULL): goto ma74; case ((uint8_t)27ULL): goto ma74; case ((uint8_t)28ULL): goto ma74; case ((uint8_t)29ULL): goto ma74; case ((uint8_t)30ULL): goto ma74; case ((uint8_t)31ULL): goto ma74; case ((uint8_t)32ULL): goto ma74; case ((uint8_t)33ULL): goto ma74; case ((uint8_t)34ULL): goto ma74; case ((uint8_t)35ULL): goto ma74; case ((uint8_t)36ULL): goto ma74; case ((uint8_t)37ULL): goto ma74; case ((uint8_t)38ULL): goto ma74; case ((uint8_t)39ULL): goto ma74; case ((uint8_t)40ULL): goto ma74; case ((uint8_t)41ULL): goto ma74; case ((uint8_t)42ULL): goto ma74; case ((uint8_t)43ULL): goto ma74; case ((uint8_t)44ULL): goto ma74; case ((uint8_t)45ULL): goto ma74; case ((uint8_t)46ULL): goto ma74; case ((uint8_t)47ULL): goto ma74; case ((uint8_t)48ULL): goto ma74; case ((uint8_t)49ULL): goto ma74; case ((uint8_t)50ULL): goto ma74; case ((uint8_t)51ULL): goto ma74; case ((uint8_t)52ULL): goto ma74; case ((uint8_t)53ULL): goto ma74; case ((uint8_t)54ULL): goto ma74; case ((uint8_t)55ULL): goto ma74; case ((uint8_t)56ULL): goto ma74; case ((uint8_t)57ULL): goto ma74; case ((uint8_t)58ULL): goto ma74; default: goto ma74; }
ma71:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t78 != ((uint8_t)4ULL)) goto mn77;
        uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 8));
        gm_l10 = t79;
        void * t80 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t81;
        memset((uint8_t *)&t81 + 4, 0, 4);
        gm_t88rcucg1b7uk t82 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t81 + 0, t82);
        uint8_t t83 = gm_l10;
        gm_taevtln836oa8 t84;
        memset(&t84, 0, sizeof t84);
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t84 + 8), t83);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t81 + 8, t84);
        gm_b16_8 t85 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t81 + 112, t85);
        gm_t8oufsr65o5ha t86;
        GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t86 + 8), t81);
        gm_tftb1odc739t5 t87 = gm_f_push_h9qsb4d(t80, &t86);
        gm_tftb1odc739t5 t88 = t87;
        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_src8lmgjh8bsvgp, 158, 25);
        gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
        goto me70;
mn77:;
ma72:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t91 != ((uint8_t)0ULL)) goto mn90;
        gm_tbci5ospk5nnn t92 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t69) + 8) + 8));
        F.l11 = t92;
        void * t93 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t94;
        memset((uint8_t *)&t94 + 4, 0, 4);
        gm_t88rcucg1b7uk t95 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t94 + 0, t95);
        gm_tngtuiuskn938 t96;
        memcpy((uint8_t *)&t96 + 0, ((uint8_t *)&F.l11), 48);
        memcpy((uint8_t *)&t96 + 48, ((uint8_t *)&F.l11), 48);
        gm_taevtln836oa8 t97;
        GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t97 + 8), t96);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t94 + 8, t97);
        gm_b16_8 t98 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 112, t98);
        gm_t8oufsr65o5ha t99;
        GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t99 + 8), t94);
        gm_tftb1odc739t5 t100 = gm_f_push_h9qsb4d(t93, &t99);
        gm_tftb1odc739t5 t101 = t100;
        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_src8lmgjh8bsvgp, 159, 24);
        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
        goto me70;
mn90:;
ma73:;
        uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
        if (t104 != ((uint8_t)5ULL)) goto mn103;
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((((uint8_t *)t69) + 8) + 8));
        gm_l12 = t105;
        void * t106 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t107;
        memset((uint8_t *)&t107 + 4, 0, 4);
        gm_t88rcucg1b7uk t108 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t107 + 0, t108);
        gm_b16_8 t109 = gm_l12;
        gm_taevtln836oa8 t110;
        memset(&t110, 0, sizeof t110);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)8ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 8), t109);
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t107 + 8, t110);
        gm_b16_8 t111 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 112, t111);
        gm_t8oufsr65o5ha t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t112 + 8), t107);
        gm_tftb1odc739t5 t113 = gm_f_push_h9qsb4d(t106, &t112);
        gm_tftb1odc739t5 t114 = t113;
        if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_src8lmgjh8bsvgp, 160, 24);
        gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
        goto me70;
mn103:;
ma74:;
        void * t117 = (void *)((uint8_t *)&F.l3);
        gm_t88rcucg1b7uk t118 = gm_l2;
        gm_t8oufsr65o5ha t119;
        memset(&t119, 0, sizeof t119);
        GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t119 + 8), t118);
        gm_tftb1odc739t5 t120 = gm_f_push_h9qsb4d(t117, &t119);
        gm_tftb1odc739t5 t121 = t120;
        if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_src8lmgjh8bsvgp, 161, 18);
        gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        goto me70;
mn116:;
ma75:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 157, 22);
me70:;
        goto me6;
mn66:;
ma10:;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t124 != ((uint8_t)3ULL)) goto mn123;
        gm_tbci5ospk5nnn t125 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 0));
        F.l13 = t125;
        gm_tbci5ospk5nnn t126 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t5) + 8) + 8) + 48));
        F.l14 = t126;
        uint8_t t127 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 8) + 96));
        gm_l15 = t127;
        {
            gm_tbci5ospk5nnn t129;
            uint8_t t131 = gm_l15;
            if (!t131) goto ie130;
            gm_tbci5ospk5nnn t133 = F.l14;
            gm_tbci5ospk5nnn t134 = t133;
            t129 = t134;
            goto ix130;
ie130:;
            gm_tbci5ospk5nnn t136 = F.l14;
            gm_tbci5ospk5nnn t137;
            gm_f_one_08qg1op((void *)&t137, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t138;
            gm_f_sub_pktm5cr((void *)&t138, (void *)NULL, cx, NULL, &t136, &t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t139 = t138;
            t129 = t139;
ix130:;
            F.l16 = t129;
        }
        {
            void * t140 = (void *)((uint8_t *)&F.l3);
            gm_tbq1t03a53gpo t141;
            memset((uint8_t *)&t141 + 4, 0, 4);
            gm_t88rcucg1b7uk t142 = gm_l2;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t141 + 0, t142);
            gm_tngtuiuskn938 t143;
            memcpy((uint8_t *)&t143 + 0, ((uint8_t *)&F.l13), 48);
            memcpy((uint8_t *)&t143 + 48, ((uint8_t *)&F.l16), 48);
            gm_taevtln836oa8 t144;
            GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)7ULL));
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t144 + 8), t143);
            GM_ST(gm_taevtln836oa8, (uint8_t *)&t141 + 8, t144);
            gm_b16_8 t145 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t141 + 112, t145);
            gm_t8oufsr65o5ha t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t146 + 8), t141);
            gm_tftb1odc739t5 t147 = gm_f_push_h9qsb4d(t140, &t146);
            gm_tftb1odc739t5 t148 = t147;
            if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_src8lmgjh8bsvgp, 165, 13);
            gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
        }
        goto me6;
mn123:;
ma11:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t151 != ((uint8_t)4ULL)) goto mn150;
        void * t152 = (void *)((uint8_t *)&F.l3);
        gm_tbq1t03a53gpo t153;
        memset((uint8_t *)&t153 + 4, 0, 4);
        gm_t88rcucg1b7uk t154 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t153 + 0, t154);
        gm_taevtln836oa8 t155;
        memset(&t155, 0, sizeof t155);
        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)1ULL));
        GM_ST(gm_taevtln836oa8, (uint8_t *)&t153 + 8, t155);
        gm_b16_8 t156 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t153 + 112, t156);
        gm_t8oufsr65o5ha t157;
        GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t157 + 8), t153);
        gm_tftb1odc739t5 t158 = gm_f_push_h9qsb4d(t152, &t157);
        gm_tftb1odc739t5 t159 = t158;
        if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 1, gm_src8lmgjh8bsvgp, 167, 18);
        gm_b0_1 t160 = GM_LD(gm_b0_1, (((uint8_t *)&t159) + 1));
        goto me6;
mn150:;
ma12:;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t162 != ((uint8_t)5ULL)) goto mn161;
        void * t163 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l17 = t163;
        gm_t88rcucg1b7uk t164 = gm_l2;
        gm_taevtln836oa8 t165;
        memset(&t165, 0, sizeof t165);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)2ULL));
        void * t166 = gm_l0;
        void * t167 = gm_l17;
        gm_b16_8 t168 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t166, t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t169 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t170 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t164, &t165, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn161:;
ma13:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t172 != ((uint8_t)6ULL)) goto mn171;
        void * t173 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l18 = t173;
        gm_t88rcucg1b7uk t174 = gm_l2;
        gm_taevtln836oa8 t175;
        memset(&t175, 0, sizeof t175);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)3ULL));
        void * t176 = gm_l0;
        void * t177 = gm_l18;
        gm_b16_8 t178 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t176, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t179 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t180 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t174, &t175, t178, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn171:;
ma14:;
        uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t182 != ((uint8_t)7ULL)) goto mn181;
        void * t183 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 8));
        gm_l19 = t183;
        gm_t88rcucg1b7uk t184 = gm_l2;
        gm_taevtln836oa8 t185;
        memset(&t185, 0, sizeof t185);
        GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)4ULL));
        void * t186 = gm_l0;
        void * t187 = gm_l19;
        gm_b16_8 t188 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t189 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t190 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t184, &t185, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn181:;
ma15:;
        uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t192 != ((uint8_t)8ULL)) goto mn191;
        uint32_t t193 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l20 = t193;
        if ((GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8)) != 0)) goto mn191;
        {
            void * t195 = gm_l0;
            gm_t88rcucg1b7uk t196 = gm_l2;
            uint32_t t197 = gm_l20;
            gm_taevtln836oa8 t198;
            memset(&t198, 0, sizeof t198);
            GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)5ULL));
            GM_ST(uint32_t, ((uint8_t *)&t198 + 8), t197);
            gm_taevtln836oa8 t199 = t198;
            void * t200 = (void *)((uint8_t *)&t199);
            gm_b16_8 t201 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t195, t196, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t201;
        }
        {
            gm_f_list_99597qp((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
            uint8_t t204 = (t203 > ((uint64_t)0ULL));
            if (!t204) goto ie202;
            {
                void * t206 = (void *)((uint8_t *)&F.l22);
                uint64_t t207 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                if (t207 >= (uintptr_t)(t208)) gm_trap_index(t207, t208, gm_src8lmgjh8bsvgp, 174, 51);
                gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t207 * 4));
                gm_t8oufsr65o5ha t210;
                memset(&t210, 0, sizeof t210);
                GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t210 + 8), t209);
                gm_tftb1odc739t5 t211 = gm_f_push_h9qsb4d(t206, &t210);
                gm_tftb1odc739t5 t212 = t211;
                if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_src8lmgjh8bsvgp, 174, 35);
                gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
            }
            goto ix202;
ie202:;
ix202:;
        }
        {
            void * t214 = (void *)((uint8_t *)&F.l3);
            gm_tbq1t03a53gpo t215;
            memset((uint8_t *)&t215 + 4, 0, 4);
            gm_t88rcucg1b7uk t216 = gm_l2;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t215 + 0, t216);
            uint32_t t217 = gm_l20;
            gm_taevtln836oa8 t218;
            memset(&t218, 0, sizeof t218);
            GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)5ULL));
            GM_ST(uint32_t, ((uint8_t *)&t218 + 8), t217);
            GM_ST(gm_taevtln836oa8, (uint8_t *)&t215 + 8, t218);
            gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t215 + 112, t219);
            gm_t8oufsr65o5ha t220;
            GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t220 + 8), t215);
            gm_tftb1odc739t5 t221 = gm_f_push_h9qsb4d(t214, &t220);
            gm_tftb1odc739t5 t222 = t221;
            if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_src8lmgjh8bsvgp, 175, 13);
            gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
        }
        goto me6;
mn191:;
ma16:;
        uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t225 != ((uint8_t)8ULL)) goto mn224;
        uint32_t t226 = GM_LD(uint32_t, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l23 = t226;
        if (!(GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8)) != 0)) goto mn224;
        void * t227 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 8));
        gm_l24 = t227;
        gm_t88rcucg1b7uk t228 = gm_l2;
        uint32_t t229 = gm_l23;
        gm_taevtln836oa8 t230;
        memset(&t230, 0, sizeof t230);
        GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)5ULL));
        GM_ST(uint32_t, ((uint8_t *)&t230 + 8), t229);
        void * t231 = gm_l0;
        void * t232 = gm_l24;
        gm_b16_8 t233 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t231, t232);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t234 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t235 = gm_f_wrap_each_2nbqohg((void *)NULL, cx, NULL, t228, &t230, t233, t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me6;
mn224:;
ma17:;
        uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t237 != ((uint8_t)9ULL)) goto mn236;
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((((uint8_t *)t5) + 8) + 8));
        gm_l25 = t238;
        {
            void * t240 = gm_l0;
            gm_t88rcucg1b7uk t241 = gm_l2;
            gm_taevtln836oa8 t242;
            memset(&t242, 0, sizeof t242);
            GM_ST(uint8_t, ((uint8_t *)&t242 + 0), ((uint8_t)6ULL));
            gm_taevtln836oa8 t243 = t242;
            void * t244 = (void *)((uint8_t *)&t243);
            gm_b16_8 t245 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t240, t241, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t245;
        }
        {
            gm_f_list_99597qp((void *)&F.l27, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t247 = gm_l26;
                gm_l28 = t247;
            }
            {
                gm_l29 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t249 = gm_l29;
                    uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    uint8_t t251 = (t249 < t250);
                    if (!t251) goto lb248;
                    {
                        uint64_t t254 = gm_l29;
                        uint64_t t255 = (uintptr_t)t254;
                        uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t255 * 4));
                        gm_l30 = t259;
                        {
                            void * t261 = (void *)((uint8_t *)&F.l27);
                            gm_t88rcucg1b7uk t262 = gm_l30;
                            gm_t8oufsr65o5ha t263;
                            memset(&t263, 0, sizeof t263);
                            GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t263 + 8), t262);
                            gm_tftb1odc739t5 t264 = gm_f_push_h9qsb4d(t261, &t263);
                            gm_tftb1odc739t5 t265 = t264;
                            if (((((uint8_t *)&t265))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t265) + 1, gm_src8lmgjh8bsvgp, 181, 28);
                            gm_b0_1 t266 = GM_LD(gm_b0_1, (((uint8_t *)&t265) + 1));
                        }
                        goto me257;
mn258:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 181, 13);
me257:;
                    }
lc248:;
                    {
                        uint64_t t268 = gm_l29;
                        uint64_t r269; bool o270 = __builtin_add_overflow(t268, ((uint64_t)1ULL), &r269);
                        uint64_t t271 = r269;
                        gm_l29 = t271;
                    }
                }
lb248:;
            }
        }
        {
            gm_f_list_furncbs((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t272 = (void *)((uint8_t *)&F.l31);
            gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
            gm_tftb1odc739t5 t274 = gm_f_push_s94euqb(t272, t273);
            gm_tftb1odc739t5 t275 = t274;
            if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 1, gm_src8lmgjh8bsvgp, 183, 13);
            gm_b0_1 t276 = GM_LD(gm_b0_1, (((uint8_t *)&t275) + 1));
        }
        {
            {
                gm_b16_8 t278 = gm_l25;
                gm_l32 = t278;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t280 = gm_l33;
                    uint64_t t281 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t282 = (t280 < t281);
                    if (!t282) goto lb279;
                    {
                        uint64_t t285 = gm_l33;
                        uint64_t t286 = (uintptr_t)t285;
                        uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_ttggq7ng4kp3p t290 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t286 * 16));
                        gm_l34 = t290;
                        {
                            void * t292 = gm_l0;
                            void * t293 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                            gm_b16_8 t294 = gm_f_lower_product_5gakcnb((void *)NULL, cx, NULL, t292, t293);
                            if (gm_unw.kind) {
                                goto gm_ll283;
                            }
                            gm_l35 = t294;
                        }
                        {
                            gm_f_list_furncbs((void *)&F.l36, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll283;
                            }
                        }
                        {
                            {
                                gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l37 = t296;
                            }
                            {
                                gm_l38 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t298 = gm_l38;
                                    uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                    uint8_t t300 = (t298 < t299);
                                    if (!t300) goto lb297;
                                    {
                                        uint64_t t303 = gm_l38;
                                        uint64_t t304 = (uintptr_t)t303;
                                        uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                                        gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t304 * 16));
                                        gm_l39 = t308;
                                        {
                                            {
                                                gm_b16_8 t311 = gm_l35;
                                                gm_l40 = t311;
                                            }
                                            {
                                                gm_l41 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t313 = gm_l41;
                                                    uint64_t t314 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                                    uint8_t t315 = (t313 < t314);
                                                    if (!t315) goto lb312;
                                                    {
                                                        uint64_t t318 = gm_l41;
                                                        uint64_t t319 = (uintptr_t)t318;
                                                        uint64_t t320 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                                        gm_t8oufsr65o5ha t323 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t319 * 136));
                                                        F.l42 = t323;
                                                        {
                                                            gm_f_list_99597qp((void *)&F.l43, (void *)NULL, cx, NULL);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll316;
                                                            }
                                                        }
                                                        {
                                                            void * t325 = (void *)((uint8_t *)&F.l43);
                                                            gm_b16_8 t326 = gm_l39;
                                                            gm_b16_8 t327 = t326;
                                                            gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
                                                            gm_tftb1odc739t5 t329 = gm_f_extend_eht4gub(t325, t328);
                                                            gm_tftb1odc739t5 t330 = t329;
                                                            if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_src8lmgjh8bsvgp, 190, 25);
                                                            gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
                                                        }
                                                        {
                                                            uint32_t t333 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                            uint64_t t334 = (uint64_t)t333;
                                                            uint64_t t335 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                                                            uint8_t t336 = (t334 < t335);
                                                            if (!t336) goto ie332;
                                                            {
                                                                uint32_t t338 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 0));
                                                                uint64_t t339 = (uint64_t)t338;
                                                                uint64_t t340 = (uintptr_t)t339;
                                                                uint64_t t341 = GM_LD(uintptr_t, (((uint8_t *)&F.l43) + 0) + 8);
                                                                if (t340 >= (uintptr_t)(t341)) gm_trap_index(t340, t341, gm_src8lmgjh8bsvgp, 191, 60);
                                                                gm_t8oufsr65o5ha t342 = F.l42;
                                                                GM_ST(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l43) + 0)) + t340 * 136), t342);
                                                            }
                                                            goto ix332;
ie332:;
ix332:;
                                                        }
                                                        {
                                                            void * t343 = (void *)((uint8_t *)&F.l36);
                                                            gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                                                            gm_tftb1odc739t5 t345 = gm_f_push_s94euqb(t343, t344);
                                                            gm_tftb1odc739t5 t346 = t345;
                                                            if (((((uint8_t *)&t346))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t346) + 1, gm_src8lmgjh8bsvgp, 192, 25);
                                                            gm_b0_1 t347 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 1));
                                                        }
                                                        goto me321;
mn322:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 188, 21);
me321:;
                                                    }
lc312:;
                                                    {
                                                        uint64_t t349 = gm_l41;
                                                        uint64_t r350; bool o351 = __builtin_add_overflow(t349, ((uint64_t)1ULL), &r350);
                                                        uint64_t t352 = r350;
                                                        gm_l41 = t352;
                                                    }
                                                }
gm_ll316:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb312; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc312; }
                                                }
                                                goto gm_ll301;
lb312:;
                                            }
                                        }
                                        goto me306;
mn307:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 187, 17);
me306:;
                                    }
lc297:;
                                    {
                                        uint64_t t354 = gm_l38;
                                        uint64_t r355; bool o356 = __builtin_add_overflow(t354, ((uint64_t)1ULL), &r355);
                                        uint64_t t357 = r355;
                                        gm_l38 = t357;
                                    }
                                }
gm_ll301:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb297; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc297; }
                                }
                                goto gm_ll283;
lb297:;
                            }
                        }
                        {
                            gm_trqfvmjtt4sij t358 = F.l36;
                            F.l31 = t358;
                        }
                        goto me288;
mn289:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 184, 13);
me288:;
                    }
lc279:;
                    {
                        uint64_t t360 = gm_l33;
                        uint64_t r361; bool o362 = __builtin_add_overflow(t360, ((uint64_t)1ULL), &r361);
                        uint64_t t363 = r361;
                        gm_l33 = t363;
                    }
                }
gm_ll283:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb279; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc279; }
                }
                goto gm_lad;
lb279:;
            }
        }
        {
            {
                gm_b16_8 t365 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                gm_l44 = t365;
            }
            {
                gm_l45 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t367 = gm_l45;
                    uint64_t t368 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    uint8_t t369 = (t367 < t368);
                    if (!t369) goto lb366;
                    {
                        uint64_t t372 = gm_l45;
                        uint64_t t373 = (uintptr_t)t372;
                        uint64_t t374 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t373 * 16));
                        gm_l46 = t377;
                        {
                            void * t379 = (void *)((uint8_t *)&F.l3);
                            gm_tbq1t03a53gpo t380;
                            memset((uint8_t *)&t380 + 4, 0, 4);
                            gm_t88rcucg1b7uk t381 = gm_l2;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t380 + 0, t381);
                            gm_taevtln836oa8 t382;
                            memset(&t382, 0, sizeof t382);
                            GM_ST(uint8_t, ((uint8_t *)&t382 + 0), ((uint8_t)6ULL));
                            GM_ST(gm_taevtln836oa8, (uint8_t *)&t380 + 8, t382);
                            gm_b16_8 t383 = gm_l46;
                            GM_ST(gm_b16_8, (uint8_t *)&t380 + 112, t383);
                            gm_t8oufsr65o5ha t384;
                            GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t384 + 8), t380);
                            gm_tftb1odc739t5 t385 = gm_f_push_h9qsb4d(t379, &t384);
                            gm_tftb1odc739t5 t386 = t385;
                            if (((((uint8_t *)&t386))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t386) + 1, gm_src8lmgjh8bsvgp, 197, 40);
                            gm_b0_1 t387 = GM_LD(gm_b0_1, (((uint8_t *)&t386) + 1));
                        }
                        goto me375;
mn376:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 197, 13);
me375:;
                    }
lc366:;
                    {
                        uint64_t t389 = gm_l45;
                        uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                        uint64_t t392 = r390;
                        gm_l45 = t392;
                    }
                }
lb366:;
            }
        }
        goto me6;
mn236:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 154, 5);
me6:;
    }
    {
        gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t394 = 0;
        F.ret = t393;
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_nested_present_binding_r48665i(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nested_present_binding_r48665i F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* top */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    gm_b16_8 gm_l5; /* alts */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* q */
    gm_tl874t2atnlh3 gm_l9; /* at */
    void * gm_l10; /* q */
    gm_b16_8 gm_l11; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l14; /* f */
    gm_tl874t2atnlh3 gm_l15; /* at */
    GM_STACK_CHECK(&F, 773u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma6; default: goto ma10; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)5ULL)) goto pa15;
        void * t17 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t17;
        goto po14;
pa15:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)6ULL)) goto pa18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t20;
        goto po14;
pa18:;
        goto mn13;
po14:;
        {
            uint8_t t23 = gm_l1;
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto ie22;
            void * t25 = gm_l2;
            uint8_t t26;
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            if (t29 != ((uint8_t)1ULL)) goto mn28;
            t26 = ((uint8_t)1);
            goto me27;
mn28:;
            t26 = ((uint8_t)0);
            goto me27;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 106, 28);
me27:;
            if (!t26) goto ie22;
            {
                void * t32 = gm_l2;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 120));
                gm_toceffb697p0u t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 13, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 14, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t34), t33);
                ((uint8_t *)&t34)[12] = 1;
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            void * t36 = gm_l2;
            uint8_t t37 = gm_l1;
            gm_toceffb697p0u t38 = gm_f_nested_present_binding_r48665i(t36, t37);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto me3;
mn13:;
ma5:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)7ULL)) goto mn40;
        void * t42 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t42;
        void * t43 = gm_l4;
        uint8_t t44 = gm_l1;
        gm_toceffb697p0u t45 = gm_f_nested_present_binding_r48665i(t43, t44);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me3;
mn40:;
ma6:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)10ULL)) goto mn47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t49;
        {
            {
                gm_b16_8 t52 = gm_l5;
                gm_l6 = t52;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l7;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        void * t62 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 8));
                        void * t63 = t62;
                        void * t66 = GM_LD(void *, ((uint8_t *)&t63));
                        gm_l8 = t66;
                        {
                            void * t69 = gm_l8;
                            uint8_t t70 = gm_l1;
                            gm_toceffb697p0u t71 = gm_f_nested_present_binding_r48665i(t69, t70);
                            gm_toceffb697p0u t72 = t71;
                            if (!((((uint8_t *)&t72))[12] != 0)) goto ie68;
                            gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t72));
                            gm_l9 = t73;
                            {
                                gm_tl874t2atnlh3 t75 = gm_l9;
                                gm_toceffb697p0u t76;
                                GM_ST(uint8_t, (uint8_t *)&t76 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t76 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t76), t75);
                                ((uint8_t *)&t76)[12] = 1;
                                uint8_t t77 = 0;
                                F.ret = t76;
                                goto gm_fret;
                            }
                            goto ix68;
ie68:;
ix68:;
                        }
                        goto me64;
mn65:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 111, 13);
me64:;
                    }
lc53:;
                    {
                        uint64_t t79 = gm_l7;
                        uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                        uint64_t t82 = r80;
                        gm_l7 = t82;
                    }
                }
lb53:;
            }
        }
        goto me3;
mn47:;
ma7:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)8ULL)) goto mn83;
        if ((GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn83;
        goto me3;
mn83:;
ma8:;
        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t87 != ((uint8_t)8ULL)) goto mn86;
        if (!(GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn86;
        void * t88 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t88;
        void * t89 = gm_l10;
        gm_toceffb697p0u t90 = gm_f_nested_present_binding_r48665i(t89, ((uint8_t)0));
        uint8_t t91 = 0;
        F.ret = t90;
        goto gm_fret;
        goto me3;
mn86:;
ma9:;
        uint8_t t93 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t93 != ((uint8_t)9ULL)) goto mn92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t94;
        {
            {
                gm_b16_8 t97 = gm_l11;
                gm_l12 = t97;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t99 = gm_l13;
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t101 = (t99 < t100);
                    if (!t101) goto lb98;
                    {
                        uint64_t t104 = gm_l13;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_ttggq7ng4kp3p t109 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t105 * 16));
                        gm_l14 = t109;
                        {
                            void * t112 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                            gm_toceffb697p0u t113 = gm_f_nested_present_binding_r48665i(t112, ((uint8_t)0));
                            gm_toceffb697p0u t114 = t113;
                            if (!((((uint8_t *)&t114))[12] != 0)) goto ie111;
                            gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t114));
                            gm_l15 = t115;
                            {
                                gm_tl874t2atnlh3 t117 = gm_l15;
                                gm_toceffb697p0u t118;
                                GM_ST(uint8_t, (uint8_t *)&t118 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t118 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t118), t117);
                                ((uint8_t *)&t118)[12] = 1;
                                uint8_t t119 = 0;
                                F.ret = t118;
                                goto gm_fret;
                            }
                            goto ix111;
ie111:;
ix111:;
                        }
                        goto me107;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 118, 13);
me107:;
                    }
lc98:;
                    {
                        uint64_t t121 = gm_l13;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        uint64_t t124 = r122;
                        gm_l13 = t124;
                    }
                }
lb98:;
            }
        }
        goto me3;
mn92:;
ma10:;
        goto me3;
mn125:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 104, 5);
me3:;
    }
    {
        gm_toceffb697p0u t127;
        memset(&t127, 0, sizeof t127);
        uint8_t t128 = 0;
        F.ret = t127;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rebuild_90secge(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_rebuild_90secge F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    uint64_t gm_l2; /* arity */
    gm_b16_8 gm_l3; /* w */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l2;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t4 = (t2 < t3 ? t2 : t3);
        gm_l4 = t4;
    }
    {
        gm_f_list_99597qp((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l5);
        gm_tbq1t03a53gpo t6;
        memset((uint8_t *)&t6 + 4, 0, 4);
        gm_t88rcucg1b7uk t7 = gm_l0;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t6 + 0, t7);
        memcpy((uint8_t *)&t6 + 8, ((uint8_t *)&F.l1), 104);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t10 = gm_l4;
        uint64_t t11 = (uintptr_t)t10;
        if ((uintptr_t)0 > t11 || t11 > (uintptr_t)t8) gm_trap_slice((uintptr_t)0, t11, t8, gm_src8lmgjh8bsvgp, 444, 27);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(((uint8_t *)t9 + (uintptr_t)0 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(t11 - (uintptr_t)0));
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 112, t12);
        gm_t8oufsr65o5ha t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t13 + 8), t6);
        gm_tftb1odc739t5 t14 = gm_f_push_h9qsb4d(t5, &t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src8lmgjh8bsvgp, 444, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        void * t17 = (void *)((uint8_t *)&F.l5);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t19 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t20 = gm_l4;
        uint64_t t21 = (uintptr_t)t20;
        if (t21 > (uintptr_t)t18 || (uintptr_t)t18 > (uintptr_t)t18) gm_trap_slice(t21, (uintptr_t)t18, t18, gm_src8lmgjh8bsvgp, 445, 16);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t19 + t21 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)((uintptr_t)t18 - t21));
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_tftb1odc739t5 t25 = gm_f_extend_eht4gub(t17, t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src8lmgjh8bsvgp, 445, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        uint8_t t29 = 0;
        F.ret = t28;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_show_witness_fmsa425(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_witness_fmsa425 F;
    void * gm_l0; /* s */
    void * gm_l1; /* d */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l4; /* subs */
    uint8_t gm_l5; /* b */
    gm_b16_8 gm_l8; /* b */
    uint32_t gm_l10; /* v */
    gm_b16_8 gm_l11; /* name */
    gm_tep2drsl9408b gm_l12; /* uv */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* id */
    gm_b16_8 gm_l19; /* fields */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 2925u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)1ULL)) goto ie2;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t3) + 8) + 0));
        gm_l2 = t5;
        gm_taevtln836oa8 t6 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)t3) + 8) + 8));
        F.l3 = t6;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 112));
        gm_l4 = t7;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            switch (t19) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma15; default: goto ma18; }
ma10:;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t21 != ((uint8_t)0ULL)) goto mn20;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
            gm_l5 = t22;
            gm_b16_8 t23;
            uint8_t t25 = gm_l5;
            if (!t25) goto ie24;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(4));
            gm_b16_8 t28 = t27;
            t23 = t28;
            goto ix24;
ie24:;
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
            gm_b16_8 t31 = t30;
            t23 = t31;
ix24:;
            uint8_t t32 = 0;
            F.ret = t23;
            goto gm_fret;
            goto me9;
mn20:;
ma11:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t34 != ((uint8_t)1ULL)) goto mn33;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_squedv51820an));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(4));
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
            goto me9;
mn33:;
ma12:;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t40 != ((uint8_t)2ULL)) goto pa39;
            goto po38;
pa39:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t42 != ((uint8_t)3ULL)) goto pa41;
            goto po38;
pa41:;
            goto mn37;
po38:;
            void * t43 = gm_l0;
            uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_src8lmgjh8bsvgp, 455, 47);
            void * t46 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t44 * 136);
            gm_b16_8 t47 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t43, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
            goto me9;
mn37:;
ma13:;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t50 != ((uint8_t)4ULL)) goto mn49;
            void * t51 = gm_l0;
            uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_src8lmgjh8bsvgp, 456, 59);
            void * t54 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t52 * 136);
            gm_b16_8 t55 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t51, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t56;
            GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
            gm_tabb710d9mq2e t57;
            gm_f_format_08505jg((void *)&t57, (void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t58 = t57;
            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_src8lmgjh8bsvgp, 456, 24);
            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
            gm_b16_8 t60 = t59;
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            uint8_t t62 = 0;
            F.ret = t61;
            goto gm_fret;
            goto me9;
mn49:;
ma14:;
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t64 != ((uint8_t)7ULL)) goto mn63;
            gm_tbci5ospk5nnn t65 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l3) + 8) + 0));
            F.l6 = t65;
            gm_tbci5ospk5nnn t66 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l3) + 8) + 48));
            F.l7 = t66;
            {
                uint8_t t69 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (!t69) goto ie68;
                {
                    gm_b16_8 t71 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t72 = 0;
                    F.ret = t71;
                    goto gm_fret;
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                gm_t6j6smj7qoctu t73;
                gm_b16_8 t74 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
                gm_b16_8 t75 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t75);
                gm_tabb710d9mq2e t76;
                gm_f_format_hj7ahc4((void *)&t76, (void *)NULL, cx, NULL, &t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t77 = t76;
                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src8lmgjh8bsvgp, 459, 20);
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                gm_b16_8 t79 = t78;
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                uint8_t t81 = 0;
                F.ret = t80;
                goto gm_fret;
            }
            goto me9;
mn63:;
ma15:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t83 != ((uint8_t)8ULL)) goto mn82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_l8 = t84;
            {
                gm_f_list_uvfu1k1((void *)&F.l9, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t86 = gm_l8;
                void * t87 = (void *)((uint8_t *)&F.l9);
                gm_b0_1 t88 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_b16_8 t90 = t89;
                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                uint8_t t92 = 0;
                F.ret = t91;
                goto gm_fret;
            }
            goto me9;
mn82:;
ma16:;
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t94 != ((uint8_t)5ULL)) goto mn93;
            uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
            gm_l10 = t95;
            {
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sfqk3rro4g7t0));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(1));
                gm_l11 = t97;
            }
            {
                void * t99 = gm_l0;
                gm_t88rcucg1b7uk t100 = gm_l2;
                gm_tdlpv0q69lov1 t101;
                gm_f_union_variants_a7khaqp((void *)&t101, t99, GM_CONV(gm_csguankko4lmhd, t100));
                gm_tdlpv0q69lov1 t102 = t101;
                if (!((((uint8_t *)&t102))[24] != 0)) goto ie98;
                gm_tep2drsl9408b t103 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&t102));
                gm_l12 = t103;
                {
                    uint32_t t105 = gm_l10;
                    uint64_t t106 = (uint64_t)t105;
                    uint64_t t107 = (uintptr_t)t106;
                    uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 0) + 8);
                    if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_src8lmgjh8bsvgp, 468, 52);
                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 0)) + t107 * 72) + 0));
                    gm_l11 = t109;
                }
                goto ix98;
ie98:;
ix98:;
            }
            {
                uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t112 = ((t111 == ((uint64_t)0ULL)));
                if (!t112) goto ie110;
                {
                    gm_b16_8 t114 = gm_l11;
                    gm_tnpifv20irnph t115;
                    GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                    gm_tabb710d9mq2e t116;
                    gm_f_format_g0ibdjv((void *)&t116, (void *)NULL, cx, NULL, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t117 = t116;
                    if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src8lmgjh8bsvgp, 469, 39);
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                    gm_b16_8 t119 = t118;
                    gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                    uint8_t t121 = 0;
                    F.ret = t120;
                    goto gm_fret;
                }
                goto ix110;
ie110:;
ix110:;
            }
            {
                gm_t6j6smj7qoctu t122;
                gm_b16_8 t123 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t122 + 0, t123);
                void * t124 = gm_l0;
                uint64_t t125 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t125 >= (uintptr_t)(t126)) gm_trap_index(t125, t126, gm_src8lmgjh8bsvgp, 470, 65);
                void * t127 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t125 * 136);
                gm_b16_8 t128 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t124, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t122 + 16, t128);
                gm_tabb710d9mq2e t129;
                gm_f_format_i83sdhh((void *)&t129, (void *)NULL, cx, NULL, &t122);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t130 = t129;
                if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src8lmgjh8bsvgp, 470, 20);
                gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                gm_b16_8 t132 = t131;
                gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                uint8_t t134 = 0;
                F.ret = t133;
                goto gm_fret;
            }
            goto me9;
mn93:;
ma17:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t136 != ((uint8_t)6ULL)) goto mn135;
            {
                gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_l14 = t139;
                }
                {
                    for (;;) {
                        uint64_t t141 = gm_l15;
                        uint64_t t142 = gm_l14;
                        uint8_t t143 = (t141 < t142);
                        if (!t143) goto lb140;
                        {
                            uint64_t t146 = gm_l15;
                            uint64_t t147 = t146;
                            uint64_t t150 = GM_LD(uint64_t, ((uint8_t *)&t147));
                            gm_l16 = t150;
                            {
                                void * t152 = (void *)((uint8_t *)&F.l13);
                                void * t153 = gm_l0;
                                uint64_t t154 = gm_l16;
                                uint64_t t155 = (uintptr_t)t154;
                                uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_src8lmgjh8bsvgp, 474, 64);
                                void * t157 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t155 * 136);
                                gm_b16_8 t158 = gm_f_show_witness_fmsa425((void *)NULL, cx, NULL, t153, t157);
                                if (gm_unw.kind) {
                                    goto gm_ll144;
                                }
                                gm_tftb1odc739t5 t159 = gm_f_push_3ef746b(t152, t158);
                                gm_tftb1odc739t5 t160 = t159;
                                if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_src8lmgjh8bsvgp, 474, 36);
                                gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
                            }
                            goto me148;
mn149:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 474, 13);
me148:;
                        }
lc140:;
                        {
                            uint64_t t163 = gm_l15;
                            uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                            uint64_t t166 = r164;
                            gm_l15 = t166;
                        }
                    }
gm_ll144:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb140; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc140; }
                    }
                    goto gm_lad;
lb140:;
                }
            }
            {
                void * t168 = gm_l0;
                void * t169 = (void *)(((uint8_t *)t168) + 8);
                gm_t88rcucg1b7uk t170 = gm_l2;
                gm_t53at80pfdus3 t171;
                gm_f_kind_jcnt4hm((void *)&t171, t169, GM_CONV(gm_csguankko4lmhd, t170));
                gm_t53at80pfdus3 t172 = t171;
                uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&t172) + 0));
                if (t173 != ((uint8_t)17ULL)) goto ie167;
                uint32_t t174 = GM_LD(uint32_t, ((((uint8_t *)&t172) + 8) + 0));
                gm_l17 = t174;
                {
                    gm_f_list_epvducg((void *)&F.l18, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t177 = gm_l0;
                    gm_t88rcucg1b7uk t178 = gm_l2;
                    gm_th2lqbo77pvsc t179;
                    gm_f_struct_fields_chqu3it((void *)&t179, t177, GM_CONV(gm_csguankko4lmhd, t178));
                    gm_th2lqbo77pvsc t180 = t179;
                    if (!((((uint8_t *)&t180))[16] != 0)) goto ie176;
                    gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                    gm_l19 = t181;
                    {
                        {
                            gm_b16_8 t184 = gm_l19;
                            gm_b16_8 t185 = t184;
                            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                            gm_l20 = t186;
                        }
                        {
                            gm_l21 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t188 = gm_l21;
                                uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                uint8_t t190 = (t188 < t189);
                                if (!t190) goto lb187;
                                {
                                    gm_t8ta482bl6be0 t193;
                                    uint64_t t194 = gm_l21;
                                    GM_ST(uint64_t, (uint8_t *)&t193 + 0, t194);
                                    uint64_t t195 = gm_l21;
                                    uint64_t t196 = (uintptr_t)t195;
                                    uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    memcpy((uint8_t *)&t193 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t196 * 40), 40);
                                    gm_t8ta482bl6be0 t198 = t193;
                                    uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&t198) + 0));
                                    gm_l22 = t201;
                                    gm_t0mglksrkfa5e t202 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t198) + 8));
                                    F.l23 = t202;
                                    {
                                        uint64_t t205 = gm_l22;
                                        uint64_t t206 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                        uint8_t t207 = (t205 < t206);
                                        if (!t207) goto ie204;
                                        uint64_t t208 = gm_l22;
                                        uint64_t t209 = (uintptr_t)t208;
                                        uint64_t t210 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                        if (t209 >= (uintptr_t)(t210)) gm_trap_index(t209, t210, gm_src8lmgjh8bsvgp, 479, 52);
                                        uint8_t t211 = (!((GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t209 * 16) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t209 * 16)), gm_sfdbfrv46lihd, 1) == 0));
                                        if (!t211) goto ie204;
                                        {
                                            void * t213 = (void *)((uint8_t *)&F.l18);
                                            gm_t6j6smj7qoctu t214;
                                            gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t214 + 0, t215);
                                            uint64_t t216 = gm_l22;
                                            uint64_t t217 = (uintptr_t)t216;
                                            uint64_t t218 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t217 >= (uintptr_t)(t218)) gm_trap_index(t217, t218, gm_src8lmgjh8bsvgp, 479, 120);
                                            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t217 * 16));
                                            GM_ST(gm_b16_8, (uint8_t *)&t214 + 16, t219);
                                            gm_tabb710d9mq2e t220;
                                            gm_f_format_f6rro2t((void *)&t220, (void *)NULL, cx, NULL, &t214);
                                            if (gm_unw.kind) {
                                                goto gm_ll191;
                                            }
                                            gm_tabb710d9mq2e t221 = t220;
                                            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_src8lmgjh8bsvgp, 479, 91);
                                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
                                            gm_b16_8 t223 = t222;
                                            gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                                            gm_tftb1odc739t5 t225 = gm_f_push_3ef746b(t213, t224);
                                            gm_tftb1odc739t5 t226 = t225;
                                            if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 1, gm_src8lmgjh8bsvgp, 479, 80);
                                            gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t226) + 1));
                                        }
                                        goto ix204;
ie204:;
ix204:;
                                    }
                                    goto me199;
mn200:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 478, 21);
me199:;
                                }
lc187:;
                                {
                                    uint64_t t229 = gm_l21;
                                    uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                    uint64_t t232 = r230;
                                    gm_l21 = t232;
                                }
                            }
gm_ll191:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb187; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc187; }
                            }
                            goto gm_lad;
lb187:;
                        }
                    }
                    goto ix176;
ie176:;
ix176:;
                }
                {
                    gm_t6j6smj7qoctu t233;
                    uint32_t t234 = gm_l17;
                    uint64_t t235 = (uint64_t)t234;
                    uint64_t t236 = (uintptr_t)t235;
                    void * t237 = gm_l0;
                    uint64_t t238 = GM_LD(uintptr_t, ((((uint8_t *)t237) + 1088) + 0) + 8);
                    if (t236 >= (uintptr_t)(t238)) gm_trap_index(t236, t238, gm_src8lmgjh8bsvgp, 482, 45);
                    gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t237) + 1088) + 0)) + t236 * 184) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t233 + 0, t239);
                    gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                    gm_b16_8 t241 = t240;
                    gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                    gm_b16_8 t243;
                    GM_ST(void *, (uint8_t *)&t243, (void *)(gm_sg5gdnrith2i6));
                    GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(2));
                    gm_tabb710d9mq2e t244;
                    gm_f_join_sfknljb((void *)&t244, (void *)NULL, cx, NULL, t242, t243);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t245 = t244;
                    if (((((uint8_t *)&t245))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t245) + 8, gm_src8lmgjh8bsvgp, 482, 75);
                    gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&t245) + 8));
                    GM_ST(gm_b16_8, (uint8_t *)&t233 + 16, t246);
                    gm_tabb710d9mq2e t247;
                    gm_f_format_99gc605((void *)&t247, (void *)NULL, cx, NULL, &t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t248 = t247;
                    if (((((uint8_t *)&t248))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t248) + 8, gm_src8lmgjh8bsvgp, 482, 24);
                    gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&t248) + 8));
                    gm_b16_8 t250 = t249;
                    gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                    uint8_t t252 = 0;
                    F.ret = t251;
                    goto gm_fret;
                }
                goto ix167;
ie167:;
ix167:;
            }
            {
                gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t254 = t253;
                gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                gm_b16_8 t256;
                GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t257;
                gm_f_join_sfknljb((void *)&t257, (void *)NULL, cx, NULL, t255, t256);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t258 = t257;
                if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 8, gm_src8lmgjh8bsvgp, 484, 39);
                gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
                gm_tnpifv20irnph t260;
                GM_ST(gm_b16_8, ((uint8_t *)&t260 + 0), t259);
                gm_tabb710d9mq2e t261;
                gm_f_format_0gv6kss((void *)&t261, (void *)NULL, cx, NULL, t260);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t262 = t261;
                if (((((uint8_t *)&t262))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t262) + 8, gm_src8lmgjh8bsvgp, 484, 20);
                gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t262) + 8));
                gm_b16_8 t264 = t263;
                gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                uint8_t t266 = 0;
                F.ret = t265;
                goto gm_fret;
            }
            goto me9;
mn135:;
ma18:;
            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 452, 5);
me9:;
        }
        goto ix2;
ie2:;
        {
            gm_b16_8 t268;
            GM_ST(void *, (uint8_t *)&t268, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(1));
            uint8_t t269 = 0;
            F.ret = t268;
            goto gm_fret;
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
__attribute__((aligned(4))) void gm_f_specialize_si0k4v5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_specialize_si0k4v5 F;
    gm_b16_8 gm_l0; /* row */
    void * gm_l1; /* c */
    gm_b16_8 gm_l2; /* arity_tys */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_b16_8 gm_l8; /* subs */
    GM_STACK_CHECK(&F, 681u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_99597qp((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_src8lmgjh8bsvgp, 339, 11);
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        {
            gm_b16_8 t14 = gm_l2;
            gm_l4 = t14;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l5;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l5;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t22 * 4));
                    gm_l6 = t26;
                    {
                        void * t28 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t29 = gm_l6;
                        gm_t8oufsr65o5ha t30;
                        memset(&t30, 0, sizeof t30);
                        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 8), t29);
                        gm_tftb1odc739t5 t31 = gm_f_push_h9qsb4d(t28, &t30);
                        gm_tftb1odc739t5 t32 = t31;
                        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src8lmgjh8bsvgp, 340, 42);
                        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 340, 21);
me24:;
                }
lc15:;
                {
                    uint64_t t35 = gm_l5;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    uint64_t t38 = r36;
                    gm_l5 = t38;
                }
            }
lb15:;
        }
        goto me10;
mn11:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        gm_taevtln836oa8 t41 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 8) + 8));
        F.l7 = t41;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 136) + 8) + 112));
        gm_l8 = t42;
        {
            void * t45 = (void *)((uint8_t *)&F.l7);
            void * t46 = gm_l1;
            uint8_t t47 = gm_f_covers_59m49eg(t45, t46);
            uint8_t t48 = (uint8_t)!t47;
            if (!t48) goto ie44;
            {
                gm_th2lqbo77pvsc t50;
                memset(&t50, 0, sizeof t50);
                uint8_t t51 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix44;
ie44:;
ix44:;
        }
        {
            void * t52 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t53 = gm_l8;
            gm_b16_8 t54 = t53;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_tftb1odc739t5 t56 = gm_f_extend_eht4gub(t52, t55);
            gm_tftb1odc739t5 t57 = t56;
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_src8lmgjh8bsvgp, 343, 13);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        goto me10;
mn39:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 339, 5);
me10:;
    }
    {
        void * t59 = (void *)((uint8_t *)&F.l3);
        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t61 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
        if (t62 > (uintptr_t)t60 || (uintptr_t)t60 > (uintptr_t)t60) gm_trap_slice(t62, (uintptr_t)t60, t60, gm_src8lmgjh8bsvgp, 346, 16);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t61 + t62 * 136)));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)t60 - t62));
        gm_tftb1odc739t5 t64 = gm_f_extend_eht4gub(t59, t63);
        gm_tftb1odc739t5 t65 = t64;
        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src8lmgjh8bsvgp, 346, 5);
        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
    }
    {
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_th2lqbo77pvsc t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t68), t67);
        ((uint8_t *)&t68)[16] = 1;
        uint8_t t69 = 0;
        F.ret = t68;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sub_types_o2rpif3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_sub_types_o2rpif3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* c */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* err */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint32_t gm_l12; /* v */
    gm_tep2drsl9408b gm_l13; /* uv */
    gm_t88rcucg1b7uk gm_l14; /* p */
    GM_STACK_CHECK(&F, 821u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma8:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t16 != ((uint8_t)13ULL)) goto mn15;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l4 = t17;
        void * t19 = gm_l2;
        uint8_t t20 = ((GM_LD(uint8_t, ((uint8_t *)t19) + 0) == ((uint8_t)2ULL)));
        if (!t20) goto ie18;
        {
            void * t22 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t23 = gm_l4;
            gm_tftb1odc739t5 t24 = gm_f_push_k3l9nic(t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_tftb1odc739t5 t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src8lmgjh8bsvgp, 215, 42);
            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
        }
        goto ix18;
ie18:;
ix18:;
        goto me7;
mn15:;
ma9:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t28 != ((uint8_t)14ULL)) goto mn27;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l5 = t29;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l6 = t30;
        {
            void * t33 = gm_l2;
            uint8_t t34 = ((GM_LD(uint8_t, ((uint8_t *)t33) + 0) == ((uint8_t)3ULL)));
            if (!t34) goto ie32;
            {
                void * t36 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t37 = gm_l6;
                gm_tftb1odc739t5 t38 = gm_f_push_k3l9nic(t36, GM_CONV(gm_csguankko4lmhd, t37));
                gm_tftb1odc739t5 t39 = t38;
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_src8lmgjh8bsvgp, 217, 28);
                gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t42 = gm_l2;
            uint8_t t43 = ((GM_LD(uint8_t, ((uint8_t *)t42) + 0) == ((uint8_t)4ULL)));
            if (!t43) goto ie41;
            {
                void * t45 = (void *)((uint8_t *)&F.l3);
                gm_t88rcucg1b7uk t46 = gm_l5;
                gm_tftb1odc739t5 t47 = gm_f_push_k3l9nic(t45, GM_CONV(gm_csguankko4lmhd, t46));
                gm_tftb1odc739t5 t48 = t47;
                if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src8lmgjh8bsvgp, 218, 29);
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
            }
            goto ix41;
ie41:;
ix41:;
        }
        goto me7;
mn27:;
ma10:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t51 != ((uint8_t)12ULL)) goto mn50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l7 = t52;
        void * t54 = gm_l2;
        uint8_t t55 = ((GM_LD(uint8_t, ((uint8_t *)t54) + 0) == ((uint8_t)6ULL)));
        if (!t55) goto ie53;
        {
            void * t57 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t58 = gm_l7;
            gm_tftb1odc739t5 t59 = gm_f_extend_lfqasgq(t57, t58);
            gm_tftb1odc739t5 t60 = t59;
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_src8lmgjh8bsvgp, 220, 48);
            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        }
        goto ix53;
ie53:;
ix53:;
        goto me7;
mn50:;
ma11:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t63 != ((uint8_t)17ULL)) goto mn62;
        {
            void * t66 = gm_l2;
            uint8_t t67 = ((GM_LD(uint8_t, ((uint8_t *)t66) + 0) == ((uint8_t)6ULL)));
            if (!t67) goto ie65;
            {
                void * t70 = gm_l0;
                gm_t88rcucg1b7uk t71 = gm_l1;
                gm_th2lqbo77pvsc t72;
                gm_f_struct_fields_chqu3it((void *)&t72, t70, GM_CONV(gm_csguankko4lmhd, t71));
                gm_th2lqbo77pvsc t73 = t72;
                if (!((((uint8_t *)&t73))[16] != 0)) goto ie69;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_l8 = t74;
                {
                    {
                        gm_b16_8 t77 = gm_l8;
                        gm_l9 = t77;
                    }
                    {
                        gm_l10 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t79 = gm_l10;
                            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            uint8_t t81 = (t79 < t80);
                            if (!t81) goto lb78;
                            {
                                uint64_t t84 = gm_l10;
                                uint64_t t85 = (uintptr_t)t84;
                                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                gm_t0mglksrkfa5e t89 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t85 * 40));
                                F.l11 = t89;
                                {
                                    void * t91 = (void *)((uint8_t *)&F.l3);
                                    gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                                    gm_tftb1odc739t5 t93 = gm_f_push_k3l9nic(t91, GM_CONV(gm_csguankko4lmhd, t92));
                                    gm_tftb1odc739t5 t94 = t93;
                                    if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_src8lmgjh8bsvgp, 224, 39);
                                    gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                                }
                                goto me87;
mn88:;
                                gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 224, 21);
me87:;
                            }
lc78:;
                            {
                                uint64_t t97 = gm_l10;
                                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                uint64_t t100 = r98;
                                gm_l10 = t100;
                            }
                        }
lb78:;
                    }
                }
                goto ix69;
ie69:;
ix69:;
            }
            goto ix65;
ie65:;
            void * t102 = gm_l2;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)t102) + 0));
            if (t103 != ((uint8_t)5ULL)) goto ie101;
            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)t102) + 8));
            gm_l12 = t104;
            {
                void * t107 = gm_l0;
                gm_t88rcucg1b7uk t108 = gm_l1;
                gm_tdlpv0q69lov1 t109;
                gm_f_union_variants_a7khaqp((void *)&t109, t107, GM_CONV(gm_csguankko4lmhd, t108));
                gm_tdlpv0q69lov1 t110 = t109;
                if (!((((uint8_t *)&t110))[24] != 0)) goto ie106;
                gm_tep2drsl9408b t111 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&t110));
                gm_l13 = t111;
                {
                    uint32_t t114 = gm_l12;
                    uint64_t t115 = (uint64_t)t114;
                    uint64_t t116 = (uintptr_t)t115;
                    uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)&gm_l13) + 0) + 8);
                    if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_src8lmgjh8bsvgp, 228, 29);
                    if (!(((((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l13) + 0)) + t116 * 72) + 16))[4] != 0)) goto ie113;
                    gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l13) + 0)) + t116 * 72) + 16));
                    gm_l14 = t118;
                    {
                        void * t120 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t121 = gm_l14;
                        gm_tftb1odc739t5 t122 = gm_f_push_k3l9nic(t120, GM_CONV(gm_csguankko4lmhd, t121));
                        gm_tftb1odc739t5 t123 = t122;
                        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_src8lmgjh8bsvgp, 228, 61);
                        gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                    }
                    goto ix113;
ie113:;
ix113:;
                }
                goto ix106;
ie106:;
ix106:;
            }
            goto ix101;
ie101:;
ix101:;
ix65:;
        }
        goto me7;
mn62:;
ma12:;
        goto me7;
mn125:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 214, 5);
me7:;
    }
    {
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t128 = 0;
        F.ret = t127;
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
__attribute__((aligned(4))) void gm_f_useful_ge1iad5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_useful_ge1iad5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* rows */
    gm_b16_8 gm_l2; /* q */
    void * gm_l3; /* head */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l14; /* all */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l23; /* tys */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* r */
    gm_b16_8 gm_l28; /* x */
    gm_b16_8 gm_l29; /* spec_q */
    gm_b16_8 gm_l30; /* w */
    gm_th2lqbo77pvsc gm_l31; /* ctors */
    uint8_t gm_l32; /* complete */
    gm_b16_8 gm_l33; /* cs */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    gm_b16_8 gm_l40; /* tys */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l44; /* r */
    gm_b16_8 gm_l45; /* x */
    gm_b16_8 gm_l46; /* spec_q */
    gm_b16_8 gm_l47; /* w */
    gm_b16_8 gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    gm_b16_8 gm_l51; /* r */
    gm_b16_8 gm_l52; /* w */
    gm_b16_8 gm_l54; /* <tmp> */
    uint64_t gm_l55; /* <tmp> */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    gm_t88rcucg1b7uk gm_l60; /* t */
    GM_STACK_CHECK(&F, 5298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (!t8) goto ie6;
            {
                gm_b16_8 t10 = gm_f_empty_slice_6rs1n5s();
                gm_th2lqbo77pvsc t11;
                GM_ST(uint8_t, (uint8_t *)&t11 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t11 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t11 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t11), t10);
                ((uint8_t *)&t11)[16] = 1;
                uint8_t t12 = 0;
                F.ret = t11;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_th2lqbo77pvsc t13;
            memset(&t13, 0, sizeof t13);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src8lmgjh8bsvgp, 357, 14);
        void * t17 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 136);
        gm_l3 = t17;
    }
    {
        void * t18 = gm_l3;
        gm_t88rcucg1b7uk t19 = GM_CONV(gm_t88rcucg1b7uk, gm_f_dpat_ty_2ipsk5e(t18));
        gm_l4 = t19;
    }
    {
        gm_f_list_99597qp((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t21 = gm_l1;
            gm_l6 = t21;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l7;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l7;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t29 * 16));
                    gm_l8 = t33;
                    {
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t37 = (t36 > ((uint64_t)0ULL));
                        if (!t37) goto ie35;
                        {
                            void * t39 = (void *)((uint8_t *)&F.l5);
                            uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src8lmgjh8bsvgp, 361, 36);
                            gm_tftb1odc739t5 t42 = gm_f_push_h9qsb4d(t39, &(*(gm_t8oufsr65o5ha *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t40 * 136)));
                            gm_tftb1odc739t5 t43 = t42;
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_src8lmgjh8bsvgp, 361, 24);
                            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    goto me31;
mn32:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 360, 5);
me31:;
                }
lc22:;
                {
                    uint64_t t46 = gm_l7;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l7 = t49;
                }
            }
lb22:;
        }
    }
    {
        void * t50 = gm_l3;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t50) + 0));
        if (t53 != ((uint8_t)1ULL)) goto mn52;
        gm_taevtln836oa8 t54 = GM_LD(gm_taevtln836oa8, ((((uint8_t *)t50) + 8) + 8));
        F.l9 = t54;
        {
            gm_f_list_ahp090e((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
            if (t57 != ((uint8_t)7ULL)) goto ie56;
            gm_tbci5ospk5nnn t58 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l9) + 8) + 0));
            F.l11 = t58;
            gm_tbci5ospk5nnn t59 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l9) + 8) + 48));
            F.l12 = t59;
            {
                gm_f_list_99597qp((void *)&F.l13, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t61 = (void *)((uint8_t *)&F.l13);
                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                gm_b16_8 t63 = t62;
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                gm_tftb1odc739t5 t65 = gm_f_extend_eht4gub(t61, t64);
                gm_tftb1odc739t5 t66 = t65;
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_src8lmgjh8bsvgp, 369, 17);
                gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
            }
            {
                void * t68 = (void *)((uint8_t *)&F.l13);
                void * t69 = gm_l3;
                gm_tftb1odc739t5 t70 = gm_f_push_h9qsb4d(t68, &(*(gm_t8oufsr65o5ha *)((uint8_t *)t69)));
                gm_tftb1odc739t5 t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_src8lmgjh8bsvgp, 370, 17);
                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
            }
            {
                void * t73 = gm_l0;
                gm_t88rcucg1b7uk t74 = gm_l4;
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t76 = t75;
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_th2lqbo77pvsc t78;
                gm_f_all_ctors_o7aijfe((void *)&t78, (void *)NULL, cx, NULL, t73, t74, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t79 = t78;
                gm_b16_8 t80;
                if (((((uint8_t *)&t79))[16] != 0)) {
                    gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 0));
                    t80 = t81;
                } else {
                    gm_b16_8 t82 = gm_f_empty_slice_kvdmouk();
                    t80 = t82;
                }
                gm_l14 = t80;
            }
            {
                {
                    gm_b16_8 t84 = gm_l14;
                    gm_l15 = t84;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t86 = gm_l16;
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t88 = (t86 < t87);
                        if (!t88) goto lb85;
                        {
                            uint64_t t91 = gm_l16;
                            uint64_t t92 = (uintptr_t)t91;
                            uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_taevtln836oa8 t96 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t92 * 104));
                            F.l17 = t96;
                            {
                                uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                if (t99 != ((uint8_t)7ULL)) goto ie98;
                                gm_tbci5ospk5nnn t100 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l17) + 8) + 0));
                                F.l18 = t100;
                                gm_tbci5ospk5nnn t101 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)&F.l17) + 8) + 48));
                                F.l19 = t101;
                                int32_t t102 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                                uint8_t t103 = (t102 >= ((int32_t)0LL));
                                if (!t103) goto ie98;
                                int32_t t104 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                                uint8_t t105 = (t104 <= ((int32_t)0LL));
                                if (!t105) goto ie98;
                                {
                                    void * t107 = (void *)((uint8_t *)&F.l10);
                                    gm_tftb1odc739t5 t108 = gm_f_push_95uqvlq(t107, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l17)));
                                    gm_tftb1odc739t5 t109 = t108;
                                    if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_src8lmgjh8bsvgp, 373, 82);
                                    gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                                }
                                goto ix98;
ie98:;
ix98:;
                            }
                            goto me94;
mn95:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 372, 17);
me94:;
                        }
lc85:;
                        {
                            uint64_t t112 = gm_l16;
                            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                            uint64_t t115 = r113;
                            gm_l16 = t115;
                        }
                    }
lb85:;
                }
            }
            goto ix56;
ie56:;
            {
                void * t117 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t118 = gm_f_push_95uqvlq(t117, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l9)));
                gm_tftb1odc739t5 t119 = t118;
                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_src8lmgjh8bsvgp, 376, 17);
                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
            }
ix56:;
        }
        {
            {
                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_l20 = t122;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t124 = gm_l21;
                    uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t126 = (t124 < t125);
                    if (!t126) goto lb123;
                    {
                        uint64_t t129 = gm_l21;
                        uint64_t t130 = (uintptr_t)t129;
                        uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_taevtln836oa8 t134 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t130 * 104));
                        F.l22 = t134;
                        {
                            void * t136 = gm_l0;
                            gm_t88rcucg1b7uk t137 = gm_l4;
                            void * t138 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t139 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t136, t137, t138);
                            if (gm_unw.kind) {
                                goto gm_ll127;
                            }
                            gm_l23 = t139;
                        }
                        {
                            gm_f_list_furncbs((void *)&F.l24, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll127;
                            }
                        }
                        {
                            {
                                gm_b16_8 t141 = gm_l1;
                                gm_l25 = t141;
                            }
                            {
                                gm_l26 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t143 = gm_l26;
                                    uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                    uint8_t t145 = (t143 < t144);
                                    if (!t145) goto lb142;
                                    {
                                        uint64_t t148 = gm_l26;
                                        uint64_t t149 = (uintptr_t)t148;
                                        uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t149 * 16));
                                        gm_l27 = t153;
                                        {
                                            gm_b16_8 t156 = gm_l27;
                                            gm_b16_8 t157 = t156;
                                            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                                            void * t159 = (void *)((uint8_t *)&F.l22);
                                            gm_b16_8 t160 = gm_l23;
                                            gm_b16_8 t161 = t160;
                                            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                            gm_th2lqbo77pvsc t163;
                                            gm_f_specialize_si0k4v5((void *)&t163, (void *)NULL, cx, NULL, t158, t159, t162);
                                            if (gm_unw.kind) {
                                                goto gm_ll146;
                                            }
                                            gm_th2lqbo77pvsc t164 = t163;
                                            if (!((((uint8_t *)&t164))[16] != 0)) goto ie155;
                                            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                                            gm_l28 = t165;
                                            {
                                                void * t167 = (void *)((uint8_t *)&F.l24);
                                                gm_b16_8 t168 = gm_l28;
                                                gm_tftb1odc739t5 t169 = gm_f_push_s94euqb(t167, t168);
                                                gm_tftb1odc739t5 t170 = t169;
                                                if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 1, gm_src8lmgjh8bsvgp, 382, 58);
                                                gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 1));
                                            }
                                            goto ix155;
ie155:;
ix155:;
                                        }
                                        goto me151;
mn152:;
                                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 381, 17);
me151:;
                                    }
lc142:;
                                    {
                                        uint64_t t173 = gm_l26;
                                        uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                                        uint64_t t176 = r174;
                                        gm_l26 = t176;
                                    }
                                }
gm_ll146:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb142; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc142; }
                                }
                                goto gm_ll127;
lb142:;
                            }
                        }
                        {
                            gm_b16_8 t177 = gm_l2;
                            void * t178 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t179 = gm_l23;
                            gm_b16_8 t180 = t179;
                            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                            gm_th2lqbo77pvsc t182;
                            gm_f_specialize_si0k4v5((void *)&t182, (void *)NULL, cx, NULL, t177, t178, t181);
                            if (gm_unw.kind) {
                                goto gm_ll127;
                            }
                            gm_th2lqbo77pvsc t183 = t182;
                            gm_b16_8 t184;
                            if (((((uint8_t *)&t183))[16] != 0)) {
                                gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 0));
                                t184 = t185;
                            } else {
                                gm_th2lqbo77pvsc t186;
                                memset(&t186, 0, sizeof t186);
                                uint8_t t187 = 0;
                                F.ret = t186;
                                goto gm_fret;
                            }
                            gm_l29 = t184;
                        }
                        {
                            void * t189 = gm_l0;
                            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                            gm_b16_8 t191 = t190;
                            gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                            gm_b16_8 t193 = gm_l29;
                            gm_b16_8 t194 = t193;
                            gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
                            gm_th2lqbo77pvsc t196;
                            gm_f_useful_ge1iad5((void *)&t196, (void *)NULL, cx, NULL, t189, t192, t195);
                            if (gm_unw.kind) {
                                goto gm_ll127;
                            }
                            gm_th2lqbo77pvsc t197 = t196;
                            if (!((((uint8_t *)&t197))[16] != 0)) goto ie188;
                            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                            gm_l30 = t198;
                            {
                                gm_t88rcucg1b7uk t200 = gm_l4;
                                uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                gm_b16_8 t202 = gm_l30;
                                gm_b16_8 t203 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t200, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l22)), t201, t202);
                                if (gm_unw.kind) {
                                    goto gm_ll127;
                                }
                                gm_th2lqbo77pvsc t204;
                                GM_ST(uint8_t, (uint8_t *)&t204 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t204 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t204 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t204), t203);
                                ((uint8_t *)&t204)[16] = 1;
                                uint8_t t205 = 0;
                                F.ret = t204;
                                goto gm_fret;
                            }
                            goto ix188;
ie188:;
ix188:;
                        }
                        goto me132;
mn133:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 378, 13);
me132:;
                    }
lc123:;
                    {
                        uint64_t t207 = gm_l21;
                        uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                        uint64_t t210 = r208;
                        gm_l21 = t210;
                    }
                }
gm_ll127:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb123; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc123; }
                }
                goto gm_lad;
lb123:;
            }
        }
        {
            gm_th2lqbo77pvsc t211;
            memset(&t211, 0, sizeof t211);
            uint8_t t212 = 0;
            F.ret = t211;
            goto gm_fret;
        }
        goto me51;
mn52:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)t50) + 0));
        if (t214 != ((uint8_t)0ULL)) goto mn213;
        {
            void * t216 = gm_l0;
            gm_t88rcucg1b7uk t217 = gm_l4;
            gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t219 = t218;
            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
            gm_f_all_ctors_o7aijfe((void *)&gm_l31, (void *)NULL, cx, NULL, t216, t217, t220);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l32 = ((uint8_t)0);
        }
        {
            if (!((((uint8_t *)&gm_l31))[16] != 0)) goto ie221;
            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
            gm_l33 = t222;
            {
                gm_l32 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t225 = gm_l33;
                    gm_l34 = t225;
                }
                {
                    gm_l35 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t227 = gm_l35;
                        uint64_t t228 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        uint8_t t229 = (t227 < t228);
                        if (!t229) goto lb226;
                        {
                            uint64_t t232 = gm_l35;
                            uint64_t t233 = (uintptr_t)t232;
                            uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                            gm_taevtln836oa8 t237 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t233 * 104));
                            F.l36 = t237;
                            {
                                gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                gm_b16_8 t241 = t240;
                                gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                                void * t243 = (void *)((uint8_t *)&F.l36);
                                uint8_t t244 = gm_f_column_covers_gn61qhr(t242, t243);
                                uint8_t t245 = (uint8_t)!t244;
                                if (!t245) goto ie239;
                                {
                                    gm_l32 = ((uint8_t)0);
                                }
                                goto ix239;
ie239:;
ix239:;
                            }
                            goto me235;
mn236:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 394, 17);
me235:;
                        }
lc226:;
                        {
                            uint64_t t248 = gm_l35;
                            uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                            uint64_t t251 = r249;
                            gm_l35 = t251;
                        }
                    }
lb226:;
                }
            }
            goto ix221;
ie221:;
ix221:;
        }
        {
            uint8_t t253 = gm_l32;
            if (!t253) goto ie252;
            {
                {
                    gm_th2lqbo77pvsc t256 = gm_l31;
                    gm_th2lqbo77pvsc t257 = t256;
                    gm_b16_8 t258;
                    if (((((uint8_t *)&t257))[16] != 0)) {
                        gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&t257) + 0));
                        t258 = t259;
                    } else {
                        gm_b16_8 t260 = gm_f_empty_slice_kvdmouk();
                        t258 = t260;
                    }
                    gm_l37 = t258;
                }
                {
                    gm_l38 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t262 = gm_l38;
                        uint64_t t263 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        uint8_t t264 = (t262 < t263);
                        if (!t264) goto lb261;
                        {
                            uint64_t t267 = gm_l38;
                            uint64_t t268 = (uintptr_t)t267;
                            uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            gm_taevtln836oa8 t272 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t268 * 104));
                            F.l39 = t272;
                            {
                                void * t274 = gm_l0;
                                gm_t88rcucg1b7uk t275 = gm_l4;
                                void * t276 = (void *)((uint8_t *)&F.l39);
                                gm_b16_8 t277 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t274, t275, t276);
                                if (gm_unw.kind) {
                                    goto gm_ll265;
                                }
                                gm_l40 = t277;
                            }
                            {
                                gm_f_list_furncbs((void *)&F.l41, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll265;
                                }
                            }
                            {
                                {
                                    gm_b16_8 t279 = gm_l1;
                                    gm_l42 = t279;
                                }
                                {
                                    gm_l43 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t281 = gm_l43;
                                        uint64_t t282 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                        uint8_t t283 = (t281 < t282);
                                        if (!t283) goto lb280;
                                        {
                                            uint64_t t286 = gm_l43;
                                            uint64_t t287 = (uintptr_t)t286;
                                            uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                            gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t287 * 16));
                                            gm_l44 = t291;
                                            {
                                                gm_b16_8 t294 = gm_l44;
                                                gm_b16_8 t295 = t294;
                                                gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
                                                void * t297 = (void *)((uint8_t *)&F.l39);
                                                gm_b16_8 t298 = gm_l40;
                                                gm_b16_8 t299 = t298;
                                                gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                                                gm_th2lqbo77pvsc t301;
                                                gm_f_specialize_si0k4v5((void *)&t301, (void *)NULL, cx, NULL, t296, t297, t300);
                                                if (gm_unw.kind) {
                                                    goto gm_ll284;
                                                }
                                                gm_th2lqbo77pvsc t302 = t301;
                                                if (!((((uint8_t *)&t302))[16] != 0)) goto ie293;
                                                gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                                                gm_l45 = t303;
                                                {
                                                    void * t305 = (void *)((uint8_t *)&F.l41);
                                                    gm_b16_8 t306 = gm_l45;
                                                    gm_tftb1odc739t5 t307 = gm_f_push_s94euqb(t305, t306);
                                                    gm_tftb1odc739t5 t308 = t307;
                                                    if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 1, gm_src8lmgjh8bsvgp, 403, 58);
                                                    gm_b0_1 t309 = GM_LD(gm_b0_1, (((uint8_t *)&t308) + 1));
                                                }
                                                goto ix293;
ie293:;
ix293:;
                                            }
                                            goto me289;
mn290:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 402, 21);
me289:;
                                        }
lc280:;
                                        {
                                            uint64_t t311 = gm_l43;
                                            uint64_t r312; bool o313 = __builtin_add_overflow(t311, ((uint64_t)1ULL), &r312);
                                            uint64_t t314 = r312;
                                            gm_l43 = t314;
                                        }
                                    }
gm_ll284:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb280; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc280; }
                                    }
                                    goto gm_ll265;
lb280:;
                                }
                            }
                            {
                                gm_b16_8 t315 = gm_l2;
                                void * t316 = (void *)((uint8_t *)&F.l39);
                                gm_b16_8 t317 = gm_l40;
                                gm_b16_8 t318 = t317;
                                gm_b16_8 t319 = GM_LD(gm_b16_8, ((uint8_t *)&t318));
                                gm_th2lqbo77pvsc t320;
                                gm_f_specialize_si0k4v5((void *)&t320, (void *)NULL, cx, NULL, t315, t316, t319);
                                if (gm_unw.kind) {
                                    goto gm_ll265;
                                }
                                gm_th2lqbo77pvsc t321 = t320;
                                gm_b16_8 t322;
                                if (((((uint8_t *)&t321))[16] != 0)) {
                                    gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&t321) + 0));
                                    t322 = t323;
                                } else {
                                    gm_th2lqbo77pvsc t324;
                                    memset(&t324, 0, sizeof t324);
                                    uint8_t t325 = 0;
                                    F.ret = t324;
                                    goto gm_fret;
                                }
                                gm_l46 = t322;
                            }
                            {
                                void * t327 = gm_l0;
                                gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
                                gm_b16_8 t329 = t328;
                                gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
                                gm_b16_8 t331 = gm_l46;
                                gm_b16_8 t332 = t331;
                                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
                                gm_th2lqbo77pvsc t334;
                                gm_f_useful_ge1iad5((void *)&t334, (void *)NULL, cx, NULL, t327, t330, t333);
                                if (gm_unw.kind) {
                                    goto gm_ll265;
                                }
                                gm_th2lqbo77pvsc t335 = t334;
                                if (!((((uint8_t *)&t335))[16] != 0)) goto ie326;
                                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                                gm_l47 = t336;
                                {
                                    gm_t88rcucg1b7uk t338 = gm_l4;
                                    uint64_t t339 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    gm_b16_8 t340 = gm_l47;
                                    gm_b16_8 t341 = gm_f_rebuild_90secge((void *)NULL, cx, NULL, t338, &(*(gm_taevtln836oa8 *)((uint8_t *)&F.l39)), t339, t340);
                                    if (gm_unw.kind) {
                                        goto gm_ll265;
                                    }
                                    gm_th2lqbo77pvsc t342;
                                    GM_ST(uint8_t, (uint8_t *)&t342 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t342 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t342 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t342), t341);
                                    ((uint8_t *)&t342)[16] = 1;
                                    uint8_t t343 = 0;
                                    F.ret = t342;
                                    goto gm_fret;
                                }
                                goto ix326;
ie326:;
ix326:;
                            }
                            goto me270;
mn271:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 399, 17);
me270:;
                        }
lc261:;
                        {
                            uint64_t t345 = gm_l38;
                            uint64_t r346; bool o347 = __builtin_add_overflow(t345, ((uint64_t)1ULL), &r346);
                            uint64_t t348 = r346;
                            gm_l38 = t348;
                        }
                    }
gm_ll265:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb261; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc261; }
                    }
                    goto gm_lad;
lb261:;
                }
            }
            {
                gm_th2lqbo77pvsc t349;
                memset(&t349, 0, sizeof t349);
                uint8_t t350 = 0;
                F.ret = t349;
                goto gm_fret;
            }
            goto ix252;
ie252:;
ix252:;
        }
        {
            gm_f_list_furncbs((void *)&F.l48, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t352 = gm_l1;
                gm_l49 = t352;
            }
            {
                gm_l50 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t354 = gm_l50;
                    uint64_t t355 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                    uint8_t t356 = (t354 < t355);
                    if (!t356) goto lb353;
                    {
                        uint64_t t359 = gm_l50;
                        uint64_t t360 = (uintptr_t)t359;
                        uint64_t t361 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                        gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t360 * 16));
                        gm_l51 = t364;
                        {
                            uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                            uint8_t t368 = (t367 > ((uint64_t)0ULL));
                            if (!t368) goto ie366;
                            uint64_t t369 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t370 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                            if (t369 >= (uintptr_t)(t370)) gm_trap_index(t369, t370, gm_src8lmgjh8bsvgp, 412, 34);
                            uint8_t t371;
                            uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t369 * 136) + 0));
                            if (t374 != ((uint8_t)0ULL)) goto mn373;
                            t371 = ((uint8_t)1);
                            goto me372;
mn373:;
                            t371 = ((uint8_t)0);
                            goto me372;
mn375:;
                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 412, 34);
me372:;
                            if (!t371) goto ie366;
                            {
                                void * t377 = (void *)((uint8_t *)&F.l48);
                                uint64_t t378 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                                void * t379 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)));
                                uint64_t t380 = (uintptr_t)((uint64_t)1ULL);
                                if (t380 > (uintptr_t)t378 || (uintptr_t)t378 > (uintptr_t)t378) gm_trap_slice(t380, (uintptr_t)t378, t378, gm_src8lmgjh8bsvgp, 412, 66);
                                gm_b16_8 t381;
                                GM_ST(void *, (uint8_t *)&t381, (void *)(((uint8_t *)t379 + t380 * 136)));
                                GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)((uintptr_t)t378 - t380));
                                gm_tftb1odc739t5 t382 = gm_f_push_s94euqb(t377, t381);
                                gm_tftb1odc739t5 t383 = t382;
                                if (((((uint8_t *)&t383))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t383) + 1, gm_src8lmgjh8bsvgp, 412, 53);
                                gm_b0_1 t384 = GM_LD(gm_b0_1, (((uint8_t *)&t383) + 1));
                            }
                            goto ix366;
ie366:;
ix366:;
                        }
                        goto me362;
mn363:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 411, 13);
me362:;
                    }
lc353:;
                    {
                        uint64_t t386 = gm_l50;
                        uint64_t r387; bool o388 = __builtin_add_overflow(t386, ((uint64_t)1ULL), &r387);
                        uint64_t t389 = r387;
                        gm_l50 = t389;
                    }
                }
lb353:;
            }
        }
        {
            void * t390 = gm_l0;
            gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
            gm_b16_8 t392 = t391;
            gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
            uint64_t t394 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t395 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t396 = (uintptr_t)((uint64_t)1ULL);
            if (t396 > (uintptr_t)t394 || (uintptr_t)t394 > (uintptr_t)t394) gm_trap_slice(t396, (uintptr_t)t394, t394, gm_src8lmgjh8bsvgp, 414, 43);
            gm_b16_8 t397;
            GM_ST(void *, (uint8_t *)&t397, (void *)(((uint8_t *)t395 + t396 * 136)));
            GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)((uintptr_t)t394 - t396));
            gm_th2lqbo77pvsc t398;
            gm_f_useful_ge1iad5((void *)&t398, (void *)NULL, cx, NULL, t390, t393, t397);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t399 = t398;
            gm_b16_8 t400;
            if (((((uint8_t *)&t399))[16] != 0)) {
                gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&t399) + 0));
                t400 = t401;
            } else {
                gm_th2lqbo77pvsc t402;
                memset(&t402, 0, sizeof t402);
                uint8_t t403 = 0;
                F.ret = t402;
                goto gm_fret;
            }
            gm_l52 = t400;
        }
        {
            gm_t88rcucg1b7uk t404 = gm_l4;
            gm_t8oufsr65o5ha t405;
            memset(&t405, 0, sizeof t405);
            GM_ST(uint8_t, ((uint8_t *)&t405 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t405 + 8), t404);
            F.l53 = t405;
        }
        {
            {
                gm_th2lqbo77pvsc t407 = gm_l31;
                gm_th2lqbo77pvsc t408 = t407;
                gm_b16_8 t409;
                if (((((uint8_t *)&t408))[16] != 0)) {
                    gm_b16_8 t410 = GM_LD(gm_b16_8, (((uint8_t *)&t408) + 0));
                    t409 = t410;
                } else {
                    gm_b16_8 t411 = gm_f_empty_slice_kvdmouk();
                    t409 = t411;
                }
                gm_l54 = t409;
            }
            {
                gm_l55 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t413 = gm_l55;
                    uint64_t t414 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                    uint8_t t415 = (t413 < t414);
                    if (!t415) goto lb412;
                    {
                        uint64_t t418 = gm_l55;
                        uint64_t t419 = (uintptr_t)t418;
                        uint64_t t420 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                        gm_taevtln836oa8 t423 = GM_LD(gm_taevtln836oa8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l54)) + t419 * 104));
                        F.l56 = t423;
                        {
                            gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                            gm_b16_8 t427 = t426;
                            gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                            void * t429 = (void *)((uint8_t *)&F.l56);
                            uint8_t t430 = gm_f_column_covers_gn61qhr(t428, t429);
                            uint8_t t431 = (uint8_t)!t430;
                            if (!t431) goto ie425;
                            {
                                gm_f_list_99597qp((void *)&F.l57, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll416;
                                }
                            }
                            {
                                {
                                    void * t434 = gm_l0;
                                    gm_t88rcucg1b7uk t435 = gm_l4;
                                    void * t436 = (void *)((uint8_t *)&F.l56);
                                    gm_b16_8 t437 = gm_f_sub_types_o2rpif3((void *)NULL, cx, NULL, t434, t435, t436);
                                    if (gm_unw.kind) {
                                        goto gm_ll416;
                                    }
                                    gm_l58 = t437;
                                }
                                {
                                    gm_l59 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t439 = gm_l59;
                                        uint64_t t440 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                                        uint8_t t441 = (t439 < t440);
                                        if (!t441) goto lb438;
                                        {
                                            uint64_t t444 = gm_l59;
                                            uint64_t t445 = (uintptr_t)t444;
                                            uint64_t t446 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                                            gm_t88rcucg1b7uk t449 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t445 * 4));
                                            gm_l60 = t449;
                                            {
                                                void * t451 = (void *)((uint8_t *)&F.l57);
                                                gm_t88rcucg1b7uk t452 = gm_l60;
                                                gm_t8oufsr65o5ha t453;
                                                memset(&t453, 0, sizeof t453);
                                                GM_ST(uint8_t, ((uint8_t *)&t453 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t453 + 8), t452);
                                                gm_tftb1odc739t5 t454 = gm_f_push_h9qsb4d(t451, &t453);
                                                gm_tftb1odc739t5 t455 = t454;
                                                if (((((uint8_t *)&t455))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t455) + 1, gm_src8lmgjh8bsvgp, 420, 53);
                                                gm_b0_1 t456 = GM_LD(gm_b0_1, (((uint8_t *)&t455) + 1));
                                            }
                                            goto me447;
mn448:;
                                            gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 420, 21);
me447:;
                                        }
lc438:;
                                        {
                                            uint64_t t458 = gm_l59;
                                            uint64_t r459; bool o460 = __builtin_add_overflow(t458, ((uint64_t)1ULL), &r459);
                                            uint64_t t461 = r459;
                                            gm_l59 = t461;
                                        }
                                    }
lb438:;
                                }
                            }
                            {
                                gm_tbq1t03a53gpo t462;
                                memset((uint8_t *)&t462 + 4, 0, 4);
                                gm_t88rcucg1b7uk t463 = gm_l4;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t462 + 0, t463);
                                memcpy((uint8_t *)&t462 + 8, ((uint8_t *)&F.l56), 104);
                                gm_b16_8 t464 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t462 + 112, t464);
                                gm_t8oufsr65o5ha t465;
                                GM_ST(uint8_t, (uint8_t *)&t465 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t465 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t465 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t465 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t465 + 8), t462);
                                F.l53 = t465;
                            }
                            {
                                goto lb412;
                            }
                            goto ix425;
ie425:;
ix425:;
                        }
                        goto me421;
mn422:;
                        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 417, 13);
me421:;
                    }
lc412:;
                    {
                        uint64_t t467 = gm_l55;
                        uint64_t r468; bool o469 = __builtin_add_overflow(t467, ((uint64_t)1ULL), &r468);
                        uint64_t t470 = r468;
                        gm_l55 = t470;
                    }
                }
gm_ll416:;
                if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb412; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc412; }
                }
                goto gm_lad;
lb412:;
            }
        }
        {
            gm_f_list_99597qp((void *)&F.l61, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t471 = (void *)((uint8_t *)&F.l61);
            gm_tftb1odc739t5 t472 = gm_f_push_h9qsb4d(t471, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l53)));
            gm_tftb1odc739t5 t473 = t472;
            if (((((uint8_t *)&t473))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t473) + 1, gm_src8lmgjh8bsvgp, 426, 13);
            gm_b0_1 t474 = GM_LD(gm_b0_1, (((uint8_t *)&t473) + 1));
        }
        {
            void * t475 = (void *)((uint8_t *)&F.l61);
            gm_b16_8 t476 = gm_l52;
            gm_b16_8 t477 = t476;
            gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)&t477));
            gm_tftb1odc739t5 t479 = gm_f_extend_eht4gub(t475, t478);
            gm_tftb1odc739t5 t480 = t479;
            if (((((uint8_t *)&t480))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t480) + 1, gm_src8lmgjh8bsvgp, 427, 13);
            gm_b0_1 t481 = GM_LD(gm_b0_1, (((uint8_t *)&t480) + 1));
        }
        {
            gm_b16_8 t482 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
            gm_th2lqbo77pvsc t483;
            GM_ST(uint8_t, (uint8_t *)&t483 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t483 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t483 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t483), t482);
            ((uint8_t *)&t483)[16] = 1;
            uint8_t t484 = 0;
            F.ret = t483;
            goto gm_fret;
        }
        goto me51;
mn213:;
        gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 363, 5);
me51:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_wrap_each_2nbqohg(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_wrap_each_2nbqohg F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_b16_8 gm_l2; /* ds */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
                    gm_t8oufsr65o5ha t15 = GM_LD(gm_t8oufsr65o5ha, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 136));
                    F.l6 = t15;
                    {
                        gm_f_list_99597qp((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t17 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t18 = gm_f_push_h9qsb4d(t17, &(*(gm_t8oufsr65o5ha *)((uint8_t *)&F.l6)));
                        gm_tftb1odc739t5 t19 = t18;
                        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_src8lmgjh8bsvgp, 206, 9);
                        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
                    }
                    {
                        void * t21 = gm_l3;
                        gm_tbq1t03a53gpo t22;
                        memset((uint8_t *)&t22 + 4, 0, 4);
                        gm_t88rcucg1b7uk t23 = gm_l0;
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t22 + 0, t23);
                        memcpy((uint8_t *)&t22 + 8, ((uint8_t *)&F.l1), 104);
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t22 + 112, t24);
                        gm_t8oufsr65o5ha t25;
                        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tbq1t03a53gpo, ((uint8_t *)&t25 + 8), t22);
                        gm_tftb1odc739t5 t26 = gm_f_push_h9qsb4d(t21, &t25);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src8lmgjh8bsvgp, 207, 9);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src8lmgjh8bsvgp, 204, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l5 = t33;
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
