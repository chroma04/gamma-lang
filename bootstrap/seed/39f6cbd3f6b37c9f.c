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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t2lqu8jsj511d a[3]; } gm_t8o2b5qnbhu2s;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
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
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 a[27]; } gm_tlg1vmfbpbvtq;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; } gm_t6htphat2tk14;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t8o2b5qnbhu2s) == 48 && _Alignof(gm_t8o2b5qnbhu2s) == 8 && offsetof(gm_t8o2b5qnbhu2s, a) == 0, "layout of `[3](u64, u64)`");
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlg1vmfbpbvtq) == 432 && _Alignof(gm_tlg1vmfbpbvtq) == 8 && offsetof(gm_tlg1vmfbpbvtq, a) == 0, "layout of `[27]string`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t6htphat2tk14) == 8 && _Alignof(gm_t6htphat2tk14) == 1 && offsetof(gm_t6htphat2tk14, f0) == 0 && offsetof(gm_t6htphat2tk14, f1) == 2 && offsetof(gm_t6htphat2tk14, f2) == 4 && offsetof(gm_t6htphat2tk14, f3) == 6, "layout of `(BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_complex_call_8so3bss {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* args */
    gm_trqfvmjtt4sij l10; /* stmts */
    void * ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_binary_p7eerjs {
    gm_frame h;
    gm_trqfvmjtt4sij l33; /* stmts */
    gm_trqfvmjtt4sij l36; /* elems */
    void * ret;
};
struct gm_fr_compound_number_elem_6d1d2ni {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compound_number_unary_4h478fs {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_core_call_values_qq2la05 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* stmts */
    gm_trqfvmjtt4sij l5; /* written */
    gm_tc3oht4ksth9d l13; /* e */
    void * ret;
};
struct gm_fr_describe_operand_509et87 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
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
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_imaginary_literal_6d41gfv {
    gm_frame h;
    void * ret;
};
struct gm_fr_is_complex_var_bk0jb4c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_compound_number_kbipjl5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_math_vector_09mtuao {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_var_3sb5sti {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_math_elem_iuerhch {
    gm_frame h;
    void * ret;
};
struct gm_fr_math_vector_conversion_0cttjm0 {
    gm_frame h;
    gm_trqfvmjtt4sij l12; /* stmts */
    gm_trqfvmjtt4sij l14; /* elems */
    void * ret;
};
struct gm_fr_math_vector_method_ko2814s {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* stmts */
    gm_trqfvmjtt4sij l24; /* elems */
    gm_t8o2b5qnbhu2s l25; /* <tmp> */
    gm_trqfvmjtt4sij l34; /* elems */
    gm_trqfvmjtt4sij l39; /* elems */
    void * ret;
};
struct gm_fr_mentions_complex_constan_lrgkqac {
    gm_frame h;
    gm_t5vbkdq81qfs7 l7; /* r */
    gm_t0ialelq3ql1k l8; /* v */
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_plain_31j25h8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_promote_real_constant_v0nhili {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    void * ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_t69m56meqefs8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_settle_complex_operands_9439n4u {
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
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arith_op_hsstk43(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_uts2vhe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_1oelod5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6up4rm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_86pfrlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c8fgv1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gdra5k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gmfdekn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_iepqo20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jn4ifp1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ko5t8rr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mcgdakk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p0j9lf7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s7bl5bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_unf194k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uubl3s3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_vq3bur7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_fnepb5f(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_new_var_up2m1nq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_untyped_complex_20agrmu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s26md5sqfb6u7[7];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s4bo4cvutg59h[16];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s6fdn1h3c830p[5];
extern const uint8_t gm_s7sn5glap1gkf[7];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd7vgd2pm42gk[14];
extern const uint8_t gm_sdib2k2ql9mgl[9];
extern const uint8_t gm_sf372rql24e3n[17];
extern const uint8_t gm_sfa8f3bk569gj[26];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sgvnpogt67see[6];
extern const uint8_t gm_sjsojd1o1489e[34];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_sp66nadh171v3[19];
extern const uint8_t gm_sprospqi1g16o[7];
extern const uint8_t gm_sq5sjasaqn114[7];
extern const uint8_t gm_sr486u97qk9sk[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr7qn5gtrg5md[114];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srci09ctgf0djoe[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssnn9u3skptri[75];
extern const uint8_t gm_su3gef13fgq4c[7];
extern const uint8_t gm_sump4nk3r1lgv[8];
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
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_complex_var_bk0jb4c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 96);
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_taari1s0m51a7 t7 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t2, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t9;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[1] = 1;
    uint8_t t10 = (gm_eq5307j5k9ms(((uint8_t *)&t7), ((uint8_t *)&t9)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)3ULL)) goto mn6;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_math_vector_09mtuao(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_math_vector_09mtuao F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_math_vector_parts_49c7865((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    if (((((uint8_t *)&t3))[16] != 0)) goto mn6;
    t4 = ((uint8_t)0);
    goto me5;
mn6:;
    t4 = ((uint8_t)1);
    goto me5;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 26, 62);
me5:;
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_plain_31j25h8 F;
    void * gm_l0; /* ts */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)11ULL)) goto mn6;
    uint8_t t8 = GM_LD(uint8_t, (((((uint8_t *)&t3) + 8) + 12) + 0));
    if (t8 != ((uint8_t)0ULL)) goto mn6;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 430, 53);
me5:;
    F.ret = t4;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
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
            gm_b16_8 t11 = gm_f_empty_slice_qm5tp1g();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_emnjbt3(t10, ((uint64_t)1ULL), t12);
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
        gm_tpm220j5qhjke t21 = F.l1;
        GM_ST(gm_tpm220j5qhjke, ((uint8_t *)t20 + t18 * 48), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_vj3ckdj_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h08har5_2(t10, ((uint64_t)1ULL), t12);
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
        gm_trcmmd12cefb1 t21 = gm_l1;
        GM_ST(gm_trcmmd12cefb1, ((uint8_t *)t20 + t18 * 32), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
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
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_61fb705_2(t10, ((uint64_t)1ULL), t12);
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
        gm_t69m56meqefs8 t21 = F.l1;
        GM_ST(gm_t69m56meqefs8, ((uint8_t *)t20 + t18 * 88), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 88)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
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
            gm_b16_8 t11 = gm_f_empty_slice_fa4sjvq();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_08uc6h5(t10, ((uint64_t)1ULL), t12);
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
        gm_t7te2tdpebbt4 t21 = F.l1;
        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)t20 + t18 * 144), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 144)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_fv6i5s8();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_5719f1b(t10, ((uint64_t)1ULL), t12);
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
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_zero_s25l990((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1);
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
__attribute__((aligned(4))) void * gm_f_complex_call_8so3bss(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_complex_call_8so3bss F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* args0 */
    uint8_t gm_l3; /* swap */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* f */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* a */
    void * gm_l11; /* first */
    gm_t5fru0lp11n7t gm_l12; /* <temporary array> */
    void * gm_l13; /* call */
    GM_STACK_CHECK(&F, 736u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sump4nk3r1lgv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_core_fn_uts2vhe((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            {
                void * t9 = gm_l0;
                gm_tl874t2atnlh3 t10 = gm_l4;
                gm_b16_8 t11 = gm_l1;
                gm_tnpifv20irnph t12;
                GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
                gm_tabb710d9mq2e t13;
                gm_f_format_unf194k((void *)&t13, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srci09ctgf0djoe, 298, 24);
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
                gm_b0_1 t16 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t9, t10, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l5 = t6;
    }
    {
        gm_f_list_s1kpkpr((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t23 = gm_l2;
            gm_l7 = t23;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l8;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l8;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    void * t33 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t31 * 8));
                    void * t36 = GM_LD(void *, ((uint8_t *)&t33));
                    gm_l9 = t36;
                    {
                        void * t38 = (void *)((uint8_t *)&F.l6);
                        void * t39 = gm_l9;
                        gm_tftb1odc739t5 t40 = gm_f_push_pf02nv6(t38, t39);
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srci09ctgf0djoe, 302, 22);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 302, 5);
me34:;
                }
lc24:;
                {
                    uint64_t t43 = gm_l8;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l8 = t46;
                }
            }
lb24:;
        }
    }
    {
        uint8_t t48 = gm_l3;
        if (!t48) goto ie47;
        {
            gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t50 = gm_l0;
            uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srci09ctgf0djoe, 306, 31);
            void * t53 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t51 * 8));
            void * t54 = (void *)((uint8_t *)&F.l10);
            void * t55 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t50, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t55;
        }
        {
        }
        {
            void * t56 = gm_l0;
            uint32_t t57 = gm_l5;
            {
                uint64_t t59 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srci09ctgf0djoe, 307, 49);
                void * t61 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t59 * 8));
                void * t62 = gm_l11;
                gm_t5fru0lp11n7t t63;
                GM_ST(void *, ((uint8_t *)&t63 + 0), t61);
                GM_ST(void *, ((uint8_t *)&t63 + 8), t62);
                gm_l12 = t63;
            }
            void * t64 = ((uint8_t *)&gm_l12);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 307, 40);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t64 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t66 = t65;
            gm_tl874t2atnlh3 t67 = gm_l4;
            void * t68 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t56, t57, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t68;
        }
        {
            void * t69 = gm_l13;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t72 = gm_l13;
            gm_tl874t2atnlh3 t73 = gm_l4;
            void * t74 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t70, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        void * t76 = gm_l0;
        uint32_t t77 = gm_l5;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        gm_tl874t2atnlh3 t80 = gm_l4;
        void * t81 = gm_f_core_call_values_qq2la05((void *)NULL, cx, NULL, t76, t77, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = 0;
        F.ret = t81;
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
__attribute__((aligned(4))) void * gm_f_compound_number_binary_p7eerjs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_compound_number_binary_p7eerjs F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs0 */
    void * gm_l3; /* r0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* lhs */
    void * gm_l7; /* r */
    gm_b16_8 gm_l8; /* opn */
    gm_t88rcucg1b7uk gm_l9; /* lt */
    gm_t88rcucg1b7uk gm_l10; /* rt */
    uint8_t gm_l11; /* promoted */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    void * gm_l13; /* p */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    void * gm_l15; /* p */
    uint8_t gm_l16; /* left_is_agg */
    gm_t88rcucg1b7uk gm_l17; /* agg */
    gm_t88rcucg1b7uk gm_l18; /* elem */
    gm_t88rcucg1b7uk gm_l19; /* other */
    uint8_t gm_l20; /* both */
    uint8_t gm_l21; /* complex */
    gm_b16_8 gm_l22; /* what */
    uint8_t gm_l23; /* shift */
    uint8_t gm_l24; /* scales */
    gm_b16_8 gm_l25; /* hint */
    gm_b16_8 gm_l26; /* l */
    gm_b16_8 gm_l27; /* rr */
    gm_b16_8 gm_l28; /* shown */
    gm_b16_8 gm_l29; /* name */
    uint8_t gm_l30; /* swap */
    gm_t5fru0lp11n7t gm_l31; /* <temporary array> */
    uint64_t gm_l32; /* n */
    void * gm_l34; /* l */
    void * gm_l35; /* rv */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* i */
    void * gm_l40; /* a */
    void * gm_l41; /* b */
    void * gm_l42; /* e */
    gm_t9qkh0rsooc8d gm_l43; /* f */
    gm_ts6oqppjh0ba3 gm_l44; /* top */
    void * gm_l45; /* arr */
    GM_STACK_CHECK(&F, 4130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l2;
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l3;
        gm_l7 = t6;
    }
    {
        gm_ts6oqppjh0ba3 t7 = gm_l1;
        gm_b16_8 t8 = gm_f_binop_text_0ljfc0r(t7);
        gm_l8 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_ts6oqppjh0ba3 t10 = gm_l1;
        void * t11 = gm_l6;
        void * t12 = gm_l7;
        gm_tl874t2atnlh3 t13 = gm_l4;
        gm_b0_1 t14 = gm_f_settle_complex_operands_9439n4u((void *)NULL, cx, NULL, t9, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 96);
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = gm_l6;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
        gm_t88rcucg1b7uk t21 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t16, t18, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 96);
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l7;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
        gm_t88rcucg1b7uk t28 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t23, t25, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t28;
    }
    {
        uint8_t t30 = 0;
        void * t31 = gm_l5;
        gm_t88rcucg1b7uk t32 = gm_l9;
        uint8_t t33 = gm_f_is_error_8k27gd1(t31, GM_CONV(gm_csguankko4lmhd, t32));
        if (!t33) {
            void * t34 = gm_l5;
            gm_t88rcucg1b7uk t35 = gm_l10;
            uint8_t t36 = gm_f_is_error_8k27gd1(t34, GM_CONV(gm_csguankko4lmhd, t35));
            t30 = t36;
        } else t30 = 1;
        if (!t30) goto ie29;
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            gm_tl874t2atnlh3 t40 = gm_l4;
            void * t41 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        uint8_t t44 = 0;
        uint8_t t45 = 0;
        gm_ts6oqppjh0ba3 t46 = gm_l1;
        gm_t9qkh0rsooc8d t47;
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
        uint8_t t48 = (((GM_LD(uint8_t, ((uint8_t *)&t46) + 0) == ((uint8_t)0ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t46) + 1), ((uint8_t *)&t47))));
        if (!t48) {
            gm_ts6oqppjh0ba3 t49 = gm_l1;
            gm_t9qkh0rsooc8d t50;
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
            uint8_t t51 = (((GM_LD(uint8_t, ((uint8_t *)&t49) + 0) == ((uint8_t)1ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t49) + 1), ((uint8_t *)&t50))));
            t45 = t51;
        } else t45 = 1;
        if (!t45) {
            uint8_t t52 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            t44 = t52;
        } else t44 = 1;
        if (!t44) goto ie43;
        {
            gm_l11 = ((uint8_t)0);
        }
        {
            void * t55 = gm_l0;
            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
            gm_t88rcucg1b7uk t57 = gm_l9;
            gm_tkn42u7na3t81 t58 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t58))[4] != 0)) goto ie54;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t58));
            gm_l12 = t59;
            {
                uint8_t t62 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                if (!t62) goto ie61;
                {
                    void * t65 = gm_l0;
                    void * t66 = gm_l7;
                    gm_t88rcucg1b7uk t67 = gm_l9;
                    gm_t88rcucg1b7uk t68 = gm_l12;
                    void * t69 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t65, t66, t67, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!(GM_LD(void *, ((uint8_t *)&t69)) != 0)) goto ie64;
                    void * t70 = GM_LD(void *, ((uint8_t *)&t69));
                    gm_l13 = t70;
                    {
                        void * t72 = gm_l13;
                        gm_l7 = t72;
                    }
                    {
                        gm_t88rcucg1b7uk t73 = gm_l9;
                        gm_l10 = t73;
                    }
                    {
                        gm_l11 = ((uint8_t)1);
                    }
                    goto ix64;
ie64:;
ix64:;
                }
                goto ix61;
ie61:;
ix61:;
            }
            goto ix54;
ie54:;
ix54:;
        }
        {
            uint8_t t75 = gm_l11;
            uint8_t t76 = (uint8_t)!t75;
            if (!t76) goto ie74;
            {
                void * t79 = gm_l0;
                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                gm_t88rcucg1b7uk t81 = gm_l10;
                gm_tkn42u7na3t81 t82 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t82))[4] != 0)) goto ie78;
                gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t82));
                gm_l14 = t83;
                {
                    void * t86 = gm_l0;
                    void * t87 = gm_l6;
                    gm_t88rcucg1b7uk t88 = gm_l10;
                    gm_t88rcucg1b7uk t89 = gm_l14;
                    void * t90 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t86, t87, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!(GM_LD(void *, ((uint8_t *)&t90)) != 0)) goto ie85;
                    void * t91 = GM_LD(void *, ((uint8_t *)&t90));
                    gm_l15 = t91;
                    {
                        void * t93 = gm_l15;
                        gm_l6 = t93;
                    }
                    {
                        gm_t88rcucg1b7uk t94 = gm_l10;
                        gm_l9 = t94;
                    }
                    goto ix85;
ie85:;
ix85:;
                }
                goto ix78;
ie78:;
ix78:;
            }
            goto ix74;
ie74:;
ix74:;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t95 = gm_l0;
        gm_t88rcucg1b7uk t96 = gm_l9;
        uint8_t t97 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t97;
    }
    {
        gm_t88rcucg1b7uk t98;
        uint8_t t100 = gm_l16;
        if (!t100) goto ie99;
        gm_t88rcucg1b7uk t102 = gm_l9;
        gm_t88rcucg1b7uk t103 = t102;
        t98 = t103;
        goto ix99;
ie99:;
        gm_t88rcucg1b7uk t105 = gm_l10;
        gm_t88rcucg1b7uk t106 = t105;
        t98 = t106;
ix99:;
        gm_l17 = t98;
    }
    {
        void * t107 = gm_l0;
        gm_t88rcucg1b7uk t108 = gm_l17;
        gm_tkn42u7na3t81 t109 = gm_f_compound_number_elem_6d1d2ni((void *)NULL, cx, NULL, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t110;
        if (((((uint8_t *)&t109))[4] != 0)) {
            gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t109) + 0));
            t110 = t111;
        } else {
            void * t112 = gm_l0;
            void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
            gm_tl874t2atnlh3 t114 = gm_l4;
            void * t115 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t113, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t116 = 0;
            F.ret = t115;
            goto gm_fret;
        }
        gm_l18 = t110;
    }
    {
        gm_t88rcucg1b7uk t117;
        uint8_t t119 = gm_l16;
        if (!t119) goto ie118;
        gm_t88rcucg1b7uk t121 = gm_l10;
        gm_t88rcucg1b7uk t122 = t121;
        t117 = t122;
        goto ix118;
ie118:;
        gm_t88rcucg1b7uk t124 = gm_l9;
        gm_t88rcucg1b7uk t125 = t124;
        t117 = t125;
ix118:;
        gm_l19 = t117;
    }
    {
        void * t126 = gm_l0;
        gm_t88rcucg1b7uk t127 = gm_l19;
        uint8_t t128 = gm_f_is_compound_number_kbipjl5((void *)NULL, cx, NULL, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t128;
    }
    {
        void * t129 = gm_l5;
        gm_t88rcucg1b7uk t130 = gm_l17;
        uint8_t t131 = gm_f_is_math_vector_09mtuao(t129, GM_CONV(gm_csguankko4lmhd, t130));
        uint8_t t132 = (uint8_t)!t131;
        gm_l21 = t132;
    }
    {
        gm_b16_8 t133;
        uint8_t t135 = gm_l21;
        if (!t135) goto ie134;
        gm_b16_8 t137;
        GM_ST(void *, (uint8_t *)&t137, (void *)(gm_s4bo4cvutg59h));
        GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(15));
        gm_b16_8 t138 = t137;
        t133 = t138;
        goto ix134;
ie134:;
        gm_b16_8 t140;
        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sr486u97qk9sk));
        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(12));
        gm_b16_8 t141 = t140;
        t133 = t141;
ix134:;
        gm_l22 = t133;
    }
    {
        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t146 != ((uint8_t)16ULL)) goto pa145;
        goto po144;
pa145:;
        uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t148 != ((uint8_t)17ULL)) goto pa147;
        goto po144;
pa147:;
        uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t150 != ((uint8_t)18ULL)) goto pa149;
        goto po144;
pa149:;
        uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t152 != ((uint8_t)19ULL)) goto pa151;
        goto po144;
pa151:;
        goto mn143;
po144:;
        {
            void * t154 = gm_l0;
            gm_tl874t2atnlh3 t155 = gm_l4;
            gm_b16_8 t156 = gm_l22;
            gm_tnpifv20irnph t157;
            GM_ST(gm_b16_8, ((uint8_t *)&t157 + 0), t156);
            gm_tabb710d9mq2e t158;
            gm_f_format_vq3bur7((void *)&t158, (void *)NULL, cx, NULL, t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 8, gm_srci09ctgf0djoe, 166, 28);
            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&t158) + 8));
            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
            gm_b0_1 t161 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t154, t155, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t162 = gm_l0;
            void * t163 = GM_LD(void *, (((uint8_t *)t162) + 0));
            gm_tl874t2atnlh3 t164 = gm_l4;
            void * t165 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t166 = 0;
            F.ret = t165;
            goto gm_fret;
        }
        goto me142;
mn143:;
        goto me142;
mn167:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 164, 5);
me142:;
    }
    {
        gm_ts6oqppjh0ba3 t170 = gm_l1;
        gm_t9qkh0rsooc8d t171;
        GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t172;
        GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t172 + 1), t171);
        gm_t9qkh0rsooc8d t173;
        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t174;
        GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t174 + 1), t173);
        gm_t9qkh0rsooc8d t175;
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t176;
        GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t176 + 1), t175);
        gm_t9qkh0rsooc8d t177;
        GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)3ULL));
        gm_ts6oqppjh0ba3 t178;
        GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t178 + 1), t177);
        gm_t6htphat2tk14 t179;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t179 + 0), t172);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t179 + 2), t174);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t179 + 4), t176);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t179 + 6), t178);
        uint8_t t180 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t179) + 0, ((uint8_t *)&t170))) t180 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t179) + 2, ((uint8_t *)&t170))) t180 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t179) + 4, ((uint8_t *)&t170))) t180 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t179) + 6, ((uint8_t *)&t170))) t180 = 1;
        uint8_t t181 = (uint8_t)(t180);
        if (!t181) goto ie169;
        {
            void * t183 = gm_l0;
            gm_tl874t2atnlh3 t184 = gm_l4;
            gm_t6j6smj7qoctu t185;
            gm_b16_8 t186 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t185 + 0, t186);
            gm_b16_8 t187 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t185 + 16, t187);
            gm_tabb710d9mq2e t188;
            gm_f_format_1oelod5((void *)&t188, (void *)NULL, cx, NULL, &t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_srci09ctgf0djoe, 172, 24);
            gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
            gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
            gm_b0_1 t191 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t183, t184, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t192 = gm_l0;
            void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
            gm_tl874t2atnlh3 t194 = gm_l4;
            void * t195 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t196 = 0;
            F.ret = t195;
            goto gm_fret;
        }
        goto ix169;
ie169:;
ix169:;
    }
    {
        uint8_t t197 = 0;
        uint8_t t198;
        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t201 != ((uint8_t)3ULL)) goto mn200;
        t198 = ((uint8_t)1);
        goto me199;
mn200:;
        t198 = ((uint8_t)0);
        goto me199;
mn202:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 175, 14);
me199:;
        if (!t198) {
            uint8_t t203 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)6ULL)));
            t197 = t203;
        } else t197 = 1;
        gm_l23 = t197;
    }
    {
        uint8_t t205 = gm_l21;
        if (!t205) goto ie204;
        uint8_t t206 = 0;
        uint8_t t207 = 0;
        uint8_t t208 = 0;
        gm_ts6oqppjh0ba3 t209 = gm_l1;
        gm_t9qkh0rsooc8d t210;
        GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)0ULL));
        uint8_t t211 = (((GM_LD(uint8_t, ((uint8_t *)&t209) + 0) == ((uint8_t)0ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t209) + 1), ((uint8_t *)&t210))));
        if (!t211) {
            gm_ts6oqppjh0ba3 t212 = gm_l1;
            gm_t9qkh0rsooc8d t213;
            GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)0ULL));
            uint8_t t214 = (((GM_LD(uint8_t, ((uint8_t *)&t212) + 0) == ((uint8_t)1ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t212) + 1), ((uint8_t *)&t213))));
            t208 = t214;
        } else t208 = 1;
        if (!t208) {
            gm_ts6oqppjh0ba3 t215 = gm_l1;
            gm_t9qkh0rsooc8d t216;
            GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)0ULL));
            uint8_t t217 = (((GM_LD(uint8_t, ((uint8_t *)&t215) + 0) == ((uint8_t)2ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t215) + 1), ((uint8_t *)&t216))));
            t207 = t217;
        } else t207 = 1;
        if (!t207) {
            uint8_t t218 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            t206 = t218;
        } else t206 = 1;
        uint8_t t219 = (uint8_t)!t206;
        if (!t219) goto ie204;
        {
            void * t221 = gm_l0;
            gm_tl874t2atnlh3 t222 = gm_l4;
            gm_b16_8 t223 = gm_l8;
            gm_tnpifv20irnph t224;
            GM_ST(gm_b16_8, ((uint8_t *)&t224 + 0), t223);
            gm_tabb710d9mq2e t225;
            gm_f_format_s7bl5bp((void *)&t225, (void *)NULL, cx, NULL, t224);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 8, gm_srci09ctgf0djoe, 177, 24);
            gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&t225) + 8));
            gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
            gm_b0_1 t228 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t221, t222, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t229 = gm_l0;
            void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
            gm_tl874t2atnlh3 t231 = gm_l4;
            void * t232 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t233 = 0;
            F.ret = t232;
            goto gm_fret;
        }
        goto ix204;
ie204:;
ix204:;
    }
    {
        uint8_t t235 = gm_l23;
        if (!t235) goto ie234;
        {
            uint8_t t238 = 0;
            uint8_t t239 = gm_l16;
            uint8_t t240 = (uint8_t)!t239;
            if (!t240) {
                uint8_t t241 = gm_l20;
                t238 = t241;
            } else t238 = 1;
            if (!t238) goto ie237;
            {
                void * t243 = gm_l0;
                gm_tl874t2atnlh3 t244 = gm_l4;
                gm_b16_8 t245;
                GM_ST(void *, (uint8_t *)&t245, (void *)(gm_ssnn9u3skptri));
                GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(74));
                gm_b0_1 t246 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t243, t244, t245);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t247 = gm_l0;
                void * t248 = GM_LD(void *, (((uint8_t *)t247) + 0));
                gm_tl874t2atnlh3 t249 = gm_l4;
                void * t250 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t248, t249);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t251 = 0;
                F.ret = t250;
                goto gm_fret;
            }
            goto ix237;
ie237:;
ix237:;
        }
        {
            void * t253 = gm_l0;
            void * t254 = (void *)(((uint8_t *)t253) + 96);
            void * t255 = gm_l5;
            void * t256 = gm_l7;
            gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t256) + 0));
            gm_t9qkh0rsooc8d t258;
            GM_ST(uint8_t, ((uint8_t *)&t258 + 0), ((uint8_t)1ULL));
            uint8_t t259 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t254, t255, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t260 = (uint8_t)!t259;
            if (!t260) goto ie252;
            {
                void * t262 = gm_l0;
                void * t263 = gm_l7;
                gm_tl874t2atnlh3 t264 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t263) + 72));
                void * t265 = gm_l0;
                gm_t88rcucg1b7uk t266 = gm_l10;
                gm_b16_8 t267 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t265, t266);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t268;
                GM_ST(gm_b16_8, ((uint8_t *)&t268 + 0), t267);
                gm_tabb710d9mq2e t269;
                gm_f_format_pdashts((void *)&t269, (void *)NULL, cx, NULL, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 8, gm_srci09ctgf0djoe, 187, 30);
                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                gm_b0_1 t272 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t262, t264, t271);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t273 = gm_l0;
                void * t274 = GM_LD(void *, (((uint8_t *)t273) + 0));
                gm_tl874t2atnlh3 t275 = gm_l4;
                void * t276 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t274, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t277 = 0;
                F.ret = t276;
                goto gm_fret;
            }
            goto ix252;
ie252:;
ix252:;
        }
        goto ix234;
ie234:;
        uint8_t t279 = gm_l20;
        if (!t279) goto ie278;
        {
            void * t282 = gm_l5;
            void * t283 = gm_l0;
            gm_t88rcucg1b7uk t284 = gm_l9;
            gm_t88rcucg1b7uk t285 = gm_l10;
            gm_tl874t2atnlh3 t286 = gm_l4;
            gm_b16_8 t287 = gm_l8;
            gm_tnpifv20irnph t288;
            GM_ST(gm_b16_8, ((uint8_t *)&t288 + 0), t287);
            gm_tabb710d9mq2e t289;
            gm_f_format_ko5t8rr((void *)&t289, (void *)NULL, cx, NULL, t288);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t289))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t289) + 8, gm_srci09ctgf0djoe, 191, 57);
            gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&t289) + 8));
            gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&t290));
            gm_t88rcucg1b7uk t292 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t283, t284, t285, t286, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t293 = gm_f_is_error_8k27gd1(t282, GM_CONV(gm_csguankko4lmhd, t292));
            if (!t293) goto ie281;
            {
                void * t295 = gm_l0;
                void * t296 = GM_LD(void *, (((uint8_t *)t295) + 0));
                gm_tl874t2atnlh3 t297 = gm_l4;
                void * t298 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t296, t297);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t299 = 0;
                F.ret = t298;
                goto gm_fret;
            }
            goto ix281;
ie281:;
ix281:;
        }
        goto ix278;
ie278:;
        {
            uint8_t t301 = 0;
            uint8_t t302;
            uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t305 != ((uint8_t)2ULL)) goto mn304;
            t302 = ((uint8_t)1);
            goto me303;
mn304:;
            t302 = ((uint8_t)0);
            goto me303;
mn306:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 194, 19);
me303:;
            if (!t302) {
                uint8_t t307 = 0;
                uint8_t t308 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                if (t308) {
                    uint8_t t309 = gm_l16;
                    t307 = t309;
                } else t307 = 0;
                t301 = t307;
            } else t301 = 1;
            gm_l24 = t301;
        }
        {
            uint8_t t311 = gm_l24;
            uint8_t t312 = (uint8_t)!t311;
            if (!t312) goto ie310;
            {
                gm_b16_8 t314;
                uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t320) { case ((uint8_t)0ULL): goto ma317; case ((uint8_t)1ULL): goto ma317; case ((uint8_t)2ULL): goto ma318; case ((uint8_t)3ULL): goto ma318; case ((uint8_t)4ULL): goto ma316; case ((uint8_t)5ULL): goto ma318; case ((uint8_t)6ULL): goto ma318; case ((uint8_t)7ULL): goto ma318; case ((uint8_t)8ULL): goto ma318; case ((uint8_t)9ULL): goto ma318; case ((uint8_t)10ULL): goto ma318; case ((uint8_t)11ULL): goto ma318; case ((uint8_t)12ULL): goto ma318; case ((uint8_t)13ULL): goto ma318; case ((uint8_t)14ULL): goto ma318; case ((uint8_t)15ULL): goto ma318; case ((uint8_t)16ULL): goto ma318; case ((uint8_t)17ULL): goto ma318; case ((uint8_t)18ULL): goto ma318; case ((uint8_t)19ULL): goto ma318; case ((uint8_t)20ULL): goto ma318; case ((uint8_t)21ULL): goto ma318; default: goto ma318; }
ma316:;
                uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t322 != ((uint8_t)4ULL)) goto mn321;
                gm_b16_8 t323;
                GM_ST(void *, (uint8_t *)&t323, (void *)(gm_sjsojd1o1489e));
                GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(33));
                t314 = t323;
                goto me315;
mn321:;
ma317:;
                uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t327 != ((uint8_t)0ULL)) goto pa326;
                goto po325;
pa326:;
                uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t329 != ((uint8_t)1ULL)) goto pa328;
                goto po325;
pa328:;
                goto mn324;
po325:;
                gm_b16_8 t330;
                uint8_t t332 = gm_l21;
                if (!t332) goto ie331;
                gm_b16_8 t334;
                GM_ST(void *, (uint8_t *)&t334, (void *)(gm_sr7qn5gtrg5md));
                GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(113));
                gm_b16_8 t335 = t334;
                t330 = t335;
                goto ix331;
ie331:;
                gm_b16_8 t337 = gm_l22;
                gm_tnpifv20irnph t338;
                GM_ST(gm_b16_8, ((uint8_t *)&t338 + 0), t337);
                gm_tabb710d9mq2e t339;
                gm_f_format_jn4ifp1((void *)&t339, (void *)NULL, cx, NULL, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 8, gm_srci09ctgf0djoe, 201, 21);
                gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
                gm_b16_8 t341 = t340;
                gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                t330 = t342;
ix331:;
                t314 = t330;
                goto me315;
mn324:;
ma318:;
                gm_b16_8 t344;
                GM_ST(void *, (uint8_t *)&t344, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(0));
                t314 = t344;
                goto me315;
mn343:;
ma319:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 196, 28);
me315:;
                gm_l25 = t314;
            }
            {
                void * t345 = gm_l0;
                gm_t88rcucg1b7uk t346 = gm_l9;
                gm_b16_8 t347 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t345, t346);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t347;
            }
            {
                void * t348 = gm_l0;
                gm_t88rcucg1b7uk t349 = gm_l10;
                gm_b16_8 t350 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t348, t349);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t350;
            }
            {
                void * t351 = gm_l0;
                gm_tl874t2atnlh3 t352 = gm_l4;
                gm_tud7oovfvnf43 t353;
                gm_b16_8 t354 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t353 + 0, t354);
                void * t355 = gm_l0;
                gm_t88rcucg1b7uk t356 = gm_l17;
                gm_b16_8 t357 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t355, t356);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t353 + 16, t357);
                gm_b16_8 t358 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t353 + 32, t358);
                gm_b16_8 t359 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t353 + 48, t359);
                gm_b16_8 t360 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t353 + 64, t360);
                gm_tabb710d9mq2e t361;
                gm_f_format_p0j9lf7((void *)&t361, (void *)NULL, cx, NULL, &t353);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 8, gm_srci09ctgf0djoe, 207, 28);
                gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&t361) + 8));
                gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
                gm_b0_1 t364 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t351, t352, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t365 = gm_l0;
                void * t366 = GM_LD(void *, (((uint8_t *)t365) + 0));
                gm_tl874t2atnlh3 t367 = gm_l4;
                void * t368 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t366, t367);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t369 = 0;
                F.ret = t368;
                goto gm_fret;
            }
            goto ix310;
ie310:;
ix310:;
        }
        {
            void * t371 = gm_l0;
            void * t372 = (void *)(((uint8_t *)t371) + 96);
            void * t373 = gm_l5;
            gm_t88rcucg1b7uk t374 = gm_l19;
            gm_t88rcucg1b7uk t375 = gm_l18;
            gm_t9qkh0rsooc8d t376 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t372, t373, t374, t375);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t377;
            if (!((((uint8_t *)&t376))[0] != 0)) goto mn379;
            t377 = ((uint8_t)1);
            goto me378;
mn379:;
            t377 = ((uint8_t)0);
            goto me378;
mn380:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 210, 12);
me378:;
            if (!t377) goto ie370;
            {
                void * t382 = gm_l0;
                void * t383 = gm_l0;
                void * t384 = (void *)(((uint8_t *)t383) + 96);
                void * t385 = gm_l0;
                void * t386 = GM_LD(void *, (((uint8_t *)t385) + 0));
                gm_t88rcucg1b7uk t387 = gm_l19;
                gm_t88rcucg1b7uk t388 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t384, t386, t387);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t389 = gm_f_describe_operand_509et87((void *)NULL, cx, NULL, t382, t388);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t389;
            }
            {
                void * t390 = gm_l0;
                gm_tl874t2atnlh3 t391 = gm_l4;
                gm_t0uuhaljsf410 t392;
                gm_b16_8 t393 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t392 + 0, t393);
                void * t394 = gm_l0;
                gm_t88rcucg1b7uk t395 = gm_l17;
                gm_b16_8 t396 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t394, t395);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t392 + 16, t396);
                void * t397 = gm_l0;
                gm_t88rcucg1b7uk t398 = gm_l18;
                gm_b16_8 t399 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t397, t398);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t392 + 32, t399);
                gm_b16_8 t400 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t392 + 48, t400);
                gm_tabb710d9mq2e t401;
                gm_f_format_davqh84((void *)&t401, (void *)NULL, cx, NULL, &t392);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t401))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t401) + 8, gm_srci09ctgf0djoe, 212, 28);
                gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)&t401) + 8));
                gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
                gm_b0_1 t404 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t390, t391, t403);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t405 = gm_l0;
                void * t406 = GM_LD(void *, (((uint8_t *)t405) + 0));
                gm_tl874t2atnlh3 t407 = gm_l4;
                void * t408 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t406, t407);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t409 = 0;
                F.ret = t408;
                goto gm_fret;
            }
            goto ix370;
ie370:;
ix370:;
        }
ix278:;
ix234:;
    }
    {
        uint8_t t411 = gm_l21;
        if (!t411) goto ie410;
        {
            gm_b16_8 t413;
            uint8_t t420 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t420) { case ((uint8_t)0ULL): goto ma415; case ((uint8_t)1ULL): goto ma416; case ((uint8_t)2ULL): goto ma417; case ((uint8_t)3ULL): goto ma418; case ((uint8_t)4ULL): goto ma418; case ((uint8_t)5ULL): goto ma418; case ((uint8_t)6ULL): goto ma418; case ((uint8_t)7ULL): goto ma418; case ((uint8_t)8ULL): goto ma418; case ((uint8_t)9ULL): goto ma418; case ((uint8_t)10ULL): goto ma418; case ((uint8_t)11ULL): goto ma418; case ((uint8_t)12ULL): goto ma418; case ((uint8_t)13ULL): goto ma418; case ((uint8_t)14ULL): goto ma418; case ((uint8_t)15ULL): goto ma418; case ((uint8_t)16ULL): goto ma418; case ((uint8_t)17ULL): goto ma418; case ((uint8_t)18ULL): goto ma418; case ((uint8_t)19ULL): goto ma418; case ((uint8_t)20ULL): goto ma418; case ((uint8_t)21ULL): goto ma418; default: goto ma418; }
ma415:;
            uint8_t t422 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t422 != ((uint8_t)0ULL)) goto mn421;
            gm_b16_8 t423;
            GM_ST(void *, (uint8_t *)&t423, (void *)(gm_s26md5sqfb6u7));
            GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(6));
            t413 = t423;
            goto me414;
mn421:;
ma416:;
            uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t425 != ((uint8_t)1ULL)) goto mn424;
            gm_b16_8 t426;
            GM_ST(void *, (uint8_t *)&t426, (void *)(gm_s7sn5glap1gkf));
            GM_ST(uintptr_t, (uint8_t *)&t426 + 8, (uintptr_t)(6));
            t413 = t426;
            goto me414;
mn424:;
ma417:;
            uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t428 != ((uint8_t)2ULL)) goto mn427;
            gm_b16_8 t429;
            uint8_t t431 = gm_l20;
            if (!t431) goto ie430;
            gm_b16_8 t433;
            GM_ST(void *, (uint8_t *)&t433, (void *)(gm_su3gef13fgq4c));
            GM_ST(uintptr_t, (uint8_t *)&t433 + 8, (uintptr_t)(6));
            gm_b16_8 t434 = t433;
            t429 = t434;
            goto ix430;
ie430:;
            gm_b16_8 t436;
            GM_ST(void *, (uint8_t *)&t436, (void *)(gm_sdib2k2ql9mgl));
            GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(8));
            gm_b16_8 t437 = t436;
            t429 = t437;
ix430:;
            t413 = t429;
            goto me414;
mn427:;
ma418:;
            gm_b16_8 t439;
            uint8_t t441 = gm_l20;
            if (!t441) goto ie440;
            gm_b16_8 t443;
            GM_ST(void *, (uint8_t *)&t443, (void *)(gm_sq5sjasaqn114));
            GM_ST(uintptr_t, (uint8_t *)&t443 + 8, (uintptr_t)(6));
            gm_b16_8 t444 = t443;
            t439 = t444;
            goto ix440;
ie440:;
            gm_b16_8 t446;
            GM_ST(void *, (uint8_t *)&t446, (void *)(gm_sd7vgd2pm42gk));
            GM_ST(uintptr_t, (uint8_t *)&t446 + 8, (uintptr_t)(13));
            gm_b16_8 t447 = t446;
            t439 = t447;
ix440:;
            t413 = t439;
            goto me414;
mn438:;
ma419:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 217, 24);
me414:;
            gm_l29 = t413;
        }
        {
            uint8_t t448 = 0;
            uint8_t t449 = gm_l20;
            uint8_t t450 = (uint8_t)!t449;
            if (t450) {
                uint8_t t451 = gm_l16;
                uint8_t t452 = (uint8_t)!t451;
                t448 = t452;
            } else t448 = 0;
            gm_l30 = t448;
        }
        {
        }
        {
            void * t453 = gm_l0;
            gm_b16_8 t454 = gm_l29;
            {
                void * t456 = gm_l6;
                void * t457 = gm_l7;
                gm_t5fru0lp11n7t t458;
                GM_ST(void *, ((uint8_t *)&t458 + 0), t456);
                GM_ST(void *, ((uint8_t *)&t458 + 8), t457);
                gm_l31 = t458;
            }
            void * t459 = ((uint8_t *)&gm_l31);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 225, 38);
            gm_b16_8 t460;
            GM_ST(void *, (uint8_t *)&t460, (void *)(((uint8_t *)t459 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t461 = t460;
            uint8_t t462 = gm_l30;
            gm_tl874t2atnlh3 t463 = gm_l4;
            void * t464 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t453, t454, t461, t462, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t465 = 0;
            F.ret = t464;
            goto gm_fret;
        }
        goto ix410;
ie410:;
ix410:;
    }
    {
        void * t466 = gm_l0;
        void * t467 = GM_LD(void *, (((uint8_t *)t466) + 0));
        void * t468 = (void *)(((uint8_t *)t467) + 8);
        gm_t88rcucg1b7uk t469 = gm_l17;
        gm_tb7u19loo3j87 t470;
        gm_f_math_vector_parts_49c7865((void *)&t470, t468, GM_CONV(gm_csguankko4lmhd, t469));
        if (!((((uint8_t *)&t470))[16] != 0)) gm_trap("unwrap of null", gm_srci09ctgf0djoe, 227, 10);
        gm_tlcm4lvu3g1a8 t471 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t470));
        uint64_t t472 = GM_LD(uint64_t, (((uint8_t *)&t471) + 0));
        gm_l32 = t472;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t473 = gm_l0;
        void * t474 = gm_l6;
        void * t475 = (void *)((uint8_t *)&F.l33);
        void * t476 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t473, t474, t475);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t476;
    }
    {
        void * t477 = gm_l0;
        void * t478 = gm_l7;
        void * t479 = (void *)((uint8_t *)&F.l33);
        void * t480 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t477, t478, t479);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l35 = t480;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l38 = ((uint64_t)0ULL);
        }
        {
            uint64_t t482 = gm_l32;
            gm_l37 = t482;
        }
        {
            for (;;) {
                uint64_t t484 = gm_l38;
                uint64_t t485 = gm_l37;
                uint8_t t486 = (t484 < t485);
                if (!t486) goto lb483;
                {
                    uint64_t t489 = gm_l38;
                    uint64_t t492 = GM_LD(uint64_t, ((uint8_t *)&t489));
                    gm_l39 = t492;
                    {
                        void * t494;
                        uint8_t t496 = gm_l16;
                        if (!t496) goto ie495;
                        void * t498 = gm_l0;
                        void * t499 = gm_l34;
                        gm_t88rcucg1b7uk t500 = gm_l18;
                        uint64_t t501 = gm_l39;
                        void * t502 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t498, t499, t500, t501);
                        if (gm_unw.kind) {
                            goto gm_ll487;
                        }
                        void * t503 = t502;
                        t494 = t503;
                        goto ix495;
ie495:;
                        void * t505 = gm_l34;
                        void * t506 = t505;
                        t494 = t506;
ix495:;
                        gm_l40 = t494;
                    }
                    {
                        void * t507;
                        uint8_t t509 = 0;
                        uint8_t t510 = gm_l16;
                        uint8_t t511 = (uint8_t)!t510;
                        if (!t511) {
                            uint8_t t512 = gm_l20;
                            t509 = t512;
                        } else t509 = 1;
                        if (!t509) goto ie508;
                        void * t514 = gm_l0;
                        void * t515 = gm_l35;
                        gm_t88rcucg1b7uk t516 = gm_l18;
                        uint64_t t517 = gm_l39;
                        void * t518 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t514, t515, t516, t517);
                        if (gm_unw.kind) {
                            goto gm_ll487;
                        }
                        void * t519 = t518;
                        t507 = t519;
                        goto ix508;
ie508:;
                        void * t521 = gm_l35;
                        void * t522 = t521;
                        t507 = t522;
ix508:;
                        gm_l41 = t507;
                    }
                    {
                        void * t523 = gm_l0;
                        void * t524 = GM_LD(void *, (((uint8_t *)t523) + 0));
                        gm_tl874t2atnlh3 t525 = gm_l4;
                        void * t526 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t524, t525);
                        if (gm_unw.kind) {
                            goto gm_ll487;
                        }
                        gm_l42 = t526;
                    }
                    {
                        uint8_t t528 = gm_l23;
                        if (!t528) goto ie527;
                        {
                            void * t530 = gm_l0;
                            void * t531 = (void *)(((uint8_t *)t530) + 320);
                            gm_t91vm1g0k2rpm t532;
                            memset((uint8_t *)&t532 + 34, 0, 6);
                            gm_t88rcucg1b7uk t533 = gm_l18;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t532 + 0, t533);
                            gm_tl874t2atnlh3 t534 = gm_l4;
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t532 + 4, t534);
                            gm_b16_8 t535 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t532 + 16, t535);
                            GM_ST(uint8_t, (uint8_t *)&t532 + 32, ((uint8_t)1));
                            GM_ST(uint8_t, (uint8_t *)&t532 + 33, ((uint8_t)0));
                            gm_tpm220j5qhjke t536;
                            GM_ST(uint8_t, (uint8_t *)&t536 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t536 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t536 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t536 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t536 + 8), t532);
                            gm_tftb1odc739t5 t537 = gm_f_push_7koe9lk(t531, &t536);
                            if (((((uint8_t *)&t537))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t537) + 1, gm_srci09ctgf0djoe, 237, 13);
                            gm_b0_1 t538 = GM_LD(gm_b0_1, (((uint8_t *)&t537) + 1));
                        }
                        {
                            gm_ts6oqppjh0ba3 t539;
                            uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                            if (t541 != ((uint8_t)3ULL)) goto ie540;
                            gm_t9qkh0rsooc8d t542 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                            gm_l43 = t542;
                            gm_t9qkh0rsooc8d t544 = gm_l43;
                            gm_ts6oqppjh0ba3 t545;
                            GM_ST(uint8_t, ((uint8_t *)&t545 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t545 + 1), t544);
                            gm_ts6oqppjh0ba3 t546 = t545;
                            t539 = t546;
                            goto ix540;
ie540:;
                            gm_ts6oqppjh0ba3 t548;
                            GM_ST(uint8_t, (uint8_t *)&t548 + 1, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t548 + 0), ((uint8_t)6ULL));
                            gm_ts6oqppjh0ba3 t549 = t548;
                            t539 = t549;
ix540:;
                            gm_l44 = t539;
                        }
                        {
                            gm_t88rcucg1b7uk t550 = gm_l18;
                            gm_trl9d7m1k5lbd t551;
                            memset((uint8_t *)&t551 + 2, 0, 6);
                            gm_ts6oqppjh0ba3 t552 = gm_l44;
                            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t551 + 0, t552);
                            void * t553 = gm_l40;
                            GM_ST(void *, (uint8_t *)&t551 + 8, t553);
                            void * t554 = gm_l41;
                            GM_ST(void *, (uint8_t *)&t551 + 16, t554);
                            gm_teti411eujvf4 t555;
                            memset(&t555, 0, sizeof t555);
                            GM_ST(uint8_t, ((uint8_t *)&t555 + 0), ((uint8_t)30ULL));
                            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t555 + 8), t551);
                            gm_tl874t2atnlh3 t556 = gm_l4;
                            void * t557 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t550, &t555, t556);
                            if (gm_unw.kind) {
                                goto gm_ll487;
                            }
                            gm_l42 = t557;
                        }
                        goto ix527;
ie527:;
                        {
                            void * t559 = gm_l0;
                            gm_ts6oqppjh0ba3 t560 = gm_l1;
                            void * t561 = gm_l40;
                            void * t562 = gm_l41;
                            gm_tl874t2atnlh3 t563 = gm_l4;
                            void * t564 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t559, t560, t561, t562, t563);
                            if (gm_unw.kind) {
                                goto gm_ll487;
                            }
                            gm_l42 = t564;
                        }
ix527:;
                    }
                    {
                        void * t566 = gm_l5;
                        void * t567 = gm_l42;
                        gm_t88rcucg1b7uk t568 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t567) + 0));
                        uint8_t t569 = gm_f_is_error_8k27gd1(t566, GM_CONV(gm_csguankko4lmhd, t568));
                        if (!t569) goto ie565;
                        {
                            void * t571 = gm_l42;
                            uint8_t t572 = 0;
                            F.ret = t571;
                            goto gm_fret;
                        }
                        goto ix565;
ie565:;
ix565:;
                    }
                    {
                        void * t573 = (void *)((uint8_t *)&F.l36);
                        void * t574 = gm_l42;
                        gm_tftb1odc739t5 t575 = gm_f_push_d943vgp_2(t573, &(*(gm_t69m56meqefs8 *)((uint8_t *)t574)));
                        if (((((uint8_t *)&t575))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t575) + 1, gm_srci09ctgf0djoe, 244, 9);
                        gm_b0_1 t576 = GM_LD(gm_b0_1, (((uint8_t *)&t575) + 1));
                    }
                    goto me490;
mn491:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 232, 5);
me490:;
                }
lc483:;
                {
                    uint64_t t578 = gm_l38;
                    uint64_t r579; bool o580 = __builtin_add_overflow(t578, ((uint64_t)1ULL), &r579);
                    uint64_t t581 = r579;
                    gm_l38 = t581;
                }
            }
gm_ll487:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb483; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc483; }
            }
            goto gm_lad;
lb483:;
        }
    }
    {
        gm_t88rcucg1b7uk t582 = gm_l17;
        gm_b16_8 t583 = GM_LD(gm_b16_8, (((uint8_t *)&F.l36) + 0));
        gm_teti411eujvf4 t584;
        memset(&t584, 0, sizeof t584);
        GM_ST(uint8_t, ((uint8_t *)&t584 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t584 + 8), t583);
        gm_tl874t2atnlh3 t585 = gm_l4;
        void * t586 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t582, &t584, t585);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l45 = t586;
    }
    {
        gm_t88rcucg1b7uk t587 = gm_l17;
        gm_b16_8 t588 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
        void * t589 = gm_l45;
        gm_tl874t2atnlh3 t590 = gm_l4;
        void * t591 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t587, t588, t589, t590);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t592 = 0;
        F.ret = t591;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_compound_number_elem_6d1d2ni(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_compound_number_elem_6d1d2ni F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tb7u19loo3j87 t7;
        gm_f_math_vector_parts_49c7865((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
            gm_tkn42u7na3t81 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[4] = 1;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        gm_t88rcucg1b7uk t15 = gm_l1;
        gm_tkn42u7na3t81 t16 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = 0;
        F.ret = t16;
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
__attribute__((aligned(4))) void * gm_f_compound_number_unary_4h478fs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_compound_number_unary_4h478fs F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tn3r4rvlh0rsv gm_l6; /* <temporary array> */
    gm_tlcm4lvu3g1a8 gm_l7; /* p */
    gm_t9qkh0rsooc8d gm_l8; /* top */
    uint8_t gm_l9; /* int_only */
    uint8_t gm_l10; /* signed_only */
    gm_b16_8 gm_l11; /* s */
    void * gm_l13; /* x */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* arr */
    GM_STACK_CHECK(&F, 1426u);
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
        void * t6 = gm_l0;
        void * t7 = gm_l2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t9) goto ie5;
        {
            {
                void * t12 = gm_l0;
                void * t13 = gm_l2;
                gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
                gm_tl874t2atnlh3 t15 = gm_l3;
                gm_t88rcucg1b7uk t16 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t12, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 96);
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = gm_l2;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
        gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t23;
    }
    {
        void * t25 = gm_l4;
        gm_t88rcucg1b7uk t26 = gm_l5;
        uint8_t t27 = gm_f_is_error_8k27gd1(t25, GM_CONV(gm_csguankko4lmhd, t26));
        if (!t27) goto ie24;
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            gm_tl874t2atnlh3 t31 = gm_l3;
            void * t32 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t34 = gm_l4;
        gm_t88rcucg1b7uk t35 = gm_l5;
        gm_tb7u19loo3j87 t36;
        gm_f_math_vector_parts_49c7865((void *)&t36, t34, GM_CONV(gm_csguankko4lmhd, t35));
        gm_tlcm4lvu3g1a8 t37;
        if (((((uint8_t *)&t36))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t38 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t36) + 0));
            t37 = t38;
        } else {
            {
                uint8_t t41 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                if (!t41) goto ie40;
                {
                }
                {
                    void * t43 = gm_l0;
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sprospqi1g16o));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
                    {
                        void * t46 = gm_l2;
                        gm_tn3r4rvlh0rsv t47;
                        GM_ST(void *, ((uint8_t *)&t47 + 0), t46);
                        gm_l6 = t47;
                    }
                    void * t48 = ((uint8_t *)&gm_l6);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 257, 58);
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t48 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t50 = t49;
                    gm_tl874t2atnlh3 t51 = gm_l3;
                    void * t52 = gm_f_complex_call_8so3bss((void *)NULL, cx, NULL, t43, t44, t50, ((uint8_t)0), t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t53 = 0;
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                void * t54 = gm_l0;
                gm_tl874t2atnlh3 t55 = gm_l3;
                gm_b16_8 t56;
                uint8_t t58 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
                if (!t58) goto ie57;
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sa831sfid0hf3));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
                gm_b16_8 t61 = t60;
                t56 = t61;
                goto ix57;
ie57:;
                gm_b16_8 t63;
                GM_ST(void *, (uint8_t *)&t63, (void *)(gm_scvhpklutvpjg));
                GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(3));
                gm_b16_8 t64 = t63;
                t56 = t64;
ix57:;
                gm_tnpifv20irnph t65;
                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t56);
                gm_tabb710d9mq2e t66;
                gm_f_format_gdra5k8((void *)&t66, (void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srci09ctgf0djoe, 258, 24);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t54, t55, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
            gm_tl874t2atnlh3 t72 = gm_l3;
            void * t73 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        gm_l7 = t37;
    }
    {
        gm_t9qkh0rsooc8d t75;
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
        gm_l8 = t75;
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(1));
        gm_l11 = t76;
    }
    {
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t82) { case ((uint8_t)0ULL): goto ma78; case ((uint8_t)1ULL): goto ma79; case ((uint8_t)2ULL): goto ma80; case ((uint8_t)3ULL): goto ma80; case ((uint8_t)4ULL): goto ma80; case ((uint8_t)5ULL): goto ma80; default: goto ma80; }
ma78:;
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t84 != ((uint8_t)0ULL)) goto mn83;
        {
            gm_l10 = ((uint8_t)1);
        }
        goto me77;
mn83:;
ma79:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t87 != ((uint8_t)1ULL)) goto mn86;
        {
            gm_t9qkh0rsooc8d t89;
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)1ULL));
            gm_l8 = t89;
        }
        {
            gm_l9 = ((uint8_t)1);
        }
        {
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sa831sfid0hf3));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(1));
            gm_l11 = t90;
        }
        goto me77;
mn86:;
ma80:;
        {
            void * t93 = gm_l0;
            gm_tl874t2atnlh3 t94 = gm_l3;
            void * t95 = gm_l0;
            gm_t88rcucg1b7uk t96 = gm_l5;
            gm_b16_8 t97 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t95, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t98;
            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
            gm_tabb710d9mq2e t99;
            gm_f_format_6up4rm5((void *)&t99, (void *)NULL, cx, NULL, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srci09ctgf0djoe, 273, 28);
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_b0_1 t102 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t93, t94, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t103 = gm_l0;
            void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
            gm_tl874t2atnlh3 t105 = gm_l3;
            void * t106 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t107 = 0;
            F.ret = t106;
            goto gm_fret;
        }
        goto me77;
mn91:;
ma81:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 265, 5);
me77:;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t108 = gm_l0;
        void * t109 = gm_l2;
        void * t110 = (void *)((uint8_t *)&F.l12);
        void * t111 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t108, t109, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t111;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
            gm_l15 = t113;
        }
        {
            for (;;) {
                uint64_t t115 = gm_l16;
                uint64_t t116 = gm_l15;
                uint8_t t117 = (t115 < t116);
                if (!t117) goto lb114;
                {
                    uint64_t t120 = gm_l16;
                    uint64_t t123 = GM_LD(uint64_t, ((uint8_t *)&t120));
                    gm_l17 = t123;
                    {
                        void * t125 = gm_l0;
                        void * t126 = (void *)(((uint8_t *)t125) + 320);
                        gm_t91vm1g0k2rpm t127;
                        memset((uint8_t *)&t127 + 34, 0, 6);
                        gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t127 + 0, t128);
                        gm_tl874t2atnlh3 t129 = gm_l3;
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t127 + 4, t129);
                        gm_b16_8 t130 = gm_l11;
                        GM_ST(gm_b16_8, (uint8_t *)&t127 + 16, t130);
                        uint8_t t131 = gm_l9;
                        GM_ST(uint8_t, (uint8_t *)&t127 + 32, t131);
                        uint8_t t132 = gm_l10;
                        GM_ST(uint8_t, (uint8_t *)&t127 + 33, t132);
                        gm_tpm220j5qhjke t133;
                        GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t133 + 8), t127);
                        gm_tftb1odc739t5 t134 = gm_f_push_7koe9lk(t126, &t133);
                        if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srci09ctgf0djoe, 281, 9);
                        gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                    }
                    {
                        void * t136 = (void *)((uint8_t *)&F.l14);
                        gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        gm_t9qkh0rsooc8d t138 = gm_l8;
                        void * t139 = gm_l0;
                        void * t140 = gm_l13;
                        gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t142 = gm_l17;
                        void * t143 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t139, t140, t141, t142);
                        if (gm_unw.kind) {
                            goto gm_ll118;
                        }
                        gm_t8ghgl2jj0qha t144;
                        GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t144 + 0), t138);
                        GM_ST(void *, ((uint8_t *)&t144 + 8), t143);
                        gm_teti411eujvf4 t145;
                        memset(&t145, 0, sizeof t145);
                        GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)29ULL));
                        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t145 + 8), t144);
                        gm_tl874t2atnlh3 t146 = gm_l3;
                        void * t147 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t145, t146);
                        if (gm_unw.kind) {
                            goto gm_ll118;
                        }
                        gm_tftb1odc739t5 t148 = gm_f_push_d943vgp_2(t136, &(*(gm_t69m56meqefs8 *)((uint8_t *)t147)));
                        if (((((uint8_t *)&t148))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t148) + 1, gm_srci09ctgf0djoe, 282, 9);
                        gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 1));
                    }
                    goto me121;
mn122:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 280, 5);
me121:;
                }
lc114:;
                {
                    uint64_t t151 = gm_l16;
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    uint64_t t154 = r152;
                    gm_l16 = t154;
                }
            }
gm_ll118:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb114; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc114; }
            }
            goto gm_lad;
lb114:;
        }
    }
    {
        gm_t88rcucg1b7uk t155 = gm_l5;
        gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_teti411eujvf4 t157;
        memset(&t157, 0, sizeof t157);
        GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 8), t156);
        gm_tl874t2atnlh3 t158 = gm_l3;
        void * t159 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t155, &t157, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t159;
    }
    {
        gm_t88rcucg1b7uk t160 = gm_l5;
        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        void * t162 = gm_l18;
        gm_tl874t2atnlh3 t163 = gm_l3;
        void * t164 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t160, t161, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t165 = 0;
        F.ret = t164;
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
__attribute__((aligned(4))) void * gm_f_core_call_values_qq2la05(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_core_call_values_qq2la05 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l8; /* v */
    gm_t88rcucg1b7uk gm_l9; /* vt */
    gm_b16_8 gm_l10; /* local_name */
    uint32_t gm_l11; /* local */
    gm_t08olq97rscn3 gm_l12; /* id */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_6oos54v((void *)&F.l5, (void *)NULL, cx, NULL);
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
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t11 * 8));
                    void * t16 = GM_LD(void *, ((uint8_t *)&t13));
                    gm_l8 = t16;
                    {
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)t18) + 96);
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                        void * t22 = gm_l8;
                        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + 0));
                        gm_t88rcucg1b7uk t24 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t19, t21, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l9 = t24;
                    }
                    {
                        void * t25 = gm_l0;
                        uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 16) + 0) + 8);
                        gm_t4ce0jd3tdro4 t27;
                        GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t26);
                        gm_tabb710d9mq2e t28;
                        gm_f_format_uubl3s3((void *)&t28, (void *)NULL, cx, NULL, t27);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srci09ctgf0djoe, 319, 23);
                        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
                        gm_l10 = t29;
                    }
                    {
                        void * t30 = gm_l0;
                        gm_b16_8 t31 = gm_l10;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                        gm_t88rcucg1b7uk t33 = gm_l9;
                        void * t34 = gm_l8;
                        gm_tl874t2atnlh3 t35 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t34) + 72));
                        uint32_t t36 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t30, t32, t33, ((uint8_t)0), t35);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l11 = t36;
                    }
                    {
                        void * t37 = (void *)((uint8_t *)&F.l4);
                        uint32_t t38 = gm_l11;
                        void * t39 = gm_l8;
                        gm_ttggq7ng4kp3p t40;
                        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t40 + 0), t38);
                        GM_ST(void *, ((uint8_t *)&t40 + 8), t39);
                        gm_trcmmd12cefb1 t41;
                        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t41 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t41 + 8), t40);
                        gm_tftb1odc739t5 t42 = gm_f_push_d6c76ud_2(t37, &t41);
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srci09ctgf0djoe, 321, 9);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    {
                        void * t44 = gm_l0;
                        gm_b16_8 t45 = gm_l10;
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                        uint32_t t47 = gm_l11;
                        gm_t930q5l38hdap t48;
                        memset(&t48, 0, sizeof t48);
                        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t48 + 8), t47);
                        void * t49 = gm_l8;
                        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t49) + 72));
                        gm_b0_1 t51 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t44, t46, &t48, t50);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        memset((uint8_t *)&gm_l12 + 28, 0, 4);
                        gm_b16_8 t52 = gm_l10;
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                        GM_ST(gm_b16_8, (uint8_t *)&gm_l12 + 0, t53);
                        void * t54 = gm_l8;
                        gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l12 + 16, t55);
                    }
                    {
                        memset((uint8_t *)&F.l13 + 4, 0, 4);
                        memset((uint8_t *)&F.l13 + 84, 0, 4);
                        GM_ST(uint32_t, (uint8_t *)&F.l13 + 0, ((uint32_t)4294967295ULL));
                        gm_t08olq97rscn3 t56 = gm_l12;
                        gm_t2gtbh39ptekd t57;
                        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t57 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57 + 8), t56);
                        GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&F.l13 + 8, t57);
                        void * t58 = gm_l8;
                        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l13 + 72, t59);
                    }
                    {
                        void * t60 = (void *)((uint8_t *)&F.l5);
                        gm_t7te2tdpebbt4 t61;
                        memset((uint8_t *)&t61 + 140, 0, 4);
                        gm_tc3oht4ksth9d t62 = F.l13;
                        gm_t22mtv3sd9prf t63;
                        memset(&t63, 0, sizeof t63);
                        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t63 + 8), t62);
                        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t61 + 0, t63);
                        void * t64 = gm_l8;
                        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t61 + 128, t65);
                        gm_tftb1odc739t5 t66 = gm_f_push_jis5lhu(t60, &t61);
                        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srci09ctgf0djoe, 325, 9);
                        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 317, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t69 = gm_l7;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l7 = t72;
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
        void * t73 = gm_l0;
        uint32_t t74 = gm_l1;
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        gm_tl874t2atnlh3 t77 = gm_l3;
        void * t78 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t73, t74, ((void *)0), t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t78;
    }
    {
        void * t79 = gm_l14;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t79) + 0));
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        void * t82 = gm_l14;
        gm_tl874t2atnlh3 t83 = gm_l3;
        void * t84 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t80, t81, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t85 = 0;
        F.ret = t84;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_operand_509et87(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_describe_operand_509et87 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 281u);
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
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_taari1s0m51a7 t8 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t3, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t8))[1] != 0)) goto mn10;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sp66nadh171v3));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(18));
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
        goto me9;
mn10:;
        if (!((((uint8_t *)&t8))[1] != 0)) goto mn14;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sf372rql24e3n));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(16));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me9;
mn14:;
        if (!((((uint8_t *)&t8))[1] != 0)) goto mn18;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t19 != ((uint8_t)2ULL)) goto mn18;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfa8f3bk569gj));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(25));
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me9;
mn18:;
        if (((((uint8_t *)&t8))[1] != 0)) goto mn22;
        goto me9;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 27, 5);
me9:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 96);
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        gm_t88rcucg1b7uk t29 = gm_l1;
        gm_t88rcucg1b7uk t30 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t26, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t31 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t24, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
        gm_tabb710d9mq2e t33;
        gm_f_format_h4jcodi((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srci09ctgf0djoe, 33, 12);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        uint8_t t36 = 0;
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
__attribute__((aligned(4))) void * gm_f_imaginary_literal_6d41gfv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_imaginary_literal_6d41gfv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* l */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_t9qkh0rsooc8d gm_l5; /* suffix */
    gm_t88rcucg1b7uk gm_l6; /* part */
    uint32_t gm_l7; /* c */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    GM_STACK_CHECK(&F, 546u);
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
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t6;
    }
    {
        void * t8 = gm_l1;
        if (!(((((uint8_t *)t8) + 96))[1] != 0)) goto ie7;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + 96));
        gm_l5 = t9;
        {
            void * t11 = gm_l3;
            uint16_t t12;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; default: goto ma17; }
ma14:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t20 != ((uint8_t)0ULL)) goto mn19;
            t12 = ((uint16_t)16ULL);
            goto me13;
mn19:;
ma15:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t22 != ((uint8_t)1ULL)) goto mn21;
            t12 = ((uint16_t)32ULL);
            goto me13;
mn21:;
ma16:;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t24 != ((uint8_t)2ULL)) goto mn23;
            t12 = ((uint16_t)64ULL);
            goto me13;
mn23:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 41, 35);
me13:;
            gm_t53at80pfdus3 t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)3ULL));
            GM_ST(uint16_t, ((uint8_t *)&t25 + 8), t12);
            gm_t88rcucg1b7uk t26 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t11, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
        }
        {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s0rphlcbno91v));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(7));
            gm_t9805qtonbsag t31 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t31))[4] != 0)) goto ie27;
            uint32_t t32 = GM_LD(uint32_t, ((uint8_t *)&t31));
            gm_l7 = t32;
            {
            }
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                uint32_t t36 = gm_l7;
                {
                    gm_t88rcucg1b7uk t38 = gm_l6;
                    gm_tnqk2qt7j42o9 t39;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t39 + 0), t38);
                    gm_l8 = t39;
                }
                void * t40 = ((uint8_t *)&gm_l8);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 46, 81);
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t40 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t42 = t41;
                gm_t88rcucg1b7uk t43 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t35, t36, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t43;
            }
            goto ix27;
ie27:;
ix27:;
        }
        goto ix7;
ie7:;
        {
            void * t45 = gm_l0;
            gm_t9qkh0rsooc8d t46;
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)2ULL));
            gm_t88rcucg1b7uk t47 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t47;
        }
ix7:;
    }
    {
        void * t49 = gm_l0;
        uint8_t t50;
        if (((((((uint8_t *)t49) + 96) + 56))[8] != 0)) goto mn52;
        t50 = ((uint8_t)1);
        goto me51;
mn52:;
        t50 = ((uint8_t)0);
        goto me51;
mn53:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 50, 8);
me51:;
        if (!t50) goto ie48;
        void * t54 = gm_l3;
        gm_t88rcucg1b7uk t55 = gm_l4;
        uint8_t t56 = gm_f_is_error_8k27gd1(t54, GM_CONV(gm_csguankko4lmhd, t55));
        uint8_t t57 = (uint8_t)!t56;
        if (!t57) goto ie48;
        {
            {
                void * t60 = gm_l0;
                gm_t9qkh0rsooc8d t61;
                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
                gm_t88rcucg1b7uk t62 = gm_f_literal_var_3sb5sti((void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        gm_t88rcucg1b7uk t63 = gm_l4;
        gm_tngtuiuskn938 t64;
        gm_f_zero_9r6o7sj((void *)&t64, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t65 = gm_l1;
        gm_teti411eujvf4 t66;
        gm_f_untyped_complex_20agrmu((void *)&t66, (void *)NULL, cx, NULL, &t64, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t65) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t67 = gm_l2;
        void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t63, &t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t69 = 0;
        F.ret = t68;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_complex_var_bk0jb4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_compound_number_kbipjl5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_compound_number_kbipjl5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 132u);
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
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t3, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        uint8_t t8 = 0;
        uint8_t t9 = 0;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l2;
        uint8_t t14 = gm_f_is_math_vector_09mtuao(t12, GM_CONV(gm_csguankko4lmhd, t13));
        if (!t14) {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_t88rcucg1b7uk t17 = gm_l2;
            gm_tkn42u7na3t81 t18 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19;
            if (((((uint8_t *)&t18))[4] != 0)) goto mn21;
            t19 = ((uint8_t)0);
            goto me20;
mn21:;
            t19 = ((uint8_t)1);
            goto me20;
mn22:;
            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 15, 42);
me20:;
            t9 = t19;
        } else t9 = 1;
        if (!t9) {
            void * t23 = gm_l0;
            gm_t88rcucg1b7uk t24 = gm_l2;
            uint8_t t25 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t8 = t25;
        } else t8 = 1;
        uint8_t t26 = 0;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_literal_var_3sb5sti(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_literal_var_3sb5sti F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    uint32_t gm_l2; /* c */
    gm_tnqk2qt7j42o9 gm_l3; /* <temporary array> */
    GM_STACK_CHECK(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
        if (!t3) goto ie2;
        void * t4 = gm_l0;
        uint8_t t5;
        if (((((((uint8_t *)t4) + 96) + 56))[8] != 0)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn8:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 56, 29);
me6:;
        if (!t5) goto ie2;
        {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s0rphlcbno91v));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
            gm_t9805qtonbsag t14 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t14))[4] != 0)) goto ie10;
            uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)&t14));
            gm_l2 = t15;
            {
            }
            {
                void * t17 = gm_l0;
                uint8_t *t18 = ((((uint8_t *)t17) + 96) + 56);
                uint32_t t19 = gm_l2;
                void * t20 = gm_l0;
                void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                uint32_t t22 = gm_l2;
                {
                    void * t24 = gm_l0;
                    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                    void * t26 = (void *)(((uint8_t *)t25) + 8);
                    gm_t53at80pfdus3 t27;
                    memset(&t27, 0, sizeof t27);
                    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)3ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t27 + 8), ((uint16_t)64ULL));
                    gm_t88rcucg1b7uk t28 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t26, &t27);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnqk2qt7j42o9 t29;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29 + 0), t28);
                    gm_l3 = t29;
                }
                void * t30 = ((uint8_t *)&gm_l3);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 58, 74);
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t30 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t32 = t31;
                gm_t88rcucg1b7uk t33 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t21, t22, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tplpbqf3duvn3 t34;
                GM_ST(uint32_t, ((uint8_t *)&t34 + 0), t19);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 4), t33);
                gm_thdmr9qicg35r t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
                GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t35), t34);
                ((uint8_t *)&t35)[8] = 1;
                GM_ST(gm_thdmr9qicg35r, t18, t35);
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
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 96);
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_t9qkh0rsooc8d t41 = gm_l1;
        gm_t88rcucg1b7uk t42 = gm_f_new_var_up2m1nq((void *)NULL, cx, NULL, t37, t40, t41);
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
inline __attribute__((aligned(4))) void * gm_f_math_elem_iuerhch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_math_elem_iuerhch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* index */
    GM_STACK_CHECK(&F, 365u);
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
        gm_t88rcucg1b7uk t5 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t6 = gm_l3;
        gm_tbci5ospk5nnn t7;
        gm_f_from_u64_ud13q5v((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t8 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t8 + 8), t7);
        void * t9 = gm_l1;
        gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
        void * t11 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t5, &t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t11;
    }
    {
        gm_t88rcucg1b7uk t12 = gm_l2;
        gm_tlcl4u558l6p5 t13;
        memset((uint8_t *)&t13 + 17, 0, 7);
        void * t14 = gm_l1;
        GM_ST(void *, (uint8_t *)&t13 + 0, t14);
        void * t15 = gm_l4;
        GM_ST(void *, (uint8_t *)&t13 + 8, t15);
        GM_ST(uint8_t, (uint8_t *)&t13 + 16, ((uint8_t)0));
        gm_teti411eujvf4 t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t16 + 8), t13);
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
        void * t19 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t12, &t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_math_vector_conversion_0cttjm0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_math_vector_conversion_0cttjm0 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    void * gm_l13; /* v */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_tn3r4rvlh0rsv gm_l18; /* <temporary array> */
    void * gm_l19; /* arr */
    gm_b16_8 gm_l20; /* why */
    GM_STACK_CHECK(&F, 1871u);
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
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_l1;
        uint8_t t15 = gm_f_is_math_vector_09mtuao(t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint8_t t19 = gm_f_is_math_vector_09mtuao(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie12;
        {
            uint8_t t22 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l5;
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t27 != ((uint8_t)11ULL)) goto ie23;
        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)&t26) + 8) + 0));
        gm_l6 = t28;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t26) + 8) + 8));
        gm_l7 = t29;
        {
            void * t32 = gm_l4;
            gm_t88rcucg1b7uk t33 = gm_l1;
            gm_t53at80pfdus3 t34;
            gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
            if (t35 != ((uint8_t)11ULL)) goto ie31;
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t34) + 8) + 0));
            gm_l8 = t36;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 8));
            gm_l9 = t37;
            {
                gm_b0_1 t39;
            }
            {
                uint8_t t40 = 0;
                void * t41 = gm_l4;
                gm_t88rcucg1b7uk t42 = gm_l5;
                uint8_t t43 = gm_f_is_math_vector_09mtuao(t41, GM_CONV(gm_csguankko4lmhd, t42));
                if (t43) {
                    void * t44 = gm_l4;
                    gm_t88rcucg1b7uk t45 = gm_l1;
                    uint8_t t46 = gm_f_is_math_vector_09mtuao(t44, GM_CONV(gm_csguankko4lmhd, t45));
                    t40 = t46;
                } else t40 = 0;
                gm_l10 = t40;
            }
            {
                uint64_t t48 = gm_l6;
                uint64_t t49 = gm_l8;
                uint8_t t50 = ((t48 == t49));
                if (!t50) goto ie47;
                gm_t88rcucg1b7uk t51 = gm_l7;
                gm_t88rcucg1b7uk t52 = gm_l9;
                uint8_t t53 = (gm_eq9m6mvtmgde(((uint8_t *)&t51), ((uint8_t *)&t52)));
                if (!t53) goto ie47;
                uint8_t t54 = 0;
                uint8_t t55 = 0;
                uint8_t t56 = gm_l10;
                if (!t56) {
                    void * t57 = gm_l4;
                    gm_t88rcucg1b7uk t58 = gm_l5;
                    uint8_t t59 = gm_f_plain_31j25h8(t57, GM_CONV(gm_csguankko4lmhd, t58));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t55 = t59;
                } else t55 = 1;
                if (!t55) {
                    void * t60 = gm_l4;
                    gm_t88rcucg1b7uk t61 = gm_l1;
                    uint8_t t62 = gm_f_plain_31j25h8(t60, GM_CONV(gm_csguankko4lmhd, t61));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t54 = t62;
                } else t54 = 1;
                if (!t54) goto ie47;
                {
                }
                {
                    gm_tvdg94bk7aa22 t64;
                    GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t64 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)7ULL));
                    gm_t88rcucg1b7uk t65 = gm_l1;
                    {
                        void * t67 = gm_l2;
                        gm_tn3r4rvlh0rsv t68;
                        GM_ST(void *, ((uint8_t *)&t68 + 0), t67);
                        gm_l11 = t68;
                    }
                    void * t69 = ((uint8_t *)&gm_l11);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 432, 109);
                    gm_b16_8 t70;
                    GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t69 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t71 = t70;
                    gm_tl874t2atnlh3 t72 = gm_l3;
                    void * t73 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t64, t65, t71, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t74 = 0;
                    F.ret = t73;
                    goto gm_fret;
                }
                goto ix47;
ie47:;
ix47:;
            }
            {
                uint8_t t76 = gm_l10;
                if (!t76) goto ie75;
                uint64_t t77 = gm_l6;
                uint64_t t78 = gm_l8;
                uint8_t t79 = ((t77 == t78));
                if (!t79) goto ie75;
                void * t80 = gm_l0;
                void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
                gm_t88rcucg1b7uk t82 = gm_l7;
                gm_t88rcucg1b7uk t83 = gm_l9;
                uint8_t t84 = gm_f_lossless_a54o69s(t81, GM_CONV(gm_csguankko4lmhd, t82), GM_CONV(gm_csguankko4lmhd, t83));
                if (!t84) goto ie75;
                {
                    gm_f_list_ir0h3tc_2((void *)&F.l12, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t86 = gm_l0;
                    void * t87 = gm_l2;
                    void * t88 = (void *)((uint8_t *)&F.l12);
                    void * t89 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t86, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t89;
                }
                {
                    gm_f_list_vp1djv1_2((void *)&F.l14, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_l16 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t91 = gm_l6;
                        gm_l15 = t91;
                    }
                    {
                        for (;;) {
                            uint64_t t93 = gm_l16;
                            uint64_t t94 = gm_l15;
                            uint8_t t95 = (t93 < t94);
                            if (!t95) goto lb92;
                            {
                                uint64_t t98 = gm_l16;
                                uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)&t98));
                                gm_l17 = t101;
                                {
                                }
                                {
                                    void * t103 = (void *)((uint8_t *)&F.l14);
                                    gm_tvdg94bk7aa22 t104;
                                    GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t104 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)0ULL));
                                    gm_t88rcucg1b7uk t105 = gm_l9;
                                    {
                                        void * t107 = gm_l0;
                                        void * t108 = gm_l13;
                                        gm_t88rcucg1b7uk t109 = gm_l7;
                                        uint64_t t110 = gm_l17;
                                        void * t111 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t107, t108, t109, t110);
                                        if (gm_unw.kind) {
                                            goto gm_ll96;
                                        }
                                        gm_tn3r4rvlh0rsv t112;
                                        GM_ST(void *, ((uint8_t *)&t112 + 0), t111);
                                        gm_l18 = t112;
                                    }
                                    void * t113 = ((uint8_t *)&gm_l18);
                                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 437, 57);
                                    gm_b16_8 t114;
                                    GM_ST(void *, (uint8_t *)&t114, (void *)(((uint8_t *)t113 + (uintptr_t)0 * 8)));
                                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                                    gm_b16_8 t115 = t114;
                                    gm_tl874t2atnlh3 t116 = gm_l3;
                                    void * t117 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t104, t105, t115, t116);
                                    if (gm_unw.kind) {
                                        goto gm_ll96;
                                    }
                                    gm_tftb1odc739t5 t118 = gm_f_push_d943vgp_2(t103, &(*(gm_t69m56meqefs8 *)((uint8_t *)t117)));
                                    if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srci09ctgf0djoe, 437, 25);
                                    gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                                }
                                goto me99;
mn100:;
                                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 437, 9);
me99:;
                            }
lc92:;
                            {
                                uint64_t t121 = gm_l16;
                                uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                                uint64_t t124 = r122;
                                gm_l16 = t124;
                            }
                        }
gm_ll96:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb92; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc92; }
                        }
                        goto gm_lad;
lb92:;
                    }
                }
                {
                    gm_t88rcucg1b7uk t125 = gm_l1;
                    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                    gm_teti411eujvf4 t127;
                    memset(&t127, 0, sizeof t127);
                    GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t127 + 8), t126);
                    gm_tl874t2atnlh3 t128 = gm_l3;
                    void * t129 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t125, &t127, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t129;
                }
                {
                    gm_t88rcucg1b7uk t130 = gm_l1;
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                    void * t132 = gm_l19;
                    gm_tl874t2atnlh3 t133 = gm_l3;
                    void * t134 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t130, t131, t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t135 = 0;
                    F.ret = t134;
                    goto gm_fret;
                }
                goto ix75;
ie75:;
ix75:;
            }
            {
                gm_b16_8 t136;
                uint8_t t138 = gm_l10;
                if (!t138) goto ie137;
                uint64_t t139 = gm_l6;
                uint64_t t140 = gm_l8;
                uint8_t t141 = ((t139 == t140));
                if (!t141) goto ie137;
                gm_t6j6smj7qoctu t143;
                void * t144 = gm_l0;
                gm_t88rcucg1b7uk t145 = gm_l7;
                gm_b16_8 t146 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t144, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t143 + 0, t146);
                void * t147 = gm_l0;
                gm_t88rcucg1b7uk t148 = gm_l9;
                gm_b16_8 t149 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t147, t148);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t143 + 16, t149);
                gm_tabb710d9mq2e t150;
                gm_f_format_mrlevsk((void *)&t150, (void *)NULL, cx, NULL, &t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_srci09ctgf0djoe, 441, 40);
                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
                gm_b16_8 t152 = t151;
                gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                t136 = t153;
                goto ix137;
ie137:;
                gm_b16_8 t155;
                GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(0));
                gm_b16_8 t156 = t155;
                t136 = t156;
ix137:;
                gm_l20 = t136;
            }
            {
                void * t157 = gm_l0;
                gm_tl874t2atnlh3 t158 = gm_l3;
                gm_tfeab279c16rr t159;
                void * t160 = gm_l0;
                gm_t88rcucg1b7uk t161 = gm_l5;
                gm_b16_8 t162 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t160, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t159 + 0, t162);
                void * t163 = gm_l0;
                gm_t88rcucg1b7uk t164 = gm_l1;
                gm_b16_8 t165 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t163, t164);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t159 + 16, t165);
                gm_b16_8 t166 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t159 + 32, t166);
                gm_tabb710d9mq2e t167;
                gm_f_format_d0lfl1k((void *)&t167, (void *)NULL, cx, NULL, &t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 8, gm_srci09ctgf0djoe, 442, 20);
                gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&t167) + 8));
                gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                gm_b0_1 t170 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t157, t158, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
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
            goto ix31;
ie31:;
            {
                {
                    void * t178 = gm_l0;
                    gm_tl874t2atnlh3 t179 = gm_l3;
                    gm_t6j6smj7qoctu t180;
                    void * t181 = gm_l0;
                    gm_t88rcucg1b7uk t182 = gm_l5;
                    gm_b16_8 t183 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t181, t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t180 + 0, t183);
                    void * t184 = gm_l0;
                    gm_t88rcucg1b7uk t185 = gm_l1;
                    gm_b16_8 t186 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t184, t185);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t180 + 16, t186);
                    gm_tabb710d9mq2e t187;
                    gm_f_format_9rhpk5t((void *)&t187, (void *)NULL, cx, NULL, &t180);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 8, gm_srci09ctgf0djoe, 427, 24);
                    gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
                    gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                    gm_b0_1 t190 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t178, t179, t189);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t191 = gm_l0;
                    void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
                    gm_tl874t2atnlh3 t193 = gm_l3;
                    void * t194 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t192, t193);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t195 = 0;
                    F.ret = t194;
                    goto gm_fret;
                }
            }
ix31:;
        }
        goto ix23;
ie23:;
        {
            {
                void * t198 = gm_l0;
                gm_tl874t2atnlh3 t199 = gm_l3;
                gm_t6j6smj7qoctu t200;
                void * t201 = gm_l0;
                gm_t88rcucg1b7uk t202 = gm_l5;
                gm_b16_8 t203 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t201, t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 0, t203);
                void * t204 = gm_l0;
                gm_t88rcucg1b7uk t205 = gm_l1;
                gm_b16_8 t206 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t204, t205);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t200 + 16, t206);
                gm_tabb710d9mq2e t207;
                gm_f_format_9rhpk5t((void *)&t207, (void *)NULL, cx, NULL, &t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_srci09ctgf0djoe, 423, 24);
                gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                gm_b0_1 t210 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t198, t199, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t211 = gm_l0;
                void * t212 = GM_LD(void *, (((uint8_t *)t211) + 0));
                gm_tl874t2atnlh3 t213 = gm_l3;
                void * t214 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t212, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t215 = 0;
                F.ret = t214;
                goto gm_fret;
            }
        }
ix23:;
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
__attribute__((aligned(4))) void * gm_f_math_vector_method_ko2814s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_math_vector_method_ko2814s F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l11; /* x */
    gm_ts6oqppjh0ba3 gm_l12; /* mul */
    gm_ts6oqppjh0ba3 gm_l13; /* add */
    gm_ts6oqppjh0ba3 gm_l14; /* sub */
    void * gm_l15; /* y0 */
    void * gm_l16; /* y */
    void * gm_l17; /* tail */
    void * gm_l18; /* acc */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* p */
    void * gm_l23; /* a */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t2lqu8jsj511d gm_l28; /* ij */
    uint64_t gm_l29; /* i */
    uint64_t gm_l30; /* j */
    void * gm_l31; /* p */
    void * gm_l32; /* q */
    gm_tvdg94bk7aa22 gm_l33; /* op */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* i */
    gm_t5fru0lp11n7t gm_l38; /* <temporary array> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* i */
    gm_tn3r4rvlh0rsv gm_l43; /* <temporary array> */
    void * gm_l44; /* tail */
    void * gm_l45; /* acc */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    void * gm_l49; /* e */
    void * gm_l50; /* p */
    void * gm_l51; /* a */
    void * gm_l52; /* total */
    gm_tn3r4rvlh0rsv gm_l53; /* <temporary array> */
    void * gm_l54; /* tail */
    gm_b16_8 gm_l55; /* hint */
    GM_STACK_CHECK(&F, 4559u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l6;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_math_vector_parts_49c7865((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tlcm4lvu3g1a8 t8;
        if (((((uint8_t *)&t7))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
            gm_tl874t2atnlh3 t12 = gm_l5;
            void * t13 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l7 = t8;
    }
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t15;
    }
    {
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t16;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        void * t19 = (void *)((uint8_t *)&F.l10);
        void * t20 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t20;
    }
    {
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t21);
        gm_l12 = t22;
    }
    {
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 1), t23);
        gm_l13 = t24;
    }
    {
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26 + 1), t25);
        gm_l14 = t26;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sg1h22ve4uqmq, 3) == 0)) goto pa30;
        goto po29;
pa30:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0)) goto pa31;
        goto po29;
pa31:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0)) goto pa32;
        goto po29;
pa32:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s2mtoab140jp6, 3) == 0)) goto pa33;
        goto po29;
pa33:;
        goto mn28;
po29:;
        {
            void * t36 = gm_l0;
            gm_b16_8 t37 = gm_l4;
            gm_b16_8 t38 = gm_l3;
            gm_tl874t2atnlh3 t39 = gm_l5;
            uint8_t t40 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t36, t37, ((uint64_t)1ULL), t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t41 = (uint8_t)!t40;
            if (!t41) goto ie35;
            {
                void * t43 = gm_l0;
                void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                gm_tl874t2atnlh3 t45 = gm_l5;
                void * t46 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t47 = 0;
                F.ret = t46;
                goto gm_fret;
            }
            goto ix35;
ie35:;
ix35:;
        }
        {
            void * t48 = gm_l0;
            gm_b16_8 t49 = gm_l4;
            gm_t88rcucg1b7uk t50 = gm_l2;
            gm_tl874t2atnlh3 t51 = gm_l5;
            void * t52 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t48, t49, ((uint64_t)0ULL), t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t52;
        }
        {
            void * t53 = gm_l0;
            void * t54 = gm_l15;
            void * t55 = (void *)((uint8_t *)&F.l10);
            void * t56 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t53, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t56;
        }
        {
            uint8_t t58 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0));
            if (!t58) goto ie57;
            uint64_t t59 = gm_l8;
            uint8_t t60 = (!(t59 == ((uint64_t)3ULL)));
            if (!t60) goto ie57;
            {
                void * t62 = gm_l0;
                gm_tl874t2atnlh3 t63 = gm_l5;
                gm_t3877pk7lv748 t64;
                void * t65 = gm_l0;
                gm_t88rcucg1b7uk t66 = gm_l2;
                gm_b16_8 t67 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t67);
                uint64_t t68 = gm_l8;
                GM_ST(uint64_t, (uint8_t *)&t64 + 16, t68);
                gm_tabb710d9mq2e t69;
                gm_f_format_c8fgv1v((void *)&t69, (void *)NULL, cx, NULL, &t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srci09ctgf0djoe, 348, 32);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t63, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t73 = gm_l0;
                void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
                gm_tl874t2atnlh3 t75 = gm_l5;
                void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t78 = gm_l0;
            void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
            gm_tl874t2atnlh3 t80 = gm_l5;
            void * t81 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t81;
        }
        {
            uint8_t t83 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sg1h22ve4uqmq, 3) == 0));
            if (!t83) goto ie82;
            {
                gm_l18 = ((void *)0);
            }
            {
                {
                    gm_l20 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t86 = gm_l8;
                    gm_l19 = t86;
                }
                {
                    for (;;) {
                        uint64_t t88 = gm_l20;
                        uint64_t t89 = gm_l19;
                        uint8_t t90 = (t88 < t89);
                        if (!t90) goto lb87;
                        {
                            uint64_t t93 = gm_l20;
                            uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&t93));
                            gm_l21 = t96;
                            {
                                void * t98 = gm_l0;
                                gm_ts6oqppjh0ba3 t99 = gm_l12;
                                void * t100 = gm_l0;
                                void * t101 = gm_l11;
                                gm_t88rcucg1b7uk t102 = gm_l9;
                                uint64_t t103 = gm_l21;
                                void * t104 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t100, t101, t102, t103);
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                void * t105 = gm_l0;
                                void * t106 = gm_l16;
                                gm_t88rcucg1b7uk t107 = gm_l9;
                                uint64_t t108 = gm_l21;
                                void * t109 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t105, t106, t107, t108);
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                gm_tl874t2atnlh3 t110 = gm_l5;
                                void * t111 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t98, t99, t104, t109, t110);
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                gm_l22 = t111;
                            }
                            {
                                void * t112;
                                void * t114 = gm_l18;
                                if (!(GM_LD(void *, ((uint8_t *)&t114)) != 0)) goto ie113;
                                void * t115 = GM_LD(void *, ((uint8_t *)&t114));
                                gm_l23 = t115;
                                void * t117 = gm_l0;
                                gm_ts6oqppjh0ba3 t118 = gm_l13;
                                void * t119 = gm_l23;
                                void * t120 = gm_l22;
                                gm_tl874t2atnlh3 t121 = gm_l5;
                                void * t122 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t117, t118, t119, t120, t121);
                                if (gm_unw.kind) {
                                    goto gm_ll91;
                                }
                                void * t123 = t122;
                                t112 = t123;
                                goto ix113;
ie113:;
                                void * t125 = gm_l22;
                                void * t126 = t125;
                                t112 = t126;
ix113:;
                                gm_l18 = t112;
                            }
                            goto me94;
mn95:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 354, 17);
me94:;
                        }
lc87:;
                        {
                            uint64_t t128 = gm_l20;
                            uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                            uint64_t t131 = r129;
                            gm_l20 = t131;
                        }
                    }
gm_ll91:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb87; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc87; }
                    }
                    goto gm_lad;
lb87:;
                }
            }
            {
                void * t132 = gm_l18;
                void * t133;
                if ((GM_LD(void *, ((uint8_t *)&t132)) != 0)) {
                    void * t134 = GM_LD(void *, (((uint8_t *)&t132) + 0));
                    t133 = t134;
                } else {
                    void * t135 = gm_l0;
                    void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                    gm_tl874t2atnlh3 t137 = gm_l5;
                    void * t138 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t136, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t139 = 0;
                    F.ret = t138;
                    goto gm_fret;
                }
                gm_l17 = t133;
            }
            goto ix82;
ie82:;
            uint8_t t141 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sgvnpogt67see, 5) == 0));
            if (!t141) goto ie140;
            {
                gm_f_list_vp1djv1_2((void *)&F.l24, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    {
                        gm_t2lqu8jsj511d t145;
                        GM_ST(uint64_t, ((uint8_t *)&t145 + 0), ((uint64_t)1ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t145 + 8), ((uint64_t)2ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 0, t145);
                        gm_t2lqu8jsj511d t146;
                        GM_ST(uint64_t, ((uint8_t *)&t146 + 0), ((uint64_t)2ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t146 + 8), ((uint64_t)0ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 16, t146);
                        gm_t2lqu8jsj511d t147;
                        GM_ST(uint64_t, ((uint8_t *)&t147 + 0), ((uint64_t)0ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t147 + 8), ((uint64_t)1ULL));
                        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&F.l25 + 32, t147);
                    }
                    void * t148 = ((uint8_t *)&F.l25);
                    if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srci09ctgf0djoe, 361, 17);
                    gm_b16_8 t149;
                    GM_ST(void *, (uint8_t *)&t149, (void *)(((uint8_t *)t148 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                    gm_b16_8 t150 = t149;
                    gm_l26 = t150;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t152 = gm_l27;
                        uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t154 = (t152 < t153);
                        if (!t154) goto lb151;
                        {
                            uint64_t t157 = gm_l27;
                            uint64_t t158 = (uintptr_t)t157;
                            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            gm_t2lqu8jsj511d t162 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t158 * 16));
                            gm_l28 = t162;
                            {
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 0));
                                gm_l29 = t164;
                            }
                            {
                                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                gm_l30 = t165;
                            }
                            {
                                void * t166 = gm_l0;
                                gm_ts6oqppjh0ba3 t167 = gm_l12;
                                void * t168 = gm_l0;
                                void * t169 = gm_l11;
                                gm_t88rcucg1b7uk t170 = gm_l9;
                                uint64_t t171 = gm_l29;
                                void * t172 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t168, t169, t170, t171);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                void * t173 = gm_l0;
                                void * t174 = gm_l16;
                                gm_t88rcucg1b7uk t175 = gm_l9;
                                uint64_t t176 = gm_l30;
                                void * t177 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t173, t174, t175, t176);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                gm_tl874t2atnlh3 t178 = gm_l5;
                                void * t179 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t166, t167, t172, t177, t178);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                gm_l31 = t179;
                            }
                            {
                                void * t180 = gm_l0;
                                gm_ts6oqppjh0ba3 t181 = gm_l12;
                                void * t182 = gm_l0;
                                void * t183 = gm_l11;
                                gm_t88rcucg1b7uk t184 = gm_l9;
                                uint64_t t185 = gm_l30;
                                void * t186 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t182, t183, t184, t185);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                void * t187 = gm_l0;
                                void * t188 = gm_l16;
                                gm_t88rcucg1b7uk t189 = gm_l9;
                                uint64_t t190 = gm_l29;
                                void * t191 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t187, t188, t189, t190);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                gm_tl874t2atnlh3 t192 = gm_l5;
                                void * t193 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t180, t181, t186, t191, t192);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                gm_l32 = t193;
                            }
                            {
                                void * t194 = (void *)((uint8_t *)&F.l24);
                                void * t195 = gm_l0;
                                gm_ts6oqppjh0ba3 t196 = gm_l14;
                                void * t197 = gm_l31;
                                void * t198 = gm_l32;
                                gm_tl874t2atnlh3 t199 = gm_l5;
                                void * t200 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t195, t196, t197, t198, t199);
                                if (gm_unw.kind) {
                                    goto gm_ll155;
                                }
                                gm_tftb1odc739t5 t201 = gm_f_push_d943vgp_2(t194, &(*(gm_t69m56meqefs8 *)((uint8_t *)t200)));
                                if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srci09ctgf0djoe, 366, 21);
                                gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                            }
                            goto me160;
mn161:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 361, 17);
me160:;
                        }
lc151:;
                        {
                            uint64_t t204 = gm_l27;
                            uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                            uint64_t t207 = r205;
                            gm_l27 = t207;
                        }
                    }
gm_ll155:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb151; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc151; }
                    }
                    goto gm_lad;
lb151:;
                }
            }
            {
                gm_t88rcucg1b7uk t208 = gm_l2;
                gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                gm_teti411eujvf4 t210;
                memset(&t210, 0, sizeof t210);
                GM_ST(uint8_t, ((uint8_t *)&t210 + 0), ((uint8_t)36ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t210 + 8), t209);
                gm_tl874t2atnlh3 t211 = gm_l5;
                void * t212 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t208, &t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t212;
            }
            goto ix140;
ie140:;
            {
                gm_tvdg94bk7aa22 t214;
                uint8_t t216 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0));
                if (!t216) goto ie215;
                gm_tvdg94bk7aa22 t218;
                GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t218 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)16ULL));
                gm_tvdg94bk7aa22 t219 = t218;
                t214 = t219;
                goto ix215;
ie215:;
                gm_tvdg94bk7aa22 t221;
                GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t221 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)17ULL));
                gm_tvdg94bk7aa22 t222 = t221;
                t214 = t222;
ix215:;
                gm_l33 = t214;
            }
            {
                gm_f_list_vp1djv1_2((void *)&F.l34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_l36 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t224 = gm_l8;
                    gm_l35 = t224;
                }
                {
                    for (;;) {
                        uint64_t t226 = gm_l36;
                        uint64_t t227 = gm_l35;
                        uint8_t t228 = (t226 < t227);
                        if (!t228) goto lb225;
                        {
                            uint64_t t231 = gm_l36;
                            uint64_t t234 = GM_LD(uint64_t, ((uint8_t *)&t231));
                            gm_l37 = t234;
                            {
                            }
                            {
                                void * t236 = (void *)((uint8_t *)&F.l34);
                                gm_tvdg94bk7aa22 t237 = gm_l33;
                                gm_t88rcucg1b7uk t238 = gm_l9;
                                {
                                    void * t240 = gm_l0;
                                    void * t241 = gm_l11;
                                    gm_t88rcucg1b7uk t242 = gm_l9;
                                    uint64_t t243 = gm_l37;
                                    void * t244 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t240, t241, t242, t243);
                                    if (gm_unw.kind) {
                                        goto gm_ll229;
                                    }
                                    void * t245 = gm_l0;
                                    void * t246 = gm_l16;
                                    gm_t88rcucg1b7uk t247 = gm_l9;
                                    uint64_t t248 = gm_l37;
                                    void * t249 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t245, t246, t247, t248);
                                    if (gm_unw.kind) {
                                        goto gm_ll229;
                                    }
                                    gm_t5fru0lp11n7t t250;
                                    GM_ST(void *, ((uint8_t *)&t250 + 0), t244);
                                    GM_ST(void *, ((uint8_t *)&t250 + 8), t249);
                                    gm_l38 = t250;
                                }
                                void * t251 = ((uint8_t *)&gm_l38);
                                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 372, 64);
                                gm_b16_8 t252;
                                GM_ST(void *, (uint8_t *)&t252, (void *)(((uint8_t *)t251 + (uintptr_t)0 * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                                gm_b16_8 t253 = t252;
                                gm_tl874t2atnlh3 t254 = gm_l5;
                                void * t255 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t237, t238, t253, t254);
                                if (gm_unw.kind) {
                                    goto gm_ll229;
                                }
                                gm_tftb1odc739t5 t256 = gm_f_push_d943vgp_2(t236, &(*(gm_t69m56meqefs8 *)((uint8_t *)t255)));
                                if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 1, gm_srci09ctgf0djoe, 372, 35);
                                gm_b0_1 t257 = GM_LD(gm_b0_1, (((uint8_t *)&t256) + 1));
                            }
                            goto me232;
mn233:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 372, 17);
me232:;
                        }
lc225:;
                        {
                            uint64_t t259 = gm_l36;
                            uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                            uint64_t t262 = r260;
                            gm_l36 = t262;
                        }
                    }
gm_ll229:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb225; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc225; }
                    }
                    goto gm_lad;
lb225:;
                }
            }
            {
                gm_t88rcucg1b7uk t263 = gm_l2;
                gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                gm_teti411eujvf4 t265;
                memset(&t265, 0, sizeof t265);
                GM_ST(uint8_t, ((uint8_t *)&t265 + 0), ((uint8_t)36ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t265 + 8), t264);
                gm_tl874t2atnlh3 t266 = gm_l5;
                void * t267 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t263, &t265, t266);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t267;
            }
ix140:;
ix82:;
        }
        {
            void * t268 = gm_l17;
            gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t268) + 0));
            gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t271 = gm_l17;
            gm_tl874t2atnlh3 t272 = gm_l5;
            void * t273 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t269, t270, t271, t272);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t274 = 0;
            F.ret = t273;
            goto gm_fret;
        }
        goto me27;
mn28:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbl3pbs9195ho, 3) == 0)) goto mn275;
        {
            void * t278 = gm_l0;
            gm_b16_8 t279 = gm_l4;
            gm_b16_8 t280 = gm_l3;
            gm_tl874t2atnlh3 t281 = gm_l5;
            uint8_t t282 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t278, t279, ((uint64_t)0ULL), t280, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t283 = (uint8_t)!t282;
            if (!t283) goto ie277;
            {
                void * t285 = gm_l0;
                void * t286 = GM_LD(void *, (((uint8_t *)t285) + 0));
                gm_tl874t2atnlh3 t287 = gm_l5;
                void * t288 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t286, t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t289 = 0;
                F.ret = t288;
                goto gm_fret;
            }
            goto ix277;
ie277:;
ix277:;
        }
        {
            uint8_t t291 = 0;
            void * t292 = gm_l6;
            gm_t88rcucg1b7uk t293 = gm_l9;
            uint8_t t294 = gm_f_is_signed_integer_s66djm7(t292, GM_CONV(gm_csguankko4lmhd, t293));
            if (!t294) {
                void * t295 = gm_l6;
                gm_t88rcucg1b7uk t296 = gm_l9;
                uint8_t t297 = gm_f_is_float_3jk89qu(t295, GM_CONV(gm_csguankko4lmhd, t296));
                t291 = t297;
            } else t291 = 1;
            uint8_t t298 = (uint8_t)!t291;
            if (!t298) goto ie290;
            {
                void * t300 = gm_l0;
                gm_tl874t2atnlh3 t301 = gm_l5;
                void * t302 = gm_l0;
                gm_t88rcucg1b7uk t303 = gm_l9;
                gm_b16_8 t304 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t302, t303);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t305;
                GM_ST(gm_b16_8, ((uint8_t *)&t305 + 0), t304);
                gm_tabb710d9mq2e t306;
                gm_f_format_gmfdekn((void *)&t306, (void *)NULL, cx, NULL, t305);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 8, gm_srci09ctgf0djoe, 380, 32);
                gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&t306) + 8));
                gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&t307));
                gm_b0_1 t309 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t300, t301, t308);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t310 = gm_l0;
                void * t311 = GM_LD(void *, (((uint8_t *)t310) + 0));
                gm_tl874t2atnlh3 t312 = gm_l5;
                void * t313 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t311, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t314 = 0;
                F.ret = t313;
                goto gm_fret;
            }
            goto ix290;
ie290:;
ix290:;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l39, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                uint64_t t316 = gm_l8;
                gm_l40 = t316;
            }
            {
                for (;;) {
                    uint64_t t318 = gm_l41;
                    uint64_t t319 = gm_l40;
                    uint8_t t320 = (t318 < t319);
                    if (!t320) goto lb317;
                    {
                        uint64_t t323 = gm_l41;
                        uint64_t t326 = GM_LD(uint64_t, ((uint8_t *)&t323));
                        gm_l42 = t326;
                        {
                        }
                        {
                            void * t328 = (void *)((uint8_t *)&F.l39);
                            gm_tvdg94bk7aa22 t329;
                            GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t329 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)19ULL));
                            gm_t88rcucg1b7uk t330 = gm_l9;
                            {
                                void * t332 = gm_l0;
                                void * t333 = gm_l11;
                                gm_t88rcucg1b7uk t334 = gm_l9;
                                uint64_t t335 = gm_l42;
                                void * t336 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t332, t333, t334, t335);
                                if (gm_unw.kind) {
                                    goto gm_ll321;
                                }
                                gm_tn3r4rvlh0rsv t337;
                                GM_ST(void *, ((uint8_t *)&t337 + 0), t336);
                                gm_l43 = t337;
                            }
                            void * t338 = ((uint8_t *)&gm_l43);
                            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 384, 62);
                            gm_b16_8 t339;
                            GM_ST(void *, (uint8_t *)&t339, (void *)(((uint8_t *)t338 + (uintptr_t)0 * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t339 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                            gm_b16_8 t340 = t339;
                            gm_tl874t2atnlh3 t341 = gm_l5;
                            void * t342 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t329, t330, t340, t341);
                            if (gm_unw.kind) {
                                goto gm_ll321;
                            }
                            gm_tftb1odc739t5 t343 = gm_f_push_d943vgp_2(t328, &(*(gm_t69m56meqefs8 *)((uint8_t *)t342)));
                            if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 1, gm_srci09ctgf0djoe, 384, 31);
                            gm_b0_1 t344 = GM_LD(gm_b0_1, (((uint8_t *)&t343) + 1));
                        }
                        goto me324;
mn325:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 384, 13);
me324:;
                    }
lc317:;
                    {
                        uint64_t t346 = gm_l41;
                        uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                        uint64_t t349 = r347;
                        gm_l41 = t349;
                    }
                }
gm_ll321:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb317; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc317; }
                }
                goto gm_lad;
lb317:;
            }
        }
        {
            gm_t88rcucg1b7uk t350 = gm_l2;
            gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
            gm_teti411eujvf4 t352;
            memset(&t352, 0, sizeof t352);
            GM_ST(uint8_t, ((uint8_t *)&t352 + 0), ((uint8_t)36ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t352 + 8), t351);
            gm_tl874t2atnlh3 t353 = gm_l5;
            void * t354 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t350, &t352, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t354;
        }
        {
            gm_t88rcucg1b7uk t355 = gm_l2;
            gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t357 = gm_l44;
            gm_tl874t2atnlh3 t358 = gm_l5;
            void * t359 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t355, t356, t357, t358);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t360 = 0;
            F.ret = t359;
            goto gm_fret;
        }
        goto me27;
mn275:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6fdn1h3c830p, 4) == 0)) goto mn361;
        {
            void * t364 = gm_l0;
            gm_b16_8 t365 = gm_l4;
            gm_b16_8 t366 = gm_l3;
            gm_tl874t2atnlh3 t367 = gm_l5;
            uint8_t t368 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t364, t365, ((uint64_t)0ULL), t366, t367);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t369 = (uint8_t)!t368;
            if (!t369) goto ie363;
            {
                void * t371 = gm_l0;
                void * t372 = GM_LD(void *, (((uint8_t *)t371) + 0));
                gm_tl874t2atnlh3 t373 = gm_l5;
                void * t374 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t372, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t375 = 0;
                F.ret = t374;
                goto gm_fret;
            }
            goto ix363;
ie363:;
ix363:;
        }
        {
            void * t377 = gm_l6;
            gm_t88rcucg1b7uk t378 = gm_l9;
            uint8_t t379 = gm_f_is_float_3jk89qu(t377, GM_CONV(gm_csguankko4lmhd, t378));
            uint8_t t380 = (uint8_t)!t379;
            if (!t380) goto ie376;
            {
                void * t382 = gm_l0;
                gm_tl874t2atnlh3 t383 = gm_l5;
                void * t384 = gm_l0;
                gm_t88rcucg1b7uk t385 = gm_l9;
                gm_b16_8 t386 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t384, t385);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t387;
                GM_ST(gm_b16_8, ((uint8_t *)&t387 + 0), t386);
                gm_tabb710d9mq2e t388;
                gm_f_format_mcgdakk((void *)&t388, (void *)NULL, cx, NULL, t387);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t388))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t388) + 8, gm_srci09ctgf0djoe, 391, 32);
                gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)&t388) + 8));
                gm_b16_8 t390 = GM_LD(gm_b16_8, ((uint8_t *)&t389));
                gm_b0_1 t391 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t382, t383, t390);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t392 = gm_l0;
                void * t393 = GM_LD(void *, (((uint8_t *)t392) + 0));
                gm_tl874t2atnlh3 t394 = gm_l5;
                void * t395 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t393, t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t396 = 0;
                F.ret = t395;
                goto gm_fret;
            }
            goto ix376;
ie376:;
ix376:;
        }
        {
            gm_l45 = ((void *)0);
        }
        {
            {
                gm_l47 = ((uint64_t)0ULL);
            }
            {
                uint64_t t398 = gm_l8;
                gm_l46 = t398;
            }
            {
                for (;;) {
                    uint64_t t400 = gm_l47;
                    uint64_t t401 = gm_l46;
                    uint8_t t402 = (t400 < t401);
                    if (!t402) goto lb399;
                    {
                        uint64_t t405 = gm_l47;
                        uint64_t t408 = GM_LD(uint64_t, ((uint8_t *)&t405));
                        gm_l48 = t408;
                        {
                            void * t410 = gm_l0;
                            void * t411 = gm_l11;
                            gm_t88rcucg1b7uk t412 = gm_l9;
                            uint64_t t413 = gm_l48;
                            void * t414 = gm_f_math_elem_iuerhch((void *)NULL, cx, NULL, t410, t411, t412, t413);
                            if (gm_unw.kind) {
                                goto gm_ll403;
                            }
                            gm_l49 = t414;
                        }
                        {
                            void * t415 = gm_l0;
                            gm_ts6oqppjh0ba3 t416 = gm_l12;
                            void * t417 = gm_l49;
                            void * t418 = gm_l49;
                            gm_tl874t2atnlh3 t419 = gm_l5;
                            void * t420 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t415, t416, t417, t418, t419);
                            if (gm_unw.kind) {
                                goto gm_ll403;
                            }
                            gm_l50 = t420;
                        }
                        {
                            void * t421;
                            void * t423 = gm_l45;
                            if (!(GM_LD(void *, ((uint8_t *)&t423)) != 0)) goto ie422;
                            void * t424 = GM_LD(void *, ((uint8_t *)&t423));
                            gm_l51 = t424;
                            void * t426 = gm_l0;
                            gm_ts6oqppjh0ba3 t427 = gm_l13;
                            void * t428 = gm_l51;
                            void * t429 = gm_l50;
                            gm_tl874t2atnlh3 t430 = gm_l5;
                            void * t431 = gm_f_arith_op_hsstk43((void *)NULL, cx, NULL, t426, t427, t428, t429, t430);
                            if (gm_unw.kind) {
                                goto gm_ll403;
                            }
                            void * t432 = t431;
                            t421 = t432;
                            goto ix422;
ie422:;
                            void * t434 = gm_l50;
                            void * t435 = t434;
                            t421 = t435;
ix422:;
                            gm_l45 = t421;
                        }
                        goto me406;
mn407:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 395, 13);
me406:;
                    }
lc399:;
                    {
                        uint64_t t437 = gm_l47;
                        uint64_t r438; bool o439 = __builtin_add_overflow(t437, ((uint64_t)1ULL), &r438);
                        uint64_t t440 = r438;
                        gm_l47 = t440;
                    }
                }
gm_ll403:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb399; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc399; }
                }
                goto gm_lad;
lb399:;
            }
        }
        {
            void * t441 = gm_l45;
            void * t442;
            if ((GM_LD(void *, ((uint8_t *)&t441)) != 0)) {
                void * t443 = GM_LD(void *, (((uint8_t *)&t441) + 0));
                t442 = t443;
            } else {
                void * t444 = gm_l0;
                void * t445 = GM_LD(void *, (((uint8_t *)t444) + 0));
                gm_tl874t2atnlh3 t446 = gm_l5;
                void * t447 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t445, t446);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t448 = 0;
                F.ret = t447;
                goto gm_fret;
            }
            gm_l52 = t442;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t449;
            GM_ST(uint8_t, (uint8_t *)&t449 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t449 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t449 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t449 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t449 + 0), ((uint8_t)26ULL));
            gm_t88rcucg1b7uk t450 = gm_l9;
            {
                void * t452 = gm_l52;
                gm_tn3r4rvlh0rsv t453;
                GM_ST(void *, ((uint8_t *)&t453 + 0), t452);
                gm_l53 = t453;
            }
            void * t454 = ((uint8_t *)&gm_l53);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 401, 42);
            gm_b16_8 t455;
            GM_ST(void *, (uint8_t *)&t455, (void *)(((uint8_t *)t454 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t456 = t455;
            gm_tl874t2atnlh3 t457 = gm_l5;
            void * t458 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t449, t450, t456, t457);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l54 = t458;
        }
        {
            void * t459 = gm_l54;
            gm_t88rcucg1b7uk t460 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t459) + 0));
            gm_b16_8 t461 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            void * t462 = gm_l54;
            gm_tl874t2atnlh3 t463 = gm_l5;
            void * t464 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t460, t461, t462, t463);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t465 = 0;
            F.ret = t464;
            goto gm_fret;
        }
        goto me27;
mn361:;
        goto me27;
mn466:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 342, 5);
me27:;
    }
    {
        gm_b16_8 t468;
        uint8_t t470 = 0;
        gm_b16_8 t471 = gm_l3;
        gm_tlg1vmfbpbvtq t472 = GM_LD(gm_tlg1vmfbpbvtq, gm_cukabrstocl);
        uint8_t t473 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(27); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t472) + i * 16, ((uint8_t *)&t471))) { t473 = 1; break; }
        uint8_t t474 = (uint8_t)(t473);
        if (!t474) {
            gm_b16_8 t475 = gm_l3;
            gm_tfeab279c16rr t476;
            gm_b16_8 t477;
            GM_ST(void *, (uint8_t *)&t477, (void *)(gm_sbb1ri5er9djq));
            GM_ST(uintptr_t, (uint8_t *)&t477 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t476 + 0, t477);
            gm_b16_8 t478;
            GM_ST(void *, (uint8_t *)&t478, (void *)(gm_snivf25e5fme8));
            GM_ST(uintptr_t, (uint8_t *)&t478 + 8, (uintptr_t)(5));
            GM_ST(gm_b16_8, (uint8_t *)&t476 + 16, t478);
            gm_b16_8 t479;
            GM_ST(void *, (uint8_t *)&t479, (void *)(gm_s4pmpk07ugvq6));
            GM_ST(uintptr_t, (uint8_t *)&t479 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t476 + 32, t479);
            uint8_t t480 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t476) + 0, ((uint8_t *)&t475))) t480 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t476) + 16, ((uint8_t *)&t475))) t480 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t476) + 32, ((uint8_t *)&t475))) t480 = 1;
            uint8_t t481 = (uint8_t)(t480);
            t470 = t481;
        } else t470 = 1;
        if (!t470) goto ie469;
        gm_b16_8 t483 = gm_l3;
        gm_tnpifv20irnph t484;
        GM_ST(gm_b16_8, ((uint8_t *)&t484 + 0), t483);
        gm_tabb710d9mq2e t485;
        gm_f_format_86pfrlj((void *)&t485, (void *)NULL, cx, NULL, t484);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t485))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t485) + 8, gm_srci09ctgf0djoe, 407, 9);
        gm_b16_8 t486 = GM_LD(gm_b16_8, (((uint8_t *)&t485) + 8));
        gm_b16_8 t487 = t486;
        gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
        t468 = t488;
        goto ix469;
ie469:;
        gm_b16_8 t490;
        GM_ST(void *, (uint8_t *)&t490, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t490 + 8, (uintptr_t)(0));
        gm_b16_8 t491 = t490;
        t468 = t491;
ix469:;
        gm_l55 = t468;
    }
    {
        void * t492 = gm_l0;
        gm_tl874t2atnlh3 t493 = gm_l5;
        gm_t6j6smj7qoctu t494;
        gm_b16_8 t495 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t494 + 0, t495);
        gm_b16_8 t496 = gm_l55;
        GM_ST(gm_b16_8, (uint8_t *)&t494 + 16, t496);
        gm_tabb710d9mq2e t497;
        gm_f_format_iepqo20((void *)&t497, (void *)NULL, cx, NULL, &t494);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t497))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t497) + 8, gm_srci09ctgf0djoe, 411, 20);
        gm_b16_8 t498 = GM_LD(gm_b16_8, (((uint8_t *)&t497) + 8));
        gm_b16_8 t499 = GM_LD(gm_b16_8, ((uint8_t *)&t498));
        gm_b0_1 t500 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t492, t493, t499);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t501 = gm_l0;
        void * t502 = GM_LD(void *, (((uint8_t *)t501) + 0));
        gm_tl874t2atnlh3 t503 = gm_l5;
        void * t504 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t502, t503);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t505 = 0;
        F.ret = t504;
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
__attribute__((aligned(4))) uint8_t gm_f_mentions_complex_constan_lrgkqac(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_mentions_complex_constan_lrgkqac F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* l */
    void * gm_l3; /* inner */
    void * gm_l4; /* inner */
    void * gm_l5; /* lhs */
    void * gm_l6; /* rhs */
    GM_STACK_CHECK(&F, 633u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        void * t14 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t14;
        void * t15 = gm_l2;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 98));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me3;
mn12:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)9ULL)) goto mn18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t20;
        void * t21 = gm_l0;
        void * t22 = gm_l3;
        uint8_t t23 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me3;
mn18:;
ma6:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)12ULL)) goto mn25;
        void * t27 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t27;
        void * t28 = gm_l0;
        void * t29 = gm_l4;
        uint8_t t30 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me3;
mn25:;
ma7:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)13ULL)) goto mn32;
        void * t34 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t34;
        void * t35 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l6 = t35;
        uint8_t t36 = 0;
        void * t37 = gm_l0;
        void * t38 = gm_l5;
        uint8_t t39 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t39) {
            void * t40 = gm_l0;
            void * t41 = gm_l6;
            uint8_t t42 = gm_f_mentions_complex_constan_lrgkqac((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t36 = t42;
        } else t36 = 1;
        uint8_t t43 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me3;
mn32:;
ma8:;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t47 != ((uint8_t)6ULL)) goto pa46;
        goto po45;
pa46:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t49 != ((uint8_t)22ULL)) goto pa48;
        goto po45;
pa48:;
        goto mn44;
po45:;
        {
            void * t52 = gm_l0;
            void * t53 = gm_l1;
            gm_tkv1vap90vht2 t54;
            gm_f_try_untyped_8j8up35((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t54))[208] != 0)) goto ie51;
            F.l7 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t54) + 0));
            {
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                if (t57 != ((uint8_t)0ULL)) goto ie56;
                gm_t0ialelq3ql1k t58 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l7) + 8));
                F.l8 = t58;
                uint8_t t59;
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                if (t62 != ((uint8_t)4ULL)) goto mn61;
                t59 = ((uint8_t)1);
                goto me60;
mn61:;
                t59 = ((uint8_t)0);
                goto me60;
mn63:;
                gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 74, 36);
me60:;
                if (!t59) goto ie56;
                {
                    uint8_t t65 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix56;
ie56:;
ix56:;
            }
            goto ix51;
ie51:;
ix51:;
        }
        {
            uint8_t t66 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn44:;
ma9:;
        uint8_t t68 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn67:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 67, 5);
me3:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_plain_31j25h8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_promote_real_constant_v0nhili F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* agg */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_taari1s0m51a7 gm_l4; /* k */
    void * gm_l5; /* zero */
    GM_STACK_CHECK(&F, 526u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        uint8_t t4;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        t4 = ((uint8_t)0);
        goto me5;
mn11:;
ma7:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t14 != ((uint8_t)2ULL)) goto mn13;
        t4 = ((uint8_t)0);
        goto me5;
mn13:;
ma8:;
        t4 = ((uint8_t)1);
        goto me5;
mn15:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 114, 8);
me5:;
        if (!t4) goto ie2;
        {
            uint8_t t17 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 96);
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_taari1s0m51a7 t25 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t19, t22, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t25;
    }
    {
        uint8_t t27 = 0;
        uint8_t t28;
        if (((((uint8_t *)&gm_l4))[1] != 0)) goto mn30;
        t28 = ((uint8_t)1);
        goto me29;
mn30:;
        t28 = ((uint8_t)0);
        goto me29;
mn31:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 116, 8);
me29:;
        if (!t28) {
            gm_taari1s0m51a7 t32 = gm_l4;
            gm_t9qkh0rsooc8d t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t34;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[1] = 1;
            uint8_t t35 = (gm_eq5307j5k9ms(((uint8_t *)&t32), ((uint8_t *)&t34)));
            t27 = t35;
        } else t27 = 1;
        if (!t27) goto ie26;
        {
            uint8_t t37 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 96);
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
        void * t43 = (void *)(((uint8_t *)t42) + 8);
        void * t44 = gm_l1;
        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 0));
        gm_t88rcucg1b7uk t46 = gm_l3;
        gm_t9qkh0rsooc8d t47 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t40, t43, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48;
        if (!((((uint8_t *)&t47))[0] != 0)) goto mn50;
        t48 = ((uint8_t)1);
        goto me49;
mn50:;
        t48 = ((uint8_t)0);
        goto me49;
mn51:;
        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 117, 8);
me49:;
        if (!t48) goto ie38;
        {
            uint8_t t53 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_t88rcucg1b7uk t54 = gm_l3;
        gm_tngtuiuskn938 t55;
        gm_f_zero_9r6o7sj((void *)&t55, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t56;
        gm_f_untyped_float_m6lbr0g((void *)&t56, (void *)NULL, cx, NULL, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t57 = gm_l1;
        gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
        void * t59 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t56, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t59;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t60 = (void *)((uint8_t *)&F.l6);
        void * t61 = gm_l1;
        gm_tftb1odc739t5 t62 = gm_f_push_d943vgp_2(t60, &(*(gm_t69m56meqefs8 *)((uint8_t *)t61)));
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srci09ctgf0djoe, 120, 5);
        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
    }
    {
        void * t64 = (void *)((uint8_t *)&F.l6);
        void * t65 = gm_l5;
        gm_tftb1odc739t5 t66 = gm_f_push_d943vgp_2(t64, &(*(gm_t69m56meqefs8 *)((uint8_t *)t65)));
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srci09ctgf0djoe, 121, 5);
        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
    }
    {
        gm_t88rcucg1b7uk t68 = gm_l2;
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_teti411eujvf4 t70;
        memset(&t70, 0, sizeof t70);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t69);
        void * t71 = gm_l1;
        gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
        void * t73 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t68, &t70, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t74 = 0;
        F.ret = t73;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_settle_complex_operands_9439n4u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_settle_complex_operands_9439n4u F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* r */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* lc */
    uint8_t gm_l7; /* rc */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* side */
    gm_t88rcucg1b7uk gm_l11; /* var */
    gm_t88rcucg1b7uk gm_l12; /* other0 */
    uint8_t gm_l13; /* var_left */
    gm_t88rcucg1b7uk gm_l14; /* other */
    uint32_t gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l16; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l17; /* t */
    gm_tqsk8dt6ib3qq gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_t88rcucg1b7uk gm_l21; /* t */
    GM_STACK_CHECK(&F, 932u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        uint8_t t8 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        uint8_t t12 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t12;
    }
    {
        uint8_t t14 = gm_l6;
        uint8_t t15 = (uint8_t)!t14;
        if (!t15) goto ie13;
        uint8_t t16 = gm_l7;
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie13;
        {
            gm_b0_1 t19;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t22 = gm_l6;
        if (!t22) goto ie21;
        uint8_t t23 = gm_l7;
        if (!t23) goto ie21;
        {
            {
                void * t26 = gm_l0;
                void * t27 = (void *)(((uint8_t *)t26) + 96);
                void * t28 = gm_l5;
                void * t29 = gm_l2;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
                void * t31 = gm_l3;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
                gm_t9qkh0rsooc8d t33 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t27, t28, t30, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            gm_l8 = ((uint64_t)2ULL);
        }
        {
            for (;;) {
                uint64_t t36 = gm_l9;
                uint64_t t37 = gm_l8;
                uint8_t t38 = (t36 < t37);
                if (!t38) goto lb35;
                {
                    uint64_t t41 = gm_l9;
                    uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t41));
                    gm_l10 = t44;
                    {
                        gm_t88rcucg1b7uk t46;
                        uint64_t t48 = gm_l10;
                        uint8_t t49 = ((t48 == ((uint64_t)0ULL)));
                        if (!t49) goto ie47;
                        void * t51 = gm_l2;
                        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t51) + 0));
                        gm_t88rcucg1b7uk t53 = t52;
                        t46 = t53;
                        goto ix47;
ie47:;
                        void * t55 = gm_l3;
                        gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t55) + 0));
                        gm_t88rcucg1b7uk t57 = t56;
                        t46 = t57;
ix47:;
                        gm_l11 = t46;
                    }
                    {
                        gm_t88rcucg1b7uk t58;
                        uint64_t t60 = gm_l10;
                        uint8_t t61 = ((t60 == ((uint64_t)0ULL)));
                        if (!t61) goto ie59;
                        void * t63 = gm_l3;
                        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
                        gm_t88rcucg1b7uk t65 = t64;
                        t58 = t65;
                        goto ix59;
ie59:;
                        void * t67 = gm_l2;
                        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + 0));
                        gm_t88rcucg1b7uk t69 = t68;
                        t58 = t69;
ix59:;
                        gm_l12 = t58;
                    }
                    {
                        uint64_t t70 = gm_l10;
                        uint8_t t71 = ((t70 == ((uint64_t)0ULL)));
                        gm_l13 = t71;
                    }
                    {
                        void * t73 = gm_l0;
                        gm_t88rcucg1b7uk t74 = gm_l11;
                        uint8_t t75 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t73, t74);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        uint8_t t76 = (uint8_t)!t75;
                        if (!t76) goto ie72;
                        {
                            goto lc35;
                        }
                        goto ix72;
ie72:;
ix72:;
                    }
                    {
                        void * t78 = gm_l0;
                        void * t79 = (void *)(((uint8_t *)t78) + 96);
                        void * t80 = gm_l0;
                        void * t81 = GM_LD(void *, (((uint8_t *)t80) + 0));
                        gm_t88rcucg1b7uk t82 = gm_l12;
                        gm_t88rcucg1b7uk t83 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t79, t81, t82);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        gm_l14 = t83;
                    }
                    {
                        void * t85 = gm_l0;
                        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                        gm_t88rcucg1b7uk t87 = gm_l14;
                        gm_tkn42u7na3t81 t88 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t86, t87);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        uint8_t t89;
                        if (((((uint8_t *)&t88))[4] != 0)) goto mn91;
                        t89 = ((uint8_t)0);
                        goto me90;
mn91:;
                        t89 = ((uint8_t)1);
                        goto me90;
mn92:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 96, 12);
me90:;
                        if (!t89) goto ie84;
                        {
                            {
                                void * t95 = gm_l0;
                                void * t96 = (void *)(((uint8_t *)t95) + 96);
                                void * t97 = gm_l5;
                                gm_t88rcucg1b7uk t98 = gm_l11;
                                gm_t88rcucg1b7uk t99 = gm_l14;
                                gm_t9qkh0rsooc8d t100 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t96, t97, t98, t99);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                            }
                        }
                        goto ix84;
ie84:;
                        uint8_t t102 = 0;
                        void * t103 = gm_l5;
                        gm_t88rcucg1b7uk t104 = gm_l14;
                        uint8_t t105 = gm_f_is_float_3jk89qu(t103, GM_CONV(gm_csguankko4lmhd, t104));
                        if (!t105) {
                            void * t106 = gm_l5;
                            gm_t88rcucg1b7uk t107 = gm_l14;
                            gm_t53at80pfdus3 t108;
                            gm_f_kind_jcnt4hm((void *)&t108, t106, GM_CONV(gm_csguankko4lmhd, t107));
                            uint8_t t109;
                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t108) + 0));
                            if (t112 != ((uint8_t)19ULL)) goto mn111;
                            t109 = ((uint8_t)1);
                            goto me110;
mn111:;
                            t109 = ((uint8_t)0);
                            goto me110;
mn113:;
                            gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 98, 43);
me110:;
                            t102 = t109;
                        } else t102 = 1;
                        if (!t102) goto ie101;
                        uint8_t t114 = 0;
                        uint8_t t115;
                        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        if (t118 != ((uint8_t)2ULL)) goto mn117;
                        t115 = ((uint8_t)1);
                        goto me116;
mn117:;
                        t115 = ((uint8_t)0);
                        goto me116;
mn119:;
                        gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 98, 78);
me116:;
                        if (!t115) {
                            uint8_t t120 = 0;
                            uint8_t t121 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
                            if (t121) {
                                uint8_t t122 = gm_l13;
                                t120 = t122;
                            } else t120 = 0;
                            t114 = t120;
                        } else t114 = 1;
                        if (!t114) goto ie101;
                        {
                            void * t125 = gm_l0;
                            void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
                            gm_b16_8 t127;
                            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s0rphlcbno91v));
                            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(7));
                            gm_t9805qtonbsag t128 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t126, t127);
                            if (gm_unw.kind) {
                                goto gm_ll39;
                            }
                            if (!((((uint8_t *)&t128))[4] != 0)) goto ie124;
                            uint32_t t129 = GM_LD(uint32_t, ((uint8_t *)&t128));
                            gm_l15 = t129;
                            {
                            }
                            {
                                void * t131 = gm_l0;
                                void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
                                uint32_t t133 = gm_l15;
                                {
                                    gm_t88rcucg1b7uk t135 = gm_l14;
                                    gm_tnqk2qt7j42o9 t136;
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t136 + 0), t135);
                                    gm_l16 = t136;
                                }
                                void * t137 = ((uint8_t *)&gm_l16);
                                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srci09ctgf0djoe, 101, 46);
                                gm_b16_8 t138;
                                GM_ST(void *, (uint8_t *)&t138, (void *)(((uint8_t *)t137 + (uintptr_t)0 * 4)));
                                GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                                gm_b16_8 t139 = t138;
                                gm_t88rcucg1b7uk t140 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t132, t133, t139);
                                if (gm_unw.kind) {
                                    goto gm_ll39;
                                }
                                gm_l17 = t140;
                            }
                            {
                                {
                                    void * t142 = gm_l0;
                                    void * t143 = (void *)(((uint8_t *)t142) + 96);
                                    void * t144 = gm_l5;
                                    gm_t88rcucg1b7uk t145 = gm_l11;
                                    gm_t88rcucg1b7uk t146 = gm_l17;
                                    gm_t9qkh0rsooc8d t147 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t143, t144, t145, t146);
                                    if (gm_unw.kind) {
                                        goto gm_ll39;
                                    }
                                }
                            }
                            goto ix124;
ie124:;
ix124:;
                        }
                        goto ix101;
ie101:;
ix101:;
ix84:;
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 90, 5);
me42:;
                }
lc35:;
                {
                    uint64_t t149 = gm_l9;
                    uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                    uint64_t t152 = r150;
                    gm_l9 = t152;
                }
            }
gm_ll39:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
            }
            goto gm_lad;
lb35:;
        }
    }
    {
        {
            {
                void * t155 = gm_l2;
                gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t155) + 0));
                void * t157 = gm_l3;
                gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t157) + 0));
                gm_tqsk8dt6ib3qq t159;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t159 + 0), t156);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t159 + 4), t158);
                gm_l18 = t159;
            }
            void * t160 = ((uint8_t *)&gm_l18);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srci09ctgf0djoe, 106, 5);
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(((uint8_t *)t160 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t162 = t161;
            gm_l19 = t162;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t164 = gm_l20;
                uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t166 = (t164 < t165);
                if (!t166) goto lb163;
                {
                    uint64_t t169 = gm_l20;
                    uint64_t t170 = (uintptr_t)t169;
                    uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t170 * 4));
                    gm_l21 = t174;
                    {
                        void * t177 = gm_l0;
                        gm_t88rcucg1b7uk t178 = gm_l21;
                        uint8_t t179 = gm_f_is_complex_var_bk0jb4c((void *)NULL, cx, NULL, t177, t178);
                        if (gm_unw.kind) {
                            goto gm_ll167;
                        }
                        if (!t179) goto ie176;
                        {
                            {
                                void * t182 = gm_l0;
                                gm_t88rcucg1b7uk t183 = gm_l21;
                                gm_tl874t2atnlh3 t184 = gm_l4;
                                gm_t88rcucg1b7uk t185 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t182, t183, t184);
                                if (gm_unw.kind) {
                                    goto gm_ll167;
                                }
                            }
                        }
                        goto ix176;
ie176:;
ix176:;
                    }
                    goto me172;
mn173:;
                    gm_trap("no match arm matched the scrutinee", gm_srci09ctgf0djoe, 106, 5);
me172:;
                }
lc163:;
                {
                    uint64_t t187 = gm_l20;
                    uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                    uint64_t t190 = r188;
                    gm_l20 = t190;
                }
            }
gm_ll167:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb163; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc163; }
            }
            goto gm_lad;
lb163:;
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
