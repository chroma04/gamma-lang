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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint64_t v1; } f1; } gm_tiov0vlpoa3un;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint8_t v1; } f1; } gm_tv5kjk0k3g9nq;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiov0vlpoa3un) == 24 && _Alignof(gm_tiov0vlpoa3un) == 8 && offsetof(gm_tiov0vlpoa3un, f0) == 0 && offsetof(gm_tiov0vlpoa3un, f1) == 8, "layout of `string!u64`");
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { gm_tsoiri44d0i6q f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tjm4rm50v5mle;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tv5kjk0k3g9nq) == 24 && _Alignof(gm_tv5kjk0k3g9nq) == 8 && offsetof(gm_tv5kjk0k3g9nq, f0) == 0 && offsetof(gm_tv5kjk0k3g9nq, f1) == 8, "layout of `string!u8`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_tjm4rm50v5mle v1; } f1; } gm_tat31u3lvqjk4;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tjm4rm50v5mle) == 96 && _Alignof(gm_tjm4rm50v5mle) == 8 && offsetof(gm_tjm4rm50v5mle, f0) == 0 && offsetof(gm_tjm4rm50v5mle, f1) == 8 && offsetof(gm_tjm4rm50v5mle, f2) == 48 && offsetof(gm_tjm4rm50v5mle, f3) == 88, "layout of `Output`");
_Static_assert(sizeof(gm_tat31u3lvqjk4) == 104 && _Alignof(gm_tat31u3lvqjk4) == 8 && offsetof(gm_tat31u3lvqjk4, f0) == 0 && offsetof(gm_tat31u3lvqjk4, f1) == 8, "layout of `RunError!Output`");
struct gm_fr_asyncify_bs28ebq {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* modules */
    gm_trqfvmjtt4sij l12; /* argv */
    gm_tjm4rm50v5mle l14; /* out */
    gm_tl572d7579hns ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_import_modules_dn9ek0o {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* modules */
    gm_t470aqkia5212 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_read_error_o0vecnb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_run_error_text_ht475e9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_success_q3lb6vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_wasi_link_flags_6mu198c {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* flags */
    gm_b16_8 ret;
};
struct gm_fr_wasm_byte_8i1175n {
    gm_frame h;
    gm_tv5kjk0k3g9nq ret;
};
struct gm_fr_wasm_bytes_g836n28 {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_wasm_limits_6sltsf9 {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_wasm_name_8ial5lb {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_wasm_opt_o18b1rf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_wasm_opt_version_hapilm6 {
    gm_frame h;
    gm_tjm4rm50v5mle l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_wasm_uleb_lst1rv3 {
    gm_frame h;
    gm_tiov0vlpoa3un ret;
};
__attribute__((aligned(4))) void gm_f_asyncify_bs28ebq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_15tuglm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_46627ts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_l5e62u0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q0doeth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_q190eul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_import_modules_dn9ek0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_output_mg6eie5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_o0vecnb(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_text_ht475e9(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasi_link_flags_6mu198c(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_wasm_byte_8i1175n(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tv5kjk0k3g9nq gm_f_wasm_byte_8i1175n_c(void * a0);
__attribute__((aligned(4))) void gm_f_wasm_bytes_g836n28(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_wasm_bytes_g836n28_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_wasm_limits_6sltsf9(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tl572d7579hns gm_f_wasm_limits_6sltsf9_c(void * a0);
__attribute__((aligned(4))) void gm_f_wasm_name_8ial5lb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_wasm_name_8ial5lb_c(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_o18b1rf(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_version_hapilm6(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_wasm_uleb_lst1rv3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tiov0vlpoa3un gm_f_wasm_uleb_lst1rv3_c(void * a0);
extern uint8_t gm_c7mc3uaor1r_2[80] __attribute__((aligned(8)));
extern const uint8_t gm_s2j7kqlnfrbrh[9];
extern const uint8_t gm_s3vboaodpmi9a[4];
extern const uint8_t gm_s449uaav7f0l0[11];
extern const uint8_t gm_s5lecs8v1je8f[18];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s930a5k4dd69o[10];
extern const uint8_t gm_s9citadvj27tv[39];
extern const uint8_t gm_sf9nvp09l65v3[17];
extern const uint8_t gm_sg9fnspse4evr[22];
extern const uint8_t gm_shgi3g6gdv99o[4];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sjn4odi0ubper[4];
extern const uint8_t gm_skou19srual26[10];
extern const uint8_t gm_sn0peadng5i31[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcddjnq2mhhonr[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4i4cmesm57g[];
extern const uint8_t gm_stpnhgp46l3tu[9];
extern const uint8_t gm_svu2guaq187mi[9];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_o0vecnb(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_read_error_o0vecnb F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcddjnq2mhhonr, 84, 60);
me2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_text_ht475e9(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0) {
    struct gm_fr_run_error_text_ht475e9 F;
    gm_tj5jn79kq9pnm gm_l0; /* e */
    gm_toe5fmr3vvf4d gm_l1; /* io */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t5;
    gm_toe5fmr3vvf4d t6 = gm_l1;
    gm_b16_8 t7 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t1 = t7;
    goto me2;
mn3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)1ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sidpdvffdra02));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(13));
    t1 = t10;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcddjnq2mhhonr, 89, 68);
me2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_success_q3lb6vt F;
    gm_tsoiri44d0i6q gm_l0; /* s */
    uint32_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
        gm_l1 = t6;
        uint32_t t7 = gm_l1;
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t2 = t8;
        goto me3;
mn4:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        t2 = ((uint8_t)0);
        goto me3;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srct4i4cmesm57g, 65, 12);
me3:;
        uint8_t t11 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_asyncify_bs28ebq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_asyncify_bs28ebq F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* opt */
    uint8_t gm_l2; /* debug_info */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* e */
    uint8_t gm_l7; /* asyncified */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* m */
    gm_b16_8 gm_l11; /* tool */
    gm_tj5jn79kq9pnm gm_l13; /* e */
    GM_STACK_CHECK(&F, 2433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_tgvh15qshf643 t3;
        gm_f_read_all_r9rh9qd((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgvh15qshf643 t4 = t3;
        gm_b16_8 t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_tj5jn79kq9pnm t7 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t4) + 8));
            gm_l3 = t7;
            gm_t6j6smj7qoctu t9;
            gm_b16_8 t10 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
            gm_tj5jn79kq9pnm t11 = gm_l3;
            gm_b16_8 t12 = gm_f_read_error_o0vecnb((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t12);
            gm_tabb710d9mq2e t13;
            gm_f_format_04pispc((void *)&t13, (void *)NULL, cx, NULL, &t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srcddjnq2mhhonr, 54, 52);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_tl572d7579hns t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            gm_tl572d7579hns t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        gm_l4 = t5;
    }
    {
        gm_b16_8 t21 = gm_l4;
        gm_b16_8 t22 = t21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_t470aqkia5212 t24;
        gm_f_import_modules_dn9ek0o((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t470aqkia5212 t25 = t24;
        gm_trqfvmjtt4sij t26;
        if (!((((uint8_t *)&t25))[0] != 0)) {
            gm_trqfvmjtt4sij t27 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t25) + 8));
            t26 = t27;
        } else {
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_l5 = t28;
            gm_t6j6smj7qoctu t30;
            gm_b16_8 t31 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t31);
            gm_b16_8 t32 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t32);
            gm_tabb710d9mq2e t33;
            gm_f_format_15tuglm((void *)&t33, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcddjnq2mhhonr, 55, 58);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = t35;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            gm_tl572d7579hns t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            ((uint8_t *)&t38)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
            gm_tl572d7579hns t39 = t38;
            uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
            F.ret = t38;
            goto gm_fret;
        }
        F.l6 = t26;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_l8 = t42;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t44 = gm_l9;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t46 = (t44 < t45);
                if (!t46) goto lb43;
                {
                    uint64_t t49 = gm_l9;
                    uint64_t t50 = (uintptr_t)t49;
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t50 * 16));
                    gm_l10 = t54;
                    {
                        uint8_t t57 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l10)), gm_svu2guaq187mi, 8) == 0));
                        if (!t57) goto ie56;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto ix56;
ie56:;
ix56:;
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_srcddjnq2mhhonr, 57, 5);
me52:;
                }
lc43:;
                {
                    uint64_t t60 = gm_l9;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l9 = t63;
                }
            }
lb43:;
        }
    }
    {
        uint8_t t65 = gm_l7;
        uint8_t t66 = (uint8_t)!t65;
        if (!t66) goto ie64;
        {
            gm_b0_1 t68;
            gm_tl572d7579hns t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 16, 0);
            ((uint8_t *)&t69)[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&t69 + 8), t68);
            gm_tl572d7579hns t70 = t69;
            uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
            F.ret = t69;
            goto gm_fret;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        gm_b16_8 t72 = gm_f_wasm_opt_o18b1rf((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t72;
    }
    {
        gm_f_list_epvducg((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t73 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t74 = gm_l11;
        gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t73, t74);
        gm_tftb1odc739t5 t76 = t75;
        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcddjnq2mhhonr, 63, 5);
        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
    }
    {
        void * t78 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t79 = gm_l0;
        gm_tftb1odc739t5 t80 = gm_f_push_3ef746b(t78, t79);
        gm_tftb1odc739t5 t81 = t80;
        if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcddjnq2mhhonr, 64, 5);
        gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
    }
    {
        void * t83 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s449uaav7f0l0));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(10));
        gm_tftb1odc739t5 t85 = gm_f_push_3ef746b(t83, t84);
        gm_tftb1odc739t5 t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcddjnq2mhhonr, 65, 5);
        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
    }
    {
        void * t88 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s9citadvj27tv));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(38));
        gm_tftb1odc739t5 t90 = gm_f_push_3ef746b(t88, t89);
        gm_tftb1odc739t5 t91 = t90;
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcddjnq2mhhonr, 68, 5);
        gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
    }
    {
        uint8_t t94 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s3vboaodpmi9a, 3) == 0));
        if (!t94) goto ie93;
        {
            void * t96 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t97;
            uint8_t t99 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_shgi3g6gdv99o, 3) == 0));
            if (!t99) goto ie98;
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sjn4odi0ubper));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(3));
            gm_b16_8 t102 = t101;
            t97 = t102;
            goto ix98;
ie98:;
            gm_b16_8 t104 = gm_l1;
            gm_b16_8 t105 = t104;
            t97 = t105;
ix98:;
            gm_tftb1odc739t5 t106 = gm_f_push_3ef746b(t96, t97);
            gm_tftb1odc739t5 t107 = t106;
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcddjnq2mhhonr, 71, 27);
            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
        }
        goto ix93;
ie93:;
ix93:;
    }
    {
        uint8_t t110 = gm_l2;
        if (!t110) goto ie109;
        {
            void * t112 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sn0peadng5i31));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t114 = gm_f_push_3ef746b(t112, t113);
            gm_tftb1odc739t5 t115 = t114;
            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcddjnq2mhhonr, 72, 21);
            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        void * t117 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t118;
        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t119 = gm_f_push_3ef746b(t117, t118);
        gm_tftb1odc739t5 t120 = t119;
        if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srcddjnq2mhhonr, 73, 5);
        gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
    }
    {
        void * t122 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t123 = gm_l0;
        gm_tftb1odc739t5 t124 = gm_f_push_3ef746b(t122, t123);
        gm_tftb1odc739t5 t125 = t124;
        if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcddjnq2mhhonr, 74, 5);
        gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
    }
    {
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        gm_b16_8 t128 = t127;
        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
        gm_t32dckm4jfarl t130 = GM_LD(gm_t32dckm4jfarl, gm_c7mc3uaor1r_2);
        gm_tat31u3lvqjk4 t131;
        gm_f_output_mg6eie5((void *)&t131, (void *)NULL, cx, NULL, t129, &t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tat31u3lvqjk4 t132 = t131;
        gm_tjm4rm50v5mle t133;
        if (!((((uint8_t *)&t132))[0] != 0)) {
            gm_tjm4rm50v5mle t134 = GM_LD(gm_tjm4rm50v5mle, (((uint8_t *)&t132) + 8));
            t133 = t134;
        } else {
            gm_tj5jn79kq9pnm t135 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t132) + 8));
            gm_l13 = t135;
            gm_t6j6smj7qoctu t137;
            gm_b16_8 t138 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
            gm_tj5jn79kq9pnm t139 = gm_l13;
            gm_b16_8 t140 = gm_f_run_error_text_ht475e9((void *)NULL, cx, NULL, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t140);
            gm_tabb710d9mq2e t141;
            gm_f_format_q190eul((void *)&t141, (void *)NULL, cx, NULL, &t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t142 = t141;
            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 8, gm_srcddjnq2mhhonr, 76, 17);
            gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
            gm_b16_8 t144 = t143;
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_tl572d7579hns t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
            ((uint8_t *)&t146)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
            gm_tl572d7579hns t147 = t146;
            uint8_t t148 = ((((uint8_t *)&t147))[0] != 0);
            F.ret = t146;
            goto gm_fret;
        }
        F.l14 = t133;
    }
    {
        gm_tsoiri44d0i6q t150 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&F.l14) + 0));
        uint8_t t151 = gm_f_success_q3lb6vt(t150);
        uint8_t t152 = (uint8_t)!t151;
        if (!t152) goto ie149;
        {
            gm_t6j6smj7qoctu t154;
            gm_b16_8 t155 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t154 + 0, t155);
            gm_b16_8 t156 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l14) + 48) + 0));
            gm_b16_8 t157 = t156;
            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
            GM_ST(gm_b16_8, (uint8_t *)&t154 + 16, t158);
            gm_tabb710d9mq2e t159;
            gm_f_format_l5e62u0((void *)&t159, (void *)NULL, cx, NULL, &t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t160 = t159;
            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_srcddjnq2mhhonr, 79, 17);
            gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
            gm_b16_8 t162 = t161;
            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
            gm_tl572d7579hns t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
            ((uint8_t *)&t164)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t164 + 8), t163);
            gm_tl572d7579hns t165 = t164;
            uint8_t t166 = ((((uint8_t *)&t165))[0] != 0);
            F.ret = t164;
            goto gm_fret;
        }
        goto ix149;
ie149:;
ix149:;
    }
    {
        gm_b0_1 t167;
        gm_tl572d7579hns t168;
        GM_ST(uint8_t, (uint8_t *)&t168 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t168 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t168 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t168 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t168 + 16, 0);
        ((uint8_t *)&t168)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t168 + 8), t167);
        gm_tl572d7579hns t169 = t168;
        uint8_t t170 = ((((uint8_t *)&t169))[0] != 0);
        F.ret = t168;
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
__attribute__((aligned(4))) void gm_f_import_modules_dn9ek0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_import_modules_dn9ek0o F;
    gm_b16_8 gm_l0; /* wasm */
    gm_t3877pk7lv748 gm_l1; /* r */
    gm_b16_8 gm_l2; /* head */
    uint8_t gm_l4; /* id */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* body */
    gm_t3877pk7lv748 gm_l7; /* s */
    uint64_t gm_l8; /* count */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* kind */
    GM_STACK_CHECK(&F, 2846u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&gm_l1 + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&gm_l1 + 16, ((uint64_t)0ULL));
    }
    {
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_t3ujhu6qph7de t4;
        gm_f_wasm_bytes_g836n28((void *)&t4, t3, ((uint64_t)4ULL));
        gm_t3ujhu6qph7de t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
            gm_t470aqkia5212 t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l2 = t8;
    }
    {
        uint8_t t10 = 0;
        uint8_t t11 = 0;
        uint8_t t12 = 0;
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcddjnq2mhhonr, 98, 13);
        uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 1));
        uint8_t t16 = ((t15 == ((uint8_t)0ULL)));
        if (t16) {
            uint64_t t17 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srcddjnq2mhhonr, 98, 30);
            uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 1));
            uint8_t t20 = ((t19 == ((uint8_t)97ULL)));
            t12 = t20;
        } else t12 = 0;
        if (t12) {
            uint64_t t21 = (uintptr_t)((uint64_t)2ULL);
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcddjnq2mhhonr, 98, 49);
            uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t21 * 1));
            uint8_t t24 = ((t23 == ((uint8_t)115ULL)));
            t11 = t24;
        } else t11 = 0;
        if (t11) {
            uint64_t t25 = (uintptr_t)((uint64_t)3ULL);
            uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcddjnq2mhhonr, 98, 68);
            uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t25 * 1));
            uint8_t t28 = ((t27 == ((uint8_t)109ULL)));
            t10 = t28;
        } else t10 = 0;
        uint8_t t29 = (uint8_t)!t10;
        if (!t29) goto ie9;
        {
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sg9fnspse4evr));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(21));
            gm_t470aqkia5212 t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t32 + 40, 0);
            ((uint8_t *)&t32)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 8), t31);
            gm_t470aqkia5212 t33 = t32;
            uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
            F.ret = t32;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        {
            void * t36 = (void *)((uint8_t *)&gm_l1);
            gm_t3ujhu6qph7de t37;
            gm_f_wasm_bytes_g836n28((void *)&t37, t36, ((uint64_t)4ULL));
            gm_t3ujhu6qph7de t38 = t37;
            if (((((uint8_t *)&t38))[0] != 0)) {
                gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                gm_t470aqkia5212 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
                ((uint8_t *)&t40)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t40 + 8), t39);
                F.ret = t40;
                goto gm_fret;
            }
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 16));
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t45 = (t43 < t44);
            if (!t45) goto lb42;
            {
                void * t48 = (void *)((uint8_t *)&gm_l1);
                gm_tv5kjk0k3g9nq t49;
                gm_f_wasm_byte_8i1175n((void *)&t49, t48);
                gm_tv5kjk0k3g9nq t50 = t49;
                if (((((uint8_t *)&t50))[0] != 0)) {
                    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                    gm_t470aqkia5212 t52;
                    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t52 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t52 + 40, 0);
                    ((uint8_t *)&t52)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t52 + 8), t51);
                    F.ret = t52;
                    goto gm_fret;
                }
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t50) + 8));
                gm_l4 = t53;
            }
            {
                void * t54 = (void *)((uint8_t *)&gm_l1);
                gm_tiov0vlpoa3un t55;
                gm_f_wasm_uleb_lst1rv3((void *)&t55, t54);
                gm_tiov0vlpoa3un t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) {
                    gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                    gm_t470aqkia5212 t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t58 + 40, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
                    F.ret = t58;
                    goto gm_fret;
                }
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&t56) + 8));
                uint64_t t60 = (uint64_t)t59;
                gm_l5 = t60;
            }
            {
                void * t61 = (void *)((uint8_t *)&gm_l1);
                uint64_t t62 = gm_l5;
                gm_t3ujhu6qph7de t63;
                gm_f_wasm_bytes_g836n28((void *)&t63, t61, t62);
                gm_t3ujhu6qph7de t64 = t63;
                if (((((uint8_t *)&t64))[0] != 0)) {
                    gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                    gm_t470aqkia5212 t66;
                    GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t66 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t66 + 40, 0);
                    ((uint8_t *)&t66)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
                    F.ret = t66;
                    goto gm_fret;
                }
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                gm_l6 = t67;
            }
            {
                uint8_t t69 = gm_l4;
                uint8_t t70 = (!(t69 == ((uint8_t)2ULL)));
                if (!t70) goto ie68;
                {
                    goto lc42;
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                gm_b16_8 t72 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&gm_l7 + 0, t72);
                GM_ST(uint64_t, (uint8_t *)&gm_l7 + 16, ((uint64_t)0ULL));
            }
            {
                void * t73 = (void *)((uint8_t *)&gm_l7);
                gm_tiov0vlpoa3un t74;
                gm_f_wasm_uleb_lst1rv3((void *)&t74, t73);
                gm_tiov0vlpoa3un t75 = t74;
                if (((((uint8_t *)&t75))[0] != 0)) {
                    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                    gm_t470aqkia5212 t77;
                    GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
                    ((uint8_t *)&t77)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t76);
                    F.ret = t77;
                    goto gm_fret;
                }
                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&t75) + 8));
                gm_l8 = t78;
            }
            {
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t80 = gm_l8;
                    gm_l9 = t80;
                }
                {
                    for (;;) {
                        uint64_t t82 = gm_l10;
                        uint64_t t83 = gm_l9;
                        uint8_t t84 = (t82 < t83);
                        if (!t84) goto lb81;
                        {
                            uint64_t t87 = gm_l10;
                            uint64_t t88 = t87;
                            {
                                void * t92 = (void *)((uint8_t *)&F.l3);
                                void * t93 = (void *)((uint8_t *)&gm_l7);
                                gm_t3ujhu6qph7de t94;
                                gm_f_wasm_name_8ial5lb((void *)&t94, t93);
                                gm_t3ujhu6qph7de t95 = t94;
                                if (((((uint8_t *)&t95))[0] != 0)) {
                                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                                    gm_t470aqkia5212 t97;
                                    GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t97 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t97 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t97 + 40, 0);
                                    ((uint8_t *)&t97)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t97 + 8), t96);
                                    F.ret = t97;
                                    goto gm_fret;
                                }
                                gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                                gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t92, t98);
                                gm_tftb1odc739t5 t100 = t99;
                                if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srcddjnq2mhhonr, 109, 13);
                                gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
                            }
                            {
                                {
                                    void * t103 = (void *)((uint8_t *)&gm_l7);
                                    gm_t3ujhu6qph7de t104;
                                    gm_f_wasm_name_8ial5lb((void *)&t104, t103);
                                    gm_t3ujhu6qph7de t105 = t104;
                                    if (((((uint8_t *)&t105))[0] != 0)) {
                                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                                        gm_t470aqkia5212 t107;
                                        GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t107 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t107 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t107 + 40, 0);
                                        ((uint8_t *)&t107)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t107 + 8), t106);
                                        F.ret = t107;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                                }
                            }
                            {
                                void * t109 = (void *)((uint8_t *)&gm_l7);
                                gm_tv5kjk0k3g9nq t110;
                                gm_f_wasm_byte_8i1175n((void *)&t110, t109);
                                gm_tv5kjk0k3g9nq t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) {
                                    gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                                    gm_t470aqkia5212 t113;
                                    GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t113 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t113 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t113 + 40, 0);
                                    ((uint8_t *)&t113)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t113 + 8), t112);
                                    F.ret = t113;
                                    goto gm_fret;
                                }
                                uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&t111) + 8));
                                gm_l11 = t114;
                            }
                            {
                                uint8_t t115 = gm_l11;
                                uint8_t t116 = t115;
                                uint8_t t119 = GM_LD(uint8_t, ((uint8_t *)&t116));
                                if (!(t119 == ((uint8_t)0ULL))) goto mn118;
                                {
                                    {
                                        void * t122 = (void *)((uint8_t *)&gm_l7);
                                        gm_tiov0vlpoa3un t123;
                                        gm_f_wasm_uleb_lst1rv3((void *)&t123, t122);
                                        gm_tiov0vlpoa3un t124 = t123;
                                        if (((((uint8_t *)&t124))[0] != 0)) {
                                            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                                            gm_t470aqkia5212 t126;
                                            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t126 + 24, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t126 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t126 + 40, 0);
                                            ((uint8_t *)&t126)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 8), t125);
                                            F.ret = t126;
                                            goto gm_fret;
                                        }
                                        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&t124) + 8));
                                    }
                                }
                                goto me117;
mn118:;
                                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)&t116));
                                if (!(t129 == ((uint8_t)1ULL))) goto mn128;
                                {
                                    {
                                        void * t132 = (void *)((uint8_t *)&gm_l7);
                                        gm_tv5kjk0k3g9nq t133;
                                        gm_f_wasm_byte_8i1175n((void *)&t133, t132);
                                        gm_tv5kjk0k3g9nq t134 = t133;
                                        if (((((uint8_t *)&t134))[0] != 0)) {
                                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                                            gm_t470aqkia5212 t136;
                                            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t136 + 24, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t136 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t136 + 40, 0);
                                            ((uint8_t *)&t136)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t136 + 8), t135);
                                            F.ret = t136;
                                            goto gm_fret;
                                        }
                                        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t134) + 8));
                                    }
                                }
                                {
                                    void * t138 = (void *)((uint8_t *)&gm_l7);
                                    gm_tl572d7579hns t139;
                                    gm_f_wasm_limits_6sltsf9((void *)&t139, t138);
                                    gm_tl572d7579hns t140 = t139;
                                    if (((((uint8_t *)&t140))[0] != 0)) {
                                        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
                                        gm_t470aqkia5212 t142;
                                        GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t142 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t142 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t142 + 40, 0);
                                        ((uint8_t *)&t142)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t142 + 8), t141);
                                        F.ret = t142;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 8));
                                }
                                goto me117;
mn128:;
                                uint8_t t145 = GM_LD(uint8_t, ((uint8_t *)&t116));
                                if (!(t145 == ((uint8_t)2ULL))) goto mn144;
                                {
                                    void * t147 = (void *)((uint8_t *)&gm_l7);
                                    gm_tl572d7579hns t148;
                                    gm_f_wasm_limits_6sltsf9((void *)&t148, t147);
                                    gm_tl572d7579hns t149 = t148;
                                    if (((((uint8_t *)&t149))[0] != 0)) {
                                        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&t149) + 8));
                                        gm_t470aqkia5212 t151;
                                        GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t151 + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t151 + 32, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t151 + 40, 0);
                                        ((uint8_t *)&t151)[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t151 + 8), t150);
                                        F.ret = t151;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 8));
                                }
                                goto me117;
mn144:;
                                uint8_t t154 = GM_LD(uint8_t, ((uint8_t *)&t116));
                                if (!(t154 == ((uint8_t)3ULL))) goto mn153;
                                {
                                    {
                                        void * t157 = (void *)((uint8_t *)&gm_l7);
                                        gm_t3ujhu6qph7de t158;
                                        gm_f_wasm_bytes_g836n28((void *)&t158, t157, ((uint64_t)2ULL));
                                        gm_t3ujhu6qph7de t159 = t158;
                                        if (((((uint8_t *)&t159))[0] != 0)) {
                                            gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
                                            gm_t470aqkia5212 t161;
                                            GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t161 + 24, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t161 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t161 + 40, 0);
                                            ((uint8_t *)&t161)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t161 + 8), t160);
                                            F.ret = t161;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
                                    }
                                }
                                goto me117;
mn153:;
                                uint8_t t164 = GM_LD(uint8_t, ((uint8_t *)&t116));
                                if (!(t164 == ((uint8_t)4ULL))) goto mn163;
                                {
                                    {
                                        void * t167 = (void *)((uint8_t *)&gm_l7);
                                        gm_tv5kjk0k3g9nq t168;
                                        gm_f_wasm_byte_8i1175n((void *)&t168, t167);
                                        gm_tv5kjk0k3g9nq t169 = t168;
                                        if (((((uint8_t *)&t169))[0] != 0)) {
                                            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&t169) + 8));
                                            gm_t470aqkia5212 t171;
                                            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t171 + 24, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t171 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t171 + 40, 0);
                                            ((uint8_t *)&t171)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t171 + 8), t170);
                                            F.ret = t171;
                                            goto gm_fret;
                                        }
                                        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t169) + 8));
                                    }
                                }
                                {
                                    {
                                        void * t174 = (void *)((uint8_t *)&gm_l7);
                                        gm_tiov0vlpoa3un t175;
                                        gm_f_wasm_uleb_lst1rv3((void *)&t175, t174);
                                        gm_tiov0vlpoa3un t176 = t175;
                                        if (((((uint8_t *)&t176))[0] != 0)) {
                                            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&t176) + 8));
                                            gm_t470aqkia5212 t178;
                                            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t178 + 24, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t178 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t178 + 40, 0);
                                            ((uint8_t *)&t178)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t178 + 8), t177);
                                            F.ret = t178;
                                            goto gm_fret;
                                        }
                                        uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&t176) + 8));
                                    }
                                }
                                goto me117;
mn163:;
                                {
                                    uint8_t t182 = gm_l11;
                                    gm_t9qkh0rsooc8d t183;
                                    GM_ST(uint8_t, ((uint8_t *)&t183 + 0), t182);
                                    gm_tabb710d9mq2e t184;
                                    gm_f_format_q0doeth((void *)&t184, (void *)NULL, cx, NULL, t183);
                                    if (gm_unw.kind) {
                                        goto gm_ll85;
                                    }
                                    gm_tabb710d9mq2e t185 = t184;
                                    if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_srcddjnq2mhhonr, 129, 32);
                                    gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                                    gm_b16_8 t187 = t186;
                                    gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                    gm_t470aqkia5212 t189;
                                    GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t189 + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t189 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t189 + 40, 0);
                                    ((uint8_t *)&t189)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t189 + 8), t188);
                                    gm_t470aqkia5212 t190 = t189;
                                    uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
                                    F.ret = t189;
                                    goto gm_fret;
                                }
                                goto me117;
mn180:;
                                gm_trap("no match arm matched the scrutinee", gm_srcddjnq2mhhonr, 112, 13);
me117:;
                            }
                            goto me89;
mn90:;
                            gm_trap("no match arm matched the scrutinee", gm_srcddjnq2mhhonr, 108, 9);
me89:;
                        }
lc81:;
                        {
                            uint64_t t193 = gm_l10;
                            uint64_t r194; bool o195 = __builtin_add_overflow(t193, ((uint64_t)1ULL), &r194);
                            uint64_t t196 = r194;
                            gm_l10 = t196;
                        }
                    }
gm_ll85:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
                    }
                    goto gm_ll46;
lb81:;
                }
            }
lc42:;
        }
gm_ll46:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb42; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc42; }
        }
        goto gm_lad;
lb42:;
    }
    {
        gm_trqfvmjtt4sij t197 = F.l3;
        gm_t470aqkia5212 t198;
        GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
        ((uint8_t *)&t198)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t198 + 8), t197);
        gm_t470aqkia5212 t199 = t198;
        uint8_t t200 = ((((uint8_t *)&t199))[0] != 0);
        F.ret = t198;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_read_error_o0vecnb(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_run_error_text_ht475e9(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_wasi_link_flags_6mu198c(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_wasi_link_flags_6mu198c F;
    GM_STACK_CHECK(&F, 221u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_epvducg((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l0);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5lecs8v1je8f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        gm_tftb1odc739t5 t4 = gm_f_push_3ef746b(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcddjnq2mhhonr, 31, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l0);
        gm_t19enj90vg8bd t8;
        GM_ST(int32_t, ((uint8_t *)&t8 + 0), ((int32_t)67108864LL));
        gm_tabb710d9mq2e t9;
        gm_f_format_46627ts((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcddjnq2mhhonr, 32, 16);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_tftb1odc739t5 t14 = gm_f_push_3ef746b(t7, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcddjnq2mhhonr, 32, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 0));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
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
inline __attribute__((aligned(4))) void gm_f_wasm_byte_8i1175n(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasm_byte_8i1175n F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t3ujhu6qph7de t3;
        gm_f_wasm_bytes_g836n28((void *)&t3, t2, ((uint64_t)1ULL));
        gm_t3ujhu6qph7de t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_tv5kjk0k3g9nq t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l1 = t7;
    }
    {
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcddjnq2mhhonr, 150, 12);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t8 * 1));
        gm_tv5kjk0k3g9nq t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
        ((uint8_t *)&t11)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 8), t10);
        gm_tv5kjk0k3g9nq t12 = t11;
        uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
        F.ret = t11;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tv5kjk0k3g9nq gm_f_wasm_byte_8i1175n_c(void * a0) { gm_tv5kjk0k3g9nq gm_r; gm_f_wasm_byte_8i1175n(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_wasm_bytes_g836n28(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasm_bytes_g836n28 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 16));
        uint64_t r8; bool o9 = __builtin_sub_overflow(t5, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcddjnq2mhhonr, 142, 12);
        uint64_t t10 = r8;
        uint8_t t11 = (t3 > t10);
        if (!t11) goto ie2;
        {
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s930a5k4dd69o));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(9));
            gm_t3ujhu6qph7de t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            gm_t3ujhu6qph7de t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
        void * t19 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t17) + 0)));
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 16));
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 16));
        uint64_t t25 = gm_l1;
        uint64_t r26; bool o27 = __builtin_add_overflow(t24, t25, &r26);
        if (o27) gm_trap("integer overflow in `+`", gm_srcddjnq2mhhonr, 143, 20);
        uint64_t t28 = r26;
        uint64_t t29 = (uintptr_t)t28;
        if (t22 > t29 || t29 > (uintptr_t)t18) gm_trap_slice(t22, t29, t18, gm_srcddjnq2mhhonr, 143, 10);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t19 + t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29 - t22));
        gm_l2 = t30;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l0;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 16));
        uint64_t t34 = gm_l1;
        uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
        if (o36) gm_trap("integer overflow in `+`", gm_srcddjnq2mhhonr, 144, 5);
        uint64_t t37 = r35;
        GM_ST(uint64_t, (((uint8_t *)t31) + 16), t37);
    }
    {
        gm_b16_8 t38 = gm_l2;
        gm_t3ujhu6qph7de t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        ((uint8_t *)&t39)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
        gm_t3ujhu6qph7de t40 = t39;
        uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
        F.ret = t39;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_wasm_bytes_g836n28_c(void * a0, uint64_t a1) { gm_t3ujhu6qph7de gm_r; gm_f_wasm_bytes_g836n28(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_wasm_limits_6sltsf9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasm_limits_6sltsf9 F;
    void * gm_l0; /* r */
    uint8_t gm_l1; /* flags */
    GM_STACK_CHECK_SHALLOW(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tv5kjk0k3g9nq t3;
        gm_f_wasm_byte_8i1175n((void *)&t3, t2);
        gm_tv5kjk0k3g9nq t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_tl572d7579hns t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t4) + 8));
        gm_l1 = t7;
    }
    {
        {
            void * t9 = gm_l0;
            gm_tiov0vlpoa3un t10;
            gm_f_wasm_uleb_lst1rv3((void *)&t10, t9);
            gm_tiov0vlpoa3un t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) {
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
                gm_tl572d7579hns t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                ((uint8_t *)&t13)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
                F.ret = t13;
                goto gm_fret;
            }
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t11) + 8));
        }
    }
    {
        uint8_t t16 = gm_l1;
        uint8_t t17 = (uint8_t)(t16 & ((uint8_t)1ULL));
        uint8_t t18 = (!(t17 == ((uint8_t)0ULL)));
        if (!t18) goto ie15;
        {
            {
                void * t21 = gm_l0;
                gm_tiov0vlpoa3un t22;
                gm_f_wasm_uleb_lst1rv3((void *)&t22, t21);
                gm_tiov0vlpoa3un t23 = t22;
                if (((((uint8_t *)&t23))[0] != 0)) {
                    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                    gm_tl572d7579hns t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
                    F.ret = t25;
                    goto gm_fret;
                }
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t23) + 8));
            }
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        gm_b0_1 t27;
        gm_tl572d7579hns t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 16, 0);
        ((uint8_t *)&t28)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
        gm_tl572d7579hns t29 = t28;
        uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tl572d7579hns gm_f_wasm_limits_6sltsf9_c(void * a0) { gm_tl572d7579hns gm_r; gm_f_wasm_limits_6sltsf9(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_wasm_name_8ial5lb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasm_name_8ial5lb F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tiov0vlpoa3un t3;
        gm_f_wasm_uleb_lst1rv3((void *)&t3, t2);
        gm_tiov0vlpoa3un t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
            gm_t3ujhu6qph7de t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
        uint64_t t8 = (uint64_t)t7;
        gm_l1 = t8;
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = gm_l1;
        gm_t3ujhu6qph7de t11;
        gm_f_wasm_bytes_g836n28((void *)&t11, t9, t10);
        gm_t3ujhu6qph7de t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_t3ujhu6qph7de t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_t3ujhu6qph7de t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        ((uint8_t *)&t18)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
        gm_t3ujhu6qph7de t19 = t18;
        uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_wasm_name_8ial5lb_c(void * a0) { gm_t3ujhu6qph7de gm_r; gm_f_wasm_name_8ial5lb(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_o18b1rf(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_wasm_opt_o18b1rf F;
    gm_b16_8 gm_l0; /* v */
    GM_STACK_CHECK(&F, 363u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2j7kqlnfrbrh));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_srcddjnq2mhhonr, 38, 10);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t4;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_stpnhgp46l3tu));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(8));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l0 = t9;
    }
    {
        gm_b16_8 t13;
        gm_b16_8 t15 = gm_l0;
        gm_b16_8 t16 = gm_f_trim_fdrfe6i(t15);
        gm_b16_8 t17 = t16;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
        if (!t19) goto ie14;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_stpnhgp46l3tu));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(8));
        gm_b16_8 t22 = t21;
        t13 = t22;
        goto ix14;
ie14:;
        gm_b16_8 t24 = gm_l0;
        gm_b16_8 t25 = t24;
        t13 = t25;
ix14:;
        uint8_t t26 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_version_hapilm6(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_wasm_opt_version_hapilm6 F;
    gm_t0kl141g5m2qf gm_l0; /* argv */
    GM_STACK_CHECK(&F, 698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_wasm_opt_o18b1rf((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&gm_l0 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skou19srual26));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&gm_l0 + 16, t3);
    }
    {
        void * t4 = ((uint8_t *)&gm_l0);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcddjnq2mhhonr, 46, 27);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(((uint8_t *)t4 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_t32dckm4jfarl t8 = GM_LD(gm_t32dckm4jfarl, gm_c7mc3uaor1r_2);
        gm_tat31u3lvqjk4 t9;
        gm_f_output_mg6eie5((void *)&t9, (void *)NULL, cx, NULL, t7, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tat31u3lvqjk4 t10 = t9;
        gm_tjm4rm50v5mle t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_tjm4rm50v5mle t12 = GM_LD(gm_tjm4rm50v5mle, (((uint8_t *)&t10) + 8));
            t11 = t12;
        } else {
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        F.l1 = t11;
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 8) + 0));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
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
inline __attribute__((aligned(4))) void gm_f_wasm_uleb_lst1rv3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasm_uleb_lst1rv3 F;
    void * gm_l0; /* r */
    uint64_t gm_l1; /* v */
    uint64_t gm_l2; /* shift */
    uint8_t gm_l3; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint8_t t4 = (t3 < ((uint64_t)64ULL));
            if (!t4) goto lb2;
            {
                void * t7 = gm_l0;
                gm_tv5kjk0k3g9nq t8;
                gm_f_wasm_byte_8i1175n((void *)&t8, t7);
                gm_tv5kjk0k3g9nq t9 = t8;
                if (((((uint8_t *)&t9))[0] != 0)) {
                    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
                    gm_tiov0vlpoa3un t11;
                    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
                    ((uint8_t *)&t11)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
                    F.ret = t11;
                    goto gm_fret;
                }
                uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t9) + 8));
                gm_l3 = t12;
            }
            {
                uint64_t t13 = gm_l1;
                uint8_t t14 = gm_l3;
                uint8_t t15 = (uint8_t)(t14 & ((uint8_t)127ULL));
                uint64_t t16 = (uint64_t)t15;
                uint64_t t17 = gm_l2;
                unsigned __int128 n18 = (unsigned __int128)t17;
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcddjnq2mhhonr, 158, 14);
                uint64_t r19 = (uint64_t)((uint64_t)t16 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t16;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcddjnq2mhhonr, 158, 14);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                gm_l1 = t22;
            }
            {
                uint8_t t24 = gm_l3;
                uint8_t t25 = (uint8_t)(t24 & ((uint8_t)128ULL));
                uint8_t t26 = ((t25 == ((uint8_t)0ULL)));
                if (!t26) goto ie23;
                {
                    uint64_t t28 = gm_l1;
                    gm_tiov0vlpoa3un t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                    ((uint8_t *)&t29)[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&t29 + 8), t28);
                    gm_tiov0vlpoa3un t30 = t29;
                    uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
                    F.ret = t29;
                    goto gm_fret;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)7ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srcddjnq2mhhonr, 160, 9);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
lc2:;
        }
lb2:;
    }
    {
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sf9nvp09l65v3));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(16));
        gm_tiov0vlpoa3un t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        ((uint8_t *)&t37)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t37 + 8), t36);
        gm_tiov0vlpoa3un t38 = t37;
        uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
        F.ret = t37;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tiov0vlpoa3un gm_f_wasm_uleb_lst1rv3_c(void * a0) { gm_tiov0vlpoa3un gm_r; gm_f_wasm_uleb_lst1rv3(&gm_r, a0); return gm_r; }
