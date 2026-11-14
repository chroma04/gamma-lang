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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; union { uint8_t v0; uint8_t v1; } f1; } gm_t5sj3u8e14oco;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_t16on0i8enr1i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_t5sj3u8e14oco f0; uint8_t f1; } gm_t5hib78lmj2mf;
_Static_assert(sizeof(gm_t5sj3u8e14oco) == 2 && _Alignof(gm_t5sj3u8e14oco) == 1 && offsetof(gm_t5sj3u8e14oco, f0) == 0 && offsetof(gm_t5sj3u8e14oco, f1) == 1, "layout of `Shape`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_tbgd14esf0jvi a[2]; } gm_t0vh9i8m50f41;
_Static_assert(sizeof(gm_t16on0i8enr1i) == 32 && _Alignof(gm_t16on0i8enr1i) == 8 && offsetof(gm_t16on0i8enr1i, f0) == 0 && offsetof(gm_t16on0i8enr1i, f1) == 16 && offsetof(gm_t16on0i8enr1i, f2) == 28, "layout of `LabelUse`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hib78lmj2mf) == 3 && _Alignof(gm_t5hib78lmj2mf) == 1 && offsetof(gm_t5hib78lmj2mf, f0) == 0 && offsetof(gm_t5hib78lmj2mf, f1) == 2, "layout of `?Shape`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tg0vmr665gcag;
typedef struct { gm_t16on0i8enr1i f0; uint8_t f1; uint8_t pe[7]; } gm_tgqc207d6ne28;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tsu0mic7gtnv6;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; } gm_tt7vn9ehj0acr;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; gm_ts6oqppjh0ba3 f4; gm_ts6oqppjh0ba3 f5; gm_ts6oqppjh0ba3 f6; gm_ts6oqppjh0ba3 f7; } gm_tv78bma7gi894;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t0vh9i8m50f41) == 64 && _Alignof(gm_t0vh9i8m50f41) == 8 && offsetof(gm_t0vh9i8m50f41, a) == 0, "layout of `[2]Edit`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
_Static_assert(sizeof(gm_tg0vmr665gcag) == 216 && _Alignof(gm_tg0vmr665gcag) == 8 && offsetof(gm_tg0vmr665gcag, f0) == 0 && offsetof(gm_tg0vmr665gcag, f1) == 40 && offsetof(gm_tg0vmr665gcag, f2) == 96 && offsetof(gm_tg0vmr665gcag, f3) == 136 && offsetof(gm_tg0vmr665gcag, f4) == 176, "layout of `FileFacts`");
_Static_assert(sizeof(gm_tgqc207d6ne28) == 40 && _Alignof(gm_tgqc207d6ne28) == 8 && offsetof(gm_tgqc207d6ne28, f0) == 0 && offsetof(gm_tgqc207d6ne28, f1) == 32, "layout of `?LabelUse`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { void * f0; gm_tg0vmr665gcag f1; gm_trqfvmjtt4sij f2; } gm_ts271qav2bkv9;
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_tsu0mic7gtnv6) == 56 && _Alignof(gm_tsu0mic7gtnv6) == 8 && offsetof(gm_tsu0mic7gtnv6, f0) == 0 && offsetof(gm_tsu0mic7gtnv6, f1) == 16, "layout of `Allow`");
_Static_assert(sizeof(gm_tt7vn9ehj0acr) == 3 && _Alignof(gm_tt7vn9ehj0acr) == 1 && offsetof(gm_tt7vn9ehj0acr, f0) == 0 && offsetof(gm_tt7vn9ehj0acr, f1) == 2, "layout of `?BinOp`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_ttja7ii61nila;
_Static_assert(sizeof(gm_tv78bma7gi894) == 16 && _Alignof(gm_tv78bma7gi894) == 1 && offsetof(gm_tv78bma7gi894, f0) == 0 && offsetof(gm_tv78bma7gi894, f1) == 2 && offsetof(gm_tv78bma7gi894, f2) == 4 && offsetof(gm_tv78bma7gi894, f3) == 6 && offsetof(gm_tv78bma7gi894, f4) == 8 && offsetof(gm_tv78bma7gi894, f5) == 10 && offsetof(gm_tv78bma7gi894, f6) == 12 && offsetof(gm_tv78bma7gi894, f7) == 14, "layout of `(BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
typedef struct { gm_ttja7ii61nila f0; uint8_t f1; uint8_t pe[7]; } gm_t9t5bnrf784qu;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_ts271qav2bkv9) == 264 && _Alignof(gm_ts271qav2bkv9) == 8 && offsetof(gm_ts271qav2bkv9, f0) == 0 && offsetof(gm_ts271qav2bkv9, f1) == 8 && offsetof(gm_ts271qav2bkv9, f2) == 224, "layout of `Walk`");
_Static_assert(sizeof(gm_ttja7ii61nila) == 48 && _Alignof(gm_ttja7ii61nila) == 8 && offsetof(gm_ttja7ii61nila, f0) == 0 && offsetof(gm_ttja7ii61nila, f1) == 8, "layout of `IfElse`");
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t9t5bnrf784qu) == 56 && _Alignof(gm_t9t5bnrf784qu) == 8 && offsetof(gm_t9t5bnrf784qu, f0) == 0 && offsetof(gm_t9t5bnrf784qu, f1) == 48, "layout of `?IfElse`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_accepts_3h4b27e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_binding_op_gcp8f8i {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_binding_stmt_80l1chq {
    gm_frame h;
    gm_tl5vm0tcqvdvb l10; /* site */
    gm_b0_1 ret;
};
struct gm_fr_blank_ineorlq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_block_btrailr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_capture_abs7k4r {
    gm_frame h;
    gm_tnvnsr1bfvtbd l4; /* fix */
    gm_b0_1 ret;
};
struct gm_fr_check_name_hf7dide {
    gm_frame h;
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_collapsible_else_ehhjtii {
    gm_frame h;
    gm_ttja7ii61nila l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_cond_e0272rd {
    gm_frame h;
    gm_tq5j0547p8452 l5; /* part */
    gm_t0d02142k4tu8 l6; /* pat */
    gm_b0_1 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_convert_c7mdt88 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_dedent_aa5vbel {
    gm_frame h;
    uint64_t l3; /* shift */
    gm_trqfvmjtt4sij l4; /* out */
    uint8_t l5; /* first */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_empty_branches_7o4oudt {
    gm_frame h;
    gm_ttja7ii61nila l3; /* els */
    gm_t79rbdv4rafe5 l4; /* b */
    gm_t4gut2c9fj1kt l7; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_9f3suev {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kqiv2v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_3limf78 {
    gm_frame h;
    void * l0; /* w */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_tioi3jh9v22g5 l10; /* x */
    gm_t99vmhfllumbh l17; /* c */
    gm_tndsf0a4f4lj0 l22; /* arm */
    gm_t79rbdv4rafe5 l36; /* b */
    gm_tchr52g0ema53 l39; /* b */
    gm_t1s75i08qtptd l41; /* label */
    gm_b0_1 ret;
};
struct gm_fr_fields_8ttf3eb {
    gm_frame h;
    gm_tfc3oljlofib6 l4; /* m */
    gm_t24301fas1b2a l5; /* f */
    gm_t7bjbmukv26e7 l9; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_file_facts_tdo30n8 {
    gm_frame h;
    gm_tg0vmr665gcag ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_c8tpla6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_lit_hd5tsqt {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* labels */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
struct gm_fr_for_body_1063u09 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_8jjs01b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_cbj68cn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_comment_qa4dr2o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_2auo7cg {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_identical_branches_ug6ccol {
    gm_frame h;
    gm_ttja7ii61nila l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_tcuumjkg5eni6 l7; /* br */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_if_expr_k16f2n5 {
    gm_frame h;
    gm_tcuumjkg5eni6 l5; /* br */
    gm_tq5j0547p8452 l8; /* part */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_ttja7ii61nila l13; /* els */
    gm_t79rbdv4rafe5 l14; /* b */
    gm_b0_1 ret;
};
struct gm_fr_indent_of_p7r0sg5 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_is_constant_call_vj2jgti {
    gm_frame h;
    gm_t7te2tdpebbt4 l4; /* a */
    gm_tc3oht4ksth9d l5; /* x */
    gm_tc3oht4ksth9d l6; /* x */
    gm_tv1o1j44ii4db l10; /* el */
    uint8_t ret;
};
struct gm_fr_is_literal_9ac2fdt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_q12rpq9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_68kn81r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_a3ennq0 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* allowed */
    gm_tkacitepg5fk0 l5; /* a */
    gm_tc3oht4ksth9d l9; /* e */
    gm_tkacitepg5fk0 l13; /* a */
    gm_tc3oht4ksth9d l17; /* e */
    gm_t4gut2c9fj1kt l20; /* d */
    gm_tv2uf4leil5hq l21; /* b */
    gm_t7bjbmukv26e7 l24; /* wh */
    gm_t99vmhfllumbh l35; /* c */
    gm_tcfiotghd1s5k l36; /* t */
    gm_b0_1 ret;
};
struct gm_fr_lint_diag_4vra4qu {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu17r8d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_names_in_1jbgo4k {
    gm_frame h;
    gm_tm4pc0emhrv1g l5; /* t */
    gm_b0_1 ret;
};
struct gm_fr_needless_bool_mifff69 {
    gm_frame h;
    gm_ttja7ii61nila l3; /* els */
    gm_t79rbdv4rafe5 l4; /* els_block */
    gm_t4gut2c9fj1kt l18; /* d */
    gm_b0_1 ret;
};
struct gm_fr_negation_tmaga6d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_normalize_space_v7dp7uh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_paren_at_av727r5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l7; /* d */
    gm_t0vh9i8m50f41 l8; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_paren_unless_simple_mhajkgl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pattern_8qtr7os {
    gm_frame h;
    gm_tl5vm0tcqvdvb l4; /* site */
    gm_tnvnsr1bfvtbd l6; /* fix */
    gm_b0_1 ret;
};
struct gm_fr_pop_h4mkmpb {
    gm_frame h;
    gm_tgqc207d6ne28 ret;
};
struct gm_fr_pop_label_0jfjsf5 {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_push_l0e5v4c {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_push_label_hn6s4vp {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_rule_scmud9h {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_shape_2jg6iuk {
    gm_frame h;
    gm_t5hib78lmj2mf ret;
};
struct gm_fr_shrink_to_hi_5bdve33 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_sig_fn4opan {
    gm_frame h;
    gm_tf2orqcrphmek l5; /* p */
    gm_b0_1 ret;
};
struct gm_fr_stmt_j3ls0p3 {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_t43d6nbdfap6p l12; /* wl */
    gm_tq5j0547p8452 l15; /* part */
    gm_t0d02142k4tu8 l18; /* pat */
    gm_t79rbdv4rafe5 l21; /* b */
    gm_b0_1 ret;
};
struct gm_fr_text_kspioh6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_top_level_name_dv4kpc9 {
    gm_frame h;
    gm_tkacitepg5fk0 l9; /* a */
    gm_tc0sbs1tp6d7f l15; /* op */
    gm_b0_1 ret;
};
struct gm_fr_upper_camel_a0jt5mh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_use_label_8j0qqam {
    gm_frame h;
    gm_t1s75i08qtptd l1; /* label */
    gm_b0_1 ret;
};
struct gm_fr_variants_eef2p79 {
    gm_frame h;
    gm_t03a3imodqi32 l4; /* m */
    gm_tnljbma3s3h16 l5; /* v */
    gm_t7bjbmukv26e7 l6; /* wh */
    gm_b0_1 ret;
};
struct gm_fr_walk_file_do4uq8b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_tech5q5 {
    gm_frame h;
    gm_ts271qav2bkv9 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_when_stmt_qbi9l8v {
    gm_frame h;
    gm_tapdr2ag17fh7 l2; /* els */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_accepts_3h4b27e(gm_t9qkh0rsooc8d a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_binding_op_gcp8f8i(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_stmt_80l1chq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_blank_ineorlq(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_btrailr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_capture_abs7k4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_name_hf7dide(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_collapsible_else_ehhjtii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_cond_e0272rd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_convert_c7mdt88(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dedent_aa5vbel(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_empty_branches_7o4oudt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_3limf78(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_8ttf3eb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_ojgi3et_sq5308c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_lit_hd5tsqt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_for_each_child_tkrfoqu(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_1vkmnih(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4pq08p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h13dbf2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jm2cnr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o2cbrdu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_reauhlh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t472cpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_identical_branches_ug6ccol(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_k16f2n5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint64_t gm_f_indent_of_p7r0sg5(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_constant_call_vj2jgti(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_9ac2fdt(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_item_a3ennq0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_needless_bool_mifff69(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_negation_tmaga6d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_space_v7dp7uh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_paren_at_av727r5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_paren_unless_simple_mhajkgl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_8qtr7os(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_h4mkmpb(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_label_0jfjsf5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_amge2t0(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_em9e1kh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_label_hn6s4vp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_amueool(void * a0, gm_cseahvg8v7p0vs a1, const void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t5hib78lmj2mf gm_f_shape_2jg6iuk(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_fn4opan(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s44sj5s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s76o7tg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_j3ls0p3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_top_level_name_dv4kpc9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam_c(void * a0, gm_t1s75i08qtptd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_variants_eef2p79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_qbi9l8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern void *const gm_hl7iqllvekj4[];
extern void *const gm_hldg2pcvebta[];
extern void *const gm_hlhb9f1a7omo[];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s1hha8fc2kd79[18];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1o0cl13ab3oc[18];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s3i3n5bvpvmmi[16];
extern const uint8_t gm_s5cr5924ev095[65];
extern const uint8_t gm_s5f9u0o3duhns[13];
extern const uint8_t gm_s5gkblcfbl1mv[121];
extern const uint8_t gm_s6mpqtffqhn51[80];
extern const uint8_t gm_s6vg88ce880ha[13];
extern const uint8_t gm_s71j6n6s8up14[17];
extern const uint8_t gm_s816n04ro16nc[48];
extern const uint8_t gm_s91c3em0lmpov[37];
extern const uint8_t gm_s9ln4rnb6talr[4];
extern const uint8_t gm_sbgcsq0m3astj[43];
extern const uint8_t gm_sblt6ctj6g4ck[21];
extern const uint8_t gm_sc06resjcdhg4[19];
extern const uint8_t gm_scd8m51mtmjv9[19];
extern const uint8_t gm_sci6hkonrjrne[19];
extern const uint8_t gm_scmgj2ccom9v4[40];
extern const uint8_t gm_sd1d50j1ea4vf[46];
extern const uint8_t gm_sdtpg69jtnl30[30];
extern const uint8_t gm_seqndsmrihepc[10];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfna4d5mnmoa8[30];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sfr3tv65k63d4[16];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgrap32255cto[47];
extern const uint8_t gm_sh4r0fq5alj61[14];
extern const uint8_t gm_shfvspt8ngibo[19];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shquo2s3jci8a[42];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_sium1s93t7o1a[19];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjuj50mlj6mg0[14];
extern const uint8_t gm_sk6ueggk5ji6v[10];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_sllrmadmbrcvv[54];
extern const uint8_t gm_sm7emfdvcb7c2[3];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_snh86nvrv0us3[19];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so9ldq1i41cj2[19];
extern const uint8_t gm_sob1n7uh7kpps[14];
extern const uint8_t gm_sotb03b2eqtp5[20];
extern const uint8_t gm_spg4ktuno2hf7[51];
extern const uint8_t gm_spic2fa42u00e[19];
extern const uint8_t gm_spj9t9b8lqlpp[8];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const uint8_t gm_sqit0ai5v3joc[23];
extern const uint8_t gm_sqmr87iegdcns[7];
extern const uint8_t gm_sr1bm2v312fv6[15];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src10tru552hn49[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_sso47q408qu33[24];
extern const uint8_t gm_suls9doo5jh0i[11];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
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
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
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
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    gm_tf5srh4mp4b3i t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 82, 50);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9f3suev F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kqiv2v5 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_c8tpla6 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_3limf78 *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_comment_qa4dr2o F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_text_kspioh6(t1, GM_CONV(gm_cseahvg8v7p0vs, t2));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_si4j8sh2qika1));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    uint8_t t5 = gm_f_contains_gkki4kq(t3, t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_2auo7cg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_kqiv2v5();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_q12rpq9 F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = gm_l0;
        gm_l1 = t2;
    }
    uint8_t t3 = 0;
    uint8_t t4 = gm_l1;
    uint8_t t5 = (((uint8_t)97ULL) <= t4);
    if (t5) {
        uint8_t t6 = gm_l1;
        uint8_t t7 = (t6 <= ((uint8_t)122ULL));
        t3 = t7;
    } else t3 = 0;
    uint8_t t8 = t3;
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    uint8_t t5 = 0;
    uint8_t t6 = gm_l0;
    uint8_t t7 = ((t6 == ((uint8_t)32ULL)));
    if (!t7) {
        uint8_t t8 = gm_l0;
        uint8_t t9 = ((t8 == ((uint8_t)9ULL)));
        t5 = t9;
    } else t5 = 1;
    if (!t5) {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)10ULL)));
        t4 = t11;
    } else t4 = 1;
    if (!t4) {
        uint8_t t12 = gm_l0;
        uint8_t t13 = ((t12 == ((uint8_t)13ULL)));
        t3 = t13;
    } else t3 = 1;
    if (!t3) {
        uint8_t t14 = gm_l0;
        uint8_t t15 = ((t14 == ((uint8_t)11ULL)));
        t2 = t15;
    } else t2 = 1;
    if (!t2) {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        t1 = t17;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_68kn81r F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = gm_l0;
        gm_l1 = t2;
    }
    uint8_t t3 = 0;
    uint8_t t4 = gm_l1;
    uint8_t t5 = (((uint8_t)65ULL) <= t4);
    if (t5) {
        uint8_t t6 = gm_l1;
        uint8_t t7 = (t6 <= ((uint8_t)90ULL));
        t3 = t7;
    } else t3 = 0;
    uint8_t t8 = t3;
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_diag_4vra4qu F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tl874t2atnlh3 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_f_lint_eo1tub5((void *)&(*(gm_t4gut2c9fj1kt *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_eo1tub5 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_f_warning_q0m5jjs((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
        ((uint8_t *)&t5)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 160), t5);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 184);
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu17r8d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_9f3suev();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n5n7pq1();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_h4mkmpb(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_h4mkmpb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t16on0i8enr1i gm_l2; /* x */
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
            gm_tgqc207d6ne28 t8;
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
        gm_t16on0i8enr1i t17 = GM_LD(gm_t16on0i8enr1i, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_t16on0i8enr1i t29 = gm_l2;
        gm_tgqc207d6ne28 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 36, 0);
        GM_ST(gm_t16on0i8enr1i, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[32] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_l0e5v4c F;
    void * gm_l0; /* w */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 8) + 0);
        gm_tftb1odc739t5 t4 = gm_f_push_l0rgbjf(t3, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src10tru552hn49, 72, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rule_scmud9h F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tf5srh4mp4b3i t2 = gm_f_lint_rule_o3ccae1(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shrink_to_hi_5bdve33 F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_kspioh6 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 16) + 8);
    void * t6 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t4) + 16)));
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t8 = (uint64_t)t7;
    uint64_t t9 = (uintptr_t)t8;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t11 = (uint64_t)t10;
    uint64_t t12 = (uintptr_t)t11;
    if (t9 > t12 || t12 > (uintptr_t)t5) gm_trap_slice(t9, t12, t5, gm_src10tru552hn49, 69, 60);
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t6 + t9 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t9));
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_upper_camel_a0jt5mh F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_src10tru552hn49, 751, 64);
        uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t5 * 1));
        uint8_t t8 = gm_f_is_upper_68kn81r(t7);
        t2 = t8;
    } else t2 = 0;
    if (t2) {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10 = t9;
        uint8_t t11 = 0;
        uint8_t t12 = ((uint8_t)95ULL);
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 1, ((uint8_t *)&t12))) { t11 = 1; break; }
        uint8_t t13 = (uint8_t)(!t11);
        t1 = t13;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_walk_tech5q5 F;
    void * gm_l0; /* sm */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tg0vmr665gcag t2;
    gm_f_file_facts_tdo30n8((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_uaa66su((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(void *, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 0, t1);
    GM_ST(gm_tg0vmr665gcag, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 8, t2);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_ts271qav2bkv9 *)gm_out) + 224, t3);
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
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_warning_q0m5jjs F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_tl874t2atnlh3 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 0), t4);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 184);
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
inline __attribute__((aligned(4))) uint8_t gm_f_accepts_3h4b27e(gm_t9qkh0rsooc8d a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_accepts_3h4b27e F;
    gm_t9qkh0rsooc8d gm_l0; /* case */
    gm_b16_8 gm_l1; /* s */
    uint8_t gm_l2; /* snake */
    uint8_t gm_l3; /* has_lower */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)1);
    }
    {
        gm_l3 = ((uint8_t)0);
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l6 = t17;
                    {
                        uint8_t t20 = gm_l6;
                        uint8_t t21 = gm_f_is_upper_68kn81r(t20);
                        if (!t21) goto ie19;
                        {
                            gm_l2 = ((uint8_t)0);
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        uint8_t t24 = gm_l6;
                        uint8_t t25 = gm_f_is_lower_q12rpq9(t24);
                        if (!t25) goto ie23;
                        {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 740, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l5;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l5 = t31;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t32;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t38) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma36; default: goto ma37; }
ma34:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t40 != ((uint8_t)0ULL)) goto mn39;
        gm_b16_8 t41 = gm_l1;
        uint8_t t42 = gm_f_upper_camel_a0jt5mh(t41);
        t32 = t42;
        goto me33;
mn39:;
ma35:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t44 != ((uint8_t)1ULL)) goto mn43;
        uint8_t t45 = gm_l2;
        t32 = t45;
        goto me33;
mn43:;
ma36:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t47 != ((uint8_t)2ULL)) goto mn46;
        uint8_t t48 = 0;
        uint8_t t49 = 0;
        uint8_t t50 = gm_l2;
        if (!t50) {
            uint8_t t51 = gm_l3;
            uint8_t t52 = (uint8_t)!t51;
            t49 = t52;
        } else t49 = 1;
        if (!t49) {
            gm_b16_8 t53 = gm_l1;
            uint8_t t54 = gm_f_upper_camel_a0jt5mh(t53);
            t48 = t54;
        } else t48 = 1;
        t32 = t48;
        goto me33;
mn46:;
ma37:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 744, 12);
me33:;
        uint8_t t55 = 0;
        F.ret = t32;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_binding_op_gcp8f8i(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binding_op_gcp8f8i F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* between */
    gm_b16_8 gm_l2; /* t */
    uint64_t gm_l3; /* off */
    uint32_t gm_l4; /* len */
    uint32_t gm_l5; /* lo */
    GM_STACK_CHECK_SHALLOW(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_f_text_kspioh6(t2, GM_CONV(gm_cseahvg8v7p0vs, t3));
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
            uint64_t t9 = gm_l3;
            uint64_t t10 = (uintptr_t)t9;
            uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src10tru552hn49, 243, 42);
            uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 1));
            uint8_t t13 = gm_f_is_space_uen38t5(t12);
            if (!t13) goto lb5;
            {
                uint64_t t16 = gm_l3;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 243, 52);
                uint64_t t19 = r17;
                gm_l3 = t19;
            }
lc5:;
        }
lb5:;
    }
    {
        uint64_t t21 = gm_l3;
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t23 = (t21 >= t22);
        if (!t23) goto ie20;
        {
            gm_toceffb697p0u t25;
            memset(&t25, 0, sizeof t25);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        uint32_t t27;
        uint8_t t29 = 0;
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t31 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t32 = gm_l3;
        uint64_t t33 = (uintptr_t)t32;
        if (t33 > (uintptr_t)t30 || (uintptr_t)t30 > (uintptr_t)t30) gm_trap_slice(t33, (uintptr_t)t30, t30, gm_src10tru552hn49, 245, 19);
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t31 + t33 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)((uintptr_t)t30 - t33));
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_shge5n743ua4h));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
        uint8_t t36 = gm_f_starts_with_6vvt01e(t34, t35);
        if (!t36) {
            uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
            uint64_t t39 = gm_l3;
            uint64_t t40 = (uintptr_t)t39;
            if (t40 > (uintptr_t)t37 || (uintptr_t)t37 > (uintptr_t)t37) gm_trap_slice(t40, (uintptr_t)t37, t37, gm_src10tru552hn49, 245, 49);
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t38 + t40 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)t37 - t40));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_ss67f0gmfen1m));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(2));
            uint8_t t43 = gm_f_starts_with_6vvt01e(t41, t42);
            t29 = t43;
        } else t29 = 1;
        if (!t29) goto ie28;
        uint32_t t45 = ((uint32_t)2ULL);
        t27 = t45;
        goto ix28;
ie28:;
        uint32_t t47 = ((uint32_t)1ULL);
        t27 = t47;
ix28:;
        gm_l4 = t27;
    }
    {
        uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint64_t t49 = gm_l3;
        uint32_t t50 = (uint32_t)t49;
        uint32_t r51; bool o52 = __builtin_add_overflow(t48, t50, &r51);
        if (o52) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 246, 11);
        uint32_t t53 = r51;
        gm_l5 = t53;
    }
    {
        uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint32_t t55 = gm_l5;
        uint32_t t56 = gm_l5;
        uint32_t t57 = gm_l4;
        uint32_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
        if (o59) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 247, 56);
        uint32_t t60 = r58;
        gm_tl874t2atnlh3 t61;
        GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t54);
        GM_ST(uint32_t, ((uint8_t *)&t61 + 4), t55);
        GM_ST(uint32_t, ((uint8_t *)&t61 + 8), t60);
        gm_toceffb697p0u t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[12] = 1;
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_stmt_80l1chq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_binding_stmt_80l1chq F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* inner */
    void * gm_l4; /* i */
    gm_t9qkh0rsooc8d gm_l5; /* case */
    uint32_t gm_l6; /* from */
    void * gm_l7; /* t */
    gm_toceffb697p0u gm_l8; /* op */
    gm_b16_8 gm_l9; /* init_text */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t08olq97rscn3 gm_l13; /* n */
    void * gm_l14; /* t */
    GM_STACK_CHECK(&F, 1313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)1ULL)));
        if (!t5) goto ie2;
        {
            uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
            void * t8 = gm_l1;
            uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 8) + 8);
            if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src10tru552hn49, 201, 17);
            gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 8)) + t7 * 32));
            gm_l2 = t10;
        }
        {
            void * t11 = gm_l1;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 40));
            gm_l3 = t12;
        }
        {
            void * t14 = gm_l3;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            if (t15 != ((uint8_t)35ULL)) goto ie13;
            void * t16 = GM_LD(void *, (((((uint8_t *)t14) + 8) + 8) + 16));
            gm_l4 = t16;
            {
                void * t18 = gm_l4;
                gm_l3 = t18;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t20 = gm_l1;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 40));
            uint8_t t22;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            if (t25 != ((uint8_t)36ULL)) goto mn24;
            t22 = ((uint8_t)1);
            goto me23;
mn24:;
            t22 = ((uint8_t)0);
            goto me23;
mn26:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 204, 12);
me23:;
            if (!t22) goto ie19;
            {
                void * t28 = gm_l0;
                gm_t9qkh0rsooc8d t29;
                GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sq0dqiq83b924));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(8));
                gm_b0_1 t31 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t28, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix19;
ie19:;
            void * t33 = gm_l3;
            uint8_t t34;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            switch (t40) { case ((uint8_t)0ULL): goto ma38; case ((uint8_t)1ULL): goto ma38; case ((uint8_t)2ULL): goto ma38; case ((uint8_t)3ULL): goto ma38; case ((uint8_t)4ULL): goto ma38; case ((uint8_t)5ULL): goto ma38; case ((uint8_t)6ULL): goto ma38; case ((uint8_t)7ULL): goto ma38; case ((uint8_t)8ULL): goto ma38; case ((uint8_t)9ULL): goto ma38; case ((uint8_t)10ULL): goto ma38; case ((uint8_t)11ULL): goto ma38; case ((uint8_t)12ULL): goto ma38; case ((uint8_t)13ULL): goto ma38; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma38; case ((uint8_t)16ULL): goto ma38; case ((uint8_t)17ULL): goto ma38; case ((uint8_t)18ULL): goto ma38; case ((uint8_t)19ULL): goto ma38; case ((uint8_t)20ULL): goto ma38; case ((uint8_t)21ULL): goto ma38; case ((uint8_t)22ULL): goto ma38; case ((uint8_t)23ULL): goto ma38; case ((uint8_t)24ULL): goto ma38; case ((uint8_t)25ULL): goto ma38; case ((uint8_t)26ULL): goto ma38; case ((uint8_t)27ULL): goto ma38; case ((uint8_t)28ULL): goto ma38; case ((uint8_t)29ULL): goto ma38; case ((uint8_t)30ULL): goto ma38; case ((uint8_t)31ULL): goto ma38; case ((uint8_t)32ULL): goto ma36; case ((uint8_t)33ULL): goto ma37; case ((uint8_t)34ULL): goto ma38; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma38; case ((uint8_t)37ULL): goto ma38; case ((uint8_t)38ULL): goto ma38; case ((uint8_t)39ULL): goto ma38; case ((uint8_t)40ULL): goto ma38; case ((uint8_t)41ULL): goto ma38; case ((uint8_t)42ULL): goto ma38; case ((uint8_t)43ULL): goto ma38; case ((uint8_t)44ULL): goto ma38; case ((uint8_t)45ULL): goto ma38; case ((uint8_t)46ULL): goto ma38; case ((uint8_t)47ULL): goto ma38; case ((uint8_t)48ULL): goto ma38; case ((uint8_t)49ULL): goto ma38; case ((uint8_t)50ULL): goto ma38; default: goto ma38; }
ma36:;
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            if (t42 != ((uint8_t)32ULL)) goto mn41;
            t34 = ((uint8_t)1);
            goto me35;
mn41:;
ma37:;
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t33) + 8) + 0));
            if (t44 != ((uint8_t)33ULL)) goto mn43;
            t34 = ((uint8_t)1);
            goto me35;
mn43:;
ma38:;
            t34 = ((uint8_t)0);
            goto me35;
mn45:;
ma39:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 206, 19);
me35:;
            if (!t34) goto ie32;
            {
                void * t47 = gm_l0;
                void * t48 = gm_l1;
                gm_b0_1 t49 = gm_f_top_level_name_dv4kpc9((void *)NULL, cx, NULL, t47, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix32;
ie32:;
            {
                gm_t9qkh0rsooc8d t51;
                void * t53 = gm_l1;
                uint8_t t54 = ((GM_LD(uint8_t, (((uint8_t *)t53) + 32) + 0) == ((uint8_t)0ULL)));
                if (!t54) goto ie52;
                gm_t9qkh0rsooc8d t56;
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)2ULL));
                gm_t9qkh0rsooc8d t57 = t56;
                t51 = t57;
                goto ix52;
ie52:;
                gm_t9qkh0rsooc8d t59;
                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)1ULL));
                gm_t9qkh0rsooc8d t60 = t59;
                t51 = t60;
ix52:;
                gm_l5 = t51;
            }
            {
                void * t61 = gm_l0;
                gm_t9qkh0rsooc8d t62 = gm_l5;
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sjg1ri23hm753));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(5));
                gm_b0_1 t64 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t61, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)), t62, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix32:;
ix19:;
        }
        {
            uint32_t t65 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
            gm_l6 = t65;
        }
        {
            void * t67 = gm_l1;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 24));
            void * t69 = t68;
            if (!(GM_LD(void *, ((uint8_t *)&t69)) != 0)) goto ie66;
            void * t70 = GM_LD(void *, ((uint8_t *)&t69));
            gm_l7 = t70;
            {
                void * t72 = gm_l7;
                uint32_t t73 = GM_LD(uint32_t, ((((uint8_t *)t72) + 72) + 8));
                gm_l6 = t73;
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            void * t74 = gm_l0;
            uint32_t t75 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 0));
            uint32_t t76 = gm_l6;
            void * t77 = gm_l1;
            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 40));
            uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)t78) + 72) + 4));
            gm_tl874t2atnlh3 t80;
            GM_ST(uint32_t, ((uint8_t *)&t80 + 0), t75);
            GM_ST(uint32_t, ((uint8_t *)&t80 + 4), t76);
            GM_ST(uint32_t, ((uint8_t *)&t80 + 8), t79);
            gm_toceffb697p0u t81 = gm_f_binding_op_gcp8f8i(t74, GM_CONV(gm_cseahvg8v7p0vs, t80));
            gm_l8 = t81;
        }
        {
            void * t82 = gm_l0;
            void * t83 = gm_l1;
            void * t84 = GM_LD(void *, (((uint8_t *)t83) + 40));
            gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
            gm_b16_8 t86 = gm_f_text_kspioh6(t82, GM_CONV(gm_cseahvg8v7p0vs, t85));
            gm_l9 = t86;
        }
        {
            gm_tb9j2bleq9j8f t87;
            memset((uint8_t *)&t87 + 2, 0, 2);
            memset((uint8_t *)&t87 + 23, 0, 1);
            void * t88 = gm_l1;
            uint8_t t89 = ((GM_LD(uint8_t, (((uint8_t *)t88) + 32) + 0) == ((uint8_t)1ULL)));
            GM_ST(uint8_t, (uint8_t *)&t87 + 0, t89);
            void * t90 = gm_l1;
            void * t91 = GM_LD(void *, (((uint8_t *)t90) + 24));
            void * t92 = t91;
            uint8_t t93;
            if ((GM_LD(void *, ((uint8_t *)&t92)) != 0)) goto mn95;
            t93 = ((uint8_t)0);
            goto me94;
mn95:;
            t93 = ((uint8_t)1);
            goto me94;
mn96:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 221, 20);
me94:;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, t93);
            gm_toceffb697p0u t97 = gm_l8;
            GM_ST(gm_toceffb697p0u, (uint8_t *)&t87 + 4, t97);
            uint8_t t98 = 0;
            uint8_t t99 = 0;
            uint8_t t100 = 0;
            uint8_t t101 = 0;
            uint8_t t102 = 0;
            void * t103 = gm_l1;
            void * t104 = GM_LD(void *, (((uint8_t *)t103) + 40));
            uint8_t t105 = gm_f_is_literal_9ac2fdt(t104);
            if (!t105) {
                void * t106 = gm_l1;
                void * t107 = GM_LD(void *, (((uint8_t *)t106) + 40));
                uint8_t t108 = gm_f_is_constant_call_vj2jgti(t107);
                t102 = t108;
            } else t102 = 1;
            if (!t102) {
                gm_b16_8 t109 = gm_l9;
                gm_b16_8 t110 = t109;
                uint8_t t111 = 0;
                uint8_t t112 = ((uint8_t)64ULL);
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t110) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t110))) + i * 1, ((uint8_t *)&t112))) { t111 = 1; break; }
                uint8_t t113 = (uint8_t)(t111);
                t101 = t113;
            } else t101 = 1;
            if (!t101) {
                gm_b16_8 t114 = gm_l9;
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sk754mj3p1el6));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(1));
                uint8_t t116 = gm_f_starts_with_6vvt01e(t114, t115);
                t100 = t116;
            } else t100 = 1;
            if (!t100) {
                gm_b16_8 t117 = gm_l9;
                gm_b16_8 t118;
                GM_ST(void *, (uint8_t *)&t118, (void *)(gm_srs1vrn2uaams));
                GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(1));
                uint8_t t119 = gm_f_starts_with_6vvt01e(t117, t118);
                t99 = t119;
            } else t99 = 1;
            if (!t99) {
                gm_b16_8 t120 = gm_l9;
                gm_b16_8 t121;
                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sfqk3rro4g7t0));
                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(1));
                uint8_t t122 = gm_f_starts_with_6vvt01e(t120, t121);
                t98 = t122;
            } else t98 = 1;
            GM_ST(uint8_t, (uint8_t *)&t87 + 20, t98);
            void * t123 = gm_l1;
            void * t124 = GM_LD(void *, (((uint8_t *)t123) + 40));
            uint8_t t125;
            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t124) + 8) + 0));
            if (t128 != ((uint8_t)36ULL)) goto mn127;
            t125 = ((uint8_t)1);
            goto me126;
mn127:;
            t125 = ((uint8_t)0);
            goto me126;
mn129:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 224, 21);
me126:;
            GM_ST(uint8_t, (uint8_t *)&t87 + 21, t125);
            void * t130 = gm_l1;
            void * t131 = GM_LD(void *, (((uint8_t *)t130) + 40));
            uint8_t t132 = ((GM_LD(uint8_t, (((uint8_t *)t131) + 8) + 0) == ((uint8_t)5ULL)));
            GM_ST(uint8_t, (uint8_t *)&t87 + 22, t132);
            gm_tl5vm0tcqvdvb t133;
            GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t133 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t133 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb9j2bleq9j8f, ((uint8_t *)&t133 + 8), t87);
            F.l10 = t133;
        }
        {
            void * t134 = gm_l0;
            void * t135 = (void *)((((uint8_t *)t134) + 8) + 40);
            gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
            gm_tftb1odc739t5 t137 = gm_f_put_amueool(t135, GM_CONV(gm_cseahvg8v7p0vs, t136), &(*(gm_tl5vm0tcqvdvb *)((uint8_t *)&F.l10)));
            gm_tftb1odc739t5 t138 = t137;
            if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 1, gm_src10tru552hn49, 227, 9);
            gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 1));
        }
        goto ix2;
ie2:;
        {
            {
                void * t142 = gm_l1;
                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)t142) + 8));
                gm_l11 = t143;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t145 = gm_l12;
                    uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t147 = (t145 < t146);
                    if (!t147) goto lb144;
                    {
                        uint64_t t150 = gm_l12;
                        uint64_t t151 = (uintptr_t)t150;
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t08olq97rscn3 t155 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t151 * 32));
                        gm_l13 = t155;
                        {
                            void * t157 = gm_l0;
                            gm_t9qkh0rsooc8d t158;
                            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)1ULL));
                            gm_b16_8 t159;
                            GM_ST(void *, (uint8_t *)&t159, (void *)(gm_sjg1ri23hm753));
                            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(5));
                            gm_b0_1 t160 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t157, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l13)), t158, t159);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                        }
                        {
                            void * t161 = gm_l0;
                            void * t162 = (void *)((((uint8_t *)t161) + 8) + 40);
                            gm_tl874t2atnlh3 t163 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                            gm_tbgd14esf0jvi t164;
                            memset((uint8_t *)&t164 + 12, 0, 4);
                            gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t164 + 0, t165);
                            gm_b16_8 t166;
                            GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(1));
                            GM_ST(gm_b16_8, (uint8_t *)&t164 + 16, t166);
                            gm_tl5vm0tcqvdvb t167;
                            GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t167 + 40, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t167 + 8), t164);
                            gm_tftb1odc739t5 t168 = gm_f_put_amueool(t162, GM_CONV(gm_cseahvg8v7p0vs, t163), &t167);
                            gm_tftb1odc739t5 t169 = t168;
                            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_src10tru552hn49, 231, 13);
                            gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
                        }
                        goto me153;
mn154:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 229, 9);
me153:;
                    }
lc144:;
                    {
                        uint64_t t172 = gm_l12;
                        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
                        uint64_t t175 = r173;
                        gm_l12 = t175;
                    }
                }
gm_ll148:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb144; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc144; }
                }
                goto gm_lad;
lb144:;
            }
        }
ix2:;
    }
    {
        void * t177 = gm_l1;
        void * t178 = GM_LD(void *, (((uint8_t *)t177) + 24));
        void * t179 = t178;
        if (!(GM_LD(void *, ((uint8_t *)&t179)) != 0)) goto ie176;
        void * t180 = GM_LD(void *, ((uint8_t *)&t179));
        gm_l14 = t180;
        {
            void * t182 = gm_l0;
            void * t183 = gm_l14;
            gm_b0_1 t184 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix176;
ie176:;
ix176:;
    }
    {
        void * t185 = gm_l0;
        gm_b16_8 t186;
        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s3i3n5bvpvmmi));
        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(15));
        void * t187 = gm_l1;
        void * t188 = GM_LD(void *, (((uint8_t *)t187) + 40));
        gm_b0_1 t189 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t185, t186, t188, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t190 = gm_l0;
        void * t191 = gm_l1;
        void * t192 = GM_LD(void *, (((uint8_t *)t191) + 40));
        gm_b0_1 t193 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t190, t192);
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
inline __attribute__((aligned(4))) uint8_t gm_f_blank_ineorlq(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_blank_ineorlq F;
    gm_b16_8 gm_l0; /* t */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l3 = t17;
                    {
                        uint8_t t20 = gm_l3;
                        uint8_t t21 = gm_f_is_space_uen38t5(t20);
                        uint8_t t22 = (uint8_t)!t21;
                        if (!t22) goto ie19;
                        uint8_t t23 = gm_l3;
                        uint8_t t24 = (!(t23 == ((uint8_t)123ULL)));
                        if (!t24) goto ie19;
                        uint8_t t25 = gm_l3;
                        uint8_t t26 = (!(t25 == ((uint8_t)125ULL)));
                        if (!t26) goto ie19;
                        {
                            uint8_t t28 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 670, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l2;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l2 = t33;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t34 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_block_btrailr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_btrailr F;
    void * gm_l0; /* w */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* last */
    uint8_t gm_l6; /* is_err */
    gm_b16_8 gm_l7; /* msg */
    GM_STACK_CHECK(&F, 594u);
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
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l1;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 8) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src10tru552hn49, 310, 39);
                        void * t22 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 8)) + t19 * 144);
                        gm_b0_1 t23 = gm_f_stmt_j3ls0p3((void *)NULL, cx, NULL, t17, t22);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 310, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l3 = t28;
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
        void * t30 = gm_l1;
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 8) + 8);
        uint8_t t32 = (t31 > ((uint64_t)0ULL));
        if (!t32) goto ie29;
        {
            void * t34 = gm_l1;
            uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 8) + 8);
            uint64_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint64_t)1ULL), &r36);
            if (o37) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 312, 26);
            uint64_t t38 = r36;
            uint64_t t39 = (uintptr_t)t38;
            void * t40 = gm_l1;
            uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 8) + 8);
            if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_src10tru552hn49, 312, 18);
            void * t42 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t40) + 8)) + t39 * 144);
            gm_l5 = t42;
        }
        {
            void * t44 = gm_l5;
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
            if (t45 != ((uint8_t)3ULL)) goto ie43;
            uint8_t t46 = GM_LD(uint8_t, (((((uint8_t *)t44) + 8) + 8) + 0));
            gm_l6 = t46;
            {
                gm_b16_8 t48;
                uint8_t t50 = gm_l6;
                if (!t50) goto ie49;
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s5gkblcfbl1mv));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(120));
                gm_b16_8 t53 = t52;
                t48 = t53;
                goto ix49;
ie49:;
                gm_b16_8 t55;
                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s5cr5924ev095));
                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(64));
                gm_b16_8 t56 = t55;
                t48 = t56;
ix49:;
                gm_l7 = t48;
            }
            {
                void * t57 = gm_l0;
                void * t58 = gm_l5;
                gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 128));
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sob1n7uh7kpps));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(13));
                gm_b16_8 t61 = gm_l7;
                gm_t4gut2c9fj1kt t62;
                gm_f_lint_diag_4vra4qu((void *)&t62, (void *)NULL, cx, NULL, t59, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t63 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t57, &t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix43;
ie43:;
ix43:;
        }
        goto ix29;
ie29:;
ix29:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_capture_abs7k4r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_capture_abs7k4r F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    uint8_t gm_l2; /* removable */
    gm_b16_8 gm_l3; /* src */
    gm_b16_8 gm_l5; /* trimmed */
    gm_b16_8 gm_l6; /* rest */
    uint64_t gm_l7; /* k */
    uint64_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 709u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjg1ri23hm753));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        gm_b0_1 t5 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t2, &(*(gm_t08olq97rscn3 *)((uint8_t *)a1)), t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 0));
        void * t9 = gm_f_file_o15fqku(t7, t8);
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 16) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 16)));
        uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 8));
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        if (t14 > (uintptr_t)t10 || (uintptr_t)t10 > (uintptr_t)t10) gm_trap_slice(t14, (uintptr_t)t10, t10, gm_src10tru552hn49, 294, 12);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)((uintptr_t)t10 - t14));
        gm_l3 = t15;
    }
    {
        gm_tnvnsr1bfvtbd t16;
        memset(&t16, 0, sizeof t16);
        F.l4 = t16;
    }
    {
        gm_b16_8 t17 = gm_l3;
        gm_b16_8 t18 = gm_f_trim_start_179crak(t17);
        gm_l5 = t18;
    }
    {
        uint8_t t20 = gm_l2;
        if (!t20) goto ie19;
        gm_b16_8 t21 = gm_l5;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sm8obe455p2d9));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
        uint8_t t23 = gm_f_starts_with_6vvt01e(t21, t22);
        if (!t23) goto ie19;
        {
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)));
            uint64_t t27 = (uintptr_t)((uint64_t)2ULL);
            if (t27 > (uintptr_t)t25 || (uintptr_t)t25 > (uintptr_t)t25) gm_trap_slice(t27, (uintptr_t)t25, t25, gm_src10tru552hn49, 298, 17);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t26 + t27 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)((uintptr_t)t25 - t27));
            gm_l6 = t28;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t30 = gm_l7;
                uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                uint64_t t33 = gm_l7;
                uint64_t t34 = (uintptr_t)t33;
                uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t34 >= (uintptr_t)(t35)) gm_trap_index(t34, t35, gm_src10tru552hn49, 300, 32);
                uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t34 * 1));
                gm_tqs5p4n2tq19v t37;
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t37 + 1), ((uint8_t)9ULL));
                gm_tqs5p4n2tq19v t38 = t37;
                uint8_t t39 = 0;
                uint8_t t40 = t36;
                if (gm_eqjmccqdb90f(((uint8_t *)&t38) + 0, ((uint8_t *)&t40))) t39 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t38) + 1, ((uint8_t *)&t40))) t39 = 1;
                uint8_t t41 = (uint8_t)(t39);
                if (!t41) goto lb29;
                {
                    uint64_t t44 = gm_l7;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    if (o46) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 300, 57);
                    uint64_t t47 = r45;
                    gm_l7 = t47;
                }
lc29:;
            }
lb29:;
        }
        {
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint64_t t50 = gm_l7;
            uint64_t r51; bool o52 = __builtin_sub_overflow(t49, t50, &r51);
            if (o52) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 301, 27);
            uint64_t t53 = r51;
            uint64_t r54; bool o55 = __builtin_sub_overflow(t48, t53, &r54);
            if (o55) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 301, 16);
            uint64_t t56 = r54;
            gm_l8 = t56;
        }
        {
            gm_tbgd14esf0jvi t57;
            memset((uint8_t *)&t57 + 12, 0, 4);
            uint32_t t58 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 0));
            uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 4));
            uint32_t t60 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 8));
            uint64_t t61 = gm_l8;
            uint32_t t62 = (uint32_t)t61;
            uint32_t r63; bool o64 = __builtin_add_overflow(t60, t62, &r63);
            if (o64) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 302, 76);
            uint32_t t65 = r63;
            gm_tl874t2atnlh3 t66;
            GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t58);
            GM_ST(uint32_t, ((uint8_t *)&t66 + 4), t59);
            GM_ST(uint32_t, ((uint8_t *)&t66 + 8), t65);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t57 + 0, t66);
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t67);
            gm_tnvnsr1bfvtbd t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 36, 0);
            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t68), t57);
            ((uint8_t *)&t68)[32] = 1;
            F.l4 = t68;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t69 = gm_l0;
        void * t70 = (void *)((((uint8_t *)t69) + 8) + 40);
        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_t5jnq73fben73 t72;
        memcpy((uint8_t *)&t72 + 0, ((uint8_t *)&F.l4), 40);
        gm_tl5vm0tcqvdvb t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t73 + 8), t72);
        gm_tftb1odc739t5 t74 = gm_f_put_amueool(t70, GM_CONV(gm_cseahvg8v7p0vs, t71), &t73);
        gm_tftb1odc739t5 t75 = t74;
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src10tru552hn49, 304, 5);
        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_name_hf7dide(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3) {
    struct gm_fr_check_name_hf7dide F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    gm_t9qkh0rsooc8d gm_l2; /* case */
    gm_b16_8 gm_l3; /* what */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* want */
    GM_STACK_CHECK(&F, 650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_l4 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_src10tru552hn49, 185, 25);
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t6 * 1));
            uint8_t t9 = ((t8 == ((uint8_t)95ULL)));
            if (!t9) goto lb3;
            {
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t13 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                if (t14 > (uintptr_t)t12 || (uintptr_t)t12 > (uintptr_t)t12) gm_trap_slice(t14, (uintptr_t)t12, t12, gm_src10tru552hn49, 185, 43);
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t13 + t14 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)((uintptr_t)t12 - t14));
                gm_l4 = t15;
            }
lc3:;
        }
lb3:;
    }
    {
        uint8_t t17 = 0;
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
        if (!t19) {
            gm_t9qkh0rsooc8d t20 = gm_l2;
            gm_b16_8 t21 = gm_l4;
            uint8_t t22 = gm_f_accepts_3h4b27e(t20, t21);
            t17 = t22;
        } else t17 = 1;
        if (!t17) goto ie16;
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
    {
        gm_b16_8 t26;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma30; default: goto ma31; }
ma28:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t34 != ((uint8_t)0ULL)) goto mn33;
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sr1bm2v312fv6));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(14));
        t26 = t35;
        goto me27;
mn33:;
ma29:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t37 != ((uint8_t)1ULL)) goto mn36;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_suls9doo5jh0i));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(10));
        t26 = t38;
        goto me27;
mn36:;
ma30:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_spg4ktuno2hf7));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(50));
        t26 = t41;
        goto me27;
mn39:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 187, 13);
me27:;
        gm_l5 = t26;
    }
    {
        gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sqmr87iegdcns));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(6));
        gm_tfeab279c16rr t44;
        gm_b16_8 t45 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 16, t46);
        gm_b16_8 t47 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 32, t47);
        gm_tabb710d9mq2e t48;
        gm_f_format_t472cpe((void *)&t48, (void *)NULL, cx, NULL, &t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_src10tru552hn49, 192, 41);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_b16_8 t51 = t50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_f_lint_diag_4vra4qu((void *)&F.l6, (void *)NULL, cx, NULL, t42, t43, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t53 = (void *)((uint8_t *)&F.l6);
        gm_t9qkh0rsooc8d t54 = gm_l2;
        gm_b16_8 t55 = gm_l4;
        gm_b16_8 t56 = gm_f_convert_c7mdt88((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t57;
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
        gm_tabb710d9mq2e t58;
        gm_f_format_ooibcq2((void *)&t58, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_src10tru552hn49, 193, 16);
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
        gm_b16_8 t61 = t60;
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_f_add_note_46v502s(t53, t62);
    }
    {
        void * t63 = gm_l0;
        gm_b0_1 t64 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t63, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collapsible_else_ehhjtii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collapsible_else_ehhjtii F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    void * gm_l4; /* inner */
    uint32_t gm_l6; /* file */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    gm_b16_8 gm_l9; /* inner_text */
    gm_b16_8 gm_l10; /* t */
    GM_STACK_CHECK(&F, 861u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t9t5bnrf784qu t3 = GM_LD(gm_t9t5bnrf784qu, (((uint8_t *)t2) + 16));
        gm_t9t5bnrf784qu t4 = t3;
        gm_ttja7ii61nila t5;
        if (((((uint8_t *)&t4))[48] != 0)) {
            gm_ttja7ii61nila t6 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        F.l2 = t5;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t10 != ((uint8_t)0ULL)) goto ie9;
        gm_t79rbdv4rafe5 t11 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l2) + 8));
        F.l3 = t11;
        {
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 8) + 8);
            uint8_t t15 = (!(t14 == ((uint64_t)1ULL)));
            if (!t15) goto ie13;
            {
                gm_b0_1 t17;
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 8) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src10tru552hn49, 654, 21);
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 8)) + t20 * 144) + 8) + 0));
            if (t22 != ((uint8_t)2ULL)) goto ie19;
            void * t23 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 8)) + t20 * 144) + 8) + 8));
            gm_l4 = t23;
            {
                void * t26 = gm_l4;
                uint8_t t27;
                uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
                if (t30 != ((uint8_t)40ULL)) goto mn29;
                t27 = ((uint8_t)0);
                goto me28;
mn29:;
                t27 = ((uint8_t)1);
                goto me28;
mn31:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 655, 8);
me28:;
                if (!t27) goto ie25;
                {
                    gm_b0_1 t33;
                    uint8_t t34 = 0;
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sotb03b2eqtp5));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(19));
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s816n04ro16nc));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(47));
                gm_f_lint_diag_4vra4qu((void *)&F.l5, (void *)NULL, cx, NULL, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 24) + 0));
                gm_l6 = t38;
            }
            {
                void * t39 = gm_l0;
                uint32_t t40 = gm_l6;
                uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 24) + 4));
                void * t42 = gm_l4;
                uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)t42) + 72) + 4));
                gm_tl874t2atnlh3 t44;
                GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t40);
                GM_ST(uint32_t, ((uint8_t *)&t44 + 4), t41);
                GM_ST(uint32_t, ((uint8_t *)&t44 + 8), t43);
                gm_b16_8 t45 = gm_f_text_kspioh6(t39, GM_CONV(gm_cseahvg8v7p0vs, t44));
                gm_l7 = t45;
            }
            {
                void * t46 = gm_l0;
                uint32_t t47 = gm_l6;
                void * t48 = gm_l4;
                uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)t48) + 72) + 8));
                uint32_t t50 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 24) + 8));
                gm_tl874t2atnlh3 t51;
                GM_ST(uint32_t, ((uint8_t *)&t51 + 0), t47);
                GM_ST(uint32_t, ((uint8_t *)&t51 + 4), t49);
                GM_ST(uint32_t, ((uint8_t *)&t51 + 8), t50);
                gm_b16_8 t52 = gm_f_text_kspioh6(t46, GM_CONV(gm_cseahvg8v7p0vs, t51));
                gm_l8 = t52;
            }
            {
                void * t53 = gm_l0;
                void * t54 = gm_l4;
                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
                gm_b16_8 t56 = gm_f_text_kspioh6(t53, GM_CONV(gm_cseahvg8v7p0vs, t55));
                gm_l9 = t56;
            }
            {
                gm_b16_8 t58 = gm_l7;
                uint8_t t59 = gm_f_blank_ineorlq(t58);
                if (!t59) goto ie57;
                gm_b16_8 t60 = gm_l8;
                uint8_t t61 = gm_f_blank_ineorlq(t60);
                if (!t61) goto ie57;
                gm_b16_8 t62 = gm_l9;
                gm_b16_8 t63 = t62;
                uint8_t t64 = 0;
                uint8_t t65 = ((uint8_t)96ULL);
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t63) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t63))) + i * 1, ((uint8_t *)&t65))) { t64 = 1; break; }
                uint8_t t66 = (uint8_t)(!t64);
                if (!t66) goto ie57;
                gm_b16_8 t67 = gm_l9;
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sm7emfdvcb7c2));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(2));
                uint8_t t69 = gm_f_contains_gkki4kq(t67, t68);
                uint8_t t70 = (uint8_t)!t69;
                if (!t70) goto ie57;
                {
                    gm_b16_8 t72 = gm_l9;
                    void * t73 = gm_l0;
                    void * t74 = gm_l4;
                    gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 72));
                    uint64_t t76 = gm_f_indent_of_p7r0sg5(t73, GM_CONV(gm_cseahvg8v7p0vs, t75));
                    void * t77 = gm_l0;
                    gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                    uint64_t t79 = gm_f_indent_of_p7r0sg5(t77, GM_CONV(gm_cseahvg8v7p0vs, t78));
                    gm_b16_8 t80 = gm_f_dedent_aa5vbel((void *)NULL, cx, NULL, t72, t76, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t80;
                }
                {
                    void * t81 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t82;
                    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sfna4d5mnmoa8));
                    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(29));
                    gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
                    gm_b16_8 t84 = gm_l10;
                    gm_b0_1 t85 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t81, t82, t83, t84);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix57;
ie57:;
ix57:;
            }
            {
                void * t86 = gm_l0;
                gm_b0_1 t87 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t86, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix19;
ie19:;
            {
                gm_b0_1 t89;
                uint8_t t90 = 0;
                F.ret = t89;
                goto gm_fret;
            }
ix19:;
        }
        goto ix9;
ie9:;
        {
            gm_b0_1 t92;
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
ix9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_cond_e0272rd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_cond_e0272rd F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    uint8_t gm_l2; /* head */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l7; /* value */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tq5j0547p8452 t16 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 80));
                    F.l5 = t16;
                    {
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t20 != ((uint8_t)0ULL)) goto mn19;
                        gm_t0d02142k4tu8 t21 = GM_LD(gm_t0d02142k4tu8, ((((uint8_t *)&F.l5) + 8) + 0));
                        F.l6 = t21;
                        void * t22 = GM_LD(void *, ((((uint8_t *)&F.l5) + 8) + 64));
                        gm_l7 = t22;
                        {
                            void * t24 = gm_l0;
                            void * t25 = (void *)((uint8_t *)&F.l6);
                            gm_t9qkh0rsooc8d t26;
                            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
                            gm_b0_1 t27 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t24, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            void * t28 = gm_l0;
                            void * t29 = gm_l7;
                            gm_b0_1 t30 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t28, t29);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn19:;
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                        if (t32 != ((uint8_t)1ULL)) goto mn31;
                        void * t33 = GM_LD(void *, (((uint8_t *)&F.l5) + 8));
                        gm_l8 = t33;
                        {
                            void * t36 = gm_l1;
                            uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
                            uint8_t t38 = ((t37 == ((uint64_t)1ULL)));
                            if (!t38) goto ie35;
                            {
                                void * t40 = gm_l0;
                                gm_b16_8 t41;
                                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sjuj50mlj6mg0));
                                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(13));
                                void * t42 = gm_l8;
                                uint8_t t43 = gm_l2;
                                gm_b0_1 t44 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t40, t41, t42, t43);
                                if (gm_unw.kind) {
                                    goto gm_ll9;
                                }
                            }
                            goto ix35;
ie35:;
ix35:;
                        }
                        {
                            void * t45 = gm_l0;
                            void * t46 = gm_l8;
                            gm_b0_1 t47 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t45, t46);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 372, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 371, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t49 = gm_l4;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l4 = t52;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_convert_c7mdt88(void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, gm_b16_8 a1) {
    struct gm_fr_convert_c7mdt88 F;
    gm_t9qkh0rsooc8d gm_l0; /* case */
    gm_b16_8 gm_l1; /* s */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    uint8_t gm_l8; /* <tmp> */
    uint8_t gm_l9; /* prev_lower */
    uint8_t gm_l10; /* next_lower */
    uint8_t gm_l11; /* ends_us */
    GM_STACK_CHECK(&F, 584u);
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
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t4 != ((uint8_t)0ULL)) goto mn3;
        {
            {
                gm_b16_8 t7 = gm_l1;
                gm_l3 = t7;
            }
            gm_hval t8 = ((gm_hval){(void *)&F.h, cx});
            gm_node wn9 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hldg2pcvebta, .env = t8.env, .hctx = t8.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
            gm_b16_8 t12 = gm_l3;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
            gm_b0_1 t14 = gm_f_split_obtinjv_s44sj5s((void *)NULL, (&wn9), NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_ll11;
            }
            F.w0 = t14;
            goto gm_ls15;
gm_ll11:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we10; }
            goto gm_lad;
gm_ls15:;
we10:;
            gm_b0_1 t16 = F.w0;
            gm_b0_1 t17 = t16;
        }
        goto me2;
mn3:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t21 != ((uint8_t)1ULL)) goto pa20;
        goto po19;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t23 != ((uint8_t)2ULL)) goto pa22;
        goto po19;
pa22:;
        goto mn18;
po19:;
        {
            {
                gm_b16_8 t26 = gm_l1;
                gm_l4 = t26;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t30 = (t28 < t29);
                    if (!t30) goto lb27;
                    {
                        uint64_t t33 = gm_l5;
                        uint64_t t34 = gm_l5;
                        uint64_t t35 = (uintptr_t)t34;
                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t35 * 1));
                        gm_tf5srh4mp4b3i t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t33);
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 8), t37);
                        gm_tf5srh4mp4b3i t39 = t38;
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                        gm_l6 = t42;
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 8));
                        gm_l7 = t43;
                        {
                            uint8_t t46 = gm_l7;
                            uint8_t t47 = gm_f_is_upper_68kn81r(t46);
                            if (!t47) goto ie45;
                            {
                                uint8_t t49 = 0;
                                uint64_t t50 = gm_l6;
                                uint8_t t51 = (t50 > ((uint64_t)0ULL));
                                if (t51) {
                                    uint8_t t52 = 0;
                                    uint64_t t53 = gm_l6;
                                    uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
                                    if (o55) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 767, 57);
                                    uint64_t t56 = r54;
                                    uint64_t t57 = (uintptr_t)t56;
                                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                    if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_src10tru552hn49, 767, 55);
                                    uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t57 * 1));
                                    uint8_t t60 = gm_f_is_lower_q12rpq9(t59);
                                    if (!t60) {
                                        {
                                            uint64_t t62 = gm_l6;
                                            uint64_t r63; bool o64 = __builtin_sub_overflow(t62, ((uint64_t)1ULL), &r63);
                                            if (o64) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 767, 70);
                                            uint64_t t65 = r63;
                                            uint64_t t66 = (uintptr_t)t65;
                                            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                            if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_src10tru552hn49, 767, 68);
                                            uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t66 * 1));
                                            gm_l8 = t68;
                                        }
                                        uint8_t t69 = 0;
                                        uint8_t t70 = gm_l8;
                                        uint8_t t71 = (((uint8_t)48ULL) <= t70);
                                        if (t71) {
                                            uint8_t t72 = gm_l8;
                                            uint8_t t73 = (t72 <= ((uint8_t)57ULL));
                                            t69 = t73;
                                        } else t69 = 0;
                                        uint8_t t74 = t69;
                                        t52 = t74;
                                    } else t52 = 1;
                                    t49 = t52;
                                } else t49 = 0;
                                gm_l9 = t49;
                            }
                            {
                                uint8_t t75 = 0;
                                uint64_t t76 = gm_l6;
                                uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                                if (o78) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 768, 35);
                                uint64_t t79 = r77;
                                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                uint8_t t81 = (t79 < t80);
                                if (t81) {
                                    uint64_t t82 = gm_l6;
                                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                                    if (o84) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 768, 64);
                                    uint64_t t85 = r83;
                                    uint64_t t86 = (uintptr_t)t85;
                                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                    if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_src10tru552hn49, 768, 62);
                                    uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                                    uint8_t t89 = gm_f_is_lower_q12rpq9(t88);
                                    t75 = t89;
                                } else t75 = 0;
                                gm_l10 = t75;
                            }
                            {
                                uint8_t t90 = 0;
                                uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                uint8_t t92 = (t91 > ((uint64_t)0ULL));
                                if (t92) {
                                    uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    uint64_t r94; bool o95 = __builtin_sub_overflow(t93, ((uint64_t)1ULL), &r94);
                                    if (o95) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 769, 64);
                                    uint64_t t96 = r94;
                                    uint64_t t97 = (uintptr_t)t96;
                                    uint64_t t98 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                                    if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_src10tru552hn49, 769, 54);
                                    uint8_t t99 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t97 * 1));
                                    uint8_t t100 = ((t99 == ((uint8_t)95ULL)));
                                    t90 = t100;
                                } else t90 = 0;
                                gm_l11 = t90;
                            }
                            {
                                uint64_t t102 = gm_l6;
                                uint8_t t103 = (t102 > ((uint64_t)0ULL));
                                if (!t103) goto ie101;
                                uint8_t t104 = gm_l11;
                                uint8_t t105 = (uint8_t)!t104;
                                if (!t105) goto ie101;
                                uint8_t t106 = 0;
                                uint8_t t107 = gm_l9;
                                if (!t107) {
                                    uint8_t t108 = gm_l10;
                                    t106 = t108;
                                } else t106 = 1;
                                if (!t106) goto ie101;
                                {
                                    void * t110 = (void *)((uint8_t *)&F.l2);
                                    gm_tftb1odc739t5 t111 = gm_f_push_t83rg2m(t110, ((uint8_t)95ULL));
                                    gm_tftb1odc739t5 t112 = t111;
                                    if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_src10tru552hn49, 770, 79);
                                    gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
                                }
                                goto ix101;
ie101:;
ix101:;
                            }
                            {
                                void * t114 = (void *)((uint8_t *)&F.l2);
                                uint8_t t115 = gm_l7;
                                uint8_t r116; bool o117 = __builtin_add_overflow(t115, ((uint8_t)32ULL), &r116);
                                if (o117) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 771, 30);
                                uint8_t t118 = r116;
                                gm_tftb1odc739t5 t119 = gm_f_push_t83rg2m(t114, t118);
                                gm_tftb1odc739t5 t120 = t119;
                                if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_src10tru552hn49, 771, 21);
                                gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                            }
                            goto ix45;
ie45:;
                            {
                                void * t123 = (void *)((uint8_t *)&F.l2);
                                uint8_t t124 = gm_l7;
                                gm_tftb1odc739t5 t125 = gm_f_push_t83rg2m(t123, t124);
                                gm_tftb1odc739t5 t126 = t125;
                                if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_src10tru552hn49, 773, 21);
                                gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
                            }
ix45:;
                        }
                        goto me40;
mn41:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 765, 13);
me40:;
                    }
lc27:;
                    {
                        uint64_t t129 = gm_l5;
                        uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                        uint64_t t132 = r130;
                        gm_l5 = t132;
                    }
                }
lb27:;
            }
        }
        goto me2;
mn18:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 756, 5);
me2:;
    }
    {
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t134 = t133;
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        uint8_t t136 = 0;
        F.ret = t135;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dedent_aa5vbel(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_dedent_aa5vbel F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* to */
    gm_b16_8 gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = gm_l2;
        uint64_t r4; bool o5 = __builtin_sub_overflow(t2, t3, &r4);
        uint64_t t6 = o5 ? (0 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r4;
        F.l3 = t6;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        F.l5 = ((uint8_t)1);
    }
    {
        {
            gm_b16_8 t8 = gm_l0;
            gm_l6 = t8;
        }
        gm_hval t9 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn10 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl7iqllvekj4, .env = t9.env, .hctx = t9.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t13 = gm_l6;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
        gm_b0_1 t15 = gm_f_split_obtinjv_s76o7tg((void *)NULL, (&wn10), NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_ll12;
        }
        F.w0 = t15;
        goto gm_ls16;
gm_ll12:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we11; }
        goto gm_lad;
gm_ls16:;
we11:;
        gm_b0_1 t17 = F.w0;
        gm_b0_1 t18 = t17;
    }
    {
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        uint8_t t22 = 0;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_empty_branches_7o4oudt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_empty_branches_7o4oudt F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    void * gm_l2; /* then */
    gm_tl874t2atnlh3 gm_l5; /* prev */
    gm_tl874t2atnlh3 gm_l6; /* span */
    GM_STACK_CHECK(&F, 713u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)1ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l1;
        uint8_t t7;
        if ((((((uint8_t *)t6) + 16))[48] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 630, 32);
me8:;
        if (!t7) goto ie2;
        {
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            void * t13 = gm_l1;
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)t13) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_src10tru552hn49, 631, 18);
            void * t15 = (void *)(((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 0)) + t12 * 72) + 32);
            gm_l2 = t15;
        }
        {
            void * t17 = gm_l2;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 8) + 8);
            uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
            if (!t19) goto ie16;
            void * t20 = gm_l0;
            void * t21 = gm_l2;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 24));
            uint8_t t23 = gm_f_has_comment_qa4dr2o(t20, GM_CONV(gm_cseahvg8v7p0vs, t22));
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto ie16;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l2;
                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 24));
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s5f9u0o3duhns));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(12));
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_shquo2s3jci8a));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(41));
                gm_t4gut2c9fj1kt t31;
                gm_f_lint_diag_4vra4qu((void *)&t31, (void *)NULL, cx, NULL, t28, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t32 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t26, &t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix16;
ie16:;
ix16:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t34 = gm_l1;
        if (!(((((uint8_t *)t34) + 16))[48] != 0)) goto ie33;
        gm_ttja7ii61nila t35 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)t34) + 16));
        F.l3 = t35;
        {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t38 != ((uint8_t)0ULL)) goto ie37;
            gm_t79rbdv4rafe5 t39 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l3) + 8));
            F.l4 = t39;
            {
                uint64_t t42 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 8) + 8);
                uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
                if (!t43) goto ie41;
                void * t44 = gm_l0;
                gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                uint8_t t46 = gm_f_has_comment_qa4dr2o(t44, GM_CONV(gm_cseahvg8v7p0vs, t45));
                uint8_t t47 = (uint8_t)!t46;
                if (!t47) goto ie41;
                {
                    void * t49 = gm_l1;
                    uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 0) + 8);
                    uint64_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint64_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 639, 36);
                    uint64_t t53 = r51;
                    uint64_t t54 = (uintptr_t)t53;
                    void * t55 = gm_l1;
                    uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)t55) + 0) + 8);
                    if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_src10tru552hn49, 639, 25);
                    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t55) + 0)) + t54 * 72) + 32) + 24));
                    gm_l5 = t57;
                }
                {
                    uint32_t t58 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 0));
                    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                    uint32_t t60 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 8));
                    gm_tl874t2atnlh3 t61;
                    GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t58);
                    GM_ST(uint32_t, ((uint8_t *)&t61 + 4), t59);
                    GM_ST(uint32_t, ((uint8_t *)&t61 + 8), t60);
                    gm_l6 = t61;
                }
                {
                    gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s5f9u0o3duhns));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(12));
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sblt6ctj6g4ck));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(20));
                    gm_f_lint_diag_4vra4qu((void *)&F.l7, (void *)NULL, cx, NULL, t62, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t65 = (void *)((uint8_t *)&F.l7);
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s1hha8fc2kd79));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(17));
                    gm_tl874t2atnlh3 t67 = gm_l6;
                    gm_b16_8 t68;
                    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(0));
                    gm_b0_1 t69 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t65, t66, t67, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t70 = gm_l0;
                    gm_b0_1 t71 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t70, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix41;
ie41:;
ix41:;
            }
            goto ix37;
ie37:;
ix37:;
        }
        goto ix33;
ie33:;
ix33:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_expr_3limf78(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_3limf78 F;
    void * gm_l1; /* e */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* operand */
    void * gm_l4; /* inner */
    void * gm_l5; /* x */
    gm_t9qkh0rsooc8d gm_l6; /* op2 */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* t */
    gm_t08olq97rscn3 gm_l11; /* c */
    void * gm_l12; /* sig */
    void * gm_l13; /* f */
    void * gm_l14; /* h */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l18; /* i */
    void * gm_l19; /* m */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* g */
    void * gm_l27; /* wi */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    void * gm_l31; /* on */
    void * gm_l32; /* f */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    void * gm_l37; /* v */
    void * gm_l38; /* x */
    void * gm_l40; /* v */
    GM_STACK_CHECK(&F, 2175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma5; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma5; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma4; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma15; case ((uint8_t)44ULL): goto ma16; case ((uint8_t)45ULL): goto ma17; case ((uint8_t)46ULL): goto ma18; case ((uint8_t)47ULL): goto ma19; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma5; case ((uint8_t)50ULL): goto ma4; default: goto ma20; }
ma4:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)0ULL)) goto pa24;
        goto po23;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)1ULL)) goto pa26;
        goto po23;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)2ULL)) goto pa28;
        goto po23;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)3ULL)) goto pa30;
        goto po23;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)4ULL)) goto pa32;
        goto po23;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)5ULL)) goto pa34;
        goto po23;
pa34:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)6ULL)) goto pa36;
        goto po23;
pa36:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)7ULL)) goto pa38;
        goto po23;
pa38:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)8ULL)) goto pa40;
        goto po23;
pa40:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)38ULL)) goto pa42;
        goto po23;
pa42:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)50ULL)) goto pa44;
        goto po23;
pa44:;
        goto mn22;
po23:;
        goto me3;
mn22:;
ma5:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)9ULL)) goto pa49;
        goto po48;
pa49:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)11ULL)) goto pa51;
        goto po48;
pa51:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t54 != ((uint8_t)15ULL)) goto pa53;
        goto po48;
pa53:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)16ULL)) goto pa55;
        goto po48;
pa55:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)18ULL)) goto pa57;
        goto po48;
pa57:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)24ULL)) goto pa59;
        goto po48;
pa59:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)31ULL)) goto pa61;
        goto po48;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)25ULL)) goto pa63;
        goto po48;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)26ULL)) goto pa65;
        goto po48;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)27ULL)) goto pa67;
        goto po48;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t70 != ((uint8_t)10ULL)) goto pa69;
        goto po48;
pa69:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)13ULL)) goto pa71;
        goto po48;
pa71:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t74 != ((uint8_t)17ULL)) goto pa73;
        goto po48;
pa73:;
        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t76 != ((uint8_t)29ULL)) goto pa75;
        goto po48;
pa75:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t78 != ((uint8_t)19ULL)) goto pa77;
        goto po48;
pa77:;
        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t80 != ((uint8_t)49ULL)) goto pa79;
        goto po48;
pa79:;
        uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t82 != ((uint8_t)20ULL)) goto pa81;
        goto po48;
pa81:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)21ULL)) goto pa83;
        goto po48;
pa83:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t86 != ((uint8_t)22ULL)) goto pa85;
        goto po48;
pa85:;
        uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t88 != ((uint8_t)23ULL)) goto pa87;
        goto po48;
pa87:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t90 != ((uint8_t)28ULL)) goto pa89;
        goto po48;
pa89:;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t92 != ((uint8_t)35ULL)) goto pa91;
        goto po48;
pa91:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t94 != ((uint8_t)48ULL)) goto pa93;
        goto po48;
pa93:;
        goto mn47;
po48:;
        void * t95 = gm_l1;
        gm_closure t96 = ((gm_closure){(void *)gm_f_fn_c8tpla6, (void *)&F.h, cx});
        gm_f_for_each_child_tkrfoqu(t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn47:;
ma6:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t98 != ((uint8_t)12ULL)) goto mn97;
        gm_t9qkh0rsooc8d t99 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t99;
        void * t100 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l3 = t100;
        {
            uint8_t t103 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
            if (!t103) goto ie102;
            {
                void * t105 = gm_l3;
                gm_l4 = t105;
            }
            {
                for (;;) {
                    if (!((uint8_t)1)) goto lb106;
                    {
                        void * t110 = gm_l4;
                        uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
                        if (t111 != ((uint8_t)9ULL)) goto ie109;
                        void * t112 = GM_LD(void *, ((((uint8_t *)t110) + 8) + 8));
                        gm_l5 = t112;
                        {
                            void * t114 = gm_l5;
                            gm_l4 = t114;
                        }
                        goto ix109;
ie109:;
                        {
                            goto lb106;
                        }
ix109:;
                    }
lc106:;
                }
lb106:;
            }
            {
                void * t117 = gm_l4;
                uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t117) + 8) + 0));
                if (t118 != ((uint8_t)12ULL)) goto ie116;
                gm_t9qkh0rsooc8d t119 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t117) + 8) + 8) + 0));
                gm_l6 = t119;
                void * t120 = GM_LD(void *, (((((uint8_t *)t117) + 8) + 8) + 8));
                gm_l7 = t120;
                {
                    uint8_t t123 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l6) + 0) == ((uint8_t)5ULL)));
                    if (!t123) goto ie122;
                    {
                        void * t125 = F.l0;
                        void * t126 = gm_l7;
                        gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t126) + 72));
                        gm_b16_8 t128 = gm_f_text_kspioh6(t125, GM_CONV(gm_cseahvg8v7p0vs, t127));
                        gm_l8 = t128;
                    }
                    {
                        void * t129 = gm_l1;
                        gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t129) + 72));
                        gm_b16_8 t131;
                        GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sfr3tv65k63d4));
                        GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(15));
                        gm_b16_8 t132;
                        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_snh86nvrv0us3));
                        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(18));
                        gm_f_lint_diag_4vra4qu((void *)&F.l9, (void *)NULL, cx, NULL, t130, t131, t132);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t133 = (void *)((uint8_t *)&F.l9);
                        gm_b16_8 t134;
                        GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sci6hkonrjrne));
                        GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(18));
                        void * t135 = gm_l1;
                        gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t135) + 72));
                        void * t137 = gm_l7;
                        gm_b16_8 t138 = gm_l8;
                        gm_b16_8 t139 = gm_f_paren_unless_simple_mhajkgl((void *)NULL, cx, NULL, t137, t138, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b0_1 t140 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t133, t134, t136, t139);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t141 = F.l0;
                        gm_b0_1 t142 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t141, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix122;
ie122:;
ix122:;
                }
                goto ix116;
ie116:;
ix116:;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            void * t143 = F.l0;
            void * t144 = gm_l3;
            gm_b0_1 t145 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t143, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn97:;
ma7:;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t147 != ((uint8_t)14ULL)) goto mn146;
        gm_tioi3jh9v22g5 t148 = GM_LD(gm_tioi3jh9v22g5, ((((uint8_t *)t2) + 8) + 8));
        F.l10 = t148;
        {
            if (!(((((uint8_t *)&F.l10) + 8))[32] != 0)) goto ie150;
            gm_t08olq97rscn3 t151 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l10) + 8));
            gm_l11 = t151;
            {
                void * t153 = F.l0;
                gm_b0_1 t154 = gm_f_capture_abs7k4r((void *)NULL, cx, NULL, t153, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)), ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix150;
ie150:;
ix150:;
        }
        {
            void * t155 = F.l0;
            void * t156 = GM_LD(void *, (((uint8_t *)&F.l10) + 0));
            gm_b0_1 t157 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t158 = F.l0;
            void * t159 = GM_LD(void *, (((uint8_t *)&F.l10) + 48));
            gm_b0_1 t160 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn146:;
ma8:;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t162 != ((uint8_t)30ULL)) goto mn161;
        void * t163 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l12 = t163;
        void * t164 = F.l0;
        void * t165 = gm_l12;
        gm_b0_1 t166 = gm_f_sig_fn4opan((void *)NULL, cx, NULL, t164, t165, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn161:;
ma9:;
        uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t170 != ((uint8_t)32ULL)) goto pa169;
        goto po168;
pa169:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t172 != ((uint8_t)33ULL)) goto pa171;
        goto po168;
pa171:;
        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t174 != ((uint8_t)34ULL)) goto pa173;
        goto po168;
pa173:;
        uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t176 != ((uint8_t)39ULL)) goto pa175;
        goto po168;
pa175:;
        goto mn167;
po168:;
        goto me3;
mn167:;
ma10:;
        uint8_t t179 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t179 != ((uint8_t)36ULL)) goto mn178;
        void * t180 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l13 = t180;
        void * t181 = F.l0;
        void * t182 = gm_l13;
        gm_b0_1 t183 = gm_f_fn_lit_hd5tsqt((void *)NULL, cx, NULL, t181, t182);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn178:;
ma11:;
        uint8_t t185 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t185 != ((uint8_t)37ULL)) goto mn184;
        void * t186 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t186;
        {
            {
                void * t189 = gm_l14;
                gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)t189) + 8));
                gm_l15 = t190;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t192 = gm_l16;
                    uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t194 = (t192 < t193);
                    if (!t194) goto lb191;
                    {
                        uint64_t t197 = gm_l16;
                        uint64_t t198 = (uintptr_t)t197;
                        uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_t99vmhfllumbh t202 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t198 * 72));
                        F.l17 = t202;
                        {
                            void * t204 = F.l0;
                            void * t205 = GM_LD(void *, (((uint8_t *)&F.l17) + 48));
                            gm_b0_1 t206 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t204, t205);
                            if (gm_unw.kind) {
                                goto gm_ll195;
                            }
                        }
                        goto me200;
mn201:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 456, 13);
me200:;
                    }
lc191:;
                    {
                        uint64_t t208 = gm_l16;
                        uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                        uint64_t t211 = r209;
                        gm_l16 = t211;
                    }
                }
gm_ll195:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb191; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc191; }
                }
                goto gm_lad;
lb191:;
            }
        }
        goto me3;
mn184:;
ma12:;
        uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t213 != ((uint8_t)40ULL)) goto mn212;
        void * t214 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l18 = t214;
        void * t215 = F.l0;
        void * t216 = gm_l1;
        void * t217 = gm_l18;
        gm_b0_1 t218 = gm_f_if_expr_k16f2n5((void *)NULL, cx, NULL, t215, t216, t217);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn212:;
ma13:;
        uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t220 != ((uint8_t)41ULL)) goto mn219;
        void * t221 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l19 = t221;
        {
            void * t223 = F.l0;
            gm_b16_8 t224;
            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_s1o0cl13ab3oc));
            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(17));
            void * t225 = gm_l19;
            void * t226 = GM_LD(void *, (((uint8_t *)t225) + 0));
            gm_b0_1 t227 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t223, t224, t226, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t228 = F.l0;
            void * t229 = gm_l19;
            void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
            gm_b0_1 t231 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t228, t230);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t233 = gm_l19;
                gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)t233) + 8));
                gm_l20 = t234;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t236 = gm_l21;
                    uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t238 = (t236 < t237);
                    if (!t238) goto lb235;
                    {
                        uint64_t t241 = gm_l21;
                        uint64_t t242 = (uintptr_t)t241;
                        uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_tndsf0a4f4lj0 t246 = GM_LD(gm_tndsf0a4f4lj0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t242 * 48));
                        F.l22 = t246;
                        {
                            {
                                gm_l24 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t249 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                                gm_l23 = t249;
                            }
                            {
                                for (;;) {
                                    uint64_t t251 = gm_l24;
                                    uint64_t t252 = gm_l23;
                                    uint8_t t253 = (t251 < t252);
                                    if (!t253) goto lb250;
                                    {
                                        uint64_t t256 = gm_l24;
                                        uint64_t t257 = t256;
                                        uint64_t t260 = GM_LD(uint64_t, ((uint8_t *)&t257));
                                        gm_l25 = t260;
                                        {
                                            void * t262 = F.l0;
                                            uint64_t t263 = gm_l25;
                                            uint64_t t264 = (uintptr_t)t263;
                                            uint64_t t265 = GM_LD(uintptr_t, (((uint8_t *)&F.l22) + 0) + 8);
                                            if (t264 >= (uintptr_t)(t265)) gm_trap_index(t264, t265, gm_src10tru552hn49, 463, 55);
                                            void * t266 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l22) + 0)) + t264 * 64);
                                            gm_t9qkh0rsooc8d t267;
                                            GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)1ULL));
                                            gm_b0_1 t268 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t262, t266, t267);
                                            if (gm_unw.kind) {
                                                goto gm_ll254;
                                            }
                                        }
                                        goto me258;
mn259:;
                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 463, 17);
me258:;
                                    }
lc250:;
                                    {
                                        uint64_t t270 = gm_l24;
                                        uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                                        uint64_t t273 = r271;
                                        gm_l24 = t273;
                                    }
                                }
gm_ll254:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb250; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc250; }
                                }
                                goto gm_ll239;
lb250:;
                            }
                        }
                        {
                            void * t275 = GM_LD(void *, (((uint8_t *)&F.l22) + 16));
                            void * t276 = t275;
                            if (!(GM_LD(void *, ((uint8_t *)&t276)) != 0)) goto ie274;
                            void * t277 = GM_LD(void *, ((uint8_t *)&t276));
                            gm_l26 = t277;
                            {
                                void * t279 = F.l0;
                                void * t280 = gm_l26;
                                gm_b0_1 t281 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t279, t280);
                                if (gm_unw.kind) {
                                    goto gm_ll239;
                                }
                            }
                            goto ix274;
ie274:;
ix274:;
                        }
                        {
                            void * t282 = F.l0;
                            void * t283 = GM_LD(void *, (((uint8_t *)&F.l22) + 24));
                            gm_b0_1 t284 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t282, t283);
                            if (gm_unw.kind) {
                                goto gm_ll239;
                            }
                        }
                        goto me244;
mn245:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 462, 13);
me244:;
                    }
lc235:;
                    {
                        uint64_t t286 = gm_l21;
                        uint64_t r287; bool o288 = __builtin_add_overflow(t286, ((uint64_t)1ULL), &r287);
                        uint64_t t289 = r287;
                        gm_l21 = t289;
                    }
                }
gm_ll239:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb235; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc235; }
                }
                goto gm_lad;
lb235:;
            }
        }
        goto me3;
mn219:;
ma14:;
        uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t291 != ((uint8_t)42ULL)) goto mn290;
        void * t292 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l27 = t292;
        {
            {
                gm_l29 = ((uint64_t)0ULL);
            }
            {
                void * t295 = gm_l27;
                uint64_t t296 = GM_LD(uintptr_t, (((uint8_t *)t295) + 40) + 8);
                gm_l28 = t296;
            }
            {
                for (;;) {
                    uint64_t t298 = gm_l29;
                    uint64_t t299 = gm_l28;
                    uint8_t t300 = (t298 < t299);
                    if (!t300) goto lb297;
                    {
                        uint64_t t303 = gm_l29;
                        uint64_t t304 = t303;
                        uint64_t t307 = GM_LD(uint64_t, ((uint8_t *)&t304));
                        gm_l30 = t307;
                        {
                            void * t309 = F.l0;
                            uint64_t t310 = gm_l30;
                            uint64_t t311 = (uintptr_t)t310;
                            void * t312 = gm_l27;
                            uint64_t t313 = GM_LD(uintptr_t, (((uint8_t *)t312) + 40) + 8);
                            if (t311 >= (uintptr_t)(t313)) gm_trap_index(t311, t313, gm_src10tru552hn49, 469, 51);
                            void * t314 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t312) + 40)) + t311 * 88);
                            gm_b0_1 t315 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t309, t314);
                            if (gm_unw.kind) {
                                goto gm_ll301;
                            }
                        }
                        goto me305;
mn306:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 469, 13);
me305:;
                    }
lc297:;
                    {
                        uint64_t t317 = gm_l29;
                        uint64_t r318; bool o319 = __builtin_add_overflow(t317, ((uint64_t)1ULL), &r318);
                        uint64_t t320 = r318;
                        gm_l29 = t320;
                    }
                }
gm_ll301:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb297; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc297; }
                }
                goto gm_lad;
lb297:;
            }
        }
        {
            void * t322 = gm_l27;
            void * t323 = GM_LD(void *, (((uint8_t *)t322) + 56));
            void * t324 = t323;
            if (!(GM_LD(void *, ((uint8_t *)&t324)) != 0)) goto ie321;
            void * t325 = GM_LD(void *, ((uint8_t *)&t324));
            gm_l31 = t325;
            {
                void * t327 = F.l0;
                void * t328 = gm_l31;
                gm_b0_1 t329 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t327, t328);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix321;
ie321:;
ix321:;
        }
        {
            void * t330 = F.l0;
            void * t331 = gm_l27;
            gm_b0_1 t332 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t330, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t331) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t333 = F.l0;
            void * t334 = gm_l27;
            void * t335 = (void *)(((uint8_t *)t334) + 64);
            gm_b0_1 t336 = gm_f_block_btrailr((void *)NULL, cx, NULL, t333, t335);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t337 = F.l0;
            void * t338 = gm_l27;
            gm_b0_1 t339 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t337, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t338) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn290:;
ma15:;
        uint8_t t341 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t341 != ((uint8_t)43ULL)) goto mn340;
        void * t342 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l32 = t342;
        {
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                void * t345 = gm_l32;
                uint64_t t346 = GM_LD(uintptr_t, (((uint8_t *)t345) + 40) + 8);
                gm_l33 = t346;
            }
            {
                for (;;) {
                    uint64_t t348 = gm_l34;
                    uint64_t t349 = gm_l33;
                    uint8_t t350 = (t348 < t349);
                    if (!t350) goto lb347;
                    {
                        uint64_t t353 = gm_l34;
                        uint64_t t354 = t353;
                        uint64_t t357 = GM_LD(uint64_t, ((uint8_t *)&t354));
                        gm_l35 = t357;
                        {
                            void * t359 = F.l0;
                            uint64_t t360 = gm_l35;
                            uint64_t t361 = (uintptr_t)t360;
                            void * t362 = gm_l32;
                            uint64_t t363 = GM_LD(uintptr_t, (((uint8_t *)t362) + 40) + 8);
                            if (t361 >= (uintptr_t)(t363)) gm_trap_index(t361, t363, gm_src10tru552hn49, 476, 49);
                            void * t364 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t362) + 40)) + t361 * 64);
                            gm_t9qkh0rsooc8d t365;
                            GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)1ULL));
                            gm_b0_1 t366 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t359, t364, t365);
                            if (gm_unw.kind) {
                                goto gm_ll351;
                            }
                        }
                        goto me355;
mn356:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 476, 13);
me355:;
                    }
lc347:;
                    {
                        uint64_t t368 = gm_l34;
                        uint64_t r369; bool o370 = __builtin_add_overflow(t368, ((uint64_t)1ULL), &r369);
                        uint64_t t371 = r369;
                        gm_l34 = t371;
                    }
                }
gm_ll351:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb347; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc347; }
                }
                goto gm_lad;
lb347:;
            }
        }
        {
            void * t372 = F.l0;
            gm_b16_8 t373;
            GM_ST(void *, (uint8_t *)&t373, (void *)(gm_shfvspt8ngibo));
            GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(18));
            void * t374 = gm_l32;
            void * t375 = GM_LD(void *, (((uint8_t *)t374) + 56));
            gm_b0_1 t376 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t372, t373, t375, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t377 = F.l0;
            void * t378 = gm_l32;
            void * t379 = GM_LD(void *, (((uint8_t *)t378) + 56));
            gm_b0_1 t380 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t377, t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t381 = F.l0;
            void * t382 = gm_l32;
            gm_b0_1 t383 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t381, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t382) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t384 = F.l0;
            void * t385 = gm_l32;
            void * t386 = (void *)(((uint8_t *)t385) + 64);
            gm_b0_1 t387 = gm_f_block_btrailr((void *)NULL, cx, NULL, t384, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t388 = F.l0;
            void * t389 = gm_l32;
            gm_b0_1 t390 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t388, &(*(gm_t1s75i08qtptd *)(((uint8_t *)t389) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn340:;
ma16:;
        uint8_t t392 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t392 != ((uint8_t)44ULL)) goto mn391;
        gm_t79rbdv4rafe5 t393 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l36 = t393;
        void * t394 = F.l0;
        void * t395 = (void *)((uint8_t *)&F.l36);
        gm_b0_1 t396 = gm_f_block_btrailr((void *)NULL, cx, NULL, t394, t395);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn391:;
ma17:;
        uint8_t t398 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t398 != ((uint8_t)45ULL)) goto mn397;
        void * t399 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l37 = t399;
        {
            void * t402 = gm_l37;
            void * t403 = t402;
            if (!(GM_LD(void *, ((uint8_t *)&t403)) != 0)) goto ie401;
            void * t404 = GM_LD(void *, ((uint8_t *)&t403));
            gm_l38 = t404;
            {
                void * t406 = F.l0;
                gm_b16_8 t407;
                GM_ST(void *, (uint8_t *)&t407, (void *)(gm_sc06resjcdhg4));
                GM_ST(uintptr_t, (uint8_t *)&t407 + 8, (uintptr_t)(18));
                void * t408 = gm_l38;
                gm_b0_1 t409 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t406, t407, t408, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t410 = F.l0;
                void * t411 = gm_l38;
                gm_b0_1 t412 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t410, t411);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix401;
ie401:;
ix401:;
        }
        goto me3;
mn397:;
ma18:;
        uint8_t t414 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t414 != ((uint8_t)46ULL)) goto mn413;
        gm_tchr52g0ema53 t415 = GM_LD(gm_tchr52g0ema53, ((((uint8_t *)t2) + 8) + 8));
        F.l39 = t415;
        {
            void * t417 = F.l0;
            gm_f_use_label_8j0qqam(t417, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l39) + 0)));
        }
        {
            void * t419 = GM_LD(void *, (((uint8_t *)&F.l39) + 40));
            void * t420 = t419;
            if (!(GM_LD(void *, ((uint8_t *)&t420)) != 0)) goto ie418;
            void * t421 = GM_LD(void *, ((uint8_t *)&t420));
            gm_l40 = t421;
            {
                void * t423 = F.l0;
                void * t424 = gm_l40;
                gm_b0_1 t425 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t423, t424);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix418;
ie418:;
ix418:;
        }
        goto me3;
mn413:;
ma19:;
        uint8_t t427 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t427 != ((uint8_t)47ULL)) goto mn426;
        gm_t1s75i08qtptd t428 = GM_LD(gm_t1s75i08qtptd, ((((uint8_t *)t2) + 8) + 8));
        F.l41 = t428;
        void * t429 = F.l0;
        gm_f_use_label_8j0qqam(t429, &(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l41)));
        goto me3;
mn426:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 423, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_8ttf3eb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fields_8ttf3eb F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 717u);
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
                    gm_tfc3oljlofib6 t15 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 80));
                    F.l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_t24301fas1b2a t20 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&F.l4) + 8));
                        F.l5 = t20;
                        {
                            {
                                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                                gm_l6 = t23;
                            }
                            {
                                gm_l7 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t25 = gm_l7;
                                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint8_t t27 = (t25 < t26);
                                    if (!t27) goto lb24;
                                    {
                                        uint64_t t30 = gm_l7;
                                        uint64_t t31 = (uintptr_t)t30;
                                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t31 * 32));
                                        gm_l8 = t35;
                                        {
                                            void * t37 = gm_l0;
                                            gm_t9qkh0rsooc8d t38;
                                            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
                                            gm_b16_8 t39;
                                            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s1jboc8lkvac0));
                                            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(5));
                                            gm_b0_1 t40 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t37, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l8)), t38, t39);
                                            if (gm_unw.kind) {
                                                goto gm_ll28;
                                            }
                                        }
                                        goto me33;
mn34:;
                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 161, 17);
me33:;
                                    }
lc24:;
                                    {
                                        uint64_t t42 = gm_l7;
                                        uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                        uint64_t t45 = r43;
                                        gm_l7 = t45;
                                    }
                                }
gm_ll28:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
                                }
                                goto gm_ll8;
lb24:;
                            }
                        }
                        goto me17;
mn18:;
                        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t47 != ((uint8_t)1ULL)) goto mn46;
                        gm_t7bjbmukv26e7 t48 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l4) + 8));
                        F.l9 = t48;
                        {
                            void * t50 = gm_l0;
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
                            gm_b16_8 t52 = t51;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                            gm_b0_1 t54 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t50, t53);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t55 = gm_l0;
                            gm_th2lqbo77pvsc t56 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l9) + 24));
                            gm_th2lqbo77pvsc t57 = t56;
                            gm_b16_8 t58;
                            if (((((uint8_t *)&t57))[16] != 0)) {
                                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 0));
                                t58 = t59;
                            } else {
                                gm_b16_8 t60 = gm_f_empty_slice_d53jqcq();
                                t58 = t60;
                            }
                            gm_b16_8 t61 = t58;
                            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                            gm_b0_1 t63 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t55, t62);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 159, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 158, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t65 = gm_l3;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l3 = t68;
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
inline __attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_file_facts_tdo30n8 F;
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t3;
        gm_f_hash_map_2auo7cg((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t4;
        gm_f_list_7s4om10((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t5;
        gm_f_list_fu17r8d((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t6;
        gm_f_list_prbqo9j((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 0, t2);
        GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 40, t3);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 96, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 136, t5);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tg0vmr665gcag *)gm_out) + 176, t6);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_c8tpla6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_lit_hd5tsqt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_lit_hd5tsqt F;
    void * gm_l0; /* w */
    void * gm_l1; /* f */
    void * gm_l4; /* e */
    GM_STACK_CHECK(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = (void *)(((uint8_t *)t3) + 0);
        gm_b0_1 t5 = gm_f_sig_fn4opan((void *)NULL, cx, NULL, t2, t4, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t6) + 224));
        F.l2 = t7;
    }
    {
        void * t8 = gm_l0;
        uint8_t *t9 = (((uint8_t *)t8) + 224);
        gm_f_list_uaa66su((void *)&(*(gm_trqfvmjtt4sij *)t9), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t10) + 88) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        gm_t79rbdv4rafe5 t14 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t10) + 88) + 8));
        F.l3 = t14;
        void * t15 = gm_l0;
        void * t16 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t17 = gm_f_block_btrailr((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn12:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t10) + 88) + 0));
        if (t19 != ((uint8_t)1ULL)) goto mn18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t10) + 88) + 8));
        gm_l4 = t20;
        void * t21 = gm_l0;
        void * t22 = gm_l4;
        gm_b0_1 t23 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me11;
mn18:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 523, 5);
me11:;
    }
    {
        void * t24 = gm_l0;
        gm_trqfvmjtt4sij t25 = F.l2;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t24) + 224), t25);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1063u09(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1063u09 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* word */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1) + 0) + 8);
        uint8_t t7 = (t6 > ((uint64_t)0ULL));
        if (!t7) goto ie5;
        {
            void * t9 = (void *)((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1);
            gm_tftb1odc739t5 t10 = gm_f_push_t83rg2m(t9, ((uint8_t)32ULL));
            gm_tftb1odc739t5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src10tru552hn49, 694, 32);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t13 = (void *)((uint8_t *)&((struct gm_fr_normalize_space_v7dp7uh *)((&F.h)->up))->l1);
        gm_b16_8 t14 = gm_l1;
        gm_tftb1odc739t5 t15 = gm_f_extend_0cs85gs(t13, t14);
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_src10tru552hn49, 695, 9);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 693, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8jjs01b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_8jjs01b F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* word */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t11 = (void *)((uint8_t *)&((struct gm_fr_convert_c7mdt88 *)((&F.h)->up))->l2);
        uint8_t t12;
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src10tru552hn49, 760, 38);
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
        uint8_t t17 = gm_f_is_lower_q12rpq9(t16);
        if (!t17) goto ie13;
        uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src10tru552hn49, 760, 49);
        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t19 * 1));
        uint8_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint8_t)32ULL), &r22);
        if (o23) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 760, 49);
        uint8_t t24 = r22;
        uint8_t t25 = t24;
        t12 = t25;
        goto ix13;
ie13:;
        uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_src10tru552hn49, 760, 71);
        uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 1));
        uint8_t t30 = t29;
        t12 = t30;
ix13:;
        gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t11, t12);
        gm_tftb1odc739t5 t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src10tru552hn49, 760, 17);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    {
        void * t34 = (void *)((uint8_t *)&((struct gm_fr_convert_c7mdt88 *)((&F.h)->up))->l2);
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t36 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t37 = (uintptr_t)((uint64_t)1ULL);
        if (t37 > (uintptr_t)t35 || (uintptr_t)t35 > (uintptr_t)t35) gm_trap_slice(t37, (uintptr_t)t35, t35, gm_src10tru552hn49, 761, 28);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t36 + t37 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)((uintptr_t)t35 - t37));
        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t34, t38);
        gm_tftb1odc739t5 t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src10tru552hn49, 761, 17);
        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 758, 13);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_cbj68cn F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    uint64_t gm_l2; /* blanks */
    GM_STACK_CHECK(&F, 271u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l5));
        if (!t6) goto ie5;
        {
            void * t8 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
            gm_b16_8 t9 = gm_l1;
            gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
            gm_tftb1odc739t5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src10tru552hn49, 843, 13);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l5), ((uint8_t)0));
        }
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t15 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
        gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t15, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_src10tru552hn49, 847, 9);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t20 = gm_l2;
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t22 = (t20 < t21);
            if (!t22) goto lb19;
            uint64_t t23 = gm_l2;
            uint64_t t24 = (uintptr_t)t23;
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src10tru552hn49, 849, 37);
            uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 1));
            gm_tqs5p4n2tq19v t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t27 + 1), ((uint8_t)9ULL));
            gm_tqs5p4n2tq19v t28 = t27;
            uint8_t t29 = 0;
            uint8_t t30 = t26;
            if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 0, ((uint8_t *)&t30))) t29 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t28) + 1, ((uint8_t *)&t30))) t29 = 1;
            uint8_t t31 = (uint8_t)(t29);
            if (!t31) goto lb19;
            {
                uint64_t t34 = gm_l2;
                uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 849, 67);
                uint64_t t37 = r35;
                gm_l2 = t37;
            }
lc19:;
        }
lb19:;
    }
    {
        void * t38 = (void *)((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l4);
        gm_b16_8 t39 = gm_l1;
        gm_b16_8 t40 = t39;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&t40) + 8);
        void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t40)));
        uint64_t t43 = gm_l2;
        uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_dedent_aa5vbel *)((&F.h)->up))->l3));
        uint64_t t45 = (t43 < t44 ? t43 : t44);
        uint64_t t46 = (uintptr_t)t45;
        if (t46 > (uintptr_t)t41 || (uintptr_t)t41 > (uintptr_t)t41) gm_trap_slice(t46, (uintptr_t)t41, t41, gm_src10tru552hn49, 850, 20);
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t42 + t46 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)((uintptr_t)t41 - t46));
        gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t38, t47);
        gm_tftb1odc739t5 t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src10tru552hn49, 850, 9);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 841, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_has_comment_qa4dr2o(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_identical_branches_ug6ccol(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_identical_branches_ug6ccol F;
    void * gm_l0; /* w */
    void * gm_l1; /* i */
    gm_b16_8 gm_l4; /* first */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tl874t2atnlh3 gm_l8; /* span */
    GM_STACK_CHECK(&F, 768u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t9t5bnrf784qu t3 = GM_LD(gm_t9t5bnrf784qu, (((uint8_t *)t2) + 16));
        gm_t9t5bnrf784qu t4 = t3;
        gm_ttja7ii61nila t5;
        if (((((uint8_t *)&t4))[48] != 0)) {
            gm_ttja7ii61nila t6 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        F.l2 = t5;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t10 != ((uint8_t)0ULL)) goto ie9;
        gm_t79rbdv4rafe5 t11 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l2) + 8));
        F.l3 = t11;
        {
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 8) + 8);
            uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
            if (!t15) goto ie13;
            {
                gm_b0_1 t17;
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t19 = gm_l0;
            gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t21 = gm_f_text_kspioh6(t19, GM_CONV(gm_cseahvg8v7p0vs, t20));
            gm_b16_8 t22 = gm_f_normalize_space_v7dp7uh((void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t22;
        }
        {
            {
                void * t24 = gm_l1;
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 0));
                gm_l5 = t25;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l6;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t29 = (t27 < t28);
                    if (!t29) goto lb26;
                    {
                        uint64_t t32 = gm_l6;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_tcuumjkg5eni6 t37 = GM_LD(gm_tcuumjkg5eni6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t33 * 72));
                        F.l7 = t37;
                        {
                            void * t40 = gm_l0;
                            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l7) + 32) + 24));
                            gm_b16_8 t42 = gm_f_text_kspioh6(t40, GM_CONV(gm_cseahvg8v7p0vs, t41));
                            gm_b16_8 t43 = gm_f_normalize_space_v7dp7uh((void *)NULL, cx, NULL, t42);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            gm_b16_8 t44 = gm_l4;
                            gm_b16_8 t45 = t43;
                            gm_b16_8 t46 = t44;
                            uint8_t t47 = (!gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
                            if (!t47) goto ie39;
                            {
                                gm_b0_1 t49;
                                uint8_t t50 = 0;
                                F.ret = t49;
                                goto gm_fret;
                            }
                            goto ix39;
ie39:;
ix39:;
                        }
                        goto me35;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 681, 5);
me35:;
                    }
lc26:;
                    {
                        uint64_t t52 = gm_l6;
                        uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                        uint64_t t55 = r53;
                        gm_l6 = t55;
                    }
                }
gm_ll30:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
                }
                goto gm_lad;
lb26:;
            }
        }
        {
            uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
            void * t57 = gm_l1;
            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 0) + 8);
            if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_src10tru552hn49, 684, 13);
            gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 0)) + t56 * 72) + 32) + 24));
            gm_l8 = t59;
        }
        {
            gm_tl874t2atnlh3 t60 = gm_l8;
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_scd8m51mtmjv9));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(18));
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_scmgj2ccom9v4));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(39));
            gm_f_lint_diag_4vra4qu((void *)&F.l9, (void *)NULL, cx, NULL, t60, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t63 = (void *)((uint8_t *)&F.l9);
            gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sso47q408qu33));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(23));
            gm_f_add_label_50fj7vv(t63, GM_CONV(gm_cseahvg8v7p0vs, t64), t65);
        }
        {
            void * t66 = gm_l0;
            gm_b0_1 t67 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t66, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
        {
            gm_b0_1 t69;
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
ix9:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_if_expr_k16f2n5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_if_expr_k16f2n5 F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l9; /* x */
    uint8_t gm_l10; /* v */
    gm_b16_8 gm_l11; /* msg */
    gm_t08olq97rscn3 gm_l15; /* name */
    void * gm_l16; /* body */
    GM_STACK_CHECK(&F, 951u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l2;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tcuumjkg5eni6 t16 = GM_LD(gm_tcuumjkg5eni6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 72));
                    F.l5 = t16;
                    {
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)&F.l5) + 0);
                        gm_b0_1 t20 = gm_f_cond_e0272rd((void *)NULL, cx, NULL, t18, t19, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        {
                            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 0) + 0));
                            gm_l6 = t22;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t24 = gm_l7;
                                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t26 = (t24 < t25);
                                if (!t26) goto lb23;
                                {
                                    uint64_t t29 = gm_l7;
                                    uint64_t t30 = (uintptr_t)t29;
                                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    gm_tq5j0547p8452 t34 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t30 * 80));
                                    F.l8 = t34;
                                    {
                                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                        if (t37 != ((uint8_t)1ULL)) goto ie36;
                                        void * t38 = GM_LD(void *, (((uint8_t *)&F.l8) + 8));
                                        gm_l9 = t38;
                                        {
                                            void * t41 = gm_l9;
                                            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                                            if (t42 != ((uint8_t)3ULL)) goto ie40;
                                            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 8));
                                            gm_l10 = t43;
                                            {
                                                gm_b16_8 t45;
                                                uint8_t t47 = gm_l10;
                                                if (!t47) goto ie46;
                                                gm_b16_8 t49;
                                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sdtpg69jtnl30));
                                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(29));
                                                gm_b16_8 t50 = t49;
                                                t45 = t50;
                                                goto ix46;
ie46:;
                                                gm_b16_8 t52;
                                                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sllrmadmbrcvv));
                                                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(53));
                                                gm_b16_8 t53 = t52;
                                                t45 = t53;
ix46:;
                                                gm_l11 = t45;
                                            }
                                            {
                                                void * t54 = gm_l9;
                                                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
                                                gm_b16_8 t56;
                                                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_spic2fa42u00e));
                                                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(18));
                                                gm_b16_8 t57 = gm_l11;
                                                gm_f_lint_diag_4vra4qu((void *)&F.l12, (void *)NULL, cx, NULL, t55, t56, t57);
                                                if (gm_unw.kind) {
                                                    goto gm_ll27;
                                                }
                                            }
                                            {
                                                void * t58 = (void *)((uint8_t *)&F.l12);
                                                gm_b16_8 t59;
                                                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s6mpqtffqhn51));
                                                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(79));
                                                gm_f_add_note_46v502s(t58, t59);
                                            }
                                            {
                                                void * t60 = gm_l0;
                                                gm_b0_1 t61 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t60, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
                                                if (gm_unw.kind) {
                                                    goto gm_ll27;
                                                }
                                            }
                                            goto ix40;
ie40:;
ix40:;
                                        }
                                        goto ix36;
ie36:;
ix36:;
                                    }
                                    goto me32;
mn33:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 533, 9);
me32:;
                                }
lc23:;
                                {
                                    uint64_t t63 = gm_l7;
                                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                    uint64_t t66 = r64;
                                    gm_l7 = t66;
                                }
                            }
gm_ll27:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
                            }
                            goto gm_ll9;
lb23:;
                        }
                    }
                    {
                        void * t67 = gm_l0;
                        void * t68 = (void *)(((uint8_t *)&F.l5) + 32);
                        gm_b0_1 t69 = gm_f_block_btrailr((void *)NULL, cx, NULL, t67, t68);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 531, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t71 = gm_l4;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l4 = t74;
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
        void * t76 = gm_l2;
        if (!(((((uint8_t *)t76) + 16))[48] != 0)) goto ie75;
        gm_ttja7ii61nila t77 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)t76) + 16));
        F.l13 = t77;
        {
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
            if (t81 != ((uint8_t)0ULL)) goto mn80;
            gm_t79rbdv4rafe5 t82 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l13) + 8));
            F.l14 = t82;
            void * t83 = gm_l0;
            void * t84 = (void *)((uint8_t *)&F.l14);
            gm_b0_1 t85 = gm_f_block_btrailr((void *)NULL, cx, NULL, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me79;
mn80:;
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
            if (t87 != ((uint8_t)1ULL)) goto mn86;
            gm_t08olq97rscn3 t88 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l13) + 8) + 0));
            gm_l15 = t88;
            void * t89 = GM_LD(void *, ((((uint8_t *)&F.l13) + 8) + 32));
            gm_l16 = t89;
            {
                void * t91 = gm_l0;
                gm_t08olq97rscn3 t92 = gm_l15;
                void * t93 = gm_l16;
                uint8_t t94;
                uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
                if (t97 != ((uint8_t)44ULL)) goto mn96;
                t94 = ((uint8_t)1);
                goto me95;
mn96:;
                t94 = ((uint8_t)0);
                goto me95;
mn98:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 549, 33);
me95:;
                gm_b0_1 t99 = gm_f_capture_abs7k4r((void *)NULL, cx, NULL, t91, &t92, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t100 = gm_l0;
                void * t101 = gm_l16;
                gm_b0_1 t102 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t100, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me79;
mn86:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 546, 9);
me79:;
        }
        goto ix75;
ie75:;
ix75:;
    }
    {
        void * t103 = gm_l0;
        void * t104 = gm_l1;
        void * t105 = gm_l2;
        gm_b0_1 t106 = gm_f_needless_bool_mifff69((void *)NULL, cx, NULL, t103, t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t107 = gm_l0;
        void * t108 = gm_l2;
        gm_b0_1 t109 = gm_f_empty_branches_7o4oudt((void *)NULL, cx, NULL, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t110 = gm_l0;
        void * t111 = gm_l2;
        gm_b0_1 t112 = gm_f_collapsible_else_ehhjtii((void *)NULL, cx, NULL, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t113 = gm_l0;
        void * t114 = gm_l2;
        gm_b0_1 t115 = gm_f_identical_branches_ug6ccol((void *)NULL, cx, NULL, t113, t114);
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
inline __attribute__((aligned(4))) uint64_t gm_f_indent_of_p7r0sg5(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_indent_of_p7r0sg5 F;
    void * gm_l0; /* w */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* src */
    uint64_t gm_l3; /* start */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        void * t5 = gm_f_file_o15fqku(t3, t4);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
        gm_l2 = t6;
    }
    {
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        uint64_t t8 = (uint64_t)t7;
        gm_l3 = t8;
    }
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t10 > ((uint64_t)0ULL));
            if (!t11) goto lb9;
            uint64_t t12 = gm_l3;
            uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
            if (o14) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 725, 29);
            uint64_t t15 = r13;
            uint64_t t16 = (uintptr_t)t15;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_src10tru552hn49, 725, 25);
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 1));
            uint8_t t19 = (!(t18 == ((uint8_t)10ULL)));
            if (!t19) goto lb9;
            {
                uint64_t t22 = gm_l3;
                uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 725, 54);
                uint64_t t25 = r23;
                gm_l3 = t25;
            }
lc9:;
        }
lb9:;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t27 = gm_l3;
            uint64_t t28 = gm_l4;
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 727, 11);
            uint64_t t31 = r29;
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t33 = (t31 < t32);
            if (!t33) goto lb26;
            uint64_t t34 = gm_l3;
            uint64_t t35 = gm_l4;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 727, 39);
            uint64_t t38 = r36;
            uint64_t t39 = (uintptr_t)t38;
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_src10tru552hn49, 727, 35);
            uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t39 * 1));
            gm_tqs5p4n2tq19v t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t42 + 1), ((uint8_t)9ULL));
            gm_tqs5p4n2tq19v t43 = t42;
            uint8_t t44 = 0;
            uint8_t t45 = t41;
            if (gm_eqjmccqdb90f(((uint8_t *)&t43) + 0, ((uint8_t *)&t45))) t44 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t43) + 1, ((uint8_t *)&t45))) t44 = 1;
            uint8_t t46 = (uint8_t)(t44);
            if (!t46) goto lb26;
            {
                uint64_t t49 = gm_l4;
                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 727, 67);
                uint64_t t52 = r50;
                gm_l4 = t52;
            }
lc26:;
        }
lb26:;
    }
    {
        uint64_t t53 = gm_l4;
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_constant_call_vj2jgti(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_constant_call_vj2jgti F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 764u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)19ULL)) goto mn9;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l1 = t11;
        {
            {
                gm_b16_8 t14 = gm_l1;
                gm_l2 = t14;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t16 = gm_l3;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t18 = (t16 < t17);
                    if (!t18) goto lb15;
                    {
                        uint64_t t21 = gm_l3;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        gm_t7te2tdpebbt4 t26 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t22 * 144));
                        F.l4 = t26;
                        {
                            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                            if (t32 != ((uint8_t)0ULL)) goto pa31;
                            gm_tc3oht4ksth9d t33 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l4) + 0) + 8));
                            F.l5 = t33;
                            goto po30;
pa31:;
                            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                            if (t35 != ((uint8_t)1ULL)) goto pa34;
                            gm_tc3oht4ksth9d t36 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l4) + 0) + 8) + 32));
                            F.l5 = t36;
                            goto po30;
pa34:;
                            goto mn29;
po30:;
                            void * t38 = (void *)((uint8_t *)&F.l5);
                            uint8_t t39 = gm_f_is_literal_9ac2fdt(t38);
                            uint8_t t40 = (uint8_t)!t39;
                            if (!t40) goto ie37;
                            {
                                uint8_t t42 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix37;
ie37:;
ix37:;
                            goto me28;
mn29:;
                            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 0) + 0));
                            if (t44 != ((uint8_t)2ULL)) goto mn43;
                            uint8_t t45 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                            goto me28;
mn43:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 800, 17);
me28:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 799, 13);
me24:;
                    }
lc15:;
                    {
                        uint64_t t47 = gm_l3;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l3 = t50;
                    }
                }
lb15:;
            }
        }
        {
            uint8_t t51 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t53 != ((uint8_t)10ULL)) goto mn52;
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t54;
        {
            {
                gm_b16_8 t57 = gm_l7;
                gm_l8 = t57;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t59 = gm_l9;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t61 = (t59 < t60);
                    if (!t61) goto lb58;
                    {
                        uint64_t t64 = gm_l9;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tv1o1j44ii4db t69 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t65 * 136));
                        F.l10 = t69;
                        {
                            void * t72 = (void *)(((uint8_t *)&F.l10) + 40);
                            uint8_t t73 = gm_f_is_literal_9ac2fdt(t72);
                            uint8_t t74 = (uint8_t)!t73;
                            if (!t74) goto ie71;
                            {
                                uint8_t t76 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix71;
ie71:;
ix71:;
                        }
                        goto me67;
mn68:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 808, 13);
me67:;
                    }
lc58:;
                    {
                        uint64_t t78 = gm_l9;
                        uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                        uint64_t t81 = r79;
                        gm_l9 = t81;
                    }
                }
lb58:;
            }
        }
        {
            uint8_t t82 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn52:;
ma6:;
        uint8_t t84 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn83:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 797, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_9ac2fdt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_9ac2fdt F;
    void * gm_l0; /* e */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* operand */
    void * gm_l3; /* operand */
    void * gm_l4; /* lhs */
    void * gm_l5; /* rhs */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)0ULL)) goto pa13;
        goto po12;
pa13:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)1ULL)) goto pa15;
        goto po12;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)2ULL)) goto pa17;
        goto po12;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)3ULL)) goto pa19;
        goto po12;
pa19:;
        goto mn11;
po12:;
        uint8_t t21 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)12ULL)) goto mn22;
        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t24;
        void * t25 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l2 = t25;
        uint8_t t26 = 0;
        gm_t9qkh0rsooc8d t27 = gm_l1;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)1ULL));
        gm_t0am46iohlpol t30;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 0), t28);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 1), t29);
        gm_t0am46iohlpol t31 = t30;
        uint8_t t32 = 0;
        gm_t9qkh0rsooc8d t33 = t27;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t31) + 0, ((uint8_t *)&t33))) t32 = 1;
        if (gm_eqlv5i3f25jk(((uint8_t *)&t31) + 1, ((uint8_t *)&t33))) t32 = 1;
        uint8_t t34 = (uint8_t)(t32);
        if (t34) {
            void * t35 = gm_l2;
            uint8_t t36 = gm_f_is_literal_9ac2fdt(t35);
            t26 = t36;
        } else t26 = 0;
        uint8_t t37 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me3;
mn22:;
ma6:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)9ULL)) goto mn38;
        void * t40 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t40;
        void * t41 = gm_l3;
        uint8_t t42 = gm_f_is_literal_9ac2fdt(t41);
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
        goto me3;
mn38:;
ma7:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t45 != ((uint8_t)13ULL)) goto mn44;
        void * t46 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t46;
        void * t47 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l5 = t47;
        uint8_t t48 = 0;
        void * t49 = gm_l4;
        uint8_t t50 = gm_f_is_literal_9ac2fdt(t49);
        if (t50) {
            void * t51 = gm_l5;
            uint8_t t52 = gm_f_is_literal_9ac2fdt(t51);
            t48 = t52;
        } else t48 = 0;
        uint8_t t53 = 0;
        F.ret = t48;
        goto gm_fret;
        goto me3;
mn44:;
ma8:;
        uint8_t t55 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn54:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 784, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_lower_q12rpq9(uint8_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_upper_68kn81r(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_item_a3ennq0(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_item_a3ennq0 F;
    void * gm_l0; /* w */
    void * gm_l1; /* it */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* args */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tibupt4dmcqai gm_l10; /* s */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* args */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tibupt4dmcqai gm_l18; /* s */
    gm_b16_8 gm_l19; /* name */
    uint8_t gm_l22; /* foreign_ */
    void * gm_l23; /* t */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    gm_b16_8 gm_l28; /* els */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    void * gm_l32; /* h */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    void * gm_l37; /* e */
    GM_STACK_CHECK(&F, 2502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 40));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tkacitepg5fk0 t16 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 72));
                    F.l5 = t16;
                    {
                        uint8_t t19 = (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l5) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l5) + 0) + 0)), gm_slb6cj7ndgvdm, 5) == 0));
                        if (!t19) goto ie18;
                        {
                            goto lc5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        gm_th2lqbo77pvsc t21 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l5) + 32));
                        gm_th2lqbo77pvsc t22 = t21;
                        gm_b16_8 t23;
                        if (((((uint8_t *)&t22))[16] != 0)) {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
                            t23 = t24;
                        } else {
                            goto lc5;
                        }
                        gm_l6 = t23;
                    }
                    {
                        {
                            gm_b16_8 t26 = gm_l6;
                            gm_l7 = t26;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t28 = gm_l8;
                                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t30 = (t28 < t29);
                                if (!t30) goto lb27;
                                {
                                    uint64_t t33 = gm_l8;
                                    uint64_t t34 = (uintptr_t)t33;
                                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_tc3oht4ksth9d t38 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t34 * 88));
                                    F.l9 = t38;
                                    {
                                        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 8) + 0));
                                        if (t41 != ((uint8_t)2ULL)) goto ie40;
                                        gm_tibupt4dmcqai t42 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)&F.l9) + 8) + 8));
                                        gm_l10 = t42;
                                        {
                                            void * t44 = (void *)((uint8_t *)&F.l2);
                                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
                                            gm_b16_8 t46 = t45;
                                            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                            gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t44, t47);
                                            gm_tftb1odc739t5 t49 = t48;
                                            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src10tru552hn49, 85, 36);
                                            gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                                        }
                                        goto ix40;
ie40:;
ix40:;
                                    }
                                    goto me36;
mn37:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 84, 9);
me36:;
                                }
lc27:;
                                {
                                    uint64_t t52 = gm_l8;
                                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                                    uint64_t t55 = r53;
                                    gm_l8 = t55;
                                }
                            }
lb27:;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 81, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t57 = gm_l4;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l4 = t60;
                }
            }
lb5:;
        }
    }
    {
        {
            void * t62 = gm_l1;
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t62) + 40));
            gm_l11 = t63;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t65 = gm_l12;
                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t67 = (t65 < t66);
                if (!t67) goto lb64;
                {
                    uint64_t t70 = gm_l12;
                    uint64_t t71 = (uintptr_t)t70;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_tkacitepg5fk0 t75 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t71 * 72));
                    F.l13 = t75;
                    {
                        uint8_t t78 = (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l13) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l13) + 0) + 0)), gm_slb6cj7ndgvdm, 5) == 0));
                        if (!t78) goto ie77;
                        {
                            goto lc64;
                        }
                        goto ix77;
ie77:;
ix77:;
                    }
                    {
                        gm_th2lqbo77pvsc t80 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l13) + 32));
                        gm_th2lqbo77pvsc t81 = t80;
                        gm_b16_8 t82;
                        if (((((uint8_t *)&t81))[16] != 0)) {
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 0));
                            t82 = t83;
                        } else {
                            goto lc64;
                        }
                        gm_l14 = t82;
                    }
                    {
                        {
                            gm_b16_8 t85 = gm_l14;
                            gm_l15 = t85;
                        }
                        {
                            gm_l16 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t87 = gm_l16;
                                uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                uint8_t t89 = (t87 < t88);
                                if (!t89) goto lb86;
                                {
                                    uint64_t t92 = gm_l16;
                                    uint64_t t93 = (uintptr_t)t92;
                                    uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    gm_tc3oht4ksth9d t97 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t93 * 88));
                                    F.l17 = t97;
                                    {
                                        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 8) + 0));
                                        if (t100 != ((uint8_t)2ULL)) goto ie99;
                                        gm_tibupt4dmcqai t101 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)&F.l17) + 8) + 8));
                                        gm_l18 = t101;
                                        {
                                            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                            gm_l19 = t103;
                                        }
                                        {
                                            gm_b16_8 t105 = gm_l19;
                                            gm_b16_8 t106 = t105;
                                            gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                            gm_b16_8 t108 = t107;
                                            uint8_t t109 = (!((GM_LD(uintptr_t, ((uint8_t *)&t108) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&t108)), gm_s0nf1rg04f3m5, 3) == 0));
                                            if (!t109) goto ie104;
                                            gm_b16_8 t110 = gm_l19;
                                            gm_b16_8 t111 = t110;
                                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                            gm_tf5srh4mp4b3i t113 = gm_f_rule_scmud9h(t112);
                                            gm_tf5srh4mp4b3i t114 = t113;
                                            uint8_t t115;
                                            if (((((uint8_t *)&t114))[8] != 0)) goto mn117;
                                            t115 = ((uint8_t)1);
                                            goto me116;
mn117:;
                                            t115 = ((uint8_t)0);
                                            goto me116;
mn118:;
                                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 94, 42);
me116:;
                                            if (!t115) goto ie104;
                                            {
                                                gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l17) + 72));
                                                gm_b16_8 t121 = gm_l19;
                                                gm_tnpifv20irnph t122;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t122 + 0), t121);
                                                gm_tabb710d9mq2e t123;
                                                gm_f_format_h13dbf2((void *)&t123, (void *)NULL, cx, NULL, t122);
                                                if (gm_unw.kind) {
                                                    goto gm_ll90;
                                                }
                                                gm_tabb710d9mq2e t124 = t123;
                                                if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_src10tru552hn49, 95, 45);
                                                gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                                                gm_b16_8 t126 = t125;
                                                gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                                                gm_f_error_s6hn871((void *)&F.l20, (void *)NULL, cx, NULL, t120, t127);
                                                if (gm_unw.kind) {
                                                    goto gm_ll90;
                                                }
                                            }
                                            {
                                                void * t128 = (void *)((uint8_t *)&F.l20);
                                                gm_b16_8 t129;
                                                GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s91c3em0lmpov));
                                                GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(36));
                                                gm_f_add_note_46v502s(t128, t129);
                                            }
                                            {
                                                void * t130 = gm_l0;
                                                void * t131 = (void *)((((uint8_t *)t130) + 8) + 176);
                                                gm_tftb1odc739t5 t132 = gm_f_push_l0rgbjf(t131, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l20)));
                                                gm_tftb1odc739t5 t133 = t132;
                                                if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_src10tru552hn49, 97, 21);
                                                gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                                            }
                                            goto ix104;
ie104:;
ix104:;
                                        }
                                        goto ix99;
ie99:;
ix99:;
                                    }
                                    goto me95;
mn96:;
                                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 91, 9);
me95:;
                                }
lc86:;
                                {
                                    uint64_t t136 = gm_l16;
                                    uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                                    uint64_t t139 = r137;
                                    gm_l16 = t139;
                                }
                            }
gm_ll90:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb86; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc86; }
                            }
                            goto gm_ll68;
lb86:;
                        }
                    }
                    goto me73;
mn74:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 88, 5);
me73:;
                }
lc64:;
                {
                    uint64_t t141 = gm_l12;
                    uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                    uint64_t t144 = r142;
                    gm_l12 = t144;
                }
            }
gm_ll68:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb64; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc64; }
            }
            goto gm_lad;
lb64:;
        }
    }
    {
        uint64_t t146 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t147 = (t146 > ((uint64_t)0ULL));
        if (!t147) goto ie145;
        {
            void * t149 = gm_l0;
            void * t150 = (void *)((((uint8_t *)t149) + 8) + 136);
            gm_tsu0mic7gtnv6 t151;
            memset((uint8_t *)&t151 + 12, 0, 4);
            void * t152 = gm_l1;
            gm_tl874t2atnlh3 t153 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t152) + 4));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t151 + 0, t153);
            memcpy((uint8_t *)&t151 + 16, ((uint8_t *)&F.l2), 40);
            gm_tftb1odc739t5 t154 = gm_f_push_amge2t0(t150, &t151);
            gm_tftb1odc739t5 t155 = t154;
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_src10tru552hn49, 102, 32);
            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        void * t157 = gm_l1;
        uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        switch (t165) { case ((uint8_t)0ULL): goto ma159; case ((uint8_t)1ULL): goto ma160; case ((uint8_t)2ULL): goto ma161; case ((uint8_t)3ULL): goto ma162; case ((uint8_t)4ULL): goto ma163; default: goto ma164; }
ma159:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        if (t167 != ((uint8_t)0ULL)) goto mn166;
        gm_tv2uf4leil5hq t168 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t157) + 64) + 8));
        F.l21 = t168;
        {
            void * t170 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
            uint8_t t171;
            uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t170) + 8) + 0));
            if (t174 != ((uint8_t)39ULL)) goto mn173;
            t171 = ((uint8_t)1);
            goto me172;
mn173:;
            t171 = ((uint8_t)0);
            goto me172;
mn175:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 105, 25);
me172:;
            gm_l22 = t171;
        }
        {
            uint64_t t177 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 8) + 8);
            uint8_t t178 = (t177 > ((uint64_t)0ULL));
            if (!t178) goto ie176;
            uint8_t t179 = gm_l22;
            uint8_t t180 = (uint8_t)!t179;
            if (!t180) goto ie176;
            {
                void * t182 = gm_l0;
                uint64_t t183 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t184 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 8) + 8);
                if (t183 >= (uintptr_t)(t184)) gm_trap_index(t183, t184, gm_src10tru552hn49, 106, 68);
                void * t185 = (void *)((uint8_t *)&F.l21);
                gm_b0_1 t186 = gm_f_top_level_name_dv4kpc9((void *)NULL, cx, NULL, t182, &(*(gm_t08olq97rscn3 *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l21) + 8)) + t183 * 32)), t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix176;
ie176:;
ix176:;
        }
        {
            void * t188 = GM_LD(void *, (((uint8_t *)&F.l21) + 24));
            void * t189 = t188;
            if (!(GM_LD(void *, ((uint8_t *)&t189)) != 0)) goto ie187;
            void * t190 = GM_LD(void *, ((uint8_t *)&t189));
            gm_l23 = t190;
            {
                void * t192 = gm_l0;
                void * t193 = gm_l23;
                gm_b0_1 t194 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t192, t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix187;
ie187:;
ix187:;
        }
        {
            void * t195 = gm_l0;
            void * t196 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
            gm_b0_1 t197 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me158;
mn166:;
ma160:;
        uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        if (t199 != ((uint8_t)1ULL)) goto mn198;
        gm_t7bjbmukv26e7 t200 = GM_LD(gm_t7bjbmukv26e7, ((((uint8_t *)t157) + 64) + 8));
        F.l24 = t200;
        {
            void * t202 = gm_l0;
            void * t203 = (void *)((((uint8_t *)t202) + 8) + 96);
            gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l24) + 48));
            gm_tftb1odc739t5 t205 = gm_f_push_0mk7udh(t203, GM_CONV(gm_cseahvg8v7p0vs, t204));
            gm_tftb1odc739t5 t206 = t205;
            if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 1, gm_src10tru552hn49, 111, 13);
            gm_b0_1 t207 = GM_LD(gm_b0_1, (((uint8_t *)&t206) + 1));
        }
        {
            void * t208 = gm_l0;
            void * t209 = GM_LD(void *, (((uint8_t *)&F.l24) + 0));
            gm_b0_1 t210 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t208, t209);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                uint64_t t212 = GM_LD(uintptr_t, (((uint8_t *)&F.l24) + 8) + 8);
                gm_l25 = t212;
            }
            {
                for (;;) {
                    uint64_t t214 = gm_l26;
                    uint64_t t215 = gm_l25;
                    uint8_t t216 = (t214 < t215);
                    if (!t216) goto lb213;
                    {
                        uint64_t t219 = gm_l26;
                        uint64_t t220 = t219;
                        uint64_t t223 = GM_LD(uint64_t, ((uint8_t *)&t220));
                        gm_l27 = t223;
                        {
                            void * t225 = gm_l0;
                            uint64_t t226 = gm_l27;
                            uint64_t t227 = (uintptr_t)t226;
                            uint64_t t228 = GM_LD(uintptr_t, (((uint8_t *)&F.l24) + 8) + 8);
                            if (t227 >= (uintptr_t)(t228)) gm_trap_index(t227, t228, gm_src10tru552hn49, 113, 47);
                            void * t229 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l24) + 8)) + t227 * 160);
                            gm_b0_1 t230 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t225, t229);
                            if (gm_unw.kind) {
                                goto gm_ll217;
                            }
                        }
                        goto me221;
mn222:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 113, 13);
me221:;
                    }
lc213:;
                    {
                        uint64_t t232 = gm_l26;
                        uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                        uint64_t t235 = r233;
                        gm_l26 = t235;
                    }
                }
gm_ll217:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb213; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc213; }
                }
                goto gm_lad;
lb213:;
            }
        }
        {
            if (!(((((uint8_t *)&F.l24) + 24))[16] != 0)) goto ie236;
            gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 24));
            gm_l28 = t237;
            {
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_l29 = t240;
                }
                {
                    for (;;) {
                        uint64_t t242 = gm_l30;
                        uint64_t t243 = gm_l29;
                        uint8_t t244 = (t242 < t243);
                        if (!t244) goto lb241;
                        {
                            uint64_t t247 = gm_l30;
                            uint64_t t248 = t247;
                            uint64_t t251 = GM_LD(uint64_t, ((uint8_t *)&t248));
                            gm_l31 = t251;
                            {
                                void * t253 = gm_l0;
                                uint64_t t254 = gm_l31;
                                uint64_t t255 = (uintptr_t)t254;
                                uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                if (t255 >= (uintptr_t)(t256)) gm_trap_index(t255, t256, gm_src10tru552hn49, 115, 47);
                                void * t257 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t255 * 160);
                                gm_b0_1 t258 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t253, t257);
                                if (gm_unw.kind) {
                                    goto gm_ll245;
                                }
                            }
                            goto me249;
mn250:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 115, 17);
me249:;
                        }
lc241:;
                        {
                            uint64_t t260 = gm_l30;
                            uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                            uint64_t t263 = r261;
                            gm_l30 = t263;
                        }
                    }
gm_ll245:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb241; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc241; }
                    }
                    goto gm_lad;
lb241:;
                }
            }
            goto ix236;
ie236:;
ix236:;
        }
        goto me158;
mn198:;
ma161:;
        uint8_t t265 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        if (t265 != ((uint8_t)2ULL)) goto mn264;
        void * t266 = GM_LD(void *, ((((uint8_t *)t157) + 64) + 8));
        gm_l32 = t266;
        {
            {
                void * t269 = gm_l32;
                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)t269) + 64));
                gm_l33 = t270;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t272 = gm_l34;
                    uint64_t t273 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t274 = (t272 < t273);
                    if (!t274) goto lb271;
                    {
                        uint64_t t277 = gm_l34;
                        uint64_t t278 = (uintptr_t)t277;
                        uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_t99vmhfllumbh t282 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t278 * 72));
                        F.l35 = t282;
                        {
                            void * t284 = gm_l0;
                            void * t285 = GM_LD(void *, (((uint8_t *)&F.l35) + 48));
                            gm_b0_1 t286 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t284, t285);
                            if (gm_unw.kind) {
                                goto gm_ll275;
                            }
                        }
                        goto me280;
mn281:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 119, 13);
me280:;
                    }
lc271:;
                    {
                        uint64_t t288 = gm_l34;
                        uint64_t r289; bool o290 = __builtin_add_overflow(t288, ((uint64_t)1ULL), &r289);
                        uint64_t t291 = r289;
                        gm_l34 = t291;
                    }
                }
gm_ll275:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb271; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc271; }
                }
                goto gm_lad;
lb271:;
            }
        }
        goto me158;
mn264:;
ma162:;
        uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        if (t293 != ((uint8_t)3ULL)) goto mn292;
        gm_tcfiotghd1s5k t294 = GM_LD(gm_tcfiotghd1s5k, ((((uint8_t *)t157) + 64) + 8));
        F.l36 = t294;
        void * t295 = gm_l0;
        void * t296 = (void *)(((uint8_t *)&F.l36) + 40);
        gm_b0_1 t297 = gm_f_block_btrailr((void *)NULL, cx, NULL, t295, t296);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me158;
mn292:;
ma163:;
        uint8_t t299 = GM_LD(uint8_t, ((((uint8_t *)t157) + 64) + 0));
        if (t299 != ((uint8_t)4ULL)) goto mn298;
        void * t300 = GM_LD(void *, ((((uint8_t *)t157) + 64) + 8));
        gm_l37 = t300;
        void * t301 = gm_l0;
        void * t302 = gm_l37;
        gm_b0_1 t303 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t301, t302);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me158;
mn298:;
ma164:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 103, 5);
me158:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_names_in_1jbgo4k F;
    gm_b16_8 gm_l0; /* tokens */
    gm_b16_8 gm_l1; /* spans */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* s */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* w */
    GM_STACK_CHECK(&F, 430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
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
        {
            gm_b16_8 t9 = gm_l0;
            gm_l3 = t9;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tm4pc0emhrv1g t21 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 48));
                    F.l5 = t21;
                    {
                        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 0) + 0));
                        if (t24 != ((uint8_t)0ULL)) goto ie23;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l5) + 0) + 8));
                        gm_l6 = t25;
                        {
                            {
                                gm_b16_8 t28 = gm_l1;
                                gm_l7 = t28;
                            }
                            {
                                gm_l8 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t30 = gm_l8;
                                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    uint8_t t32 = (t30 < t31);
                                    if (!t32) goto lb29;
                                    {
                                        uint64_t t35 = gm_l8;
                                        uint64_t t36 = (uintptr_t)t35;
                                        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                        gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t36 * 12));
                                        gm_l9 = t40;
                                        {
                                            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
                                            uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 0));
                                            uint8_t t45 = ((t43 == t44));
                                            if (!t45) goto ie42;
                                            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                                            uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 4));
                                            uint8_t t48 = (t46 <= t47);
                                            if (!t48) goto ie42;
                                            uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 8));
                                            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                                            uint8_t t51 = (t49 <= t50);
                                            if (!t51) goto ie42;
                                            {
                                                void * t54 = gm_l2;
                                                gm_b16_8 t55 = gm_l6;
                                                uint8_t t56 = gm_f_contains_57n7jqd(t54, t55);
                                                uint8_t t57 = (uint8_t)!t56;
                                                if (!t57) goto ie53;
                                                {
                                                    void * t59 = gm_l2;
                                                    gm_b16_8 t60 = gm_l6;
                                                    gm_tftb1odc739t5 t61 = gm_f_put_5c6qgui(t59, t60, ((uint8_t)1));
                                                    gm_tftb1odc739t5 t62 = t61;
                                                    if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_src10tru552hn49, 862, 46);
                                                    gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                                                }
                                                goto ix53;
ie53:;
ix53:;
                                            }
                                            {
                                                goto lb29;
                                            }
                                            goto ix42;
ie42:;
ix42:;
                                        }
                                        goto me38;
mn39:;
                                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 860, 13);
me38:;
                                    }
lc29:;
                                    {
                                        uint64_t t65 = gm_l8;
                                        uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                                        uint64_t t68 = r66;
                                        gm_l8 = t68;
                                    }
                                }
lb29:;
                            }
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 858, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t70 = gm_l4;
                    uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
                    uint64_t t73 = r71;
                    gm_l4 = t73;
                }
            }
lb10:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_needless_bool_mifff69(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_needless_bool_mifff69 F;
    void * gm_l0; /* w */
    void * gm_l1; /* e */
    void * gm_l2; /* i */
    void * gm_l5; /* br */
    void * gm_l6; /* c */
    gm_b16_8 gm_l7; /* prefix */
    uint8_t gm_l8; /* negate */
    gm_t5sj3u8e14oco gm_l9; /* a */
    gm_t5sj3u8e14oco gm_l10; /* b */
    uint8_t gm_l11; /* x */
    uint8_t gm_l12; /* y */
    uint8_t gm_l13; /* x */
    uint8_t gm_l14; /* y */
    gm_b16_8 gm_l15; /* t */
    gm_b16_8 gm_l16; /* value */
    gm_b16_8 gm_l17; /* msg */
    GM_STACK_CHECK(&F, 1256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = (!(t4 == ((uint64_t)1ULL)));
        if (!t5) goto ie2;
        {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l2;
        gm_t9t5bnrf784qu t10 = GM_LD(gm_t9t5bnrf784qu, (((uint8_t *)t9) + 16));
        gm_t9t5bnrf784qu t11 = t10;
        gm_ttja7ii61nila t12;
        if (((((uint8_t *)&t11))[48] != 0)) {
            gm_ttja7ii61nila t13 = GM_LD(gm_ttja7ii61nila, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_b0_1 t14;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        F.l3 = t12;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t17 != ((uint8_t)0ULL)) goto ie16;
        gm_t79rbdv4rafe5 t18 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l3) + 8));
        F.l4 = t18;
        {
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            void * t21 = gm_l2;
            uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 0) + 8);
            if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_src10tru552hn49, 585, 12);
            void * t23 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 0)) + t20 * 72);
            gm_l5 = t23;
        }
        {
            void * t25 = gm_l5;
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 0) + 0) + 8);
            uint8_t t27 = (!(t26 == ((uint64_t)1ULL)));
            if (!t27) goto ie24;
            {
                gm_b0_1 t29;
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix24;
ie24:;
ix24:;
        }
        {
            uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
            void * t33 = gm_l5;
            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 0) + 0) + 8);
            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src10tru552hn49, 587, 17);
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 0) + 0)) + t32 * 80) + 0));
            if (t35 != ((uint8_t)1ULL)) goto ie31;
            void * t36 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 0) + 0)) + t32 * 80) + 8));
            gm_l6 = t36;
            {
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(0));
                gm_l7 = t38;
            }
            {
                gm_l8 = ((uint8_t)0);
            }
            {
                void * t39 = gm_l5;
                void * t40 = (void *)(((uint8_t *)t39) + 32);
                gm_t5hib78lmj2mf t41 = gm_f_shape_2jg6iuk(t40);
                gm_t5hib78lmj2mf t42 = t41;
                gm_t5sj3u8e14oco t43;
                if (((((uint8_t *)&t42))[2] != 0)) {
                    gm_t5sj3u8e14oco t44 = GM_LD(gm_t5sj3u8e14oco, (((uint8_t *)&t42) + 0));
                    t43 = t44;
                } else {
                    gm_b0_1 t45;
                    uint8_t t46 = 0;
                    F.ret = t45;
                    goto gm_fret;
                }
                gm_l9 = t43;
            }
            {
                void * t47 = (void *)((uint8_t *)&F.l4);
                gm_t5hib78lmj2mf t48 = gm_f_shape_2jg6iuk(t47);
                gm_t5hib78lmj2mf t49 = t48;
                gm_t5sj3u8e14oco t50;
                if (((((uint8_t *)&t49))[2] != 0)) {
                    gm_t5sj3u8e14oco t51 = GM_LD(gm_t5sj3u8e14oco, (((uint8_t *)&t49) + 0));
                    t50 = t51;
                } else {
                    gm_b0_1 t52;
                    uint8_t t53 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                gm_l10 = t50;
            }
            {
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                if (t56 != ((uint8_t)0ULL)) goto mn55;
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
                gm_l11 = t57;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    if (t60 != ((uint8_t)0ULL)) goto ie59;
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
                    gm_l12 = t61;
                    {
                        uint8_t t64 = gm_l11;
                        uint8_t t65 = gm_l12;
                        uint8_t t66 = ((t64 == t65));
                        if (!t66) goto ie63;
                        {
                            gm_b0_1 t68;
                            uint8_t t69 = 0;
                            F.ret = t68;
                            goto gm_fret;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    {
                        uint8_t t70 = gm_l11;
                        uint8_t t71 = (uint8_t)!t70;
                        gm_l8 = t71;
                    }
                    goto ix59;
ie59:;
                    {
                        gm_b0_1 t73;
                        uint8_t t74 = 0;
                        F.ret = t73;
                        goto gm_fret;
                    }
ix59:;
                }
                goto me54;
mn55:;
                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                if (t76 != ((uint8_t)1ULL)) goto mn75;
                uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
                gm_l13 = t77;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    if (t80 != ((uint8_t)1ULL)) goto ie79;
                    uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
                    gm_l14 = t81;
                    {
                        uint8_t t84 = gm_l13;
                        uint8_t t85 = gm_l14;
                        uint8_t t86 = ((t84 == t85));
                        if (!t86) goto ie83;
                        {
                            gm_b0_1 t88;
                            uint8_t t89 = 0;
                            F.ret = t88;
                            goto gm_fret;
                        }
                        goto ix83;
ie83:;
ix83:;
                    }
                    {
                        gm_b16_8 t90;
                        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_smbbaf3n1gml2));
                        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(7));
                        gm_l7 = t90;
                    }
                    {
                        uint8_t t91 = gm_l13;
                        uint8_t t92 = (uint8_t)!t91;
                        gm_l8 = t92;
                    }
                    goto ix79;
ie79:;
                    {
                        gm_b0_1 t94;
                        uint8_t t95 = 0;
                        F.ret = t94;
                        goto gm_fret;
                    }
ix79:;
                }
                goto me54;
mn75:;
                gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 592, 5);
me54:;
            }
            {
                uint8_t t97 = 0;
                void * t98 = gm_l0;
                void * t99 = gm_l5;
                gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t99) + 32) + 24));
                uint8_t t101 = gm_f_has_comment_qa4dr2o(t98, GM_CONV(gm_cseahvg8v7p0vs, t100));
                if (!t101) {
                    void * t102 = gm_l0;
                    gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                    uint8_t t104 = gm_f_has_comment_qa4dr2o(t102, GM_CONV(gm_cseahvg8v7p0vs, t103));
                    t97 = t104;
                } else t97 = 1;
                if (!t97) goto ie96;
                {
                    gm_b0_1 t106;
                    uint8_t t107 = 0;
                    F.ret = t106;
                    goto gm_fret;
                }
                goto ix96;
ie96:;
ix96:;
            }
            {
                void * t108 = gm_l0;
                void * t109 = gm_l6;
                gm_tl874t2atnlh3 t110 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t109) + 72));
                gm_b16_8 t111 = gm_f_text_kspioh6(t108, GM_CONV(gm_cseahvg8v7p0vs, t110));
                gm_l15 = t111;
            }
            {
                gm_b16_8 t112;
                uint8_t t114 = gm_l8;
                if (!t114) goto ie113;
                void * t116 = gm_l0;
                void * t117 = gm_l6;
                gm_b16_8 t118 = gm_f_negation_tmaga6d((void *)NULL, cx, NULL, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t119 = t118;
                t112 = t119;
                goto ix113;
ie113:;
                gm_b16_8 t121 = gm_l15;
                gm_b16_8 t122 = t121;
                t112 = t122;
ix113:;
                gm_l16 = t112;
            }
            {
                gm_b16_8 t123;
                uint8_t t125 = gm_l8;
                if (!t125) goto ie124;
                gm_b16_8 t127;
                GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sd1d50j1ea4vf));
                GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(45));
                gm_b16_8 t128 = t127;
                t123 = t128;
                goto ix124;
ie124:;
                gm_b16_8 t130;
                GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sbgcsq0m3astj));
                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(42));
                gm_b16_8 t131 = t130;
                t123 = t131;
ix124:;
                gm_l17 = t123;
            }
            {
                void * t132 = gm_l1;
                gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t132) + 72));
                gm_b16_8 t134;
                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sh4r0fq5alj61));
                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(13));
                gm_b16_8 t135 = gm_l17;
                gm_f_lint_diag_4vra4qu((void *)&F.l18, (void *)NULL, cx, NULL, t133, t134, t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t136 = (void *)((uint8_t *)&F.l18);
                gm_t6j6smj7qoctu t137;
                gm_b16_8 t138 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
                gm_b16_8 t139 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t139);
                gm_tabb710d9mq2e t140;
                gm_f_format_4pq08p9((void *)&t140, (void *)NULL, cx, NULL, &t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t141 = t140;
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_src10tru552hn49, 610, 15);
                gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
                gm_b16_8 t143 = t142;
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                void * t145 = gm_l1;
                gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
                gm_t6j6smj7qoctu t147;
                gm_b16_8 t148 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t148);
                gm_b16_8 t149 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t149);
                gm_tabb710d9mq2e t150;
                gm_f_format_8n626pi((void *)&t150, (void *)NULL, cx, NULL, &t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t151 = t150;
                if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 8, gm_src10tru552hn49, 610, 83);
                gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
                gm_b16_8 t153 = t152;
                gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                gm_b0_1 t155 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t136, t144, t146, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t156 = gm_l0;
                gm_b0_1 t157 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t156, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l18)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix31;
ie31:;
            {
                gm_b0_1 t159;
                uint8_t t160 = 0;
                F.ret = t159;
                goto gm_fret;
            }
ix31:;
        }
        goto ix16;
ie16:;
        {
            gm_b0_1 t162;
            uint8_t t163 = 0;
            F.ret = t162;
            goto gm_fret;
        }
ix16:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_negation_tmaga6d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_negation_tmaga6d F;
    void * gm_l0; /* w */
    void * gm_l1; /* c */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    void * gm_l3; /* lhs */
    void * gm_l4; /* rhs */
    gm_tt7vn9ehj0acr gm_l5; /* flipped */
    gm_ts6oqppjh0ba3 gm_l6; /* f */
    GM_STACK_CHECK(&F, 563u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)13ULL)) goto ie2;
        gm_ts6oqppjh0ba3 t5 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        void * t6 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        void * t7 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 16));
        gm_l4 = t7;
        {
            gm_tt7vn9ehj0acr t9;
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; default: goto ma15; }
ma11:;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t19 != ((uint8_t)12ULL)) goto mn18;
            gm_ts6oqppjh0ba3 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)13ULL));
            gm_tt7vn9ehj0acr t21;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[2] = 1;
            t9 = t21;
            goto me10;
mn18:;
ma12:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t23 != ((uint8_t)13ULL)) goto mn22;
            gm_ts6oqppjh0ba3 t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)12ULL));
            gm_tt7vn9ehj0acr t25;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[2] = 1;
            t9 = t25;
            goto me10;
mn22:;
ma13:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t27 != ((uint8_t)14ULL)) goto mn26;
            gm_ts6oqppjh0ba3 t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)15ULL));
            gm_tt7vn9ehj0acr t29;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
            ((uint8_t *)&t29)[2] = 1;
            t9 = t29;
            goto me10;
mn26:;
ma14:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t31 != ((uint8_t)15ULL)) goto mn30;
            gm_ts6oqppjh0ba3 t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)14ULL));
            gm_tt7vn9ehj0acr t33;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t33), t32);
            ((uint8_t *)&t33)[2] = 1;
            t9 = t33;
            goto me10;
mn30:;
ma15:;
            gm_tt7vn9ehj0acr t35;
            memset(&t35, 0, sizeof t35);
            t9 = t35;
            goto me10;
mn34:;
ma16:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 617, 31);
me10:;
            gm_l5 = t9;
        }
        {
            if (!((((uint8_t *)&gm_l5))[2] != 0)) goto ie36;
            gm_ts6oqppjh0ba3 t37 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&gm_l5));
            gm_l6 = t37;
            {
                gm_tfeab279c16rr t39;
                void * t40 = gm_l0;
                void * t41 = gm_l3;
                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
                gm_b16_8 t43 = gm_f_text_kspioh6(t40, GM_CONV(gm_cseahvg8v7p0vs, t42));
                GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t43);
                gm_ts6oqppjh0ba3 t44 = gm_l6;
                gm_b16_8 t45 = gm_f_binop_text_0ljfc0r(t44);
                GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t45);
                void * t46 = gm_l0;
                void * t47 = gm_l4;
                gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
                gm_b16_8 t49 = gm_f_text_kspioh6(t46, GM_CONV(gm_cseahvg8v7p0vs, t48));
                GM_ST(gm_b16_8, (uint8_t *)&t39 + 32, t49);
                gm_tabb710d9mq2e t50;
                gm_f_format_o2cbrdu((void *)&t50, (void *)NULL, cx, NULL, &t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t51 = t50;
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_src10tru552hn49, 624, 34);
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
                gm_b16_8 t53 = t52;
                gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                uint8_t t55 = 0;
                F.ret = t54;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t56 = gm_l1;
        void * t57 = gm_l0;
        void * t58 = gm_l1;
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
        gm_b16_8 t60 = gm_f_text_kspioh6(t57, GM_CONV(gm_cseahvg8v7p0vs, t59));
        gm_b16_8 t61 = gm_f_paren_unless_simple_mhajkgl((void *)NULL, cx, NULL, t56, t60, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t62;
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
        gm_tabb710d9mq2e t63;
        gm_f_format_udg5t2e((void *)&t63, (void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t64 = t63;
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src10tru552hn49, 626, 12);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
        gm_b16_8 t66 = t65;
        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
        uint8_t t68 = 0;
        F.ret = t67;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_space_v7dp7uh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_normalize_space_v7dp7uh F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 161u);
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
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        gm_hval t4 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn5 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlhb9f1a7omo, .env = t4.env, .hctx = t4.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_fields_ojgi3et_sq5308c((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
        F.w0 = t9;
        goto gm_ls10;
gm_ll7:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
        goto gm_lad;
gm_ls10:;
we6:;
        gm_b0_1 t11 = F.w0;
        gm_b0_1 t12 = t11;
    }
    {
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t14 = t13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_paren_at_av727r5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3) {
    struct gm_fr_paren_at_av727r5 F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* what */
    void * gm_l2; /* e */
    uint8_t gm_l3; /* head */
    void * gm_l4; /* inner */
    gm_b16_8 gm_l5; /* t */
    uint32_t gm_l6; /* file */
    GM_STACK_CHECK(&F, 573u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)9ULL)) goto ie2;
        void * t5 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
        gm_l4 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l2;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
            gm_b16_8 t10 = gm_f_text_kspioh6(t7, GM_CONV(gm_cseahvg8v7p0vs, t9));
            gm_l5 = t10;
        }
        {
            uint8_t t12 = 0;
            gm_b16_8 t13 = gm_l5;
            gm_b16_8 t14 = t13;
            uint8_t t15 = 0;
            uint8_t t16 = ((uint8_t)10ULL);
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t14) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t14))) + i * 1, ((uint8_t *)&t16))) { t15 = 1; break; }
            uint8_t t17 = (uint8_t)(t15);
            if (!t17) {
                uint8_t t18 = 0;
                uint8_t t19 = gm_l3;
                if (t19) {
                    gm_b16_8 t20 = gm_l5;
                    gm_b16_8 t21 = t20;
                    uint8_t t22 = 0;
                    uint8_t t23 = ((uint8_t)123ULL);
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t21) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t21))) + i * 1, ((uint8_t *)&t23))) { t22 = 1; break; }
                    uint8_t t24 = (uint8_t)(t22);
                    t18 = t24;
                } else t18 = 0;
                t12 = t18;
            } else t12 = 1;
            if (!t12) goto ie11;
            {
                gm_b0_1 t26;
                uint8_t t27 = 0;
                F.ret = t26;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            void * t28 = gm_l2;
            uint32_t t29 = GM_LD(uint32_t, ((((uint8_t *)t28) + 72) + 0));
            gm_l6 = t29;
        }
        {
            void * t30 = gm_l2;
            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_so9ldq1i41cj2));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(18));
            gm_b16_8 t33 = gm_l1;
            gm_tnpifv20irnph t34;
            GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
            gm_tabb710d9mq2e t35;
            gm_f_format_reauhlh((void *)&t35, (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_src10tru552hn49, 709, 50);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_f_lint_diag_4vra4qu((void *)&F.l7, (void *)NULL, cx, NULL, t31, t32, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 0)) + 12, 0, 4);
            uint32_t t40 = gm_l6;
            void * t41 = gm_l2;
            uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)t41) + 72) + 4));
            void * t43 = gm_l4;
            uint32_t t44 = GM_LD(uint32_t, ((((uint8_t *)t43) + 72) + 4));
            gm_tl874t2atnlh3 t45;
            GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t40);
            GM_ST(uint32_t, ((uint8_t *)&t45 + 4), t42);
            GM_ST(uint32_t, ((uint8_t *)&t45 + 8), t44);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 0)) + 0, t45);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 0)) + 16, t46);
            memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 32)) + 12, 0, 4);
            uint32_t t47 = gm_l6;
            void * t48 = gm_l4;
            uint32_t t49 = GM_LD(uint32_t, ((((uint8_t *)t48) + 72) + 8));
            void * t50 = gm_l2;
            uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)t50) + 72) + 8));
            gm_tl874t2atnlh3 t52;
            GM_ST(uint32_t, ((uint8_t *)&t52 + 0), t47);
            GM_ST(uint32_t, ((uint8_t *)&t52 + 4), t49);
            GM_ST(uint32_t, ((uint8_t *)&t52 + 8), t51);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 32)) + 0, t52);
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&F.l8 + 32)) + 16, t53);
        }
        {
            void * t54 = (void *)((uint8_t *)&F.l7);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sqit0ai5v3joc));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(22));
            void * t56 = ((uint8_t *)&F.l8);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src10tru552hn49, 714, 47);
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t56 + (uintptr_t)0 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t58 = t57;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_b0_1 t60 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t54, t55, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t61 = gm_l0;
            gm_b0_1 t62 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t61, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t64;
            uint8_t t65 = 0;
            F.ret = t64;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_paren_unless_simple_mhajkgl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_paren_unless_simple_mhajkgl F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l1; /* t */
    uint8_t gm_l2; /* comparisons */
    uint8_t gm_l3; /* wrap */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    GM_STACK_CHECK(&F, 340u);
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
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)13ULL)) goto mn9;
        gm_ts6oqppjh0ba3 t11 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t11;
        {
            gm_ts6oqppjh0ba3 t14 = gm_l4;
            gm_ts6oqppjh0ba3 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)20ULL));
            gm_ts6oqppjh0ba3 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)21ULL));
            gm_t9ldr1q5hn9fq t17;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t17 + 0), t15);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t17 + 2), t16);
            gm_t9ldr1q5hn9fq t18 = t17;
            uint8_t t19 = 0;
            gm_ts6oqppjh0ba3 t20 = t14;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t18) + 0, ((uint8_t *)&t20))) t19 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t18) + 2, ((uint8_t *)&t20))) t19 = 1;
            uint8_t t21 = (uint8_t)(t19);
            if (!t21) goto ie13;
            {
                gm_l3 = ((uint8_t)1);
            }
            goto ix13;
ie13:;
            uint8_t t24 = gm_l2;
            if (!t24) goto ie23;
            {
                gm_ts6oqppjh0ba3 t26 = gm_l4;
                gm_ts6oqppjh0ba3 t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)12ULL));
                gm_ts6oqppjh0ba3 t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)13ULL));
                gm_ts6oqppjh0ba3 t29;
                GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)14ULL));
                gm_ts6oqppjh0ba3 t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)15ULL));
                gm_ts6oqppjh0ba3 t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)16ULL));
                gm_ts6oqppjh0ba3 t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)17ULL));
                gm_ts6oqppjh0ba3 t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)18ULL));
                gm_ts6oqppjh0ba3 t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)19ULL));
                gm_tv78bma7gi894 t35;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 0), t27);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 2), t28);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 4), t29);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 6), t30);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 8), t31);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 10), t32);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 12), t33);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35 + 14), t34);
                gm_tv78bma7gi894 t36 = t35;
                uint8_t t37 = 0;
                gm_ts6oqppjh0ba3 t38 = t26;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 0, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 2, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 4, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 6, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 8, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 10, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 12, ((uint8_t *)&t38))) t37 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t36) + 14, ((uint8_t *)&t38))) t37 = 1;
                uint8_t t39 = (uint8_t)(t37);
                gm_l3 = t39;
            }
            goto ix23;
ie23:;
ix23:;
ix13:;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)14ULL)) goto mn40;
        {
            gm_l3 = ((uint8_t)1);
        }
        goto me3;
mn40:;
ma6:;
        goto me3;
mn43:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 822, 5);
me3:;
    }
    {
        gm_b16_8 t45;
        uint8_t t47 = gm_l3;
        if (!t47) goto ie46;
        gm_b16_8 t49 = gm_l1;
        gm_tnpifv20irnph t50;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
        gm_tabb710d9mq2e t51;
        gm_f_format_1vkmnih((void *)&t51, (void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t52 = t51;
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_src10tru552hn49, 833, 22);
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
        gm_b16_8 t54 = t53;
        gm_b16_8 t55 = t54;
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        t45 = t56;
        goto ix46;
ie46:;
        gm_b16_8 t58 = gm_l1;
        gm_b16_8 t59 = t58;
        t45 = t59;
ix46:;
        uint8_t t60 = 0;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pattern_8qtr7os(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_pattern_8qtr7os F;
    void * gm_l0; /* w */
    void * gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* ctx */
    gm_t08olq97rscn3 gm_l3; /* id */
    gm_t08olq97rscn3 gm_l5; /* id */
    void * gm_l7; /* inner */
    gm_tagc77imt2chm gm_l8; /* pp */
    uint8_t gm_l9; /* structish */
    gm_b16_8 gm_l10; /* args */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_t9qkh0rsooc8d gm_l14; /* ctx2 */
    gm_b16_8 gm_l15; /* args */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    void * gm_l19; /* e */
    void * gm_l20; /* lo */
    void * gm_l21; /* hi */
    GM_STACK_CHECK(&F, 1722u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma8; default: goto ma12; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t16;
        {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sjg1ri23hm753));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
            gm_b0_1 t21 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t18, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)), t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tl5vm0tcqvdvb t22;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t28) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma25; case ((uint8_t)2ULL): goto ma26; default: goto ma27; }
ma24:;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t30 != ((uint8_t)0ULL)) goto mn29;
            gm_t5jnq73fben73 t31;
            gm_tnvnsr1bfvtbd t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(gm_tnvnsr1bfvtbd, (uint8_t *)&t31 + 0, t32);
            gm_tl5vm0tcqvdvb t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t33 + 8), t31);
            t22 = t33;
            goto me23;
mn29:;
ma25:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t35 != ((uint8_t)1ULL)) goto mn34;
            gm_t5jnq73fben73 t36;
            gm_tbgd14esf0jvi t37;
            memset((uint8_t *)&t37 + 12, 0, 4);
            gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t37 + 0, t38);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t39);
            gm_tnvnsr1bfvtbd t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 36, 0);
            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t40), t37);
            ((uint8_t *)&t40)[32] = 1;
            GM_ST(gm_tnvnsr1bfvtbd, (uint8_t *)&t36 + 0, t40);
            gm_tl5vm0tcqvdvb t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t41 + 8), t36);
            t22 = t41;
            goto me23;
mn34:;
ma26:;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t43 != ((uint8_t)2ULL)) goto mn42;
            gm_t5jnq73fben73 t44;
            gm_tbgd14esf0jvi t45;
            memset((uint8_t *)&t45 + 12, 0, 4);
            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
            gm_tl874t2atnlh3 t47 = GM_CONV(gm_tl874t2atnlh3, gm_f_shrink_to_hi_5bdve33(GM_CONV(gm_cseahvg8v7p0vs, t46)));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t45 + 0, t47);
            gm_b16_8 t48;
            GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s9ln4rnb6talr));
            GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t48);
            gm_tnvnsr1bfvtbd t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 36, 0);
            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t49), t45);
            ((uint8_t *)&t49)[32] = 1;
            GM_ST(gm_tnvnsr1bfvtbd, (uint8_t *)&t44 + 0, t49);
            gm_tl5vm0tcqvdvb t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t50 + 8), t44);
            t22 = t50;
            goto me23;
mn42:;
ma27:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 254, 26);
me23:;
            F.l4 = t22;
        }
        {
            void * t51 = gm_l0;
            void * t52 = (void *)((((uint8_t *)t51) + 8) + 40);
            gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 16));
            gm_tftb1odc739t5 t54 = gm_f_put_amueool(t52, GM_CONV(gm_cseahvg8v7p0vs, t53), &(*(gm_tl5vm0tcqvdvb *)((uint8_t *)&F.l4)));
            gm_tftb1odc739t5 t55 = t54;
            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_src10tru552hn49, 259, 13);
            gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
        }
        goto me3;
mn14:;
ma5:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)2ULL)) goto mn57;
        gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t59;
        {
            void * t61 = gm_l0;
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)1ULL));
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sjg1ri23hm753));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(5));
            gm_b0_1 t64 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t61, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)), t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tnvnsr1bfvtbd t65;
            memset(&t65, 0, sizeof t65);
            F.l6 = t65;
        }
        {
            uint8_t t67 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)1ULL)));
            if (!t67) goto ie66;
            {
                gm_tbgd14esf0jvi t69;
                memset((uint8_t *)&t69 + 12, 0, 4);
                void * t70 = gm_l1;
                gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 48));
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t69 + 0, t71);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sfdbfrv46lihd));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
                GM_ST(gm_b16_8, (uint8_t *)&t69 + 16, t72);
                gm_tnvnsr1bfvtbd t73;
                GM_ST(uint8_t, (uint8_t *)&t73 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t73 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t73 + 36, 0);
                GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t73), t69);
                ((uint8_t *)&t73)[32] = 1;
                F.l6 = t73;
            }
            goto ix66;
ie66:;
ix66:;
        }
        {
            void * t74 = gm_l0;
            void * t75 = (void *)((((uint8_t *)t74) + 8) + 40);
            gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
            gm_t5jnq73fben73 t77;
            memcpy((uint8_t *)&t77 + 0, ((uint8_t *)&F.l6), 40);
            gm_tl5vm0tcqvdvb t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t5jnq73fben73, ((uint8_t *)&t78 + 8), t77);
            gm_tftb1odc739t5 t79 = gm_f_put_amueool(t75, GM_CONV(gm_cseahvg8v7p0vs, t76), &t78);
            gm_tftb1odc739t5 t80 = t79;
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_src10tru552hn49, 267, 13);
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        }
        goto me3;
mn57:;
ma6:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t83 != ((uint8_t)6ULL)) goto mn82;
        void * t84 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t84;
        void * t85 = gm_l0;
        void * t86 = gm_l7;
        gm_t9qkh0rsooc8d t87;
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)1ULL));
        gm_b0_1 t88 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t85, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn82:;
ma7:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t90 != ((uint8_t)7ULL)) goto mn89;
        gm_tagc77imt2chm t91 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t91;
        {
            void * t93 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
            uint8_t t94;
            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
            if (t97 != ((uint8_t)6ULL)) goto mn96;
            t94 = ((uint8_t)1);
            goto me95;
mn96:;
            t94 = ((uint8_t)0);
            goto me95;
mn98:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 272, 26);
me95:;
            gm_l9 = t94;
        }
        {
            gm_th2lqbo77pvsc t99 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&gm_l8) + 8));
            gm_th2lqbo77pvsc t100 = t99;
            gm_b16_8 t101;
            if (((((uint8_t *)&t100))[16] != 0)) {
                gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 0));
                t101 = t102;
            } else {
                gm_b16_8 t103 = gm_f_empty_slice_r9g5q6n();
                t101 = t103;
            }
            gm_l10 = t101;
        }
        {
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                gm_l11 = t105;
            }
            {
                for (;;) {
                    uint64_t t107 = gm_l12;
                    uint64_t t108 = gm_l11;
                    uint8_t t109 = (t107 < t108);
                    if (!t109) goto lb106;
                    {
                        uint64_t t112 = gm_l12;
                        uint64_t t113 = t112;
                        uint64_t t116 = GM_LD(uint64_t, ((uint8_t *)&t113));
                        gm_l13 = t116;
                        {
                            gm_t9qkh0rsooc8d t118;
                            uint64_t t120 = gm_l13;
                            uint64_t t121 = (uintptr_t)t120;
                            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_src10tru552hn49, 275, 36);
                            uint8_t t123;
                            if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t121 * 120) + 0))[32] != 0)) goto mn125;
                            t123 = ((uint8_t)1);
                            goto me124;
mn125:;
                            t123 = ((uint8_t)0);
                            goto me124;
mn126:;
                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 275, 36);
me124:;
                            if (!t123) goto ie119;
                            uint8_t t127 = gm_l9;
                            if (!t127) goto ie119;
                            gm_t9qkh0rsooc8d t129;
                            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)2ULL));
                            gm_t9qkh0rsooc8d t130 = t129;
                            t118 = t130;
                            goto ix119;
ie119:;
                            gm_t9qkh0rsooc8d t132;
                            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)1ULL));
                            gm_t9qkh0rsooc8d t133 = t132;
                            t118 = t133;
ix119:;
                            gm_l14 = t118;
                        }
                        {
                            void * t134 = gm_l0;
                            uint64_t t135 = gm_l13;
                            uint64_t t136 = (uintptr_t)t135;
                            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_src10tru552hn49, 276, 28);
                            void * t138 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t136 * 120) + 40);
                            gm_t9qkh0rsooc8d t139 = gm_l14;
                            gm_b0_1 t140 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t134, t138, t139);
                            if (gm_unw.kind) {
                                goto gm_ll110;
                            }
                        }
                        goto me114;
mn115:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 274, 13);
me114:;
                    }
lc106:;
                    {
                        uint64_t t142 = gm_l12;
                        uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                        uint64_t t145 = r143;
                        gm_l12 = t145;
                    }
                }
gm_ll110:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb106; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc106; }
                }
                goto gm_lad;
lb106:;
            }
        }
        goto me3;
mn89:;
ma8:;
        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t147 != ((uint8_t)8ULL)) goto mn146;
        gm_b16_8 t148 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t148;
        {
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                uint64_t t151 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                gm_l16 = t151;
            }
            {
                for (;;) {
                    uint64_t t153 = gm_l17;
                    uint64_t t154 = gm_l16;
                    uint8_t t155 = (t153 < t154);
                    if (!t155) goto lb152;
                    {
                        uint64_t t158 = gm_l17;
                        uint64_t t159 = t158;
                        uint64_t t162 = GM_LD(uint64_t, ((uint8_t *)&t159));
                        gm_l18 = t162;
                        {
                            void * t164 = gm_l0;
                            uint64_t t165 = gm_l18;
                            uint64_t t166 = (uintptr_t)t165;
                            uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t166 >= (uintptr_t)(t167)) gm_trap_index(t166, t167, gm_src10tru552hn49, 280, 47);
                            void * t168 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t166 * 120) + 40);
                            gm_t9qkh0rsooc8d t169;
                            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)1ULL));
                            gm_b0_1 t170 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t164, t168, t169);
                            if (gm_unw.kind) {
                                goto gm_ll156;
                            }
                        }
                        goto me160;
mn161:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 280, 13);
me160:;
                    }
lc152:;
                    {
                        uint64_t t172 = gm_l17;
                        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
                        uint64_t t175 = r173;
                        gm_l17 = t175;
                    }
                }
gm_ll156:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb152; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc152; }
                }
                goto gm_lad;
lb152:;
            }
        }
        goto me3;
mn146:;
ma9:;
        uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t177 != ((uint8_t)3ULL)) goto mn176;
        void * t178 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l19 = t178;
        void * t179 = gm_l0;
        void * t180 = gm_l19;
        gm_b0_1 t181 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t179, t180);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn176:;
ma10:;
        uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t183 != ((uint8_t)5ULL)) goto mn182;
        void * t184 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l20 = t184;
        void * t185 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l21 = t185;
        {
            void * t187 = gm_l0;
            void * t188 = gm_l20;
            gm_b0_1 t189 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t187, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t190 = gm_l0;
            void * t191 = gm_l21;
            gm_b0_1 t192 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t190, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn182:;
ma11:;
        uint8_t t196 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t196 != ((uint8_t)0ULL)) goto pa195;
        goto po194;
pa195:;
        uint8_t t198 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t198 != ((uint8_t)4ULL)) goto pa197;
        goto po194;
pa197:;
        goto mn193;
po194:;
        goto me3;
mn193:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 251, 5);
me3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_label_0jfjsf5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_pop_label_0jfjsf5 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    gm_t16on0i8enr1i gm_l3; /* entry */
    uint32_t gm_l4; /* file */
    gm_b16_8 gm_l5; /* rest */
    uint64_t gm_l6; /* colon */
    uint64_t gm_l7; /* after */
    uint32_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 953u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_t1s75i08qtptd t2 = F.l1;
        gm_t1s75i08qtptd t3 = t2;
        gm_t08olq97rscn3 t4;
        if (((((uint8_t *)&t3))[32] != 0)) {
            gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 224);
        gm_tgqc207d6ne28 t10;
        gm_f_pop_h4mkmpb((void *)&t10, t9);
        gm_tgqc207d6ne28 t11 = t10;
        gm_t16on0i8enr1i t12;
        if (((((uint8_t *)&t11))[32] != 0)) {
            gm_t16on0i8enr1i t13 = GM_LD(gm_t16on0i8enr1i, (((uint8_t *)&t11) + 0));
            t12 = t13;
        } else {
            gm_b0_1 t14;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l3 = t12;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 28));
        if (!t17) goto ie16;
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 0));
        gm_l4 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        uint32_t t24 = gm_l4;
        void * t25 = gm_f_file_o15fqku(t23, t24);
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 16) + 8);
        void * t27 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 16)));
        uint32_t t28 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
        uint64_t t29 = (uint64_t)t28;
        uint64_t t30 = (uintptr_t)t29;
        if (t30 > (uintptr_t)t26 || (uintptr_t)t26 > (uintptr_t)t26) gm_trap_slice(t30, (uintptr_t)t26, t26, gm_src10tru552hn49, 398, 13);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t27 + t30 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)t26 - t30));
        gm_l5 = t31;
    }
    {
        gm_b16_8 t32 = gm_l5;
        gm_tf5srh4mp4b3i t33 = gm_f_index_of_hdeg351(t32, ((uint8_t)58ULL));
        gm_tf5srh4mp4b3i t34 = t33;
        uint64_t t35;
        if (((((uint8_t *)&t34))[8] != 0)) {
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            t35 = ((uint64_t)0ULL);
        }
        gm_l6 = t35;
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t38 = gm_l6;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 401, 11);
            uint64_t t41 = r39;
            uint64_t t42 = gm_l7;
            uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
            if (o44) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 401, 11);
            uint64_t t45 = r43;
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t47 = (t45 < t46);
            if (!t47) goto lb37;
            uint64_t t48 = gm_l6;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 401, 49);
            uint64_t t51 = r49;
            uint64_t t52 = gm_l7;
            uint64_t r53; bool o54 = __builtin_add_overflow(t51, t52, &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 401, 49);
            uint64_t t55 = r53;
            uint64_t t56 = (uintptr_t)t55;
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_src10tru552hn49, 401, 44);
            uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t56 * 1));
            gm_tqs5p4n2tq19v t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t59 + 1), ((uint8_t)9ULL));
            gm_tqs5p4n2tq19v t60 = t59;
            uint8_t t61 = 0;
            uint8_t t62 = t58;
            if (gm_eqjmccqdb90f(((uint8_t *)&t60) + 0, ((uint8_t *)&t62))) t61 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t60) + 1, ((uint8_t *)&t62))) t61 = 1;
            uint8_t t63 = (uint8_t)(t61);
            if (!t63) goto lb37;
            {
                uint64_t t66 = gm_l7;
                uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                if (o68) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 401, 85);
                uint64_t t69 = r67;
                gm_l7 = t69;
            }
lc37:;
        }
lb37:;
    }
    {
        uint32_t t70 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 8));
        uint64_t t71 = gm_l6;
        uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
        if (o73) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 402, 25);
        uint64_t t74 = r72;
        uint64_t t75 = gm_l7;
        uint64_t r76; bool o77 = __builtin_add_overflow(t74, t75, &r76);
        if (o77) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 402, 25);
        uint64_t t78 = r76;
        uint32_t t79 = (uint32_t)t78;
        uint32_t r80; bool o81 = __builtin_add_overflow(t70, t79, &r80);
        if (o81) gm_trap("integer overflow in `+`", gm_src10tru552hn49, 402, 12);
        uint32_t t82 = r80;
        gm_l8 = t82;
    }
    {
        gm_tl874t2atnlh3 t83 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s6vg88ce880ha));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(12));
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_tnpifv20irnph t86;
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
        gm_tabb710d9mq2e t87;
        gm_f_format_jm2cnr1((void *)&t87, (void *)NULL, cx, NULL, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t88 = t87;
        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_src10tru552hn49, 403, 44);
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
        gm_b16_8 t90 = t89;
        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
        gm_f_lint_diag_4vra4qu((void *)&F.l9, (void *)NULL, cx, NULL, t83, t84, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t92 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s71j6n6s8up14));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(16));
        uint32_t t94 = gm_l4;
        uint32_t t95 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 16) + 4));
        uint32_t t96 = gm_l8;
        gm_tl874t2atnlh3 t97;
        GM_ST(uint32_t, ((uint8_t *)&t97 + 0), t94);
        GM_ST(uint32_t, ((uint8_t *)&t97 + 4), t95);
        GM_ST(uint32_t, ((uint8_t *)&t97 + 8), t96);
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(0));
        gm_b0_1 t99 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t92, t93, t97, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t100 = gm_l0;
        gm_b0_1 t101 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t100, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_push_l0e5v4c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_label_hn6s4vp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_push_label_hn6s4vp F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_t1s75i08qtptd t2 = F.l1;
        gm_t1s75i08qtptd t3 = t2;
        gm_t08olq97rscn3 t4;
        if (((((uint8_t *)&t3))[32] != 0)) {
            gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 224);
        gm_t16on0i8enr1i t10;
        memset((uint8_t *)&t10 + 29, 0, 3);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l2) + 16));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 16, t12);
        GM_ST(uint8_t, (uint8_t *)&t10 + 28, ((uint8_t)0));
        gm_tftb1odc739t5 t13 = gm_f_push_em9e1kh(t9, &t10);
        gm_tftb1odc739t5 t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_src10tru552hn49, 388, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t5hib78lmj2mf gm_f_shape_2jg6iuk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shape_2jg6iuk F;
    void * gm_l0; /* b */
    void * gm_l1; /* x */
    uint8_t gm_l2; /* v */
    void * gm_l3; /* r */
    void * gm_l4; /* v */
    uint8_t gm_l5; /* b2 */
    GM_STACK_CHECK_SHALLOW(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
        uint8_t t5 = (!(t4 == ((uint64_t)1ULL)));
        if (!t5) goto ie2;
        {
            gm_t5hib78lmj2mf t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 8) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_src10tru552hn49, 568, 17);
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 8)) + t10 * 144) + 8) + 0));
        if (t13 != ((uint8_t)2ULL)) goto ie9;
        void * t14 = GM_LD(void *, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 8)) + t10 * 144) + 8) + 8));
        gm_l1 = t14;
        {
            void * t16 = gm_l1;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma20; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma20; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma20; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma20; case ((uint8_t)22ULL): goto ma20; case ((uint8_t)23ULL): goto ma20; case ((uint8_t)24ULL): goto ma20; case ((uint8_t)25ULL): goto ma20; case ((uint8_t)26ULL): goto ma20; case ((uint8_t)27ULL): goto ma20; case ((uint8_t)28ULL): goto ma20; case ((uint8_t)29ULL): goto ma20; case ((uint8_t)30ULL): goto ma20; case ((uint8_t)31ULL): goto ma20; case ((uint8_t)32ULL): goto ma20; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma20; case ((uint8_t)35ULL): goto ma20; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma20; case ((uint8_t)38ULL): goto ma20; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma19; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; default: goto ma20; }
ma18:;
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            if (t24 != ((uint8_t)3ULL)) goto mn23;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 8));
            gm_l2 = t25;
            uint8_t t26 = gm_l2;
            gm_t5sj3u8e14oco t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t27 + 1), t26);
            gm_t5hib78lmj2mf t28;
            GM_ST(gm_t5sj3u8e14oco, ((uint8_t *)&t28), t27);
            ((uint8_t *)&t28)[2] = 1;
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
            goto me17;
mn23:;
ma19:;
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            if (t31 != ((uint8_t)45ULL)) goto mn30;
            void * t32 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l3 = t32;
            {
                void * t34 = gm_l3;
                void * t35 = t34;
                void * t36;
                if ((GM_LD(void *, ((uint8_t *)&t35)) != 0)) {
                    void * t37 = GM_LD(void *, (((uint8_t *)&t35) + 0));
                    t36 = t37;
                } else {
                    gm_t5hib78lmj2mf t38;
                    memset(&t38, 0, sizeof t38);
                    uint8_t t39 = 0;
                    F.ret = t38;
                    goto gm_fret;
                }
                gm_l4 = t36;
            }
            {
                void * t41 = gm_l4;
                uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                if (t42 != ((uint8_t)3ULL)) goto ie40;
                uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 8));
                gm_l5 = t43;
                {
                    uint8_t t45 = gm_l5;
                    gm_t5sj3u8e14oco t46;
                    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t46 + 1), t45);
                    gm_t5hib78lmj2mf t47;
                    GM_ST(gm_t5sj3u8e14oco, ((uint8_t *)&t47), t46);
                    ((uint8_t *)&t47)[2] = 1;
                    uint8_t t48 = 0;
                    F.ret = t47;
                    goto gm_fret;
                }
                goto ix40;
ie40:;
                {
                    gm_t5hib78lmj2mf t50;
                    memset(&t50, 0, sizeof t50);
                    uint8_t t51 = 0;
                    F.ret = t50;
                    goto gm_fret;
                }
ix40:;
            }
            goto me17;
mn30:;
ma20:;
            gm_t5hib78lmj2mf t53;
            memset(&t53, 0, sizeof t53);
            uint8_t t54 = 0;
            F.ret = t53;
            goto gm_fret;
            goto me17;
mn52:;
ma21:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 569, 5);
me17:;
        }
        goto ix9;
ie9:;
        {
            gm_t5hib78lmj2mf t56;
            memset(&t56, 0, sizeof t56);
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
ix9:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_hi_5bdve33(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_fn4opan(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_sig_fn4opan F;
    void * gm_l0; /* w */
    void * gm_l1; /* sg */
    uint8_t gm_l2; /* literal */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l6; /* ty_param */
    void * gm_l7; /* t */
    gm_t08olq97rscn3 gm_l8; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    void * gm_l12; /* t */
    void * gm_l13; /* d */
    void * gm_l14; /* r */
    GM_STACK_CHECK(&F, 756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tf2orqcrphmek t16 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 56));
                    F.l5 = t16;
                    {
                        uint8_t t19 = gm_l2;
                        if (!t19) goto ie18;
                        {
                            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 16));
                            gm_l6 = t21;
                        }
                        {
                            void * t23 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                            void * t24 = t23;
                            if (!(GM_LD(void *, ((uint8_t *)&t24)) != 0)) goto ie22;
                            void * t25 = GM_LD(void *, ((uint8_t *)&t24));
                            gm_l7 = t25;
                            {
                                void * t28 = gm_l7;
                                uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                                if (t29 != ((uint8_t)6ULL)) goto ie27;
                                gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t28) + 8) + 8));
                                gm_l8 = t30;
                                {
                                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                                    gm_t6j6smj7qoctu t34;
                                    gm_b16_8 t35;
                                    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s2737i1iq57nf));
                                    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(4));
                                    GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
                                    gm_b16_8 t36;
                                    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sgbumttiou4de));
                                    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(7));
                                    GM_ST(gm_b16_8, (uint8_t *)&t34 + 16, t36);
                                    gm_t6j6smj7qoctu t37 = t34;
                                    uint8_t t38 = 0;
                                    gm_b16_8 t39 = t33;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t37) + 0, ((uint8_t *)&t39))) t38 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t37) + 16, ((uint8_t *)&t39))) t38 = 1;
                                    uint8_t t40 = (uint8_t)(t38);
                                    if (!t40) goto ie32;
                                    {
                                        gm_l6 = ((uint8_t)1);
                                    }
                                    goto ix32;
ie32:;
ix32:;
                                }
                                goto ix27;
ie27:;
ix27:;
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        {
                            uint8_t t43 = gm_l6;
                            uint8_t t44 = (uint8_t)!t43;
                            if (!t44) goto ie42;
                            {
                                {
                                    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                    gm_l9 = t47;
                                }
                                {
                                    gm_l10 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t49 = gm_l10;
                                        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                        uint8_t t51 = (t49 < t50);
                                        if (!t51) goto lb48;
                                        {
                                            uint64_t t54 = gm_l10;
                                            uint64_t t55 = (uintptr_t)t54;
                                            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                            gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t55 * 32));
                                            gm_l11 = t59;
                                            {
                                                void * t61 = gm_l0;
                                                gm_t9qkh0rsooc8d t62;
                                                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)1ULL));
                                                gm_b16_8 t63;
                                                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_seqndsmrihepc));
                                                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(9));
                                                gm_b0_1 t64 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t61, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)), t62, t63);
                                                if (gm_unw.kind) {
                                                    goto gm_ll52;
                                                }
                                            }
                                            goto me57;
mn58:;
                                            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 509, 17);
me57:;
                                        }
lc48:;
                                        {
                                            uint64_t t66 = gm_l10;
                                            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                                            uint64_t t69 = r67;
                                            gm_l10 = t69;
                                        }
                                    }
gm_ll52:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
                                    }
                                    goto gm_ll9;
lb48:;
                                }
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    {
                        void * t71 = GM_LD(void *, (((uint8_t *)&F.l5) + 24));
                        void * t72 = t71;
                        if (!(GM_LD(void *, ((uint8_t *)&t72)) != 0)) goto ie70;
                        void * t73 = GM_LD(void *, ((uint8_t *)&t72));
                        gm_l12 = t73;
                        {
                            void * t75 = gm_l0;
                            void * t76 = gm_l12;
                            gm_b0_1 t77 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix70;
ie70:;
ix70:;
                    }
                    {
                        void * t79 = GM_LD(void *, (((uint8_t *)&F.l5) + 32));
                        void * t80 = t79;
                        if (!(GM_LD(void *, ((uint8_t *)&t80)) != 0)) goto ie78;
                        void * t81 = GM_LD(void *, ((uint8_t *)&t80));
                        gm_l13 = t81;
                        {
                            void * t83 = gm_l0;
                            void * t84 = gm_l13;
                            gm_b0_1 t85 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t83, t84);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto ix78;
ie78:;
ix78:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 499, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t87 = gm_l4;
                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                    uint64_t t90 = r88;
                    gm_l4 = t90;
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
        void * t92 = gm_l1;
        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 24));
        void * t94 = t93;
        if (!(GM_LD(void *, ((uint8_t *)&t94)) != 0)) goto ie91;
        void * t95 = GM_LD(void *, ((uint8_t *)&t94));
        gm_l14 = t95;
        {
            void * t97 = gm_l0;
            void * t98 = gm_l14;
            gm_b0_1 t99 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t97, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix91;
ie91:;
ix91:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_stmt_j3ls0p3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_j3ls0p3 F;
    void * gm_l0; /* w */
    void * gm_l1; /* s */
    gm_b16_8 gm_l3; /* places */
    gm_tjsp7bhdn2e8j gm_l4; /* op */
    void * gm_l5; /* value */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* e */
    void * gm_l10; /* body */
    void * gm_l11; /* wh */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l16; /* e */
    void * gm_l17; /* e */
    void * gm_l19; /* value */
    void * gm_l20; /* fallback */
    GM_STACK_CHECK(&F, 1282u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_tv2uf4leil5hq t17 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t2) + 8) + 8) + 16));
        F.l2 = t17;
        void * t18 = gm_l0;
        void * t19 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t20 = gm_f_binding_stmt_80l1chq((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn15:;
ma5:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l3 = t23;
        gm_tjsp7bhdn2e8j t24 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l4 = t24;
        void * t25 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 24));
        gm_l5 = t25;
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l6 = t28;
            }
            {
                for (;;) {
                    uint64_t t30 = gm_l7;
                    uint64_t t31 = gm_l6;
                    uint8_t t32 = (t30 < t31);
                    if (!t32) goto lb29;
                    {
                        uint64_t t35 = gm_l7;
                        uint64_t t36 = t35;
                        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t36));
                        gm_l8 = t39;
                        {
                            void * t41 = gm_l0;
                            uint64_t t42 = gm_l8;
                            uint64_t t43 = (uintptr_t)t42;
                            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src10tru552hn49, 328, 46);
                            void * t45 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t43 * 88);
                            gm_b0_1 t46 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t41, t45);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                        }
                        goto me37;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 328, 13);
me37:;
                    }
lc29:;
                    {
                        uint64_t t48 = gm_l7;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l7 = t51;
                    }
                }
gm_ll33:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
                }
                goto gm_lad;
lb29:;
            }
        }
        {
            uint8_t t53 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)0ULL)));
            if (!t53) goto ie52;
            {
                void * t55 = gm_l0;
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sium1s93t7o1a));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(18));
                void * t57 = gm_l5;
                gm_b0_1 t58 = gm_f_paren_at_av727r5((void *)NULL, cx, NULL, t55, t56, t57, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix52;
ie52:;
ix52:;
        }
        {
            void * t59 = gm_l0;
            void * t60 = gm_l5;
            gm_b0_1 t61 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn21:;
ma6:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)2ULL)) goto mn62;
        void * t64 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t64;
        void * t65 = gm_l0;
        void * t66 = gm_l9;
        gm_b0_1 t67 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn62:;
ma7:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)3ULL)) goto mn68;
        void * t70 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t70;
        void * t71 = gm_l0;
        void * t72 = gm_l10;
        gm_b0_1 t73 = gm_f_stmt_j3ls0p3((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn68:;
ma8:;
        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t75 != ((uint8_t)4ULL)) goto mn74;
        void * t76 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t76;
        void * t77 = gm_l0;
        void * t78 = gm_l11;
        gm_b0_1 t79 = gm_f_when_stmt_qbi9l8v((void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn74:;
ma9:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t81 != ((uint8_t)5ULL)) goto mn80;
        gm_t43d6nbdfap6p t82 = GM_LD(gm_t43d6nbdfap6p, ((((uint8_t *)t2) + 8) + 8));
        F.l12 = t82;
        {
            void * t84 = gm_l0;
            void * t85 = (void *)(((uint8_t *)&F.l12) + 40);
            gm_b0_1 t86 = gm_f_cond_e0272rd((void *)NULL, cx, NULL, t84, t85, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t88 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 40) + 0));
                gm_l13 = t88;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t90 = gm_l14;
                    uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t92 = (t90 < t91);
                    if (!t92) goto lb89;
                    {
                        uint64_t t95 = gm_l14;
                        uint64_t t96 = (uintptr_t)t95;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_tq5j0547p8452 t100 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t96 * 80));
                        F.l15 = t100;
                        {
                            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 0));
                            if (t103 != ((uint8_t)1ULL)) goto ie102;
                            void * t104 = GM_LD(void *, (((uint8_t *)&F.l15) + 8));
                            gm_l16 = t104;
                            {
                                void * t107 = gm_l16;
                                uint8_t t108 = (((GM_LD(uint8_t, (((uint8_t *)t107) + 8) + 0) == ((uint8_t)3ULL)) && ((GM_LD(uint8_t, ((((uint8_t *)t107) + 8) + 8)) != 0) == (((uint8_t)0) != 0))));
                                if (!t108) goto ie106;
                                {
                                    void * t110 = gm_l0;
                                    void * t111 = gm_l16;
                                    gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t111) + 72));
                                    gm_b16_8 t113;
                                    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_spic2fa42u00e));
                                    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(18));
                                    gm_b16_8 t114;
                                    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sgrap32255cto));
                                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(46));
                                    gm_t4gut2c9fj1kt t115;
                                    gm_f_lint_diag_4vra4qu((void *)&t115, (void *)NULL, cx, NULL, t112, t113, t114);
                                    if (gm_unw.kind) {
                                        goto gm_ll93;
                                    }
                                    gm_b0_1 t116 = gm_f_push_l0e5v4c((void *)NULL, cx, NULL, t110, &t115);
                                    if (gm_unw.kind) {
                                        goto gm_ll93;
                                    }
                                }
                                goto ix106;
ie106:;
ix106:;
                            }
                            goto ix102;
ie102:;
ix102:;
                        }
                        goto me98;
mn99:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 337, 13);
me98:;
                    }
lc89:;
                    {
                        uint64_t t118 = gm_l14;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l14 = t121;
                    }
                }
gm_ll93:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb89; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc89; }
                }
                goto gm_lad;
lb89:;
            }
        }
        {
            void * t122 = gm_l0;
            gm_b0_1 t123 = gm_f_push_label_hn6s4vp((void *)NULL, cx, NULL, t122, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l12) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t124 = gm_l0;
            void * t125 = (void *)(((uint8_t *)&F.l12) + 72);
            gm_b0_1 t126 = gm_f_block_btrailr((void *)NULL, cx, NULL, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t127 = gm_l0;
            gm_b0_1 t128 = gm_f_pop_label_0jfjsf5((void *)NULL, cx, NULL, t127, &(*(gm_t1s75i08qtptd *)(((uint8_t *)&F.l12) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn80:;
ma10:;
        uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t130 != ((uint8_t)6ULL)) goto mn129;
        void * t131 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l17 = t131;
        void * t132 = gm_l0;
        void * t133 = gm_l17;
        gm_b0_1 t134 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t132, t133);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn129:;
ma11:;
        uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t136 != ((uint8_t)7ULL)) goto mn135;
        gm_t0d02142k4tu8 t137 = GM_LD(gm_t0d02142k4tu8, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l18 = t137;
        void * t138 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 64));
        gm_l19 = t138;
        void * t139 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 72));
        gm_l20 = t139;
        {
            void * t141 = gm_l0;
            void * t142 = (void *)((uint8_t *)&F.l18);
            gm_t9qkh0rsooc8d t143;
            GM_ST(uint8_t, ((uint8_t *)&t143 + 0), ((uint8_t)0ULL));
            gm_b0_1 t144 = gm_f_pattern_8qtr7os((void *)NULL, cx, NULL, t141, t142, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t145 = gm_l0;
            void * t146 = gm_l19;
            gm_b0_1 t147 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t148 = gm_l0;
            void * t149 = gm_l20;
            gm_b0_1 t150 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn135:;
ma12:;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t152 != ((uint8_t)8ULL)) goto mn151;
        gm_t79rbdv4rafe5 t153 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l21 = t153;
        void * t154 = gm_l0;
        void * t155 = (void *)((uint8_t *)&F.l21);
        gm_b0_1 t156 = gm_f_block_btrailr((void *)NULL, cx, NULL, t154, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn151:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 325, 5);
me3:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_kspioh6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_top_level_name_dv4kpc9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2) {
    struct gm_fr_top_level_name_dv4kpc9 F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* b */
    void * gm_l3; /* inner */
    void * gm_l4; /* i */
    uint8_t gm_l5; /* c_union */
    gm_b16_8 gm_l6; /* attrs */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l10; /* s */
    void * gm_l11; /* u */
    void * gm_l12; /* e */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    GM_STACK_CHECK(&F, 814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
        gm_l3 = t3;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb4;
            {
                void * t8 = gm_l3;
                uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
                if (t9 != ((uint8_t)35ULL)) goto ie7;
                void * t10 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
                gm_l4 = t10;
                {
                    void * t12 = gm_l4;
                    gm_l3 = t12;
                }
                goto ix7;
ie7:;
                {
                    goto lb4;
                }
ix7:;
            }
lc4:;
        }
lb4:;
    }
    {
        void * t14 = gm_l3;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma19; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma19; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma19; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma19; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; case ((uint8_t)27ULL): goto ma19; case ((uint8_t)28ULL): goto ma19; case ((uint8_t)29ULL): goto ma19; case ((uint8_t)30ULL): goto ma19; case ((uint8_t)31ULL): goto ma19; case ((uint8_t)32ULL): goto ma16; case ((uint8_t)33ULL): goto ma16; case ((uint8_t)34ULL): goto ma16; case ((uint8_t)35ULL): goto ma19; case ((uint8_t)36ULL): goto ma17; case ((uint8_t)37ULL): goto ma19; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma19; case ((uint8_t)40ULL): goto ma19; case ((uint8_t)41ULL): goto ma19; case ((uint8_t)42ULL): goto ma19; case ((uint8_t)43ULL): goto ma19; case ((uint8_t)44ULL): goto ma19; case ((uint8_t)45ULL): goto ma19; case ((uint8_t)46ULL): goto ma19; case ((uint8_t)47ULL): goto ma19; case ((uint8_t)48ULL): goto ma19; case ((uint8_t)49ULL): goto ma19; case ((uint8_t)50ULL): goto ma19; default: goto ma19; }
ma16:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t25 != ((uint8_t)32ULL)) goto pa24;
        goto po23;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t27 != ((uint8_t)33ULL)) goto pa26;
        goto po23;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t29 != ((uint8_t)34ULL)) goto pa28;
        goto po23;
pa28:;
        goto mn22;
po23:;
        void * t30 = gm_l0;
        gm_t9qkh0rsooc8d t31;
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
        gm_b0_1 t33 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t30, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me15;
mn22:;
ma17:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t35 != ((uint8_t)36ULL)) goto mn34;
        void * t36 = gm_l0;
        gm_t9qkh0rsooc8d t37;
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sq0dqiq83b924));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(8));
        gm_b0_1 t39 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t36, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me15;
mn34:;
ma18:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t41 != ((uint8_t)38ULL)) goto mn40;
        void * t42 = gm_l0;
        gm_t9qkh0rsooc8d t43;
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)1ULL));
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sg3kpa3h16no3));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
        gm_b0_1 t45 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t42, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)), t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me15;
mn40:;
ma19:;
        goto me15;
mn46:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 134, 5);
me15:;
    }
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        void * t49 = gm_l2;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 40));
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
        if (t51 != ((uint8_t)35ULL)) goto ie48;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((((uint8_t *)t50) + 8) + 8) + 0));
        gm_l6 = t52;
        {
            {
                gm_b16_8 t55 = gm_l6;
                gm_l7 = t55;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t57 = gm_l8;
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t59 = (t57 < t58);
                    if (!t59) goto lb56;
                    {
                        uint64_t t62 = gm_l8;
                        uint64_t t63 = (uintptr_t)t62;
                        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_tkacitepg5fk0 t67 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t63 * 72));
                        F.l9 = t67;
                        {
                            uint8_t t70 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l9) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l9) + 0) + 0)), gm_suuv0qpi4v4e4, 7) == 0));
                            if (!t70) goto ie69;
                            {
                                gm_l5 = ((uint8_t)1);
                            }
                            goto ix69;
ie69:;
ix69:;
                        }
                        goto me65;
mn66:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 143, 9);
me65:;
                    }
lc56:;
                    {
                        uint64_t t73 = gm_l8;
                        uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                        uint64_t t76 = r74;
                        gm_l8 = t76;
                    }
                }
lb56:;
            }
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        void * t77 = gm_l3;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
        switch (t84) { case ((uint8_t)0ULL): goto ma82; case ((uint8_t)1ULL): goto ma82; case ((uint8_t)2ULL): goto ma82; case ((uint8_t)3ULL): goto ma82; case ((uint8_t)4ULL): goto ma82; case ((uint8_t)5ULL): goto ma82; case ((uint8_t)6ULL): goto ma82; case ((uint8_t)7ULL): goto ma82; case ((uint8_t)8ULL): goto ma82; case ((uint8_t)9ULL): goto ma82; case ((uint8_t)10ULL): goto ma82; case ((uint8_t)11ULL): goto ma82; case ((uint8_t)12ULL): goto ma82; case ((uint8_t)13ULL): goto ma82; case ((uint8_t)14ULL): goto ma82; case ((uint8_t)15ULL): goto ma82; case ((uint8_t)16ULL): goto ma82; case ((uint8_t)17ULL): goto ma82; case ((uint8_t)18ULL): goto ma82; case ((uint8_t)19ULL): goto ma82; case ((uint8_t)20ULL): goto ma82; case ((uint8_t)21ULL): goto ma82; case ((uint8_t)22ULL): goto ma82; case ((uint8_t)23ULL): goto ma82; case ((uint8_t)24ULL): goto ma82; case ((uint8_t)25ULL): goto ma82; case ((uint8_t)26ULL): goto ma82; case ((uint8_t)27ULL): goto ma82; case ((uint8_t)28ULL): goto ma82; case ((uint8_t)29ULL): goto ma82; case ((uint8_t)30ULL): goto ma82; case ((uint8_t)31ULL): goto ma82; case ((uint8_t)32ULL): goto ma79; case ((uint8_t)33ULL): goto ma80; case ((uint8_t)34ULL): goto ma81; case ((uint8_t)35ULL): goto ma82; case ((uint8_t)36ULL): goto ma82; case ((uint8_t)37ULL): goto ma82; case ((uint8_t)38ULL): goto ma82; case ((uint8_t)39ULL): goto ma82; case ((uint8_t)40ULL): goto ma82; case ((uint8_t)41ULL): goto ma82; case ((uint8_t)42ULL): goto ma82; case ((uint8_t)43ULL): goto ma82; case ((uint8_t)44ULL): goto ma82; case ((uint8_t)45ULL): goto ma82; case ((uint8_t)46ULL): goto ma82; case ((uint8_t)47ULL): goto ma82; case ((uint8_t)48ULL): goto ma82; case ((uint8_t)49ULL): goto ma82; case ((uint8_t)50ULL): goto ma82; default: goto ma82; }
ma79:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
        if (t86 != ((uint8_t)32ULL)) goto mn85;
        void * t87 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
        gm_l10 = t87;
        void * t88 = gm_l0;
        void * t89 = gm_l10;
        gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)t89) + 40));
        gm_b16_8 t91 = t90;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
        gm_b0_1 t93 = gm_f_fields_8ttf3eb((void *)NULL, cx, NULL, t88, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me78;
mn85:;
ma80:;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
        if (t95 != ((uint8_t)33ULL)) goto mn94;
        void * t96 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
        gm_l11 = t96;
        uint8_t t98 = gm_l5;
        uint8_t t99 = (uint8_t)!t98;
        if (!t99) goto ie97;
        {
            void * t101 = gm_l0;
            void * t102 = gm_l11;
            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)t102) + 48));
            gm_b16_8 t104 = t103;
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
            gm_b0_1 t106 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t101, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix97;
ie97:;
ix97:;
        goto me78;
mn94:;
ma81:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
        if (t108 != ((uint8_t)34ULL)) goto mn107;
        void * t109 = GM_LD(void *, ((((uint8_t *)t77) + 8) + 8));
        gm_l12 = t109;
        {
            {
                void * t112 = gm_l12;
                gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)t112) + 24));
                gm_l13 = t113;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t115 = gm_l14;
                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t117 = (t115 < t116);
                    if (!t117) goto lb114;
                    {
                        uint64_t t120 = gm_l14;
                        uint64_t t121 = (uintptr_t)t120;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_tc0sbs1tp6d7f t125 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t121 * 80));
                        F.l15 = t125;
                        {
                            void * t127 = gm_l0;
                            gm_t9qkh0rsooc8d t128;
                            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)1ULL));
                            gm_b16_8 t129;
                            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sk6ueggk5ji6v));
                            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(9));
                            gm_b0_1 t130 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t127, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l15) + 0)), t128, t129);
                            if (gm_unw.kind) {
                                goto gm_ll118;
                            }
                        }
                        goto me123;
mn124:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 151, 13);
me123:;
                    }
lc114:;
                    {
                        uint64_t t132 = gm_l14;
                        uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                        uint64_t t135 = r133;
                        gm_l14 = t135;
                    }
                }
gm_ll118:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
                }
                goto gm_lad;
lb114:;
            }
        }
        goto me78;
mn107:;
ma82:;
        goto me78;
mn136:;
ma83:;
        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 147, 5);
me78:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_upper_camel_a0jt5mh(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_use_label_8j0qqam(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_use_label_8j0qqam F;
    void * gm_l0; /* w */
    gm_t08olq97rscn3 gm_l2; /* l */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t1s75i08qtptd t2 = GM_LD(gm_t1s75i08qtptd, ((uint8_t *)a1));
        gm_t1s75i08qtptd t3 = t2;
        gm_t08olq97rscn3 t4;
        if (((((uint8_t *)&t3))[32] != 0)) {
            gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 224) + 0) + 8);
        gm_l3 = t9;
    }
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint8_t t12 = (t11 > ((uint64_t)0ULL));
            if (!t12) goto lb10;
            {
                uint64_t t15 = gm_l3;
                uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `-`", gm_src10tru552hn49, 412, 9);
                uint64_t t18 = r16;
                gm_l3 = t18;
            }
            {
                uint64_t t20 = gm_l3;
                uint64_t t21 = (uintptr_t)t20;
                void * t22 = gm_l0;
                uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 224) + 0) + 8);
                if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_src10tru552hn49, 413, 12);
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 224) + 0)) + t21 * 32) + 0));
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
                gm_b16_8 t26 = t24;
                gm_b16_8 t27 = t25;
                uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
                if (!t28) goto ie19;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = (uintptr_t)t30;
                    void * t32 = gm_l0;
                    uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 224) + 0) + 8);
                    if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_src10tru552hn49, 414, 13);
                    GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 224) + 0)) + t31 * 32) + 28), ((uint8_t)1));
                }
                {
                    gm_b0_1 t34;
                    uint8_t t35 = 0;
                    F.ret = t34;
                    goto gm_fret;
                }
                goto ix19;
ie19:;
ix19:;
            }
lc10:;
        }
lb10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_use_label_8j0qqam_c(void * a0, gm_t1s75i08qtptd a1) { gm_f_use_label_8j0qqam(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_variants_eef2p79(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_variants_eef2p79 F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l1; /* members */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 540u);
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
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 72));
                    F.l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l4) + 8));
                        F.l5 = t20;
                        void * t21 = gm_l0;
                        gm_t9qkh0rsooc8d t22;
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
                        gm_b16_8 t23;
                        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_spj9t9b8lqlpp));
                        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(7));
                        gm_b0_1 t24 = gm_f_check_name_hf7dide((void *)NULL, cx, NULL, t21, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l5) + 0)), t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn18:;
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t26 != ((uint8_t)1ULL)) goto mn25;
                        gm_t7bjbmukv26e7 t27 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l4) + 8));
                        F.l6 = t27;
                        {
                            void * t29 = gm_l0;
                            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
                            gm_b16_8 t31 = t30;
                            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                            gm_b0_1 t33 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t29, t32);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t34 = gm_l0;
                            gm_th2lqbo77pvsc t35 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l6) + 24));
                            gm_th2lqbo77pvsc t36 = t35;
                            gm_b16_8 t37;
                            if (((((uint8_t *)&t36))[16] != 0)) {
                                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 0));
                                t37 = t38;
                            } else {
                                gm_b16_8 t39 = gm_f_empty_slice_u4e74ha();
                                t37 = t39;
                            }
                            gm_b16_8 t40 = t37;
                            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                            gm_b0_1 t42 = gm_f_variants_eef2p79((void *)NULL, cx, NULL, t34, t41);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 173, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 172, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t44 = gm_l3;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l3 = t47;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_walk_file_do4uq8b F;
    void * gm_l0; /* w */
    void * gm_l1; /* file */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 161u);
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
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l4 = t15;
                    {
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l1;
                        uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_src10tru552hn49, 76, 42);
                        void * t22 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 0)) + t19 * 160);
                        gm_b0_1 t23 = gm_f_item_a3ennq0((void *)NULL, cx, NULL, t17, t22);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 76, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l3 = t28;
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
extern __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_when_stmt_qbi9l8v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_when_stmt_qbi9l8v F;
    void * gm_l0; /* w */
    void * gm_l1; /* wh */
    void * gm_l4; /* inner */
    GM_STACK_CHECK(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 8) + 96);
        void * t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t4) + 104));
        gm_tftb1odc739t5 t6 = gm_f_push_0mk7udh(t3, GM_CONV(gm_cseahvg8v7p0vs, t5));
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_src10tru552hn49, 359, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l1;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        gm_b0_1 t12 = gm_f_expr_3limf78((void *)NULL, cx, NULL, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l1;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_b0_1 t16 = gm_f_block_btrailr((void *)NULL, cx, NULL, t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = gm_l1;
        if (!(((((uint8_t *)t18) + 48))[48] != 0)) goto ie17;
        gm_tapdr2ag17fh7 t19 = GM_LD(gm_tapdr2ag17fh7, (((uint8_t *)t18) + 48));
        F.l2 = t19;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t23 != ((uint8_t)0ULL)) goto mn22;
            gm_t79rbdv4rafe5 t24 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&F.l2) + 8));
            F.l3 = t24;
            void * t25 = gm_l0;
            void * t26 = (void *)((uint8_t *)&F.l3);
            gm_b0_1 t27 = gm_f_block_btrailr((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me21;
mn22:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t29 != ((uint8_t)1ULL)) goto mn28;
            void * t30 = GM_LD(void *, (((uint8_t *)&F.l2) + 8));
            gm_l4 = t30;
            void * t31 = gm_l0;
            void * t32 = gm_l4;
            gm_b0_1 t33 = gm_f_when_stmt_qbi9l8v((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me21;
mn28:;
            gm_trap("no match arm matched the scrutinee", gm_src10tru552hn49, 363, 9);
me21:;
        }
        goto ix17;
ie17:;
ix17:;
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
