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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
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
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
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
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ta42iqot1auae v0; gm_tghmb53v8qi1e v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t6e9d80inoppc;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t6e9d80inoppc) == 112 && _Alignof(gm_t6e9d80inoppc) == 8 && offsetof(gm_t6e9d80inoppc, f0) == 0 && offsetof(gm_t6e9d80inoppc, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t6e9d80inoppc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfbvnc90lci8r;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
typedef struct { gm_t08olq97rscn3 f0; gm_tfbvnc90lci8r f1; } gm_t2tlhjbsqg5eb;
_Static_assert(sizeof(gm_tfbvnc90lci8r) == 136 && _Alignof(gm_tfbvnc90lci8r) == 8 && offsetof(gm_tfbvnc90lci8r, f0) == 0 && offsetof(gm_tfbvnc90lci8r, f1) == 8 && offsetof(gm_tfbvnc90lci8r, f2) == 120, "layout of `Expr`");
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
_Static_assert(sizeof(gm_t2tlhjbsqg5eb) == 168 && _Alignof(gm_t2tlhjbsqg5eb) == 8 && offsetof(gm_t2tlhjbsqg5eb, f0) == 0 && offsetof(gm_t2tlhjbsqg5eb, f1) == 32, "layout of `(name: Ident, value: Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfbvnc90lci8r v0; gm_t2tlhjbsqg5eb v1; gm_tfbvnc90lci8r v2; } f1; } gm_tq6gu4l68u3rv;
typedef struct { gm_tq6gu4l68u3rv f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tpff59ptr3s92;
_Static_assert(sizeof(gm_tq6gu4l68u3rv) == 176 && _Alignof(gm_tq6gu4l68u3rv) == 8 && offsetof(gm_tq6gu4l68u3rv, f0) == 0 && offsetof(gm_tq6gu4l68u3rv, f1) == 8, "layout of `ArgKind`");
_Static_assert(sizeof(gm_tpff59ptr3s92) == 192 && _Alignof(gm_tpff59ptr3s92) == 8 && offsetof(gm_tpff59ptr3s92, f0) == 0 && offsetof(gm_tpff59ptr3s92, f1) == 176, "layout of `Arg`");
struct gm_fr_soa_args_mnec87i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_soa_bound_61nh2oj {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_coerce_c57qnm7 {
    gm_frame h;
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_soa_column_once_ads8l3i {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* stmts */
    void * ret;
};
struct gm_fr_soa_columns_tuple_ns477md {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* stmts */
    gm_trqfvmjtt4sij l8; /* cols */
    gm_trqfvmjtt4sij l9; /* elems */
    gm_trqfvmjtt4sij l10; /* names */
    gm_t0mglksrkfa5e l14; /* f */
    void * ret;
};
struct gm_fr_soa_construct_8nun9tl {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* stmts */
    gm_trqfvmjtt4sij l9; /* columns */
    gm_t2tlhjbsqg5eb l11; /* a */
    gm_t0mglksrkfa5e l16; /* f */
    gm_trqfvmjtt4sij l18; /* elems */
    gm_tpff59ptr3s92 l21; /* a */
    gm_tfbvnc90lci8r l22; /* e */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l28; /* parts */
    void * ret;
};
struct gm_fr_soa_copy_from_vrsguuh {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    void * ret;
};
struct gm_fr_soa_core_use_n5drmrl {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_deref_uiefo9t {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_element_04p1d8c {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* stmts */
    void * ret;
};
struct gm_fr_soa_element_field_lo71q0l {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_eq_ho8ji9t {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    void * ret;
};
struct gm_fr_soa_field_index_tas8jh7 {
    gm_frame h;
    gm_t0mglksrkfa5e l5; /* f */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_soa_for_ovoqsoj {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    void * ret;
};
struct gm_fr_soa_free_9554eha {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    void * ret;
};
struct gm_fr_soa_gather_7oos4rn {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_index_value_rabl6gs {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_len_m9p2hp1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_make_5imjrl1 {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* stmts */
    void * ret;
};
struct gm_fr_soa_member_vkrr5kf {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* stmts */
    void * ret;
};
struct gm_fr_soa_needs_fields_vtvc2fa {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_of_jufdcj2 {
    gm_frame h;
    gm_t8cc1mtbrhlpt ret;
};
struct gm_fr_soa_operand_eohpslm {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_slice_4dujfr9 {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trqfvmjtt4sij l15; /* args */
    void * ret;
};
struct gm_fr_soa_store_d7ivlu4 {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trcmmd12cefb1 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_field_visible_here_20vnoda(void * a0, gm_t9qkh0rsooc8d a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_format_4hl9u7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_60vq8qs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8g701o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_amt7kei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_blrf705(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_htqqv8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jp1a7ai(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_kgkjlf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pkmkakl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qu7vf4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qvsrn69(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rh16oh6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_te2qqk4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_given_27f1vc1(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hidden_field_message_b0pdek7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_args_mnec87i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_bound_61nh2oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_soa_coerce_c57qnm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_column_once_ads8l3i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_columns_tuple_ns477md(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_from_vrsguuh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_core_use_n5drmrl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_element_04p1d8c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_element_field_lo71q0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, const void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_eq_ho8ji9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, uint8_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_soa_field_index_tas8jh7(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_soa_for_ovoqsoj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) void * gm_f_soa_free_9554eha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_gather_7oos4rn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_soa_index_value_rabl6gs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_soa_len_m9p2hp1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_make_5imjrl1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void * gm_f_soa_member_vkrr5kf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_needs_fields_vtvc2fa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_operand_eohpslm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_slice_4dujfr9(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, uint8_t a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) void gm_f_soa_store_d7ivlu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
extern const uint8_t gm_s06mk2gjnlvf8[73];
extern const uint8_t gm_scnj362636897[25];
extern const uint8_t gm_sf24jo61ba8vn[23];
extern const uint8_t gm_sfflovsm8rlpo[51];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgb0oed9c3voo[5];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_shvoch74oh0kq[59];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_sr5or4g6pm2gc[26];
extern const char gm_srcb4m5qm2daehm[];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssvnvjq0tn1qr[70];
extern const uint8_t gm_st9n2vprlv3c1[22];
extern const uint8_t gm_suh0878eve8t9[10];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_args_mnec87i(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_soa_args_mnec87i F;
    gm_b16_8 gm_l0; /* items */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l4; /* a */
    GM_STACK_CHECK(&F, 214u);
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
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 8));
                    void * t14 = t13;
                    void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        void * t19 = (void *)((uint8_t *)&F.l1);
                        void * t20 = gm_l4;
                        gm_tftb1odc739t5 t21 = gm_f_push_d943vgp_2(t19, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t20)));
                        gm_tftb1odc739t5 t22 = t21;
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcb4m5qm2daehm, 30, 22);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 30, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l3 = t28;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t30 = 0;
        F.ret = t29;
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
inline __attribute__((aligned(4))) void * gm_f_soa_bound_61nh2oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_soa_bound_61nh2oj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* stmts */
    void * gm_l3; /* v */
    void * gm_l4; /* tys */
    GM_STACK_CHECK(&F, 419u);
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
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = (void *)(((uint8_t *)t8) + 8);
        gm_l4 = t9;
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 96);
        void * t13 = gm_l4;
        void * t14 = gm_l3;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
        uint8_t t17 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t12, t13, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie10;
        void * t19 = gm_l4;
        void * t20 = gm_l3;
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 0));
        uint8_t t22 = gm_f_is_error_8k27gd1(t19, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie10;
        {
            void * t25 = gm_l0;
            void * t26 = gm_l3;
            gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 208));
            void * t28 = gm_l0;
            void * t29 = gm_l0;
            void * t30 = (void *)(((uint8_t *)t29) + 96);
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            void * t33 = gm_l3;
            gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
            gm_t88rcucg1b7uk t35 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t30, t32, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t36 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t28, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_tabb710d9mq2e t38;
            gm_f_format_pkmkakl((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcb4m5qm2daehm, 187, 26);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b0_1 t43 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t25, t27, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = gm_l3;
        void * t46 = gm_l2;
        void * t47 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t44, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = 0;
        F.ret = t47;
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
__attribute__((aligned(4))) void * gm_f_soa_coerce_c57qnm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_soa_coerce_c57qnm7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* src */
    gm_t88rcucg1b7uk gm_l3; /* target */
    void * gm_l4; /* tys */
    gm_tjpqvfd72o62u gm_l5; /* to */
    gm_tl874t2atnlh3 gm_l6; /* span */
    uint8_t gm_l7; /* is_const */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    gm_tjpqvfd72o62u gm_l9; /* from */
    void * gm_l10; /* arr */
    void * gm_l11; /* s */
    void * gm_l12; /* c */
    gm_tjpqvfd72o62u gm_l13; /* from */
    void * gm_l15; /* v */
    gm_tn3r4rvlh0rsv gm_l16; /* <temporary array> */
    void * gm_l17; /* c */
    GM_STACK_CHECK(&F, 1731u);
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
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        gm_t88rcucg1b7uk t7 = gm_l3;
        gm_t8cc1mtbrhlpt t8;
        gm_f_soa_info_pq4q43l((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t8cc1mtbrhlpt t9 = t8;
        gm_tjpqvfd72o62u t10;
        if (((((uint8_t *)&t9))[32] != 0)) {
            gm_tjpqvfd72o62u t11 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            uint8_t t12 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l5 = t10;
    }
    {
        uint8_t t14;
        if ((((((uint8_t *)&gm_l5) + 8))[8] != 0)) goto mn16;
        t14 = ((uint8_t)0);
        goto me15;
mn16:;
        t14 = ((uint8_t)1);
        goto me15;
mn17:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 225, 8);
me15:;
        if (!t14) goto ie13;
        {
            uint8_t t19 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t20 = gm_l1;
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 208));
        gm_l6 = t21;
    }
    {
        void * t23 = gm_l4;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_t53at80pfdus3 t25;
        gm_f_kind_jcnt4hm((void *)&t25, t23, GM_CONV(gm_csguankko4lmhd, t24));
        gm_t53at80pfdus3 t26 = t25;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t27 != ((uint8_t)8ULL)) goto ie22;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&t26) + 8) + 0));
        gm_l7 = t28;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t26) + 8) + 4));
        gm_l8 = t29;
        {
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            gm_t88rcucg1b7uk t34 = gm_l8;
            gm_t8cc1mtbrhlpt t35;
            gm_f_soa_info_pq4q43l((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            gm_t8cc1mtbrhlpt t36 = t35;
            if (!((((uint8_t *)&t36))[32] != 0)) goto ie31;
            gm_tjpqvfd72o62u t37 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)&t36));
            gm_l9 = t37;
            {
                uint8_t t40;
                if ((((((uint8_t *)&gm_l9) + 8))[8] != 0)) goto mn42;
                t40 = ((uint8_t)0);
                goto me41;
mn42:;
                t40 = ((uint8_t)1);
                goto me41;
mn43:;
                gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 229, 16);
me41:;
                if (!t40) goto ie39;
                gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 0));
                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 0));
                gm_t88rcucg1b7uk t46 = t44;
                gm_t88rcucg1b7uk t47 = t45;
                uint8_t t48 = (gm_eq9m6mvtmgde(((uint8_t *)&t46), ((uint8_t *)&t47)));
                if (!t48) goto ie39;
                uint8_t t49 = 0;
                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
                if (!t50) {
                    uint8_t t51 = gm_l7;
                    uint8_t t52 = (uint8_t)!t51;
                    t49 = t52;
                } else t49 = 1;
                if (!t49) goto ie39;
                {
                    gm_t88rcucg1b7uk t54 = gm_l8;
                    void * t55 = gm_l1;
                    gm_t5brlu7fal70j t56;
                    memset(&t56, 0, sizeof t56);
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&t56 + 8), t55);
                    gm_tl874t2atnlh3 t57 = gm_l6;
                    void * t58 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t56, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t58;
                }
                {
                    void * t59 = gm_l0;
                    void * t60 = gm_l10;
                    gm_tl874t2atnlh3 t61 = gm_l6;
                    void * t62 = gm_f_soa_slice_4dujfr9((void *)NULL, cx, NULL, t59, t60, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l9)), ((void *)0), ((void *)0), ((uint8_t)0), t61);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t62;
                }
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
                    if (!t64) goto ie63;
                    {
                        void * t67 = gm_l0;
                        void * t68 = gm_l11;
                        void * t69 = gm_l11;
                        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
                        gm_t88rcucg1b7uk t71 = gm_l3;
                        void * t72 = gm_f_soa_coerce_c57qnm7((void *)NULL, cx, NULL, t67, t68, t70, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t73 = t72;
                        if (!(GM_LD(void *, ((uint8_t *)&t73)) != 0)) goto ie66;
                        void * t74 = GM_LD(void *, ((uint8_t *)&t73));
                        gm_l12 = t74;
                        {
                            void * t76 = gm_l12;
                            uint8_t t77 = 0;
                            F.ret = t76;
                            goto gm_fret;
                        }
                        goto ix66;
ie66:;
ix66:;
                    }
                    goto ix63;
ie63:;
ix63:;
                }
                {
                    void * t78 = gm_l11;
                    uint8_t t79 = 0;
                    F.ret = t78;
                    goto gm_fret;
                }
                goto ix39;
ie39:;
ix39:;
            }
            goto ix31;
ie31:;
ix31:;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t80 = gm_l0;
        void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
        gm_t88rcucg1b7uk t82 = gm_l2;
        gm_t8cc1mtbrhlpt t83;
        gm_f_soa_info_pq4q43l((void *)&t83, t81, GM_CONV(gm_csguankko4lmhd, t82));
        gm_t8cc1mtbrhlpt t84 = t83;
        gm_tjpqvfd72o62u t85;
        if (((((uint8_t *)&t84))[32] != 0)) {
            gm_tjpqvfd72o62u t86 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t84) + 0));
            t85 = t86;
        } else {
            uint8_t t87 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l13 = t85;
    }
    {
        uint8_t t89 = 0;
        uint8_t t90 = 0;
        uint8_t t91 = 0;
        uint8_t t92;
        if ((((((uint8_t *)&gm_l13) + 8))[8] != 0)) goto mn94;
        t92 = ((uint8_t)0);
        goto me93;
mn94:;
        t92 = ((uint8_t)1);
        goto me93;
mn95:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 240, 8);
me93:;
        if (!t92) {
            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 0));
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 0));
            gm_t88rcucg1b7uk t98 = t96;
            gm_t88rcucg1b7uk t99 = t97;
            uint8_t t100 = (!gm_eq9m6mvtmgde(((uint8_t *)&t98), ((uint8_t *)&t99)));
            t91 = t100;
        } else t91 = 1;
        if (!t91) {
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
            t90 = t101;
        } else t90 = 1;
        if (!t90) {
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
            uint8_t t103 = (uint8_t)!t102;
            t89 = t103;
        } else t89 = 1;
        if (!t89) goto ie88;
        {
            uint8_t t105 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t106 = gm_l0;
        void * t107 = gm_l1;
        void * t108 = (void *)((uint8_t *)&F.l14);
        void * t109 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t106, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t109;
    }
    {
    }
    {
        void * t110;
        void * t112 = gm_l0;
        void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
        uint8_t t114 = gm_f_soa_generic_kvk21mq(t113, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l13)));
        if (!t114) goto ie111;
        gm_t88rcucg1b7uk t116 = gm_l3;
        gm_tandvbt63uk7n t117;
        memset((uint8_t *)&t117 + 4, 0, 4);
        gm_tesaltf0mhcki t118;
        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t117 + 0, t118);
        {
            void * t120 = gm_l15;
            gm_tn3r4rvlh0rsv t121;
            GM_ST(void *, ((uint8_t *)&t121 + 0), t120);
            gm_l16 = t121;
        }
        void * t122 = ((uint8_t *)&gm_l16);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcb4m5qm2daehm, 244, 66);
        gm_b16_8 t123;
        GM_ST(void *, (uint8_t *)&t123, (void *)(((uint8_t *)t122 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t124 = t123;
        gm_b16_8 t125 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t117 + 8, t125);
        gm_t5brlu7fal70j t126;
        memset(&t126, 0, sizeof t126);
        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t126 + 8), t117);
        gm_tl874t2atnlh3 t127 = gm_l6;
        void * t128 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t116, &t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t129 = t128;
        t110 = t129;
        goto ix111;
ie111:;
        void * t131 = gm_l0;
        void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
        void * t133 = gm_l15;
        gm_t88rcucg1b7uk t134 = gm_l2;
        gm_t88rcucg1b7uk t135 = gm_l3;
        gm_tl874t2atnlh3 t136 = gm_l6;
        void * t137 = gm_f_soa_to_const_hhiu49k((void *)NULL, cx, NULL, t132, t133, t134, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t138 = t137;
        t110 = t138;
ix111:;
        gm_l17 = t110;
    }
    {
        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t140 = gm_l17;
        gm_tl874t2atnlh3 t141 = gm_l6;
        void * t142 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t139, t140, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t143 = 0;
        F.ret = t142;
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
inline __attribute__((aligned(4))) void * gm_f_soa_column_once_ads8l3i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_column_once_ads8l3i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    void * gm_l7; /* col */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = 0;
        uint8_t t4;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn6;
        t4 = ((uint8_t)0);
        goto me5;
mn6:;
        t4 = ((uint8_t)1);
        goto me5;
mn7:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 68, 8);
me5:;
        if (!t4) {
            void * t8 = gm_l1;
            uint8_t t9 = gm_f_soa_repeatable_rqn3pcn(t8);
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            uint64_t t14 = gm_l3;
            gm_tl874t2atnlh3 t15 = gm_l4;
            void * t16 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t12, t13, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        void * t20 = (void *)((uint8_t *)&F.l5);
        void * t21 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = gm_l6;
        uint64_t t25 = gm_l3;
        gm_tl874t2atnlh3 t26 = gm_l4;
        void * t27 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t23, t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t27;
    }
    {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        void * t29 = gm_l7;
        gm_tl874t2atnlh3 t30 = gm_l4;
        void * t31 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t28, t29, t30);
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
__attribute__((aligned(4))) void * gm_f_soa_columns_tuple_ns477md(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_soa_columns_tuple_ns477md F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* what */
    gm_tjpqvfd72o62u gm_l3; /* info */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    gm_b16_8 gm_l7; /* fields */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    void * gm_l15; /* c */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    GM_STACK_CHECK(&F, 1331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t8cc1mtbrhlpt t4;
        gm_f_soa_of_jufdcj2((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8cc1mtbrhlpt t5 = t4;
        gm_tjpqvfd72o62u t6;
        if (((((uint8_t *)&t5))[32] != 0)) {
            gm_tjpqvfd72o62u t7 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            void * t8 = gm_l1;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
        uint8_t t13 = gm_f_soa_generic_kvk21mq(t12, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)));
        if (!t13) goto ie10;
        {
            void * t15 = gm_l0;
            gm_b16_8 t16 = gm_l2;
            void * t17 = gm_l1;
            gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 208));
            void * t19 = gm_f_soa_needs_fields_vtvc2fa((void *)NULL, cx, NULL, t15, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t16, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint8_t t22;
        if ((((((uint8_t *)&gm_l3) + 8))[8] != 0)) goto mn24;
        t22 = ((uint8_t)0);
        goto me23;
mn24:;
        t22 = ((uint8_t)1);
        goto me23;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 350, 8);
me23:;
        if (!t22) goto ie21;
        {
            void * t27 = gm_l1;
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t29 = gm_l1;
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 208));
        gm_l4 = t30;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l1;
        void * t33 = (void *)((uint8_t *)&F.l5);
        void * t34 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t31, t32, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t34;
    }
    {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
        gm_b16_8 t37 = gm_f_soa_fields_dg06fgc(t36, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)));
        gm_l7 = t37;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t39 = gm_l7;
            gm_l11 = t39;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t41 = gm_l12;
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    gm_t8ta482bl6be0 t46;
                    uint64_t t47 = gm_l12;
                    GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
                    uint64_t t48 = gm_l12;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    memcpy((uint8_t *)&t46 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t49 * 40), 40);
                    gm_t8ta482bl6be0 t51 = t46;
                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&t51) + 0));
                    gm_l13 = t54;
                    gm_t0mglksrkfa5e t55 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t51) + 8));
                    F.l14 = t55;
                    {
                        void * t57 = gm_l0;
                        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                        void * t59 = gm_l6;
                        uint64_t t60 = gm_l13;
                        gm_tl874t2atnlh3 t61 = gm_l4;
                        void * t62 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t58, t59, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t60, t61);
                        if (gm_unw.kind) {
                            goto gm_ll44;
                        }
                        gm_l15 = t62;
                    }
                    {
                        void * t63 = (void *)((uint8_t *)&F.l9);
                        void * t64 = gm_l15;
                        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 0));
                        gm_tftb1odc739t5 t66 = gm_f_push_k3l9nic(t63, GM_CONV(gm_csguankko4lmhd, t65));
                        gm_tftb1odc739t5 t67 = t66;
                        if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcb4m5qm2daehm, 360, 9);
                        gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                    }
                    {
                        void * t69 = (void *)((uint8_t *)&F.l10);
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                        gm_tftb1odc739t5 t71 = gm_f_push_3ef746b(t69, t70);
                        gm_tftb1odc739t5 t72 = t71;
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcb4m5qm2daehm, 361, 9);
                        gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                    }
                    {
                        void * t74 = (void *)((uint8_t *)&F.l8);
                        void * t75 = gm_l15;
                        gm_tftb1odc739t5 t76 = gm_f_push_d943vgp_2(t74, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t75)));
                        gm_tftb1odc739t5 t77 = t76;
                        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcb4m5qm2daehm, 362, 9);
                        gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                    }
                    goto me52;
mn53:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 358, 5);
me52:;
                }
lc40:;
                {
                    uint64_t t80 = gm_l12;
                    uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                    uint64_t t83 = r81;
                    gm_l12 = t83;
                }
            }
gm_ll44:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
            }
            goto gm_lad;
lb40:;
        }
    }
    {
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
        void * t86 = (void *)(((uint8_t *)t85) + 8);
        gm_t6j6smj7qoctu t87;
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t89 = t88;
        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
        GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t90);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t92 = t91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t93);
        gm_t53at80pfdus3 t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t94 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t94 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t94 + 8), t87);
        gm_t88rcucg1b7uk t95 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t86, &t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t95;
    }
    {
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_t88rcucg1b7uk t97 = gm_l16;
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_t5brlu7fal70j t99;
        memset(&t99, 0, sizeof t99);
        GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 8), t98);
        gm_tl874t2atnlh3 t100 = gm_l4;
        void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t97, &t99, t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t102 = gm_l4;
        void * t103 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t96, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = 0;
        F.ret = t103;
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
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_construct_8nun9tl F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint64_t gm_l6; /* n */
    gm_b16_8 gm_l7; /* fields */
    uint8_t gm_l10; /* fill */
    void * gm_l12; /* x */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* k */
    void * gm_l17; /* part */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    void * gm_l23; /* x */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* k */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    void * gm_l31; /* x */
    GM_STACK_CHECK(&F, 3774u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        gm_tf5srh4mp4b3i t5 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)a2) + 8));
        gm_tf5srh4mp4b3i t6 = t5;
        uint64_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l4;
                void * t12 = gm_l0;
                gm_t88rcucg1b7uk t13 = gm_l1;
                gm_b16_8 t14 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t15;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
                gm_tabb710d9mq2e t16;
                gm_f_format_qu7vf4f((void *)&t16, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcb4m5qm2daehm, 255, 24);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                gm_b16_8 t19 = t18;
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                gm_b0_1 t21 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            gm_tl874t2atnlh3 t24 = gm_l4;
            void * t25 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        gm_l6 = t7;
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        uint8_t t30 = gm_f_soa_generic_kvk21mq(t29, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t30) goto ie27;
        {
            void * t32 = gm_l0;
            gm_tjpqvfd72o62u t33 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a2));
            void * t34 = gm_l0;
            gm_t88rcucg1b7uk t35 = gm_l1;
            gm_b16_8 t36 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_tabb710d9mq2e t38;
            gm_f_format_60vq8qs((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcb4m5qm2daehm, 259, 42);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_tl874t2atnlh3 t43 = gm_l4;
            void * t44 = gm_f_soa_needs_fields_vtvc2fa((void *)NULL, cx, NULL, t32, &t33, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t46 = gm_l0;
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
        gm_b16_8 t48 = gm_f_soa_fields_dg06fgc(t47, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l7 = t48;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t51 = ((t50 == ((uint64_t)1ULL)));
        if (!t51) goto ie49;
        uint64_t t52 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcb4m5qm2daehm, 265, 39);
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t52 * 192) + 0) + 0));
        if (t54 != ((uint8_t)1ULL)) goto ie49;
        gm_t2tlhjbsqg5eb t55 = GM_LD(gm_t2tlhjbsqg5eb, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t52 * 192) + 0) + 8));
        F.l11 = t55;
        {
            uint8_t t58 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l11) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l11) + 0) + 0)), gm_spevct0q3cjgb, 4) == 0));
            if (!t58) goto ie57;
            {
                gm_l10 = ((uint8_t)1);
            }
            {
                void * t60 = gm_l0;
                void * t61 = (void *)(((uint8_t *)&F.l11) + 32);
                gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
                void * t63 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t60, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t63;
            }
            {
                void * t64 = gm_l0;
                void * t65 = gm_l12;
                void * t66 = (void *)((uint8_t *)&F.l8);
                void * t67 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t64, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t67;
            }
            {
                {
                    gm_b16_8 t69 = gm_l7;
                    gm_l13 = t69;
                }
                {
                    gm_l14 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t71 = gm_l14;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        uint8_t t73 = (t71 < t72);
                        if (!t73) goto lb70;
                        {
                            gm_t8ta482bl6be0 t76;
                            uint64_t t77 = gm_l14;
                            GM_ST(uint64_t, (uint8_t *)&t76 + 0, t77);
                            uint64_t t78 = gm_l14;
                            uint64_t t79 = (uintptr_t)t78;
                            uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            memcpy((uint8_t *)&t76 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t79 * 40), 40);
                            gm_t8ta482bl6be0 t81 = t76;
                            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&t81) + 0));
                            gm_l15 = t84;
                            gm_t0mglksrkfa5e t85 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t81) + 8));
                            F.l16 = t85;
                            {
                                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                                void * t88 = gm_l12;
                                void * t89 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t88);
                                if (gm_unw.kind) {
                                    goto gm_ll74;
                                }
                                uint64_t t90 = gm_l15;
                                uint32_t t91 = (uint32_t)t90;
                                gm_tbl8il92l97ut t92;
                                GM_ST(uint32_t, (uint8_t *)&t92 + 12, 0);
                                GM_ST(void *, ((uint8_t *)&t92 + 0), t89);
                                GM_ST(uint32_t, ((uint8_t *)&t92 + 8), t91);
                                gm_t5brlu7fal70j t93;
                                memset(&t93, 0, sizeof t93);
                                GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)19ULL));
                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t93 + 8), t92);
                                gm_tl874t2atnlh3 t94 = gm_l4;
                                void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t87, &t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_ll74;
                                }
                                gm_l17 = t95;
                            }
                            {
                                void * t96 = (void *)((uint8_t *)&F.l9);
                                void * t97 = gm_l5;
                                uint64_t t98 = gm_l6;
                                gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                                gm_t88rcucg1b7uk t100 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t97, t98, t99);
                                if (gm_unw.kind) {
                                    goto gm_ll74;
                                }
                                void * t101 = gm_l17;
                                gm_t5brlu7fal70j t102;
                                memset(&t102, 0, sizeof t102);
                                GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)37ULL));
                                GM_ST(void *, ((uint8_t *)&t102 + 8), t101);
                                gm_tl874t2atnlh3 t103 = gm_l4;
                                void * t104 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t100, &t102, t103);
                                if (gm_unw.kind) {
                                    goto gm_ll74;
                                }
                                gm_tftb1odc739t5 t105 = gm_f_push_d943vgp_2(t96, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t104)));
                                gm_tftb1odc739t5 t106 = t105;
                                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srcb4m5qm2daehm, 272, 17);
                                gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
                            }
                            goto me82;
mn83:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 270, 13);
me82:;
                        }
lc70:;
                        {
                            uint64_t t109 = gm_l14;
                            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
                            uint64_t t112 = r110;
                            gm_l14 = t112;
                        }
                    }
gm_ll74:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                    }
                    goto gm_lad;
lb70:;
                }
            }
            goto ix57;
ie57:;
ix57:;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        uint8_t t114 = gm_l10;
        uint8_t t115 = (uint8_t)!t114;
        if (!t115) goto ie113;
        {
            gm_f_list_s1kpkpr((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t118 = gm_l3;
                gm_l19 = t118;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t120 = gm_l20;
                    uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t122 = (t120 < t121);
                    if (!t122) goto lb119;
                    {
                        uint64_t t125 = gm_l20;
                        uint64_t t126 = (uintptr_t)t125;
                        uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_tpff59ptr3s92 t130 = GM_LD(gm_tpff59ptr3s92, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t126 * 192));
                        F.l21 = t130;
                        {
                            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 0) + 0));
                            if (t134 != ((uint8_t)0ULL)) goto mn133;
                            gm_tfbvnc90lci8r t135 = GM_LD(gm_tfbvnc90lci8r, ((((uint8_t *)&F.l21) + 0) + 8));
                            F.l22 = t135;
                            {
                                void * t137 = gm_l0;
                                void * t138 = (void *)((uint8_t *)&F.l22);
                                gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
                                void * t140 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t137, t138, t139);
                                if (gm_unw.kind) {
                                    goto gm_ll123;
                                }
                                gm_l23 = t140;
                            }
                            {
                                void * t141 = (void *)((uint8_t *)&F.l18);
                                void * t142 = gm_l0;
                                void * t143 = gm_l23;
                                void * t144 = (void *)((uint8_t *)&F.l8);
                                void * t145 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t142, t143, t144);
                                if (gm_unw.kind) {
                                    goto gm_ll123;
                                }
                                gm_tftb1odc739t5 t146 = gm_f_push_pf02nv6(t141, t145);
                                gm_tftb1odc739t5 t147 = t146;
                                if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srcb4m5qm2daehm, 282, 21);
                                gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
                            }
                            goto me132;
mn133:;
                            {
                                void * t151 = gm_l0;
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 176));
                                gm_b16_8 t153;
                                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_s06mk2gjnlvf8));
                                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(72));
                                gm_b0_1 t154 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t151, t152, t153);
                                if (gm_unw.kind) {
                                    goto gm_ll123;
                                }
                            }
                            {
                                void * t155 = gm_l0;
                                void * t156 = GM_LD(void *, (((uint8_t *)t155) + 0));
                                gm_tl874t2atnlh3 t157 = gm_l4;
                                void * t158 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t156, t157);
                                if (gm_unw.kind) {
                                    goto gm_ll123;
                                }
                                uint8_t t159 = 0;
                                F.ret = t158;
                                goto gm_fret;
                            }
                            goto me132;
mn149:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 279, 13);
me132:;
                        }
                        goto me128;
mn129:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 278, 9);
me128:;
                    }
lc119:;
                    {
                        uint64_t t161 = gm_l20;
                        uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                        uint64_t t164 = r162;
                        gm_l20 = t164;
                    }
                }
gm_ll123:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb119; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc119; }
                }
                goto gm_lad;
lb119:;
            }
        }
        {
            uint64_t t166 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
            uint64_t t167 = (uint64_t)t166;
            uint64_t t168 = gm_l6;
            uint8_t t169 = (!(t167 == t168));
            if (!t169) goto ie165;
            {
                void * t171 = gm_l0;
                gm_tl874t2atnlh3 t172 = gm_l4;
                gm_tq3m5qgvkpje8 t173;
                void * t174 = gm_l0;
                gm_t88rcucg1b7uk t175 = gm_l1;
                gm_b16_8 t176 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 0, t176);
                uint64_t t177 = gm_l6;
                GM_ST(uint64_t, (uint8_t *)&t173 + 16, t177);
                uint64_t t178 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
                gm_b16_8 t179 = gm_f_given_27f1vc1((void *)NULL, cx, NULL, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 24, t179);
                gm_tabb710d9mq2e t180;
                gm_f_format_blrf705((void *)&t180, (void *)NULL, cx, NULL, &t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t181 = t180;
                if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srcb4m5qm2daehm, 291, 28);
                gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
                gm_b16_8 t183 = t182;
                gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                gm_b0_1 t185 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t171, t172, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t186 = gm_l0;
                void * t187 = GM_LD(void *, (((uint8_t *)t186) + 0));
                gm_tl874t2atnlh3 t188 = gm_l4;
                void * t189 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t187, t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t190 = 0;
                F.ret = t189;
                goto gm_fret;
            }
            goto ix165;
ie165:;
ix165:;
        }
        {
            {
                gm_b16_8 t192 = gm_l7;
                gm_l24 = t192;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t194 = gm_l25;
                    uint64_t t195 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t196 = (t194 < t195);
                    if (!t196) goto lb193;
                    {
                        gm_t8ta482bl6be0 t199;
                        uint64_t t200 = gm_l25;
                        GM_ST(uint64_t, (uint8_t *)&t199 + 0, t200);
                        uint64_t t201 = gm_l25;
                        uint64_t t202 = (uintptr_t)t201;
                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        memcpy((uint8_t *)&t199 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t202 * 40), 40);
                        gm_t8ta482bl6be0 t204 = t199;
                        uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&t204) + 0));
                        gm_l26 = t207;
                        gm_t0mglksrkfa5e t208 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t204) + 8));
                        F.l27 = t208;
                        {
                            gm_f_list_vp1djv1_2((void *)&F.l28, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll197;
                            }
                        }
                        {
                            {
                                gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                                gm_l29 = t211;
                            }
                            {
                                gm_l30 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t213 = gm_l30;
                                    uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                    uint8_t t215 = (t213 < t214);
                                    if (!t215) goto lb212;
                                    {
                                        uint64_t t218 = gm_l30;
                                        uint64_t t219 = (uintptr_t)t218;
                                        uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                        void * t221 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t219 * 8));
                                        void * t222 = t221;
                                        void * t225 = GM_LD(void *, ((uint8_t *)&t222));
                                        gm_l31 = t225;
                                        {
                                            void * t227 = (void *)((uint8_t *)&F.l28);
                                            gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                            void * t229 = gm_l31;
                                            void * t230 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t229);
                                            if (gm_unw.kind) {
                                                goto gm_ll216;
                                            }
                                            uint64_t t231 = gm_l26;
                                            uint32_t t232 = (uint32_t)t231;
                                            gm_tbl8il92l97ut t233;
                                            GM_ST(uint32_t, (uint8_t *)&t233 + 12, 0);
                                            GM_ST(void *, ((uint8_t *)&t233 + 0), t230);
                                            GM_ST(uint32_t, ((uint8_t *)&t233 + 8), t232);
                                            gm_t5brlu7fal70j t234;
                                            memset(&t234, 0, sizeof t234);
                                            GM_ST(uint8_t, ((uint8_t *)&t234 + 0), ((uint8_t)19ULL));
                                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t234 + 8), t233);
                                            gm_tl874t2atnlh3 t235 = gm_l4;
                                            void * t236 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t228, &t234, t235);
                                            if (gm_unw.kind) {
                                                goto gm_ll216;
                                            }
                                            gm_tftb1odc739t5 t237 = gm_f_push_d943vgp_2(t227, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t236)));
                                            gm_tftb1odc739t5 t238 = t237;
                                            if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 1, gm_srcb4m5qm2daehm, 296, 36);
                                            gm_b0_1 t239 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 1));
                                        }
                                        goto me223;
mn224:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 296, 13);
me223:;
                                    }
lc212:;
                                    {
                                        uint64_t t241 = gm_l30;
                                        uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                                        uint64_t t244 = r242;
                                        gm_l30 = t244;
                                    }
                                }
gm_ll216:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb212; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc212; }
                                }
                                goto gm_ll197;
lb212:;
                            }
                        }
                        {
                            void * t245 = (void *)((uint8_t *)&F.l9);
                            void * t246 = gm_l5;
                            uint64_t t247 = gm_l6;
                            gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            gm_t88rcucg1b7uk t249 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t246, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_ll197;
                            }
                            gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                            gm_t5brlu7fal70j t251;
                            memset(&t251, 0, sizeof t251);
                            GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)36ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t251 + 8), t250);
                            gm_tl874t2atnlh3 t252 = gm_l4;
                            void * t253 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t249, &t251, t252);
                            if (gm_unw.kind) {
                                goto gm_ll197;
                            }
                            gm_tftb1odc739t5 t254 = gm_f_push_d943vgp_2(t245, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t253)));
                            gm_tftb1odc739t5 t255 = t254;
                            if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_srcb4m5qm2daehm, 297, 13);
                            gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
                        }
                        goto me205;
mn206:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 294, 9);
me205:;
                    }
lc193:;
                    {
                        uint64_t t258 = gm_l25;
                        uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                        uint64_t t261 = r259;
                        gm_l25 = t261;
                    }
                }
gm_ll197:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb193; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc193; }
                }
                goto gm_lad;
lb193:;
            }
        }
        goto ix113;
ie113:;
ix113:;
    }
    {
        gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_t88rcucg1b7uk t263 = gm_l1;
        gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_t5brlu7fal70j t265;
        memset(&t265, 0, sizeof t265);
        GM_ST(uint8_t, ((uint8_t *)&t265 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t265 + 8), t264);
        gm_tl874t2atnlh3 t266 = gm_l4;
        void * t267 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t263, &t265, t266);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t268 = gm_l4;
        void * t269 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t262, t267, t268);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t270 = 0;
        F.ret = t269;
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
__attribute__((aligned(4))) void * gm_f_soa_copy_from_vrsguuh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_copy_from_vrsguuh F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tqs5p4n2tq19v gm_l6; /* mutability */
    uint8_t gm_l7; /* writable */
    uint8_t gm_l8; /* m */
    gm_b16_8 gm_l9; /* what */
    void * gm_l11; /* dst */
    gm_t88rcucg1b7uk gm_l12; /* src_ty */
    void * gm_l13; /* src */
    gm_t5fru0lp11n7t gm_l14; /* <temporary array> */
    void * gm_l15; /* copy */
    GM_STACK_CHECK(&F, 1260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        gm_tqs5p4n2tq19v t5;
        memset(&t5, 0, sizeof t5);
        gm_l6 = t5;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a2) + 24));
        uint8_t t7 = (uint8_t)!t6;
        gm_l7 = t7;
    }
    {
        uint8_t t9;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 430, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t14 = gm_l0;
            void * t15 = gm_l1;
            gm_tqs5p4n2tq19v t16 = gm_f_place_mutability_0tlm4g8(t14, t15);
            gm_l6 = t16;
        }
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            if (!((((uint8_t *)&gm_l6))[1] != 0)) goto ie17;
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
            gm_l8 = t18;
            {
                uint8_t t20 = gm_l8;
                gm_l7 = t20;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        uint8_t t22 = gm_l7;
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie21;
        {
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sf24jo61ba8vn));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(22));
            gm_l9 = t25;
        }
        {
            uint8_t t27;
            if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn29;
            t27 = ((uint8_t)0);
            goto me28;
mn29:;
            t27 = ((uint8_t)1);
            goto me28;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 437, 12);
me28:;
            if (!t27) goto ie26;
            {
                gm_b16_8 t32;
                uint8_t t34;
                if (((((uint8_t *)&gm_l6))[1] != 0)) goto mn36;
                t34 = ((uint8_t)1);
                goto me35;
mn36:;
                t34 = ((uint8_t)0);
                goto me35;
mn37:;
                gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 438, 23);
me35:;
                if (!t34) goto ie33;
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_scnj362636897));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(24));
                gm_b16_8 t40 = t39;
                t32 = t40;
                goto ix33;
ie33:;
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sr5or4g6pm2gc));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(25));
                gm_b16_8 t43 = t42;
                t32 = t43;
ix33:;
                gm_l9 = t32;
            }
            goto ix26;
ie26:;
ix26:;
        }
        {
            void * t44 = gm_l0;
            gm_tl874t2atnlh3 t45 = gm_l4;
            gm_b16_8 t46 = gm_l9;
            gm_tnpifv20irnph t47;
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
            gm_tabb710d9mq2e t48;
            gm_f_format_kgkjlf6((void *)&t48, (void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t49 = t48;
            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_srcb4m5qm2daehm, 440, 24);
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
            gm_b16_8 t51 = t50;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_b0_1 t53 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t55 = gm_l0;
        gm_b16_8 t56 = gm_l3;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sgvvkov5kvprt));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(9));
        gm_tl874t2atnlh3 t58 = gm_l4;
        uint8_t t59 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t55, t56, ((uint64_t)1ULL), t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t60 = (uint8_t)!t59;
        if (!t60) goto ie54;
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
        goto ix54;
ie54:;
ix54:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t67 = gm_l0;
        void * t68 = gm_l1;
        void * t69 = (void *)((uint8_t *)&F.l10);
        void * t70 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t67, t68, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t70;
    }
    {
        void * t71 = gm_l0;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_tf5srh4mp4b3i t74;
        memset(&t74, 0, sizeof t74);
        gm_tl874t2atnlh3 t75 = gm_l4;
        gm_t88rcucg1b7uk t76 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t72, t73, t74, ((uint8_t)1), t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t76;
    }
    {
        void * t77 = gm_l0;
        gm_b16_8 t78 = gm_l3;
        gm_t88rcucg1b7uk t79 = gm_l12;
        gm_tl874t2atnlh3 t80 = gm_l4;
        void * t81 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t77, t78, ((uint64_t)0ULL), t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t81;
    }
    {
        void * t83 = gm_l5;
        void * t84 = gm_l13;
        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t84) + 0));
        uint8_t t86 = gm_f_is_error_8k27gd1(t83, GM_CONV(gm_csguankko4lmhd, t85));
        if (!t86) goto ie82;
        {
            void * t88 = gm_l0;
            void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
            gm_tl874t2atnlh3 t90 = gm_l4;
            void * t91 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        void * t93 = gm_l0;
        void * t94 = gm_l13;
        void * t95 = (void *)((uint8_t *)&F.l10);
        void * t96 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t93, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t96;
    }
    {
    }
    {
        void * t97;
        void * t99 = gm_l0;
        void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
        uint8_t t101 = gm_f_soa_generic_kvk21mq(t100, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t101) goto ie98;
        void * t103 = gm_l5;
        gm_t88rcucg1b7uk t104 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tandvbt63uk7n t105;
        memset((uint8_t *)&t105 + 4, 0, 4);
        gm_tesaltf0mhcki t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t105 + 0, t106);
        {
            void * t108 = gm_l11;
            void * t109 = gm_l13;
            gm_t5fru0lp11n7t t110;
            GM_ST(void *, ((uint8_t *)&t110 + 0), t108);
            GM_ST(void *, ((uint8_t *)&t110 + 8), t109);
            gm_l14 = t110;
        }
        void * t111 = ((uint8_t *)&gm_l14);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcb4m5qm2daehm, 450, 71);
        gm_b16_8 t112;
        GM_ST(void *, (uint8_t *)&t112, (void *)(((uint8_t *)t111 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t113 = t112;
        gm_b16_8 t114 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t105 + 8, t114);
        gm_t5brlu7fal70j t115;
        memset(&t115, 0, sizeof t115);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t115 + 8), t105);
        gm_tl874t2atnlh3 t116 = gm_l4;
        void * t117 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t104, &t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t118 = t117;
        t97 = t118;
        goto ix98;
ie98:;
        void * t120 = gm_l0;
        void * t121 = GM_LD(void *, (((uint8_t *)t120) + 0));
        void * t122 = gm_l11;
        void * t123 = gm_l13;
        gm_tl874t2atnlh3 t124 = gm_l4;
        void * t125 = gm_f_soa_copy_columns_s4nhe5h((void *)NULL, cx, NULL, t121, t122, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t126 = t125;
        t97 = t126;
ix98:;
        gm_l15 = t97;
    }
    {
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        void * t128 = gm_l15;
        gm_tl874t2atnlh3 t129 = gm_l4;
        void * t130 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t127, t128, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t131 = 0;
        F.ret = t130;
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
inline __attribute__((aligned(4))) void * gm_f_soa_core_use_n5drmrl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_core_use_n5drmrl F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint32_t gm_l3; /* f */
    gm_tnpifv20irnph gm_l4; /* effects */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t6 = t5;
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                gm_tl874t2atnlh3 t11 = gm_l2;
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sfflovsm8rlpo));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(50));
                gm_b0_1 t13 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t14 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        uint32_t t15 = gm_l3;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 344) + 0) + 8);
        if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srcb4m5qm2daehm, 375, 16);
        gm_tnpifv20irnph t21 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 344) + 0)) + t17 * 136) + 64));
        gm_l4 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)((uint8_t *)&gm_l4);
        gm_tl874t2atnlh3 t24 = gm_l2;
        gm_b0_1 t25 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        gm_tl874t2atnlh3 t27 = gm_l2;
        void * t28 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_soa_deref_uiefo9t F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    GM_STACK_CHECK(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
        gm_l2 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l2;
        gm_t53at80pfdus3 t14;
        gm_f_kind_jcnt4hm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_t53at80pfdus3 t15 = t14;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t16 != ((uint8_t)8ULL)) goto ie9;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t15) + 8) + 4));
        gm_l3 = t17;
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_t88rcucg1b7uk t22 = gm_l3;
            gm_t8cc1mtbrhlpt t23;
            gm_f_soa_info_pq4q43l((void *)&t23, t21, GM_CONV(gm_csguankko4lmhd, t22));
            gm_t8cc1mtbrhlpt t24 = t23;
            uint8_t t25;
            if (((((uint8_t *)&t24))[32] != 0)) goto mn27;
            t25 = ((uint8_t)0);
            goto me26;
mn27:;
            t25 = ((uint8_t)1);
            goto me26;
mn28:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 22, 12);
me26:;
            if (!t25) goto ie19;
            {
                gm_t88rcucg1b7uk t30 = gm_l3;
                void * t31 = gm_l1;
                gm_t5brlu7fal70j t32;
                memset(&t32, 0, sizeof t32);
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t32 + 8), t31);
                void * t33 = gm_l1;
                gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 208));
                void * t35 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t30, &t32, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t37 = gm_l1;
        uint8_t t38 = 0;
        F.ret = t37;
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
inline __attribute__((aligned(4))) void * gm_f_soa_element_04p1d8c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_element_04p1d8c F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* index_ast */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    void * gm_l7; /* i */
    void * gm_l8; /* g */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = (void *)((uint8_t *)&F.l5);
        void * t5 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t2, t3, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l3;
        void * t8 = gm_f_soa_index_value_rabl6gs((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l7;
        void * t11 = (void *)((uint8_t *)&F.l5);
        void * t12 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l6;
        void * t15 = gm_l7;
        gm_tl874t2atnlh3 t16 = gm_l4;
        void * t17 = gm_f_soa_gather_7oos4rn((void *)NULL, cx, NULL, t13, t14, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t17;
    }
    {
        gm_tl874t2atnlh3 t19 = gm_l4;
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 2496) + 0));
        gm_b16_8 t23 = t22;
        uint8_t t24 = 0;
        gm_tl874t2atnlh3 t25 = t19;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t23) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t23))) + i * 12, ((uint8_t *)&t25))) { t24 = 1; break; }
        uint8_t t26 = (uint8_t)(t24);
        uint8_t t27 = (uint8_t)!t26;
        if (!t27) goto ie18;
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            void * t31 = (void *)(((uint8_t *)t30) + 2496);
            gm_tl874t2atnlh3 t32 = gm_l4;
            gm_tftb1odc739t5 t33 = gm_f_push_0mk7udh(t31, GM_CONV(gm_cseahvg8v7p0vs, t32));
            gm_tftb1odc739t5 t34 = t33;
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcb4m5qm2daehm, 108, 45);
            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        void * t37 = gm_l8;
        gm_tl874t2atnlh3 t38 = gm_l4;
        void * t39 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t36, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_soa_element_field_lo71q0l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, const void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_element_field_lo71q0l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* index_ast */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l6; /* fields */
    uint64_t gm_l7; /* k */
    void * gm_l8; /* col */
    void * gm_l9; /* i */
    GM_STACK_CHECK(&F, 1252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l5 = a5;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = gm_f_soa_generic_kvk21mq(t4, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t5) goto ie2;
        {
            {
                void * t8 = gm_l0;
                void * t9 = gm_l3;
                gm_tkn42u7na3t81 t10;
                memset(&t10, 0, sizeof t10);
                void * t11 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t8, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t12 = gm_l0;
            gm_tjpqvfd72o62u t13 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a2));
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
            gm_tnpifv20irnph t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
            gm_tabb710d9mq2e t16;
            gm_f_format_te2qqk4((void *)&t16, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcb4m5qm2daehm, 116, 42);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_tl874t2atnlh3 t21 = gm_l5;
            void * t22 = gm_f_soa_needs_fields_vtvc2fa((void *)NULL, cx, NULL, t12, &t13, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        gm_b16_8 t26 = gm_f_soa_fields_dg06fgc(t25, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l6 = t26;
    }
    {
        gm_b16_8 t27 = gm_l6;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
        gm_tf5srh4mp4b3i t29 = gm_f_soa_field_index_tas8jh7(t27, t28);
        gm_tf5srh4mp4b3i t30 = t29;
        uint64_t t31;
        if (((((uint8_t *)&t30))[8] != 0)) {
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            {
                {
                    void * t35 = gm_l0;
                    void * t36 = gm_l3;
                    gm_tkn42u7na3t81 t37;
                    memset(&t37, 0, sizeof t37);
                    void * t38 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t35, t36, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            {
                void * t39 = gm_l0;
                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a4) + 16));
                gm_t6j6smj7qoctu t41;
                void * t42 = gm_l0;
                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
                gm_b16_8 t44 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t44);
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t45);
                gm_tabb710d9mq2e t46;
                gm_f_format_htqqv8t((void *)&t46, (void *)NULL, cx, NULL, &t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t47 = t46;
                if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_srcb4m5qm2daehm, 121, 29);
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                gm_b16_8 t49 = t48;
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t40, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            gm_tl874t2atnlh3 t54 = gm_l5;
            void * t55 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        gm_l7 = t31;
    }
    {
        void * t58 = gm_l0;
        uint64_t t59 = gm_l7;
        uint64_t t60 = (uintptr_t)t59;
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srcb4m5qm2daehm, 124, 34);
        gm_t9qkh0rsooc8d t62 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t60 * 40) + 20));
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        uint8_t t64 = gm_f_field_visible_here_20vnoda(t58, t62, GM_CONV(gm_csguankko4lmhd, t63));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) goto ie57;
        {
            void * t67 = gm_l0;
            gm_tl874t2atnlh3 t68 = gm_l5;
            void * t69 = gm_l0;
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)a4) + 0));
            gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
            uint64_t t72 = gm_l7;
            uint64_t t73 = (uintptr_t)t72;
            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t73 >= (uintptr_t)(t74)) gm_trap_index(t73, t74, gm_srcb4m5qm2daehm, 125, 71);
            gm_t9qkh0rsooc8d t75 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t73 * 40) + 20));
            gm_b16_8 t76 = gm_f_hidden_field_message_b0pdek7((void *)NULL, cx, NULL, t69, t70, t71, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t77 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t67, t68, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        void * t78 = gm_l0;
        void * t79 = gm_l1;
        uint64_t t80 = gm_l7;
        gm_tl874t2atnlh3 t81 = gm_l5;
        void * t82 = gm_f_soa_column_once_ads8l3i((void *)NULL, cx, NULL, t78, t79, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t82;
    }
    {
        void * t83 = gm_l0;
        void * t84 = gm_l3;
        void * t85 = gm_f_soa_index_value_rabl6gs((void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t85;
    }
    {
        uint64_t t86 = gm_l7;
        uint64_t t87 = (uintptr_t)t86;
        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_srcb4m5qm2daehm, 129, 19);
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t87 * 40) + 16));
        gm_tlcl4u558l6p5 t90;
        memset((uint8_t *)&t90 + 17, 0, 7);
        void * t91 = gm_l8;
        GM_ST(void *, (uint8_t *)&t90 + 0, t91);
        void * t92 = gm_l9;
        GM_ST(void *, (uint8_t *)&t90 + 8, t92);
        GM_ST(uint8_t, (uint8_t *)&t90 + 16, ((uint8_t)1));
        gm_t5brlu7fal70j t93;
        memset(&t93, 0, sizeof t93);
        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t93 + 8), t90);
        gm_tl874t2atnlh3 t94 = gm_l5;
        void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t96 = 0;
        F.ret = t95;
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
__attribute__((aligned(4))) void * gm_f_soa_eq_ho8ji9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, uint8_t a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_eq_ho8ji9t F;
    void * gm_l0; /* cx */
    void * gm_l1; /* l */
    void * gm_l2; /* r */
    gm_tjpqvfd72o62u gm_l3; /* info */
    uint8_t gm_l4; /* negated */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    void * gm_l8; /* lv */
    void * gm_l9; /* rv */
    void * gm_l10; /* all */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    void * gm_l14; /* a */
    void * gm_l15; /* b */
    void * gm_l16; /* eq */
    void * gm_l17; /* x */
    void * gm_l18; /* e */
    GM_STACK_CHECK(&F, 1496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_soa_generic_kvk21mq(t7, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)));
        if (!t8) goto ie5;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_st9n2vprlv3c1));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(21));
            gm_tl874t2atnlh3 t12 = gm_l5;
            void * t13 = gm_f_soa_needs_fields_vtvc2fa((void *)NULL, cx, NULL, t10, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l1;
        void * t17 = (void *)((uint8_t *)&F.l7);
        void * t18 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t15, t16, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l2;
        void * t21 = (void *)((uint8_t *)&F.l7);
        void * t22 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t19, t20, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t22;
    }
    {
        gm_l10 = ((void *)0);
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            gm_b16_8 t26 = gm_f_soa_fields_dg06fgc(t25, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)));
            gm_b16_8 t27 = t26;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&t27) + 8);
            gm_l11 = t28;
        }
        {
            for (;;) {
                uint64_t t30 = gm_l12;
                uint64_t t31 = gm_l11;
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                {
                    uint64_t t35 = gm_l12;
                    uint64_t t36 = t35;
                    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t36));
                    gm_l13 = t39;
                    {
                        void * t41 = gm_l0;
                        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
                        void * t43 = gm_l8;
                        uint64_t t44 = gm_l13;
                        gm_tl874t2atnlh3 t45 = gm_l5;
                        void * t46 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t42, t43, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t44, t45);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_l14 = t46;
                    }
                    {
                        void * t47 = gm_l0;
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                        void * t49 = gm_l9;
                        uint64_t t50 = gm_l13;
                        gm_tl874t2atnlh3 t51 = gm_l5;
                        void * t52 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t48, t49, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l3)), t50, t51);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_l15 = t52;
                    }
                    {
                        void * t53 = gm_l6;
                        gm_t88rcucg1b7uk t54 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_tlcl4u558l6p5 t55;
                        memset((uint8_t *)&t55 + 17, 0, 7);
                        void * t56 = gm_l14;
                        GM_ST(void *, (uint8_t *)&t55 + 0, t56);
                        void * t57 = gm_l15;
                        GM_ST(void *, (uint8_t *)&t55 + 8, t57);
                        GM_ST(uint8_t, (uint8_t *)&t55 + 16, ((uint8_t)0));
                        gm_t5brlu7fal70j t58;
                        memset(&t58, 0, sizeof t58);
                        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)43ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t58 + 8), t55);
                        gm_tl874t2atnlh3 t59 = gm_l5;
                        void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t58, t59);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_l16 = t60;
                    }
                    {
                        void * t62 = gm_l10;
                        void * t63 = t62;
                        if (!(GM_LD(void *, ((uint8_t *)&t63)) != 0)) goto ie61;
                        void * t64 = GM_LD(void *, ((uint8_t *)&t63));
                        gm_l17 = t64;
                        {
                            void * t66 = gm_l6;
                            gm_t88rcucg1b7uk t67 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t66);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_ta612fil812pm t68;
                            memset((uint8_t *)&t68 + 1, 0, 7);
                            GM_ST(uint8_t, (uint8_t *)&t68 + 0, ((uint8_t)1));
                            void * t69 = gm_l17;
                            GM_ST(void *, (uint8_t *)&t68 + 8, t69);
                            void * t70 = gm_l16;
                            GM_ST(void *, (uint8_t *)&t68 + 16, t70);
                            gm_t5brlu7fal70j t71;
                            memset(&t71, 0, sizeof t71);
                            GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)31ULL));
                            GM_ST(gm_ta612fil812pm, ((uint8_t *)&t71 + 8), t68);
                            gm_tl874t2atnlh3 t72 = gm_l5;
                            void * t73 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t67, &t71, t72);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_l10 = t73;
                        }
                        goto ix61;
ie61:;
                        {
                            void * t75 = gm_l16;
                            gm_l10 = t75;
                        }
ix61:;
                    }
                    goto me37;
mn38:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 329, 5);
me37:;
                }
lc29:;
                {
                    uint64_t t77 = gm_l12;
                    uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                    uint64_t t80 = r78;
                    gm_l12 = t80;
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
        void * t81 = gm_l10;
        void * t82 = t81;
        void * t83;
        if ((GM_LD(void *, ((uint8_t *)&t82)) != 0)) {
            void * t84 = GM_LD(void *, (((uint8_t *)&t82) + 0));
            t83 = t84;
        } else {
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
            gm_tl874t2atnlh3 t87 = gm_l5;
            void * t88 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        gm_l18 = t83;
    }
    {
        uint8_t t91 = gm_l4;
        if (!t91) goto ie90;
        {
            void * t93 = gm_l6;
            gm_t88rcucg1b7uk t94 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t95;
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)2ULL));
            void * t96 = gm_l18;
            gm_t8ghgl2jj0qha t97;
            GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t97 + 0), t95);
            GM_ST(void *, ((uint8_t *)&t97 + 8), t96);
            gm_t5brlu7fal70j t98;
            memset(&t98, 0, sizeof t98);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t98 + 8), t97);
            gm_tl874t2atnlh3 t99 = gm_l5;
            void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t100;
        }
        goto ix90;
ie90:;
ix90:;
    }
    {
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        void * t102 = gm_l18;
        gm_tl874t2atnlh3 t103 = gm_l5;
        void * t104 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t101, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t105 = 0;
        F.ret = t104;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_soa_field_index_tas8jh7(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_field_index_tas8jh7 F;
    gm_b16_8 gm_l0; /* fields */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    GM_STACK_CHECK(&F, 428u);
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
                    gm_t8ta482bl6be0 t10;
                    uint64_t t11 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t10 + 0, t11);
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t10 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 40), 40);
                    gm_t8ta482bl6be0 t15 = t10;
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l4 = t18;
                    gm_t0mglksrkfa5e t19 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t15) + 8));
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
                    gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 60, 5);
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
__attribute__((aligned(4))) void * gm_f_soa_for_ovoqsoj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3) {
    struct gm_fr_soa_for_ovoqsoj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* src */
    gm_tjpqvfd72o62u gm_l3; /* info */
    void * gm_l4; /* tys */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_t88rcucg1b7uk gm_l6; /* usize_ */
    void * gm_l8; /* v */
    uint32_t gm_l9; /* i */
    uint32_t gm_l10; /* n */
    void * gm_l11; /* len */
    void * gm_l12; /* item */
    void * gm_l13; /* lp */
    GM_STACK_CHECK(&F, 1161u);
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
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l1;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t5) + 104));
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t8;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        void * t11 = (void *)((uint8_t *)&F.l7);
        void * t12 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t9, t10, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a3)), t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t12;
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l6;
        gm_tl874t2atnlh3 t15 = gm_l5;
        uint32_t t16 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l6;
        gm_tl874t2atnlh3 t19 = gm_l5;
        uint32_t t20 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l8;
        gm_tl874t2atnlh3 t23 = gm_l5;
        void * t24 = gm_f_soa_len_m9p2hp1((void *)NULL, cx, NULL, t21, t22, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a3)), t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t24;
    }
    {
        void * t25 = (void *)((uint8_t *)&F.l7);
        uint32_t t26 = gm_l9;
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        gm_tl874t2atnlh3 t29 = gm_l5;
        void * t30 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t28, ((uint64_t)0ULL), t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t31;
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t26);
        GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
        gm_trcmmd12cefb1 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t32 + 8), t31);
        gm_tftb1odc739t5 t33 = gm_f_push_d6c76ud_2(t25, &t32);
        gm_tftb1odc739t5 t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcb4m5qm2daehm, 313, 5);
        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l7);
        uint32_t t37 = gm_l10;
        void * t38 = gm_l11;
        gm_ttggq7ng4kp3p t39;
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t37);
        GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
        gm_trcmmd12cefb1 t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t40 + 8), t39);
        gm_tftb1odc739t5 t41 = gm_f_push_d6c76ud_2(t36, &t40);
        gm_tftb1odc739t5 t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcb4m5qm2daehm, 314, 5);
        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
    }
    {
        void * t44 = gm_l0;
        void * t45 = gm_l8;
        gm_tjpqvfd72o62u t46 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a3));
        gm_t88rcucg1b7uk t47 = gm_l6;
        uint32_t t48 = gm_l9;
        gm_t5brlu7fal70j t49;
        memset(&t49, 0, sizeof t49);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t49 + 8), t48);
        gm_tl874t2atnlh3 t50 = gm_l5;
        void * t51 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t47, &t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t52 = gm_l5;
        void * t53 = gm_f_soa_gather_7oos4rn((void *)NULL, cx, NULL, t44, t45, &t46, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t53;
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l1;
        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a3) + 0));
        uint32_t t57 = gm_l9;
        gm_t88rcucg1b7uk t58 = gm_l6;
        gm_t88rcucg1b7uk t59 = gm_l6;
        uint32_t t60 = gm_l10;
        gm_t5brlu7fal70j t61;
        memset(&t61, 0, sizeof t61);
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t61 + 8), t60);
        gm_tl874t2atnlh3 t62 = gm_l5;
        void * t63 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t59, &t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t64 = gm_l12;
        void * t65 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t54, t55, t56, t57, t58, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t65;
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l7);
        void * t67 = gm_l13;
        gm_trcmmd12cefb1 t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t68 + 8), t67);
        gm_tftb1odc739t5 t69 = gm_f_push_d6c76ud_2(t66, &t68);
        gm_tftb1odc739t5 t70 = t69;
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcb4m5qm2daehm, 317, 5);
        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
    }
    {
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_tl874t2atnlh3 t75 = gm_l5;
        void * t76 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t73, t74, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t77 = 0;
        F.ret = t76;
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
__attribute__((aligned(4))) void * gm_f_soa_free_9554eha(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_free_9554eha F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* site */
    void * gm_l8; /* v */
    gm_t5fru0lp11n7t gm_l9; /* <temporary array> */
    void * gm_l10; /* call */
    GM_STACK_CHECK(&F, 914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t6;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 408, 8);
me7:;
        if (!t6) goto ie5;
        {
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l4;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_shvoch74oh0kq));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(58));
            gm_b0_1 t14 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_tl874t2atnlh3 t17 = gm_l4;
            void * t18 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        {
            void * t21 = gm_l0;
            gm_b16_8 t22 = gm_l3;
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_srv4cua6q51nq));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_tl874t2atnlh3 t24 = gm_l4;
            uint8_t t25 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t21, t22, ((uint64_t)0ULL), t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        void * t26 = gm_l0;
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sg7n1u5l63uaa));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(8));
        gm_tl874t2atnlh3 t28 = gm_l4;
        void * t29 = gm_f_soa_core_use_n5drmrl((void *)NULL, cx, NULL, t26, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t30 = t29;
        void * t31;
        if ((GM_LD(void *, ((uint8_t *)&t30)) != 0)) {
            void * t32 = GM_LD(void *, (((uint8_t *)&t30) + 0));
            t31 = t32;
        } else {
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
            gm_tl874t2atnlh3 t35 = gm_l4;
            void * t36 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        gm_l6 = t31;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l1;
        void * t40 = (void *)((uint8_t *)&F.l7);
        void * t41 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t38, t39, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t41;
    }
    {
    }
    {
        void * t42;
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        uint8_t t46 = gm_f_soa_generic_kvk21mq(t45, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t46) goto ie43;
        void * t48 = gm_l5;
        gm_t88rcucg1b7uk t49 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tandvbt63uk7n t50;
        memset((uint8_t *)&t50 + 4, 0, 4);
        gm_tesaltf0mhcki t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)6ULL));
        GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t50 + 0, t51);
        {
            void * t53 = gm_l8;
            void * t54 = gm_l6;
            gm_t5fru0lp11n7t t55;
            GM_ST(void *, ((uint8_t *)&t55 + 0), t53);
            GM_ST(void *, ((uint8_t *)&t55 + 8), t54);
            gm_l9 = t55;
        }
        void * t56 = ((uint8_t *)&gm_l9);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcb4m5qm2daehm, 417, 67);
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t56 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t58 = t57;
        gm_b16_8 t59 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 8, t59);
        gm_t5brlu7fal70j t60;
        memset(&t60, 0, sizeof t60);
        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t60 + 8), t50);
        gm_tl874t2atnlh3 t61 = gm_l4;
        void * t62 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t63 = t62;
        t42 = t63;
        goto ix43;
ie43:;
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        void * t67 = gm_l8;
        void * t68 = gm_l6;
        gm_tl874t2atnlh3 t69 = gm_l4;
        void * t70 = gm_f_soa_free_columns_n1tvn1j((void *)NULL, cx, NULL, t66, t67, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = t70;
        t42 = t71;
ix43:;
        gm_l10 = t42;
    }
    {
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        void * t73 = gm_l10;
        gm_tl874t2atnlh3 t74 = gm_l4;
        void * t75 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t72, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = 0;
        F.ret = t75;
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
inline __attribute__((aligned(4))) void * gm_f_soa_gather_7oos4rn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_gather_7oos4rn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t5fru0lp11n7t gm_l5; /* <temporary array> */
    GM_STACK_CHECK(&F, 511u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = gm_f_soa_generic_kvk21mq(t4, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t5) goto ie2;
        {
        }
        {
            gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
            gm_tandvbt63uk7n t8;
            memset((uint8_t *)&t8 + 4, 0, 4);
            gm_tesaltf0mhcki t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t8 + 0, t9);
            {
                void * t11 = gm_l1;
                void * t12 = gm_l3;
                gm_t5fru0lp11n7t t13;
                GM_ST(void *, ((uint8_t *)&t13 + 0), t11);
                GM_ST(void *, ((uint8_t *)&t13 + 8), t12);
                gm_l5 = t13;
            }
            void * t14 = ((uint8_t *)&gm_l5);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcb4m5qm2daehm, 86, 72);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t14 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t16 = t15;
            gm_b16_8 t17 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 8, t17);
            gm_t5brlu7fal70j t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t18 + 8), t8);
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t7, &t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
        void * t24 = gm_l1;
        void * t25 = gm_l3;
        gm_tl874t2atnlh3 t26 = gm_l4;
        void * t27 = gm_f_soa_gather_of_4timqsa((void *)NULL, cx, NULL, t23, t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = 0;
        F.ret = t27;
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
inline __attribute__((aligned(4))) void * gm_f_soa_index_value_rabl6gs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_soa_index_value_rabl6gs F;
    void * gm_l0; /* cx */
    void * gm_l1; /* index_ast */
    void * gm_l2; /* tys */
    void * gm_l3; /* i */
    GM_STACK_CHECK(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        gm_tkn42u7na3t81 t7;
        memset(&t7, 0, sizeof t7);
        void * t8 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 96);
        void * t12 = gm_l2;
        void * t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
        uint8_t t16 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t11, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie9;
        void * t18 = gm_l2;
        void * t19 = gm_l3;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_f_is_error_8k27gd1(t18, GM_CONV(gm_csguankko4lmhd, t20));
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie9;
        {
            void * t24 = gm_l0;
            void * t25 = gm_l3;
            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 208));
            void * t27 = gm_l0;
            void * t28 = gm_l0;
            void * t29 = (void *)(((uint8_t *)t28) + 96);
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            void * t32 = gm_l3;
            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
            gm_t88rcucg1b7uk t34 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t29, t31, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t35 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t27, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t36;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
            gm_tabb710d9mq2e t37;
            gm_f_format_amt7kei((void *)&t37, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t38 = t37;
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_srcb4m5qm2daehm, 96, 26);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
            gm_b16_8 t40 = t39;
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t24, t26, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t43 = gm_l3;
        uint8_t t44 = 0;
        F.ret = t43;
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
inline __attribute__((aligned(4))) void * gm_f_soa_len_m9p2hp1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_len_m9p2hp1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tn3r4rvlh0rsv gm_l4; /* <temporary array> */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        uint8_t t3;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn5;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn6:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 77, 8);
me4:;
        if (!t3) goto ie2;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_soa_generic_kvk21mq(t8, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t9) goto ie2;
        {
        }
        {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            void * t13 = (void *)(((uint8_t *)t12) + 8);
            gm_t88rcucg1b7uk t14 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tandvbt63uk7n t15;
            memset((uint8_t *)&t15 + 4, 0, 4);
            gm_tesaltf0mhcki t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t15 + 0, t16);
            {
                void * t18 = gm_l1;
                gm_tn3r4rvlh0rsv t19;
                GM_ST(void *, ((uint8_t *)&t19 + 0), t18);
                gm_l4 = t19;
            }
            void * t20 = ((uint8_t *)&gm_l4);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcb4m5qm2daehm, 78, 82);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t20 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t22 = t21;
            gm_b16_8 t23 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t15 + 8, t23);
            gm_t5brlu7fal70j t24;
            memset(&t24, 0, sizeof t24);
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t24 + 8), t15);
            gm_tl874t2atnlh3 t25 = gm_l3;
            void * t26 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t14, &t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = gm_l1;
        gm_tl874t2atnlh3 t31 = gm_l3;
        void * t32 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t29, t30, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_soa_make_5imjrl1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_make_5imjrl1 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* n */
    void * gm_l7; /* site */
    void * gm_l9; /* m */
    gm_t88rcucg1b7uk gm_l10; /* err */
    uint32_t gm_l11; /* f */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_t5fru0lp11n7t gm_l13; /* <temporary array> */
    GM_STACK_CHECK(&F, 1401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t6;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 383, 8);
me7:;
        if (!t6) goto ie5;
        {
            void * t11 = gm_l0;
            gm_tl874t2atnlh3 t12 = gm_l4;
            void * t13 = gm_l0;
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_b16_8 t15 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t16;
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
            gm_tabb710d9mq2e t17;
            gm_f_format_4hl9u7b((void *)&t17, (void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcb4m5qm2daehm, 384, 24);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = t19;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_b0_1 t22 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t11, t12, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            gm_tl874t2atnlh3 t25 = gm_l4;
            void * t26 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t29 = gm_l0;
        gm_b16_8 t30 = gm_l3;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sgb0oed9c3voo));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(4));
        gm_tl874t2atnlh3 t32 = gm_l4;
        uint8_t t33 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t29, t30, ((uint64_t)1ULL), t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t34 = (uint8_t)!t33;
        if (!t34) goto ie28;
        {
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
            gm_tl874t2atnlh3 t38 = gm_l4;
            void * t39 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t41 = gm_l0;
        gm_b16_8 t42 = gm_l3;
        void * t43 = gm_l5;
        gm_t88rcucg1b7uk t44 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t45 = gm_l4;
        void * t46 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t41, t42, ((uint64_t)0ULL), t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t46;
    }
    {
        void * t47 = gm_l0;
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_suh0878eve8t9));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(9));
        gm_tl874t2atnlh3 t49 = gm_l4;
        void * t50 = gm_f_soa_core_use_n5drmrl((void *)NULL, cx, NULL, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t51 = t50;
        void * t52;
        if ((GM_LD(void *, ((uint8_t *)&t51)) != 0)) {
            void * t53 = GM_LD(void *, (((uint8_t *)&t51) + 0));
            t52 = t53;
        } else {
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
            gm_tl874t2atnlh3 t56 = gm_l4;
            void * t57 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        gm_l7 = t52;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t59 = gm_l0;
        void * t60 = gm_l6;
        void * t61 = (void *)((uint8_t *)&F.l8);
        void * t62 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t59, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t62;
    }
    {
        void * t63 = gm_l6;
        gm_l9 = t63;
    }
    {
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
        uint8_t t67 = gm_f_soa_generic_kvk21mq(t66, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t67) goto ie64;
        {
            void * t69 = gm_l5;
            gm_t88rcucg1b7uk t70 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t70;
        }
        {
            void * t72 = gm_l0;
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_ss496b65r9omj));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(5));
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_suh0878eve8t9));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(9));
            gm_t9805qtonbsag t75 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t72, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t76 = t75;
            if (!((((uint8_t *)&t76))[4] != 0)) goto ie71;
            uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)&t76));
            gm_l11 = t77;
            {
                void * t80 = gm_l5;
                uint32_t t81 = gm_l11;
                uint64_t t82 = (uint64_t)t81;
                uint64_t t83 = (uintptr_t)t82;
                void * t84 = gm_l0;
                void * t85 = GM_LD(void *, (((uint8_t *)t84) + 0));
                uint64_t t86 = GM_LD(uintptr_t, ((((uint8_t *)t85) + 344) + 0) + 8);
                if (t83 >= (uintptr_t)(t86)) gm_trap_index(t83, t86, gm_srcb4m5qm2daehm, 396, 42);
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t85) + 344) + 0)) + t83 * 136) + 56));
                gm_t53at80pfdus3 t88;
                gm_f_kind_jcnt4hm((void *)&t88, t80, GM_CONV(gm_csguankko4lmhd, t87));
                gm_t53at80pfdus3 t89 = t88;
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t89) + 0));
                if (t90 != ((uint8_t)14ULL)) goto ie79;
                gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t89) + 8) + 0));
                gm_l12 = t91;
                {
                    gm_t88rcucg1b7uk t93 = gm_l12;
                    gm_l10 = t93;
                }
                goto ix79;
ie79:;
ix79:;
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
        }
        {
            void * t94 = gm_l5;
            gm_t88rcucg1b7uk t95 = gm_l10;
            gm_t88rcucg1b7uk t96 = gm_l1;
            gm_t88rcucg1b7uk t97 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t94, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tandvbt63uk7n t98;
            memset((uint8_t *)&t98 + 4, 0, 4);
            gm_tesaltf0mhcki t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)5ULL));
            GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t98 + 0, t99);
            {
                void * t101 = gm_l6;
                void * t102 = gm_l7;
                gm_t5fru0lp11n7t t103;
                GM_ST(void *, ((uint8_t *)&t103 + 0), t101);
                GM_ST(void *, ((uint8_t *)&t103 + 8), t102);
                gm_l13 = t103;
            }
            void * t104 = ((uint8_t *)&gm_l13);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcb4m5qm2daehm, 398, 79);
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(((uint8_t *)t104 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t106 = t105;
            gm_b16_8 t107 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t98 + 8, t107);
            gm_t5brlu7fal70j t108;
            memset(&t108, 0, sizeof t108);
            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t108 + 8), t98);
            gm_tl874t2atnlh3 t109 = gm_l4;
            void * t110 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t97, &t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t110;
        }
        goto ix64;
ie64:;
        {
            void * t112 = gm_l0;
            void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
            void * t114 = gm_l0;
            void * t115 = (void *)(((uint8_t *)t114) + 16);
            gm_t88rcucg1b7uk t116 = gm_l1;
            void * t117 = gm_l6;
            void * t118 = gm_l7;
            gm_tl874t2atnlh3 t119 = gm_l4;
            void * t120 = gm_f_soa_make_columns_ardr0ua((void *)NULL, cx, NULL, t113, t115, t116, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t117, t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t120;
        }
ix64:;
    }
    {
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        void * t122 = gm_l9;
        gm_tl874t2atnlh3 t123 = gm_l4;
        void * t124 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t121, t122, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t125 = 0;
        F.ret = t124;
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
__attribute__((aligned(4))) void * gm_f_soa_member_vkrr5kf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_member_vkrr5kf F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    void * gm_l7; /* len */
    gm_b16_8 gm_l8; /* fields */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint64_t gm_l10; /* k */
    GM_STACK_CHECK(&F, 1067u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, (((uint8_t *)a3) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)a3) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
        if (!t3) goto ie2;
        {
            gm_f_list_ir0h3tc_2((void *)&F.l5, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t5;
            uint8_t t7;
            if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn9;
            t7 = ((uint8_t)0);
            goto me8;
mn9:;
            t7 = ((uint8_t)1);
            goto me8;
mn10:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 136, 17);
me8:;
            if (!t7) goto ie6;
            void * t12 = gm_l1;
            void * t13 = t12;
            t5 = t13;
            goto ix6;
ie6:;
            void * t15 = gm_l0;
            void * t16 = gm_l1;
            void * t17 = (void *)((uint8_t *)&F.l5);
            void * t18 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t15, t16, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t19 = t18;
            t5 = t19;
ix6:;
            gm_l6 = t5;
        }
        {
            void * t20 = gm_l0;
            void * t21 = gm_l6;
            gm_tl874t2atnlh3 t22 = gm_l4;
            void * t23 = gm_f_soa_len_m9p2hp1((void *)NULL, cx, NULL, t20, t21, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t23;
        }
        {
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            void * t25 = gm_l7;
            gm_tl874t2atnlh3 t26 = gm_l4;
            void * t27 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        uint8_t t32 = gm_f_soa_generic_kvk21mq(t31, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t32) goto ie29;
        {
            void * t34 = gm_l0;
            gm_tjpqvfd72o62u t35 = GM_LD(gm_tjpqvfd72o62u, ((uint8_t *)a2));
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_tabb710d9mq2e t38;
            gm_f_format_8g701o0((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srcb4m5qm2daehm, 141, 42);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = t40;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_tl874t2atnlh3 t43 = gm_l4;
            void * t44 = gm_f_soa_needs_fields_vtvc2fa((void *)NULL, cx, NULL, t34, &t35, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        void * t46 = gm_l0;
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
        gm_b16_8 t48 = gm_f_soa_fields_dg06fgc(t47, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l8 = t48;
    }
    {
        gm_b16_8 t49 = gm_l8;
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
        gm_tf5srh4mp4b3i t51 = gm_f_soa_field_index_tas8jh7(t49, t50);
        gm_tf5srh4mp4b3i t52 = t51;
        uint64_t t53;
        if (((((uint8_t *)&t52))[8] != 0)) {
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&t52) + 0));
            t53 = t54;
        } else {
            {
                void * t56 = gm_l0;
                void * t57 = (void *)(((uint8_t *)t56) + 96);
                void * t58 = gm_l0;
                void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
                void * t60 = gm_l1;
                gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t60) + 0));
                gm_t88rcucg1b7uk t62 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t57, t59, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t62;
            }
            {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a3) + 16));
                gm_t6j6smj7qoctu t65;
                void * t66 = gm_l0;
                gm_t88rcucg1b7uk t67 = gm_l9;
                gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t68);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t69);
                gm_tabb710d9mq2e t70;
                gm_f_format_rh16oh6((void *)&t70, (void *)NULL, cx, NULL, &t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t71 = t70;
                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcb4m5qm2daehm, 146, 29);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
                gm_b16_8 t73 = t72;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                gm_b0_1 t75 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t64, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t76 = gm_l0;
            void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
            gm_tl874t2atnlh3 t78 = gm_l4;
            void * t79 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = 0;
            F.ret = t79;
            goto gm_fret;
        }
        gm_l10 = t53;
    }
    {
        void * t82 = gm_l0;
        uint64_t t83 = gm_l10;
        uint64_t t84 = (uintptr_t)t83;
        uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
        if (t84 >= (uintptr_t)(t85)) gm_trap_index(t84, t85, gm_srcb4m5qm2daehm, 149, 34);
        gm_t9qkh0rsooc8d t86 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t84 * 40) + 20));
        gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        uint8_t t88 = gm_f_field_visible_here_20vnoda(t82, t86, GM_CONV(gm_csguankko4lmhd, t87));
        uint8_t t89 = (uint8_t)!t88;
        if (!t89) goto ie81;
        {
            void * t91 = gm_l0;
            gm_tl874t2atnlh3 t92 = gm_l4;
            void * t93 = gm_l0;
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 0));
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
            uint64_t t96 = gm_l10;
            uint64_t t97 = (uintptr_t)t96;
            uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcb4m5qm2daehm, 150, 71);
            gm_t9qkh0rsooc8d t99 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t97 * 40) + 20));
            gm_b16_8 t100 = gm_f_hidden_field_message_b0pdek7((void *)NULL, cx, NULL, t93, t94, t95, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t101 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t91, t92, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        void * t102 = gm_l0;
        void * t103 = gm_l1;
        uint64_t t104 = gm_l10;
        gm_tl874t2atnlh3 t105 = gm_l4;
        void * t106 = gm_f_soa_column_once_ads8l3i((void *)NULL, cx, NULL, t102, t103, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t104, t105);
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
inline __attribute__((aligned(4))) void * gm_f_soa_needs_fields_vtvc2fa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_needs_fields_vtvc2fa F;
    void * gm_l0; /* cx */
    gm_tjpqvfd72o62u gm_l1; /* info */
    gm_b16_8 gm_l2; /* what */
    gm_tl874t2atnlh3 gm_l3; /* span */
    GM_STACK_CHECK(&F, 313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l3;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 0));
        gm_b16_8 t8 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t8);
        gm_tabb710d9mq2e t9;
        gm_f_format_jp1a7ai((void *)&t9, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcb4m5qm2daehm, 54, 20);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = t11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
        gm_b0_1 t14 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
        gm_tl874t2atnlh3 t17 = gm_l3;
        void * t18 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = 0;
        F.ret = t18;
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
inline __attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_soa_of_jufdcj2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tjpqvfd72o62u gm_l3; /* info */
    GM_STACK_CHECK(&F, 390u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_l2;
        gm_t8cc1mtbrhlpt t12;
        gm_f_soa_info_pq4q43l((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        gm_t8cc1mtbrhlpt t13 = t12;
        gm_tjpqvfd72o62u t14;
        if (((((uint8_t *)&t13))[32] != 0)) {
            gm_tjpqvfd72o62u t15 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_t8cc1mtbrhlpt t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t14;
    }
    {
        void * t18 = gm_l1;
        gm_t88rcucg1b7uk t19 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 0), t19);
    }
    {
        gm_tjpqvfd72o62u t20 = gm_l3;
        gm_t8cc1mtbrhlpt t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 36, 0);
        GM_ST(gm_tjpqvfd72o62u, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[32] = 1;
        uint8_t t22 = 0;
        F.ret = t21;
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
inline __attribute__((aligned(4))) void * gm_f_soa_operand_eohpslm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3) {
    struct gm_fr_soa_operand_eohpslm F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* stmts */
    uint8_t gm_l4; /* m */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t88rcucg1b7uk gm_l7; /* pty */
    uint32_t gm_l8; /* p */
    GM_STACK_CHECK(&F, 1013u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint8_t t4 = gm_f_soa_repeatable_rqn3pcn(t3);
        if (!t4) goto ie2;
        {
            void * t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t9;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 39, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t15 = gm_l0;
            void * t16 = gm_l1;
            gm_tqs5p4n2tq19v t17 = gm_f_place_mutability_0tlm4g8(t15, t16);
            gm_tqs5p4n2tq19v t18 = t17;
            if (!((((uint8_t *)&t18))[1] != 0)) goto ie14;
            uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l1;
                gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
                gm_l5 = t22;
            }
            {
                void * t23 = gm_l1;
                gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 208));
                gm_l6 = t24;
            }
            {
                void * t25 = gm_l0;
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                void * t27 = (void *)(((uint8_t *)t26) + 8);
                uint8_t t28 = gm_l4;
                uint8_t t29 = (uint8_t)!t28;
                gm_t88rcucg1b7uk t30 = gm_l5;
                gm_t88rcucg1b7uk t31 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t27, t29, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t31;
            }
            {
                void * t32 = gm_l0;
                gm_t88rcucg1b7uk t33 = gm_l7;
                gm_tl874t2atnlh3 t34 = gm_l6;
                uint32_t t35 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t32, t33, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t35;
            }
            {
                void * t36 = gm_l3;
                uint32_t t37 = gm_l8;
                gm_t88rcucg1b7uk t38 = gm_l7;
                void * t39 = gm_l1;
                gm_t5brlu7fal70j t40;
                memset(&t40, 0, sizeof t40);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)24ULL));
                GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
                gm_tl874t2atnlh3 t41 = gm_l6;
                void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t38, &t40, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttggq7ng4kp3p t43;
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t37);
                GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
                gm_trcmmd12cefb1 t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t44 + 8), t43);
                gm_tftb1odc739t5 t45 = gm_f_push_d6c76ud_2(t36, &t44);
                gm_tftb1odc739t5 t46 = t45;
                if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcb4m5qm2daehm, 45, 13);
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
            }
            {
                gm_t88rcucg1b7uk t48 = gm_l5;
                gm_t88rcucg1b7uk t49 = gm_l7;
                uint32_t t50 = gm_l8;
                gm_t5brlu7fal70j t51;
                memset(&t51, 0, sizeof t51);
                GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t51 + 8), t50);
                gm_tl874t2atnlh3 t52 = gm_l6;
                void * t53 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5brlu7fal70j t54;
                memset(&t54, 0, sizeof t54);
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)18ULL));
                GM_ST(void *, ((uint8_t *)&t54 + 8), t53);
                gm_tl874t2atnlh3 t55 = gm_l6;
                void * t56 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t48, &t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t57 = 0;
                F.ret = t56;
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
        void * t58 = gm_l0;
        void * t59 = gm_l1;
        void * t60 = gm_l3;
        void * t61 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t62 = 0;
        F.ret = t61;
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
__attribute__((aligned(4))) void * gm_f_soa_slice_4dujfr9(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, uint8_t a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_soa_slice_4dujfr9 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* lo */
    void * gm_l4; /* hi */
    uint8_t gm_l5; /* inclusive */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_tl874t2atnlh3 gm_l7; /* base_span */
    uint8_t gm_l8; /* is_const */
    uint8_t gm_l9; /* m */
    void * gm_l11; /* v */
    void * gm_l12; /* lo2 */
    void * gm_l13; /* hi2 */
    gm_t88rcucg1b7uk gm_l14; /* t */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    gm_tesaltf0mhcki gm_l18; /* op */
    void * gm_l19; /* s */
    GM_STACK_CHECK(&F, 1010u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l1;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 208));
        gm_l7 = t3;
    }
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a2) + 24));
        gm_l8 = t4;
    }
    {
        uint8_t t6;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 196, 8);
me7:;
        if (!t6) goto ie5;
        {
            void * t11 = gm_l0;
            void * t12 = gm_l1;
            gm_tqs5p4n2tq19v t13 = gm_f_place_mutability_0tlm4g8(t11, t12);
            gm_tqs5p4n2tq19v t14 = t13;
            uint8_t t15;
            if (((((uint8_t *)&t14))[1] != 0)) {
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
                t15 = t16;
            } else {
                {
                    void * t18 = gm_l0;
                    gm_tl874t2atnlh3 t19 = gm_l7;
                    gm_b16_8 t20;
                    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_ssvnvjq0tn1qr));
                    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(69));
                    gm_b0_1 t21 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t19, t20);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t22 = gm_l0;
                void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
                gm_tl874t2atnlh3 t24 = gm_l6;
                void * t25 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t26 = 0;
                F.ret = t25;
                goto gm_fret;
            }
            gm_l9 = t15;
        }
        {
            uint8_t t27 = gm_l9;
            uint8_t t28 = (uint8_t)!t27;
            gm_l8 = t28;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l1;
        void * t31 = (void *)((uint8_t *)&F.l10);
        void * t32 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t29, t30, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t32;
    }
    {
        void * t33 = gm_l0;
        void * t34 = gm_l3;
        void * t35 = (void *)((uint8_t *)&F.l10);
        void * t36 = gm_f_soa_bound_61nh2oj((void *)NULL, cx, NULL, t33, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t36;
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l4;
        void * t39 = (void *)((uint8_t *)&F.l10);
        void * t40 = gm_f_soa_bound_61nh2oj((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t40;
    }
    {
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
        uint8_t t44 = gm_f_soa_generic_kvk21mq(t43, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t44) goto ie41;
        {
            void * t46 = gm_l0;
            void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
            gm_tf5srh4mp4b3i t49;
            memset(&t49, 0, sizeof t49);
            uint8_t t50 = gm_l8;
            gm_tl874t2atnlh3 t51 = gm_l6;
            gm_t88rcucg1b7uk t52 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t47, t48, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t52;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t53 = (void *)((uint8_t *)&F.l15);
            void * t54 = gm_l11;
            gm_tftb1odc739t5 t55 = gm_f_push_d943vgp_2(t53, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t54)));
            gm_tftb1odc739t5 t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcb4m5qm2daehm, 210, 9);
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        }
        {
            void * t59 = gm_l12;
            void * t60 = t59;
            if (!(GM_LD(void *, ((uint8_t *)&t60)) != 0)) goto ie58;
            void * t61 = GM_LD(void *, ((uint8_t *)&t60));
            gm_l16 = t61;
            {
                void * t63 = (void *)((uint8_t *)&F.l15);
                void * t64 = gm_l16;
                gm_tftb1odc739t5 t65 = gm_f_push_d943vgp_2(t63, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t64)));
                gm_tftb1odc739t5 t66 = t65;
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcb4m5qm2daehm, 211, 23);
                gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            void * t69 = gm_l13;
            void * t70 = t69;
            if (!(GM_LD(void *, ((uint8_t *)&t70)) != 0)) goto ie68;
            void * t71 = GM_LD(void *, ((uint8_t *)&t70));
            gm_l17 = t71;
            {
                void * t73 = (void *)((uint8_t *)&F.l15);
                void * t74 = gm_l17;
                gm_tftb1odc739t5 t75 = gm_f_push_d943vgp_2(t73, &(*(gm_tfpf0s89fpb3r *)((uint8_t *)t74)));
                gm_tftb1odc739t5 t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcb4m5qm2daehm, 212, 23);
                gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
            }
            goto ix68;
ie68:;
ix68:;
        }
        {
            void * t78 = gm_l12;
            void * t79 = t78;
            uint8_t t80;
            if ((GM_LD(void *, ((uint8_t *)&t79)) != 0)) goto mn82;
            t80 = ((uint8_t)0);
            goto me81;
mn82:;
            t80 = ((uint8_t)1);
            goto me81;
mn83:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 213, 35);
me81:;
            void * t84 = gm_l13;
            void * t85 = t84;
            uint8_t t86;
            if ((GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto mn88;
            t86 = ((uint8_t)0);
            goto me87;
mn88:;
            t86 = ((uint8_t)1);
            goto me87;
mn89:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 213, 56);
me87:;
            uint8_t t90 = gm_l5;
            gm_toh0ootodhjue t91;
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), t80);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 1), t86);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 2), t90);
            gm_tesaltf0mhcki t92;
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)3ULL));
            GM_ST(gm_toh0ootodhjue, ((uint8_t *)&t92 + 1), t91);
            gm_l18 = t92;
        }
        {
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_t88rcucg1b7uk t94 = gm_l14;
            gm_tandvbt63uk7n t95;
            memset((uint8_t *)&t95 + 4, 0, 4);
            gm_tesaltf0mhcki t96 = gm_l18;
            GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t95 + 0, t96);
            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t95 + 8, t97);
            gm_t5brlu7fal70j t98;
            memset(&t98, 0, sizeof t98);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t98 + 8), t95);
            gm_tl874t2atnlh3 t99 = gm_l6;
            void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t101 = gm_l6;
            void * t102 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t93, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t103 = 0;
            F.ret = t102;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t104 = gm_l0;
        void * t105 = GM_LD(void *, (((uint8_t *)t104) + 0));
        void * t106 = gm_l11;
        uint8_t t107 = gm_l8;
        void * t108 = gm_l12;
        void * t109 = gm_l13;
        uint8_t t110 = gm_l5;
        gm_tl874t2atnlh3 t111 = gm_l6;
        void * t112 = gm_f_soa_slice_columns_kqq8dfr((void *)NULL, cx, NULL, t105, t106, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t107, t108, t109, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t112;
    }
    {
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        void * t114 = gm_l19;
        gm_tl874t2atnlh3 t115 = gm_l6;
        void * t116 = gm_f_soa_block_ir2b1jv((void *)NULL, cx, NULL, t113, t114, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t117 = 0;
        F.ret = t116;
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
__attribute__((aligned(4))) void gm_f_soa_store_d7ivlu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_store_d7ivlu4 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* index_ast */
    void * gm_l4; /* value */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    uint8_t gm_l7; /* writable */
    uint8_t gm_l8; /* m */
    gm_b16_8 gm_l9; /* what */
    void * gm_l11; /* v */
    void * gm_l12; /* i */
    void * gm_l13; /* x */
    gm_thmgeksj5pdth gm_l14; /* <temporary array> */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_trcmmd12cefb1 gm_l17; /* st */
    GM_STACK_CHECK(&F, 1325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a2) + 24));
        uint8_t t6 = (uint8_t)!t5;
        gm_l7 = t6;
    }
    {
        uint8_t t8;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn10;
        t8 = ((uint8_t)0);
        goto me9;
mn10:;
        t8 = ((uint8_t)1);
        goto me9;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 159, 8);
me9:;
        if (!t8) goto ie7;
        {
            gm_l7 = ((uint8_t)0);
        }
        {
            void * t14 = gm_l0;
            void * t15 = gm_l1;
            gm_tqs5p4n2tq19v t16 = gm_f_place_mutability_0tlm4g8(t14, t15);
            gm_tqs5p4n2tq19v t17 = t16;
            if (!((((uint8_t *)&t17))[1] != 0)) goto ie13;
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t17));
            gm_l8 = t18;
            {
                uint8_t t20 = gm_l8;
                gm_l7 = t20;
            }
            goto ix13;
ie13:;
ix13:;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint8_t t22 = gm_l7;
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie21;
        {
            gm_b16_8 t25;
            uint8_t t27;
            if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn29;
            t27 = ((uint8_t)0);
            goto me28;
mn29:;
            t27 = ((uint8_t)1);
            goto me28;
mn30:;
            gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 164, 20);
me28:;
            if (!t27) goto ie26;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr5or4g6pm2gc));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(25));
            gm_b16_8 t33 = t32;
            t25 = t33;
            goto ix26;
ie26:;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sf24jo61ba8vn));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(22));
            gm_b16_8 t36 = t35;
            t25 = t36;
ix26:;
            gm_l9 = t25;
        }
        {
            void * t37 = gm_l0;
            gm_tl874t2atnlh3 t38 = gm_l5;
            gm_b16_8 t39 = gm_l9;
            gm_tnpifv20irnph t40;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
            gm_tabb710d9mq2e t41;
            gm_f_format_qvsrn69((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcb4m5qm2daehm, 165, 24);
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
            gm_b16_8 t44 = t43;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            gm_b0_1 t46 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t37, t38, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t47 = gm_l0;
        void * t48 = gm_l1;
        void * t49 = (void *)((uint8_t *)&F.l10);
        void * t50 = gm_f_soa_operand_eohpslm((void *)NULL, cx, NULL, t47, t48, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t50;
    }
    {
        void * t51 = gm_l0;
        void * t52 = gm_l3;
        void * t53 = gm_f_soa_index_value_rabl6gs((void *)NULL, cx, NULL, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t53;
    }
    {
        void * t54 = gm_l0;
        void * t55 = gm_l12;
        void * t56 = (void *)((uint8_t *)&F.l10);
        void * t57 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t54, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t57;
    }
    {
        void * t58 = gm_l0;
        void * t59 = gm_l4;
        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_tkn42u7na3t81 t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
        ((uint8_t *)&t61)[4] = 1;
        void * t62 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t58, t59, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t62;
    }
    {
        void * t63 = gm_l0;
        void * t64 = gm_l13;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        void * t66 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t63, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t66;
    }
    {
        void * t67 = gm_l0;
        void * t68 = gm_l13;
        void * t69 = (void *)((uint8_t *)&F.l10);
        void * t70 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t67, t68, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t70;
    }
    {
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        uint8_t t74 = gm_f_soa_generic_kvk21mq(t73, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        if (!t74) goto ie71;
        {
        }
        {
            void * t76 = (void *)((uint8_t *)&F.l10);
            void * t77 = gm_l6;
            gm_t88rcucg1b7uk t78 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tandvbt63uk7n t79;
            memset((uint8_t *)&t79 + 4, 0, 4);
            gm_tesaltf0mhcki t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t79 + 0, t80);
            {
                void * t82 = gm_l11;
                void * t83 = gm_l12;
                void * t84 = gm_l13;
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l14)) + 0, t82);
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l14)) + 8, t83);
                GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l14)) + 16, t84);
            }
            void * t85 = ((uint8_t *)&gm_l14);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcb4m5qm2daehm, 175, 83);
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t85 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_b16_8 t87 = t86;
            gm_b16_8 t88 = gm_f_soa_args_mnec87i((void *)NULL, cx, NULL, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t79 + 8, t88);
            gm_t5brlu7fal70j t89;
            memset(&t89, 0, sizeof t89);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t89 + 8), t79);
            gm_tl874t2atnlh3 t90 = gm_l5;
            void * t91 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t78, &t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_trcmmd12cefb1 t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t92 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t92 + 8), t91);
            gm_tftb1odc739t5 t93 = gm_f_push_d6c76ud_2(t76, &t92);
            gm_tftb1odc739t5 t94 = t93;
            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcb4m5qm2daehm, 175, 9);
            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
        }
        goto ix71;
ie71:;
        {
            {
                void * t98 = gm_l0;
                void * t99 = GM_LD(void *, (((uint8_t *)t98) + 0));
                void * t100 = gm_l11;
                void * t101 = gm_l12;
                void * t102 = gm_l13;
                gm_tl874t2atnlh3 t103 = gm_l5;
                gm_b16_8 t104 = gm_f_soa_scatter_h5ruqu8((void *)NULL, cx, NULL, t99, t100, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t101, t102, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t104;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t106 = gm_l16;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t108 = (t106 < t107);
                    if (!t108) goto lb105;
                    {
                        uint64_t t111 = gm_l16;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_trcmmd12cefb1 t116 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t112 * 32));
                        gm_l17 = t116;
                        {
                            void * t118 = (void *)((uint8_t *)&F.l10);
                            gm_tftb1odc739t5 t119 = gm_f_push_d6c76ud_2(t118, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l17)));
                            gm_tftb1odc739t5 t120 = t119;
                            if (((((uint8_t *)&t120))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t120) + 1, gm_srcb4m5qm2daehm, 177, 59);
                            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 1));
                        }
                        goto me114;
mn115:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb4m5qm2daehm, 177, 9);
me114:;
                    }
lc105:;
                    {
                        uint64_t t123 = gm_l16;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l16 = t126;
                    }
                }
lb105:;
            }
        }
ix71:;
    }
    {
        void * t127 = gm_l0;
        void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_tl874t2atnlh3 t130 = gm_l5;
        void * t131 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t128, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t132;
        GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t132 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t132 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t132 + 8), t131);
        uint8_t t133 = 0;
        F.ret = t132;
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
