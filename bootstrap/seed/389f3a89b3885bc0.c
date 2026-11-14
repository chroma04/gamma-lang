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
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t p1[7]; uint64_t f1; } gm_tphtgnn9vgkcv;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint64_t f0; uint32_t f1; uint8_t p2[4]; gm_tf5srh4mp4b3i f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; uint64_t f5; gm_toh0ootodhjue f6; uint8_t pe[5]; } gm_tjhb0nuou5gid;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tphtgnn9vgkcv) == 16 && _Alignof(gm_tphtgnn9vgkcv) == 8 && offsetof(gm_tphtgnn9vgkcv, f0) == 0 && offsetof(gm_tphtgnn9vgkcv, f1) == 8, "layout of `(found: bool, at: usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjhb0nuou5gid) == 64 && _Alignof(gm_tjhb0nuou5gid) == 8 && offsetof(gm_tjhb0nuou5gid, f0) == 0 && offsetof(gm_tjhb0nuou5gid, f1) == 8 && offsetof(gm_tjhb0nuou5gid, f2) == 16 && offsetof(gm_tjhb0nuou5gid, f3) == 32 && offsetof(gm_tjhb0nuou5gid, f4) == 40 && offsetof(gm_tjhb0nuou5gid, f5) == 48 && offsetof(gm_tjhb0nuou5gid, f6) == 56, "layout of `Snapshot`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; void * f5; uint64_t f6; uint32_t f7; uint8_t p8[4]; gm_trqfvmjtt4sij f8; gm_toh0ootodhjue f9; uint8_t p10[1]; uint32_t f10; uint64_t f11; gm_tf5srh4mp4b3i f12; uint64_t f13; uint64_t f14; gm_trqfvmjtt4sij f15; } gm_tnht3ia4la85o;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; } gm_t41cj6gvba3r8;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tnht3ia4la85o) == 216 && _Alignof(gm_tnht3ia4la85o) == 8 && offsetof(gm_tnht3ia4la85o, f0) == 0 && offsetof(gm_tnht3ia4la85o, f1) == 8 && offsetof(gm_tnht3ia4la85o, f2) == 24 && offsetof(gm_tnht3ia4la85o, f3) == 40 && offsetof(gm_tnht3ia4la85o, f4) == 56 && offsetof(gm_tnht3ia4la85o, f5) == 64 && offsetof(gm_tnht3ia4la85o, f6) == 72 && offsetof(gm_tnht3ia4la85o, f7) == 80 && offsetof(gm_tnht3ia4la85o, f8) == 88 && offsetof(gm_tnht3ia4la85o, f9) == 128 && offsetof(gm_tnht3ia4la85o, f10) == 132 && offsetof(gm_tnht3ia4la85o, f11) == 136 && offsetof(gm_tnht3ia4la85o, f12) == 144 && offsetof(gm_tnht3ia4la85o, f13) == 160 && offsetof(gm_tnht3ia4la85o, f14) == 168 && offsetof(gm_tnht3ia4la85o, f15) == 176, "layout of `Parser`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t41cj6gvba3r8) == 96 && _Alignof(gm_t41cj6gvba3r8) == 8 && offsetof(gm_t41cj6gvba3r8, f0) == 0 && offsetof(gm_t41cj6gvba3r8, f1) == 32 && offsetof(gm_t41cj6gvba3r8, f2) == 64, "layout of `(TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_tm4pc0emhrv1g f0; uint8_t f1; uint8_t pe[7]; } gm_tvr28kgomlp0c;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tvpnktv5fo8nl v1; } f1; } gm_ts6irudtvl17i;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_tvr28kgomlp0c) == 56 && _Alignof(gm_tvr28kgomlp0c) == 8 && offsetof(gm_tvr28kgomlp0c, f0) == 0 && offsetof(gm_tvr28kgomlp0c, f1) == 48, "layout of `?Token`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
_Static_assert(sizeof(gm_ts6irudtvl17i) == 168 && _Alignof(gm_ts6irudtvl17i) == 8 && offsetof(gm_ts6irudtvl17i, f0) == 0 && offsetof(gm_ts6irudtvl17i, f1) == 8, "layout of `Abort!Item`");
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_eof_ebcpkv0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_newline_94n732e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_reported_kw_name_it872te {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_word_18dnh5f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binds_ahead_e3p74o2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_bump_q2i40re {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* t */
    gm_tm4pc0emhrv1g ret;
};
struct gm_fr_bump_reported_kw_name_gs5eq1i {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* t */
    gm_t08olq97rscn3 ret;
};
struct gm_fr_eat_kw_tvv0v3e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q2uoqf9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enter_5bf793b {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_error_expr_dhdi86k {
    gm_frame h;
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_expect_ijgfrs1 {
    gm_frame h;
    gm_t0u0j6voh3loe ret;
};
struct gm_fr_expect_kw_7kbmp0i {
    gm_frame h;
    gm_t0u0j6voh3loe ret;
};
struct gm_fr_expect_terminator_f7kp9sk {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_b0_1 ret;
};
struct gm_fr_expected_5famfb9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fail_diag_u0q4kp3 {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_keyword_as_name_ipm23p9 {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_t08olq97rscn3 ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_2uu83vj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh {
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
struct gm_fr_nth_is_kw_4i9ruj5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_nth_is_name_oqhoobd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_decl_ident_31rb1id {
    gm_frame h;
    gm_t4jn8fl3lmk5k ret;
};
struct gm_fr_parse_expr_in_45seo3h {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_file_70g7vok {
    gm_frame h;
    gm_tnht3ia4la85o l5; /* p */
    gm_tm4pc0emhrv1g l7; /* t */
    gm_t08olq97rscn3 ret;
};
struct gm_fr_parse_file_items_9rlm6mo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* items */
    gm_tvpnktv5fo8nl l3; /* item */
    gm_b16_8 ret;
};
struct gm_fr_parse_ident_n2c6b75 {
    gm_frame h;
    gm_t4jn8fl3lmk5k ret;
};
struct gm_fr_parse_name_figjvbe {
    gm_frame h;
    gm_t4jn8fl3lmk5k ret;
};
struct gm_fr_parse_type_in_bvi7uav {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_push_1e500nc {
    gm_frame h;
    gm_tvpnktv5fo8nl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1fv0q6m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_recover_to_stmt_end_freq9f3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tbufaiv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_restore_12su1qd {
    gm_frame h;
    gm_tjhb0nuou5gid l1; /* s */
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_skip_newlines_0ap7sq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_snapshot_g4eb69q {
    gm_frame h;
    gm_tjhb0nuou5gid ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
struct gm_fr_truncate_vir0vu7 {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_eof_ebcpkv0(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tphtgnn9vgkcv gm_f_binary_search_qri8aft(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_binds_ahead_e3p74o2(void * a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tm4pc0emhrv1g gm_f_bump_q2i40re_c(void * a0);
__attribute__((aligned(4))) void gm_f_bump_reported_kw_name_gs5eq1i(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t08olq97rscn3 gm_f_bump_reported_kw_name_gs5eq1i_c(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_rhl0asi(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q2uoqf9(void);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_error_expr_dhdi86k_c(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_kw_7kbmp0i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expect_terminator_f7kp9sk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3_c(void * a0, gm_t4gut2c9fj1kt a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_06gsqc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_40j8dpf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_681ria4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_884c6p6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ltp901v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ps8id4k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_keyword_as_name_ipm23p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_k239kpg_c(void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_2uu83vj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_mk_expr_53qj7nd_c(void * a0, gm_t2gtbh39ptekd a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_nth_c0r2nvs_c(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_kw_4i9ruj5(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_decl_ident_31rb1id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_in_45seo3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_parse_file_items_9rlm6mo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_item_sbv139e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_in_bvi7uav(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1fv0q6m(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_recover_to_stmt_end_freq9f3(void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tbufaiv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_report_diag_big1nsf_c(void * a0, gm_t4gut2c9fj1kt a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_restore_12su1qd(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_restore_12su1qd_c(void * a0, gm_tjhb0nuou5gid a1);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0);
__attribute__((aligned(4))) void gm_f_snapshot_g4eb69q(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tjhb0nuou5gid gm_f_snapshot_g4eb69q_c(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1);
extern uint8_t gm_ctk5347uqth[4] __attribute__((aligned(4)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern const uint8_t gm_sdoflo4g2h6ev[14];
extern const uint8_t gm_senjrqtakqrde[16];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_struubjgj1nj2[15];
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
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
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
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 58u);
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
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t9qkh0rsooc8d t3 = gm_l1;
    uint8_t t4 = (((GM_LD(uint8_t, ((uint8_t *)&t2) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t2) + 8), ((uint8_t *)&t3))));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_eof_ebcpkv0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_eof_ebcpkv0 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&t2) + 0) == ((uint8_t)10ULL)));
    F.ret = t3;
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
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t9qkh0rsooc8d t3 = gm_l1;
    uint8_t t4 = (((GM_LD(uint8_t, ((uint8_t *)&t2) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t2) + 8), ((uint8_t *)&t3))));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_newline_94n732e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&t2) + 0) == ((uint8_t)9ULL)));
    F.ret = t3;
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
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    uint8_t t3;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    if (t6 != ((uint8_t)2ULL)) goto mn5;
    gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t2) + 8));
    gm_l1 = t7;
    gm_t9qkh0rsooc8d t8 = gm_l1;
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 176) + 0));
    uint8_t t11 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eq2ceic3f8rp(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 1, ((uint8_t *)&t8))) { t11 = 1; break; }
    uint8_t t12 = (uint8_t)(t11);
    t3 = t12;
    goto me4;
mn5:;
    t3 = ((uint8_t)0);
    goto me4;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 199, 56);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_word_18dnh5f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_nth_c0r2nvs((void *)&t2, t1, ((uint64_t)0ULL));
    uint8_t t3;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    if (t6 != ((uint8_t)0ULL)) goto mn5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 8));
    gm_l2 = t7;
    gm_b16_8 t8 = gm_l2;
    gm_b16_8 t9 = gm_l1;
    uint8_t t10 = (gm_eq0iqiubfm7e(((uint8_t *)&t8), ((uint8_t *)&t9)));
    t3 = t10;
    goto me4;
mn5:;
    t3 = ((uint8_t)0);
    goto me4;
mn11:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 170, 58);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t1))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t2 = GM_LD(void *, (((uint8_t *)&t1) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_kw_tvv0v3e F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 160)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q2uoqf9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q2uoqf9 F;
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    memmove(gm_out, (((uint8_t *)t2) + 0), 32);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_list_2uu83vj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2uu83vj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_q2uoqf9();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lpi80k4();
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
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t1));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&t2) + 4));
    F.ret = t3;
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
    GM_STACK_CHECK_SHALLOW(&F, 120u);
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
    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcuiab49hlgv7g, 126, 62);
    memmove(gm_out, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)) + t16 * 48) + 0), 32);
    goto gm_fdone;
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
    GM_STACK_CHECK_SHALLOW(&F, 69u);
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
    gm_t9qkh0rsooc8d t4 = gm_l2;
    uint8_t t5 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t3) + 8), ((uint8_t *)&t4))));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_kw_4i9ruj5(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_kw_4i9ruj5 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 69u);
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
    gm_t9qkh0rsooc8d t4 = gm_l2;
    uint8_t t5 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t3) + 8), ((uint8_t *)&t4))));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_lpi80k4();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_o86m6d7(t10, ((uint64_t)1ULL), t12);
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
        gm_tvpnktv5fo8nl t21 = F.l1;
        GM_ST(gm_tvpnktv5fo8nl, ((uint8_t *)t20 + t18 * 160), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 160)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1fv0q6m(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1fv0q6m F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_q2uoqf9();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_tbufaiv(t10, ((uint64_t)1ULL), t12);
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
        gm_t9qkh0rsooc8d t21 = gm_l1;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)t20 + t18 * 1), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
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
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)160ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tbufaiv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tbufaiv F;
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
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_newlines_0ap7sq2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint8_t t4 = gm_f_at_newline_94n732e(t3);
            if (!t4) goto lb2;
            {
                {
                    void * t8 = gm_l0;
                    gm_tm4pc0emhrv1g t9;
                    gm_f_bump_q2i40re((void *)&t9, t8);
                }
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
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
    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcuiab49hlgv7g, 122, 56);
    void * t14 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)) + t12 * 48);
    F.ret = t14;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_vir0vu7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_vir0vu7 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
extern __attribute__((aligned(4))) uint8_t gm_f_at_eof_ebcpkv0(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
extern __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_binds_ahead_e3p74o2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binds_ahead_e3p74o2 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    uint64_t gm_l2; /* i */
    gm_t9qkh0rsooc8d gm_l3; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 72));
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
            uint8_t t8 = (t5 < t7);
            if (!t8) goto lb4;
            {
                uint64_t t11 = gm_l2;
                uint64_t t12 = (uintptr_t)t11;
                void * t13 = gm_l0;
                uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)t13) + 8) + 8);
                if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcuiab49hlgv7g, 386, 15);
                uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 8)) + t12 * 48) + 0) + 0));
                switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma18; default: goto ma19; }
ma16:;
                uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 8)) + t12 * 48) + 0) + 0));
                if (t23 != ((uint8_t)8ULL)) goto mn22;
                gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 8)) + t12 * 48) + 0) + 8));
                gm_l3 = t24;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma30; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma30; case ((uint8_t)27ULL): goto ma30; case ((uint8_t)28ULL): goto ma30; case ((uint8_t)29ULL): goto ma30; case ((uint8_t)30ULL): goto ma30; case ((uint8_t)31ULL): goto ma30; case ((uint8_t)32ULL): goto ma30; case ((uint8_t)33ULL): goto ma30; case ((uint8_t)34ULL): goto ma30; case ((uint8_t)35ULL): goto ma30; case ((uint8_t)36ULL): goto ma30; case ((uint8_t)37ULL): goto ma30; case ((uint8_t)38ULL): goto ma30; case ((uint8_t)39ULL): goto ma30; case ((uint8_t)40ULL): goto ma30; case ((uint8_t)41ULL): goto ma30; case ((uint8_t)42ULL): goto ma30; case ((uint8_t)43ULL): goto ma30; case ((uint8_t)44ULL): goto ma30; case ((uint8_t)45ULL): goto ma30; case ((uint8_t)46ULL): goto ma30; case ((uint8_t)47ULL): goto ma30; case ((uint8_t)48ULL): goto ma30; case ((uint8_t)49ULL): goto ma30; case ((uint8_t)50ULL): goto ma30; case ((uint8_t)51ULL): goto ma30; case ((uint8_t)52ULL): goto ma30; case ((uint8_t)53ULL): goto ma30; case ((uint8_t)54ULL): goto ma30; case ((uint8_t)55ULL): goto ma29; case ((uint8_t)56ULL): goto ma30; case ((uint8_t)57ULL): goto ma30; case ((uint8_t)58ULL): goto ma30; case ((uint8_t)59ULL): goto ma30; case ((uint8_t)60ULL): goto ma30; case ((uint8_t)61ULL): goto ma26; case ((uint8_t)62ULL): goto ma27; case ((uint8_t)63ULL): goto ma26; case ((uint8_t)64ULL): goto ma27; case ((uint8_t)65ULL): goto ma28; case ((uint8_t)66ULL): goto ma28; case ((uint8_t)67ULL): goto ma30; case ((uint8_t)68ULL): goto ma30; default: goto ma30; }
ma26:;
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t36 != ((uint8_t)61ULL)) goto pa35;
                goto po34;
pa35:;
                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t38 != ((uint8_t)63ULL)) goto pa37;
                goto po34;
pa37:;
                goto mn33;
po34:;
                {
                    uint64_t t40 = gm_l1;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    if (o42) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 388, 41);
                    uint64_t t43 = r41;
                    gm_l1 = t43;
                }
                goto me25;
mn33:;
ma27:;
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t47 != ((uint8_t)62ULL)) goto pa46;
                goto po45;
pa46:;
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t49 != ((uint8_t)64ULL)) goto pa48;
                goto po45;
pa48:;
                goto mn44;
po45:;
                {
                    uint64_t t52 = gm_l1;
                    uint8_t t53 = ((t52 == ((uint64_t)0ULL)));
                    if (!t53) goto ie51;
                    {
                        uint8_t t55 = 0;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    goto ix51;
ie51:;
ix51:;
                }
                {
                    uint64_t t56 = gm_l1;
                    uint64_t r57; bool o58 = __builtin_sub_overflow(t56, ((uint64_t)1ULL), &r57);
                    if (o58) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 391, 21);
                    uint64_t t59 = r57;
                    gm_l1 = t59;
                }
                goto me25;
mn44:;
ma28:;
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t63 != ((uint8_t)65ULL)) goto pa62;
                goto po61;
pa62:;
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t65 != ((uint8_t)66ULL)) goto pa64;
                goto po61;
pa64:;
                goto mn60;
po61:;
                uint64_t t67 = gm_l1;
                uint8_t t68 = ((t67 == ((uint64_t)0ULL)));
                if (!t68) goto ie66;
                {
                    uint8_t t70 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix66;
ie66:;
ix66:;
                goto me25;
mn60:;
ma29:;
                uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t72 != ((uint8_t)55ULL)) goto mn71;
                uint64_t t74 = gm_l1;
                uint8_t t75 = ((t74 == ((uint64_t)0ULL)));
                if (!t75) goto ie73;
                {
                    uint8_t t77 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix73;
ie73:;
ix73:;
                goto me25;
mn71:;
ma30:;
                goto me25;
mn78:;
ma31:;
                gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 387, 26);
me25:;
                goto me15;
mn22:;
ma17:;
                uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 8)) + t12 * 48) + 0) + 0));
                if (t81 != ((uint8_t)9ULL)) goto mn80;
                uint64_t t83 = gm_l1;
                uint8_t t84 = ((t83 == ((uint64_t)0ULL)));
                if (!t84) goto ie82;
                {
                    uint8_t t86 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix82;
ie82:;
ix82:;
                goto me15;
mn80:;
ma18:;
                uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 8)) + t12 * 48) + 0) + 0));
                if (t88 != ((uint8_t)10ULL)) goto mn87;
                uint8_t t89 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me15;
mn87:;
ma19:;
                goto me15;
mn90:;
ma20:;
                gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 386, 9);
me15:;
            }
            {
                uint64_t t92 = gm_l2;
                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                if (o94) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 401, 9);
                uint64_t t95 = r93;
                gm_l2 = t95;
            }
lc4:;
        }
lb4:;
    }
    {
        uint8_t t96 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bump_q2i40re F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_f_tok_ai6sc12(t2);
        gm_tm4pc0emhrv1g t4 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)t3));
        F.l1 = t4;
    }
    {
        uint8_t t6 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)10ULL)));
        if (!t6) goto ie5;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 72));
            uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
            if (o12) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 136, 9);
            uint64_t t13 = r11;
            GM_ST(uint64_t, (((uint8_t *)t8) + 72), t13);
        }
        {
            uint8_t t15 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0) + 0) == ((uint8_t)9ULL)));
            if (!t15) goto ie14;
            {
                void * t17 = gm_l0;
                uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 32) + 8));
                GM_ST(uint32_t, (((uint8_t *)t17) + 80), t18);
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tm4pc0emhrv1g gm_f_bump_q2i40re_c(void * a0) { gm_tm4pc0emhrv1g gm_r; gm_f_bump_q2i40re(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_bump_reported_kw_name_gs5eq1i(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bump_reported_kw_name_gs5eq1i F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
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
        if (t4 != ((uint8_t)2ULL)) goto ie3;
        gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l1) + 0) + 8));
        gm_l2 = t5;
        {
            memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
            gm_t9qkh0rsooc8d t7 = gm_l2;
            gm_b16_8 t8 = gm_f_keyword_text_149oqn4(t7);
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 32));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t8);
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t9);
            goto gm_fdone;
        }
        goto ix3;
ie3:;
        {
            memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t11);
            gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 32));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t12);
            goto gm_fdone;
        }
ix3:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t08olq97rscn3 gm_f_bump_reported_kw_name_gs5eq1i_c(void * a0) { gm_t08olq97rscn3 gm_r; gm_f_bump_reported_kw_name_gs5eq1i(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_eat_kw_tvv0v3e(void * a0, gm_t9qkh0rsooc8d a1);
extern __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_enter_5bf793b F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
        uint32_t t5 = GM_LD(uint32_t, gm_ctk5347uqth);
        uint8_t t6 = (t4 >= t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t9));
            uint32_t t11 = GM_LD(uint32_t, gm_ctk5347uqth);
            gm_t88rcucg1b7uk t12;
            GM_ST(uint32_t, ((uint8_t *)&t12 + 0), t11);
            gm_tabb710d9mq2e t13;
            gm_f_format_40j8dpf((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcuiab49hlgv7g, 427, 34);
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_b0_1 t16 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t8, t10, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t17;
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t17 + 1), t16);
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l0;
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t20) + 132));
        uint32_t r22; bool o23 = __builtin_add_overflow(t21, ((uint32_t)1ULL), &r22);
        if (o23) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 429, 5);
        uint32_t t24 = r22;
        GM_ST(uint32_t, (((uint8_t *)t19) + 132), t24);
    }
    {
        gm_b0_1 t25;
        gm_t9qkh0rsooc8d t26;
        ((uint8_t *)&t26)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t26 + 1), t25);
        uint8_t t27 = ((((uint8_t *)&t26))[0] != 0);
        F.ret = t26;
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
extern __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_error_expr_dhdi86k_c(void * a0, gm_cseahvg8v7p0vs a1) { gm_tc3oht4ksth9d gm_r; gm_f_error_expr_dhdi86k(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2) {
    struct gm_fr_expect_ijgfrs1 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    gm_b16_8 gm_l2; /* ctx */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tm4pc0emhrv1g t8;
            gm_f_bump_q2i40re((void *)&t8, t7);
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 32));
            gm_t0u0j6voh3loe t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 4), t9);
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        {
            void * t16 = gm_l0;
            gm_t9qkh0rsooc8d t17 = gm_l1;
            gm_b16_8 t18 = gm_f_punct_text_ov4r40u(t17);
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tabb710d9mq2e t20;
            gm_f_format_h4jcodi((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcuiab49hlgv7g, 286, 42);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_b0_1 t23 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t16, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 8, 0);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t24 + 4), t23);
            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t26 = gm_l0;
        gm_t6j6smj7qoctu t27;
        gm_t9qkh0rsooc8d t28 = gm_l1;
        gm_b16_8 t29 = gm_f_punct_text_ov4r40u(t28);
        GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t29);
        gm_b16_8 t30 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t30);
        gm_tabb710d9mq2e t31;
        gm_f_format_884c6p6((void *)&t31, (void *)NULL, cx, NULL, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcuiab49hlgv7g, 287, 24);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b0_1 t34 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t26, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0u0j6voh3loe t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
        ((uint8_t *)&t35)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t35 + 4), t34);
        uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
        F.ret = t35;
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
inline __attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_kw_7kbmp0i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2) {
    struct gm_fr_expect_kw_7kbmp0i F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    gm_b16_8 gm_l2; /* ctx */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tm4pc0emhrv1g t8;
            gm_f_bump_q2i40re((void *)&t8, t7);
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 32));
            gm_t0u0j6voh3loe t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t10 + 4), t9);
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        {
            void * t16 = gm_l0;
            gm_t9qkh0rsooc8d t17 = gm_l1;
            gm_b16_8 t18 = gm_f_keyword_text_149oqn4(t17);
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tabb710d9mq2e t20;
            gm_f_format_h4jcodi((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcuiab49hlgv7g, 292, 42);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_b0_1 t23 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t16, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t24 + 8, 0);
            ((uint8_t *)&t24)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t24 + 4), t23);
            uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
            F.ret = t24;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t26 = gm_l0;
        gm_t6j6smj7qoctu t27;
        gm_t9qkh0rsooc8d t28 = gm_l1;
        gm_b16_8 t29 = gm_f_keyword_text_149oqn4(t28);
        GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t29);
        gm_b16_8 t30 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t30);
        gm_tabb710d9mq2e t31;
        gm_f_format_884c6p6((void *)&t31, (void *)NULL, cx, NULL, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcuiab49hlgv7g, 293, 24);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b0_1 t34 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t26, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0u0j6voh3loe t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 8, 0);
        ((uint8_t *)&t35)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t35 + 4), t34);
        uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
        F.ret = t35;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_expect_terminator_f7kp9sk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_expect_terminator_f7kp9sk F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    gm_tl874t2atnlh3 gm_l2; /* at */
    uint32_t gm_l4; /* next */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* o */
    uint8_t gm_l7; /* at_line_end */
    GM_STACK_CHECK(&F, 984u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t3fcbuehh3bs2 t4;
        gm_f_kind_k239kpg((void *)&t4, t3);
        gm_t41cj6gvba3r8 t5;
        gm_t3fcbuehh3bs2 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t5 + 0, t6);
        gm_t3fcbuehh3bs2 t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t7 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t5 + 32, t7);
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)66ULL));
        gm_t3fcbuehh3bs2 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t8);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t5 + 64, t9);
        uint8_t t10 = 0;
        if (gm_eqt7ftn1841u(((uint8_t *)&t5) + 0, ((uint8_t *)&t4))) t10 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t5) + 32, ((uint8_t *)&t4))) t10 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t5) + 64, ((uint8_t *)&t4))) t10 = 1;
        uint8_t t11 = (uint8_t)(t10);
        if (!t11) goto ie2;
        {
            gm_b0_1 t13;
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
        gm_tl874t2atnlh3 t16 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t15));
        gm_l2 = t16;
    }
    {
        gm_tl874t2atnlh3 t17 = gm_l2;
        gm_t6j6smj7qoctu t18;
        gm_b16_8 t19 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        void * t20 = gm_l0;
        gm_t3fcbuehh3bs2 t21;
        gm_f_kind_k239kpg((void *)&t21, t20);
        gm_b16_8 t22 = gm_f_describe_rhl0asi((void *)NULL, cx, NULL, &t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t22);
        gm_tabb710d9mq2e t23;
        gm_f_format_06gsqc9((void *)&t23, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcuiab49hlgv7g, 320, 25);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_f_error_s6hn871((void *)&F.l3, (void *)NULL, cx, NULL, t17, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t27 = gm_l0;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)57ULL));
        uint8_t t29 = gm_f_at_54906rm(t27, t28);
        if (!t29) goto ie26;
        {
            void * t31 = gm_l0;
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 8));
            gm_b16_8 t33 = t32;
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 72));
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 323, 25);
            uint64_t t38 = r36;
            void * t39 = gm_l0;
            uint64_t t40 = GM_LD(uintptr_t, (((uint8_t *)t39) + 8) + 8);
            uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
            if (o42) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 323, 40);
            uint64_t t43 = r41;
            uint64_t t44 = (t38 < t43 ? t38 : t43);
            uint64_t t45 = (uintptr_t)t44;
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&t33) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcuiab49hlgv7g, 323, 17);
            uint32_t t47 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t33)) + t45 * 48) + 32) + 4));
            gm_l4 = t47;
        }
        {
            void * t48 = gm_l0;
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 40));
            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_tphtgnn9vgkcv t51 = gm_f_binary_search_qri8aft(t49, t50);
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&t51) + 8));
            gm_l5 = t52;
        }
        {
            uint8_t t53;
            void * t55 = gm_l0;
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t55) + 40));
            void * t57 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t56)));
            uint64_t t58 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t56) + 8);
            uint64_t t59 = gm_l5;
            uint64_t t60 = (uintptr_t)t59;
            gm_t9805qtonbsag t61;
            if (t60 < t58) {
            uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)t57 + t60 * 4));
            gm_t9805qtonbsag t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t63), t62);
            ((uint8_t *)&t63)[4] = 1;
                t61 = t63;
            } else {
            gm_t9805qtonbsag t64;
            memset(&t64, 0, sizeof t64);
                t61 = t64;
            }
            if (!((((uint8_t *)&t61))[4] != 0)) goto ie54;
            uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&t61));
            gm_l6 = t65;
            uint32_t t67 = gm_l6;
            uint32_t t68 = gm_l4;
            uint8_t t69 = (t67 < t68);
            uint8_t t70 = t69;
            t53 = t70;
            goto ix54;
ie54:;
            uint8_t t72 = ((uint8_t)0);
            t53 = t72;
ix54:;
            gm_l7 = t53;
        }
        {
            uint8_t t74 = 0;
            uint8_t t75 = gm_l7;
            if (!t75) {
                void * t76 = gm_l0;
                gm_t3fcbuehh3bs2 t77;
                gm_f_nth_c0r2nvs((void *)&t77, t76, ((uint64_t)1ULL));
                gm_t23tohc57ebhi t78;
                gm_t3fcbuehh3bs2 t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t79 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)10ULL));
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t78 + 0, t79);
                gm_t9qkh0rsooc8d t80;
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)66ULL));
                gm_t3fcbuehh3bs2 t81;
                GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t81 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t81 + 8), t80);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t78 + 32, t81);
                uint8_t t82 = 0;
                if (gm_eqt7ftn1841u(((uint8_t *)&t78) + 0, ((uint8_t *)&t77))) t82 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t78) + 32, ((uint8_t *)&t77))) t82 = 1;
                uint8_t t83 = (uint8_t)(t82);
                t74 = t83;
            } else t74 = 1;
            if (!t74) goto ie73;
            {
                void * t85 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(gm_struubjgj1nj2));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(14));
                gm_tl874t2atnlh3 t87 = gm_l2;
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
                gm_b0_1 t89 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t85, t86, t87, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix73;
ie73:;
ix73:;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        {
            void * t91 = gm_l0;
            gm_b0_1 t92 = gm_f_fail_diag_u0q4kp3(t91, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l3)));
        }
    }
    {
        void * t93 = gm_l0;
        gm_f_recover_to_stmt_end_freq9f3(t93);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_expected_5famfb9 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 213u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        gm_tl874t2atnlh3 t4 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t3));
        gm_t6j6smj7qoctu t5;
        gm_b16_8 t6 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 0, t6);
        void * t7 = gm_l0;
        gm_t3fcbuehh3bs2 t8;
        gm_f_kind_k239kpg((void *)&t8, t7);
        gm_b16_8 t9 = gm_f_describe_rhl0asi((void *)NULL, cx, NULL, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 16, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_681ria4((void *)&t10, (void *)NULL, cx, NULL, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcuiab49hlgv7g, 280, 29);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b0_1 t13 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t2, t4, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fail_diag_u0q4kp3 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t3 = gm_l0;
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 144));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 72));
        gm_tf5srh4mp4b3i t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t7), t6);
        ((uint8_t *)&t7)[8] = 1;
        uint8_t t8 = (!gm_eq7mri15hk78(((uint8_t *)&t4), ((uint8_t *)&t7)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 72));
            gm_tf5srh4mp4b3i t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t13), t12);
            ((uint8_t *)&t13)[8] = 1;
            GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)t10) + 144), t13);
        }
        {
            void * t14 = gm_l0;
            gm_f_report_diag_big1nsf(t14, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
        }
        goto ix2;
ie2:;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 136));
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            if (o20) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 273, 9);
            uint64_t t21 = r19;
            GM_ST(uint64_t, (((uint8_t *)t16) + 136), t21);
        }
ix2:;
    }
    {
        gm_b0_1 t22;
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3_c(void * a0, gm_t4gut2c9fj1kt a1) { return gm_f_fail_diag_u0q4kp3(a0, &a1); }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_keyword_as_name_ipm23p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_keyword_as_name_ipm23p9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* text */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
        gm_l2 = t3;
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
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 136));
        uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 189, 5);
        uint64_t t12 = r10;
        GM_ST(uint64_t, (((uint8_t *)t7) + 136), t12);
    }
    {
        gm_t9qkh0rsooc8d t14 = gm_l1;
        void * t15 = gm_l0;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((((uint8_t *)t15) + 176) + 0));
        uint8_t t17 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)); i++) if (gm_eq2ceic3f8rp(((uint8_t *)GM_LD(void *, ((uint8_t *)&t16))) + i * 1, ((uint8_t *)&t14))) { t17 = 1; break; }
        uint8_t t18 = (uint8_t)(!t17);
        if (!t18) goto ie13;
        {
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 176);
            gm_t9qkh0rsooc8d t22 = gm_l1;
            gm_tftb1odc739t5 t23 = gm_f_push_1fv0q6m(t21, t22);
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcuiab49hlgv7g, 190, 36);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_t9qkh0rsooc8d t25 = gm_l1;
        gm_b16_8 t26 = gm_f_keyword_text_149oqn4(t25);
        gm_l3 = t26;
    }
    {
        gm_tl874t2atnlh3 t27 = gm_l2;
        gm_b16_8 t28 = gm_l3;
        gm_tnpifv20irnph t29;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
        gm_tabb710d9mq2e t30;
        gm_f_format_ps8id4k((void *)&t30, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcuiab49hlgv7g, 192, 25);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t27, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t34 = gm_l3;
        gm_tnpifv20irnph t35;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
        gm_tabb710d9mq2e t36;
        gm_f_format_ltp901v((void *)&t36, (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcuiab49hlgv7g, 193, 16);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_f_add_note_46v502s(t33, t38);
    }
    {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 64));
        gm_f_push_o4ckdr1(t40, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
    }
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t41 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t41);
        gm_tl874t2atnlh3 t42 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t42);
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
extern __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_kind_k239kpg_c(void * a0) { gm_t3fcbuehh3bs2 gm_r; gm_f_kind_k239kpg(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
extern __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
extern __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tc3oht4ksth9d gm_f_mk_expr_53qj7nd_c(void * a0, gm_t2gtbh39ptekd a1, gm_cseahvg8v7p0vs a2) { gm_tc3oht4ksth9d gm_r; gm_f_mk_expr_53qj7nd(&gm_r, a0, &a1, a2); return gm_r; }
extern __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
extern __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t3fcbuehh3bs2 gm_f_nth_c0r2nvs_c(void * a0, uint64_t a1) { gm_t3fcbuehh3bs2 gm_r; gm_f_nth_c0r2nvs(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
extern __attribute__((aligned(4))) uint8_t gm_f_nth_is_kw_4i9ruj5(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_name_oqhoobd F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 324u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    uint8_t t4;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; default: goto ma8; }
ma6:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t14 != ((uint8_t)0ULL)) goto pa13;
    goto po12;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    goto po12;
pa15:;
    goto mn11;
po12:;
    t4 = ((uint8_t)1);
    goto me5;
mn11:;
ma7:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t18 != ((uint8_t)2ULL)) goto mn17;
    void * t19 = gm_l0;
    uint64_t t20 = gm_l1;
    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
    if (o22) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 180, 26);
    uint64_t t23 = r21;
    gm_t3fcbuehh3bs2 t24;
    gm_f_nth_c0r2nvs((void *)&t24, t19, t23);
    gm_t41cj6gvba3r8 t25;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)56ULL));
    gm_t3fcbuehh3bs2 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t27 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 8), t26);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t25 + 0, t27);
    gm_t9qkh0rsooc8d t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)55ULL));
    gm_t3fcbuehh3bs2 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t25 + 32, t29);
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)54ULL));
    gm_t3fcbuehh3bs2 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t25 + 64, t31);
    uint8_t t32 = 0;
    if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 0, ((uint8_t *)&t24))) t32 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 32, ((uint8_t *)&t24))) t32 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t25) + 64, ((uint8_t *)&t24))) t32 = 1;
    uint8_t t33 = (uint8_t)(t32);
    t4 = t33;
    goto me5;
mn17:;
ma8:;
    t4 = ((uint8_t)0);
    goto me5;
mn34:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 176, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_parse_decl_ident_31rb1id(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_decl_ident_31rb1id F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    gm_t9qkh0rsooc8d gm_l2; /* k */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t3fcbuehh3bs2 t4;
        gm_f_kind_k239kpg((void *)&t4, t3);
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t5 != ((uint8_t)2ULL)) goto ie2;
        gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t4) + 8));
        gm_l2 = t6;
        {
            void * t8 = gm_l0;
            gm_t9qkh0rsooc8d t9 = gm_l2;
            gm_t08olq97rscn3 t10;
            gm_f_keyword_as_name_ipm23p9((void *)&t10, (void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 0;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_b16_8 t12 = gm_l1;
        gm_f_parse_ident_n2c6b75((void *)&(*(gm_t4jn8fl3lmk5k *)gm_out), (void *)NULL, cx, NULL, t11, t12);
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
inline __attribute__((aligned(4))) void gm_f_parse_expr_in_45seo3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1) {
    struct gm_fr_parse_expr_in_45seo3h F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = gm_l1;
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l2 = t4;
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
            gm_toh0ootodhjue t12 = gm_l2;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t11) + 128), t12);
        }
    }
    goto gm_ls13;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t15 = gm_l0;
            gm_toh0ootodhjue t16 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3) {
    struct gm_fr_parse_file_70g7vok F;
    uint32_t gm_l0; /* file */
    void * gm_l1; /* lexed */
    void * gm_l2; /* ids */
    void * gm_l3; /* diags */
    gm_b16_8 gm_l4; /* toks */
    gm_b16_8 gm_l6; /* items */
    uint32_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 595u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 0) + 0));
        gm_l4 = t3;
    }
    {
        memset((uint8_t *)&F.l5 + 4, 0, 4);
        memset((uint8_t *)&F.l5 + 84, 0, 4);
        memset((uint8_t *)&F.l5 + 131, 0, 1);
        uint32_t t4 = gm_l0;
        GM_ST(uint32_t, (uint8_t *)&F.l5 + 0, t4);
        gm_b16_8 t5 = gm_l4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 8, t6);
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 40) + 0));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 24, t9);
        void * t10 = gm_l1;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 80) + 0));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        GM_ST(gm_b16_8, (uint8_t *)&F.l5 + 40, t12);
        void * t13 = gm_l2;
        GM_ST(void *, (uint8_t *)&F.l5 + 56, t13);
        void * t14 = gm_l3;
        GM_ST(void *, (uint8_t *)&F.l5 + 64, t14);
        GM_ST(uint64_t, (uint8_t *)&F.l5 + 72, ((uint64_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&F.l5 + 80, ((uint32_t)0ULL));
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5 + 88)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_toh0ootodhjue t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 2), ((uint8_t)0));
        GM_ST(gm_toh0ootodhjue, (uint8_t *)&F.l5 + 128, t15);
        GM_ST(uint32_t, (uint8_t *)&F.l5 + 132, ((uint32_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&F.l5 + 136, ((uint64_t)0ULL));
        gm_tf5srh4mp4b3i t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&F.l5 + 144, t16);
        GM_ST(uint64_t, (uint8_t *)&F.l5 + 160, ((uint64_t)0ULL));
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint64_t r18; bool o19 = __builtin_mul_overflow(t17, ((uint64_t)50ULL), &r18);
        if (o19) gm_trap("integer overflow in `*`", gm_srcuiab49hlgv7g, 110, 21);
        uint64_t t20 = r18;
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1000ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 110, 21);
        uint64_t t23 = r21;
        GM_ST(uint64_t, (uint8_t *)&F.l5 + 168, t23);
        gm_f_list_2uu83vj((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5 + 176)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t25 = gm_f_parse_file_items_9rlm6mo((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t25;
    }
    {
        uint32_t t26;
        gm_b16_8 t28 = gm_l4;
        void * t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t28)));
        uint64_t t30 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t28) + 8);
        uint64_t t31 = (t30 - 1);
        gm_tvr28kgomlp0c t32;
        if (t31 < t30) {
        gm_tm4pc0emhrv1g t33 = GM_LD(gm_tm4pc0emhrv1g, ((uint8_t *)t29 + t31 * 48));
        gm_tvr28kgomlp0c t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 52, 0);
        GM_ST(gm_tm4pc0emhrv1g, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[48] = 1;
            t32 = t34;
        } else {
        gm_tvr28kgomlp0c t35;
        memset(&t35, 0, sizeof t35);
            t32 = t35;
        }
        if (!((((uint8_t *)&t32))[48] != 0)) goto ie27;
        F.l7 = (*(gm_tm4pc0emhrv1g *)(((uint8_t *)&t32) + 0));
        uint32_t t37 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 32) + 8));
        uint32_t t38 = t37;
        t26 = t38;
        goto ix27;
ie27:;
        uint32_t t40 = ((uint32_t)0ULL);
        t26 = t40;
ix27:;
        gm_l8 = t26;
    }
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t41 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t41);
        uint32_t t42 = gm_l0;
        uint32_t t43 = gm_l8;
        gm_tl874t2atnlh3 t44;
        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t42);
        GM_ST(uint32_t, ((uint8_t *)&t44 + 4), ((uint32_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t44 + 8), t43);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t44);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_parse_file_items_9rlm6mo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_file_items_9rlm6mo F;
    void * gm_l0; /* p */
    uint64_t gm_l2; /* before */
    GM_STACK_CHECK(&F, 507u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_f0gjjbh((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t5);
            }
            {
                void * t7 = gm_l0;
                uint8_t t8 = gm_f_at_eof_ebcpkv0(t7);
                if (!t8) goto ie6;
                {
                    goto lb2;
                }
                goto ix6;
ie6:;
ix6:;
            }
            {
                void * t10 = gm_l0;
                uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 72));
                gm_l2 = t11;
            }
            {
                void * t13 = gm_l0;
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)66ULL));
                uint8_t t15 = gm_f_at_54906rm(t13, t14);
                if (!t15) goto ie12;
                {
                    void * t17 = gm_l0;
                    void * t18 = gm_l0;
                    gm_tl874t2atnlh3 t19 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t18));
                    gm_b16_8 t20;
                    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sdoflo4g2h6ev));
                    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(13));
                    gm_b0_1 t21 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t17, t19, t20);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
                {
                    {
                        void * t23 = gm_l0;
                        gm_tm4pc0emhrv1g t24;
                        gm_f_bump_q2i40re((void *)&t24, t23);
                    }
                }
                {
                    goto lc2;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                void * t26 = gm_l0;
                gm_ts6irudtvl17i t27;
                gm_f_parse_item_sbv139e((void *)&t27, (void *)NULL, cx, NULL, t26);
                if (gm_unw.kind) {
                    goto gm_ll3;
                }
                if (((((uint8_t *)&t27))[0] != 0)) goto ie25;
                F.l3 = (*(gm_tvpnktv5fo8nl *)(((uint8_t *)&t27) + 8));
                {
                    void * t29 = (void *)((uint8_t *)&F.l1);
                    gm_tftb1odc739t5 t30 = gm_f_push_1e500nc(t29, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l3)));
                    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcuiab49hlgv7g, 470, 13);
                    gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                }
                {
                    void * t32 = gm_l0;
                    gm_b16_8 t33;
                    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_senjrqtakqrde));
                    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(15));
                    gm_b0_1 t34 = gm_f_expect_terminator_f7kp9sk((void *)NULL, cx, NULL, t32, t33);
                    if (gm_unw.kind) {
                        goto gm_ll3;
                    }
                }
                goto ix25;
ie25:;
                {
                    void * t36 = gm_l0;
                    gm_f_recover_to_stmt_end_freq9f3(t36);
                }
ix25:;
            }
            {
                void * t38 = gm_l0;
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 72));
                uint64_t t40 = gm_l2;
                uint8_t t41 = ((t39 == t40));
                if (!t41) goto ie37;
                {
                    {
                        void * t44 = gm_l0;
                        gm_tm4pc0emhrv1g t45;
                        gm_f_bump_q2i40re((void *)&t45, t44);
                    }
                }
                goto ix37;
ie37:;
ix37:;
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
    {
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t47 = 0;
        F.ret = t46;
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
inline __attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_ident_n2c6b75 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* s */
    GM_STACK_CHECK(&F, 269u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        gm_t3fcbuehh3bs2 t6;
        gm_f_kind_k239kpg((void *)&t6, t5);
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)0ULL)) goto ie4;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l3 = t8;
        {
            {
                void * t11 = gm_l0;
                gm_tm4pc0emhrv1g t12;
                gm_f_bump_q2i40re((void *)&t12, t11);
            }
        }
        {
            gm_t08olq97rscn3 t13;
            memset((uint8_t *)&t13 + 28, 0, 4);
            gm_b16_8 t14 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 0, t14);
            gm_tl874t2atnlh3 t15 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t13 + 16, t15);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 0;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t13);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
        {
            void * t17 = gm_l0;
            gm_b16_8 t18 = gm_l1;
            gm_b0_1 t19 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_t4jn8fl3lmk5k *)gm_out), 0, sizeof (*(gm_t4jn8fl3lmk5k *)gm_out));
            ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 1;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t19);
            goto gm_fdone;
        }
ix4:;
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
inline __attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_name_figjvbe F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* s */
    gm_t9qkh0rsooc8d gm_l4; /* k */
    GM_STACK_CHECK(&F, 431u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t3fcbuehh3bs2 t5;
        gm_f_kind_k239kpg((void *)&t5, t4);
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t15;
        {
            {
                void * t18 = gm_l0;
                gm_tm4pc0emhrv1g t19;
                gm_f_bump_q2i40re((void *)&t19, t18);
            }
        }
        {
            gm_t08olq97rscn3 t20;
            memset((uint8_t *)&t20 + 28, 0, 4);
            gm_b16_8 t21 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
            gm_tl874t2atnlh3 t22 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t20 + 16, t22);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 0;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t20);
            goto gm_fdone;
        }
        goto me6;
mn13:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        {
            {
                void * t27 = gm_l0;
                gm_tm4pc0emhrv1g t28;
                gm_f_bump_q2i40re((void *)&t28, t27);
            }
        }
        {
            gm_t08olq97rscn3 t29;
            memset((uint8_t *)&t29 + 28, 0, 4);
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t30);
            gm_tl874t2atnlh3 t31 = gm_l2;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t29 + 16, t31);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 0;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t29);
            goto gm_fdone;
        }
        goto me6;
mn23:;
ma9:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t33 != ((uint8_t)2ULL)) goto mn32;
        gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t5) + 8));
        gm_l4 = t34;
        void * t35 = gm_l0;
        gm_t9qkh0rsooc8d t36 = gm_l4;
        gm_t08olq97rscn3 t37;
        gm_f_keyword_as_name_ipm23p9((void *)&t37, (void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 0;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t37);
        goto gm_fdone;
        goto me6;
mn32:;
ma10:;
        void * t39 = gm_l0;
        gm_b16_8 t40 = gm_l1;
        gm_b0_1 t41 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_t4jn8fl3lmk5k *)gm_out), 0, sizeof (*(gm_t4jn8fl3lmk5k *)gm_out));
        ((uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_t4jn8fl3lmk5k *)gm_out) + 8, t41);
        goto gm_fdone;
        goto me6;
mn38:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 218, 5);
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
inline __attribute__((aligned(4))) void gm_f_parse_type_in_bvi7uav(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1) {
    struct gm_fr_parse_type_in_bvi7uav F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = gm_l1;
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l2 = t4;
    }
    {
    }
    {
        void * t8 = gm_l0;
        gm_f_parse_type_2m93dtq((void *)&F.ret, (void *)NULL, cx, NULL, t8);
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
            gm_toh0ootodhjue t12 = gm_l2;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t11) + 128), t12);
        }
    }
    goto gm_ls13;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t15 = gm_l0;
            gm_toh0ootodhjue t16 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_recover_to_stmt_end_freq9f3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_recover_to_stmt_end_freq9f3 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* depth */
    GM_STACK_CHECK_SHALLOW(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                gm_t3fcbuehh3bs2 t6;
                gm_f_kind_k239kpg((void *)&t6, t5);
                uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma8; default: goto ma12; }
ma8:;
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                if (t16 != ((uint8_t)10ULL)) goto mn15;
                gm_b0_1 t17;
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
                goto me7;
mn15:;
ma9:;
                uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                if (t20 != ((uint8_t)9ULL)) goto mn19;
                uint64_t t21 = gm_l1;
                uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
                if (!t22) goto mn19;
                gm_b0_1 t23;
                uint8_t t24 = 0;
                F.ret = t23;
                goto gm_fret;
                goto me7;
mn19:;
ma10:;
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                if (t26 != ((uint8_t)8ULL)) goto mn25;
                uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
                if (t27 != ((uint8_t)65ULL)) goto mn25;
                {
                    uint64_t t29 = gm_l1;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 304, 34);
                    uint64_t t32 = r30;
                    gm_l1 = t32;
                }
                goto me7;
mn25:;
ma11:;
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                if (t34 != ((uint8_t)8ULL)) goto mn33;
                uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
                if (t35 != ((uint8_t)66ULL)) goto mn33;
                {
                    uint64_t t38 = gm_l1;
                    uint8_t t39 = ((t38 == ((uint64_t)0ULL)));
                    if (!t39) goto ie37;
                    {
                        gm_b0_1 t41;
                        uint8_t t42 = 0;
                        F.ret = t41;
                        goto gm_fret;
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                {
                    uint64_t t43 = gm_l1;
                    uint64_t r44; bool o45 = __builtin_sub_overflow(t43, ((uint64_t)1ULL), &r44);
                    if (o45) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 307, 17);
                    uint64_t t46 = r44;
                    gm_l1 = t46;
                }
                goto me7;
mn33:;
ma12:;
                goto me7;
mn47:;
ma13:;
                gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 301, 9);
me7:;
            }
            {
                {
                    void * t50 = gm_l0;
                    gm_tm4pc0emhrv1g t51;
                    gm_f_bump_q2i40re((void *)&t51, t50);
                }
            }
lc2:;
        }
lb2:;
    }
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_report_diag_big1nsf_c(void * a0, gm_t4gut2c9fj1kt a1) { gm_f_report_diag_big1nsf(a0, &a1); }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_restore_12su1qd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_12su1qd F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        GM_ST(uint64_t, (((uint8_t *)t2) + 72), t3);
    }
    {
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
        GM_ST(uint32_t, (((uint8_t *)t4) + 80), t5);
    }
    {
        void * t6 = gm_l0;
        gm_tf5srh4mp4b3i t7 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)a1) + 16));
        GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)t6) + 144), t7);
    }
    {
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 32));
        GM_ST(uint64_t, (((uint8_t *)t8) + 136), t9);
    }
    {
        void * t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)a1) + 40));
        GM_ST(uint32_t, (((uint8_t *)t10) + 132), t11);
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 88);
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)a1) + 48));
        gm_f_truncate_vir0vu7(t13, t14);
    }
    {
        void * t15 = gm_l0;
        gm_toh0ootodhjue t16 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)a1) + 56));
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t15) + 128), t16);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_restore_12su1qd_c(void * a0, gm_tjhb0nuou5gid a1) { gm_f_restore_12su1qd(a0, &a1); }
extern __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
extern __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0);
inline __attribute__((aligned(4))) void gm_f_snapshot_g4eb69q(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_snapshot_g4eb69q F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset((uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 12, 0, 4);
        memset((uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 44, 0, 4);
        memset((uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 59, 0, 5);
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 72));
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 80));
        void * t6 = gm_l0;
        gm_tf5srh4mp4b3i t7 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t6) + 144));
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 136));
        void * t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 132));
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uintptr_t, ((((uint8_t *)t12) + 88) + 0) + 8);
        void * t14 = gm_l0;
        gm_toh0ootodhjue t15 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t14) + 128));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 0, t3);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 8, t5);
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 16, t7);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 32, t9);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 40, t11);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 48, t13);
        GM_ST(gm_toh0ootodhjue, (uint8_t *)&(*(gm_tjhb0nuou5gid *)gm_out) + 56, t15);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tjhb0nuou5gid gm_f_snapshot_g4eb69q_c(void * a0) { gm_tjhb0nuou5gid gm_r; gm_f_snapshot_g4eb69q(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
