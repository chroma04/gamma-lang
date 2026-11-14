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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_binop_symbol_9ijp0eo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_block_children_r2qinti {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conds_children_3kg6846 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hdas5h7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_each_child_fm49s73 {
    gm_frame h;
    gm_t710lsrq1m8gk l39; /* w */
    gm_tsth9qu1dv0g3 l54; /* callee */
    gm_t710lsrq1m8gk l70; /* x */
    gm_b0_1 ret;
};
struct gm_fr_for_each_subpat_arlrtrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_default_spec_cdd8jng {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_pat_children_e3peovr {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_sys_name_pcdoujk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_untyped_complex_20agrmu {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* re */
    gm_tngtuiuskn938 l1; /* im */
    gm_teti411eujvf4 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_teti411eujvf4 ret;
};
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1pfh89airaa0[13];
extern const uint8_t gm_s26ul1d5snk55[9];
extern const uint8_t gm_s2jvv6mjt9vs3[13];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4jhjvgae4896[13];
extern const uint8_t gm_s59a022mrijic[9];
extern const uint8_t gm_s65khs4s753q2[11];
extern const uint8_t gm_s6f0l2e1iha9i[8];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s78m4481dj77r[9];
extern const uint8_t gm_s7ri8monh3c4h[8];
extern const uint8_t gm_s84p47580vnhr[12];
extern const uint8_t gm_s86kdga2ul6h4[9];
extern const uint8_t gm_s8o3vqolbfl8t[11];
extern const uint8_t gm_s9819enjtsd7t[10];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_saamkuuf6jrtf[8];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal1kdi1rij5t[10];
extern const uint8_t gm_sb641udihu5hs[10];
extern const uint8_t gm_sbgj0biroecr0[13];
extern const uint8_t gm_sd9o9rh5qe8e7[11];
extern const uint8_t gm_sdtlgdl6ed52a[14];
extern const uint8_t gm_se3l2nlomk2pj[9];
extern const uint8_t gm_seajfhgtfmhv7[12];
extern const uint8_t gm_sed3elknidrs3[11];
extern const uint8_t gm_sf5jsgjp29cqn[14];
extern const uint8_t gm_sfbjh3dq66kkg[17];
extern const uint8_t gm_sfcjpmkuggqo3[10];
extern const uint8_t gm_sfd9hctnph9ef[12];
extern const uint8_t gm_sfku76mf5fn5d[9];
extern const uint8_t gm_shegcik3mpeut[12];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_si8jv2q8d22kl[10];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sik1l49uru5dh[13];
extern const uint8_t gm_siph7o6oug1g2[14];
extern const uint8_t gm_siqaapb0mqsrg[14];
extern const uint8_t gm_sj49v74ko3ghf[15];
extern const uint8_t gm_sk0bnuggr6sri[14];
extern const uint8_t gm_sk1d0v1onojc1[8];
extern const uint8_t gm_sk77bvr488rq3[10];
extern const uint8_t gm_skk6lm5m17t6v[11];
extern const uint8_t gm_skm4ndn7op5l5[14];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skqa2fjpbsodp[9];
extern const uint8_t gm_skti0sb80pmgp[12];
extern const uint8_t gm_slginip33cenb[14];
extern const uint8_t gm_slkubuckp49ih[12];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smatmsv0oedot[8];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smfprcpea3ibe[10];
extern const uint8_t gm_smvhngj8mlfjc[13];
extern const uint8_t gm_sp8bmcd3m68n6[7];
extern const char gm_srcod98slk3vnra[];
extern const uint8_t gm_ssj8r83l87dr2[16];
extern const uint8_t gm_sso3tuh819o23[9];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stclkv4qnta30[9];
extern const uint8_t gm_stl47boisb31o[11];
extern const uint8_t gm_su6ipjjrnjs7p[10];
extern const uint8_t gm_suno9ilftpfht[12];
extern const uint8_t gm_sv1bu4l4kq188[8];
extern const uint8_t gm_sv9p44pp2n1jk[10];
extern const uint8_t gm_svkvl31jmj9ne[12];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_hdas5h7 F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_pat_children_e3peovr *)((&F.h)->up))->l1));
        gm_f_pat_children_e3peovr(t2, t3);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 311, 83);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_untyped_complex_20agrmu F;
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tu15p0hi2qlv6 t1;
    memcpy((uint8_t *)&t1 + 0, ((uint8_t *)a0), 96);
    memcpy((uint8_t *)&t1 + 96, ((uint8_t *)&F.l1), 96);
    gm_tq7s98ihonkju t2 = gm_f_new_fnepb5f((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktdkih9htpv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcod98slk3vnra, 564, 89);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    memset(&(*(gm_teti411eujvf4 *)gm_out), 0, sizeof (*(gm_teti411eujvf4 *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 0, ((uint8_t)3ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 8, t3);
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t1))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t2 = GM_LD(void *, (((uint8_t *)&t1) + 8));
    memset(&(*(gm_teti411eujvf4 *)gm_out), 0, sizeof (*(gm_teti411eujvf4 *)gm_out));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 0, ((uint8_t)2ULL));
    GM_ST(void *, (uint8_t *)&(*(gm_teti411eujvf4 *)gm_out) + 8, t2);
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
extern __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_symbol_9ijp0eo F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 289u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(1));
    t1 = t21;
    goto me2;
mn19:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t23 != ((uint8_t)1ULL)) goto mn22;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(1));
    t1 = t24;
    goto me2;
mn22:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s6s96daqmhcs9));
    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
    t1 = t27;
    goto me2;
mn25:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t29 != ((uint8_t)3ULL)) goto mn28;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_si62ejtp0vbct));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(2));
    t1 = t30;
    goto me2;
mn28:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)4ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
    t1 = t33;
    goto me2;
mn31:;
ma8:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t35 != ((uint8_t)5ULL)) goto mn34;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_siamquc19uikr));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
    t1 = t36;
    goto me2;
mn34:;
ma9:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)6ULL)) goto mn37;
    gm_b16_8 t39;
    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9hi985r6f61m));
    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
    t1 = t39;
    goto me2;
mn37:;
ma10:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t41 != ((uint8_t)7ULL)) goto mn40;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_smerqm8g793l6));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
    t1 = t42;
    goto me2;
mn40:;
ma11:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t44 != ((uint8_t)8ULL)) goto mn43;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2u5posfkbgnq));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
    t1 = t45;
    goto me2;
mn43:;
ma12:;
    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t47 != ((uint8_t)9ULL)) goto mn46;
    gm_b16_8 t48;
    GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sa831sfid0hf3));
    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
    t1 = t48;
    goto me2;
mn46:;
ma13:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1f440st4mnkp));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
    t1 = t51;
    goto me2;
mn49:;
ma14:;
    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t53 != ((uint8_t)11ULL)) goto mn52;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s4g3cajt15dmi));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(2));
    t1 = t54;
    goto me2;
mn52:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)12ULL)) goto mn55;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sm5be4659ulci));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
    t1 = t57;
    goto me2;
mn55:;
ma16:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t59 != ((uint8_t)13ULL)) goto mn58;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s02ru83e3fffc));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
    t1 = t60;
    goto me2;
mn58:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 342, 48);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_block_children_r2qinti(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_block_children_r2qinti F;
    void * gm_l0; /* b */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    void * gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    gm_b16_8 gm_l9; /* ps */
    void * gm_l10; /* v */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l13; /* p */
    void * gm_l14; /* x */
    void * gm_l15; /* x */
    void * gm_l16; /* x */
    void * gm_l17; /* t */
    GM_STACK_CHECK(&F, 1042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
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
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t26) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; default: goto ma25; }
ma19:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)0ULL)) goto mn27;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn27;
                        goto me18;
mn27:;
ma20:;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t31 != ((uint8_t)0ULL)) goto mn30;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn30;
                        void * t32 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l5 = t32;
                        gm_closure t33 = gm_l1;
                        void * t34 = gm_l5;
                        gm_closure t35 = t33;
                        gm_node *ccx36 = t35.cx;
                        void * t37 = t35.code;
                        gm_b0_1 t38;
                        if (GM_PTR_TAGGED(t37)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t37))(t34);
                        } else {
                            t38 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t37)(t35.env, ccx36, NULL, t34);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn30:;
ma21:;
                        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t40 != ((uint8_t)1ULL)) goto mn39;
                        void * t41 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l6 = t41;
                        gm_closure t42 = gm_l1;
                        void * t43 = gm_l6;
                        gm_closure t44 = t42;
                        gm_node *ccx45 = t44.cx;
                        void * t46 = t44.code;
                        gm_b0_1 t47;
                        if (GM_PTR_TAGGED(t46)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t46))(t43);
                        } else {
                            t47 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t46)(t44.env, ccx45, NULL, t43);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn39:;
ma22:;
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t49 != ((uint8_t)2ULL)) goto mn48;
                        void * t50 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l7 = t50;
                        void * t51 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l8 = t51;
                        {
                            gm_closure t53 = gm_l1;
                            void * t54 = gm_l7;
                            gm_closure t55 = t53;
                            gm_node *ccx56 = t55.cx;
                            void * t57 = t55.code;
                            gm_b0_1 t58;
                            if (GM_PTR_TAGGED(t57)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t57))(t54);
                            } else {
                                t58 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t57)(t55.env, ccx56, NULL, t54);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            gm_closure t59 = gm_l1;
                            void * t60 = gm_l8;
                            gm_closure t61 = t59;
                            gm_node *ccx62 = t61.cx;
                            void * t63 = t61.code;
                            gm_b0_1 t64;
                            if (GM_PTR_TAGGED(t63)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t63))(t60);
                            } else {
                                t64 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t63)(t61.env, ccx62, NULL, t60);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn48:;
ma23:;
                        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t66 != ((uint8_t)3ULL)) goto mn65;
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l9 = t67;
                        void * t68 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l10 = t68;
                        {
                            {
                                gm_b16_8 t71 = gm_l9;
                                gm_l11 = t71;
                            }
                            {
                                gm_l12 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t73 = gm_l12;
                                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                    uint8_t t75 = (t73 < t74);
                                    if (!t75) goto lb72;
                                    {
                                        uint64_t t78 = gm_l12;
                                        uint64_t t79 = (uintptr_t)t78;
                                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                        void * t81 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t79 * 8));
                                        void * t84 = GM_LD(void *, ((uint8_t *)&t81));
                                        gm_l13 = t84;
                                        {
                                            void * t87 = gm_l13;
                                            if (!(GM_LD(void *, ((uint8_t *)&t87)) != 0)) goto ie86;
                                            void * t88 = GM_LD(void *, ((uint8_t *)&t87));
                                            gm_l14 = t88;
                                            {
                                                gm_closure t90 = gm_l1;
                                                void * t91 = gm_l14;
                                                gm_closure t92 = t90;
                                                gm_node *ccx93 = t92.cx;
                                                void * t94 = t92.code;
                                                gm_b0_1 t95;
                                                if (GM_PTR_TAGGED(t94)) {
                                                    ((void (*)(void *))GM_PTR_UNTAG(t94))(t91);
                                                } else {
                                                    t95 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t94)(t92.env, ccx93, NULL, t91);
                                                }
                                                if (gm_unw.kind) {
                                                    goto gm_ll76;
                                                }
                                            }
                                            goto ix86;
ie86:;
ix86:;
                                        }
                                        goto me82;
mn83:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 675, 17);
me82:;
                                    }
lc72:;
                                    {
                                        uint64_t t97 = gm_l12;
                                        uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                                        uint64_t t100 = r98;
                                        gm_l12 = t100;
                                    }
                                }
gm_ll76:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb72; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc72; }
                                }
                                goto gm_ll9;
lb72:;
                            }
                        }
                        {
                            gm_closure t101 = gm_l1;
                            void * t102 = gm_l10;
                            gm_closure t103 = t101;
                            gm_node *ccx104 = t103.cx;
                            void * t105 = t103.code;
                            gm_b0_1 t106;
                            if (GM_PTR_TAGGED(t105)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t105))(t102);
                            } else {
                                t106 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t105)(t103.env, ccx104, NULL, t102);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        goto me18;
mn65:;
ma24:;
                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t110 != ((uint8_t)4ULL)) goto pa109;
                        void * t111 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l15 = t111;
                        goto po108;
pa109:;
                        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t113 != ((uint8_t)5ULL)) goto pa112;
                        void * t114 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l15 = t114;
                        goto po108;
pa112:;
                        goto mn107;
po108:;
                        gm_closure t115 = gm_l1;
                        void * t116 = gm_l15;
                        gm_closure t117 = t115;
                        gm_node *ccx118 = t117.cx;
                        void * t119 = t117.code;
                        gm_b0_1 t120;
                        if (GM_PTR_TAGGED(t119)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t119))(t116);
                        } else {
                            t120 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t119)(t117.env, ccx118, NULL, t116);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        goto me18;
mn107:;
ma25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 666, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 665, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t122 = gm_l3;
                    uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                    uint64_t t125 = r123;
                    gm_l3 = t125;
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
        void * t127 = gm_l0;
        void * t128 = GM_LD(void *, (((uint8_t *)t127) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t128)) != 0)) goto ie126;
        void * t129 = GM_LD(void *, ((uint8_t *)&t128));
        gm_l17 = t129;
        {
            gm_closure t131 = gm_l1;
            void * t132 = gm_l17;
            gm_closure t133 = t131;
            gm_node *ccx134 = t133.cx;
            void * t135 = t133.code;
            gm_b0_1 t136;
            if (GM_PTR_TAGGED(t135)) {
                ((void (*)(void *))GM_PTR_UNTAG(t135))(t132);
            } else {
                t136 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t135)(t133.env, ccx134, NULL, t132);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix126;
ie126:;
ix126:;
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
inline __attribute__((aligned(4))) void gm_f_conds_children_3kg6846(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conds_children_3kg6846 F;
    gm_b16_8 gm_l0; /* cond */
    gm_closure gm_l1; /* f */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 395u);
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l5 = t20;
                        gm_closure t21 = gm_l1;
                        void * t22 = gm_l5;
                        gm_closure t23 = t21;
                        gm_node *ccx24 = t23.cx;
                        void * t25 = t23.code;
                        gm_b0_1 t26;
                        if (GM_PTR_TAGGED(t25)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t25))(t22);
                        } else {
                            t26 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t25)(t23.env, ccx24, NULL, t22);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn18:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t28 != ((uint8_t)1ULL)) goto mn27;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t29;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t30;
                        {
                            void * t32 = gm_l6;
                            gm_closure t33 = gm_l1;
                            gm_f_pat_children_e3peovr(t32, t33);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            gm_closure t34 = gm_l1;
                            void * t35 = gm_l7;
                            gm_closure t36 = t34;
                            gm_node *ccx37 = t36.cx;
                            void * t38 = t36.code;
                            gm_b0_1 t39;
                            if (GM_PTR_TAGGED(t38)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t38))(t35);
                            } else {
                                t39 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t38)(t36.env, ccx37, NULL, t35);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 688, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 687, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t41 = gm_l3;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l3 = t44;
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
extern __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) gm_t7g88i52rrtm5 gm_f_default_spec_9ntjq2s_c(void) { gm_t7g88i52rrtm5 gm_r; gm_f_default_spec_9ntjq2s(&gm_r); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hdas5h7(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_child_fm49s73 F;
    void * gm_l0; /* e */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* a */
    void * gm_l3; /* a */
    void * gm_l4; /* a */
    void * gm_l5; /* a */
    void * gm_l6; /* a */
    void * gm_l7; /* a */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    gm_tao63pk3r18v5 gm_l10; /* sl */
    void * gm_l11; /* x */
    void * gm_l12; /* x */
    void * gm_l13; /* a */
    void * gm_l14; /* a */
    void * gm_l15; /* a */
    gm_t3dcoc7077t1q gm_l16; /* x */
    gm_t083kd7b1kkag gm_l17; /* x */
    void * gm_l18; /* a */
    void * gm_l19; /* a */
    void * gm_l20; /* k */
    void * gm_l21; /* v */
    void * gm_l22; /* k */
    void * gm_l23; /* a */
    void * gm_l24; /* b */
    void * gm_l25; /* a */
    void * gm_l26; /* b */
    void * gm_l27; /* a */
    void * gm_l28; /* b */
    void * gm_l29; /* a */
    void * gm_l30; /* b */
    void * gm_l31; /* a */
    void * gm_l32; /* b */
    void * gm_l33; /* s */
    gm_b16_8 gm_l34; /* arms */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l37; /* a */
    void * gm_l38; /* g */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t4lnf71mruqpt gm_l42; /* h */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    void * gm_l45; /* x */
    gm_b16_8 gm_l46; /* args */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint64_t gm_l49; /* i */
    gm_tandvbt63uk7n gm_l50; /* x */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* i */
    gm_b16_8 gm_l55; /* args */
    void * gm_l56; /* c */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* i */
    gm_b16_8 gm_l60; /* xs */
    gm_b16_8 gm_l61; /* xs */
    uint64_t gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* i */
    gm_ttggq7ng4kp3p gm_l65; /* v */
    void * gm_l66; /* p */
    void * gm_l67; /* b */
    gm_trl2khmuphncd gm_l68; /* x */
    void * gm_l69; /* e2 */
    void * gm_l71; /* st */
    gm_teo9d4mncjfdu gm_l72; /* x */
    void * gm_l73; /* v */
    void * gm_l74; /* w */
    void * gm_l75; /* x */
    uint64_t gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* i */
    void * gm_l79; /* w */
    void * gm_l80; /* x */
    void * gm_l81; /* a */
    GM_STACK_CHECK(&F, 5298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t33) { case ((uint8_t)0ULL): goto ma31; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma31; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma31; case ((uint8_t)6ULL): goto ma31; case ((uint8_t)7ULL): goto ma31; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma31; case ((uint8_t)10ULL): goto ma31; case ((uint8_t)11ULL): goto ma31; case ((uint8_t)12ULL): goto ma31; case ((uint8_t)13ULL): goto ma31; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma31; case ((uint8_t)16ULL): goto ma31; case ((uint8_t)17ULL): goto ma31; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma4; case ((uint8_t)23ULL): goto ma4; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma4; case ((uint8_t)26ULL): goto ma31; case ((uint8_t)27ULL): goto ma31; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma16; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma22; case ((uint8_t)35ULL): goto ma23; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma4; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma4; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma17; case ((uint8_t)44ULL): goto ma18; case ((uint8_t)45ULL): goto ma17; case ((uint8_t)46ULL): goto ma25; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma27; case ((uint8_t)49ULL): goto ma28; case ((uint8_t)50ULL): goto ma31; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma4; case ((uint8_t)53ULL): goto ma13; case ((uint8_t)54ULL): goto ma14; case ((uint8_t)55ULL): goto ma29; case ((uint8_t)56ULL): goto ma30; case ((uint8_t)57ULL): goto ma21; case ((uint8_t)58ULL): goto ma19; default: goto ma31; }
ma4:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)18ULL)) goto pa36;
        void * t38 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t38;
        goto po35;
pa36:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)24ULL)) goto pa39;
        void * t41 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t41;
        goto po35;
pa39:;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t43 != ((uint8_t)25ULL)) goto pa42;
        void * t44 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t44;
        goto po35;
pa42:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)39ULL)) goto pa45;
        void * t47 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t47;
        goto po35;
pa45:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)37ULL)) goto pa48;
        void * t50 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t50;
        goto po35;
pa48:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)52ULL)) goto pa51;
        void * t53 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t53;
        goto po35;
pa51:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t55 != ((uint8_t)22ULL)) goto pa54;
        void * t56 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t56;
        goto po35;
pa54:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)23ULL)) goto pa57;
        void * t59 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t59;
        goto po35;
pa57:;
        goto mn34;
po35:;
        gm_closure t60 = gm_l1;
        void * t61 = gm_l2;
        gm_closure t62 = t60;
        gm_node *ccx63 = t62.cx;
        void * t64 = t62.code;
        gm_b0_1 t65;
        if (GM_PTR_TAGGED(t64)) {
            ((void (*)(void *))GM_PTR_UNTAG(t64))(t61);
        } else {
            t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t64)(t62.env, ccx63, NULL, t61);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn34:;
ma5:;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t67 != ((uint8_t)21ULL)) goto mn66;
        gm_tao63pk3r18v5 t68 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t68;
        {
            gm_closure t70 = gm_l1;
            void * t71 = GM_LD(void *, (((uint8_t *)&gm_l10) + 0));
            gm_closure t72 = t70;
            gm_node *ccx73 = t72.cx;
            void * t74 = t72.code;
            gm_b0_1 t75;
            if (GM_PTR_TAGGED(t74)) {
                ((void (*)(void *))GM_PTR_UNTAG(t74))(t71);
            } else {
                t75 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t74)(t72.env, ccx73, NULL, t71);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t77 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t77)) != 0)) goto ie76;
            void * t78 = GM_LD(void *, ((uint8_t *)&t77));
            gm_l11 = t78;
            {
                gm_closure t80 = gm_l1;
                void * t81 = gm_l11;
                gm_closure t82 = t80;
                gm_node *ccx83 = t82.cx;
                void * t84 = t82.code;
                gm_b0_1 t85;
                if (GM_PTR_TAGGED(t84)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t84))(t81);
                } else {
                    t85 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t84)(t82.env, ccx83, NULL, t81);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix76;
ie76:;
ix76:;
        }
        {
            void * t87 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t87)) != 0)) goto ie86;
            void * t88 = GM_LD(void *, ((uint8_t *)&t87));
            gm_l12 = t88;
            {
                gm_closure t90 = gm_l1;
                void * t91 = gm_l12;
                gm_closure t92 = t90;
                gm_node *ccx93 = t92.cx;
                void * t94 = t92.code;
                gm_b0_1 t95;
                if (GM_PTR_TAGGED(t94)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t94))(t91);
                } else {
                    t95 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t94)(t92.env, ccx93, NULL, t91);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix86;
ie86:;
ix86:;
        }
        goto me3;
mn66:;
ma6:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t97 != ((uint8_t)29ULL)) goto mn96;
        void * t98 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t98;
        gm_closure t99 = gm_l1;
        void * t100 = gm_l13;
        gm_closure t101 = t99;
        gm_node *ccx102 = t101.cx;
        void * t103 = t101.code;
        gm_b0_1 t104;
        if (GM_PTR_TAGGED(t103)) {
            ((void (*)(void *))GM_PTR_UNTAG(t103))(t100);
        } else {
            t104 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t103)(t101.env, ccx102, NULL, t100);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn96:;
ma7:;
        uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t106 != ((uint8_t)32ULL)) goto mn105;
        void * t107 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l14 = t107;
        gm_closure t108 = gm_l1;
        void * t109 = gm_l14;
        gm_closure t110 = t108;
        gm_node *ccx111 = t110.cx;
        void * t112 = t110.code;
        gm_b0_1 t113;
        if (GM_PTR_TAGGED(t112)) {
            ((void (*)(void *))GM_PTR_UNTAG(t112))(t109);
        } else {
            t113 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t112)(t110.env, ccx111, NULL, t109);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn105:;
ma8:;
        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t115 != ((uint8_t)40ULL)) goto mn114;
        void * t116 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l15 = t116;
        gm_closure t117 = gm_l1;
        void * t118 = gm_l15;
        gm_closure t119 = t117;
        gm_node *ccx120 = t119.cx;
        void * t121 = t119.code;
        gm_b0_1 t122;
        if (GM_PTR_TAGGED(t121)) {
            ((void (*)(void *))GM_PTR_UNTAG(t121))(t118);
        } else {
            t122 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t121)(t119.env, ccx120, NULL, t118);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn114:;
ma9:;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t124 != ((uint8_t)41ULL)) goto mn123;
        gm_t3dcoc7077t1q t125 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t2) + 8) + 8));
        gm_l16 = t125;
        {
            gm_closure t127 = gm_l1;
            void * t128 = GM_LD(void *, (((uint8_t *)&gm_l16) + 0));
            gm_closure t129 = t127;
            gm_node *ccx130 = t129.cx;
            void * t131 = t129.code;
            gm_b0_1 t132;
            if (GM_PTR_TAGGED(t131)) {
                ((void (*)(void *))GM_PTR_UNTAG(t131))(t128);
            } else {
                t132 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t131)(t129.env, ccx130, NULL, t128);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t133 = gm_l1;
            void * t134 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
            gm_closure t135 = t133;
            gm_node *ccx136 = t135.cx;
            void * t137 = t135.code;
            gm_b0_1 t138;
            if (GM_PTR_TAGGED(t137)) {
                ((void (*)(void *))GM_PTR_UNTAG(t137))(t134);
            } else {
                t138 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t137)(t135.env, ccx136, NULL, t134);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn123:;
ma10:;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t140 != ((uint8_t)42ULL)) goto mn139;
        gm_t083kd7b1kkag t141 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t2) + 8) + 8));
        gm_l17 = t141;
        gm_closure t142 = gm_l1;
        void * t143 = GM_LD(void *, (((uint8_t *)&gm_l17) + 0));
        gm_closure t144 = t142;
        gm_node *ccx145 = t144.cx;
        void * t146 = t144.code;
        gm_b0_1 t147;
        if (GM_PTR_TAGGED(t146)) {
            ((void (*)(void *))GM_PTR_UNTAG(t146))(t143);
        } else {
            t147 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t146)(t144.env, ccx145, NULL, t143);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn139:;
ma11:;
        uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t149 != ((uint8_t)19ULL)) goto mn148;
        void * t150 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l18 = t150;
        gm_closure t151 = gm_l1;
        void * t152 = gm_l18;
        gm_closure t153 = t151;
        gm_node *ccx154 = t153.cx;
        void * t155 = t153.code;
        gm_b0_1 t156;
        if (GM_PTR_TAGGED(t155)) {
            ((void (*)(void *))GM_PTR_UNTAG(t155))(t152);
        } else {
            t156 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t155)(t153.env, ccx154, NULL, t152);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn148:;
ma12:;
        uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t158 != ((uint8_t)51ULL)) goto mn157;
        void * t159 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l19 = t159;
        gm_closure t160 = gm_l1;
        void * t161 = gm_l19;
        gm_closure t162 = t160;
        gm_node *ccx163 = t162.cx;
        void * t164 = t162.code;
        gm_b0_1 t165;
        if (GM_PTR_TAGGED(t164)) {
            ((void (*)(void *))GM_PTR_UNTAG(t164))(t161);
        } else {
            t165 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t164)(t162.env, ccx163, NULL, t161);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn157:;
ma13:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t167 != ((uint8_t)53ULL)) goto mn166;
        void * t168 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l20 = t168;
        void * t169 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l21 = t169;
        {
            gm_closure t171 = gm_l1;
            void * t172 = gm_l20;
            gm_closure t173 = t171;
            gm_node *ccx174 = t173.cx;
            void * t175 = t173.code;
            gm_b0_1 t176;
            if (GM_PTR_TAGGED(t175)) {
                ((void (*)(void *))GM_PTR_UNTAG(t175))(t172);
            } else {
                t176 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t175)(t173.env, ccx174, NULL, t172);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t177 = gm_l1;
            void * t178 = gm_l21;
            gm_closure t179 = t177;
            gm_node *ccx180 = t179.cx;
            void * t181 = t179.code;
            gm_b0_1 t182;
            if (GM_PTR_TAGGED(t181)) {
                ((void (*)(void *))GM_PTR_UNTAG(t181))(t178);
            } else {
                t182 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t181)(t179.env, ccx180, NULL, t178);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn166:;
ma14:;
        uint8_t t184 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t184 != ((uint8_t)54ULL)) goto mn183;
        void * t185 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l22 = t185;
        gm_closure t186 = gm_l1;
        void * t187 = gm_l22;
        gm_closure t188 = t186;
        gm_node *ccx189 = t188.cx;
        void * t190 = t188.code;
        gm_b0_1 t191;
        if (GM_PTR_TAGGED(t190)) {
            ((void (*)(void *))GM_PTR_UNTAG(t190))(t187);
        } else {
            t191 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t190)(t188.env, ccx189, NULL, t187);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn183:;
ma15:;
        uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t193 != ((uint8_t)20ULL)) goto mn192;
        void * t194 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l23 = t194;
        void * t195 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l24 = t195;
        {
            gm_closure t197 = gm_l1;
            void * t198 = gm_l23;
            gm_closure t199 = t197;
            gm_node *ccx200 = t199.cx;
            void * t201 = t199.code;
            gm_b0_1 t202;
            if (GM_PTR_TAGGED(t201)) {
                ((void (*)(void *))GM_PTR_UNTAG(t201))(t198);
            } else {
                t202 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t201)(t199.env, ccx200, NULL, t198);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t203 = gm_l1;
            void * t204 = gm_l24;
            gm_closure t205 = t203;
            gm_node *ccx206 = t205.cx;
            void * t207 = t205.code;
            gm_b0_1 t208;
            if (GM_PTR_TAGGED(t207)) {
                ((void (*)(void *))GM_PTR_UNTAG(t207))(t204);
            } else {
                t208 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t207)(t205.env, ccx206, NULL, t204);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn192:;
ma16:;
        uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t212 != ((uint8_t)30ULL)) goto pa211;
        void * t213 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l25 = t213;
        void * t214 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l26 = t214;
        goto po210;
pa211:;
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t216 != ((uint8_t)31ULL)) goto pa215;
        void * t217 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l25 = t217;
        void * t218 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l26 = t218;
        goto po210;
pa215:;
        goto mn209;
po210:;
        {
            gm_closure t220 = gm_l1;
            void * t221 = gm_l25;
            gm_closure t222 = t220;
            gm_node *ccx223 = t222.cx;
            void * t224 = t222.code;
            gm_b0_1 t225;
            if (GM_PTR_TAGGED(t224)) {
                ((void (*)(void *))GM_PTR_UNTAG(t224))(t221);
            } else {
                t225 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t224)(t222.env, ccx223, NULL, t221);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t226 = gm_l1;
            void * t227 = gm_l26;
            gm_closure t228 = t226;
            gm_node *ccx229 = t228.cx;
            void * t230 = t228.code;
            gm_b0_1 t231;
            if (GM_PTR_TAGGED(t230)) {
                ((void (*)(void *))GM_PTR_UNTAG(t230))(t227);
            } else {
                t231 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t230)(t228.env, ccx229, NULL, t227);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn209:;
ma17:;
        uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t235 != ((uint8_t)43ULL)) goto pa234;
        void * t236 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l29 = t236;
        void * t237 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l30 = t237;
        goto po233;
pa234:;
        uint8_t t239 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t239 != ((uint8_t)45ULL)) goto pa238;
        void * t240 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l29 = t240;
        void * t241 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l30 = t241;
        goto po233;
pa238:;
        goto mn232;
po233:;
        {
            gm_closure t243 = gm_l1;
            void * t244 = gm_l29;
            gm_closure t245 = t243;
            gm_node *ccx246 = t245.cx;
            void * t247 = t245.code;
            gm_b0_1 t248;
            if (GM_PTR_TAGGED(t247)) {
                ((void (*)(void *))GM_PTR_UNTAG(t247))(t244);
            } else {
                t248 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t247)(t245.env, ccx246, NULL, t244);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t249 = gm_l1;
            void * t250 = gm_l30;
            gm_closure t251 = t249;
            gm_node *ccx252 = t251.cx;
            void * t253 = t251.code;
            gm_b0_1 t254;
            if (GM_PTR_TAGGED(t253)) {
                ((void (*)(void *))GM_PTR_UNTAG(t253))(t250);
            } else {
                t254 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t253)(t251.env, ccx252, NULL, t250);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn232:;
ma18:;
        uint8_t t256 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t256 != ((uint8_t)44ULL)) goto mn255;
        void * t257 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l33 = t257;
        gm_b16_8 t258 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l34 = t258;
        {
            gm_closure t260 = gm_l1;
            void * t261 = gm_l33;
            gm_closure t262 = t260;
            gm_node *ccx263 = t262.cx;
            void * t264 = t262.code;
            gm_b0_1 t265;
            if (GM_PTR_TAGGED(t264)) {
                ((void (*)(void *))GM_PTR_UNTAG(t264))(t261);
            } else {
                t265 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t264)(t262.env, ccx263, NULL, t261);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t267 = gm_l34;
                gm_l35 = t267;
            }
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t269 = gm_l36;
                    uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                    uint8_t t271 = (t269 < t270);
                    if (!t271) goto lb268;
                    {
                        uint64_t t274 = gm_l36;
                        uint64_t t275 = (uintptr_t)t274;
                        uint64_t t276 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        gm_tbbqefuhv2r8h t279 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t275 * 24));
                        gm_l37 = t279;
                        {
                            void * t281 = GM_LD(void *, (((uint8_t *)&gm_l37) + 0));
                            gm_closure t282 = gm_l1;
                            gm_f_pat_children_e3peovr(t281, t282);
                            if (gm_unw.kind) {
                                goto gm_ll272;
                            }
                        }
                        {
                            void * t284 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
                            if (!(GM_LD(void *, ((uint8_t *)&t284)) != 0)) goto ie283;
                            void * t285 = GM_LD(void *, ((uint8_t *)&t284));
                            gm_l38 = t285;
                            {
                                gm_closure t287 = gm_l1;
                                void * t288 = gm_l38;
                                gm_closure t289 = t287;
                                gm_node *ccx290 = t289.cx;
                                void * t291 = t289.code;
                                gm_b0_1 t292;
                                if (GM_PTR_TAGGED(t291)) {
                                    ((void (*)(void *))GM_PTR_UNTAG(t291))(t288);
                                } else {
                                    t292 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t291)(t289.env, ccx290, NULL, t288);
                                }
                                if (gm_unw.kind) {
                                    goto gm_ll272;
                                }
                            }
                            goto ix283;
ie283:;
ix283:;
                        }
                        {
                            gm_closure t293 = gm_l1;
                            void * t294 = GM_LD(void *, (((uint8_t *)&gm_l37) + 16));
                            gm_closure t295 = t293;
                            gm_node *ccx296 = t295.cx;
                            void * t297 = t295.code;
                            gm_b0_1 t298;
                            if (GM_PTR_TAGGED(t297)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t297))(t294);
                            } else {
                                t298 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t297)(t295.env, ccx296, NULL, t294);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll272;
                            }
                        }
                        goto me277;
mn278:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 605, 13);
me277:;
                    }
lc268:;
                    {
                        uint64_t t300 = gm_l36;
                        uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                        uint64_t t303 = r301;
                        gm_l36 = t303;
                    }
                }
gm_ll272:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb268; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc268; }
                }
                goto gm_lad;
lb268:;
            }
        }
        goto me3;
mn255:;
ma19:;
        uint8_t t305 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t305 != ((uint8_t)58ULL)) goto mn304;
        gm_t710lsrq1m8gk t306 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l39 = t306;
        {
            {
                gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 8));
                gm_l40 = t309;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t311 = gm_l41;
                    uint64_t t312 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t313 = (t311 < t312);
                    if (!t313) goto lb310;
                    {
                        uint64_t t316 = gm_l41;
                        uint64_t t317 = (uintptr_t)t316;
                        uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_t4lnf71mruqpt t321 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t317 * 32));
                        gm_l42 = t321;
                        {
                            uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                            if (t325 != ((uint8_t)0ULL)) goto mn324;
                            void * t326 = GM_LD(void *, (((uint8_t *)&gm_l42) + 8));
                            gm_l43 = t326;
                            gm_closure t327 = gm_l1;
                            void * t328 = gm_l43;
                            gm_closure t329 = t327;
                            gm_node *ccx330 = t329.cx;
                            void * t331 = t329.code;
                            gm_b0_1 t332;
                            if (GM_PTR_TAGGED(t331)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t331))(t328);
                            } else {
                                t332 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t331)(t329.env, ccx330, NULL, t328);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                            goto me323;
mn324:;
                            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                            if (t334 != ((uint8_t)1ULL)) goto mn333;
                            void * t335 = GM_LD(void *, ((((uint8_t *)&gm_l42) + 8) + 16));
                            gm_l44 = t335;
                            gm_closure t336 = gm_l1;
                            void * t337 = gm_l44;
                            gm_closure t338 = t336;
                            gm_node *ccx339 = t338.cx;
                            void * t340 = t338.code;
                            gm_b0_1 t341;
                            if (GM_PTR_TAGGED(t340)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t340))(t337);
                            } else {
                                t341 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t340)(t338.env, ccx339, NULL, t337);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll314;
                            }
                            goto me323;
mn333:;
                            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 613, 17);
me323:;
                        }
                        goto me319;
mn320:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 612, 13);
me319:;
                    }
lc310:;
                    {
                        uint64_t t343 = gm_l41;
                        uint64_t r344; bool o345 = __builtin_add_overflow(t343, ((uint64_t)1ULL), &r344);
                        uint64_t t346 = r344;
                        gm_l41 = t346;
                    }
                }
gm_ll314:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb310; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc310; }
                }
                goto gm_lad;
lb310:;
            }
        }
        {
            void * t348 = GM_LD(void *, (((uint8_t *)&F.l39) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t348)) != 0)) goto ie347;
            void * t349 = GM_LD(void *, ((uint8_t *)&t348));
            gm_l45 = t349;
            {
                gm_closure t351 = gm_l1;
                void * t352 = gm_l45;
                gm_closure t353 = t351;
                gm_node *ccx354 = t353.cx;
                void * t355 = t353.code;
                gm_b0_1 t356;
                if (GM_PTR_TAGGED(t355)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t355))(t352);
                } else {
                    t356 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t355)(t353.env, ccx354, NULL, t352);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix347;
ie347:;
ix347:;
        }
        {
            gm_closure t357 = gm_l1;
            void * t358 = GM_LD(void *, (((uint8_t *)&F.l39) + 32));
            gm_closure t359 = t357;
            gm_node *ccx360 = t359.cx;
            void * t361 = t359.code;
            gm_b0_1 t362;
            if (GM_PTR_TAGGED(t361)) {
                ((void (*)(void *))GM_PTR_UNTAG(t361))(t358);
            } else {
                t362 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t361)(t359.env, ccx360, NULL, t358);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me3;
mn304:;
ma20:;
        uint8_t t364 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t364 != ((uint8_t)33ULL)) goto mn363;
        gm_b16_8 t365 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l46 = t365;
        {
            gm_l48 = ((uint64_t)0ULL);
        }
        {
            uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
            gm_l47 = t367;
        }
        {
            for (;;) {
                uint64_t t369 = gm_l48;
                uint64_t t370 = gm_l47;
                uint8_t t371 = (t369 < t370);
                if (!t371) goto lb368;
                {
                    uint64_t t374 = gm_l48;
                    uint64_t t377 = GM_LD(uint64_t, ((uint8_t *)&t374));
                    gm_l49 = t377;
                    {
                        gm_closure t379 = gm_l1;
                        uint64_t t380 = gm_l49;
                        uint64_t t381 = (uintptr_t)t380;
                        uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                        if (t381 >= (uintptr_t)(t382)) gm_trap_index(t381, t382, gm_srcod98slk3vnra, 621, 56);
                        void * t383 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l46)) + t381 * 88);
                        gm_closure t384 = t379;
                        gm_node *ccx385 = t384.cx;
                        void * t386 = t384.code;
                        gm_b0_1 t387;
                        if (GM_PTR_TAGGED(t386)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t386))(t383);
                        } else {
                            t387 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t386)(t384.env, ccx385, NULL, t383);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll372;
                        }
                    }
                    goto me375;
mn376:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 621, 30);
me375:;
                }
lc368:;
                {
                    uint64_t t389 = gm_l48;
                    uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                    uint64_t t392 = r390;
                    gm_l48 = t392;
                }
            }
gm_ll372:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb368; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc368; }
            }
            goto gm_lad;
lb368:;
        }
        goto me3;
mn363:;
ma21:;
        uint8_t t394 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t394 != ((uint8_t)57ULL)) goto mn393;
        gm_tandvbt63uk7n t395 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t2) + 8) + 8));
        gm_l50 = t395;
        {
            gm_l52 = ((uint64_t)0ULL);
        }
        {
            uint64_t t397 = GM_LD(uintptr_t, (((uint8_t *)&gm_l50) + 8) + 8);
            gm_l51 = t397;
        }
        {
            for (;;) {
                uint64_t t399 = gm_l52;
                uint64_t t400 = gm_l51;
                uint8_t t401 = (t399 < t400);
                if (!t401) goto lb398;
                {
                    uint64_t t404 = gm_l52;
                    uint64_t t407 = GM_LD(uint64_t, ((uint8_t *)&t404));
                    gm_l53 = t407;
                    {
                        gm_closure t409 = gm_l1;
                        uint64_t t410 = gm_l53;
                        uint64_t t411 = (uintptr_t)t410;
                        uint64_t t412 = GM_LD(uintptr_t, (((uint8_t *)&gm_l50) + 8) + 8);
                        if (t411 >= (uintptr_t)(t412)) gm_trap_index(t411, t412, gm_srcod98slk3vnra, 622, 56);
                        void * t413 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l50) + 8)) + t411 * 88);
                        gm_closure t414 = t409;
                        gm_node *ccx415 = t414.cx;
                        void * t416 = t414.code;
                        gm_b0_1 t417;
                        if (GM_PTR_TAGGED(t416)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t416))(t413);
                        } else {
                            t417 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t416)(t414.env, ccx415, NULL, t413);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll402;
                        }
                    }
                    goto me405;
mn406:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 622, 28);
me405:;
                }
lc398:;
                {
                    uint64_t t419 = gm_l52;
                    uint64_t r420; bool o421 = __builtin_add_overflow(t419, ((uint64_t)1ULL), &r420);
                    uint64_t t422 = r420;
                    gm_l52 = t422;
                }
            }
gm_ll402:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb398; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc398; }
            }
            goto gm_lad;
lb398:;
        }
        goto me3;
mn393:;
ma22:;
        uint8_t t424 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t424 != ((uint8_t)34ULL)) goto mn423;
        gm_tsth9qu1dv0g3 t425 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l54 = t425;
        gm_b16_8 t426 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 40));
        gm_l55 = t426;
        {
            uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&F.l54) + 0));
            if (t429 != ((uint8_t)2ULL)) goto ie428;
            void * t430 = GM_LD(void *, (((uint8_t *)&F.l54) + 8));
            gm_l56 = t430;
            {
                gm_closure t432 = gm_l1;
                void * t433 = gm_l56;
                gm_closure t434 = t432;
                gm_node *ccx435 = t434.cx;
                void * t436 = t434.code;
                gm_b0_1 t437;
                if (GM_PTR_TAGGED(t436)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t436))(t433);
                } else {
                    t437 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t436)(t434.env, ccx435, NULL, t433);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix428;
ie428:;
ix428:;
        }
        {
            {
                gm_l58 = ((uint64_t)0ULL);
            }
            {
                uint64_t t439 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                gm_l57 = t439;
            }
            {
                for (;;) {
                    uint64_t t441 = gm_l58;
                    uint64_t t442 = gm_l57;
                    uint8_t t443 = (t441 < t442);
                    if (!t443) goto lb440;
                    {
                        uint64_t t446 = gm_l58;
                        uint64_t t449 = GM_LD(uint64_t, ((uint8_t *)&t446));
                        gm_l59 = t449;
                        {
                            gm_closure t451 = gm_l1;
                            uint64_t t452 = gm_l59;
                            uint64_t t453 = (uintptr_t)t452;
                            uint64_t t454 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                            if (t453 >= (uintptr_t)(t454)) gm_trap_index(t453, t454, gm_srcod98slk3vnra, 625, 39);
                            void * t455 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t453 * 88);
                            gm_closure t456 = t451;
                            gm_node *ccx457 = t456.cx;
                            void * t458 = t456.code;
                            gm_b0_1 t459;
                            if (GM_PTR_TAGGED(t458)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t458))(t455);
                            } else {
                                t459 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t458)(t456.env, ccx457, NULL, t455);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll444;
                            }
                        }
                        goto me447;
mn448:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 625, 13);
me447:;
                    }
lc440:;
                    {
                        uint64_t t461 = gm_l58;
                        uint64_t r462; bool o463 = __builtin_add_overflow(t461, ((uint64_t)1ULL), &r462);
                        uint64_t t464 = r462;
                        gm_l58 = t464;
                    }
                }
gm_ll444:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb440; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc440; }
                }
                goto gm_lad;
lb440:;
            }
        }
        goto me3;
mn423:;
ma23:;
        uint8_t t468 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t468 != ((uint8_t)35ULL)) goto pa467;
        gm_b16_8 t469 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t469;
        goto po466;
pa467:;
        uint8_t t471 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t471 != ((uint8_t)36ULL)) goto pa470;
        gm_b16_8 t472 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t472;
        goto po466;
pa470:;
        goto mn465;
po466:;
        {
            gm_l63 = ((uint64_t)0ULL);
        }
        {
            uint64_t t474 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
            gm_l62 = t474;
        }
        {
            for (;;) {
                uint64_t t476 = gm_l63;
                uint64_t t477 = gm_l62;
                uint8_t t478 = (t476 < t477);
                if (!t478) goto lb475;
                {
                    uint64_t t481 = gm_l63;
                    uint64_t t484 = GM_LD(uint64_t, ((uint8_t *)&t481));
                    gm_l64 = t484;
                    {
                        gm_closure t486 = gm_l1;
                        uint64_t t487 = gm_l64;
                        uint64_t t488 = (uintptr_t)t487;
                        uint64_t t489 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                        if (t488 >= (uintptr_t)(t489)) gm_trap_index(t488, t489, gm_srcod98slk3vnra, 627, 63);
                        void * t490 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t488 * 88);
                        gm_closure t491 = t486;
                        gm_node *ccx492 = t491.cx;
                        void * t493 = t491.code;
                        gm_b0_1 t494;
                        if (GM_PTR_TAGGED(t493)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t493))(t490);
                        } else {
                            t494 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t493)(t491.env, ccx492, NULL, t490);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll479;
                        }
                    }
                    goto me482;
mn483:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 627, 39);
me482:;
                }
lc475:;
                {
                    uint64_t t496 = gm_l63;
                    uint64_t r497; bool o498 = __builtin_add_overflow(t496, ((uint64_t)1ULL), &r497);
                    uint64_t t499 = r497;
                    gm_l63 = t499;
                }
            }
gm_ll479:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb475; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc475; }
            }
            goto gm_lad;
lb475:;
        }
        goto me3;
mn465:;
ma24:;
        uint8_t t501 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t501 != ((uint8_t)38ULL)) goto mn500;
        gm_ttggq7ng4kp3p t502 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t2) + 8) + 8));
        gm_l65 = t502;
        void * t504 = GM_LD(void *, (((uint8_t *)&gm_l65) + 8));
        if (!(GM_LD(void *, ((uint8_t *)&t504)) != 0)) goto ie503;
        void * t505 = GM_LD(void *, ((uint8_t *)&t504));
        gm_l66 = t505;
        {
            gm_closure t507 = gm_l1;
            void * t508 = gm_l66;
            gm_closure t509 = t507;
            gm_node *ccx510 = t509.cx;
            void * t511 = t509.code;
            gm_b0_1 t512;
            if (GM_PTR_TAGGED(t511)) {
                ((void (*)(void *))GM_PTR_UNTAG(t511))(t508);
            } else {
                t512 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t511)(t509.env, ccx510, NULL, t508);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix503;
ie503:;
ix503:;
        goto me3;
mn500:;
ma25:;
        uint8_t t514 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t514 != ((uint8_t)46ULL)) goto mn513;
        void * t515 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l67 = t515;
        void * t516 = gm_l67;
        gm_closure t517 = gm_l1;
        gm_f_block_children_r2qinti(t516, t517);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn513:;
ma26:;
        uint8_t t519 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t519 != ((uint8_t)47ULL)) goto mn518;
        gm_trl2khmuphncd t520 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 8) + 8));
        gm_l68 = t520;
        {
            gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 0));
            gm_closure t523 = gm_l1;
            gm_f_conds_children_3kg6846(t522, t523);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_closure t524 = gm_l1;
            void * t525 = GM_LD(void *, (((uint8_t *)&gm_l68) + 16));
            gm_closure t526 = t524;
            gm_node *ccx527 = t526.cx;
            void * t528 = t526.code;
            gm_b0_1 t529;
            if (GM_PTR_TAGGED(t528)) {
                ((void (*)(void *))GM_PTR_UNTAG(t528))(t525);
            } else {
                t529 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t528)(t526.env, ccx527, NULL, t525);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t531 = GM_LD(void *, (((uint8_t *)&gm_l68) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t531)) != 0)) goto ie530;
            void * t532 = GM_LD(void *, ((uint8_t *)&t531));
            gm_l69 = t532;
            {
                gm_closure t534 = gm_l1;
                void * t535 = gm_l69;
                gm_closure t536 = t534;
                gm_node *ccx537 = t536.cx;
                void * t538 = t536.code;
                gm_b0_1 t539;
                if (GM_PTR_TAGGED(t538)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t538))(t535);
                } else {
                    t539 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t538)(t536.env, ccx537, NULL, t535);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix530;
ie530:;
ix530:;
        }
        goto me3;
mn518:;
ma27:;
        uint8_t t541 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t541 != ((uint8_t)48ULL)) goto mn540;
        gm_t710lsrq1m8gk t542 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l70 = t542;
        {
            gm_b16_8 t544 = GM_LD(gm_b16_8, (((uint8_t *)&F.l70) + 8));
            gm_closure t545 = gm_l1;
            gm_f_conds_children_3kg6846(t544, t545);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t546 = GM_LD(void *, (((uint8_t *)&F.l70) + 24));
            gm_closure t547 = gm_l1;
            gm_f_block_children_r2qinti(t546, t547);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t549 = GM_LD(void *, (((uint8_t *)&F.l70) + 32));
            if (!(GM_LD(void *, ((uint8_t *)&t549)) != 0)) goto ie548;
            void * t550 = GM_LD(void *, ((uint8_t *)&t549));
            gm_l71 = t550;
            {
                gm_closure t552 = gm_l1;
                void * t553 = gm_l71;
                gm_closure t554 = t552;
                gm_node *ccx555 = t554.cx;
                void * t556 = t554.code;
                gm_b0_1 t557;
                if (GM_PTR_TAGGED(t556)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t556))(t553);
                } else {
                    t557 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t556)(t554.env, ccx555, NULL, t553);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix548;
ie548:;
ix548:;
        }
        goto me3;
mn540:;
ma28:;
        uint8_t t559 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t559 != ((uint8_t)49ULL)) goto mn558;
        gm_teo9d4mncjfdu t560 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t2) + 8) + 8));
        gm_l72 = t560;
        void * t562 = GM_LD(void *, (((uint8_t *)&gm_l72) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t562)) != 0)) goto ie561;
        void * t563 = GM_LD(void *, ((uint8_t *)&t562));
        gm_l73 = t563;
        {
            gm_closure t565 = gm_l1;
            void * t566 = gm_l73;
            gm_closure t567 = t565;
            gm_node *ccx568 = t567.cx;
            void * t569 = t567.code;
            gm_b0_1 t570;
            if (GM_PTR_TAGGED(t569)) {
                ((void (*)(void *))GM_PTR_UNTAG(t569))(t566);
            } else {
                t570 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t569)(t567.env, ccx568, NULL, t566);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix561;
ie561:;
ix561:;
        goto me3;
mn558:;
ma29:;
        uint8_t t572 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t572 != ((uint8_t)55ULL)) goto mn571;
        void * t573 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l74 = t573;
        {
            void * t576 = gm_l74;
            uint8_t t577 = GM_LD(uint8_t, ((((uint8_t *)t576) + 0) + 0));
            if (t577 != ((uint8_t)1ULL)) goto ie575;
            void * t578 = GM_LD(void *, ((((uint8_t *)t576) + 0) + 8));
            gm_l75 = t578;
            {
                gm_closure t580 = gm_l1;
                void * t581 = gm_l75;
                gm_closure t582 = t580;
                gm_node *ccx583 = t582.cx;
                void * t584 = t582.code;
                gm_b0_1 t585;
                if (GM_PTR_TAGGED(t584)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t584))(t581);
                } else {
                    t585 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t584)(t582.env, ccx583, NULL, t581);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix575;
ie575:;
ix575:;
        }
        {
            {
                gm_l77 = ((uint64_t)0ULL);
            }
            {
                void * t587 = gm_l74;
                uint64_t t588 = GM_LD(uintptr_t, (((uint8_t *)t587) + 72) + 8);
                gm_l76 = t588;
            }
            {
                for (;;) {
                    uint64_t t590 = gm_l77;
                    uint64_t t591 = gm_l76;
                    uint8_t t592 = (t590 < t591);
                    if (!t592) goto lb589;
                    {
                        uint64_t t595 = gm_l77;
                        uint64_t t598 = GM_LD(uint64_t, ((uint8_t *)&t595));
                        gm_l78 = t598;
                        {
                            gm_closure t600 = gm_l1;
                            uint64_t t601 = gm_l78;
                            uint64_t t602 = (uintptr_t)t601;
                            void * t603 = gm_l74;
                            uint64_t t604 = GM_LD(uintptr_t, (((uint8_t *)t603) + 72) + 8);
                            if (t602 >= (uintptr_t)(t604)) gm_trap_index(t602, t604, gm_srcod98slk3vnra, 643, 41);
                            void * t605 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t603) + 72)) + t602 * 88);
                            gm_closure t606 = t600;
                            gm_node *ccx607 = t606.cx;
                            void * t608 = t606.code;
                            gm_b0_1 t609;
                            if (GM_PTR_TAGGED(t608)) {
                                ((void (*)(void *))GM_PTR_UNTAG(t608))(t605);
                            } else {
                                t609 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t608)(t606.env, ccx607, NULL, t605);
                            }
                            if (gm_unw.kind) {
                                goto gm_ll593;
                            }
                        }
                        goto me596;
mn597:;
                        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 643, 13);
me596:;
                    }
lc589:;
                    {
                        uint64_t t611 = gm_l77;
                        uint64_t r612; bool o613 = __builtin_add_overflow(t611, ((uint64_t)1ULL), &r612);
                        uint64_t t614 = r612;
                        gm_l77 = t614;
                    }
                }
gm_ll593:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb589; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc589; }
                }
                goto gm_lad;
lb589:;
            }
        }
        goto me3;
mn571:;
ma30:;
        uint8_t t616 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t616 != ((uint8_t)56ULL)) goto mn615;
        void * t617 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l79 = t617;
        {
            void * t620 = gm_l79;
            uint8_t t621 = GM_LD(uint8_t, ((((uint8_t *)t620) + 0) + 0));
            if (t621 != ((uint8_t)1ULL)) goto ie619;
            void * t622 = GM_LD(void *, ((((uint8_t *)t620) + 0) + 8));
            gm_l80 = t622;
            {
                gm_closure t624 = gm_l1;
                void * t625 = gm_l80;
                gm_closure t626 = t624;
                gm_node *ccx627 = t626.cx;
                void * t628 = t626.code;
                gm_b0_1 t629;
                if (GM_PTR_TAGGED(t628)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t628))(t625);
                } else {
                    t629 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t628)(t626.env, ccx627, NULL, t625);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix619;
ie619:;
ix619:;
        }
        {
            gm_closure t630 = gm_l1;
            void * t631 = gm_l79;
            void * t632 = GM_LD(void *, (((uint8_t *)t631) + 40));
            gm_closure t633 = t630;
            gm_node *ccx634 = t633.cx;
            void * t635 = t633.code;
            gm_b0_1 t636;
            if (GM_PTR_TAGGED(t635)) {
                ((void (*)(void *))GM_PTR_UNTAG(t635))(t632);
            } else {
                t636 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t635)(t633.env, ccx634, NULL, t632);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t638 = gm_l79;
            void * t639 = GM_LD(void *, (((uint8_t *)t638) + 48));
            if (!(GM_LD(void *, ((uint8_t *)&t639)) != 0)) goto ie637;
            void * t640 = GM_LD(void *, ((uint8_t *)&t639));
            gm_l81 = t640;
            {
                gm_closure t642 = gm_l1;
                void * t643 = gm_l81;
                gm_closure t644 = t642;
                gm_node *ccx645 = t644.cx;
                void * t646 = t644.code;
                gm_b0_1 t647;
                if (GM_PTR_TAGGED(t646)) {
                    ((void (*)(void *))GM_PTR_UNTAG(t646))(t643);
                } else {
                    t647 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t646)(t644.env, ccx645, NULL, t643);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix637;
ie637:;
ix637:;
        }
        goto me3;
mn615:;
ma31:;
        goto me3;
mn648:;
ma32:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 569, 5);
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
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_for_each_subpat_arlrtrm F;
    void * gm_l0; /* p */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    void * gm_l5; /* q */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* fs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l10; /* x */
    gm_b16_8 gm_l11; /* ps */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l14; /* q */
    GM_STACK_CHECK(&F, 705u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma7; default: goto ma8; }
ma4:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)5ULL)) goto pa13;
        void * t15 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t15;
        goto po12;
pa13:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)6ULL)) goto pa16;
        void * t18 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t18;
        goto po12;
pa16:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)7ULL)) goto pa19;
        void * t21 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t21;
        goto po12;
pa19:;
        goto mn11;
po12:;
        gm_closure t22 = gm_l1;
        void * t23 = gm_l2;
        gm_closure t24 = t22;
        gm_node *ccx25 = t24.cx;
        void * t26 = t24.code;
        gm_b0_1 t27;
        if (GM_PTR_TAGGED(t26)) {
            ((void (*)(void *))GM_PTR_UNTAG(t26))(t23);
        } else {
            t27 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t26)(t24.env, ccx25, NULL, t23);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)8ULL)) goto mn28;
        void * t30 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t30;
        void * t32 = gm_l5;
        if (!(GM_LD(void *, ((uint8_t *)&t32)) != 0)) goto ie31;
        void * t33 = GM_LD(void *, ((uint8_t *)&t32));
        gm_l6 = t33;
        {
            gm_closure t35 = gm_l1;
            void * t36 = gm_l6;
            gm_closure t37 = t35;
            gm_node *ccx38 = t37.cx;
            void * t39 = t37.code;
            gm_b0_1 t40;
            if (GM_PTR_TAGGED(t39)) {
                ((void (*)(void *))GM_PTR_UNTAG(t39))(t36);
            } else {
                t40 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t39)(t37.env, ccx38, NULL, t36);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix31;
ie31:;
ix31:;
        goto me3;
mn28:;
ma6:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)9ULL)) goto mn41;
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t43;
        {
            gm_b16_8 t45 = gm_l7;
            gm_l8 = t45;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t47 = gm_l9;
                uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t49 = (t47 < t48);
                if (!t49) goto lb46;
                {
                    uint64_t t52 = gm_l9;
                    uint64_t t53 = (uintptr_t)t52;
                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_ttggq7ng4kp3p t57 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t53 * 16));
                    gm_l10 = t57;
                    {
                        gm_closure t59 = gm_l1;
                        void * t60 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                        gm_closure t61 = t59;
                        gm_node *ccx62 = t61.cx;
                        void * t63 = t61.code;
                        gm_b0_1 t64;
                        if (GM_PTR_TAGGED(t63)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t63))(t60);
                        } else {
                            t64 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t63)(t61.env, ccx62, NULL, t60);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll50;
                        }
                    }
                    goto me55;
mn56:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 711, 24);
me55:;
                }
lc46:;
                {
                    uint64_t t66 = gm_l9;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    uint64_t t69 = r67;
                    gm_l9 = t69;
                }
            }
gm_ll50:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
            }
            goto gm_lad;
lb46:;
        }
        goto me3;
mn41:;
ma7:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t71 != ((uint8_t)10ULL)) goto mn70;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t72;
        {
            gm_b16_8 t74 = gm_l11;
            gm_l12 = t74;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t76 = gm_l13;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    uint64_t t81 = gm_l13;
                    uint64_t t82 = (uintptr_t)t81;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    void * t84 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t82 * 8));
                    void * t87 = GM_LD(void *, ((uint8_t *)&t84));
                    gm_l14 = t87;
                    {
                        gm_closure t89 = gm_l1;
                        void * t90 = gm_l14;
                        gm_closure t91 = t89;
                        gm_node *ccx92 = t91.cx;
                        void * t93 = t91.code;
                        gm_b0_1 t94;
                        if (GM_PTR_TAGGED(t93)) {
                            ((void (*)(void *))GM_PTR_UNTAG(t93))(t90);
                        } else {
                            t94 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t93)(t91.env, ccx92, NULL, t90);
                        }
                        if (gm_unw.kind) {
                            goto gm_ll79;
                        }
                    }
                    goto me85;
mn86:;
                    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 712, 20);
me85:;
                }
lc75:;
                {
                    uint64_t t96 = gm_l13;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l13 = t99;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
        goto me3;
mn70:;
ma8:;
        goto me3;
mn100:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 708, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_default_spec_cdd8jng F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        uint8_t t8 = 0;
        void * t9 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 0));
        uint8_t t11 = ((t10 == ((uint8_t)32ULL)));
        if (t11) {
            void * t12 = gm_l0;
            uint8_t t13 = ((GM_LD(uint8_t, (((uint8_t *)t12) + 1) + 0) == ((uint8_t)0ULL)));
            t8 = t13;
        } else t8 = 0;
        if (t8) {
            void * t14 = gm_l0;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 2));
            uint8_t t16 = (uint8_t)!t15;
            t7 = t16;
        } else t7 = 0;
        if (t7) {
            void * t17 = gm_l0;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 3));
            uint8_t t19 = (uint8_t)!t18;
            t6 = t19;
        } else t6 = 0;
        if (t6) {
            void * t20 = gm_l0;
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 4));
            uint8_t t22 = (uint8_t)!t21;
            t5 = t22;
        } else t5 = 0;
        if (t5) {
            void * t23 = gm_l0;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t23) + 8));
            uint8_t t25 = ((t24 == ((uint32_t)0ULL)));
            t4 = t25;
        } else t4 = 0;
        if (t4) {
            void * t26 = gm_l0;
            uint8_t t27;
            if ((((((uint8_t *)t26) + 12))[4] != 0)) goto mn29;
            t27 = ((uint8_t)1);
            goto me28;
mn29:;
            t27 = ((uint8_t)0);
            goto me28;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 527, 112);
me28:;
            t3 = t27;
        } else t3 = 0;
        if (t3) {
            void * t31 = gm_l0;
            uint8_t t32 = ((GM_LD(uint8_t, (((uint8_t *)t31) + 20) + 0) == ((uint8_t)0ULL)));
            t2 = t32;
        } else t2 = 0;
        uint8_t t33 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
extern __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_pat_children_e3peovr(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pat_children_e3peovr F;
    void * gm_l0; /* p */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)2ULL)) goto ie2;
        void * t5 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
        gm_l2 = t5;
        {
            gm_closure t7 = F.l1;
            void * t8 = gm_l2;
            gm_closure t9 = t7;
            gm_node *ccx10 = t9.cx;
            void * t11 = t9.code;
            gm_b0_1 t12;
            if (GM_PTR_TAGGED(t11)) {
                ((void (*)(void *))GM_PTR_UNTAG(t11))(t8);
            } else {
                t12 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t11)(t9.env, ccx10, NULL, t8);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_closure t14 = ((gm_closure){(void *)gm_f_fn_hdas5h7, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t13, t14);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_name_pcdoujk F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 985u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t59) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma25; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma29; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma31; case ((uint8_t)29ULL): goto ma32; case ((uint8_t)30ULL): goto ma33; case ((uint8_t)31ULL): goto ma34; case ((uint8_t)32ULL): goto ma35; case ((uint8_t)33ULL): goto ma36; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma41; case ((uint8_t)39ULL): goto ma42; case ((uint8_t)40ULL): goto ma43; case ((uint8_t)41ULL): goto ma44; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma46; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma48; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma50; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma52; case ((uint8_t)50ULL): goto ma53; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma55; case ((uint8_t)53ULL): goto ma56; case ((uint8_t)54ULL): goto ma57; default: goto ma58; }
ma3:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t61 != ((uint8_t)0ULL)) goto mn60;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s78m4481dj77r));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(8));
    t1 = t62;
    goto me2;
mn60:;
ma4:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t64 != ((uint8_t)1ULL)) goto mn63;
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sv9p44pp2n1jk));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(9));
    t1 = t65;
    goto me2;
mn63:;
ma5:;
    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t67 != ((uint8_t)2ULL)) goto mn66;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s8o3vqolbfl8t));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(10));
    t1 = t68;
    goto me2;
mn66:;
ma6:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t70 != ((uint8_t)3ULL)) goto mn69;
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_skti0sb80pmgp));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(11));
    t1 = t71;
    goto me2;
mn69:;
ma7:;
    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t73 != ((uint8_t)4ULL)) goto mn72;
    gm_b16_8 t74;
    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sb641udihu5hs));
    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(9));
    t1 = t74;
    goto me2;
mn72:;
ma8:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t76 != ((uint8_t)5ULL)) goto mn75;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s6f0l2e1iha9i));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(7));
    t1 = t77;
    goto me2;
mn75:;
ma9:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t79 != ((uint8_t)6ULL)) goto mn78;
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sfku76mf5fn5d));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(8));
    t1 = t80;
    goto me2;
mn78:;
ma10:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t82 != ((uint8_t)7ULL)) goto mn81;
    gm_b16_8 t83;
    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_saamkuuf6jrtf));
    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(7));
    t1 = t83;
    goto me2;
mn81:;
ma11:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t85 != ((uint8_t)8ULL)) goto mn84;
    gm_b16_8 t86;
    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_se3l2nlomk2pj));
    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(8));
    t1 = t86;
    goto me2;
mn84:;
ma12:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t88 != ((uint8_t)9ULL)) goto mn87;
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sv1bu4l4kq188));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(7));
    t1 = t89;
    goto me2;
mn87:;
ma13:;
    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t91 != ((uint8_t)10ULL)) goto mn90;
    gm_b16_8 t92;
    GM_ST(void *, (uint8_t *)&t92, (void *)(gm_smatmsv0oedot));
    GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(7));
    t1 = t92;
    goto me2;
mn90:;
ma14:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t94 != ((uint8_t)11ULL)) goto mn93;
    gm_b16_8 t95;
    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s9819enjtsd7t));
    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(9));
    t1 = t95;
    goto me2;
mn93:;
ma15:;
    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t97 != ((uint8_t)12ULL)) goto mn96;
    gm_b16_8 t98;
    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sal1kdi1rij5t));
    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(9));
    t1 = t98;
    goto me2;
mn96:;
ma16:;
    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t100 != ((uint8_t)13ULL)) goto mn99;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_svkvl31jmj9ne));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(11));
    t1 = t101;
    goto me2;
mn99:;
ma17:;
    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t103 != ((uint8_t)14ULL)) goto mn102;
    gm_b16_8 t104;
    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_siph7o6oug1g2));
    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(13));
    t1 = t104;
    goto me2;
mn102:;
ma18:;
    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t106 != ((uint8_t)15ULL)) goto mn105;
    gm_b16_8 t107;
    GM_ST(void *, (uint8_t *)&t107, (void *)(gm_seajfhgtfmhv7));
    GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(11));
    t1 = t107;
    goto me2;
mn105:;
ma19:;
    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t109 != ((uint8_t)16ULL)) goto mn108;
    gm_b16_8 t110;
    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_slginip33cenb));
    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(13));
    t1 = t110;
    goto me2;
mn108:;
ma20:;
    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t112 != ((uint8_t)17ULL)) goto mn111;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sbgj0biroecr0));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(12));
    t1 = t113;
    goto me2;
mn111:;
ma21:;
    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t115 != ((uint8_t)18ULL)) goto mn114;
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sso3tuh819o23));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(8));
    t1 = t116;
    goto me2;
mn114:;
ma22:;
    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t118 != ((uint8_t)19ULL)) goto mn117;
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_shegcik3mpeut));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(11));
    t1 = t119;
    goto me2;
mn117:;
ma23:;
    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t121 != ((uint8_t)20ULL)) goto mn120;
    gm_b16_8 t122;
    GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sk1d0v1onojc1));
    GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(7));
    t1 = t122;
    goto me2;
mn120:;
ma24:;
    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t124 != ((uint8_t)21ULL)) goto mn123;
    gm_b16_8 t125;
    GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sp8bmcd3m68n6));
    GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(6));
    t1 = t125;
    goto me2;
mn123:;
ma25:;
    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t127 != ((uint8_t)22ULL)) goto mn126;
    gm_b16_8 t128;
    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_smvhngj8mlfjc));
    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(12));
    t1 = t128;
    goto me2;
mn126:;
ma26:;
    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t130 != ((uint8_t)23ULL)) goto mn129;
    gm_b16_8 t131;
    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s2jvv6mjt9vs3));
    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(12));
    t1 = t131;
    goto me2;
mn129:;
ma27:;
    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t133 != ((uint8_t)24ULL)) goto mn132;
    gm_b16_8 t134;
    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sd9o9rh5qe8e7));
    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(10));
    t1 = t134;
    goto me2;
mn132:;
ma28:;
    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t136 != ((uint8_t)25ULL)) goto mn135;
    gm_b16_8 t137;
    GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sfd9hctnph9ef));
    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(11));
    t1 = t137;
    goto me2;
mn135:;
ma29:;
    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t139 != ((uint8_t)26ULL)) goto mn138;
    gm_b16_8 t140;
    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_ssj8r83l87dr2));
    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(15));
    t1 = t140;
    goto me2;
mn138:;
ma30:;
    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t142 != ((uint8_t)27ULL)) goto mn141;
    gm_b16_8 t143;
    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_suno9ilftpfht));
    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(11));
    t1 = t143;
    goto me2;
mn141:;
ma31:;
    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t145 != ((uint8_t)28ULL)) goto mn144;
    gm_b16_8 t146;
    GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s65khs4s753q2));
    GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(10));
    t1 = t146;
    goto me2;
mn144:;
ma32:;
    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t148 != ((uint8_t)29ULL)) goto mn147;
    gm_b16_8 t149;
    GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sj49v74ko3ghf));
    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(14));
    t1 = t149;
    goto me2;
mn147:;
ma33:;
    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t151 != ((uint8_t)30ULL)) goto mn150;
    gm_b16_8 t152;
    GM_ST(void *, (uint8_t *)&t152, (void *)(gm_stl47boisb31o));
    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(10));
    t1 = t152;
    goto me2;
mn150:;
ma34:;
    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t154 != ((uint8_t)31ULL)) goto mn153;
    gm_b16_8 t155;
    GM_ST(void *, (uint8_t *)&t155, (void *)(gm_s86kdga2ul6h4));
    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(8));
    t1 = t155;
    goto me2;
mn153:;
ma35:;
    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t157 != ((uint8_t)32ULL)) goto mn156;
    gm_b16_8 t158;
    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s59a022mrijic));
    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(8));
    t1 = t158;
    goto me2;
mn156:;
ma36:;
    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t160 != ((uint8_t)33ULL)) goto mn159;
    gm_b16_8 t161;
    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sk77bvr488rq3));
    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(9));
    t1 = t161;
    goto me2;
mn159:;
ma37:;
    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t163 != ((uint8_t)34ULL)) goto mn162;
    gm_b16_8 t164;
    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_s4jhjvgae4896));
    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(12));
    t1 = t164;
    goto me2;
mn162:;
ma38:;
    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t166 != ((uint8_t)35ULL)) goto mn165;
    gm_b16_8 t167;
    GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sfbjh3dq66kkg));
    GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(16));
    t1 = t167;
    goto me2;
mn165:;
ma39:;
    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t169 != ((uint8_t)36ULL)) goto mn168;
    gm_b16_8 t170;
    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_skqa2fjpbsodp));
    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(8));
    t1 = t170;
    goto me2;
mn168:;
ma40:;
    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t172 != ((uint8_t)37ULL)) goto mn171;
    gm_b16_8 t173;
    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_s84p47580vnhr));
    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(11));
    t1 = t173;
    goto me2;
mn171:;
ma41:;
    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t175 != ((uint8_t)38ULL)) goto mn174;
    gm_b16_8 t176;
    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sk0bnuggr6sri));
    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(13));
    t1 = t176;
    goto me2;
mn174:;
ma42:;
    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t178 != ((uint8_t)39ULL)) goto mn177;
    gm_b16_8 t179;
    GM_ST(void *, (uint8_t *)&t179, (void *)(gm_siqaapb0mqsrg));
    GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(13));
    t1 = t179;
    goto me2;
mn177:;
ma43:;
    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t181 != ((uint8_t)40ULL)) goto mn180;
    gm_b16_8 t182;
    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sdtlgdl6ed52a));
    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(13));
    t1 = t182;
    goto me2;
mn180:;
ma44:;
    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t184 != ((uint8_t)41ULL)) goto mn183;
    gm_b16_8 t185;
    GM_ST(void *, (uint8_t *)&t185, (void *)(gm_smfprcpea3ibe));
    GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(9));
    t1 = t185;
    goto me2;
mn183:;
ma45:;
    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t187 != ((uint8_t)42ULL)) goto mn186;
    gm_b16_8 t188;
    GM_ST(void *, (uint8_t *)&t188, (void *)(gm_si8jv2q8d22kl));
    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(9));
    t1 = t188;
    goto me2;
mn186:;
ma46:;
    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t190 != ((uint8_t)43ULL)) goto mn189;
    gm_b16_8 t191;
    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_skm4ndn7op5l5));
    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(13));
    t1 = t191;
    goto me2;
mn189:;
ma47:;
    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t193 != ((uint8_t)44ULL)) goto mn192;
    gm_b16_8 t194;
    GM_ST(void *, (uint8_t *)&t194, (void *)(gm_stclkv4qnta30));
    GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(8));
    t1 = t194;
    goto me2;
mn192:;
ma48:;
    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t196 != ((uint8_t)45ULL)) goto mn195;
    gm_b16_8 t197;
    GM_ST(void *, (uint8_t *)&t197, (void *)(gm_s7ri8monh3c4h));
    GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(7));
    t1 = t197;
    goto me2;
mn195:;
ma49:;
    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t199 != ((uint8_t)46ULL)) goto mn198;
    gm_b16_8 t200;
    GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s26ul1d5snk55));
    GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(8));
    t1 = t200;
    goto me2;
mn198:;
ma50:;
    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t202 != ((uint8_t)47ULL)) goto mn201;
    gm_b16_8 t203;
    GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s1pfh89airaa0));
    GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(12));
    t1 = t203;
    goto me2;
mn201:;
ma51:;
    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t205 != ((uint8_t)48ULL)) goto mn204;
    gm_b16_8 t206;
    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_slkubuckp49ih));
    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(11));
    t1 = t206;
    goto me2;
mn204:;
ma52:;
    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t208 != ((uint8_t)49ULL)) goto mn207;
    gm_b16_8 t209;
    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sf5jsgjp29cqn));
    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(13));
    t1 = t209;
    goto me2;
mn207:;
ma53:;
    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t211 != ((uint8_t)50ULL)) goto mn210;
    gm_b16_8 t212;
    GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sik1l49uru5dh));
    GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(12));
    t1 = t212;
    goto me2;
mn210:;
ma54:;
    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t214 != ((uint8_t)51ULL)) goto mn213;
    gm_b16_8 t215;
    GM_ST(void *, (uint8_t *)&t215, (void *)(gm_skk6lm5m17t6v));
    GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(10));
    t1 = t215;
    goto me2;
mn213:;
ma55:;
    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t217 != ((uint8_t)52ULL)) goto mn216;
    gm_b16_8 t218;
    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sed3elknidrs3));
    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(10));
    t1 = t218;
    goto me2;
mn216:;
ma56:;
    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t220 != ((uint8_t)53ULL)) goto mn219;
    gm_b16_8 t221;
    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sfcjpmkuggqo3));
    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(9));
    t1 = t221;
    goto me2;
mn219:;
ma57:;
    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t223 != ((uint8_t)54ULL)) goto mn222;
    gm_b16_8 t224;
    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_su6ipjjrnjs7p));
    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(9));
    t1 = t224;
    goto me2;
mn222:;
ma58:;
    gm_trap("no match arm matched the scrutinee", gm_srcod98slk3vnra, 778, 44);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
