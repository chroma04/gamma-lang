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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t pe[3]; } gm_tbgiuvo2j5vcs;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { gm_tmn7n0n9raou5 f0; gm_t88rcucg1b7uk f1; } gm_tm7p68clol9c3;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_b16_8 v1; } f1; } gm_tvbp5g7fh5fh0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgiuvo2j5vcs) == 12 && _Alignof(gm_tbgiuvo2j5vcs) == 4 && offsetof(gm_tbgiuvo2j5vcs, f0) == 0 && offsetof(gm_tbgiuvo2j5vcs, f1) == 8, "layout of `?Coercion`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tm7p68clol9c3) == 12 && _Alignof(gm_tm7p68clol9c3) == 4 && offsetof(gm_tm7p68clol9c3, f0) == 0 && offsetof(gm_tm7p68clol9c3, f1) == 8, "layout of `Step`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvbp5g7fh5fh0) == 24 && _Alignof(gm_tvbp5g7fh5fh0) == 8 && offsetof(gm_tvbp5g7fh5fh0, f0) == 0 && offsetof(gm_tvbp5g7fh5fh0, f1) == 8, "layout of `LiftResult`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adds_const_e6d4bkb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_apply_steps_fd8cr97 {
    gm_frame h;
    void * ret;
};
struct gm_fr_base_coercion_t3tgsf0 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* a */
    gm_tddtjfggdat0d l26; /* b */
    gm_tbgiuvo2j5vcs ret;
};
struct gm_fr_coerce_sudc6c1 {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tddtjfggdat0d l25; /* b */
    gm_tddtjfggdat0d l26; /* a */
    gm_tddtjfggdat0d l30; /* a */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mcu2r7e {
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
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6canco6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_pointer_coercion_a03b7p1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_shape_m0o6aok {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_src_array_ok_og10umr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_steps_for_768gb3b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_steps_into_vts72l5 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b);
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_held_temp_d00dm1p(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lift_variant_ptko7np(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f21bjke(void * a0, gm_tm7p68clol9c3 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_joldpsp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_set_contains_8v9ep9m(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_coerce_c57qnm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_sn1h26ucppu4i[110];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4vq75vtnd3bq[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eq0iqiubfm7e(pa + i * 16, pb + i * 16)) return false;
    return true;
}
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq9m6mvtmgde(a, b);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t4 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mcu2r7e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
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
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6canco6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mcu2r7e();
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
inline __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_src_array_ok_og10umr F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tnnjeerolos0a gm_l2; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 103u);
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
        if (t6 != ((uint8_t)11ULL)) goto ie2;
        gm_tnnjeerolos0a t7 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t5) + 8) + 12));
        gm_l2 = t7;
        {
            uint8_t t9;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t12 != ((uint8_t)1ULL)) goto mn11;
            t9 = ((uint8_t)0);
            goto me10;
mn11:;
            t9 = ((uint8_t)1);
            goto me10;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 273, 12);
me10:;
            uint8_t t14 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adds_const_e6d4bkb F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint8_t gm_l6; /* tc */
    gm_t88rcucg1b7uk gm_l7; /* q */
    uint8_t gm_l8; /* sc */
    gm_t88rcucg1b7uk gm_l9; /* p */
    uint8_t gm_l10; /* tc */
    gm_t88rcucg1b7uk gm_l11; /* q */
    uint8_t gm_l12; /* sc */
    gm_t88rcucg1b7uk gm_l13; /* p */
    uint8_t gm_l14; /* tc */
    gm_t88rcucg1b7uk gm_l15; /* q */
    gm_t88rcucg1b7uk gm_l16; /* a */
    gm_t88rcucg1b7uk gm_l17; /* b */
    gm_t88rcucg1b7uk gm_l18; /* e1 */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* e2 */
    gm_t88rcucg1b7uk gm_l21; /* b */
    GM_STACK_CHECK(&F, 661u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma8:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t17 != ((uint8_t)8ULL)) goto mn16;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t18;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l5 = t19;
        void * t21 = gm_l3;
        gm_t88rcucg1b7uk t22 = gm_l2;
        gm_t53at80pfdus3 t23;
        gm_f_kind_jcnt4hm((void *)&t23, t21, GM_CONV(gm_csguankko4lmhd, t22));
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t24 != ((uint8_t)8ULL)) goto ie20;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t23) + 8) + 0));
        gm_l6 = t25;
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t23) + 8) + 4));
        gm_l7 = t26;
        {
            uint8_t t28 = 0;
            uint8_t t29 = 0;
            gm_t88rcucg1b7uk t30 = gm_l5;
            gm_t88rcucg1b7uk t31 = gm_l7;
            uint8_t t32 = (gm_eq9m6mvtmgde(((uint8_t *)&t30), ((uint8_t *)&t31)));
            if (t32) {
                uint8_t t33 = gm_l6;
                t29 = t33;
            } else t29 = 0;
            if (t29) {
                uint8_t t34 = gm_l4;
                uint8_t t35 = (uint8_t)!t34;
                t28 = t35;
            } else t28 = 0;
            uint8_t t36 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
        goto me7;
mn16:;
ma9:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t38 != ((uint8_t)9ULL)) goto mn37;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l8 = t39;
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l9 = t40;
        void * t42 = gm_l3;
        gm_t88rcucg1b7uk t43 = gm_l2;
        gm_t53at80pfdus3 t44;
        gm_f_kind_jcnt4hm((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
        if (t45 != ((uint8_t)9ULL)) goto ie41;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)&t44) + 8) + 0));
        gm_l10 = t46;
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t44) + 8) + 4));
        gm_l11 = t47;
        {
            uint8_t t49 = 0;
            uint8_t t50 = 0;
            gm_t88rcucg1b7uk t51 = gm_l9;
            gm_t88rcucg1b7uk t52 = gm_l11;
            uint8_t t53 = (gm_eq9m6mvtmgde(((uint8_t *)&t51), ((uint8_t *)&t52)));
            if (t53) {
                uint8_t t54 = gm_l10;
                t50 = t54;
            } else t50 = 0;
            if (t50) {
                uint8_t t55 = gm_l8;
                uint8_t t56 = (uint8_t)!t55;
                t49 = t56;
            } else t49 = 0;
            uint8_t t57 = 0;
            F.ret = t49;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
        goto me7;
mn37:;
ma10:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t59 != ((uint8_t)10ULL)) goto mn58;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l12 = t60;
        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l13 = t61;
        void * t63 = gm_l3;
        gm_t88rcucg1b7uk t64 = gm_l2;
        gm_t53at80pfdus3 t65;
        gm_f_kind_jcnt4hm((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
        if (t66 != ((uint8_t)10ULL)) goto ie62;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&t65) + 8) + 0));
        gm_l14 = t67;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t65) + 8) + 4));
        gm_l15 = t68;
        {
            uint8_t t70 = 0;
            uint8_t t71 = 0;
            gm_t88rcucg1b7uk t72 = gm_l13;
            gm_t88rcucg1b7uk t73 = gm_l15;
            uint8_t t74 = (gm_eq9m6mvtmgde(((uint8_t *)&t72), ((uint8_t *)&t73)));
            if (t74) {
                uint8_t t75 = gm_l14;
                t71 = t75;
            } else t71 = 0;
            if (t71) {
                uint8_t t76 = gm_l12;
                uint8_t t77 = (uint8_t)!t76;
                t70 = t77;
            } else t70 = 0;
            uint8_t t78 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix62;
ie62:;
ix62:;
        goto me7;
mn58:;
ma11:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t80 != ((uint8_t)13ULL)) goto mn79;
        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l16 = t81;
        void * t83 = gm_l3;
        gm_t88rcucg1b7uk t84 = gm_l2;
        gm_t53at80pfdus3 t85;
        gm_f_kind_jcnt4hm((void *)&t85, t83, GM_CONV(gm_csguankko4lmhd, t84));
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t85) + 0));
        if (t86 != ((uint8_t)13ULL)) goto ie82;
        gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t85) + 8));
        gm_l17 = t87;
        {
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l16;
            gm_t88rcucg1b7uk t91 = gm_l17;
            uint8_t t92 = gm_f_adds_const_e6d4bkb(t89, GM_CONV(gm_csguankko4lmhd, t90), GM_CONV(gm_csguankko4lmhd, t91));
            uint8_t t93 = 0;
            F.ret = t92;
            goto gm_fret;
        }
        goto ix82;
ie82:;
ix82:;
        goto me7;
mn79:;
ma12:;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t95 != ((uint8_t)14ULL)) goto mn94;
        gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l18 = t96;
        gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l19 = t97;
        void * t99 = gm_l3;
        gm_t88rcucg1b7uk t100 = gm_l2;
        gm_t53at80pfdus3 t101;
        gm_f_kind_jcnt4hm((void *)&t101, t99, GM_CONV(gm_csguankko4lmhd, t100));
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
        if (t102 != ((uint8_t)14ULL)) goto ie98;
        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t101) + 8) + 0));
        gm_l20 = t103;
        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t101) + 8) + 4));
        gm_l21 = t104;
        {
            uint8_t t106 = 0;
            gm_t88rcucg1b7uk t107 = gm_l18;
            gm_t88rcucg1b7uk t108 = gm_l20;
            uint8_t t109 = (gm_eq9m6mvtmgde(((uint8_t *)&t107), ((uint8_t *)&t108)));
            if (t109) {
                void * t110 = gm_l0;
                gm_t88rcucg1b7uk t111 = gm_l19;
                gm_t88rcucg1b7uk t112 = gm_l21;
                uint8_t t113 = gm_f_adds_const_e6d4bkb(t110, GM_CONV(gm_csguankko4lmhd, t111), GM_CONV(gm_csguankko4lmhd, t112));
                t106 = t113;
            } else t106 = 0;
            uint8_t t114 = 0;
            F.ret = t106;
            goto gm_fret;
        }
        goto ix98;
ie98:;
ix98:;
        goto me7;
mn94:;
ma13:;
        goto me7;
mn115:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 198, 5);
me7:;
    }
    {
        uint8_t t117 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_apply_steps_fd8cr97 F;
    void * gm_l0; /* e0 */
    gm_b16_8 gm_l1; /* steps */
    void * gm_l2; /* e */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tm7p68clol9c3 gm_l5; /* s */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        {
            gm_b16_8 t4 = gm_l1;
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
                    gm_tm7p68clol9c3 t16 = GM_LD(gm_tm7p68clol9c3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 12));
                    gm_l5 = t16;
                    {
                        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_tmn7n0n9raou5 t19 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l5) + 0));
                        void * t20 = gm_l2;
                        gm_t8jsc4n9vp2ib t21;
                        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t21 + 0), t19);
                        GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
                        gm_teti411eujvf4 t22;
                        memset(&t22, 0, sizeof t22);
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)32ULL));
                        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t22 + 8), t21);
                        void * t23 = gm_l2;
                        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t23) + 72));
                        void * t25 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t18, &t22, t24);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_l2 = t25;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 136, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
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
        void * t31 = gm_l2;
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
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_base_coercion_t3tgsf0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sa */
    uint16_t gm_l5; /* ba */
    uint8_t gm_l6; /* sb */
    uint16_t gm_l7; /* bb */
    uint8_t gm_l8; /* ok */
    uint16_t gm_l9; /* a */
    uint16_t gm_l10; /* b */
    gm_b16_8 gm_l11; /* a */
    gm_b16_8 gm_l12; /* na */
    gm_b16_8 gm_l13; /* b */
    gm_b16_8 gm_l14; /* nb */
    uint8_t gm_l15; /* same */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* b */
    gm_t88rcucg1b7uk gm_l21; /* e1 */
    gm_t88rcucg1b7uk gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* e2 */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l27; /* same */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tiilmfd16v3fc gm_l33; /* x */
    gm_t88rcucg1b7uk gm_l34; /* pointee */
    GM_STACK_CHECK(&F, 2225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; default: goto ma15; }
ma8:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t19 != ((uint8_t)5ULL)) goto mn18;
        gm_tmn7n0n9raou5 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)5ULL));
        gm_tbgiuvo2j5vcs t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[8] = 1;
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me7;
mn18:;
ma9:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t25;
        uint16_t t26 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
        gm_l5 = t26;
        void * t27 = gm_l3;
        gm_t88rcucg1b7uk t28 = gm_l2;
        gm_t53at80pfdus3 t29;
        gm_f_kind_jcnt4hm((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma31; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma33; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma34; case ((uint8_t)9ULL): goto ma34; case ((uint8_t)10ULL): goto ma34; case ((uint8_t)11ULL): goto ma34; case ((uint8_t)12ULL): goto ma34; case ((uint8_t)13ULL): goto ma34; case ((uint8_t)14ULL): goto ma34; case ((uint8_t)15ULL): goto ma34; case ((uint8_t)16ULL): goto ma34; case ((uint8_t)17ULL): goto ma34; case ((uint8_t)18ULL): goto ma34; case ((uint8_t)19ULL): goto ma34; case ((uint8_t)20ULL): goto ma34; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma34; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma34; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; default: goto ma34; }
ma31:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t38 != ((uint8_t)0ULL)) goto mn37;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&t29) + 8) + 0));
        gm_l6 = t39;
        uint16_t t40 = GM_LD(uint16_t, ((((uint8_t *)&t29) + 8) + 2));
        gm_l7 = t40;
        {
            uint8_t t42;
            uint8_t t44 = gm_l4;
            uint8_t t45 = gm_l6;
            uint8_t t46 = ((t44 == t45));
            if (!t46) goto ie43;
            uint16_t t48 = gm_l7;
            uint16_t t49 = gm_l5;
            uint8_t t50 = (t48 >= t49);
            uint8_t t51 = t50;
            t42 = t51;
            goto ix43;
ie43:;
            uint8_t t52;
            uint8_t t54 = gm_l4;
            uint8_t t55 = (uint8_t)!t54;
            if (!t55) goto ie53;
            uint16_t t57 = gm_l7;
            uint16_t t58 = gm_l5;
            uint8_t t59 = (t57 > t58);
            uint8_t t60 = t59;
            t52 = t60;
            goto ix53;
ie53:;
            uint8_t t62 = ((uint8_t)0);
            t52 = t62;
ix53:;
            t42 = t52;
ix43:;
            gm_l8 = t42;
        }
        {
            gm_tbgiuvo2j5vcs t63;
            uint8_t t65 = gm_l8;
            if (!t65) goto ie64;
            gm_tmn7n0n9raou5 t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
            gm_tmn7n0n9raou5 t68 = t67;
            gm_tbgiuvo2j5vcs t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t69), t68);
            ((uint8_t *)&t69)[8] = 1;
            t63 = t69;
            goto ix64;
ie64:;
            gm_tbgiuvo2j5vcs t71;
            memset(&t71, 0, sizeof t71);
            gm_tbgiuvo2j5vcs t72 = t71;
            t63 = t72;
ix64:;
            uint8_t t73 = 0;
            F.ret = t63;
            goto gm_fret;
        }
        goto me30;
mn37:;
ma32:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t75 != ((uint8_t)1ULL)) goto mn74;
        gm_tbgiuvo2j5vcs t76;
        uint8_t t78 = gm_l4;
        uint8_t t79 = (uint8_t)!t78;
        if (!t79) goto ie77;
        uint16_t t80 = gm_l5;
        uint8_t t81 = (t80 <= ((uint16_t)32ULL));
        if (!t81) goto ie77;
        gm_tmn7n0n9raou5 t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)0ULL));
        gm_tmn7n0n9raou5 t84 = t83;
        gm_tbgiuvo2j5vcs t85;
        GM_ST(uint8_t, (uint8_t *)&t85 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t85 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t85), t84);
        ((uint8_t *)&t85)[8] = 1;
        t76 = t85;
        goto ix77;
ie77:;
        gm_tbgiuvo2j5vcs t87;
        memset(&t87, 0, sizeof t87);
        gm_tbgiuvo2j5vcs t88 = t87;
        t76 = t88;
ix77:;
        uint8_t t89 = 0;
        F.ret = t76;
        goto gm_fret;
        goto me30;
mn74:;
ma33:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
        if (t91 != ((uint8_t)2ULL)) goto mn90;
        gm_tbgiuvo2j5vcs t92;
        uint8_t t94 = 0;
        uint8_t t95 = 0;
        uint8_t t96 = gm_l4;
        uint8_t t97 = (uint8_t)!t96;
        if (t97) {
            uint16_t t98 = gm_l5;
            uint8_t t99 = (t98 <= ((uint16_t)31ULL));
            t95 = t99;
        } else t95 = 0;
        if (!t95) {
            uint8_t t100 = 0;
            uint8_t t101 = gm_l4;
            if (t101) {
                uint16_t t102 = gm_l5;
                uint8_t t103 = (t102 <= ((uint16_t)32ULL));
                t100 = t103;
            } else t100 = 0;
            t94 = t100;
        } else t94 = 1;
        if (!t94) goto ie93;
        gm_tmn7n0n9raou5 t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
        gm_tmn7n0n9raou5 t106 = t105;
        gm_tbgiuvo2j5vcs t107;
        GM_ST(uint8_t, (uint8_t *)&t107 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t107 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t107), t106);
        ((uint8_t *)&t107)[8] = 1;
        t92 = t107;
        goto ix93;
ie93:;
        gm_tbgiuvo2j5vcs t109;
        memset(&t109, 0, sizeof t109);
        gm_tbgiuvo2j5vcs t110 = t109;
        t92 = t110;
ix93:;
        uint8_t t111 = 0;
        F.ret = t92;
        goto gm_fret;
        goto me30;
mn90:;
ma34:;
        goto me30;
mn112:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 145, 25);
me30:;
        goto me7;
mn23:;
ma10:;
        uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t115 != ((uint8_t)3ULL)) goto mn114;
        uint16_t t116 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
        gm_l9 = t116;
        void * t118 = gm_l3;
        gm_t88rcucg1b7uk t119 = gm_l2;
        gm_t53at80pfdus3 t120;
        gm_f_kind_jcnt4hm((void *)&t120, t118, GM_CONV(gm_csguankko4lmhd, t119));
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t120) + 0));
        if (t121 != ((uint8_t)3ULL)) goto ie117;
        uint16_t t122 = GM_LD(uint16_t, (((uint8_t *)&t120) + 8));
        gm_l10 = t122;
        {
            gm_tbgiuvo2j5vcs t124;
            uint16_t t126 = gm_l10;
            uint16_t t127 = gm_l9;
            uint8_t t128 = (t126 > t127);
            if (!t128) goto ie125;
            gm_tmn7n0n9raou5 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)1ULL));
            gm_tmn7n0n9raou5 t131 = t130;
            gm_tbgiuvo2j5vcs t132;
            GM_ST(uint8_t, (uint8_t *)&t132 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t132 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t132), t131);
            ((uint8_t *)&t132)[8] = 1;
            t124 = t132;
            goto ix125;
ie125:;
            gm_tbgiuvo2j5vcs t134;
            memset(&t134, 0, sizeof t134);
            gm_tbgiuvo2j5vcs t135 = t134;
            t124 = t135;
ix125:;
            uint8_t t136 = 0;
            F.ret = t124;
            goto gm_fret;
        }
        goto ix117;
ie117:;
ix117:;
        goto me7;
mn114:;
ma11:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t138 != ((uint8_t)12ULL)) goto mn137;
        gm_b16_8 t139 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l11 = t139;
        gm_b16_8 t140 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 16));
        gm_l12 = t140;
        void * t142 = gm_l3;
        gm_t88rcucg1b7uk t143 = gm_l2;
        gm_t53at80pfdus3 t144;
        gm_f_kind_jcnt4hm((void *)&t144, t142, GM_CONV(gm_csguankko4lmhd, t143));
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&t144) + 0));
        if (t145 != ((uint8_t)12ULL)) goto ie141;
        gm_b16_8 t146 = GM_LD(gm_b16_8, ((((uint8_t *)&t144) + 8) + 0));
        gm_l13 = t146;
        gm_b16_8 t147 = GM_LD(gm_b16_8, ((((uint8_t *)&t144) + 8) + 16));
        gm_l14 = t147;
        {
            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
            uint8_t t151 = ((t149 == t150));
            gm_l15 = t151;
        }
        {
            uint8_t t153 = gm_l15;
            if (!t153) goto ie152;
            {
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_l16 = t156;
                }
                {
                    for (;;) {
                        uint64_t t158 = gm_l17;
                        uint64_t t159 = gm_l16;
                        uint8_t t160 = (t158 < t159);
                        if (!t160) goto lb157;
                        {
                            uint64_t t163 = gm_l17;
                            uint64_t t166 = GM_LD(uint64_t, ((uint8_t *)&t163));
                            gm_l18 = t166;
                            {
                                uint64_t t169 = gm_l18;
                                uint64_t t170 = (uintptr_t)t169;
                                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_src4vq75vtnd3bq, 159, 24);
                                gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t170 * 4));
                                uint64_t t173 = gm_l18;
                                uint64_t t174 = (uintptr_t)t173;
                                uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_src4vq75vtnd3bq, 159, 36);
                                gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t174 * 4));
                                uint8_t t177 = (!gm_eq9m6mvtmgde(((uint8_t *)&t172), ((uint8_t *)&t176)));
                                if (!t177) goto ie168;
                                {
                                    gm_l15 = ((uint8_t)0);
                                }
                                goto ix168;
ie168:;
ix168:;
                            }
                            goto me164;
mn165:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 158, 17);
me164:;
                        }
lc157:;
                        {
                            uint64_t t180 = gm_l17;
                            uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                            uint64_t t183 = r181;
                            gm_l17 = t183;
                        }
                    }
lb157:;
                }
            }
            goto ix152;
ie152:;
ix152:;
        }
        {
            gm_tbgiuvo2j5vcs t184;
            uint8_t t186 = gm_l15;
            if (!t186) goto ie185;
            gm_b16_8 t187 = gm_l12;
            gm_b16_8 t188 = gm_l14;
            uint8_t t189 = (!gm_eqq39g9duc08(((uint8_t *)&t187), ((uint8_t *)&t188)));
            if (!t189) goto ie185;
            uint8_t t190 = 0;
            uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
            uint8_t t192 = ((t191 == ((uint64_t)0ULL)));
            if (!t192) {
                uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t194 = ((t193 == ((uint64_t)0ULL)));
                t190 = t194;
            } else t190 = 1;
            if (!t190) goto ie185;
            gm_tmn7n0n9raou5 t196;
            GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)8ULL));
            gm_tmn7n0n9raou5 t197 = t196;
            gm_tbgiuvo2j5vcs t198;
            GM_ST(uint8_t, (uint8_t *)&t198 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t198 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t198), t197);
            ((uint8_t *)&t198)[8] = 1;
            t184 = t198;
            goto ix185;
ie185:;
            gm_tbgiuvo2j5vcs t200;
            memset(&t200, 0, sizeof t200);
            gm_tbgiuvo2j5vcs t201 = t200;
            t184 = t201;
ix185:;
            uint8_t t202 = 0;
            F.ret = t184;
            goto gm_fret;
        }
        goto ix141;
ie141:;
ix141:;
        goto me7;
mn137:;
ma12:;
        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t204 != ((uint8_t)13ULL)) goto mn203;
        gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l19 = t205;
        void * t207 = gm_l3;
        gm_t88rcucg1b7uk t208 = gm_l2;
        gm_t53at80pfdus3 t209;
        gm_f_kind_jcnt4hm((void *)&t209, t207, GM_CONV(gm_csguankko4lmhd, t208));
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&t209) + 0));
        if (t210 != ((uint8_t)13ULL)) goto ie206;
        gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t209) + 8));
        gm_l20 = t211;
        {
            void * t214 = gm_l0;
            gm_t88rcucg1b7uk t215 = gm_l19;
            uint8_t t216 = gm_f_is_nullable_pointer_nkie06s(t214, GM_CONV(gm_csguankko4lmhd, t215));
            if (!t216) goto ie213;
            {
                gm_tbgiuvo2j5vcs t218;
                void * t220 = gm_l0;
                gm_t88rcucg1b7uk t221 = gm_l19;
                gm_t88rcucg1b7uk t222 = gm_l20;
                uint8_t t223 = gm_f_pointer_coercion_a03b7p1(t220, GM_CONV(gm_csguankko4lmhd, t221), GM_CONV(gm_csguankko4lmhd, t222));
                if (!t223) goto ie219;
                gm_tmn7n0n9raou5 t225;
                GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)11ULL));
                gm_tmn7n0n9raou5 t226 = t225;
                gm_tbgiuvo2j5vcs t227;
                GM_ST(uint8_t, (uint8_t *)&t227 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t227 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t227), t226);
                ((uint8_t *)&t227)[8] = 1;
                t218 = t227;
                goto ix219;
ie219:;
                gm_tbgiuvo2j5vcs t229;
                memset(&t229, 0, sizeof t229);
                gm_tbgiuvo2j5vcs t230 = t229;
                t218 = t230;
ix219:;
                uint8_t t231 = 0;
                F.ret = t218;
                goto gm_fret;
            }
            goto ix213;
ie213:;
ix213:;
        }
        {
            void * t233 = gm_l0;
            gm_t88rcucg1b7uk t234 = gm_l19;
            gm_t88rcucg1b7uk t235 = gm_l20;
            uint8_t t236 = gm_f_adds_const_e6d4bkb(t233, GM_CONV(gm_csguankko4lmhd, t234), GM_CONV(gm_csguankko4lmhd, t235));
            if (!t236) goto ie232;
            {
                gm_tmn7n0n9raou5 t238;
                GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)6ULL));
                gm_tbgiuvo2j5vcs t239;
                GM_ST(uint8_t, (uint8_t *)&t239 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t239 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t239), t238);
                ((uint8_t *)&t239)[8] = 1;
                uint8_t t240 = 0;
                F.ret = t239;
                goto gm_fret;
            }
            goto ix232;
ie232:;
ix232:;
        }
        goto ix206;
ie206:;
ix206:;
        goto me7;
mn203:;
ma13:;
        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t242 != ((uint8_t)14ULL)) goto mn241;
        gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 0));
        gm_l21 = t243;
        gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t6) + 8) + 4));
        gm_l22 = t244;
        void * t246 = gm_l3;
        gm_t88rcucg1b7uk t247 = gm_l2;
        gm_t53at80pfdus3 t248;
        gm_f_kind_jcnt4hm((void *)&t248, t246, GM_CONV(gm_csguankko4lmhd, t247));
        uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
        if (t249 != ((uint8_t)14ULL)) goto ie245;
        gm_t88rcucg1b7uk t250 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t248) + 8) + 0));
        gm_l23 = t250;
        gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t248) + 8) + 4));
        gm_l24 = t251;
        gm_t88rcucg1b7uk t252 = gm_l21;
        gm_t88rcucg1b7uk t253 = gm_l23;
        uint8_t t254 = (gm_eq9m6mvtmgde(((uint8_t *)&t252), ((uint8_t *)&t253)));
        if (!t254) goto ie245;
        void * t255 = gm_l0;
        gm_t88rcucg1b7uk t256 = gm_l22;
        gm_t88rcucg1b7uk t257 = gm_l24;
        uint8_t t258 = gm_f_adds_const_e6d4bkb(t255, GM_CONV(gm_csguankko4lmhd, t256), GM_CONV(gm_csguankko4lmhd, t257));
        if (!t258) goto ie245;
        {
            gm_tmn7n0n9raou5 t260;
            GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)6ULL));
            gm_tbgiuvo2j5vcs t261;
            GM_ST(uint8_t, (uint8_t *)&t261 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t261), t260);
            ((uint8_t *)&t261)[8] = 1;
            uint8_t t262 = 0;
            F.ret = t261;
            goto gm_fret;
        }
        goto ix245;
ie245:;
ix245:;
        goto me7;
mn241:;
ma14:;
        uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t264 != ((uint8_t)16ULL)) goto mn263;
        F.l25 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t6) + 8));
        void * t266 = gm_l3;
        gm_t88rcucg1b7uk t267 = gm_l2;
        gm_t53at80pfdus3 t268;
        gm_f_kind_jcnt4hm((void *)&t268, t266, GM_CONV(gm_csguankko4lmhd, t267));
        uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&t268) + 0));
        if (t269 != ((uint8_t)16ULL)) goto ie265;
        F.l26 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t268) + 8));
        {
            uint8_t t271 = 0;
            uint8_t t272 = 0;
            uint64_t t273 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
            uint64_t t274 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
            uint8_t t275 = ((t273 == t274));
            if (t275) {
                gm_t88rcucg1b7uk t276 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16));
                gm_t88rcucg1b7uk t277 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 16));
                uint8_t t278 = (gm_eq9m6mvtmgde(((uint8_t *)&t276), ((uint8_t *)&t277)));
                t272 = t278;
            } else t272 = 0;
            if (t272) {
                uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 40));
                uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                uint8_t t281 = ((t279 == t280));
                t271 = t281;
            } else t271 = 0;
            gm_l27 = t271;
        }
        {
            uint8_t t283 = gm_l27;
            if (!t283) goto ie282;
            {
                {
                    gm_l29 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t286 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                    gm_l28 = t286;
                }
                {
                    for (;;) {
                        uint64_t t288 = gm_l29;
                        uint64_t t289 = gm_l28;
                        uint8_t t290 = (t288 < t289);
                        if (!t290) goto lb287;
                        {
                            uint64_t t293 = gm_l29;
                            uint64_t t296 = GM_LD(uint64_t, ((uint8_t *)&t293));
                            gm_l30 = t296;
                            {
                                uint64_t t299 = gm_l30;
                                uint64_t t300 = (uintptr_t)t299;
                                uint64_t t301 = GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8);
                                if (t300 >= (uintptr_t)(t301)) gm_trap_index(t300, t301, gm_src4vq75vtnd3bq, 173, 24);
                                gm_t88rcucg1b7uk t302 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l25) + 0)) + t300 * 4));
                                uint64_t t303 = gm_l30;
                                uint64_t t304 = (uintptr_t)t303;
                                uint64_t t305 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 0) + 8);
                                if (t304 >= (uintptr_t)(t305)) gm_trap_index(t304, t305, gm_src4vq75vtnd3bq, 173, 43);
                                gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 0)) + t304 * 4));
                                uint8_t t307 = (!gm_eq9m6mvtmgde(((uint8_t *)&t302), ((uint8_t *)&t306)));
                                if (!t307) goto ie298;
                                {
                                    gm_l27 = ((uint8_t)0);
                                }
                                goto ix298;
ie298:;
ix298:;
                            }
                            goto me294;
mn295:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 172, 17);
me294:;
                        }
lc287:;
                        {
                            uint64_t t310 = gm_l29;
                            uint64_t r311; bool o312 = __builtin_add_overflow(t310, ((uint64_t)1ULL), &r311);
                            uint64_t t313 = r311;
                            gm_l29 = t313;
                        }
                    }
lb287:;
                }
            }
            goto ix282;
ie282:;
ix282:;
        }
        {
            uint8_t t315 = 0;
            uint8_t t316 = gm_l27;
            uint8_t t317 = (uint8_t)!t316;
            if (!t317) {
                uint64_t t318 = GM_LD(uintptr_t, ((((uint8_t *)&F.l25) + 24) + 0) + 8);
                uint64_t t319 = GM_LD(uintptr_t, ((((uint8_t *)&F.l26) + 24) + 0) + 8);
                uint8_t t320 = (t318 >= t319);
                t315 = t320;
            } else t315 = 1;
            if (!t315) goto ie314;
            {
                gm_tbgiuvo2j5vcs t322;
                memset(&t322, 0, sizeof t322);
                uint8_t t323 = 0;
                F.ret = t322;
                goto gm_fret;
            }
            goto ix314;
ie314:;
ix314:;
        }
        {
            {
                gm_b16_8 t325 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l25) + 24) + 0));
                gm_l31 = t325;
            }
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t327 = gm_l32;
                    uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                    uint8_t t329 = (t327 < t328);
                    if (!t329) goto lb326;
                    {
                        uint64_t t332 = gm_l32;
                        uint64_t t333 = (uintptr_t)t332;
                        uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        gm_tiilmfd16v3fc t337 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t333 * 24));
                        gm_l33 = t337;
                        {
                            void * t340 = (void *)(((uint8_t *)&F.l26) + 24);
                            void * t341 = (void *)((uint8_t *)&gm_l33);
                            uint8_t t342 = gm_f_set_contains_8v9ep9m(t340, t341);
                            uint8_t t343 = (uint8_t)!t342;
                            if (!t343) goto ie339;
                            {
                                gm_tbgiuvo2j5vcs t345;
                                memset(&t345, 0, sizeof t345);
                                uint8_t t346 = 0;
                                F.ret = t345;
                                goto gm_fret;
                            }
                            goto ix339;
ie339:;
ix339:;
                        }
                        goto me335;
mn336:;
                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 177, 13);
me335:;
                    }
lc326:;
                    {
                        uint64_t t348 = gm_l32;
                        uint64_t r349; bool o350 = __builtin_add_overflow(t348, ((uint64_t)1ULL), &r349);
                        uint64_t t351 = r349;
                        gm_l32 = t351;
                    }
                }
lb326:;
            }
        }
        {
            gm_tmn7n0n9raou5 t352;
            GM_ST(uint8_t, (uint8_t *)&t352 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t352 + 0), ((uint8_t)10ULL));
            gm_tbgiuvo2j5vcs t353;
            GM_ST(uint8_t, (uint8_t *)&t353 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t353 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t353), t352);
            ((uint8_t *)&t353)[8] = 1;
            uint8_t t354 = 0;
            F.ret = t353;
            goto gm_fret;
        }
        goto ix265;
ie265:;
ix265:;
        goto me7;
mn263:;
ma15:;
        goto me7;
mn355:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 143, 5);
me7:;
    }
    {
        void * t358 = gm_l0;
        gm_t88rcucg1b7uk t359 = gm_l1;
        gm_t88rcucg1b7uk t360 = gm_l2;
        uint8_t t361 = gm_f_pointer_coercion_a03b7p1(t358, GM_CONV(gm_csguankko4lmhd, t359), GM_CONV(gm_csguankko4lmhd, t360));
        if (!t361) goto ie357;
        {
            void * t364 = gm_l3;
            gm_t88rcucg1b7uk t365 = gm_l1;
            gm_t53at80pfdus3 t366;
            gm_f_kind_jcnt4hm((void *)&t366, t364, GM_CONV(gm_csguankko4lmhd, t365));
            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&t366) + 0));
            if (t367 != ((uint8_t)8ULL)) goto ie363;
            gm_t88rcucg1b7uk t368 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t366) + 8) + 4));
            gm_l34 = t368;
            void * t369 = gm_l3;
            gm_t88rcucg1b7uk t370 = gm_l2;
            gm_t53at80pfdus3 t371;
            gm_f_kind_jcnt4hm((void *)&t371, t369, GM_CONV(gm_csguankko4lmhd, t370));
            uint8_t t372;
            uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&t371) + 0));
            if (t375 != ((uint8_t)10ULL)) goto mn374;
            t372 = ((uint8_t)1);
            goto me373;
mn374:;
            t372 = ((uint8_t)0);
            goto me373;
mn376:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 50);
me373:;
            if (!t372) goto ie363;
            void * t377 = gm_l3;
            gm_t88rcucg1b7uk t378 = gm_l34;
            gm_t53at80pfdus3 t379;
            gm_f_kind_jcnt4hm((void *)&t379, t377, GM_CONV(gm_csguankko4lmhd, t378));
            uint8_t t380;
            uint8_t t386 = GM_LD(uint8_t, (((uint8_t *)&t379) + 0));
            switch (t386) { case ((uint8_t)0ULL): goto ma384; case ((uint8_t)1ULL): goto ma384; case ((uint8_t)2ULL): goto ma384; case ((uint8_t)3ULL): goto ma384; case ((uint8_t)4ULL): goto ma384; case ((uint8_t)5ULL): goto ma384; case ((uint8_t)6ULL): goto ma384; case ((uint8_t)7ULL): goto ma384; case ((uint8_t)8ULL): goto ma384; case ((uint8_t)9ULL): goto ma384; case ((uint8_t)10ULL): goto ma384; case ((uint8_t)11ULL): goto ma382; case ((uint8_t)12ULL): goto ma384; case ((uint8_t)13ULL): goto ma384; case ((uint8_t)14ULL): goto ma384; case ((uint8_t)15ULL): goto ma384; case ((uint8_t)16ULL): goto ma384; case ((uint8_t)17ULL): goto ma384; case ((uint8_t)18ULL): goto ma384; case ((uint8_t)19ULL): goto ma384; case ((uint8_t)20ULL): goto ma383; case ((uint8_t)21ULL): goto ma384; case ((uint8_t)22ULL): goto ma384; case ((uint8_t)23ULL): goto ma384; case ((uint8_t)24ULL): goto ma384; case ((uint8_t)25ULL): goto ma384; case ((uint8_t)26ULL): goto ma384; default: goto ma384; }
ma382:;
            uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&t379) + 0));
            if (t388 != ((uint8_t)11ULL)) goto mn387;
            t380 = ((uint8_t)1);
            goto me381;
mn387:;
ma383:;
            uint8_t t390 = GM_LD(uint8_t, (((uint8_t *)&t379) + 0));
            if (t390 != ((uint8_t)20ULL)) goto mn389;
            t380 = ((uint8_t)1);
            goto me381;
mn389:;
ma384:;
            t380 = ((uint8_t)0);
            goto me381;
mn391:;
ma385:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 84);
me381:;
            if (!t380) goto ie363;
            {
                gm_tmn7n0n9raou5 t393;
                GM_ST(uint8_t, (uint8_t *)&t393 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t393 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t393 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)7ULL));
                gm_tbgiuvo2j5vcs t394;
                GM_ST(uint8_t, (uint8_t *)&t394 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t394 + 10, 0);
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t394), t393);
                ((uint8_t *)&t394)[8] = 1;
                uint8_t t395 = 0;
                F.ret = t394;
                goto gm_fret;
            }
            goto ix363;
ie363:;
ix363:;
        }
        {
            gm_tmn7n0n9raou5 t396;
            GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t396 + 0), ((uint8_t)6ULL));
            gm_tbgiuvo2j5vcs t397;
            GM_ST(uint8_t, (uint8_t *)&t397 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t397 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t397), t396);
            ((uint8_t *)&t397)[8] = 1;
            uint8_t t398 = 0;
            F.ret = t397;
            goto gm_fret;
        }
        goto ix357;
ie357:;
ix357:;
    }
    {
        gm_tbgiuvo2j5vcs t399;
        memset(&t399, 0, sizeof t399);
        uint8_t t400 = 0;
        F.ret = t399;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coerce_sudc6c1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* target0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* src */
    gm_t88rcucg1b7uk gm_l5; /* target */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* b */
    uint8_t gm_l9; /* sc */
    uint8_t gm_l10; /* tc */
    void * gm_l12; /* c */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    void * gm_l14; /* p */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* na */
    gm_b16_8 gm_l17; /* b */
    gm_b16_8 gm_l18; /* nb */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_t88rcucg1b7uk gm_l22; /* found */
    gm_t88rcucg1b7uk gm_l23; /* s */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tiilmfd16v3fc gm_l29; /* x */
    uint8_t gm_l31; /* subset */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tiilmfd16v3fc gm_l34; /* x */
    gm_b16_8 gm_l35; /* steps */
    GM_STACK_CHECK(&F, 4092u);
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
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 96);
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t13, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t17;
    }
    {
        uint8_t t19 = 0;
        uint8_t t20 = 0;
        gm_t88rcucg1b7uk t21 = gm_l4;
        gm_t88rcucg1b7uk t22 = gm_l5;
        uint8_t t23 = (gm_eq9m6mvtmgde(((uint8_t *)&t21), ((uint8_t *)&t22)));
        if (!t23) {
            void * t24 = gm_l3;
            gm_t88rcucg1b7uk t25 = gm_l4;
            uint8_t t26 = gm_f_is_error_8k27gd1(t24, GM_CONV(gm_csguankko4lmhd, t25));
            t20 = t26;
        } else t20 = 1;
        if (!t20) {
            void * t27 = gm_l3;
            gm_t88rcucg1b7uk t28 = gm_l5;
            uint8_t t29 = gm_f_is_error_8k27gd1(t27, GM_CONV(gm_csguankko4lmhd, t28));
            t19 = t29;
        } else t19 = 1;
        if (!t19) goto ie18;
        {
            void * t31 = gm_l1;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t34 = gm_l3;
        gm_t88rcucg1b7uk t35 = gm_l4;
        gm_t53at80pfdus3 t36;
        gm_f_kind_jcnt4hm((void *)&t36, t34, GM_CONV(gm_csguankko4lmhd, t35));
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
        if (t37 != ((uint8_t)11ULL)) goto ie33;
        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 8));
        gm_l6 = t38;
        void * t39 = gm_l3;
        gm_t88rcucg1b7uk t40 = gm_l5;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
        if (t42 != ((uint8_t)10ULL)) goto ie33;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)&t41) + 8) + 0));
        gm_l7 = t43;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t41) + 8) + 4));
        gm_l8 = t44;
        uint8_t t45 = gm_l7;
        if (!t45) goto ie33;
        void * t46 = gm_l3;
        gm_t88rcucg1b7uk t47 = gm_l4;
        uint8_t t48 = gm_f_is_positional_array_6sufsk8(t46, GM_CONV(gm_csguankko4lmhd, t47));
        if (!t48) goto ie33;
        void * t49 = gm_l0;
        void * t50 = (void *)(((uint8_t *)t49) + 96);
        void * t51 = gm_l3;
        gm_t88rcucg1b7uk t52 = gm_l6;
        gm_t88rcucg1b7uk t53 = gm_l8;
        gm_t9qkh0rsooc8d t54 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t50, t51, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t55;
        if (!((((uint8_t *)&t54))[0] != 0)) goto mn57;
        t55 = ((uint8_t)0);
        goto me56;
mn57:;
        t55 = ((uint8_t)1);
        goto me56;
mn58:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 10, 121);
me56:;
        if (!t55) goto ie33;
        {
            void * t60 = gm_l0;
            void * t61 = gm_l0;
            void * t62 = gm_l1;
            gm_t88rcucg1b7uk t63 = gm_l5;
            void * t64 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t61, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t65 = gm_l5;
            void * t66 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t60, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t69 = gm_l3;
        gm_t88rcucg1b7uk t70 = gm_l4;
        gm_t53at80pfdus3 t71;
        gm_f_kind_jcnt4hm((void *)&t71, t69, GM_CONV(gm_csguankko4lmhd, t70));
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t71) + 0));
        if (t72 != ((uint8_t)10ULL)) goto ie68;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)&t71) + 8) + 0));
        gm_l9 = t73;
        void * t74 = gm_l3;
        gm_t88rcucg1b7uk t75 = gm_l5;
        gm_t53at80pfdus3 t76;
        gm_f_kind_jcnt4hm((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
        if (t77 != ((uint8_t)10ULL)) goto ie68;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
        gm_l10 = t78;
        uint8_t t79 = gm_l9;
        if (!t79) goto ie68;
        uint8_t t80 = gm_l10;
        uint8_t t81 = (uint8_t)!t80;
        if (!t81) goto ie68;
        void * t82 = gm_l1;
        uint8_t t83 = gm_f_is_held_temp_d00dm1p(t82);
        if (!t83) goto ie68;
        {
            void * t85 = gm_l1;
            gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t85) + 72));
            gm_t6j6smj7qoctu t87;
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l5;
            gm_b16_8 t90 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t90);
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l4;
            gm_b16_8 t93 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t93);
            gm_tabb710d9mq2e t94;
            gm_f_format_ns6dben((void *)&t94, (void *)NULL, cx, NULL, &t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_src4vq75vtnd3bq, 14, 33);
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t86, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t97 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t98;
            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sn1h26ucppu4i));
            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(109));
            gm_f_add_note_46v502s(t97, t98);
        }
        {
            void * t99 = gm_l0;
            void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
            gm_b0_1 t101 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t100, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t102 = gm_l1;
            uint8_t t103 = 0;
            F.ret = t102;
            goto gm_fret;
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        void * t105 = gm_l0;
        void * t106 = gm_l1;
        gm_t88rcucg1b7uk t107 = gm_l4;
        gm_t88rcucg1b7uk t108 = gm_l5;
        void * t109 = gm_f_soa_coerce_c57qnm7((void *)NULL, cx, NULL, t105, t106, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!(GM_LD(void *, ((uint8_t *)&t109)) != 0)) goto ie104;
        void * t110 = GM_LD(void *, ((uint8_t *)&t109));
        gm_l12 = t110;
        {
            void * t112 = gm_l12;
            uint8_t t113 = 0;
            F.ret = t112;
            goto gm_fret;
        }
        goto ix104;
ie104:;
ix104:;
    }
    {
        void * t115 = gm_l0;
        void * t116 = GM_LD(void *, (((uint8_t *)t115) + 0));
        gm_t88rcucg1b7uk t117 = gm_l5;
        gm_tkn42u7na3t81 t118 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t118))[4] != 0)) goto ie114;
        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t118));
        gm_l13 = t119;
        {
            void * t122 = gm_l0;
            void * t123 = gm_l1;
            gm_t88rcucg1b7uk t124 = gm_l5;
            gm_t88rcucg1b7uk t125 = gm_l13;
            void * t126 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t122, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!(GM_LD(void *, ((uint8_t *)&t126)) != 0)) goto ie121;
            void * t127 = GM_LD(void *, ((uint8_t *)&t126));
            gm_l14 = t127;
            {
                void * t129 = gm_l14;
                uint8_t t130 = 0;
                F.ret = t129;
                goto gm_fret;
            }
            goto ix121;
ie121:;
ix121:;
        }
        goto ix114;
ie114:;
ix114:;
    }
    {
        void * t132 = gm_l3;
        gm_t88rcucg1b7uk t133 = gm_l4;
        gm_t53at80pfdus3 t134;
        gm_f_kind_jcnt4hm((void *)&t134, t132, GM_CONV(gm_csguankko4lmhd, t133));
        uint8_t t135 = ((GM_LD(uint8_t, ((uint8_t *)&t134) + 0) == ((uint8_t)5ULL)));
        if (!t135) goto ie131;
        {
            gm_t88rcucg1b7uk t137 = gm_l5;
            gm_tmn7n0n9raou5 t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)5ULL));
            void * t139 = gm_l1;
            gm_t8jsc4n9vp2ib t140;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t140 + 0), t138);
            GM_ST(void *, ((uint8_t *)&t140 + 8), t139);
            gm_teti411eujvf4 t141;
            memset(&t141, 0, sizeof t141);
            GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t141 + 8), t140);
            void * t142 = gm_l1;
            gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
            void * t144 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t141, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t145 = 0;
            F.ret = t144;
            goto gm_fret;
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        void * t147 = gm_l3;
        gm_t88rcucg1b7uk t148 = gm_l4;
        gm_t53at80pfdus3 t149;
        gm_f_kind_jcnt4hm((void *)&t149, t147, GM_CONV(gm_csguankko4lmhd, t148));
        uint8_t t150;
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&t149) + 0));
        if (t153 != ((uint8_t)23ULL)) goto mn152;
        t150 = ((uint8_t)1);
        goto me151;
mn152:;
        t150 = ((uint8_t)0);
        goto me151;
mn154:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 25, 8);
me151:;
        if (!t150) goto ie146;
        {
            void * t156 = gm_l0;
            void * t157 = (void *)(((uint8_t *)t156) + 280);
            gm_thsh8t5u55gps t158;
            gm_t88rcucg1b7uk t159 = gm_l4;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t158 + 0, t159);
            gm_t88rcucg1b7uk t160 = gm_l5;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t158 + 4, t160);
            void * t161 = gm_l1;
            gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t158 + 8, t162);
            gm_tftb1odc739t5 t163 = gm_f_push_joldpsp(t157, &t158);
            if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_src4vq75vtnd3bq, 28, 9);
            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
        }
        {
            gm_t88rcucg1b7uk t165 = gm_l5;
            gm_tmn7n0n9raou5 t166;
            GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)12ULL));
            void * t167 = gm_l1;
            gm_t8jsc4n9vp2ib t168;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t168 + 0), t166);
            GM_ST(void *, ((uint8_t *)&t168 + 8), t167);
            gm_teti411eujvf4 t169;
            memset(&t169, 0, sizeof t169);
            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t169 + 8), t168);
            void * t170 = gm_l1;
            gm_tl874t2atnlh3 t171 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t170) + 72));
            void * t172 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t165, &t169, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t173 = 0;
            F.ret = t172;
            goto gm_fret;
        }
        goto ix146;
ie146:;
ix146:;
    }
    {
        void * t175 = gm_l3;
        gm_t88rcucg1b7uk t176 = gm_l4;
        gm_t53at80pfdus3 t177;
        gm_f_kind_jcnt4hm((void *)&t177, t175, GM_CONV(gm_csguankko4lmhd, t176));
        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t177) + 0));
        if (t178 != ((uint8_t)12ULL)) goto ie174;
        gm_b16_8 t179 = GM_LD(gm_b16_8, ((((uint8_t *)&t177) + 8) + 0));
        gm_l15 = t179;
        gm_b16_8 t180 = GM_LD(gm_b16_8, ((((uint8_t *)&t177) + 8) + 16));
        gm_l16 = t180;
        void * t181 = gm_l3;
        gm_t88rcucg1b7uk t182 = gm_l5;
        gm_t53at80pfdus3 t183;
        gm_f_kind_jcnt4hm((void *)&t183, t181, GM_CONV(gm_csguankko4lmhd, t182));
        uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&t183) + 0));
        if (t184 != ((uint8_t)12ULL)) goto ie174;
        gm_b16_8 t185 = GM_LD(gm_b16_8, ((((uint8_t *)&t183) + 8) + 0));
        gm_l17 = t185;
        gm_b16_8 t186 = GM_LD(gm_b16_8, ((((uint8_t *)&t183) + 8) + 16));
        gm_l18 = t186;
        {
            uint8_t t189 = 0;
            void * t190 = gm_l3;
            gm_t88rcucg1b7uk t191 = gm_l4;
            uint8_t t192 = gm_f_has_infer_3o82a0b(t190, GM_CONV(gm_csguankko4lmhd, t191));
            if (!t192) {
                void * t193 = gm_l3;
                gm_t88rcucg1b7uk t194 = gm_l5;
                uint8_t t195 = gm_f_has_infer_3o82a0b(t193, GM_CONV(gm_csguankko4lmhd, t194));
                t189 = t195;
            } else t189 = 1;
            if (!t189) goto ie188;
            uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t198 = ((t196 == t197));
            if (!t198) goto ie188;
            gm_b16_8 t199 = gm_l16;
            gm_b16_8 t200 = gm_l18;
            uint8_t t201 = (!gm_eqq39g9duc08(((uint8_t *)&t199), ((uint8_t *)&t200)));
            if (!t201) goto ie188;
            uint8_t t202 = 0;
            uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t204 = ((t203 == ((uint64_t)0ULL)));
            if (!t204) {
                uint64_t t205 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t206 = ((t205 == ((uint64_t)0ULL)));
                t202 = t206;
            } else t202 = 1;
            if (!t202) goto ie188;
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    gm_l19 = t209;
                }
                {
                    for (;;) {
                        uint64_t t211 = gm_l20;
                        uint64_t t212 = gm_l19;
                        uint8_t t213 = (t211 < t212);
                        if (!t213) goto lb210;
                        {
                            uint64_t t216 = gm_l20;
                            uint64_t t219 = GM_LD(uint64_t, ((uint8_t *)&t216));
                            gm_l21 = t219;
                            {
                                void * t222 = gm_l0;
                                void * t223 = (void *)(((uint8_t *)t222) + 96);
                                void * t224 = gm_l3;
                                uint64_t t225 = gm_l21;
                                uint64_t t226 = (uintptr_t)t225;
                                uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_src4vq75vtnd3bq, 35, 40);
                                gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t226 * 4));
                                uint64_t t229 = gm_l21;
                                uint64_t t230 = (uintptr_t)t229;
                                uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                if (t230 >= (uintptr_t)(t231)) gm_trap_index(t230, t231, gm_src4vq75vtnd3bq, 35, 46);
                                gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t230 * 4));
                                gm_t9qkh0rsooc8d t233 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t223, t224, t228, t232);
                                if (gm_unw.kind) {
                                    goto gm_ll214;
                                }
                                uint8_t t234;
                                if (!((((uint8_t *)&t233))[0] != 0)) goto mn236;
                                t234 = ((uint8_t)1);
                                goto me235;
mn236:;
                                t234 = ((uint8_t)0);
                                goto me235;
mn237:;
                                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 35, 20);
me235:;
                                if (!t234) goto ie221;
                                {
                                    void * t239 = gm_l0;
                                    void * t240 = (void *)(((uint8_t *)t239) + 96);
                                    void * t241 = gm_l0;
                                    void * t242 = GM_LD(void *, (((uint8_t *)t241) + 0));
                                    gm_t88rcucg1b7uk t243 = gm_l4;
                                    gm_t88rcucg1b7uk t244 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t240, t242, t243);
                                    if (gm_unw.kind) {
                                        goto gm_ll214;
                                    }
                                    gm_l22 = t244;
                                }
                                {
                                    void * t245 = gm_l0;
                                    void * t246 = gm_l1;
                                    gm_tl874t2atnlh3 t247 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t246) + 72));
                                    gm_t6j6smj7qoctu t248;
                                    void * t249 = gm_l0;
                                    gm_t88rcucg1b7uk t250 = gm_l5;
                                    gm_b16_8 t251 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t249, t250);
                                    if (gm_unw.kind) {
                                        goto gm_ll214;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t248 + 0, t251);
                                    void * t252 = gm_l0;
                                    gm_t88rcucg1b7uk t253 = gm_l22;
                                    gm_b16_8 t254 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t252, t253);
                                    if (gm_unw.kind) {
                                        goto gm_ll214;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t248 + 16, t254);
                                    gm_tabb710d9mq2e t255;
                                    gm_f_format_ns6dben((void *)&t255, (void *)NULL, cx, NULL, &t248);
                                    if (gm_unw.kind) {
                                        goto gm_ll214;
                                    }
                                    if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 8, gm_src4vq75vtnd3bq, 37, 38);
                                    gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                                    gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                                    gm_b0_1 t258 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t245, t247, t257);
                                    if (gm_unw.kind) {
                                        goto gm_ll214;
                                    }
                                }
                                {
                                    void * t259 = gm_l1;
                                    uint8_t t260 = 0;
                                    F.ret = t259;
                                    goto gm_fret;
                                }
                                goto ix221;
ie221:;
ix221:;
                            }
                            goto me217;
mn218:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 34, 13);
me217:;
                        }
lc210:;
                        {
                            uint64_t t262 = gm_l20;
                            uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                            uint64_t t265 = r263;
                            gm_l20 = t265;
                        }
                    }
gm_ll214:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb210; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc210; }
                    }
                    goto gm_lad;
lb210:;
                }
            }
            {
                gm_t88rcucg1b7uk t266 = gm_l5;
                gm_tmn7n0n9raou5 t267;
                GM_ST(uint8_t, (uint8_t *)&t267 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t267 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t267 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)8ULL));
                void * t268 = gm_l1;
                gm_t8jsc4n9vp2ib t269;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t269 + 0), t267);
                GM_ST(void *, ((uint8_t *)&t269 + 8), t268);
                gm_teti411eujvf4 t270;
                memset(&t270, 0, sizeof t270);
                GM_ST(uint8_t, ((uint8_t *)&t270 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t270 + 8), t269);
                void * t271 = gm_l1;
                gm_tl874t2atnlh3 t272 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t271) + 72));
                void * t273 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t266, &t270, t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t274 = 0;
                F.ret = t273;
                goto gm_fret;
            }
            goto ix188;
ie188:;
ix188:;
        }
        goto ix174;
ie174:;
ix174:;
    }
    {
        uint8_t t276 = 0;
        void * t277 = gm_l3;
        gm_t88rcucg1b7uk t278 = gm_l4;
        uint8_t t279 = gm_f_has_infer_3o82a0b(t277, GM_CONV(gm_csguankko4lmhd, t278));
        if (!t279) {
            void * t280 = gm_l3;
            gm_t88rcucg1b7uk t281 = gm_l5;
            uint8_t t282 = gm_f_has_infer_3o82a0b(t280, GM_CONV(gm_csguankko4lmhd, t281));
            t276 = t282;
        } else t276 = 1;
        if (!t276) goto ie275;
        {
            void * t285 = gm_l0;
            void * t286 = (void *)(((uint8_t *)t285) + 96);
            void * t287 = gm_l3;
            gm_t88rcucg1b7uk t288 = gm_l4;
            gm_t88rcucg1b7uk t289 = gm_l5;
            gm_t9qkh0rsooc8d t290 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t286, t287, t288, t289);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t291;
            if (!((((uint8_t *)&t290))[0] != 0)) goto mn293;
            t291 = ((uint8_t)1);
            goto me292;
mn293:;
            t291 = ((uint8_t)0);
            goto me292;
mn294:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 45, 12);
me292:;
            if (!t291) goto ie284;
            {
                void * t296 = gm_l0;
                void * t297 = (void *)(((uint8_t *)t296) + 96);
                void * t298 = gm_l0;
                void * t299 = GM_LD(void *, (((uint8_t *)t298) + 0));
                gm_t88rcucg1b7uk t300 = gm_l4;
                gm_t88rcucg1b7uk t301 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t297, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t301;
            }
            {
                void * t302 = gm_l0;
                void * t303 = (void *)(((uint8_t *)t302) + 96);
                void * t304 = gm_l0;
                void * t305 = GM_LD(void *, (((uint8_t *)t304) + 0));
                gm_t88rcucg1b7uk t306 = gm_l5;
                gm_t88rcucg1b7uk t307 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t303, t305, t306);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t307;
            }
            {
                void * t308 = gm_l0;
                void * t309 = gm_l1;
                gm_tl874t2atnlh3 t310 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t309) + 72));
                gm_t6j6smj7qoctu t311;
                void * t312 = gm_l0;
                gm_t88rcucg1b7uk t313 = gm_l24;
                gm_b16_8 t314 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t312, t313);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t311 + 0, t314);
                void * t315 = gm_l0;
                gm_t88rcucg1b7uk t316 = gm_l23;
                gm_b16_8 t317 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t315, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t311 + 16, t317);
                gm_tabb710d9mq2e t318;
                gm_f_format_ns6dben((void *)&t318, (void *)NULL, cx, NULL, &t311);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_src4vq75vtnd3bq, 48, 30);
                gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
                gm_b16_8 t320 = GM_LD(gm_b16_8, ((uint8_t *)&t319));
                gm_b0_1 t321 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t308, t310, t320);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix284;
ie284:;
ix284:;
        }
        {
            void * t322 = gm_l1;
            uint8_t t323 = 0;
            F.ret = t322;
            goto gm_fret;
        }
        goto ix275;
ie275:;
ix275:;
    }
    {
        void * t325 = gm_l3;
        gm_t88rcucg1b7uk t326 = gm_l5;
        gm_t53at80pfdus3 t327;
        gm_f_kind_jcnt4hm((void *)&t327, t325, GM_CONV(gm_csguankko4lmhd, t326));
        uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&t327) + 0));
        if (t328 != ((uint8_t)15ULL)) goto ie324;
        F.l25 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t327) + 8));
        {
            void * t330 = gm_l3;
            gm_t88rcucg1b7uk t331 = gm_l4;
            gm_t53at80pfdus3 t332;
            gm_f_kind_jcnt4hm((void *)&t332, t330, GM_CONV(gm_csguankko4lmhd, t331));
            uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&t332) + 0));
            switch (t338) { case ((uint8_t)0ULL): goto ma336; case ((uint8_t)1ULL): goto ma336; case ((uint8_t)2ULL): goto ma336; case ((uint8_t)3ULL): goto ma336; case ((uint8_t)4ULL): goto ma336; case ((uint8_t)5ULL): goto ma336; case ((uint8_t)6ULL): goto ma336; case ((uint8_t)7ULL): goto ma336; case ((uint8_t)8ULL): goto ma336; case ((uint8_t)9ULL): goto ma336; case ((uint8_t)10ULL): goto ma336; case ((uint8_t)11ULL): goto ma336; case ((uint8_t)12ULL): goto ma336; case ((uint8_t)13ULL): goto ma336; case ((uint8_t)14ULL): goto ma336; case ((uint8_t)15ULL): goto ma335; case ((uint8_t)16ULL): goto ma334; case ((uint8_t)17ULL): goto ma336; case ((uint8_t)18ULL): goto ma336; case ((uint8_t)19ULL): goto ma336; case ((uint8_t)20ULL): goto ma336; case ((uint8_t)21ULL): goto ma336; case ((uint8_t)22ULL): goto ma336; case ((uint8_t)23ULL): goto ma336; case ((uint8_t)24ULL): goto ma336; case ((uint8_t)25ULL): goto ma336; case ((uint8_t)26ULL): goto ma336; default: goto ma336; }
ma334:;
            uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&t332) + 0));
            if (t340 != ((uint8_t)16ULL)) goto mn339;
            F.l26 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t332) + 8));
            void * t342 = (void *)((uint8_t *)&F.l26);
            void * t343 = (void *)((uint8_t *)&F.l25);
            uint8_t t344 = gm_f_same_shape_m0o6aok(t342, t343);
            if (!t344) goto ie341;
            {
                uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                if (!t347) goto ie346;
                {
                    void * t349 = gm_l0;
                    void * t350 = gm_l1;
                    gm_tl874t2atnlh3 t351 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t350) + 72));
                    void * t352 = gm_l0;
                    gm_t88rcucg1b7uk t353 = gm_l4;
                    gm_b16_8 t354 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t352, t353);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t355;
                    GM_ST(gm_b16_8, ((uint8_t *)&t355 + 0), t354);
                    gm_tabb710d9mq2e t356;
                    gm_f_format_dr0nbiu((void *)&t356, (void *)NULL, cx, NULL, t355);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t356))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t356) + 8, gm_src4vq75vtnd3bq, 59, 38);
                    gm_b16_8 t357 = GM_LD(gm_b16_8, (((uint8_t *)&t356) + 8));
                    gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&t357));
                    gm_b0_1 t359 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t349, t351, t358);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t360 = gm_l1;
                    uint8_t t361 = 0;
                    F.ret = t360;
                    goto gm_fret;
                }
                goto ix346;
ie346:;
ix346:;
            }
            {
                {
                    gm_b16_8 t363 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 24) + 0));
                    gm_l27 = t363;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t365 = gm_l28;
                        uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t367 = (t365 < t366);
                        if (!t367) goto lb364;
                        {
                            uint64_t t370 = gm_l28;
                            uint64_t t371 = (uintptr_t)t370;
                            uint64_t t372 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_tiilmfd16v3fc t375 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t371 * 24));
                            gm_l29 = t375;
                            {
                                void * t378 = (void *)(((uint8_t *)&F.l25) + 24);
                                void * t379 = (void *)((uint8_t *)&gm_l29);
                                uint8_t t380 = gm_f_set_contains_8v9ep9m(t378, t379);
                                uint8_t t381 = (uint8_t)!t380;
                                if (!t381) goto ie377;
                                {
                                    void * t383 = gm_l0;
                                    void * t384 = (void *)(((uint8_t *)t383) + 192);
                                    gm_tj44oivvogvtl t385;
                                    memset((uint8_t *)&t385 + 36, 0, 4);
                                    memcpy((uint8_t *)&t385 + 0, ((uint8_t *)&gm_l29), 24);
                                    void * t386 = gm_l1;
                                    gm_tl874t2atnlh3 t387 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t386) + 72));
                                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t385 + 24, t387);
                                    gm_tftb1odc739t5 t388 = gm_f_push_3onuaru(t384, &t385);
                                    if (((((uint8_t *)&t388))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t388) + 1, gm_src4vq75vtnd3bq, 63, 59);
                                    gm_b0_1 t389 = GM_LD(gm_b0_1, (((uint8_t *)&t388) + 1));
                                }
                                goto ix377;
ie377:;
ix377:;
                            }
                            goto me373;
mn374:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 62, 17);
me373:;
                        }
lc364:;
                        {
                            uint64_t t391 = gm_l28;
                            uint64_t r392; bool o393 = __builtin_add_overflow(t391, ((uint64_t)1ULL), &r392);
                            uint64_t t394 = r392;
                            gm_l28 = t394;
                        }
                    }
lb364:;
                }
            }
            {
                gm_t88rcucg1b7uk t395 = gm_l5;
                gm_tmn7n0n9raou5 t396;
                GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t396 + 0), ((uint8_t)9ULL));
                void * t397 = gm_l1;
                gm_t8jsc4n9vp2ib t398;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t398 + 0), t396);
                GM_ST(void *, ((uint8_t *)&t398 + 8), t397);
                gm_teti411eujvf4 t399;
                memset(&t399, 0, sizeof t399);
                GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t399 + 8), t398);
                void * t400 = gm_l1;
                gm_tl874t2atnlh3 t401 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t400) + 72));
                void * t402 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t395, &t399, t401);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t403 = 0;
                F.ret = t402;
                goto gm_fret;
            }
            goto ix341;
ie341:;
ix341:;
            goto me333;
mn339:;
ma335:;
            uint8_t t405 = GM_LD(uint8_t, (((uint8_t *)&t332) + 0));
            if (t405 != ((uint8_t)15ULL)) goto mn404;
            F.l30 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t332) + 8));
            void * t407 = (void *)((uint8_t *)&F.l30);
            void * t408 = (void *)((uint8_t *)&F.l25);
            uint8_t t409 = gm_f_same_shape_m0o6aok(t407, t408);
            if (!t409) goto ie406;
            {
                gm_l31 = ((uint8_t)1);
            }
            {
                {
                    gm_b16_8 t412 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l30) + 24) + 0));
                    gm_l32 = t412;
                }
                {
                    gm_l33 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t414 = gm_l33;
                        uint64_t t415 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        uint8_t t416 = (t414 < t415);
                        if (!t416) goto lb413;
                        {
                            uint64_t t419 = gm_l33;
                            uint64_t t420 = (uintptr_t)t419;
                            uint64_t t421 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                            gm_tiilmfd16v3fc t424 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t420 * 24));
                            gm_l34 = t424;
                            {
                                void * t427 = (void *)(((uint8_t *)&F.l25) + 24);
                                void * t428 = (void *)((uint8_t *)&gm_l34);
                                uint8_t t429 = gm_f_set_contains_8v9ep9m(t427, t428);
                                uint8_t t430 = (uint8_t)!t429;
                                if (!t430) goto ie426;
                                {
                                    gm_l31 = ((uint8_t)0);
                                }
                                goto ix426;
ie426:;
ix426:;
                            }
                            goto me422;
mn423:;
                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 69, 17);
me422:;
                        }
lc413:;
                        {
                            uint64_t t433 = gm_l33;
                            uint64_t r434; bool o435 = __builtin_add_overflow(t433, ((uint64_t)1ULL), &r434);
                            uint64_t t436 = r434;
                            gm_l33 = t436;
                        }
                    }
lb413:;
                }
            }
            {
                uint8_t t438 = gm_l31;
                if (!t438) goto ie437;
                {
                    gm_t88rcucg1b7uk t440 = gm_l5;
                    gm_tmn7n0n9raou5 t441;
                    GM_ST(uint8_t, (uint8_t *)&t441 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t441 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t441 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t441 + 0), ((uint8_t)9ULL));
                    void * t442 = gm_l1;
                    gm_t8jsc4n9vp2ib t443;
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t443 + 0), t441);
                    GM_ST(void *, ((uint8_t *)&t443 + 8), t442);
                    gm_teti411eujvf4 t444;
                    memset(&t444, 0, sizeof t444);
                    GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)32ULL));
                    GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t444 + 8), t443);
                    void * t445 = gm_l1;
                    gm_tl874t2atnlh3 t446 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t445) + 72));
                    void * t447 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t440, &t444, t446);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t448 = 0;
                    F.ret = t447;
                    goto gm_fret;
                }
                goto ix437;
ie437:;
ix437:;
            }
            goto ix406;
ie406:;
ix406:;
            goto me333;
mn404:;
ma336:;
            goto me333;
mn449:;
ma337:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 56, 9);
me333:;
        }
        goto ix324;
ie324:;
ix324:;
    }
    {
        void * t452 = gm_l0;
        gm_t88rcucg1b7uk t453 = gm_l4;
        gm_t88rcucg1b7uk t454 = gm_l5;
        uint8_t t455 = gm_f_embeds_2jpqm45(t452, GM_CONV(gm_csguankko4lmhd, t453), GM_CONV(gm_csguankko4lmhd, t454));
        if (!t455) goto ie451;
        {
            gm_t88rcucg1b7uk t457 = gm_l5;
            gm_tmn7n0n9raou5 t458;
            GM_ST(uint8_t, (uint8_t *)&t458 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t458 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t458 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t458 + 0), ((uint8_t)6ULL));
            void * t459 = gm_l1;
            gm_t8jsc4n9vp2ib t460;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t460 + 0), t458);
            GM_ST(void *, ((uint8_t *)&t460 + 8), t459);
            gm_teti411eujvf4 t461;
            memset(&t461, 0, sizeof t461);
            GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t461 + 8), t460);
            void * t462 = gm_l1;
            gm_tl874t2atnlh3 t463 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t462) + 72));
            void * t464 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t457, &t461, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t465 = 0;
            F.ret = t464;
            goto gm_fret;
        }
        goto ix451;
ie451:;
ix451:;
    }
    {
        void * t466 = gm_l0;
        gm_t88rcucg1b7uk t467 = gm_l4;
        gm_t88rcucg1b7uk t468 = gm_l5;
        gm_th2lqbo77pvsc t469;
        gm_f_coercion_steps_ibqvhta((void *)&t469, (void *)NULL, cx, NULL, t466, t467, t468);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t470;
        if (((((uint8_t *)&t469))[16] != 0)) {
            gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&t469) + 0));
            t470 = t471;
        } else {
            {
                void * t473 = gm_l0;
                void * t474 = gm_l1;
                gm_tl874t2atnlh3 t475 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t474) + 72));
                gm_t6j6smj7qoctu t476;
                void * t477 = gm_l0;
                gm_t88rcucg1b7uk t478 = gm_l5;
                gm_b16_8 t479 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t477, t478);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t476 + 0, t479);
                void * t480 = gm_l0;
                gm_t88rcucg1b7uk t481 = gm_l4;
                gm_b16_8 t482 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t480, t481);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t476 + 16, t482);
                gm_tabb710d9mq2e t483;
                gm_f_format_ns6dben((void *)&t483, (void *)NULL, cx, NULL, &t476);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t483))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t483) + 8, gm_src4vq75vtnd3bq, 81, 26);
                gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)&t483) + 8));
                gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
                gm_b0_1 t486 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t473, t475, t485);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t487 = gm_l1;
            uint8_t t488 = 0;
            F.ret = t487;
            goto gm_fret;
        }
        gm_l35 = t470;
    }
    {
        void * t489 = gm_l1;
        gm_b16_8 t490 = gm_l35;
        gm_b16_8 t491 = GM_LD(gm_b16_8, ((uint8_t *)&t490));
        void * t492 = gm_f_apply_steps_fd8cr97((void *)NULL, cx, NULL, t489, t491);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t493 = 0;
        F.ret = t492;
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
extern __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pointer_coercion_a03b7p1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* se */
    uint8_t gm_l6; /* sk */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* p */
    uint8_t gm_l9; /* c */
    gm_t88rcucg1b7uk gm_l10; /* p */
    uint8_t gm_l11; /* c */
    gm_t88rcucg1b7uk gm_l12; /* p */
    uint8_t gm_l13; /* tc */
    gm_t88rcucg1b7uk gm_l14; /* te */
    uint8_t gm_l15; /* tk */
    uint8_t gm_l16; /* c */
    gm_t88rcucg1b7uk gm_l17; /* p */
    uint8_t gm_l18; /* c */
    gm_t88rcucg1b7uk gm_l19; /* p */
    uint8_t gm_l20; /* c */
    gm_t88rcucg1b7uk gm_l21; /* p */
    gm_tkn42u7na3t81 gm_l22; /* array_elem */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    GM_STACK_CHECK_SHALLOW(&F, 590u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l3 = t3;
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_l5 = t4;
    }
    {
        gm_l6 = ((uint8_t)0ULL);
    }
    {
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t16 != ((uint8_t)8ULL)) goto mn15;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l7 = t17;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l8 = t18;
        {
            uint8_t t20 = gm_l7;
            gm_l4 = t20;
        }
        {
            gm_t88rcucg1b7uk t21 = gm_l8;
            gm_l5 = t21;
        }
        {
            gm_l6 = ((uint8_t)0ULL);
        }
        goto me8;
mn15:;
ma10:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t23 != ((uint8_t)9ULL)) goto mn22;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l9 = t24;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l10 = t25;
        {
            uint8_t t27 = gm_l9;
            gm_l4 = t27;
        }
        {
            gm_t88rcucg1b7uk t28 = gm_l10;
            gm_l5 = t28;
        }
        {
            gm_l6 = ((uint8_t)1ULL);
        }
        goto me8;
mn22:;
ma11:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t30 != ((uint8_t)10ULL)) goto mn29;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l11 = t31;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t7) + 8) + 4));
        gm_l12 = t32;
        {
            uint8_t t34 = gm_l11;
            gm_l4 = t34;
        }
        {
            gm_t88rcucg1b7uk t35 = gm_l12;
            gm_l5 = t35;
        }
        {
            gm_l6 = ((uint8_t)2ULL);
        }
        goto me8;
mn29:;
ma12:;
        uint8_t t37 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me8;
mn36:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 215, 5);
me8:;
    }
    {
        void * t39 = gm_l3;
        gm_t88rcucg1b7uk t40 = gm_l2;
        gm_t53at80pfdus3 t41;
        gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
        uint8_t t42 = ((GM_LD(uint8_t, ((uint8_t *)&t41) + 0) == ((uint8_t)7ULL)));
        if (!t42) goto ie38;
        {
            uint8_t t44 = gm_l6;
            uint8_t t45 = (!(t44 == ((uint8_t)2ULL)));
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_l13 = ((uint8_t)0);
    }
    {
        gm_t88rcucg1b7uk t47 = gm_l2;
        gm_l14 = t47;
    }
    {
        gm_l15 = ((uint8_t)0ULL);
    }
    {
        void * t48 = gm_l3;
        gm_t88rcucg1b7uk t49 = gm_l2;
        gm_t53at80pfdus3 t50;
        gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        switch (t57) { case ((uint8_t)0ULL): goto ma55; case ((uint8_t)1ULL): goto ma55; case ((uint8_t)2ULL): goto ma55; case ((uint8_t)3ULL): goto ma55; case ((uint8_t)4ULL): goto ma55; case ((uint8_t)5ULL): goto ma55; case ((uint8_t)6ULL): goto ma55; case ((uint8_t)7ULL): goto ma55; case ((uint8_t)8ULL): goto ma52; case ((uint8_t)9ULL): goto ma53; case ((uint8_t)10ULL): goto ma54; case ((uint8_t)11ULL): goto ma55; case ((uint8_t)12ULL): goto ma55; case ((uint8_t)13ULL): goto ma55; case ((uint8_t)14ULL): goto ma55; case ((uint8_t)15ULL): goto ma55; case ((uint8_t)16ULL): goto ma55; case ((uint8_t)17ULL): goto ma55; case ((uint8_t)18ULL): goto ma55; case ((uint8_t)19ULL): goto ma55; case ((uint8_t)20ULL): goto ma55; case ((uint8_t)21ULL): goto ma55; case ((uint8_t)22ULL): goto ma55; case ((uint8_t)23ULL): goto ma55; case ((uint8_t)24ULL): goto ma55; case ((uint8_t)25ULL): goto ma55; case ((uint8_t)26ULL): goto ma55; default: goto ma55; }
ma52:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t59 != ((uint8_t)8ULL)) goto mn58;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        gm_l16 = t60;
        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t50) + 8) + 4));
        gm_l17 = t61;
        {
            uint8_t t63 = gm_l16;
            gm_l13 = t63;
        }
        {
            gm_t88rcucg1b7uk t64 = gm_l17;
            gm_l14 = t64;
        }
        {
            gm_l15 = ((uint8_t)0ULL);
        }
        goto me51;
mn58:;
ma53:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t66 != ((uint8_t)9ULL)) goto mn65;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        gm_l18 = t67;
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t50) + 8) + 4));
        gm_l19 = t68;
        {
            uint8_t t70 = gm_l18;
            gm_l13 = t70;
        }
        {
            gm_t88rcucg1b7uk t71 = gm_l19;
            gm_l14 = t71;
        }
        {
            gm_l15 = ((uint8_t)1ULL);
        }
        goto me51;
mn65:;
ma54:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t73 != ((uint8_t)10ULL)) goto mn72;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&t50) + 8) + 0));
        gm_l20 = t74;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t50) + 8) + 4));
        gm_l21 = t75;
        {
            uint8_t t77 = gm_l20;
            gm_l13 = t77;
        }
        {
            gm_t88rcucg1b7uk t78 = gm_l21;
            gm_l14 = t78;
        }
        {
            gm_l15 = ((uint8_t)2ULL);
        }
        goto me51;
mn72:;
ma55:;
        uint8_t t80 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me51;
mn79:;
ma56:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 237, 5);
me51:;
    }
    {
        uint8_t t82 = gm_l4;
        if (!t82) goto ie81;
        uint8_t t83 = gm_l13;
        uint8_t t84 = (uint8_t)!t83;
        if (!t84) goto ie81;
        {
            uint8_t t86 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        uint8_t t88 = gm_l6;
        uint8_t t89 = gm_l15;
        uint8_t t90 = ((t88 == t89));
        if (!t90) goto ie87;
        {
            gm_t88rcucg1b7uk t92 = gm_l5;
            gm_t88rcucg1b7uk t93 = gm_l14;
            uint8_t t94 = (gm_eq9m6mvtmgde(((uint8_t *)&t92), ((uint8_t *)&t93)));
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        gm_tkn42u7na3t81 t96;
        memset(&t96, 0, sizeof t96);
        gm_l22 = t96;
    }
    {
        void * t97 = gm_l3;
        gm_t88rcucg1b7uk t98 = gm_l5;
        gm_t53at80pfdus3 t99;
        gm_f_kind_jcnt4hm((void *)&t99, t97, GM_CONV(gm_csguankko4lmhd, t98));
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t99) + 0));
        switch (t105) { case ((uint8_t)0ULL): goto ma103; case ((uint8_t)1ULL): goto ma103; case ((uint8_t)2ULL): goto ma103; case ((uint8_t)3ULL): goto ma103; case ((uint8_t)4ULL): goto ma103; case ((uint8_t)5ULL): goto ma103; case ((uint8_t)6ULL): goto ma103; case ((uint8_t)7ULL): goto ma103; case ((uint8_t)8ULL): goto ma103; case ((uint8_t)9ULL): goto ma103; case ((uint8_t)10ULL): goto ma103; case ((uint8_t)11ULL): goto ma101; case ((uint8_t)12ULL): goto ma103; case ((uint8_t)13ULL): goto ma103; case ((uint8_t)14ULL): goto ma103; case ((uint8_t)15ULL): goto ma103; case ((uint8_t)16ULL): goto ma103; case ((uint8_t)17ULL): goto ma103; case ((uint8_t)18ULL): goto ma103; case ((uint8_t)19ULL): goto ma103; case ((uint8_t)20ULL): goto ma102; case ((uint8_t)21ULL): goto ma103; case ((uint8_t)22ULL): goto ma103; case ((uint8_t)23ULL): goto ma103; case ((uint8_t)24ULL): goto ma103; case ((uint8_t)25ULL): goto ma103; case ((uint8_t)26ULL): goto ma103; default: goto ma103; }
ma101:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&t99) + 0));
        if (t107 != ((uint8_t)11ULL)) goto mn106;
        gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t99) + 8) + 8));
        gm_l23 = t108;
        void * t110 = gm_l3;
        gm_t88rcucg1b7uk t111 = gm_l5;
        uint8_t t112 = gm_f_src_array_ok_og10umr(t110, GM_CONV(gm_csguankko4lmhd, t111));
        if (!t112) goto ie109;
        {
            gm_t88rcucg1b7uk t114 = gm_l23;
            gm_tkn42u7na3t81 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t115), t114);
            ((uint8_t *)&t115)[4] = 1;
            gm_l22 = t115;
        }
        goto ix109;
ie109:;
ix109:;
        goto me100;
mn106:;
ma102:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t99) + 0));
        if (t117 != ((uint8_t)20ULL)) goto mn116;
        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t99) + 8) + 4));
        gm_l24 = t118;
        {
            gm_t88rcucg1b7uk t120 = gm_l24;
            gm_tkn42u7na3t81 t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t121), t120);
            ((uint8_t *)&t121)[4] = 1;
            gm_l22 = t121;
        }
        goto me100;
mn116:;
ma103:;
        goto me100;
mn122:;
ma104:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 259, 5);
me100:;
    }
    {
        uint8_t t125 = gm_l6;
        uint8_t t126 = ((t125 == ((uint8_t)0ULL)));
        if (!t126) goto ie124;
        uint8_t t127 = gm_l15;
        uint8_t t128 = ((t127 == ((uint8_t)1ULL)));
        if (!t128) goto ie124;
        {
            uint8_t t130 = 0;
            gm_t88rcucg1b7uk t131 = gm_l5;
            gm_t88rcucg1b7uk t132 = gm_l14;
            uint8_t t133 = (gm_eq9m6mvtmgde(((uint8_t *)&t131), ((uint8_t *)&t132)));
            if (!t133) {
                gm_tkn42u7na3t81 t134 = gm_l22;
                gm_t88rcucg1b7uk t135 = gm_l14;
                gm_tkn42u7na3t81 t136;
                GM_ST(uint8_t, (uint8_t *)&t136 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t136 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136), t135);
                ((uint8_t *)&t136)[4] = 1;
                uint8_t t137 = (gm_eqs65r2l0loh(((uint8_t *)&t134), ((uint8_t *)&t136)));
                t130 = t137;
            } else t130 = 1;
            uint8_t t138 = 0;
            F.ret = t130;
            goto gm_fret;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        uint8_t t140 = gm_l6;
        uint8_t t141 = ((t140 == ((uint8_t)0ULL)));
        if (!t141) goto ie139;
        uint8_t t142 = gm_l15;
        uint8_t t143 = ((t142 == ((uint8_t)2ULL)));
        if (!t143) goto ie139;
        {
            gm_tkn42u7na3t81 t145 = gm_l22;
            gm_t88rcucg1b7uk t146 = gm_l14;
            gm_tkn42u7na3t81 t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[4] = 1;
            uint8_t t148 = (gm_eqs65r2l0loh(((uint8_t *)&t145), ((uint8_t *)&t147)));
            uint8_t t149 = 0;
            F.ret = t148;
            goto gm_fret;
        }
        goto ix139;
ie139:;
ix139:;
    }
    {
        uint8_t t150 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_shape_m0o6aok F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        void * t6 = gm_l1;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 0) + 8);
        uint8_t t8 = (!(t5 == t7));
        if (!t8) {
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 16));
            void * t11 = gm_l1;
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 16));
            uint8_t t13 = (!gm_eq9m6mvtmgde(((uint8_t *)&t10), ((uint8_t *)&t12)));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t15 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 0) + 8);
            gm_l2 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l3;
                uint64_t t21 = gm_l2;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t25));
                    gm_l4 = t28;
                    {
                        uint64_t t31 = gm_l4;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 0) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src4vq75vtnd3bq, 280, 12);
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 0)) + t32 * 4));
                        uint64_t t36 = gm_l4;
                        uint64_t t37 = (uintptr_t)t36;
                        void * t38 = gm_l1;
                        uint64_t t39 = GM_LD(uintptr_t, (((uint8_t *)t38) + 0) + 8);
                        if (t37 >= (uintptr_t)(t39)) gm_trap_index(t37, t39, gm_src4vq75vtnd3bq, 280, 31);
                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t38) + 0)) + t37 * 4));
                        uint8_t t41 = (!gm_eq9m6mvtmgde(((uint8_t *)&t35), ((uint8_t *)&t40)));
                        if (!t41) goto ie30;
                        {
                            uint8_t t43 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 279, 5);
me26:;
                }
lc19:;
                {
                    uint64_t t45 = gm_l3;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l3 = t48;
                }
            }
lb19:;
        }
    }
    {
        uint8_t t49 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3) {
    struct gm_fr_steps_for_768gb3b F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_6canco6((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        uint8_t t6 = gm_l3;
        void * t7 = (void *)((uint8_t *)&F.l4);
        uint8_t t8 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t3, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_th2lqbo77pvsc t11;
        memset(&t11, 0, sizeof t11);
        uint8_t t12 = 0;
        F.ret = t11;
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
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4) {
    struct gm_fr_steps_into_vts72l5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    void * gm_l4; /* out */
    gm_tmn7n0n9raou5 gm_l5; /* k */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* o1 */
    gm_t88rcucg1b7uk gm_l9; /* f */
    gm_t88rcucg1b7uk gm_l10; /* o2 */
    uint32_t gm_l11; /* i */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    GM_STACK_CHECK(&F, 638u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        uint8_t t5 = (gm_eq9m6mvtmgde(((uint8_t *)&t3), ((uint8_t *)&t4)));
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_tbgiuvo2j5vcs t13 = gm_f_base_coercion_t3tgsf0((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t13))[8] != 0)) goto ie8;
        gm_tmn7n0n9raou5 t14 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t13));
        gm_l5 = t14;
        {
            void * t16 = gm_l4;
            gm_tmn7n0n9raou5 t17 = gm_l5;
            gm_t88rcucg1b7uk t18 = gm_l2;
            gm_tm7p68clol9c3 t19;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t19 + 0), t17);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19 + 8), t18);
            gm_tftb1odc739t5 t20 = gm_f_push_f21bjke(t16, t19);
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src4vq75vtnd3bq, 108, 9);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        {
            uint8_t t22 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        gm_l6 = t25;
    }
    {
        uint8_t t27 = gm_l3;
        if (!t27) goto ie26;
        void * t28 = gm_l6;
        gm_t88rcucg1b7uk t29 = gm_l1;
        gm_t53at80pfdus3 t30;
        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
        if (t31 != ((uint8_t)14ULL)) goto ie26;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 0));
        gm_l7 = t32;
        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t30) + 8) + 4));
        gm_l8 = t33;
        void * t34 = gm_l6;
        gm_t88rcucg1b7uk t35 = gm_l2;
        gm_t53at80pfdus3 t36;
        gm_f_kind_jcnt4hm((void *)&t36, t34, GM_CONV(gm_csguankko4lmhd, t35));
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
        if (t37 != ((uint8_t)14ULL)) goto ie26;
        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 0));
        gm_l9 = t38;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 4));
        gm_l10 = t39;
        gm_t88rcucg1b7uk t40 = gm_l8;
        gm_t88rcucg1b7uk t41 = gm_l10;
        uint8_t t42 = (gm_eq9m6mvtmgde(((uint8_t *)&t40), ((uint8_t *)&t41)));
        if (!t42) goto ie26;
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l7;
            gm_t88rcucg1b7uk t47 = gm_l9;
            gm_tvbp5g7fh5fh0 t48;
            gm_f_lift_variant_ptko7np((void *)&t48, (void *)NULL, cx, NULL, t45, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
            if (t49 != ((uint8_t)0ULL)) goto ie44;
            uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&t48) + 8));
            gm_l11 = t50;
            {
                void * t52 = gm_l4;
                uint32_t t53 = gm_l11;
                gm_tmn7n0n9raou5 t54;
                GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)4ULL));
                GM_ST(uint32_t, ((uint8_t *)&t54 + 4), t53);
                gm_t88rcucg1b7uk t55 = gm_l2;
                gm_tm7p68clol9c3 t56;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t56 + 0), t54);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56 + 8), t55);
                gm_tftb1odc739t5 t57 = gm_f_push_f21bjke(t52, t56);
                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_src4vq75vtnd3bq, 115, 13);
                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
            }
            {
                uint8_t t59 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix44;
ie44:;
ix44:;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t60 = gm_l6;
        gm_t88rcucg1b7uk t61 = gm_l2;
        gm_t53at80pfdus3 t62;
        gm_f_kind_jcnt4hm((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
        switch (t68) { case ((uint8_t)0ULL): goto ma66; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma66; case ((uint8_t)4ULL): goto ma66; case ((uint8_t)5ULL): goto ma66; case ((uint8_t)6ULL): goto ma66; case ((uint8_t)7ULL): goto ma66; case ((uint8_t)8ULL): goto ma66; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma66; case ((uint8_t)11ULL): goto ma66; case ((uint8_t)12ULL): goto ma66; case ((uint8_t)13ULL): goto ma64; case ((uint8_t)14ULL): goto ma65; case ((uint8_t)15ULL): goto ma66; case ((uint8_t)16ULL): goto ma66; case ((uint8_t)17ULL): goto ma66; case ((uint8_t)18ULL): goto ma66; case ((uint8_t)19ULL): goto ma66; case ((uint8_t)20ULL): goto ma66; case ((uint8_t)21ULL): goto ma66; case ((uint8_t)22ULL): goto ma66; case ((uint8_t)23ULL): goto ma66; case ((uint8_t)24ULL): goto ma66; case ((uint8_t)25ULL): goto ma66; case ((uint8_t)26ULL): goto ma66; default: goto ma66; }
ma64:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
        if (t70 != ((uint8_t)13ULL)) goto mn69;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t62) + 8));
        gm_l12 = t71;
        {
            void * t74 = gm_l0;
            gm_t88rcucg1b7uk t75 = gm_l1;
            gm_t88rcucg1b7uk t76 = gm_l12;
            uint8_t t77 = gm_l3;
            void * t78 = gm_l4;
            uint8_t t79 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t74, t75, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t80 = (uint8_t)!t79;
            if (!t80) goto ie73;
            {
                uint8_t t82 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix73;
ie73:;
ix73:;
        }
        {
            void * t83 = gm_l4;
            gm_tmn7n0n9raou5 t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t85 = gm_l2;
            gm_tm7p68clol9c3 t86;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t86 + 0), t84);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t86 + 8), t85);
            gm_tftb1odc739t5 t87 = gm_f_push_f21bjke(t83, t86);
            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_src4vq75vtnd3bq, 122, 13);
            gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
        }
        {
            uint8_t t89 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me63;
mn69:;
ma65:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
        if (t91 != ((uint8_t)14ULL)) goto mn90;
        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t62) + 8) + 4));
        gm_l13 = t92;
        {
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l1;
            gm_t88rcucg1b7uk t97 = gm_l13;
            uint8_t t98 = gm_l3;
            void * t99 = gm_l4;
            uint8_t t100 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t95, t96, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t101 = (uint8_t)!t100;
            if (!t101) goto ie94;
            {
                uint8_t t103 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            void * t104 = gm_l4;
            gm_tmn7n0n9raou5 t105;
            GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)3ULL));
            gm_t88rcucg1b7uk t106 = gm_l2;
            gm_tm7p68clol9c3 t107;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t107 + 0), t105);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t107 + 8), t106);
            gm_tftb1odc739t5 t108 = gm_f_push_f21bjke(t104, t107);
            if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src4vq75vtnd3bq, 127, 13);
            gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
        }
        {
            uint8_t t110 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me63;
mn90:;
ma66:;
        uint8_t t112 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me63;
mn111:;
ma67:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 119, 5);
me63:;
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
