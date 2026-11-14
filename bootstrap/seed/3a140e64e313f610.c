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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_t04llc5uen0qo;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; void * f4; } gm_t0m68qtp7uj1u;
typedef struct { uint32_t f0; uint32_t f1; void * f2; void * f3; } gm_t0mrruemedngl;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint64_t f4; uint32_t f5; uint32_t f6; void * f7; } gm_t1lvkimu9u0hv;
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_t2cg11r39sm0e;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint32_t f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; uint32_t f3; } gm_t37c2jujtiadn;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; void * f4; } gm_t3a21dm0j52qn;
typedef struct { uint32_t f0; uint8_t f1; uint8_t p2[3]; void * f2; } gm_t3hcdie243ee3;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; } gm_t4h1llua034m2;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint32_t f2; uint32_t f3; } gm_t51d05oujnurh;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t f7; uint8_t p8[3]; uint32_t f8; uint32_t f9; } gm_t98k64j4g45rk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t f5; uint8_t f6; uint8_t p7[2]; uint64_t f7; void * f8; } gm_td7fbab3thbb2;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_te4o1hd8337m3;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; uint64_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint8_t p7[4]; void * f7; } gm_thbsr19fadfjo;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; } gm_thpdbp3q7rdrc;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[1]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_ti4ei62u4i1d9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_tjqgc9rpvqnkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_tn3qmqcq7vsk9;
typedef struct { uint32_t f0; uint32_t f1; void * f2; } gm_to8e1ootf3q7f;
typedef struct { void * f0; uint32_t f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_tpkdfv1vgiqij;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; uint64_t f3; void * f4; } gm_tu97pd4fjd5oe;
typedef struct { uint64_t f0; uint64_t f1; uint8_t f2; uint8_t p3[7]; uint64_t f3; } gm_tubi2bvif8j7u;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t04llc5uen0qo) == 32 && _Alignof(gm_t04llc5uen0qo) == 8 && offsetof(gm_t04llc5uen0qo, f0) == 0 && offsetof(gm_t04llc5uen0qo, f1) == 4 && offsetof(gm_t04llc5uen0qo, f2) == 8 && offsetof(gm_t04llc5uen0qo, f3) == 12 && offsetof(gm_t04llc5uen0qo, f4) == 16 && offsetof(gm_t04llc5uen0qo, f5) == 24, "layout of `(base: u32, off: u32, size: u32, align: u32, v: u32, n: ^Node)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_tubi2bvif8j7u f0; uint8_t f1; uint8_t pe[7]; } gm_t0hcjal40h40i;
_Static_assert(sizeof(gm_t0m68qtp7uj1u) == 24 && _Alignof(gm_t0m68qtp7uj1u) == 8 && offsetof(gm_t0m68qtp7uj1u, f0) == 0 && offsetof(gm_t0m68qtp7uj1u, f1) == 4 && offsetof(gm_t0m68qtp7uj1u, f2) == 8 && offsetof(gm_t0m68qtp7uj1u, f3) == 12 && offsetof(gm_t0m68qtp7uj1u, f4) == 16, "layout of `(at: u32, levels: u32, local: u32, size: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t0mrruemedngl) == 24 && _Alignof(gm_t0mrruemedngl) == 8 && offsetof(gm_t0mrruemedngl, f0) == 0 && offsetof(gm_t0mrruemedngl, f1) == 4 && offsetof(gm_t0mrruemedngl, f2) == 8 && offsetof(gm_t0mrruemedngl, f3) == 16, "layout of `(at: u32, r: u32, x: ^Node, n: ^Node)`");
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
_Static_assert(sizeof(gm_t1lvkimu9u0hv) == 40 && _Alignof(gm_t1lvkimu9u0hv) == 8 && offsetof(gm_t1lvkimu9u0hv, f0) == 0 && offsetof(gm_t1lvkimu9u0hv, f1) == 4 && offsetof(gm_t1lvkimu9u0hv, f2) == 8 && offsetof(gm_t1lvkimu9u0hv, f3) == 12 && offsetof(gm_t1lvkimu9u0hv, f4) == 16 && offsetof(gm_t1lvkimu9u0hv, f5) == 24 && offsetof(gm_t1lvkimu9u0hv, f6) == 28 && offsetof(gm_t1lvkimu9u0hv, f7) == 32, "layout of `(r: u32, p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
_Static_assert(sizeof(gm_t2cg11r39sm0e) == 24 && _Alignof(gm_t2cg11r39sm0e) == 8 && offsetof(gm_t2cg11r39sm0e, f0) == 0 && offsetof(gm_t2cg11r39sm0e, f1) == 8 && offsetof(gm_t2cg11r39sm0e, f2) == 16, "layout of `(r: u32, off: u64, packed: bool)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t37c2jujtiadn) == 16 && _Alignof(gm_t37c2jujtiadn) == 4 && offsetof(gm_t37c2jujtiadn, f0) == 0 && offsetof(gm_t37c2jujtiadn, f1) == 4 && offsetof(gm_t37c2jujtiadn, f2) == 8 && offsetof(gm_t37c2jujtiadn, f3) == 12, "layout of `(at: u32, size: u32, nonzero: bool, to: u32)`");
_Static_assert(sizeof(gm_t3a21dm0j52qn) == 32 && _Alignof(gm_t3a21dm0j52qn) == 8 && offsetof(gm_t3a21dm0j52qn, f0) == 0 && offsetof(gm_t3a21dm0j52qn, f1) == 4 && offsetof(gm_t3a21dm0j52qn, f2) == 8 && offsetof(gm_t3a21dm0j52qn, f3) == 16 && offsetof(gm_t3a21dm0j52qn, f4) == 24, "layout of `(p: u32, len: u32, i: u32, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_t3hcdie243ee3) == 16 && _Alignof(gm_t3hcdie243ee3) == 8 && offsetof(gm_t3hcdie243ee3, f0) == 0 && offsetof(gm_t3hcdie243ee3, f1) == 4 && offsetof(gm_t3hcdie243ee3, f2) == 8, "layout of `(at: u32, has_msg: bool, n: ^Node)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4h1llua034m2) == 16 && _Alignof(gm_t4h1llua034m2) == 8 && offsetof(gm_t4h1llua034m2, f0) == 0 && offsetof(gm_t4h1llua034m2, f1) == 4 && offsetof(gm_t4h1llua034m2, f2) == 8, "layout of `(p: u32, i: u32, esz: u64)`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; uint64_t f2; unsigned __int128 f3; uint64_t f4; uint64_t f5; uint64_t f6; void * f7; } gm_t4nomjfe3hlif;
_Static_assert(sizeof(gm_t51d05oujnurh) == 16 && _Alignof(gm_t51d05oujnurh) == 4 && offsetof(gm_t51d05oujnurh, f0) == 0 && offsetof(gm_t51d05oujnurh, f1) == 4 && offsetof(gm_t51d05oujnurh, f2) == 8 && offsetof(gm_t51d05oujnurh, f3) == 12, "layout of `Move`");
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint32_t f1; uint64_t f2; uint8_t f3; uint8_t p4[3]; gm_t9805qtonbsag f4; uint8_t pe[4]; } gm_t6r4nedrp9ovi;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { gm_tcc1rq4n1avb2 f0; uint8_t f1; uint8_t pe[3]; } gm_t736djc66po0l;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t98k64j4g45rk) == 40 && _Alignof(gm_t98k64j4g45rk) == 4 && offsetof(gm_t98k64j4g45rk, f0) == 0 && offsetof(gm_t98k64j4g45rk, f1) == 4 && offsetof(gm_t98k64j4g45rk, f2) == 8 && offsetof(gm_t98k64j4g45rk, f3) == 12 && offsetof(gm_t98k64j4g45rk, f4) == 16 && offsetof(gm_t98k64j4g45rk, f5) == 20 && offsetof(gm_t98k64j4g45rk, f6) == 24 && offsetof(gm_t98k64j4g45rk, f7) == 28 && offsetof(gm_t98k64j4g45rk, f8) == 32 && offsetof(gm_t98k64j4g45rk, f9) == 36, "layout of `VmLoop`");
typedef struct { gm_tqs5p4n2tq19v f0; uint8_t f1; } gm_t9av822btjq16;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; gm_b16_8 f2; gm_tj2m9dbh1ekv6 f3; void * f4; } gm_t9ka92sj5labh;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; } f1; } gm_t9rfq1ocel0om;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_ta6jn77i9rmqf;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; uint32_t f3; uint8_t pe[4]; } gm_taohonfcrgrgb;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_b16_8 f0; gm_tj2m9dbh1ekv6 f1; void * f2; } gm_tbeu0qv6bbs2s;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; gm_tl874t2atnlh3 f3; } gm_tc9l77ul3aqid;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_td7fbab3thbb2) == 40 && _Alignof(gm_td7fbab3thbb2) == 8 && offsetof(gm_td7fbab3thbb2, f0) == 0 && offsetof(gm_td7fbab3thbb2, f1) == 4 && offsetof(gm_td7fbab3thbb2, f2) == 8 && offsetof(gm_td7fbab3thbb2, f3) == 12 && offsetof(gm_td7fbab3thbb2, f4) == 16 && offsetof(gm_td7fbab3thbb2, f5) == 20 && offsetof(gm_td7fbab3thbb2, f6) == 21 && offsetof(gm_td7fbab3thbb2, f7) == 24 && offsetof(gm_td7fbab3thbb2, f8) == 32, "layout of `(at: u32, p: u32, len: u32, lo: u32, hi: u32, has_len: bool, inclusive: bool, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_te4o1hd8337m3) == 32 && _Alignof(gm_te4o1hd8337m3) == 8 && offsetof(gm_te4o1hd8337m3, f0) == 0 && offsetof(gm_te4o1hd8337m3, f1) == 1 && offsetof(gm_te4o1hd8337m3, f2) == 2 && offsetof(gm_te4o1hd8337m3, f3) == 4 && offsetof(gm_te4o1hd8337m3, f4) == 8 && offsetof(gm_te4o1hd8337m3, f5) == 12 && offsetof(gm_te4o1hd8337m3, f6) == 16 && offsetof(gm_te4o1hd8337m3, f7) == 24, "layout of `(op: u8, signed: bool, wrap: bool, bits: u32, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; uint64_t f3; gm_b16_8 f4; void * f5; gm_b16_8 f6; uint32_t f7; uint8_t p8[4]; uint64_t f8; uint64_t f9; gm_b16_8 f10; gm_b16_8 f11; } gm_tedlrckoa1cir;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_tb31k5l794t7p f0; uint8_t f1; uint8_t pe[7]; } gm_tgmjda3f8g6hg;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tj2m9dbh1ekv6 f2; void * f3; } gm_thaddei6om6bp;
_Static_assert(sizeof(gm_thbsr19fadfjo) == 48 && _Alignof(gm_thbsr19fadfjo) == 8 && offsetof(gm_thbsr19fadfjo, f0) == 0 && offsetof(gm_thbsr19fadfjo, f1) == 4 && offsetof(gm_thbsr19fadfjo, f2) == 8 && offsetof(gm_thbsr19fadfjo, f3) == 16 && offsetof(gm_thbsr19fadfjo, f4) == 24 && offsetof(gm_thbsr19fadfjo, f5) == 28 && offsetof(gm_thbsr19fadfjo, f6) == 32 && offsetof(gm_thbsr19fadfjo, f7) == 40, "layout of `(p: u32, len: u32, i: u32, esz: u64, size: u32, align: u32, v: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint8_t f2; uint8_t p3[3]; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_thji9nptko1bl;
_Static_assert(sizeof(gm_thpdbp3q7rdrc) == 16 && _Alignof(gm_thpdbp3q7rdrc) == 4 && offsetof(gm_thpdbp3q7rdrc, f0) == 0 && offsetof(gm_thpdbp3q7rdrc, f1) == 4 && offsetof(gm_thpdbp3q7rdrc, f2) == 8 && offsetof(gm_thpdbp3q7rdrc, f3) == 12, "layout of `(u32, u32, u32, u32)`");
_Static_assert(sizeof(gm_ti4ei62u4i1d9) == 24 && _Alignof(gm_ti4ei62u4i1d9) == 8 && offsetof(gm_ti4ei62u4i1d9, f0) == 0 && offsetof(gm_ti4ei62u4i1d9, f1) == 1 && offsetof(gm_ti4ei62u4i1d9, f2) == 2 && offsetof(gm_ti4ei62u4i1d9, f3) == 4 && offsetof(gm_ti4ei62u4i1d9, f4) == 8 && offsetof(gm_ti4ei62u4i1d9, f5) == 12 && offsetof(gm_ti4ei62u4i1d9, f6) == 16, "layout of `(op: u8, signed: bool, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; gm_ttovhfee8mf28 f1; uint32_t f2; uint32_t f3; uint32_t f4; uint8_t p5[4]; void * f5; } gm_tiag95i83438g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tjqgc9rpvqnkt) == 24 && _Alignof(gm_tjqgc9rpvqnkt) == 8 && offsetof(gm_tjqgc9rpvqnkt, f0) == 0 && offsetof(gm_tjqgc9rpvqnkt, f1) == 4 && offsetof(gm_tjqgc9rpvqnkt, f2) == 8 && offsetof(gm_tjqgc9rpvqnkt, f3) == 16, "layout of `(at: u32, size: u32, v: u64, to: u32)`");
typedef struct { gm_tkpb4kplftgaa f0; uint8_t f1; uint8_t pe[7]; } gm_tk6jsoi855f62;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3qmqcq7vsk9) == 32 && _Alignof(gm_tn3qmqcq7vsk9) == 8 && offsetof(gm_tn3qmqcq7vsk9, f0) == 0 && offsetof(gm_tn3qmqcq7vsk9, f1) == 4 && offsetof(gm_tn3qmqcq7vsk9, f2) == 8 && offsetof(gm_tn3qmqcq7vsk9, f3) == 12 && offsetof(gm_tn3qmqcq7vsk9, f4) == 16 && offsetof(gm_tn3qmqcq7vsk9, f5) == 20 && offsetof(gm_tn3qmqcq7vsk9, f6) == 24, "layout of `(r: u32, levels: u32, local: u32, rel: u32, size: u32, align: u32, n: ^Node)`");
typedef struct { gm_tmn7n0n9raou5 f0; gm_tmn7n0n9raou5 f1; gm_tmn7n0n9raou5 f2; } gm_tnj2fm376v8gd;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_ttovhfee8mf28 f1; uint32_t f2; uint32_t f3; uint8_t p4[4]; void * f4; } gm_to7n9gnqtgp47;
_Static_assert(sizeof(gm_to8e1ootf3q7f) == 16 && _Alignof(gm_to8e1ootf3q7f) == 8 && offsetof(gm_to8e1ootf3q7f, f0) == 0 && offsetof(gm_to8e1ootf3q7f, f1) == 4 && offsetof(gm_to8e1ootf3q7f, f2) == 8, "layout of `(r: u32, local: u32, n: ^Node)`");
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[7]; void * f3; } gm_tpbd74u9dfumj;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tpkdfv1vgiqij) == 24 && _Alignof(gm_tpkdfv1vgiqij) == 8 && offsetof(gm_tpkdfv1vgiqij, f0) == 0 && offsetof(gm_tpkdfv1vgiqij, f1) == 8 && offsetof(gm_tpkdfv1vgiqij, f2) == 12 && offsetof(gm_tpkdfv1vgiqij, f3) == 16, "layout of `(pat: ^Pat, off: u32, size: u32, fail: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[6]; uint64_t f5; uint64_t f6; } gm_tt49maei14cl6;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tu97pd4fjd5oe) == 32 && _Alignof(gm_tu97pd4fjd5oe) == 8 && offsetof(gm_tu97pd4fjd5oe, f0) == 0 && offsetof(gm_tu97pd4fjd5oe, f1) == 4 && offsetof(gm_tu97pd4fjd5oe, f2) == 8 && offsetof(gm_tu97pd4fjd5oe, f3) == 16 && offsetof(gm_tu97pd4fjd5oe, f4) == 24, "layout of `(p: u32, i: u32, len: u64, esz: u64, n: ^Node)`");
_Static_assert(sizeof(gm_tubi2bvif8j7u) == 32 && _Alignof(gm_tubi2bvif8j7u) == 8 && offsetof(gm_tubi2bvif8j7u, f0) == 0 && offsetof(gm_tubi2bvif8j7u, f1) == 8 && offsetof(gm_tubi2bvif8j7u, f2) == 16 && offsetof(gm_tubi2bvif8j7u, f3) == 24, "layout of `(flag: u64, size: u64, nonzero: bool, payload: u64)`");
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t p2[3]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tufm7evi61pnh;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; uint32_t f5; void * f6; } gm_t06kd2nmcgb3u;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0hcjal40h40i) == 40 && _Alignof(gm_t0hcjal40h40i) == 8 && offsetof(gm_t0hcjal40h40i, f0) == 0 && offsetof(gm_t0hcjal40h40i, f1) == 32, "layout of `?(flag: u64, size: u64, nonzero: bool, payload: u64)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; gm_tvdg94bk7aa22 f2; gm_tvdg94bk7aa22 f3; } gm_t0v9ekh8mobe3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t4nomjfe3hlif) == 64 && _Alignof(gm_t4nomjfe3hlif) == 16 && offsetof(gm_t4nomjfe3hlif, f0) == 0 && offsetof(gm_t4nomjfe3hlif, f1) == 4 && offsetof(gm_t4nomjfe3hlif, f2) == 8 && offsetof(gm_t4nomjfe3hlif, f3) == 16 && offsetof(gm_t4nomjfe3hlif, f4) == 32 && offsetof(gm_t4nomjfe3hlif, f5) == 40 && offsetof(gm_t4nomjfe3hlif, f6) == 48 && offsetof(gm_t4nomjfe3hlif, f7) == 56, "layout of `(variant: u32, tag_ty: Ty, tag_size: u64, tag: u128, tag_w: u64, off: u64, size: u64, payload: ?^Pat)`");
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6r4nedrp9ovi) == 32 && _Alignof(gm_t6r4nedrp9ovi) == 8 && offsetof(gm_t6r4nedrp9ovi, f0) == 0 && offsetof(gm_t6r4nedrp9ovi, f1) == 4 && offsetof(gm_t6r4nedrp9ovi, f2) == 8 && offsetof(gm_t6r4nedrp9ovi, f3) == 16 && offsetof(gm_t6r4nedrp9ovi, f4) == 20, "layout of `(levels: u32, local: u32, rel: u64, packed: bool, d: ?u32)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t736djc66po0l) == 12 && _Alignof(gm_t736djc66po0l) == 4 && offsetof(gm_t736djc66po0l, f0) == 0 && offsetof(gm_t736djc66po0l, f1) == 8, "layout of `?(u32, u32)`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint32_t f3; uint32_t f4; } gm_t78m5u6j4a3pq;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9av822btjq16) == 3 && _Alignof(gm_t9av822btjq16) == 1 && offsetof(gm_t9av822btjq16, f0) == 0 && offsetof(gm_t9av822btjq16, f1) == 2, "layout of `?(op: u8, wrap: bool)`");
_Static_assert(sizeof(gm_t9ka92sj5labh) == 48 && _Alignof(gm_t9ka92sj5labh) == 8 && offsetof(gm_t9ka92sj5labh, f0) == 0 && offsetof(gm_t9ka92sj5labh, f1) == 8 && offsetof(gm_t9ka92sj5labh, f2) == 16 && offsetof(gm_t9ka92sj5labh, f3) == 32 && offsetof(gm_t9ka92sj5labh, f4) == 40, "layout of `(at: u32, callee: ^Node, args: []Arg, dst: Dest, node: ^Node)`");
_Static_assert(sizeof(gm_t9rfq1ocel0om) == 12 && _Alignof(gm_t9rfq1ocel0om) == 4 && offsetof(gm_t9rfq1ocel0om, f0) == 0 && offsetof(gm_t9rfq1ocel0om, f1) == 4, "layout of `Arg`");
_Static_assert(sizeof(gm_ta6jn77i9rmqf) == 32 && _Alignof(gm_ta6jn77i9rmqf) == 8 && offsetof(gm_ta6jn77i9rmqf, f0) == 0 && offsetof(gm_ta6jn77i9rmqf, f1) == 8 && offsetof(gm_ta6jn77i9rmqf, f2) == 16 && offsetof(gm_ta6jn77i9rmqf, f3) == 24, "layout of `(how: WordCmp, l: ^Node, r: ^Node, negated: bool)`");
_Static_assert(sizeof(gm_taohonfcrgrgb) == 32 && _Alignof(gm_taohonfcrgrgb) == 8 && offsetof(gm_taohonfcrgrgb, f0) == 0 && offsetof(gm_taohonfcrgrgb, f1) == 16 && offsetof(gm_taohonfcrgrgb, f2) == 20 && offsetof(gm_taohonfcrgrgb, f3) == 24, "layout of `(decls: []const u32, at: u32, size: u32, label: u32)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbeu0qv6bbs2s) == 32 && _Alignof(gm_tbeu0qv6bbs2s) == 8 && offsetof(gm_tbeu0qv6bbs2s, f0) == 0 && offsetof(gm_tbeu0qv6bbs2s, f1) == 16 && offsetof(gm_tbeu0qv6bbs2s, f2) == 24, "layout of `(args: []Arg, dst: Dest, node: ^Node)`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tc9l77ul3aqid) == 24 && _Alignof(gm_tc9l77ul3aqid) == 4 && offsetof(gm_tc9l77ul3aqid, f0) == 0 && offsetof(gm_tc9l77ul3aqid, f1) == 4 && offsetof(gm_tc9l77ul3aqid, f2) == 8 && offsetof(gm_tc9l77ul3aqid, f3) == 12, "layout of `(local: u32, at: u32, size: u32, span: Span)`");
typedef struct { void * f0; void * f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; uint32_t f5; uint8_t p6[4]; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; uint32_t f8; uint32_t f9; uint32_t f10; uint8_t p11[4]; uint64_t f11; uint64_t f12; uint64_t f13; uint32_t f14; uint32_t f15; uint32_t f16; uint32_t f17; uint32_t f18; uint32_t f19; gm_t4c7ipjj9s3fp f20; gm_trqfvmjtt4sij f21; } gm_tcceqra2l78a4;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
_Static_assert(sizeof(gm_tedlrckoa1cir) == 144 && _Alignof(gm_tedlrckoa1cir) == 8 && offsetof(gm_tedlrckoa1cir, f0) == 0 && offsetof(gm_tedlrckoa1cir, f1) == 16 && offsetof(gm_tedlrckoa1cir, f2) == 32 && offsetof(gm_tedlrckoa1cir, f3) == 40 && offsetof(gm_tedlrckoa1cir, f4) == 48 && offsetof(gm_tedlrckoa1cir, f5) == 64 && offsetof(gm_tedlrckoa1cir, f6) == 72 && offsetof(gm_tedlrckoa1cir, f7) == 88 && offsetof(gm_tedlrckoa1cir, f8) == 96 && offsetof(gm_tedlrckoa1cir, f9) == 104 && offsetof(gm_tedlrckoa1cir, f10) == 112 && offsetof(gm_tedlrckoa1cir, f11) == 128, "layout of `Code`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_tf4r0415i49d1;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[1]; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tfd1kqjeh4les;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tgmjda3f8g6hg) == 24 && _Alignof(gm_tgmjda3f8g6hg) == 8 && offsetof(gm_tgmjda3f8g6hg, f0) == 0 && offsetof(gm_tgmjda3f8g6hg, f1) == 16, "layout of `?(rune: u32, len: usize)`");
_Static_assert(sizeof(gm_thaddei6om6bp) == 40 && _Alignof(gm_thaddei6om6bp) == 8 && offsetof(gm_thaddei6om6bp, f0) == 0 && offsetof(gm_thaddei6om6bp, f1) == 8 && offsetof(gm_thaddei6om6bp, f2) == 24 && offsetof(gm_thaddei6om6bp, f3) == 32, "layout of `(f: u32, args: []Arg, dst: Dest, node: ^Node)`");
_Static_assert(sizeof(gm_thji9nptko1bl) == 40 && _Alignof(gm_thji9nptko1bl) == 8 && offsetof(gm_thji9nptko1bl, f0) == 0 && offsetof(gm_thji9nptko1bl, f1) == 8 && offsetof(gm_thji9nptko1bl, f2) == 16 && offsetof(gm_thji9nptko1bl, f3) == 20 && offsetof(gm_thji9nptko1bl, f4) == 24 && offsetof(gm_thji9nptko1bl, f5) == 28 && offsetof(gm_thji9nptko1bl, f6) == 32, "layout of `(t: IntW, shift: IntW, wrap: bool, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tiag95i83438g) == 40 && _Alignof(gm_tiag95i83438g) == 8 && offsetof(gm_tiag95i83438g, f0) == 0 && offsetof(gm_tiag95i83438g, f1) == 8 && offsetof(gm_tiag95i83438g, f2) == 16 && offsetof(gm_tiag95i83438g, f3) == 20 && offsetof(gm_tiag95i83438g, f4) == 24 && offsetof(gm_tiag95i83438g, f5) == 32, "layout of `(t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6r4nedrp9ovi v0; gm_t2cg11r39sm0e v1; } f1; } gm_tiutk3b96cnlp;
_Static_assert(sizeof(gm_tk6jsoi855f62) == 32 && _Alignof(gm_tk6jsoi855f62) == 8 && offsetof(gm_tk6jsoi855f62, f0) == 0 && offsetof(gm_tk6jsoi855f62, f1) == 24, "layout of `?(flag: u64, size: u64, payload: u64)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tkm39klvnn4cr;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[1]; uint32_t f2; uint32_t f3; uint32_t f4; void * f5; void * f6; void * f7; } gm_tl948htoiqjcu;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnj2fm376v8gd) == 24 && _Alignof(gm_tnj2fm376v8gd) == 4 && offsetof(gm_tnj2fm376v8gd, f0) == 0 && offsetof(gm_tnj2fm376v8gd, f1) == 8 && offsetof(gm_tnj2fm376v8gd, f2) == 16, "layout of `(Coercion, Coercion, Coercion)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tnkql1rjn1vbj;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_to7n9gnqtgp47) == 32 && _Alignof(gm_to7n9gnqtgp47) == 8 && offsetof(gm_to7n9gnqtgp47, f0) == 0 && offsetof(gm_to7n9gnqtgp47, f1) == 4 && offsetof(gm_to7n9gnqtgp47, f2) == 12 && offsetof(gm_to7n9gnqtgp47, f3) == 16 && offsetof(gm_to7n9gnqtgp47, f4) == 24, "layout of `(op: UnOp, t: IntW, r: u32, a: u32, n: ^Node)`");
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_tpbd74u9dfumj) == 24 && _Alignof(gm_tpbd74u9dfumj) == 8 && offsetof(gm_tpbd74u9dfumj, f0) == 0 && offsetof(gm_tpbd74u9dfumj, f1) == 4 && offsetof(gm_tpbd74u9dfumj, f2) == 8 && offsetof(gm_tpbd74u9dfumj, f3) == 16, "layout of `(at: u32, ty: Ty, is_result: bool, n: ^Node)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint32_t f2; uint32_t f3; uint32_t f4; } gm_tqj4rgf34oslv;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tscalq56heofe;
_Static_assert(sizeof(gm_tt49maei14cl6) == 48 && _Alignof(gm_tt49maei14cl6) == 8 && offsetof(gm_tt49maei14cl6, f0) == 0 && offsetof(gm_tt49maei14cl6, f1) == 8 && offsetof(gm_tt49maei14cl6, f2) == 16 && offsetof(gm_tt49maei14cl6, f3) == 24 && offsetof(gm_tt49maei14cl6, f4) == 25 && offsetof(gm_tt49maei14cl6, f5) == 32 && offsetof(gm_tt49maei14cl6, f6) == 40, "layout of `(base: ^Node, lo: ?^Node, hi: ?^Node, inclusive: bool, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_tufm7evi61pnh) == 24 && _Alignof(gm_tufm7evi61pnh) == 4 && offsetof(gm_tufm7evi61pnh, f0) == 0 && offsetof(gm_tufm7evi61pnh, f1) == 8 && offsetof(gm_tufm7evi61pnh, f2) == 12 && offsetof(gm_tufm7evi61pnh, f3) == 16 && offsetof(gm_tufm7evi61pnh, f4) == 20, "layout of `(how: WordCmp, negated: bool, r: u32, a: u32, b: u32)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[5]; void * f2; void * f3; } gm_tuggr5sml9o52;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t06kd2nmcgb3u) == 40 && _Alignof(gm_t06kd2nmcgb3u) == 8 && offsetof(gm_t06kd2nmcgb3u, f0) == 0 && offsetof(gm_t06kd2nmcgb3u, f1) == 4 && offsetof(gm_t06kd2nmcgb3u, f2) == 12 && offsetof(gm_t06kd2nmcgb3u, f3) == 20 && offsetof(gm_t06kd2nmcgb3u, f4) == 24 && offsetof(gm_t06kd2nmcgb3u, f5) == 28 && offsetof(gm_t06kd2nmcgb3u, f6) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, r: u32, a: u32, b: u32, n: ^Node)`");
_Static_assert(sizeof(gm_t0pc2c7dafsjt) == 40 && _Alignof(gm_t0pc2c7dafsjt) == 8 && offsetof(gm_t0pc2c7dafsjt, f0) == 0 && offsetof(gm_t0pc2c7dafsjt, f1) == 4 && offsetof(gm_t0pc2c7dafsjt, f2) == 12 && offsetof(gm_t0pc2c7dafsjt, f3) == 24 && offsetof(gm_t0pc2c7dafsjt, f4) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t0v9ekh8mobe3) == 48 && _Alignof(gm_t0v9ekh8mobe3) == 4 && offsetof(gm_t0v9ekh8mobe3, f0) == 0 && offsetof(gm_t0v9ekh8mobe3, f1) == 12 && offsetof(gm_t0v9ekh8mobe3, f2) == 24 && offsetof(gm_t0v9ekh8mobe3, f3) == 36, "layout of `(Builtin, Builtin, Builtin, Builtin)`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t78m5u6j4a3pq) == 36 && _Alignof(gm_t78m5u6j4a3pq) == 4 && offsetof(gm_t78m5u6j4a3pq, f0) == 0 && offsetof(gm_t78m5u6j4a3pq, f1) == 12 && offsetof(gm_t78m5u6j4a3pq, f2) == 20 && offsetof(gm_t78m5u6j4a3pq, f3) == 28 && offsetof(gm_t78m5u6j4a3pq, f4) == 32, "layout of `(op: Builtin, from: IntW, to: IntW, r: u32, a: u32)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcceqra2l78a4) == 384 && _Alignof(gm_tcceqra2l78a4) == 8 && offsetof(gm_tcceqra2l78a4, f0) == 0 && offsetof(gm_tcceqra2l78a4, f1) == 8 && offsetof(gm_tcceqra2l78a4, f2) == 16 && offsetof(gm_tcceqra2l78a4, f3) == 56 && offsetof(gm_tcceqra2l78a4, f4) == 96 && offsetof(gm_tcceqra2l78a4, f5) == 136 && offsetof(gm_tcceqra2l78a4, f6) == 144 && offsetof(gm_tcceqra2l78a4, f7) == 184 && offsetof(gm_tcceqra2l78a4, f8) == 224 && offsetof(gm_tcceqra2l78a4, f9) == 228 && offsetof(gm_tcceqra2l78a4, f10) == 232 && offsetof(gm_tcceqra2l78a4, f11) == 240 && offsetof(gm_tcceqra2l78a4, f12) == 248 && offsetof(gm_tcceqra2l78a4, f13) == 256 && offsetof(gm_tcceqra2l78a4, f14) == 264 && offsetof(gm_tcceqra2l78a4, f15) == 268 && offsetof(gm_tcceqra2l78a4, f16) == 272 && offsetof(gm_tcceqra2l78a4, f17) == 276 && offsetof(gm_tcceqra2l78a4, f18) == 280 && offsetof(gm_tcceqra2l78a4, f19) == 284 && offsetof(gm_tcceqra2l78a4, f20) == 288 && offsetof(gm_tcceqra2l78a4, f21) == 344, "layout of `Vc`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tf4r0415i49d1) == 48 && _Alignof(gm_tf4r0415i49d1) == 8 && offsetof(gm_tf4r0415i49d1, f0) == 0 && offsetof(gm_tf4r0415i49d1, f1) == 24 && offsetof(gm_tf4r0415i49d1, f2) == 32, "layout of `(effect_: EffectRef, op: u32, args: []^Node)`");
_Static_assert(sizeof(gm_tfd1kqjeh4les) == 16 && _Alignof(gm_tfd1kqjeh4les) == 4 && offsetof(gm_tfd1kqjeh4les, f0) == 0 && offsetof(gm_tfd1kqjeh4les, f1) == 2 && offsetof(gm_tfd1kqjeh4les, f2) == 4 && offsetof(gm_tfd1kqjeh4les, f3) == 8 && offsetof(gm_tfd1kqjeh4les, f4) == 12, "layout of `(op: BinOp, wide: bool, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tiutk3b96cnlp) == 40 && _Alignof(gm_tiutk3b96cnlp) == 8 && offsetof(gm_tiutk3b96cnlp, f0) == 0 && offsetof(gm_tiutk3b96cnlp, f1) == 8, "layout of `PlaceC`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb31k5l794t7p v0; gm_thpdbp3q7rdrc v1; gm_t0m68qtp7uj1u v2; gm_tl874t2atnlh3 v3; gm_tl874t2atnlh3 v4; gm_tn3qmqcq7vsk9 v5; gm_thpdbp3q7rdrc v6; gm_thpdbp3q7rdrc v7; gm_tg0sdqqpruvqq v8; gm_tcc1rq4n1avb2 v9; gm_t51d05oujnurh v10; gm_tiag95i83438g v11; gm_thji9nptko1bl v12; gm_tn3qmqcq7vsk9 v13; gm_t0m68qtp7uj1u v14; gm_tl948htoiqjcu v15; gm_t0mrruemedngl v16; gm_t04llc5uen0qo v17; gm_t04llc5uen0qo v18; gm_t3a21dm0j52qn v19; gm_tu97pd4fjd5oe v20; gm_t1lvkimu9u0hv v21; gm_thbsr19fadfjo v22; gm_t4h1llua034m2 v23; gm_tb31k5l794t7p v24; gm_t06kd2nmcgb3u v25; gm_ti4ei62u4i1d9 v26; gm_te4o1hd8337m3 v27; gm_tnkql1rjn1vbj v28; gm_tufm7evi61pnh v29; gm_tnkql1rjn1vbj v30; gm_tufm7evi61pnh v31; gm_tcc1rq4n1avb2 v32; uint32_t v33; gm_to7n9gnqtgp47 v34; gm_t78m5u6j4a3pq v35; gm_tqj4rgf34oslv v36; gm_tfd1kqjeh4les v37; uint32_t v38; gm_tcc1rq4n1avb2 v39; gm_tcc1rq4n1avb2 v40; gm_t04llc5uen0qo v41; gm_t04llc5uen0qo v42; gm_t5r2drn9pjqkt v43; gm_tpbd74u9dfumj v44; gm_tbeu0qv6bbs2s v45; gm_ttggq7ng4kp3p v46; gm_thaddei6om6bp v47; gm_t9ka92sj5labh v48; gm_t3hcdie243ee3 v49; gm_tbeu0qv6bbs2s v50; gm_tc9l77ul3aqid v51; gm_tl874t2atnlh3 v52; gm_tl874t2atnlh3 v53; gm_tl874t2atnlh3 v54; gm_t37c2jujtiadn v55; gm_tiilmfd16v3fc v56; gm_tcc1rq4n1avb2 v57; gm_td7fbab3thbb2 v58; gm_to8e1ootf3q7f v59; gm_tjqgc9rpvqnkt v60; gm_ttggq7ng4kp3p v61; gm_ttggq7ng4kp3p v62; void * v63; gm_t2k9dbsv5ff58 v64; gm_tcc1rq4n1avb2 v65; uint32_t v66; gm_tgtub0v94d5u5 v67; uint32_t v68; gm_t3mf58gv9kf3n v69; gm_tpkdfv1vgiqij v70; void * v71; uint32_t v72; void * v73; gm_tcc1rq4n1avb2 v74; uint32_t v75; uint32_t v76; uint32_t v77; uint32_t v78; uint32_t v79; gm_tl874t2atnlh3 v80; gm_taohonfcrgrgb v81; gm_tcc1rq4n1avb2 v82; } f1; } gm_tjc2ga47o3k6g;
_Static_assert(sizeof(gm_tkm39klvnn4cr) == 32 && _Alignof(gm_tkm39klvnn4cr) == 8 && offsetof(gm_tkm39klvnn4cr, f0) == 0 && offsetof(gm_tkm39klvnn4cr, f1) == 4 && offsetof(gm_tkm39klvnn4cr, f2) == 16 && offsetof(gm_tkm39klvnn4cr, f3) == 24, "layout of `(op: BinOp, how: WordCmp, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_tl948htoiqjcu) == 40 && _Alignof(gm_tl948htoiqjcu) == 8 && offsetof(gm_tl948htoiqjcu, f0) == 0 && offsetof(gm_tl948htoiqjcu, f1) == 2 && offsetof(gm_tl948htoiqjcu, f2) == 4 && offsetof(gm_tl948htoiqjcu, f3) == 8 && offsetof(gm_tl948htoiqjcu, f4) == 12 && offsetof(gm_tl948htoiqjcu, f5) == 16 && offsetof(gm_tl948htoiqjcu, f6) == 24 && offsetof(gm_tl948htoiqjcu, f7) == 32, "layout of `(op: BinOp, wide: u8, at: u32, la: u32, lb: u32, n: ^Node, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_tnkql1rjn1vbj) == 24 && _Alignof(gm_tnkql1rjn1vbj) == 4 && offsetof(gm_tnkql1rjn1vbj, f0) == 0 && offsetof(gm_tnkql1rjn1vbj, f1) == 4 && offsetof(gm_tnkql1rjn1vbj, f2) == 12 && offsetof(gm_tnkql1rjn1vbj, f3) == 16 && offsetof(gm_tnkql1rjn1vbj, f4) == 20, "layout of `(op: BinOp, how: WordCmp, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tqj4rgf34oslv) == 32 && _Alignof(gm_tqj4rgf34oslv) == 4 && offsetof(gm_tqj4rgf34oslv, f0) == 0 && offsetof(gm_tqj4rgf34oslv, f1) == 12 && offsetof(gm_tqj4rgf34oslv, f2) == 20 && offsetof(gm_tqj4rgf34oslv, f3) == 24 && offsetof(gm_tqj4rgf34oslv, f4) == 28, "layout of `(op: Builtin, t: IntW, r: u32, a: u32, b: u32)`");
_Static_assert(sizeof(gm_tscalq56heofe) == 40 && _Alignof(gm_tscalq56heofe) == 8 && offsetof(gm_tscalq56heofe, f0) == 0 && offsetof(gm_tscalq56heofe, f1) == 12 && offsetof(gm_tscalq56heofe, f2) == 24 && offsetof(gm_tscalq56heofe, f3) == 32, "layout of `(op: Builtin, t: IntW, x: ^Node, k: ^Node)`");
_Static_assert(sizeof(gm_tuggr5sml9o52) == 24 && _Alignof(gm_tuggr5sml9o52) == 8 && offsetof(gm_tuggr5sml9o52, f0) == 0 && offsetof(gm_tuggr5sml9o52, f1) == 2 && offsetof(gm_tuggr5sml9o52, f2) == 8 && offsetof(gm_tuggr5sml9o52, f3) == 16, "layout of `(op: BinOp, wide: bool, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tjc2ga47o3k6g) == 56 && _Alignof(gm_tjc2ga47o3k6g) == 8 && offsetof(gm_tjc2ga47o3k6g, f0) == 0 && offsetof(gm_tjc2ga47o3k6g, f1) == 8, "layout of `Ins`");
typedef struct { gm_tjc2ga47o3k6g f0; uint8_t f1; uint8_t pe[7]; } gm_tk8sioamep3gv;
_Static_assert(sizeof(gm_tk8sioamep3gv) == 64 && _Alignof(gm_tk8sioamep3gv) == 8 && offsetof(gm_tk8sioamep3gv, f0) == 0 && offsetof(gm_tk8sioamep3gv, f1) == 56, "layout of `?Ins`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_arith_kind_muj23pr {
    gm_frame h;
    gm_t9av822btjq16 ret;
};
struct gm_fr_arith_op_90c7mvs {
    gm_frame h;
    gm_ts6oqppjh0ba3 ret;
};
struct gm_fr_clear_4s443ju {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_cmp_opposite_tarvh4i {
    gm_frame h;
    gm_ts6oqppjh0ba3 ret;
};
struct gm_fr_empty_slice_6sqhoug {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8754fft {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8nfeia6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dg6bggq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j41rrvu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_m2f8gtr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_7dqhv02 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_word_82qsidt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_6oos54v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bf8dj7f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_m6i5ef0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pcuroj5 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_root_levels_umkq7vp {
    gm_frame h;
    gm_tp0fa5qv7rqnn l3; /* x */
    uint32_t ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vc_addressable_ltobf02 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_align_na1h2on {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_vc_all_word_only_4ufgbu6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_alloc_3hmfvtt {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_vc_alloc_slot_5eptdbb {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vc_args_j2k3nul {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_vc_assert_ok_6n0rc7j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_block_ql7sru3 {
    gm_frame h;
    gm_t2k9dbsv5ff58 l5; /* st */
    gm_b0_1 ret;
};
struct gm_fr_vc_branch_r6k4hjp {
    gm_frame h;
    gm_trqfvmjtt4sij l16; /* skip */
    gm_b0_1 ret;
};
struct gm_fr_vc_builds_d9pue9e {
    gm_frame h;
    gm_tt49maei14cl6 l6; /* x */
    gm_t710lsrq1m8gk l7; /* w */
    uint8_t ret;
};
struct gm_fr_vc_builtin_9d4kon2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_builtin_ok_kbscem0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_call_27qg9ft {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_call_to_local_qd1fddh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_call_value_5j9edsq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_coerce_builds_j86oug9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_compact_8tukqc5 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* at */
    gm_trqfvmjtt4sij l7; /* out */
    gm_tjc2ga47o3k6g l11; /* x */
    gm_b0_1 ret;
};
struct gm_fr_vc_compiles_au5pqeh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_conds_a1m6u4v {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* fails */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_vc_copies_glmdmll {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_copy_place_8dhmkji {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_defer_block_bgamoqf {
    gm_frame h;
    gm_t2k9dbsv5ff58 l8; /* st */
    gm_b0_1 ret;
};
struct gm_fr_vc_else_9ml9jkf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_else_shape_n9vc3pg {
    gm_frame h;
    gm_t0hcjal40h40i ret;
};
struct gm_fr_vc_emit_phs805s {
    gm_frame h;
    gm_tjc2ga47o3k6g l1; /* i */
    gm_b0_1 ret;
};
struct gm_fr_vc_expr_697jn9n {
    gm_frame h;
    gm_t710lsrq1m8gk l7; /* x */
    gm_tf4r0415i49d1 l25; /* x */
    gm_t710lsrq1m8gk l26; /* w */
    gm_b0_1 ret;
};
struct gm_fr_vc_field_root_lb3os0t {
    gm_frame h;
    gm_tgmjda3f8g6hg ret;
};
struct gm_fr_vc_find_loop_gljp56a {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_vc_has_index_b75va8s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_here_cc4mbj3 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_vc_if_1vdf5lq {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* fails */
    gm_b0_1 ret;
};
struct gm_fr_vc_jump_f1p9hn7 {
    gm_frame h;
    gm_tjc2ga47o3k6g l1; /* i */
    uint32_t ret;
};
struct gm_fr_vc_leave_7oqk8g5 {
    gm_frame h;
    gm_t98k64j4g45rk l2; /* lp */
    gm_b0_1 ret;
};
struct gm_fr_vc_load_v0jk07v {
    gm_frame h;
    gm_tiutk3b96cnlp l2; /* pl */
    gm_b0_1 ret;
};
struct gm_fr_vc_local_reg_bgdr42m {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_vc_loop_upfkot7 {
    gm_frame h;
    gm_trqfvmjtt4sij l18; /* fails */
    gm_b0_1 ret;
};
struct gm_fr_vc_match_ikc07jv {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* ends */
    gm_trqfvmjtt4sij l9; /* fails */
    gm_b0_1 ret;
};
struct gm_fr_vc_operand_nmt6qjg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_vc_opt_shape_of1700t {
    gm_frame h;
    gm_tk6jsoi855f62 ret;
};
struct gm_fr_vc_pat_d3t2bfu {
    gm_frame h;
    gm_t4nomjfe3hlif l16; /* x */
    gm_b0_1 ret;
};
struct gm_fr_vc_pat_ok_be3j97k {
    gm_frame h;
    gm_t4nomjfe3hlif l7; /* x */
    uint8_t ret;
};
struct gm_fr_vc_patch_pbmmb3q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_payload_loquhre {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_perform_qci0ith {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_place_i1sksai {
    gm_frame h;
    gm_tp0fa5qv7rqnn l17; /* x */
    gm_tiutk3b96cnlp l18; /* base */
    gm_tiutk3b96cnlp ret;
};
struct gm_fr_vc_place_reg_e0pc35g {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_vc_place_word_only_b4oj6ia {
    gm_frame h;
    gm_tp0fa5qv7rqnn l4; /* x */
    uint8_t ret;
};
struct gm_fr_vc_placeable_ip26ve2 {
    gm_frame h;
    gm_tp0fa5qv7rqnn l3; /* x */
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_vc_plain_bs0ofeb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_put_byte_ner3v2q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_reads_only_vncte32 {
    gm_frame h;
    gm_t0pc2c7dafsjt l2; /* x */
    gm_tscalq56heofe l11; /* x */
    uint8_t ret;
};
struct gm_fr_vc_retarget_cjve41g {
    gm_frame h;
    gm_t06kd2nmcgb3u l11; /* x */
    gm_tiag95i83438g l16; /* x */
    gm_thji9nptko1bl l17; /* x */
    gm_t78m5u6j4a3pq l27; /* x */
    gm_t1lvkimu9u0hv l34; /* x */
    gm_thaddei6om6bp l40; /* x */
    gm_t9ka92sj5labh l42; /* x */
    gm_tk8sioamep3gv l46; /* nw */
    gm_tjc2ga47o3k6g l47; /* ins */
    uint8_t ret;
};
struct gm_fr_vc_set_local_8ov1tn5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_set_local_slot_l28kec5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_set_top_h0qmvd1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_set_word_local_p05lnpr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_slice_builds_2cdjc7j {
    gm_frame h;
    gm_tt49maei14cl6 l1; /* x */
    uint8_t ret;
};
struct gm_fr_vc_slice_get_klv78kv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_slice_ops_35to7ql {
    gm_frame h;
    gm_tiutk3b96cnlp l4; /* pl */
    gm_tcc1rq4n1avb2 ret;
};
struct gm_fr_vc_slice_qr9et9a {
    gm_frame h;
    gm_tt49maei14cl6 l1; /* x */
    gm_tiutk3b96cnlp l10; /* pl */
    gm_b0_1 ret;
};
struct gm_fr_vc_slice_regs_en7lrqs {
    gm_frame h;
    gm_t736djc66po0l ret;
};
struct gm_fr_vc_slot_at_rc6jhbj {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_vc_stmt_01mcb8k {
    gm_frame h;
    gm_t2k9dbsv5ff58 l1; /* st */
    gm_tp0fa5qv7rqnn l20; /* x */
    gm_tiutk3b96cnlp l24; /* pl */
    gm_tiutk3b96cnlp l36; /* pl */
    gm_b0_1 ret;
};
struct gm_fr_vc_to_slot_70oqqr5 {
    gm_frame h;
    gm_tf4r0415i49d1 l8; /* x */
    gm_tt49maei14cl6 l51; /* x */
    gm_t710lsrq1m8gk l52; /* w */
    gm_b0_1 ret;
};
struct gm_fr_vc_try_ok_ng7fba1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_try_tbirnd2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_unwrap_ffrmkv5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_unwrap_ok_tb3qnv4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_uses_scratch_9rt07d8 {
    gm_frame h;
    gm_t2k9dbsv5ff58 l1; /* st */
    uint8_t ret;
};
struct gm_fr_vc_variant_builds_hjfpsfe {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_vc_with_h9bq2rc {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_b0_1 ret;
};
struct gm_fr_vc_word_11r57ch {
    gm_frame h;
    gm_t0pc2c7dafsjt l7; /* x */
    gm_tscalq56heofe l34; /* x */
    gm_tf4r0415i49d1 l48; /* x */
    gm_t710lsrq1m8gk l56; /* w */
    gm_tp0fa5qv7rqnn l60; /* x */
    gm_tiutk3b96cnlp l61; /* pl */
    gm_tiutk3b96cnlp l62; /* pl */
    gm_tiutk3b96cnlp l65; /* pl */
    gm_b0_1 ret;
};
struct gm_fr_vc_word_only_rg8dfe5 {
    gm_frame h;
    gm_t0pc2c7dafsjt l2; /* x */
    gm_tscalq56heofe l11; /* x */
    gm_tf4r0415i49d1 l14; /* x */
    gm_tp0fa5qv7rqnn l15; /* x */
    uint8_t ret;
};
struct gm_fr_vc_word_to_fgjor3m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vc_word_via_slot_snr97k7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_vm_compile_gef97rj {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* consts */
    gm_t4c7ipjj9s3fp l7; /* const_at */
    gm_tcceqra2l78a4 l13; /* c */
    void * ret;
};
struct gm_fr_want_dest_rhqgfel {
    gm_frame h;
    gm_tj2m9dbh1ekv6 ret;
};
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5mn8ujkg6u(const uint8_t *a, const uint8_t *b);
inline bool gm_eq651u8j42jd(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqcmo75g2ssd(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9av822btjq16 gm_f_arith_kind_muj23pr(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_ts6oqppjh0ba3 gm_f_arith_op_90c7mvs(uint8_t a0, uint8_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_clear_4s443ju(void * a0);
__attribute__((aligned(4))) gm_ts6oqppjh0ba3 gm_f_cmp_opposite_tarvh4i(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_deinit_bgn5i9p(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_s7049sc(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_sk5botm(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_tbneotd(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_v2ik7pl(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6sqhoug(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8754fft(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nfeia6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dg6bggq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j41rrvu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m2f8gtr(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ll5819n(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_bjkgton(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_7dqhv02(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_w_a8oe7le(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_82qsidt(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bf8dj7f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_m6i5ef0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pcuroj5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_uiusob6(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7g6eqp6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d9f883g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_jis5lhu_2(void * a0, gm_t9rfq1ocel0om a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nc46kuk(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_6j13ahd(void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_root_levels_umkq7vp(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_vc_addressable_ltobf02(void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_vc_align_na1h2on(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_all_word_only_4ufgbu6(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_vc_alloc_3hmfvtt(void * a0);
__attribute__((aligned(4))) uint64_t gm_f_vc_alloc_slot_5eptdbb(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_vc_args_j2k3nul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_vc_assert_ok_6n0rc7j(gm_tvdg94bk7aa22 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_block_ql7sru3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_branch_r6k4hjp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_vc_builds_d9pue9e(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_builtin_9d4kon2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tj2m9dbh1ekv6 a2, void * a3);
__attribute__((aligned(4))) uint8_t gm_f_vc_builtin_ok_kbscem0(gm_tvdg94bk7aa22 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_27qg9ft(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tj2m9dbh1ekv6 a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_to_local_qd1fddh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_value_5j9edsq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tj2m9dbh1ekv6 a3, void * a4);
__attribute__((aligned(4))) uint8_t gm_f_vc_coerce_builds_j86oug9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_compact_8tukqc5(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_vc_compiles_au5pqeh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_vc_conds_a1m6u4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_copies_glmdmll(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_copy_place_8dhmkji(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_defer_block_bgamoqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_else_9ml9jkf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, void * a4, gm_t9805qtonbsag a5);
__attribute__((aligned(4))) void gm_f_vc_else_shape_n9vc3pg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_emit_phs805s(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_expr_697jn9n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2);
__attribute__((aligned(4))) void gm_f_vc_field_root_lb3os0t(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tgmjda3f8g6hg gm_f_vc_field_root_lb3os0t_c(void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_find_loop_gljp56a(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_has_index_b75va8s(void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_vc_here_cc4mbj3(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_if_1vdf5lq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3, gm_tmn7n0n9raou5 a4);
inline __attribute__((aligned(4))) uint32_t gm_f_vc_jump_f1p9hn7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_leave_7oqk8g5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_load_v0jk07v(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, uint64_t a3, uint64_t a4, uint32_t a5, void * a6);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_local_reg_bgdr42m(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_loop_upfkot7(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_match_ikc07jv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_tmn7n0n9raou5 a4);
__attribute__((aligned(4))) uint32_t gm_f_vc_operand_nmt6qjg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_vc_opt_shape_of1700t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_pat_d3t2bfu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) uint8_t gm_f_vc_pat_ok_be3j97k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_vc_patch_pbmmb3q(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_payload_loquhre(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_t9805qtonbsag a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_perform_qci0ith(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tj2m9dbh1ekv6 a2, void * a3);
__attribute__((aligned(4))) void gm_f_vc_place_i1sksai(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_place_reg_e0pc35g(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_place_word_only_b4oj6ia(void * a0, void * a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_vc_placeable_ip26ve2(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_vc_plain_bs0ofeb(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_put_byte_ner3v2q(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_vc_reads_only_vncte32(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_retarget_cjve41g(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_local_8ov1tn5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_local_slot_l28kec5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_vc_set_top_h0qmvd1(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_word_local_p05lnpr(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_vc_slice_builds_2cdjc7j(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_slice_builds_2cdjc7j_c(void * a0, gm_tt49maei14cl6 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_slice_get_klv78kv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint64_t a4);
__attribute__((aligned(4))) gm_tcc1rq4n1avb2 gm_f_vc_slice_ops_35to7ql(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_slice_qr9et9a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_t736djc66po0l gm_f_vc_slice_regs_en7lrqs(void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_vc_slot_at_rc6jhbj(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_stmt_01mcb8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_to_slot_70oqqr5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) uint8_t gm_f_vc_try_ok_ng7fba1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_try_tbirnd2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, gm_t9805qtonbsag a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_unwrap_ffrmkv5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_t9805qtonbsag a4);
inline __attribute__((aligned(4))) uint8_t gm_f_vc_unwrap_ok_tb3qnv4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_vc_uses_scratch_9rt07d8(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_uses_scratch_9rt07d8_c(void * a0, gm_t2k9dbsv5ff58 a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_variant_builds_hjfpsfe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_with_h9bq2rc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tmn7n0n9raou5 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_11r57ch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_vc_word_only_rg8dfe5(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_to_fgjor3m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_via_slot_snr97k7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_vm_compile_gef97rj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tj2m9dbh1ekv6 gm_f_want_dest_rhqgfel(gm_tmn7n0n9raou5 a0);
extern uint8_t gm_c7p3n1736t1[4] __attribute__((aligned(4)));
extern uint8_t gm_c956v7aird0[8] __attribute__((aligned(8)));
extern uint8_t gm_chkfraubo1r[4] __attribute__((aligned(4)));
extern uint8_t gm_cl7gtp51edk[4] __attribute__((aligned(4)));
extern uint8_t gm_kkuae0e6qb8[40] __attribute__((aligned(8)));
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcutmnmd444bu0[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5mn8ujkg6u(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq651u8j42jd(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq5mn8ujkg6u(a, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqcmo75g2ssd(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)4) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)68) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)72) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)79) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) void gm_f_clear_4s443ju(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4s443ju F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6sqhoug(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6sqhoug F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8754fft(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8754fft F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nfeia6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8nfeia6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dg6bggq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dg6bggq F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 128)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j41rrvu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j41rrvu F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m2f8gtr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_m2f8gtr F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_7dqhv02(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_7dqhv02 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_6sqhoug();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_word_82qsidt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_word_82qsidt F;
    void * gm_l0; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 104));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fa4sjvq_2();
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
inline __attribute__((aligned(4))) void gm_f_list_bf8dj7f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bf8dj7f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_m2f8gtr();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_84b0b46();
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
inline __attribute__((aligned(4))) void gm_f_list_m6i5ef0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_m6i5ef0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_j41rrvu();
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
inline __attribute__((aligned(4))) void gm_f_list_pcuroj5(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pcuroj5 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8754fft();
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l2 = t8;
        {
            gm_t88rcucg1b7uk t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t13 = gm_l1;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
        F.ret = t5;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_vc_align_na1h2on(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_align_na1h2on F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 96));
    uint64_t t5 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t6 = (uint32_t)t5;
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
inline __attribute__((aligned(4))) uint32_t gm_f_vc_alloc_3hmfvtt(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_alloc_3hmfvtt F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 224));
        uint32_t r7; bool o8 = __builtin_add_overflow(t6, ((uint32_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 186, 5);
        uint32_t t9 = r7;
        GM_ST(uint32_t, (((uint8_t *)t4) + 224), t9);
    }
    {
        void * t11 = gm_l0;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t11) + 224));
        void * t13 = gm_l0;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 228));
        uint8_t t15 = (t12 > t14);
        if (!t15) goto ie10;
        {
            void * t17 = gm_l0;
            void * t18 = gm_l0;
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 224));
            GM_ST(uint32_t, (((uint8_t *)t17) + 228), t19);
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint32_t t20 = gm_l1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_assert_ok_6n0rc7j(gm_tvdg94bk7aa22 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_assert_ok_6n0rc7j F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_b16_8 gm_l1; /* args */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)61ULL)));
    if (t3) {
        uint8_t t4 = 0;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
        if (!t6) {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)2ULL)));
            t4 = t8;
        } else t4 = 1;
        t2 = t4;
    } else t2 = 0;
    if (t2) {
        uint64_t t9 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcutmnmd444bu0, 510, 126);
        void * t11 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t9 * 8));
        uint8_t t12 = gm_f_is_word_82qsidt(t11);
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_vc_here_cc4mbj3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_here_cc4mbj3 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 16) + 0) + 8);
    uint32_t t3 = (uint32_t)t2;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_vc_jump_f1p9hn7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_vc_jump_f1p9hn7 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 16);
        gm_tftb1odc739t5 t4 = gm_f_push_7g6eqp6(t3, &(*(gm_tjc2ga47o3k6g *)((uint8_t *)&F.l1)));
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcutmnmd444bu0, 155, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 16) + 0) + 8);
        uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 156, 13);
        uint64_t t11 = r9;
        uint32_t t12 = (uint32_t)t11;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_plain_bs0ofeb(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_plain_bs0ofeb F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l1;
    uint8_t t3;
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
    if (t6 != ((uint8_t)0ULL)) goto mn5;
    t3 = ((uint8_t)1);
    goto me4;
mn5:;
    t3 = ((uint8_t)0);
    goto me4;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 266, 57);
me4:;
    if (!t3) {
        void * t8 = gm_l0;
        void * t9 = gm_l1;
        gm_t9805qtonbsag t10 = gm_f_vc_place_reg_e0pc35g(t8, t9);
        gm_t9805qtonbsag t11 = t10;
        uint8_t t12;
        if (((((uint8_t *)&t11))[4] != 0)) goto mn14;
        t12 = ((uint8_t)0);
        goto me13;
mn14:;
        t12 = ((uint8_t)1);
        goto me13;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 266, 77);
me13:;
        t1 = t12;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_vc_set_top_h0qmvd1(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_set_top_h0qmvd1 F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* top */
    GM_STACK_CHECK_SHALLOW(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        GM_ST(uint32_t, (((uint8_t *)t2) + 224), t3);
    }
    {
        void * t5 = gm_l0;
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 224));
        void * t7 = gm_l0;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 228));
        uint8_t t9 = (t6 > t8);
        if (!t9) goto ie4;
        {
            void * t11 = gm_l0;
            void * t12 = gm_l0;
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 224));
            GM_ST(uint32_t, (((uint8_t *)t11) + 228), t13);
        }
        goto ix4;
ie4:;
ix4:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_vc_slot_at_rc6jhbj(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_slot_at_rc6jhbj F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 240));
    uint64_t t3 = gm_l1;
    uint64_t r4; bool o5 = __builtin_add_overflow(t2, t3, &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 199, 53);
    uint64_t t6 = r4;
    uint32_t t7 = (uint32_t)t6;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_unwrap_ok_tb3qnv4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vc_unwrap_ok_tb3qnv4 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* x */
    GM_STACK_CHECK(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    uint8_t t4 = gm_f_fast_23em7sv(t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        void * t8 = gm_l2;
        gm_t0hcjal40h40i t9;
        gm_f_vc_else_shape_n9vc3pg((void *)&t9, (void *)NULL, cx, NULL, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0hcjal40h40i t10 = t9;
        uint8_t t11;
        if (((((uint8_t *)&t10))[32] != 0)) goto mn13;
        t11 = ((uint8_t)0);
        goto me12;
mn13:;
        t11 = ((uint8_t)1);
        goto me12;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 623, 102);
me12:;
        t1 = t11;
    } else t1 = 0;
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
__attribute__((aligned(4))) gm_t9av822btjq16 gm_f_arith_kind_muj23pr(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_kind_muj23pr F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* fl */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_t9qkh0rsooc8d gm_l3; /* fl */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9av822btjq16 t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    gm_t9qkh0rsooc8d t11 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t11;
    gm_t9av822btjq16 t12;
    gm_t9qkh0rsooc8d t14 = gm_l1;
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
    gm_t0am46iohlpol t17;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 0), t15);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 1), t16);
    gm_t0am46iohlpol t18 = t17;
    uint8_t t19 = 0;
    gm_t9qkh0rsooc8d t20 = t14;
    if (gm_eqehhdtpeti1(((uint8_t *)&t18) + 0, ((uint8_t *)&t20))) t19 = 1;
    if (gm_eqehhdtpeti1(((uint8_t *)&t18) + 1, ((uint8_t *)&t20))) t19 = 1;
    uint8_t t21 = (uint8_t)(t19);
    if (!t21) goto ie13;
    uint8_t t23 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
    gm_tqs5p4n2tq19v t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
    GM_ST(uint8_t, ((uint8_t *)&t24 + 1), t23);
    gm_tqs5p4n2tq19v t25 = t24;
    gm_t9av822btjq16 t26;
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t26), t25);
    ((uint8_t *)&t26)[2] = 1;
    t12 = t26;
    goto ix13;
ie13:;
    gm_t9av822btjq16 t28;
    memset(&t28, 0, sizeof t28);
    gm_t9av822btjq16 t29 = t28;
    t12 = t29;
ix13:;
    t1 = t12;
    goto me2;
mn9:;
ma4:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)1ULL)) goto mn30;
    gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l2 = t32;
    gm_t9av822btjq16 t33;
    gm_t9qkh0rsooc8d t35 = gm_l2;
    gm_t9qkh0rsooc8d t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t37;
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
    gm_t0am46iohlpol t38;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 0), t36);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 1), t37);
    gm_t0am46iohlpol t39 = t38;
    uint8_t t40 = 0;
    gm_t9qkh0rsooc8d t41 = t35;
    if (gm_eqehhdtpeti1(((uint8_t *)&t39) + 0, ((uint8_t *)&t41))) t40 = 1;
    if (gm_eqehhdtpeti1(((uint8_t *)&t39) + 1, ((uint8_t *)&t41))) t40 = 1;
    uint8_t t42 = (uint8_t)(t40);
    if (!t42) goto ie34;
    uint8_t t44 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)1ULL)));
    gm_tqs5p4n2tq19v t45;
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)1ULL));
    GM_ST(uint8_t, ((uint8_t *)&t45 + 1), t44);
    gm_tqs5p4n2tq19v t46 = t45;
    gm_t9av822btjq16 t47;
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t47), t46);
    ((uint8_t *)&t47)[2] = 1;
    t33 = t47;
    goto ix34;
ie34:;
    gm_t9av822btjq16 t49;
    memset(&t49, 0, sizeof t49);
    gm_t9av822btjq16 t50 = t49;
    t33 = t50;
ix34:;
    t1 = t33;
    goto me2;
mn30:;
ma5:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t52 != ((uint8_t)2ULL)) goto mn51;
    gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
    gm_l3 = t53;
    gm_t9av822btjq16 t54;
    gm_t9qkh0rsooc8d t56 = gm_l3;
    gm_t9qkh0rsooc8d t57;
    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t58;
    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)1ULL));
    gm_t0am46iohlpol t59;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 0), t57);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t59 + 1), t58);
    gm_t0am46iohlpol t60 = t59;
    uint8_t t61 = 0;
    gm_t9qkh0rsooc8d t62 = t56;
    if (gm_eqehhdtpeti1(((uint8_t *)&t60) + 0, ((uint8_t *)&t62))) t61 = 1;
    if (gm_eqehhdtpeti1(((uint8_t *)&t60) + 1, ((uint8_t *)&t62))) t61 = 1;
    uint8_t t63 = (uint8_t)(t61);
    if (!t63) goto ie55;
    uint8_t t65 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)1ULL)));
    gm_tqs5p4n2tq19v t66;
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)2ULL));
    GM_ST(uint8_t, ((uint8_t *)&t66 + 1), t65);
    gm_tqs5p4n2tq19v t67 = t66;
    gm_t9av822btjq16 t68;
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t68), t67);
    ((uint8_t *)&t68)[2] = 1;
    t54 = t68;
    goto ix55;
ie55:;
    gm_t9av822btjq16 t70;
    memset(&t70, 0, sizeof t70);
    gm_t9av822btjq16 t71 = t70;
    t54 = t71;
ix55:;
    t1 = t54;
    goto me2;
mn51:;
ma6:;
    gm_t9av822btjq16 t73;
    memset(&t73, 0, sizeof t73);
    t1 = t73;
    goto me2;
mn72:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 364, 61);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ts6oqppjh0ba3 gm_f_arith_op_90c7mvs(uint8_t a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arith_op_90c7mvs F;
    uint8_t gm_l0; /* op */
    uint8_t gm_l1; /* wrap */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    GM_STACK_CHECK_SHALLOW(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_t9qkh0rsooc8d t2;
        uint8_t t4 = gm_l1;
        if (!t4) goto ie3;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t7 = t6;
        t2 = t7;
        goto ix3;
ie3:;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
        gm_t9qkh0rsooc8d t10 = t9;
        t2 = t10;
ix3:;
        gm_l2 = t2;
    }
    {
        uint8_t t11 = gm_l0;
        uint8_t t12 = t11;
        gm_ts6oqppjh0ba3 t13;
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t12));
        if (!(t16 == ((uint8_t)0ULL))) goto mn15;
        gm_t9qkh0rsooc8d t17 = gm_l2;
        gm_ts6oqppjh0ba3 t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
        t13 = t18;
        goto me14;
mn15:;
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&t12));
        if (!(t20 == ((uint8_t)1ULL))) goto mn19;
        gm_t9qkh0rsooc8d t21 = gm_l2;
        gm_ts6oqppjh0ba3 t22;
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t21);
        t13 = t22;
        goto me14;
mn19:;
        gm_t9qkh0rsooc8d t24 = gm_l2;
        gm_ts6oqppjh0ba3 t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t24);
        t13 = t25;
        goto me14;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 374, 12);
me14:;
        uint8_t t26 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_ts6oqppjh0ba3 gm_f_cmp_opposite_tarvh4i(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_opposite_tarvh4i F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ts6oqppjh0ba3 t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma3; case ((uint8_t)11ULL): goto ma4; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)10ULL)) goto mn9;
    gm_ts6oqppjh0ba3 t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)13ULL));
    t1 = t11;
    goto me2;
mn9:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t13 != ((uint8_t)11ULL)) goto mn12;
    gm_ts6oqppjh0ba3 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)12ULL));
    t1 = t14;
    goto me2;
mn12:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)12ULL)) goto mn15;
    gm_ts6oqppjh0ba3 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)11ULL));
    t1 = t17;
    goto me2;
mn15:;
ma6:;
    gm_ts6oqppjh0ba3 t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)10ULL));
    t1 = t19;
    goto me2;
mn18:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1637, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_word_82qsidt(void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_root_levels_umkq7vp(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_levels_umkq7vp F;
    void * gm_l0; /* n */
    uint32_t gm_l1; /* levels */
    gm_t1j70din2upc4 gm_l2; /* x */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint32_t t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; case ((uint8_t)59ULL): goto ma7; case ((uint8_t)60ULL): goto ma7; case ((uint8_t)61ULL): goto ma7; case ((uint8_t)62ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t11 != ((uint8_t)7ULL)) goto mn10;
    uint32_t t12 = GM_LD(uint32_t, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l1 = t12;
    uint32_t t13 = gm_l1;
    t2 = t13;
    goto me3;
mn10:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t15 != ((uint8_t)10ULL)) goto mn14;
    gm_t1j70din2upc4 t16 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t1) + 0) + 8));
    gm_l2 = t16;
    void * t17 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
    uint32_t t18 = gm_f_root_levels_umkq7vp(t17);
    t2 = t18;
    goto me3;
mn14:;
ma6:;
    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t20 != ((uint8_t)12ULL)) goto mn19;
    gm_tp0fa5qv7rqnn t21 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t1) + 0) + 8));
    F.l3 = t21;
    void * t22 = GM_LD(void *, (((uint8_t *)&F.l3) + 0));
    uint32_t t23 = gm_f_root_levels_umkq7vp(t22);
    t2 = t23;
    goto me3;
mn19:;
ma7:;
    t2 = ((uint32_t)0ULL);
    goto me3;
mn24:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 874, 45);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_addressable_ltobf02(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_addressable_ltobf02 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tb31k5l794t7p gm_l2; /* r */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    gm_t9qkh0rsooc8d t3 = gm_f_vc_placeable_ip26ve2(t1, t2);
    gm_t9qkh0rsooc8d t4 = t3;
    uint8_t t5;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; default: goto ma10; }
ma7:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t13 != ((uint8_t)2ULL)) goto mn12;
    t5 = ((uint8_t)1);
    goto me6;
mn12:;
ma8:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t15 != ((uint8_t)1ULL)) goto mn14;
    uint8_t t16;
    void * t18 = gm_l0;
    void * t19 = gm_l1;
    gm_tgmjda3f8g6hg t20;
    gm_f_vc_field_root_lb3os0t((void *)&t20, t18, t19);
    gm_tgmjda3f8g6hg t21 = t20;
    if (!((((uint8_t *)&t21))[16] != 0)) goto ie17;
    gm_tb31k5l794t7p t22 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)&t21));
    gm_l2 = t22;
    uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
    uint64_t t25 = (uint64_t)t24;
    uint64_t t26 = (uintptr_t)t25;
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 8));
    uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 120) + 8);
    if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_srcutmnmd444bu0, 868, 44);
    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 120)) + t26 * 1));
    uint8_t t31 = t30;
    t16 = t31;
    goto ix17;
ie17:;
    uint8_t t33 = ((uint8_t)0);
    t16 = t33;
ix17:;
    t5 = t16;
    goto me6;
mn14:;
ma9:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t35 != ((uint8_t)0ULL)) goto mn34;
    t5 = ((uint8_t)0);
    goto me6;
mn34:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 866, 63);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_vc_align_na1h2on(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_vc_all_word_only_4ufgbu6(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_all_word_only_4ufgbu6 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* ns */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l4; /* x */
    GM_STACK_CHECK(&F, 175u);
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
                    void * t13 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 8));
                    void * t14 = t13;
                    void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        void * t20 = gm_l0;
                        void * t21 = gm_l4;
                        uint8_t t22 = gm_f_vc_word_only_rg8dfe5(t20, t21);
                        uint8_t t23 = (uint8_t)!t22;
                        if (!t23) goto ie19;
                        {
                            uint8_t t25 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1422, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t31 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_vc_alloc_3hmfvtt(void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_vc_alloc_slot_5eptdbb(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_alloc_slot_5eptdbb F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 248));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 248));
        uint64_t t7 = gm_l1;
        uint64_t r8; bool o9 = __builtin_add_overflow(t7, ((uint64_t)7ULL), &r8);
        if (o9) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 193, 20);
        uint64_t t10 = r8;
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 193, 19);
        uint64_t t11 = (uint64_t)(t10 / ((uint64_t)8ULL));
        uint64_t r12; bool o13 = __builtin_mul_overflow(t11, ((uint64_t)8ULL), &r12);
        if (o13) gm_trap("integer overflow in `*`", gm_srcutmnmd444bu0, 193, 19);
        uint64_t t14 = r12;
        uint64_t r15; bool o16 = __builtin_add_overflow(t6, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 193, 5);
        uint64_t t17 = r15;
        GM_ST(uint64_t, (((uint8_t *)t4) + 248), t17);
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 248));
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 256));
        uint8_t t23 = (t20 > t22);
        if (!t23) goto ie18;
        {
            void * t25 = gm_l0;
            void * t26 = gm_l0;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 248));
            GM_ST(uint64_t, (((uint8_t *)t25) + 256), t27);
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        uint64_t t28 = gm_l2;
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_vc_args_j2k3nul(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_vc_args_j2k3nul F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    void * gm_l6; /* x */
    uint8_t gm_l7; /* late */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l10; /* y */
    uint32_t gm_l11; /* at */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_6oos54v_2((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_b16_8 t4 = t3;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
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
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    void * t16 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t14 * 8));
                    gm_t22qobfhrnoif t17;
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(void *, ((uint8_t *)&t17 + 8), t16);
                    gm_t22qobfhrnoif t18 = t17;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l5 = t21;
                    void * t22 = GM_LD(void *, (((uint8_t *)&t18) + 8));
                    gm_l6 = t22;
                    {
                        void * t25 = gm_l6;
                        uint8_t t26 = gm_f_is_word_82qsidt(t25);
                        if (!t26) goto ie24;
                        {
                            gm_l7 = ((uint8_t)1);
                        }
                        {
                            {
                                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                void * t30 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                                uint64_t t31 = gm_l5;
                                uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                                if (o33) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1071, 27);
                                uint64_t t34 = r32;
                                uint64_t t35 = (uintptr_t)t34;
                                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                uint64_t t37 = (uintptr_t)t36;
                                if (t35 > t37 || t37 > (uintptr_t)t29) gm_trap_slice(t35, t37, t29, gm_srcutmnmd444bu0, 1071, 22);
                                gm_b16_8 t38;
                                GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t30 + t35 * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37 - t35));
                                gm_l8 = t38;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t40 = gm_l9;
                                    uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t42 = (t40 < t41);
                                    if (!t42) goto lb39;
                                    {
                                        uint64_t t45 = gm_l9;
                                        uint64_t t46 = (uintptr_t)t45;
                                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        void * t48 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t46 * 8));
                                        void * t49 = t48;
                                        void * t52 = GM_LD(void *, ((uint8_t *)&t49));
                                        gm_l10 = t52;
                                        {
                                            void * t55 = gm_l0;
                                            void * t56 = gm_l10;
                                            uint8_t t57 = gm_f_vc_reads_only_vncte32(t55, t56);
                                            uint8_t t58 = (uint8_t)!t57;
                                            if (!t58) goto ie54;
                                            {
                                                gm_l7 = ((uint8_t)0);
                                            }
                                            goto ix54;
ie54:;
ix54:;
                                        }
                                        goto me50;
mn51:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1071, 13);
me50:;
                                    }
lc39:;
                                    {
                                        uint64_t t61 = gm_l9;
                                        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                        uint64_t t64 = r62;
                                        gm_l9 = t64;
                                    }
                                }
lb39:;
                            }
                        }
                        {
                            void * t65 = (void *)((uint8_t *)&F.l2);
                            void * t66 = gm_l0;
                            void * t67 = gm_l6;
                            uint8_t t68 = gm_l7;
                            uint32_t t69 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t66, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_t9rfq1ocel0om t70;
                            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t70 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t70 + 4), t69);
                            gm_tftb1odc739t5 t71 = gm_f_push_jis5lhu_2(t65, t70);
                            gm_tftb1odc739t5 t72 = t71;
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcutmnmd444bu0, 1074, 13);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
                        goto ix24;
ie24:;
                        {
                            void * t75 = gm_l0;
                            void * t76 = gm_l0;
                            void * t77 = gm_l6;
                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 104));
                            uint64_t t79 = gm_f_vc_alloc_slot_5eptdbb(t76, t78);
                            uint32_t t80 = gm_f_vc_slot_at_rc6jhbj(t75, t79);
                            gm_l11 = t80;
                        }
                        {
                            void * t81 = gm_l0;
                            void * t82 = gm_l6;
                            uint32_t t83 = gm_l11;
                            gm_b0_1 t84 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t81, t82, t83);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                        }
                        {
                            void * t85 = (void *)((uint8_t *)&F.l2);
                            uint32_t t86 = gm_l11;
                            void * t87 = gm_l6;
                            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 104));
                            uint32_t t89 = (uint32_t)t88;
                            gm_tcc1rq4n1avb2 t90;
                            GM_ST(uint32_t, ((uint8_t *)&t90 + 0), t86);
                            GM_ST(uint32_t, ((uint8_t *)&t90 + 4), t89);
                            gm_t9rfq1ocel0om t91;
                            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t91 + 4), t90);
                            gm_tftb1odc739t5 t92 = gm_f_push_jis5lhu_2(t85, t91);
                            gm_tftb1odc739t5 t93 = t92;
                            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcutmnmd444bu0, 1078, 13);
                            gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                        }
ix24:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1067, 5);
me19:;
                }
lc6:;
                {
                    uint64_t t96 = gm_l4;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l4 = t99;
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
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t101 = 0;
        F.ret = t100;
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
extern __attribute__((aligned(4))) uint8_t gm_f_vc_assert_ok_6n0rc7j(gm_tvdg94bk7aa22 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_block_ql7sru3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2) {
    struct gm_fr_vc_block_ql7sru3 F;
    void * gm_l0; /* c */
    void * gm_l1; /* b */
    gm_tmn7n0n9raou5 gm_l2; /* want */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l6; /* t */
    GM_STACK_CHECK(&F, 437u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 24));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l1;
            gm_tmn7n0n9raou5 t8 = gm_l2;
            gm_b0_1 t9 = gm_f_vc_defer_block_bgamoqf((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t10;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 0));
            gm_l3 = t14;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l4;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t2k9dbsv5ff58 t26 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 40));
                    F.l5 = t26;
                    {
                        void * t28 = gm_l0;
                        gm_b0_1 t29 = gm_f_vc_stmt_01mcb8k((void *)NULL, cx, NULL, t28, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l5)));
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 986, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t31 = gm_l4;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l4 = t34;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
        }
    }
    {
        void * t36 = gm_l1;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 16));
        void * t38 = t37;
        if (!(GM_LD(void *, ((uint8_t *)&t38)) != 0)) goto ie35;
        void * t39 = GM_LD(void *, ((uint8_t *)&t38));
        gm_l6 = t39;
        {
            void * t41 = gm_l0;
            void * t42 = gm_l6;
            gm_tmn7n0n9raou5 t43 = gm_l2;
            gm_b0_1 t44 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t45;
            uint8_t t46 = 0;
            F.ret = t45;
            goto gm_fret;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t52) { case ((uint8_t)0ULL): goto ma48; case ((uint8_t)1ULL): goto ma49; case ((uint8_t)2ULL): goto ma50; case ((uint8_t)3ULL): goto ma49; default: goto ma51; }
ma48:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t54 != ((uint8_t)0ULL)) goto mn53;
        void * t55 = gm_l0;
        void * t56 = gm_l0;
        uint32_t t57 = gm_f_vc_alloc_3hmfvtt(t56);
        gm_tb31k5l794t7p t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 0), t57);
        GM_ST(uint64_t, ((uint8_t *)&t58 + 8), ((uint64_t)0ULL));
        gm_tjc2ga47o3k6g t59;
        memset(&t59, 0, sizeof t59);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t59 + 8), t58);
        gm_b0_1 t60 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t55, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me47;
mn53:;
ma49:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t64 != ((uint8_t)1ULL)) goto pa63;
        goto po62;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t66 != ((uint8_t)3ULL)) goto pa65;
        goto po62;
pa65:;
        goto mn61;
po62:;
        goto me47;
mn61:;
ma50:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t69 != ((uint8_t)2ULL)) goto mn68;
        void * t70 = gm_l0;
        gm_tjc2ga47o3k6g t71;
        memset(&t71, 0, sizeof t71);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)75ULL));
        gm_b0_1 t72 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t70, &t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me47;
mn68:;
ma51:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 991, 5);
me47:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_branch_r6k4hjp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, void * a3) {
    struct gm_fr_vc_branch_r6k4hjp F;
    void * gm_l0; /* c */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* if_true */
    void * gm_l3; /* to */
    uint32_t gm_l4; /* save */
    gm_tkm39klvnn4cr gm_l5; /* x */
    uint32_t gm_l6; /* a */
    uint32_t gm_l7; /* b */
    gm_ts6oqppjh0ba3 gm_l8; /* op */
    gm_ta6jn77i9rmqf gm_l9; /* x */
    uint32_t gm_l10; /* a */
    uint32_t gm_l11; /* b */
    void * gm_l12; /* x */
    uint8_t gm_l13; /* is_and */
    void * gm_l14; /* l */
    void * gm_l15; /* r */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* j */
    uint32_t gm_l20; /* r */
    GM_STACK_CHECK(&F, 1377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; case ((uint8_t)59ULL): goto ma10; case ((uint8_t)60ULL): goto ma10; case ((uint8_t)61ULL): goto ma10; case ((uint8_t)62ULL): goto ma10; default: goto ma10; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t14 != ((uint8_t)23ULL)) goto mn13;
        gm_tkm39klvnn4cr t15 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t4) + 0) + 8));
        gm_l5 = t15;
        {
            uint8_t t18 = 0;
            uint8_t t19 = gm_l2;
            uint8_t t20 = (uint8_t)!t19;
            if (!t20) {
                uint8_t t21;
                uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 4) + 0));
                switch (t27) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma25; case ((uint8_t)4ULL): goto ma25; default: goto ma25; }
ma23:;
                uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 4) + 0));
                if (t29 != ((uint8_t)0ULL)) goto mn28;
                t21 = ((uint8_t)1);
                goto me22;
mn28:;
ma24:;
                uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)&gm_l5) + 4) + 0));
                if (t31 != ((uint8_t)1ULL)) goto mn30;
                t21 = ((uint8_t)1);
                goto me22;
mn30:;
ma25:;
                t21 = ((uint8_t)0);
                goto me22;
mn32:;
ma26:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1600, 31);
me22:;
                t18 = t21;
            } else t18 = 1;
            if (!t18) goto ie17;
            {
                void * t34 = gm_l0;
                void * t35 = GM_LD(void *, (((uint8_t *)&gm_l5) + 16));
                void * t36 = gm_l0;
                void * t37 = GM_LD(void *, (((uint8_t *)&gm_l5) + 24));
                uint8_t t38 = gm_f_vc_reads_only_vncte32(t36, t37);
                uint32_t t39 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t34, t35, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t39;
            }
            {
                void * t40 = gm_l0;
                void * t41 = GM_LD(void *, (((uint8_t *)&gm_l5) + 24));
                uint32_t t42 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t40, t41, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t42;
            }
            {
                gm_ts6oqppjh0ba3 t43;
                uint8_t t45 = gm_l2;
                if (!t45) goto ie44;
                gm_ts6oqppjh0ba3 t47 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l5) + 0));
                gm_ts6oqppjh0ba3 t48 = gm_f_cmp_opposite_tarvh4i(t47);
                gm_ts6oqppjh0ba3 t49 = t48;
                t43 = t49;
                goto ix44;
ie44:;
                gm_ts6oqppjh0ba3 t51 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l5) + 0));
                gm_ts6oqppjh0ba3 t52 = t51;
                t43 = t52;
ix44:;
                gm_l8 = t43;
            }
            {
                void * t53 = gm_l3;
                void * t54 = gm_l0;
                gm_tnkql1rjn1vbj t55;
                memset((uint8_t *)&t55 + 2, 0, 2);
                gm_ts6oqppjh0ba3 t56 = gm_l8;
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t55 + 0, t56);
                gm_tmn7n0n9raou5 t57 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l5) + 4));
                GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t55 + 4, t57);
                uint32_t t58 = gm_l6;
                GM_ST(uint32_t, (uint8_t *)&t55 + 12, t58);
                uint32_t t59 = gm_l7;
                GM_ST(uint32_t, (uint8_t *)&t55 + 16, t59);
                GM_ST(uint32_t, (uint8_t *)&t55 + 20, ((uint32_t)0ULL));
                gm_tjc2ga47o3k6g t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t60 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)28ULL));
                GM_ST(gm_tnkql1rjn1vbj, ((uint8_t *)&t60 + 8), t55);
                uint32_t t61 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t54, &t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t62 = gm_f_push_im9d4ro(t53, t61);
                gm_tftb1odc739t5 t63 = t62;
                if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcutmnmd444bu0, 1604, 17);
                gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
            }
            goto ix17;
ie17:;
            {
                void * t66 = gm_l3;
                void * t67 = gm_l0;
                void * t68 = gm_l0;
                void * t69 = gm_l1;
                uint32_t t70 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t68, t69, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcc1rq4n1avb2 t71;
                GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t70);
                GM_ST(uint32_t, ((uint8_t *)&t71 + 4), ((uint32_t)0ULL));
                gm_tjc2ga47o3k6g t72;
                memset(&t72, 0, sizeof t72);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)40ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t72 + 8), t71);
                uint32_t t73 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t67, &t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t74 = gm_f_push_im9d4ro(t66, t73);
                gm_tftb1odc739t5 t75 = t74;
                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srcutmnmd444bu0, 1606, 17);
                gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
            }
ix17:;
        }
        goto me5;
mn13:;
ma7:;
        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t78 != ((uint8_t)24ULL)) goto mn77;
        gm_ta6jn77i9rmqf t79 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t4) + 0) + 8));
        gm_l9 = t79;
        {
            void * t81 = gm_l0;
            void * t82 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
            void * t83 = gm_l0;
            void * t84 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
            uint8_t t85 = gm_f_vc_reads_only_vncte32(t83, t84);
            uint32_t t86 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t81, t82, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t86;
        }
        {
            void * t87 = gm_l0;
            void * t88 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
            uint32_t t89 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t87, t88, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t89;
        }
        {
            void * t90 = gm_l3;
            void * t91 = gm_l0;
            gm_tufm7evi61pnh t92;
            memset((uint8_t *)&t92 + 9, 0, 3);
            gm_tmn7n0n9raou5 t93 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l9) + 0));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t92 + 0, t93);
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 24));
            uint8_t t95 = gm_l2;
            uint8_t t96 = (!(t94 == t95));
            GM_ST(uint8_t, (uint8_t *)&t92 + 8, t96);
            uint32_t t97 = gm_l10;
            GM_ST(uint32_t, (uint8_t *)&t92 + 12, t97);
            uint32_t t98 = gm_l11;
            GM_ST(uint32_t, (uint8_t *)&t92 + 16, t98);
            GM_ST(uint32_t, (uint8_t *)&t92 + 20, ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t99 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t99 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t99 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)29ULL));
            GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t99 + 8), t92);
            uint32_t t100 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t91, &t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t101 = gm_f_push_im9d4ro(t90, t100);
            gm_tftb1odc739t5 t102 = t101;
            if (((((uint8_t *)&t102))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t102) + 1, gm_srcutmnmd444bu0, 1612, 13);
            gm_b0_1 t103 = GM_LD(gm_b0_1, (((uint8_t *)&t102) + 1));
        }
        goto me5;
mn77:;
ma8:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t105 != ((uint8_t)25ULL)) goto mn104;
        void * t106 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l12 = t106;
        void * t107 = gm_l0;
        void * t108 = gm_l12;
        uint8_t t109 = gm_l2;
        uint8_t t110 = (uint8_t)!t109;
        void * t111 = gm_l3;
        gm_b0_1 t112 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t107, t108, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn104:;
ma9:;
        uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t114 != ((uint8_t)32ULL)) goto mn113;
        uint8_t t115 = GM_LD(uint8_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l13 = t115;
        void * t116 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l14 = t116;
        void * t117 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l15 = t117;
        {
            uint8_t t120 = gm_l13;
            uint8_t t121 = gm_l2;
            uint8_t t122 = ((t120 == t121));
            if (!t122) goto ie119;
            {
                gm_f_list_go2qg0b((void *)&F.l16, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t124 = gm_l0;
                void * t125 = gm_l14;
                uint8_t t126 = gm_l2;
                uint8_t t127 = (uint8_t)!t126;
                void * t128 = (void *)((uint8_t *)&F.l16);
                gm_b0_1 t129 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t124, t125, t127, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t130 = gm_l0;
                void * t131 = gm_l15;
                uint8_t t132 = gm_l2;
                void * t133 = gm_l3;
                gm_b0_1 t134 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t130, t131, t132, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                    gm_l17 = t136;
                }
                {
                    gm_l18 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t138 = gm_l18;
                        uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        uint8_t t140 = (t138 < t139);
                        if (!t140) goto lb137;
                        {
                            uint64_t t143 = gm_l18;
                            uint64_t t144 = (uintptr_t)t143;
                            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            uint32_t t146 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t144 * 4));
                            uint32_t t147 = t146;
                            uint32_t t150 = GM_LD(uint32_t, ((uint8_t *)&t147));
                            gm_l19 = t150;
                            {
                                void * t152 = gm_l0;
                                uint32_t t153 = gm_l19;
                                void * t154 = gm_l0;
                                uint32_t t155 = gm_f_vc_here_cc4mbj3(t154);
                                gm_f_vc_patch_pbmmb3q(t152, t153, t155);
                            }
                            goto me148;
mn149:;
                            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1621, 17);
me148:;
                        }
lc137:;
                        {
                            uint64_t t157 = gm_l18;
                            uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                            uint64_t t160 = r158;
                            gm_l18 = t160;
                        }
                    }
lb137:;
                }
            }
            {
                void * t161 = (void *)((uint8_t *)&F.l16);
                gm_f_deinit_padgogk(t161);
            }
            goto ix119;
ie119:;
            {
                void * t163 = gm_l0;
                void * t164 = gm_l14;
                uint8_t t165 = gm_l2;
                void * t166 = gm_l3;
                gm_b0_1 t167 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t163, t164, t165, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t168 = gm_l0;
                void * t169 = gm_l15;
                uint8_t t170 = gm_l2;
                void * t171 = gm_l3;
                gm_b0_1 t172 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t168, t169, t170, t171);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix119:;
        }
        goto me5;
mn113:;
ma10:;
        {
            void * t175 = gm_l0;
            void * t176 = gm_l1;
            uint32_t t177 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t175, t176, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t177;
        }
        {
            void * t178 = gm_l3;
            void * t179 = gm_l0;
            gm_tjc2ga47o3k6g t180;
            uint8_t t182 = gm_l2;
            if (!t182) goto ie181;
            uint32_t t184 = gm_l20;
            gm_tcc1rq4n1avb2 t185;
            GM_ST(uint32_t, ((uint8_t *)&t185 + 0), t184);
            GM_ST(uint32_t, ((uint8_t *)&t185 + 4), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t186;
            memset(&t186, 0, sizeof t186);
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)40ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t186 + 8), t185);
            gm_tjc2ga47o3k6g t187 = t186;
            t180 = t187;
            goto ix181;
ie181:;
            uint32_t t189 = gm_l20;
            gm_tcc1rq4n1avb2 t190;
            GM_ST(uint32_t, ((uint8_t *)&t190 + 0), t189);
            GM_ST(uint32_t, ((uint8_t *)&t190 + 4), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t191;
            memset(&t191, 0, sizeof t191);
            GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)39ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t191 + 8), t190);
            gm_tjc2ga47o3k6g t192 = t191;
            t180 = t192;
ix181:;
            uint32_t t193 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t179, &t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t194 = gm_f_push_im9d4ro(t178, t193);
            gm_tftb1odc739t5 t195 = t194;
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_srcutmnmd444bu0, 1630, 13);
            gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
        }
        goto me5;
mn173:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1596, 5);
me5:;
    }
    {
        void * t197 = gm_l0;
        uint32_t t198 = gm_l4;
        GM_ST(uint32_t, (((uint8_t *)t197) + 224), t198);
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
__attribute__((aligned(4))) uint8_t gm_f_vc_builds_d9pue9e(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_builds_d9pue9e F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* levels */
    void * gm_l3; /* lhs */
    gm_tmn7n0n9raou5 gm_l4; /* kind */
    void * gm_l5; /* x */
    gm_t083kd7b1kkag gm_l8; /* x */
    void * gm_l9; /* x */
    gm_tvdg94bk7aa22 gm_l10; /* op */
    gm_b16_8 gm_l11; /* args */
    GM_STACK_CHECK(&F, 779u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma17; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma17; case ((uint8_t)27ULL): goto ma17; case ((uint8_t)28ULL): goto ma17; case ((uint8_t)29ULL): goto ma17; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma4; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma15; case ((uint8_t)35ULL): goto ma4; case ((uint8_t)36ULL): goto ma4; case ((uint8_t)37ULL): goto ma4; case ((uint8_t)38ULL): goto ma4; case ((uint8_t)39ULL): goto ma17; case ((uint8_t)40ULL): goto ma17; case ((uint8_t)41ULL): goto ma17; case ((uint8_t)42ULL): goto ma17; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma17; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma4; case ((uint8_t)51ULL): goto ma4; case ((uint8_t)52ULL): goto ma4; case ((uint8_t)53ULL): goto ma17; case ((uint8_t)54ULL): goto ma17; case ((uint8_t)55ULL): goto ma17; case ((uint8_t)56ULL): goto ma17; case ((uint8_t)57ULL): goto ma17; case ((uint8_t)58ULL): goto ma17; case ((uint8_t)59ULL): goto ma17; case ((uint8_t)60ULL): goto ma17; case ((uint8_t)61ULL): goto ma11; case ((uint8_t)62ULL): goto ma17; default: goto ma17; }
ma4:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t23 != ((uint8_t)35ULL)) goto pa22;
    goto po21;
pa22:;
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t25 != ((uint8_t)37ULL)) goto pa24;
    goto po21;
pa24:;
    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t27 != ((uint8_t)36ULL)) goto pa26;
    goto po21;
pa26:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t29 != ((uint8_t)1ULL)) goto pa28;
    goto po21;
pa28:;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t31 != ((uint8_t)38ULL)) goto pa30;
    goto po21;
pa30:;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t33 != ((uint8_t)51ULL)) goto pa32;
    goto po21;
pa32:;
    uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t35 != ((uint8_t)52ULL)) goto pa34;
    goto po21;
pa34:;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t37 != ((uint8_t)50ULL)) goto pa36;
    goto po21;
pa36:;
    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t39 != ((uint8_t)31ULL)) goto pa38;
    goto po21;
pa38:;
    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t41 != ((uint8_t)19ULL)) goto pa40;
    goto po21;
pa40:;
    goto mn20;
po21:;
    t2 = ((uint8_t)1);
    goto me3;
mn20:;
ma5:;
    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t43 != ((uint8_t)7ULL)) goto mn42;
    uint32_t t44 = GM_LD(uint32_t, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l2 = t44;
    uint32_t t45 = gm_l2;
    uint8_t t46 = (t45 > ((uint32_t)0ULL));
    t2 = t46;
    goto me3;
mn42:;
ma6:;
    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t48 != ((uint8_t)46ULL)) goto mn47;
    void * t49 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l3 = t49;
    void * t50 = gm_l0;
    void * t51 = gm_l1;
    void * t52 = gm_l3;
    gm_t0hcjal40h40i t53;
    gm_f_vc_else_shape_n9vc3pg((void *)&t53, (void *)NULL, cx, NULL, t50, t51, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0hcjal40h40i t54 = t53;
    uint8_t t55;
    if (((((uint8_t *)&t54))[32] != 0)) goto mn57;
    t55 = ((uint8_t)0);
    goto me56;
mn57:;
    t55 = ((uint8_t)1);
    goto me56;
mn58:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1241, 25);
me56:;
    t2 = t55;
    goto me3;
mn47:;
ma7:;
    uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t60 != ((uint8_t)33ULL)) goto mn59;
    gm_tmn7n0n9raou5 t61 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l4 = t61;
    void * t62 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 8));
    gm_l5 = t62;
    uint8_t t63 = 0;
    void * t64 = gm_l0;
    gm_tmn7n0n9raou5 t65 = gm_l4;
    void * t66 = gm_l1;
    uint8_t t67 = gm_f_vc_coerce_builds_j86oug9((void *)NULL, cx, NULL, t64, t65, t66);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t67) {
        uint8_t t68 = 0;
        uint8_t t69 = 0;
        uint8_t t70 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)0ULL)));
        if (t70) {
            void * t71 = gm_l5;
            uint8_t t72 = gm_f_is_word_82qsidt(t71);
            t69 = t72;
        } else t69 = 0;
        if (t69) {
            void * t73 = gm_l1;
            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 104));
            uint8_t t75 = (t74 > ((uint64_t)8ULL));
            t68 = t75;
        } else t68 = 0;
        t63 = t68;
    } else t63 = 1;
    t2 = t63;
    goto me3;
mn59:;
ma8:;
    uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t77 != ((uint8_t)44ULL)) goto mn76;
    void * t78 = gm_l0;
    void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
    void * t80 = gm_l1;
    gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 96));
    gm_t53at80pfdus3 t82;
    gm_f_kind_htv6093((void *)&t82, t79, GM_CONV(gm_csguankko4lmhd, t81));
    gm_t53at80pfdus3 t83 = t82;
    uint8_t t84;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t83) + 0));
    if (t87 != ((uint8_t)14ULL)) goto mn86;
    t84 = ((uint8_t)1);
    goto me85;
mn86:;
    t84 = ((uint8_t)0);
    goto me85;
mn88:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1243, 20);
me85:;
    t2 = t84;
    goto me3;
mn76:;
ma9:;
    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t90 != ((uint8_t)43ULL)) goto mn89;
    void * t91 = gm_l0;
    void * t92 = gm_l1;
    gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 96));
    uint8_t t94 = gm_f_vc_variant_builds_hjfpsfe((void *)NULL, cx, NULL, t91, t93);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t94;
    goto me3;
mn89:;
ma10:;
    uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t96 != ((uint8_t)13ULL)) goto mn95;
    gm_tt49maei14cl6 t97 = GM_LD(gm_tt49maei14cl6, ((((uint8_t *)t1) + 0) + 8));
    F.l6 = t97;
    void * t98 = gm_l0;
    uint8_t t99 = gm_f_vc_slice_builds_2cdjc7j(t98, &(*(gm_tt49maei14cl6 *)((uint8_t *)&F.l6)));
    t2 = t99;
    goto me3;
mn95:;
ma11:;
    uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t101 != ((uint8_t)61ULL)) goto mn100;
    gm_t710lsrq1m8gk t102 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t1) + 0) + 8));
    F.l7 = t102;
    void * t103 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
    void * t104 = t103;
    uint8_t t105;
    if ((GM_LD(void *, ((uint8_t *)&t104)) != 0)) goto mn107;
    t105 = ((uint8_t)1);
    goto me106;
mn107:;
    t105 = ((uint8_t)0);
    goto me106;
mn108:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1246, 17);
me106:;
    t2 = t105;
    goto me3;
mn100:;
ma12:;
    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t110 != ((uint8_t)2ULL)) goto mn109;
    t2 = ((uint8_t)1);
    goto me3;
mn109:;
ma13:;
    uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t112 != ((uint8_t)47ULL)) goto mn111;
    gm_t083kd7b1kkag t113 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t1) + 0) + 8));
    gm_l8 = t113;
    void * t114 = gm_l0;
    void * t115 = gm_l1;
    void * t116 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
    gm_t9805qtonbsag t117 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l8) + 8));
    uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 16));
    uint8_t t119 = gm_f_vc_try_ok_ng7fba1((void *)NULL, cx, NULL, t114, t115, t116, t117, t118);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t119;
    goto me3;
mn111:;
ma14:;
    uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t121 != ((uint8_t)45ULL)) goto mn120;
    void * t122 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l9 = t122;
    void * t123 = gm_l0;
    void * t124 = gm_l1;
    void * t125 = gm_l9;
    uint8_t t126 = gm_f_vc_unwrap_ok_tb3qnv4((void *)NULL, cx, NULL, t123, t124, t125);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t126;
    goto me3;
mn120:;
ma15:;
    uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t128 != ((uint8_t)34ULL)) goto mn127;
    gm_tvdg94bk7aa22 t129 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l10 = t129;
    gm_b16_8 t130 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l11 = t130;
    gm_tvdg94bk7aa22 t131 = gm_l10;
    gm_b16_8 t132 = gm_l11;
    uint8_t t133 = gm_f_vc_builtin_ok_kbscem0(t131, t132);
    t2 = t133;
    goto me3;
mn127:;
ma16:;
    uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t137 != ((uint8_t)6ULL)) goto pa136;
    goto po135;
pa136:;
    uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t139 != ((uint8_t)10ULL)) goto pa138;
    goto po135;
pa138:;
    uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t141 != ((uint8_t)9ULL)) goto pa140;
    goto po135;
pa140:;
    uint8_t t143 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t143 != ((uint8_t)12ULL)) goto pa142;
    goto po135;
pa142:;
    goto mn134;
po135:;
    void * t144 = gm_l0;
    void * t145 = gm_l1;
    uint8_t t146 = gm_f_vc_copies_glmdmll(t144, t145);
    t2 = t146;
    goto me3;
mn134:;
ma17:;
    t2 = ((uint8_t)0);
    goto me3;
mn147:;
ma18:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1238, 64);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_builtin_9d4kon2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tj2m9dbh1ekv6 a2, void * a3) {
    struct gm_fr_vc_builtin_9d4kon2 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* args */
    gm_tj2m9dbh1ekv6 gm_l2; /* dst */
    void * gm_l3; /* n */
    uint32_t gm_l4; /* save */
    uint64_t gm_l5; /* slots */
    gm_b16_8 gm_l6; /* out */
    GM_STACK_CHECK(&F, 314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l5 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        gm_b16_8 t8 = gm_f_vc_args_j2k3nul((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_tbeu0qv6bbs2s t10;
        gm_b16_8 t11 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        gm_tj2m9dbh1ekv6 t12 = gm_l2;
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t10 + 16, t12);
        void * t13 = gm_l3;
        GM_ST(void *, (uint8_t *)&t10 + 24, t13);
        gm_tjc2ga47o3k6g t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tbeu0qv6bbs2s, ((uint8_t *)&t14 + 8), t10);
        gm_b0_1 t15 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t9, &t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = gm_l5;
        GM_ST(uint64_t, (((uint8_t *)t16) + 248), t17);
    }
    {
        void * t18 = gm_l0;
        uint32_t t19;
        uint8_t t21;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        t21 = ((uint8_t)1);
        goto me22;
mn23:;
        t21 = ((uint8_t)0);
        goto me22;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 527, 21);
me22:;
        if (!t21) goto ie20;
        uint32_t t27 = gm_l4;
        uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 527, 38);
        uint32_t t30 = r28;
        uint32_t t31 = t30;
        t19 = t31;
        goto ix20;
ie20:;
        uint32_t t33 = gm_l4;
        uint32_t t34 = t33;
        t19 = t34;
ix20:;
        gm_f_vc_set_top_h0qmvd1(t18, t19);
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
inline __attribute__((aligned(4))) uint8_t gm_f_vc_builtin_ok_kbscem0(gm_tvdg94bk7aa22 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_builtin_ok_kbscem0 F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_b16_8 gm_l1; /* args */
    GM_STACK_CHECK_SHALLOW(&F, 59u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)61ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)81ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)63ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)73ULL)) goto pa11;
    goto po4;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)72ULL)) goto pa13;
    goto po4;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)36ULL)) goto pa15;
    goto po4;
pa15:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t18 != ((uint8_t)37ULL)) goto pa17;
    goto po4;
pa17:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)38ULL)) goto pa19;
    goto po4;
pa19:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)39ULL)) goto pa21;
    goto po4;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)79ULL)) goto pa23;
    goto po4;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)80ULL)) goto pa25;
    goto po4;
pa25:;
    goto mn3;
po4:;
    t1 = ((uint8_t)0);
    goto me2;
mn3:;
    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint64_t t29 = GM_LD(uint64_t, gm_c956v7aird0);
    uint8_t t30 = (t28 <= t29);
    t1 = t30;
    goto me2;
mn27:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 514, 64);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_27qg9ft(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tj2m9dbh1ekv6 a3, void * a4) {
    struct gm_fr_vc_call_27qg9ft F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tj2m9dbh1ekv6 gm_l3; /* dst */
    void * gm_l4; /* n */
    uint32_t gm_l5; /* save */
    gm_b16_8 gm_l6; /* out */
    GM_STACK_CHECK(&F, 290u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l2;
        gm_b16_8 t6 = gm_f_vc_args_j2k3nul((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_thaddei6om6bp t8;
        memset((uint8_t *)&t8 + 4, 0, 4);
        uint32_t t9 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t8 + 0, t9);
        gm_b16_8 t10 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 8, t10);
        gm_tj2m9dbh1ekv6 t11 = gm_l3;
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t8 + 24, t11);
        void * t12 = gm_l4;
        GM_ST(void *, (uint8_t *)&t8 + 32, t12);
        gm_tjc2ga47o3k6g t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)47ULL));
        GM_ST(gm_thaddei6om6bp, ((uint8_t *)&t13 + 8), t8);
        gm_b0_1 t14 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t7, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        uint32_t t16;
        uint8_t t18;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        t18 = ((uint8_t)1);
        goto me19;
mn20:;
        t18 = ((uint8_t)0);
        goto me19;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1042, 21);
me19:;
        if (!t18) goto ie17;
        uint32_t t24 = gm_l5;
        uint32_t r25; bool o26 = __builtin_add_overflow(t24, ((uint32_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1042, 38);
        uint32_t t27 = r25;
        uint32_t t28 = t27;
        t16 = t28;
        goto ix17;
ie17:;
        uint32_t t30 = gm_l5;
        uint32_t t31 = t30;
        t16 = t31;
ix17:;
        gm_f_vc_set_top_h0qmvd1(t15, t16);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_to_local_qd1fddh(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_vc_call_to_local_qd1fddh F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* value */
    uint32_t gm_l3; /* at */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 104));
        uint64_t t6 = gm_f_vc_alloc_slot_5eptdbb(t3, t5);
        uint32_t t7 = gm_f_vc_slot_at_rc6jhbj(t2, t6);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l2;
        uint32_t t10 = gm_l3;
        gm_b0_1 t11 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_l1;
        uint32_t t14 = gm_l3;
        void * t15 = gm_l2;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 104));
        uint32_t t17 = (uint32_t)t16;
        void * t18 = gm_l2;
        gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 112));
        gm_b0_1 t20 = gm_f_vc_set_local_slot_l28kec5((void *)NULL, cx, NULL, t12, t13, t14, t17, t19);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_call_value_5j9edsq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tj2m9dbh1ekv6 a3, void * a4) {
    struct gm_fr_vc_call_value_5j9edsq F;
    void * gm_l0; /* c */
    void * gm_l1; /* callee */
    gm_b16_8 gm_l2; /* args */
    gm_tj2m9dbh1ekv6 gm_l3; /* dst */
    void * gm_l4; /* n */
    uint32_t gm_l5; /* save */
    uint32_t gm_l6; /* at */
    gm_b16_8 gm_l7; /* out */
    GM_STACK_CHECK(&F, 378u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
        uint64_t t8 = gm_f_vc_alloc_slot_5eptdbb(t5, t7);
        uint32_t t9 = gm_f_vc_slot_at_rc6jhbj(t4, t8);
        gm_l6 = t9;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        uint32_t t12 = gm_l6;
        gm_b0_1 t13 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        gm_b16_8 t15 = gm_l2;
        gm_b16_8 t16 = gm_f_vc_args_j2k3nul((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_t9ka92sj5labh t18;
        memset((uint8_t *)&t18 + 4, 0, 4);
        uint32_t t19 = gm_l6;
        GM_ST(uint32_t, (uint8_t *)&t18 + 0, t19);
        void * t20 = gm_l1;
        GM_ST(void *, (uint8_t *)&t18 + 8, t20);
        gm_b16_8 t21 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t21);
        gm_tj2m9dbh1ekv6 t22 = gm_l3;
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t18 + 32, t22);
        void * t23 = gm_l4;
        GM_ST(void *, (uint8_t *)&t18 + 40, t23);
        gm_tjc2ga47o3k6g t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)48ULL));
        GM_ST(gm_t9ka92sj5labh, ((uint8_t *)&t24 + 8), t18);
        gm_b0_1 t25 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t17, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t26 = gm_l0;
        uint32_t t27;
        uint8_t t29;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        t29 = ((uint8_t)1);
        goto me30;
mn31:;
        t29 = ((uint8_t)0);
        goto me30;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1052, 21);
me30:;
        if (!t29) goto ie28;
        uint32_t t35 = gm_l5;
        uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1052, 38);
        uint32_t t38 = r36;
        uint32_t t39 = t38;
        t27 = t39;
        goto ix28;
ie28:;
        uint32_t t41 = gm_l5;
        uint32_t t42 = t41;
        t27 = t42;
ix28:;
        gm_f_vc_set_top_h0qmvd1(t26, t27);
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
inline __attribute__((aligned(4))) uint8_t gm_f_vc_coerce_builds_j86oug9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2) {
    struct gm_fr_vc_coerce_builds_j86oug9 F;
    void * gm_l0; /* c */
    gm_tmn7n0n9raou5 gm_l1; /* kind */
    void * gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t10 != ((uint8_t)2ULL)) goto mn9;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
    void * t13 = gm_l2;
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 96));
    gm_t53at80pfdus3 t15;
    gm_f_kind_htv6093((void *)&t15, t12, GM_CONV(gm_csguankko4lmhd, t14));
    gm_t53at80pfdus3 t16 = t15;
    uint8_t t17;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
    if (t20 != ((uint8_t)13ULL)) goto mn19;
    t17 = ((uint8_t)1);
    goto me18;
mn19:;
    t17 = ((uint8_t)0);
    goto me18;
mn21:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1257, 22);
me18:;
    t1 = t17;
    goto me2;
mn9:;
ma4:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t23 != ((uint8_t)3ULL)) goto mn22;
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
    void * t26 = gm_l2;
    gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 96));
    gm_t53at80pfdus3 t28;
    gm_f_kind_htv6093((void *)&t28, t25, GM_CONV(gm_csguankko4lmhd, t27));
    gm_t53at80pfdus3 t29 = t28;
    uint8_t t30;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
    if (t33 != ((uint8_t)14ULL)) goto mn32;
    t30 = ((uint8_t)1);
    goto me31;
mn32:;
    t30 = ((uint8_t)0);
    goto me31;
mn34:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1258, 16);
me31:;
    t1 = t30;
    goto me2;
mn22:;
ma5:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t38 != ((uint8_t)6ULL)) goto pa37;
    goto po36;
pa37:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t40 != ((uint8_t)8ULL)) goto pa39;
    goto po36;
pa39:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t42 != ((uint8_t)10ULL)) goto pa41;
    goto po36;
pa41:;
    goto mn35;
po36:;
    t1 = ((uint8_t)1);
    goto me2;
mn35:;
ma6:;
    t1 = ((uint8_t)0);
    goto me2;
mn43:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1256, 91);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_compact_8tukqc5(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_vc_compact_8tukqc5 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* n */
    uint32_t gm_l3; /* k */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l12; /* t */
    uint32_t gm_l13; /* to */
    uint32_t gm_l14; /* to */
    uint32_t gm_l15; /* r */
    uint32_t gm_l16; /* to */
    uint32_t gm_l17; /* r */
    uint32_t gm_l18; /* to */
    gm_tpkdfv1vgiqij gm_l19; /* y */
    gm_tnkql1rjn1vbj gm_l20; /* y */
    gm_tufm7evi61pnh gm_l21; /* y */
    gm_t37c2jujtiadn gm_l22; /* y */
    gm_tjqgc9rpvqnkt gm_l23; /* y */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    void * gm_l27; /* lp */
    GM_STACK_CHECK(&F, 2162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 16) + 0) + 8);
        gm_l1 = t3;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l3 = ((uint32_t)0ULL);
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t5 = gm_l1;
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = gm_l4;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        uint32_t t19 = gm_l3;
                        gm_tftb1odc739t5 t20 = gm_f_push_im9d4ro(t18, t19);
                        gm_tftb1odc739t5 t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcutmnmd444bu0, 117, 9);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    {
                        uint64_t t24 = gm_l6;
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l0;
                        uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 16) + 0) + 8);
                        if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcutmnmd444bu0, 118, 17);
                        uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t26) + 16) + 0)) + t25 * 56) + 0) == ((uint8_t)79ULL)));
                        uint8_t t29 = (uint8_t)!t28;
                        if (!t29) goto ie23;
                        {
                            uint32_t t31 = gm_l3;
                            uint32_t r32; bool o33 = __builtin_add_overflow(t31, ((uint32_t)1ULL), &r32);
                            if (o33) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 118, 43);
                            uint32_t t34 = r32;
                            gm_l3 = t34;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 116, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
                }
            }
lb6:;
        }
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l2);
        uint32_t t41 = gm_l3;
        gm_tftb1odc739t5 t42 = gm_f_push_im9d4ro(t40, t41);
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcutmnmd444bu0, 120, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        uint32_t t46 = gm_l3;
        uint64_t t47 = (uint64_t)t46;
        uint64_t t48 = gm_l1;
        uint8_t t49 = ((t47 == t48));
        if (!t49) goto ie45;
        {
            void * t51 = (void *)((uint8_t *)&F.l2);
            gm_f_deinit_padgogk(t51);
        }
        {
            gm_b0_1 t52;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        gm_f_list_m6i5ef0((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t55 = gm_l1;
            gm_l8 = t55;
        }
        {
            for (;;) {
                uint64_t t57 = gm_l9;
                uint64_t t58 = gm_l8;
                uint8_t t59 = (t57 < t58);
                if (!t59) goto lb56;
                {
                    uint64_t t62 = gm_l9;
                    uint64_t t63 = t62;
                    uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)&t63));
                    gm_l10 = t66;
                    {
                        uint64_t t68 = gm_l10;
                        uint64_t t69 = (uintptr_t)t68;
                        void * t70 = gm_l0;
                        uint64_t t71 = GM_LD(uintptr_t, ((((uint8_t *)t70) + 16) + 0) + 8);
                        if (t69 >= (uintptr_t)(t71)) gm_trap_index(t69, t71, gm_srcutmnmd444bu0, 127, 14);
                        gm_tjc2ga47o3k6g t72 = GM_LD(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t70) + 16) + 0)) + t69 * 56));
                        F.l11 = t72;
                    }
                    {
                        uint8_t t74 = ((GM_LD(uint8_t, ((uint8_t *)&F.l11) + 0) == ((uint8_t)79ULL)));
                        if (!t74) goto ie73;
                        {
                            goto lc56;
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    {
                        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                        gm_l12 = t76;
                    }
                    {
                        void * t77 = (void *)((uint8_t *)&F.l7);
                        gm_tjc2ga47o3k6g t78;
                        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        switch (t91) { case ((uint8_t)0ULL): goto ma89; case ((uint8_t)1ULL): goto ma89; case ((uint8_t)2ULL): goto ma89; case ((uint8_t)3ULL): goto ma89; case ((uint8_t)4ULL): goto ma89; case ((uint8_t)5ULL): goto ma89; case ((uint8_t)6ULL): goto ma89; case ((uint8_t)7ULL): goto ma89; case ((uint8_t)8ULL): goto ma89; case ((uint8_t)9ULL): goto ma89; case ((uint8_t)10ULL): goto ma89; case ((uint8_t)11ULL): goto ma89; case ((uint8_t)12ULL): goto ma89; case ((uint8_t)13ULL): goto ma89; case ((uint8_t)14ULL): goto ma89; case ((uint8_t)15ULL): goto ma89; case ((uint8_t)16ULL): goto ma89; case ((uint8_t)17ULL): goto ma89; case ((uint8_t)18ULL): goto ma89; case ((uint8_t)19ULL): goto ma89; case ((uint8_t)20ULL): goto ma89; case ((uint8_t)21ULL): goto ma89; case ((uint8_t)22ULL): goto ma89; case ((uint8_t)23ULL): goto ma89; case ((uint8_t)24ULL): goto ma89; case ((uint8_t)25ULL): goto ma89; case ((uint8_t)26ULL): goto ma89; case ((uint8_t)27ULL): goto ma89; case ((uint8_t)28ULL): goto ma85; case ((uint8_t)29ULL): goto ma86; case ((uint8_t)30ULL): goto ma89; case ((uint8_t)31ULL): goto ma89; case ((uint8_t)32ULL): goto ma89; case ((uint8_t)33ULL): goto ma89; case ((uint8_t)34ULL): goto ma89; case ((uint8_t)35ULL): goto ma89; case ((uint8_t)36ULL): goto ma89; case ((uint8_t)37ULL): goto ma89; case ((uint8_t)38ULL): goto ma80; case ((uint8_t)39ULL): goto ma82; case ((uint8_t)40ULL): goto ma83; case ((uint8_t)41ULL): goto ma89; case ((uint8_t)42ULL): goto ma89; case ((uint8_t)43ULL): goto ma89; case ((uint8_t)44ULL): goto ma89; case ((uint8_t)45ULL): goto ma89; case ((uint8_t)46ULL): goto ma89; case ((uint8_t)47ULL): goto ma89; case ((uint8_t)48ULL): goto ma89; case ((uint8_t)49ULL): goto ma89; case ((uint8_t)50ULL): goto ma89; case ((uint8_t)51ULL): goto ma89; case ((uint8_t)52ULL): goto ma89; case ((uint8_t)53ULL): goto ma89; case ((uint8_t)54ULL): goto ma89; case ((uint8_t)55ULL): goto ma87; case ((uint8_t)56ULL): goto ma89; case ((uint8_t)57ULL): goto ma89; case ((uint8_t)58ULL): goto ma89; case ((uint8_t)59ULL): goto ma89; case ((uint8_t)60ULL): goto ma88; case ((uint8_t)61ULL): goto ma89; case ((uint8_t)62ULL): goto ma89; case ((uint8_t)63ULL): goto ma89; case ((uint8_t)64ULL): goto ma89; case ((uint8_t)65ULL): goto ma89; case ((uint8_t)66ULL): goto ma89; case ((uint8_t)67ULL): goto ma89; case ((uint8_t)68ULL): goto ma89; case ((uint8_t)69ULL): goto ma89; case ((uint8_t)70ULL): goto ma84; case ((uint8_t)71ULL): goto ma89; case ((uint8_t)72ULL): goto ma89; case ((uint8_t)73ULL): goto ma89; case ((uint8_t)74ULL): goto ma89; case ((uint8_t)75ULL): goto ma89; case ((uint8_t)76ULL): goto ma89; case ((uint8_t)77ULL): goto ma89; case ((uint8_t)78ULL): goto ma81; case ((uint8_t)79ULL): goto ma89; case ((uint8_t)80ULL): goto ma89; case ((uint8_t)81ULL): goto ma89; case ((uint8_t)82ULL): goto ma89; case ((uint8_t)83ULL): goto ma89; case ((uint8_t)84ULL): goto ma89; default: goto ma89; }
ma80:;
                        uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t93 != ((uint8_t)38ULL)) goto mn92;
                        uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 8));
                        gm_l13 = t94;
                        uint32_t t95 = gm_l13;
                        uint64_t t96 = (uint64_t)t95;
                        uint64_t t97 = (uintptr_t)t96;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srcutmnmd444bu0, 131, 33);
                        uint32_t t99 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t97 * 4));
                        gm_tjc2ga47o3k6g t100;
                        memset(&t100, 0, sizeof t100);
                        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)38ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t100 + 8), t99);
                        t78 = t100;
                        goto me79;
mn92:;
ma81:;
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t102 != ((uint8_t)78ULL)) goto mn101;
                        uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 8));
                        gm_l14 = t103;
                        uint32_t t104 = gm_l14;
                        uint64_t t105 = (uint64_t)t104;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcutmnmd444bu0, 132, 35);
                        uint32_t t108 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t106 * 4));
                        gm_tjc2ga47o3k6g t109;
                        memset(&t109, 0, sizeof t109);
                        GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)78ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t109 + 8), t108);
                        t78 = t109;
                        goto me79;
mn101:;
ma82:;
                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t111 != ((uint8_t)39ULL)) goto mn110;
                        uint32_t t112 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 8) + 0));
                        gm_l15 = t112;
                        uint32_t t113 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 8) + 4));
                        gm_l16 = t113;
                        uint32_t t114 = gm_l15;
                        uint32_t t115 = gm_l16;
                        uint64_t t116 = (uint64_t)t115;
                        uint64_t t117 = (uintptr_t)t116;
                        uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_srcutmnmd444bu0, 133, 37);
                        uint32_t t119 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t117 * 4));
                        gm_tcc1rq4n1avb2 t120;
                        GM_ST(uint32_t, ((uint8_t *)&t120 + 0), t114);
                        GM_ST(uint32_t, ((uint8_t *)&t120 + 4), t119);
                        gm_tjc2ga47o3k6g t121;
                        memset(&t121, 0, sizeof t121);
                        GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)39ULL));
                        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t121 + 8), t120);
                        t78 = t121;
                        goto me79;
mn110:;
ma83:;
                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t123 != ((uint8_t)40ULL)) goto mn122;
                        uint32_t t124 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 8) + 0));
                        gm_l17 = t124;
                        uint32_t t125 = GM_LD(uint32_t, ((((uint8_t *)&F.l11) + 8) + 4));
                        gm_l18 = t125;
                        uint32_t t126 = gm_l17;
                        uint32_t t127 = gm_l18;
                        uint64_t t128 = (uint64_t)t127;
                        uint64_t t129 = (uintptr_t)t128;
                        uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_srcutmnmd444bu0, 134, 39);
                        uint32_t t131 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t129 * 4));
                        gm_tcc1rq4n1avb2 t132;
                        GM_ST(uint32_t, ((uint8_t *)&t132 + 0), t126);
                        GM_ST(uint32_t, ((uint8_t *)&t132 + 4), t131);
                        gm_tjc2ga47o3k6g t133;
                        memset(&t133, 0, sizeof t133);
                        GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)40ULL));
                        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t133 + 8), t132);
                        t78 = t133;
                        goto me79;
mn122:;
ma84:;
                        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t135 != ((uint8_t)70ULL)) goto mn134;
                        gm_tpkdfv1vgiqij t136 = GM_LD(gm_tpkdfv1vgiqij, (((uint8_t *)&F.l11) + 8));
                        gm_l19 = t136;
                        gm_tpkdfv1vgiqij t137;
                        memset((uint8_t *)&t137 + 20, 0, 4);
                        void * t138 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                        GM_ST(void *, (uint8_t *)&t137 + 0, t138);
                        uint32_t t139 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
                        GM_ST(uint32_t, (uint8_t *)&t137 + 8, t139);
                        uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 12));
                        GM_ST(uint32_t, (uint8_t *)&t137 + 12, t140);
                        uint32_t t141 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 16));
                        uint64_t t142 = (uint64_t)t141;
                        uint64_t t143 = (uintptr_t)t142;
                        uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_srcutmnmd444bu0, 135, 54);
                        uint32_t t145 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t143 * 4));
                        GM_ST(uint32_t, (uint8_t *)&t137 + 16, t145);
                        gm_tjc2ga47o3k6g t146;
                        GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t146 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t146 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t146 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)70ULL));
                        GM_ST(gm_tpkdfv1vgiqij, ((uint8_t *)&t146 + 8), t137);
                        t78 = t146;
                        goto me79;
mn134:;
ma85:;
                        uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t148 != ((uint8_t)28ULL)) goto mn147;
                        gm_tnkql1rjn1vbj t149 = GM_LD(gm_tnkql1rjn1vbj, (((uint8_t *)&F.l11) + 8));
                        gm_l20 = t149;
                        gm_tnkql1rjn1vbj t150;
                        memset((uint8_t *)&t150 + 2, 0, 2);
                        gm_ts6oqppjh0ba3 t151 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l20) + 0));
                        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t150 + 0, t151);
                        gm_tmn7n0n9raou5 t152 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l20) + 4));
                        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t150 + 4, t152);
                        uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 12));
                        GM_ST(uint32_t, (uint8_t *)&t150 + 12, t153);
                        uint32_t t154 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 16));
                        GM_ST(uint32_t, (uint8_t *)&t150 + 16, t154);
                        uint32_t t155 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 20));
                        uint64_t t156 = (uint64_t)t155;
                        uint64_t t157 = (uintptr_t)t156;
                        uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_srcutmnmd444bu0, 136, 57);
                        uint32_t t159 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t157 * 4));
                        GM_ST(uint32_t, (uint8_t *)&t150 + 20, t159);
                        gm_tjc2ga47o3k6g t160;
                        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t160 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t160 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t160 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)28ULL));
                        GM_ST(gm_tnkql1rjn1vbj, ((uint8_t *)&t160 + 8), t150);
                        t78 = t160;
                        goto me79;
mn147:;
ma86:;
                        uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t162 != ((uint8_t)29ULL)) goto mn161;
                        gm_tufm7evi61pnh t163 = GM_LD(gm_tufm7evi61pnh, (((uint8_t *)&F.l11) + 8));
                        gm_l21 = t163;
                        gm_tufm7evi61pnh t164;
                        memset((uint8_t *)&t164 + 9, 0, 3);
                        gm_tmn7n0n9raou5 t165 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l21) + 0));
                        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t164 + 0, t165);
                        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 8));
                        GM_ST(uint8_t, (uint8_t *)&t164 + 8, t166);
                        uint32_t t167 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 12));
                        GM_ST(uint32_t, (uint8_t *)&t164 + 12, t167);
                        uint32_t t168 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                        GM_ST(uint32_t, (uint8_t *)&t164 + 16, t168);
                        uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 20));
                        uint64_t t170 = (uint64_t)t169;
                        uint64_t t171 = (uintptr_t)t170;
                        uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_srcutmnmd444bu0, 137, 60);
                        uint32_t t173 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t171 * 4));
                        GM_ST(uint32_t, (uint8_t *)&t164 + 20, t173);
                        gm_tjc2ga47o3k6g t174;
                        GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t174 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t174 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t174 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)29ULL));
                        GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t174 + 8), t164);
                        t78 = t174;
                        goto me79;
mn161:;
ma87:;
                        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t176 != ((uint8_t)55ULL)) goto mn175;
                        gm_t37c2jujtiadn t177 = GM_LD(gm_t37c2jujtiadn, (((uint8_t *)&F.l11) + 8));
                        gm_l22 = t177;
                        uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)&gm_l22) + 0));
                        uint32_t t179 = GM_LD(uint32_t, (((uint8_t *)&gm_l22) + 4));
                        uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 8));
                        uint32_t t181 = GM_LD(uint32_t, (((uint8_t *)&gm_l22) + 12));
                        uint64_t t182 = (uint64_t)t181;
                        uint64_t t183 = (uintptr_t)t182;
                        uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t183 >= (uintptr_t)(t184)) gm_trap_index(t183, t184, gm_srcutmnmd444bu0, 138, 61);
                        uint32_t t185 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t183 * 4));
                        gm_t37c2jujtiadn t186;
                        GM_ST(uint8_t, (uint8_t *)&t186 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t186 + 10, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t186 + 0), t178);
                        GM_ST(uint32_t, ((uint8_t *)&t186 + 4), t179);
                        GM_ST(uint8_t, ((uint8_t *)&t186 + 8), t180);
                        GM_ST(uint32_t, ((uint8_t *)&t186 + 12), t185);
                        gm_tjc2ga47o3k6g t187;
                        memset(&t187, 0, sizeof t187);
                        GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)55ULL));
                        GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t187 + 8), t186);
                        t78 = t187;
                        goto me79;
mn175:;
ma88:;
                        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        if (t189 != ((uint8_t)60ULL)) goto mn188;
                        gm_tjqgc9rpvqnkt t190 = GM_LD(gm_tjqgc9rpvqnkt, (((uint8_t *)&F.l11) + 8));
                        gm_l23 = t190;
                        gm_tjqgc9rpvqnkt t191;
                        memset((uint8_t *)&t191 + 20, 0, 4);
                        uint32_t t192 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t191 + 0, t192);
                        uint32_t t193 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 4));
                        GM_ST(uint32_t, (uint8_t *)&t191 + 4, t193);
                        uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                        GM_ST(uint64_t, (uint8_t *)&t191 + 8, t194);
                        uint32_t t195 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
                        uint64_t t196 = (uint64_t)t195;
                        uint64_t t197 = (uintptr_t)t196;
                        uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_srcutmnmd444bu0, 139, 53);
                        uint32_t t199 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t197 * 4));
                        GM_ST(uint32_t, (uint8_t *)&t191 + 16, t199);
                        gm_tjc2ga47o3k6g t200;
                        GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t200 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t200 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t200 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)60ULL));
                        GM_ST(gm_tjqgc9rpvqnkt, ((uint8_t *)&t200 + 8), t191);
                        t78 = t200;
                        goto me79;
mn188:;
ma89:;
                        gm_tjc2ga47o3k6g t202 = F.l11;
                        t78 = t202;
                        goto me79;
mn201:;
ma90:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 130, 18);
me79:;
                        gm_tftb1odc739t5 t203 = gm_f_push_7g6eqp6(t77, &t78);
                        gm_tftb1odc739t5 t204 = t203;
                        if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_srcutmnmd444bu0, 130, 9);
                        gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                    }
                    goto me64;
mn65:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 126, 5);
me64:;
                }
lc56:;
                {
                    uint64_t t207 = gm_l9;
                    uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                    uint64_t t210 = r208;
                    gm_l9 = t210;
                }
            }
lb56:;
        }
    }
    {
        {
            gm_l25 = ((uint64_t)0ULL);
        }
        {
            void * t212 = gm_l0;
            uint64_t t213 = GM_LD(uintptr_t, ((((uint8_t *)t212) + 56) + 0) + 8);
            gm_l24 = t213;
        }
        {
            for (;;) {
                uint64_t t215 = gm_l25;
                uint64_t t216 = gm_l24;
                uint8_t t217 = (t215 < t216);
                if (!t217) goto lb214;
                {
                    uint64_t t220 = gm_l25;
                    uint64_t t221 = t220;
                    uint64_t t224 = GM_LD(uint64_t, ((uint8_t *)&t221));
                    gm_l26 = t224;
                    {
                        uint64_t t226 = gm_l26;
                        uint64_t t227 = (uintptr_t)t226;
                        void * t228 = gm_l0;
                        uint64_t t229 = GM_LD(uintptr_t, ((((uint8_t *)t228) + 56) + 0) + 8);
                        if (t227 >= (uintptr_t)(t229)) gm_trap_index(t227, t229, gm_srcutmnmd444bu0, 144, 16);
                        void * t230 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t228) + 56) + 0)) + t227 * 40);
                        gm_l27 = t230;
                    }
                    {
                        void * t231 = gm_l27;
                        void * t232 = gm_l27;
                        uint32_t t233 = GM_LD(uint32_t, (((uint8_t *)t232) + 4));
                        uint64_t t234 = (uint64_t)t233;
                        uint64_t t235 = (uintptr_t)t234;
                        uint64_t t236 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t235 >= (uintptr_t)(t236)) gm_trap_index(t235, t236, gm_srcutmnmd444bu0, 145, 18);
                        uint32_t t237 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t235 * 4));
                        GM_ST(uint32_t, (((uint8_t *)t231) + 4), t237);
                    }
                    {
                        void * t238 = gm_l27;
                        void * t239 = gm_l27;
                        uint32_t t240 = GM_LD(uint32_t, (((uint8_t *)t239) + 8));
                        uint64_t t241 = (uint64_t)t240;
                        uint64_t t242 = (uintptr_t)t241;
                        uint64_t t243 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
                        if (t242 >= (uintptr_t)(t243)) gm_trap_index(t242, t243, gm_srcutmnmd444bu0, 146, 19);
                        uint32_t t244 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t242 * 4));
                        GM_ST(uint32_t, (((uint8_t *)t238) + 8), t244);
                    }
                    goto me222;
mn223:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 143, 5);
me222:;
                }
lc214:;
                {
                    uint64_t t246 = gm_l25;
                    uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                    uint64_t t249 = r247;
                    gm_l25 = t249;
                }
            }
lb214:;
        }
    }
    {
        void * t250 = gm_l0;
        void * t251 = (void *)(((uint8_t *)t250) + 16);
        gm_f_deinit_tbneotd(t251);
    }
    {
        void * t252 = gm_l0;
        gm_trqfvmjtt4sij t253 = F.l7;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t252) + 16), t253);
    }
    {
        void * t254 = (void *)((uint8_t *)&F.l2);
        gm_f_deinit_padgogk(t254);
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
__attribute__((aligned(4))) uint8_t gm_f_vc_compiles_au5pqeh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_compiles_au5pqeh F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_t083kd7b1kkag gm_l4; /* x */
    void * gm_l5; /* x */
    void * gm_l6; /* lhs */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma4; case ((uint8_t)22ULL): goto ma4; case ((uint8_t)23ULL): goto ma4; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma4; case ((uint8_t)26ULL): goto ma4; case ((uint8_t)27ULL): goto ma4; case ((uint8_t)28ULL): goto ma4; case ((uint8_t)29ULL): goto ma4; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma4; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma4; case ((uint8_t)36ULL): goto ma4; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; case ((uint8_t)59ULL): goto ma9; case ((uint8_t)60ULL): goto ma9; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma9; default: goto ma9; }
ma4:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t15 != ((uint8_t)0ULL)) goto pa14;
    goto po13;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t17 != ((uint8_t)6ULL)) goto pa16;
    goto po13;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t19 != ((uint8_t)21ULL)) goto pa18;
    goto po13;
pa18:;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t21 != ((uint8_t)23ULL)) goto pa20;
    goto po13;
pa20:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t23 != ((uint8_t)24ULL)) goto pa22;
    goto po13;
pa22:;
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t25 != ((uint8_t)25ULL)) goto pa24;
    goto po13;
pa24:;
    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t27 != ((uint8_t)32ULL)) goto pa26;
    goto po13;
pa26:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t29 != ((uint8_t)26ULL)) goto pa28;
    goto po13;
pa28:;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t31 != ((uint8_t)27ULL)) goto pa30;
    goto po13;
pa30:;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t33 != ((uint8_t)28ULL)) goto pa32;
    goto po13;
pa32:;
    uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t35 != ((uint8_t)29ULL)) goto pa34;
    goto po13;
pa34:;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t37 != ((uint8_t)22ULL)) goto pa36;
    goto po13;
pa36:;
    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t39 != ((uint8_t)35ULL)) goto pa38;
    goto po13;
pa38:;
    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t41 != ((uint8_t)36ULL)) goto pa40;
    goto po13;
pa40:;
    goto mn12;
po13:;
    t2 = ((uint8_t)1);
    goto me3;
mn12:;
ma5:;
    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t43 != ((uint8_t)34ULL)) goto mn42;
    gm_tvdg94bk7aa22 t44 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l2 = t44;
    gm_b16_8 t45 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l3 = t45;
    uint8_t t46 = 0;
    gm_tvdg94bk7aa22 t47 = gm_l2;
    gm_b16_8 t48 = gm_l3;
    uint8_t t49 = gm_f_vc_builtin_ok_kbscem0(t47, t48);
    if (!t49) {
        gm_tvdg94bk7aa22 t50 = gm_l2;
        gm_b16_8 t51 = gm_l3;
        uint8_t t52 = gm_f_vc_assert_ok_6n0rc7j(t50, t51);
        t46 = t52;
    } else t46 = 1;
    t2 = t46;
    goto me3;
mn42:;
ma6:;
    uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t54 != ((uint8_t)47ULL)) goto mn53;
    gm_t083kd7b1kkag t55 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t1) + 0) + 8));
    gm_l4 = t55;
    void * t56 = gm_l0;
    void * t57 = gm_l1;
    void * t58 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
    gm_t9805qtonbsag t59 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l4) + 8));
    uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 16));
    uint8_t t61 = gm_f_vc_try_ok_ng7fba1((void *)NULL, cx, NULL, t56, t57, t58, t59, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t61;
    goto me3;
mn53:;
ma7:;
    uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t63 != ((uint8_t)45ULL)) goto mn62;
    void * t64 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l5 = t64;
    void * t65 = gm_l0;
    void * t66 = gm_l1;
    void * t67 = gm_l5;
    uint8_t t68 = gm_f_vc_unwrap_ok_tb3qnv4((void *)NULL, cx, NULL, t65, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t68;
    goto me3;
mn62:;
ma8:;
    uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t70 != ((uint8_t)46ULL)) goto mn69;
    void * t71 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l6 = t71;
    void * t72 = gm_l0;
    void * t73 = gm_l1;
    void * t74 = gm_l6;
    gm_t0hcjal40h40i t75;
    gm_f_vc_else_shape_n9vc3pg((void *)&t75, (void *)NULL, cx, NULL, t72, t73, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0hcjal40h40i t76 = t75;
    uint8_t t77;
    if (((((uint8_t *)&t76))[32] != 0)) goto mn79;
    t77 = ((uint8_t)0);
    goto me78;
mn79:;
    t77 = ((uint8_t)1);
    goto me78;
mn80:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 504, 25);
me78:;
    t2 = t77;
    goto me3;
mn69:;
ma9:;
    t2 = ((uint8_t)0);
    goto me3;
mn81:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 499, 66);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_vc_conds_a1m6u4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_vc_conds_a1m6u4v F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* parts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* p */
    uint32_t gm_l6; /* save */
    void * gm_l7; /* e */
    void * gm_l8; /* pat */
    void * gm_l9; /* value */
    uint32_t gm_l10; /* at */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_go2qg0b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        void * t17 = gm_l0;
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 224));
                        gm_l6 = t18;
                    }
                    {
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t21 != ((uint8_t)0ULL)) goto mn20;
                        void * t22 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l7 = t22;
                        void * t23 = gm_l0;
                        void * t24 = gm_l7;
                        void * t25 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t26 = gm_f_vc_branch_r6k4hjp((void *)NULL, cx, NULL, t23, t24, ((uint8_t)0), t25);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me19;
mn20:;
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t28 != ((uint8_t)1ULL)) goto mn27;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l8 = t29;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l9 = t30;
                        {
                            void * t32 = gm_l0;
                            void * t33 = gm_l0;
                            void * t34 = gm_l9;
                            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 104));
                            uint64_t t36 = gm_f_vc_alloc_slot_5eptdbb(t33, t35);
                            uint32_t t37 = gm_f_vc_slot_at_rc6jhbj(t32, t36);
                            gm_l10 = t37;
                        }
                        {
                            void * t38 = gm_l0;
                            void * t39 = gm_l9;
                            uint32_t t40 = gm_l10;
                            gm_b0_1 t41 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            void * t42 = gm_l0;
                            void * t43 = gm_l8;
                            uint32_t t44 = gm_l10;
                            void * t45 = gm_l9;
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 104));
                            void * t47 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t48 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t42, t43, t44, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me19;
mn27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1578, 9);
me19:;
                    }
                    {
                        void * t49 = gm_l0;
                        uint32_t t50 = gm_l6;
                        GM_ST(uint32_t, (((uint8_t *)t49) + 224), t50);
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1576, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t52 = gm_l4;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l4 = t55;
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
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_vc_copies_glmdmll(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_copies_glmdmll F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t7 != ((uint8_t)6ULL)) goto pa6;
    goto po5;
pa6:;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t9 != ((uint8_t)10ULL)) goto pa8;
    goto po5;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t11 != ((uint8_t)9ULL)) goto pa10;
    goto po5;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t13 != ((uint8_t)12ULL)) goto pa12;
    goto po5;
pa12:;
    goto mn4;
po5:;
    void * t14 = gm_l0;
    void * t15 = gm_l1;
    gm_t9qkh0rsooc8d t16 = gm_f_vc_placeable_ip26ve2(t14, t15);
    gm_t9qkh0rsooc8d t17 = t16;
    uint8_t t18;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
    switch (t24) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma21; default: goto ma23; }
ma20:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
    if (t26 != ((uint8_t)0ULL)) goto mn25;
    t18 = ((uint8_t)0);
    goto me19;
mn25:;
ma21:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
    if (t28 != ((uint8_t)2ULL)) goto mn27;
    t18 = ((uint8_t)1);
    goto me19;
mn27:;
ma22:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
    if (t30 != ((uint8_t)1ULL)) goto mn29;
    uint8_t t31 = 0;
    void * t32 = gm_l1;
    uint32_t t33 = gm_f_root_levels_umkq7vp(t32);
    uint8_t t34 = ((t33 == ((uint32_t)0ULL)));
    if (t34) {
        void * t35 = gm_l0;
        void * t36 = gm_l1;
        uint8_t t37 = gm_f_vc_has_index_b75va8s(t35, t36);
        uint8_t t38 = (uint8_t)!t37;
        t31 = t38;
    } else t31 = 0;
    t18 = t31;
    goto me19;
mn29:;
ma23:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 533, 51);
me19:;
    t2 = t18;
    goto me3;
mn4:;
    t2 = ((uint8_t)0);
    goto me3;
mn39:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 532, 58);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_copy_place_8dhmkji(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_vc_copy_place_8dhmkji F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* at */
    uint32_t gm_l3; /* save */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* rel */
    uint32_t gm_l6; /* r */
    uint64_t gm_l7; /* off */
    uint8_t gm_l8; /* packed */
    GM_STACK_CHECK(&F, 545u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l1;
        gm_tiutk3b96cnlp t6;
        gm_f_vc_place_i1sksai((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tiutk3b96cnlp t7 = t6;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 4));
        gm_l4 = t11;
        uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)&t7) + 8) + 8));
        gm_l5 = t12;
        void * t13 = gm_l0;
        uint32_t t14 = gm_l2;
        uint32_t t15 = gm_l4;
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 72) + 8);
        if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srcutmnmd444bu0, 545, 61);
        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 72)) + t17 * 8));
        uint64_t t22 = gm_l5;
        uint64_t r23; bool o24 = __builtin_add_overflow(t21, t22, &r23);
        if (o24) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 545, 61);
        uint64_t t25 = r23;
        uint32_t t26 = (uint32_t)t25;
        void * t27 = gm_l1;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 104));
        uint32_t t29 = (uint32_t)t28;
        gm_tl874t2atnlh3 t30;
        GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t14);
        GM_ST(uint32_t, ((uint8_t *)&t30 + 4), t26);
        GM_ST(uint32_t, ((uint8_t *)&t30 + 8), t29);
        gm_tjc2ga47o3k6g t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)52ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t31 + 8), t30);
        gm_b0_1 t32 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t13, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn9:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t34 != ((uint8_t)1ULL)) goto mn33;
        uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l6 = t35;
        uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t7) + 8) + 8));
        gm_l7 = t36;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&t7) + 8) + 16));
        gm_l8 = t37;
        void * t38 = gm_l0;
        gm_t04llc5uen0qo t39;
        memset((uint8_t *)&t39 + 20, 0, 4);
        uint32_t t40 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t39 + 0, t40);
        uint32_t t41 = gm_l6;
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, t41);
        uint64_t t42 = gm_l7;
        uint32_t t43 = (uint32_t)t42;
        GM_ST(uint32_t, (uint8_t *)&t39 + 8, t43);
        void * t44 = gm_l1;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 104));
        uint32_t t46 = (uint32_t)t45;
        GM_ST(uint32_t, (uint8_t *)&t39 + 12, t46);
        uint32_t t47;
        uint8_t t49 = gm_l8;
        if (!t49) goto ie48;
        uint32_t t51 = ((uint32_t)1ULL);
        t47 = t51;
        goto ix48;
ie48:;
        void * t53 = gm_l0;
        void * t54 = gm_l1;
        uint32_t t55 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t56 = t55;
        t47 = t56;
ix48:;
        GM_ST(uint32_t, (uint8_t *)&t39 + 16, t47);
        void * t57 = gm_l1;
        GM_ST(void *, (uint8_t *)&t39 + 24, t57);
        gm_tjc2ga47o3k6g t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t58 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)41ULL));
        GM_ST(gm_t04llc5uen0qo, ((uint8_t *)&t58 + 8), t39);
        gm_b0_1 t59 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t38, &t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me8;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 544, 5);
me8:;
    }
    {
        void * t60 = gm_l0;
        uint32_t t61 = gm_l3;
        GM_ST(uint32_t, (((uint8_t *)t60) + 224), t61);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_defer_block_bgamoqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2) {
    struct gm_fr_vc_defer_block_bgamoqf F;
    void * gm_l0; /* c */
    void * gm_l1; /* b */
    gm_tmn7n0n9raou5 gm_l2; /* want */
    uint32_t gm_l3; /* save */
    uint32_t gm_l4; /* res */
    uint32_t gm_l5; /* h */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l9; /* t */
    void * gm_l10; /* t */
    uint32_t gm_l11; /* v */
    void * gm_l12; /* t */
    GM_STACK_CHECK(&F, 960u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l3 = t3;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t5) goto ie4;
        {
            void * t7 = gm_l0;
            uint32_t t8 = gm_f_vc_alloc_3hmfvtt(t7);
            gm_l4 = t8;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_vc_alloc_3hmfvtt(t9);
        gm_l5 = t10;
    }
    {
        void * t11 = gm_l0;
        uint32_t t12 = gm_l5;
        gm_tjc2ga47o3k6g t13;
        memset(&t13, 0, sizeof t13);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)66ULL));
        GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t12);
        gm_b0_1 t14 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t11, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t16) + 136));
        uint32_t r18; bool o19 = __builtin_add_overflow(t17, ((uint32_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1006, 5);
        uint32_t t20 = r18;
        GM_ST(uint32_t, (((uint8_t *)t15) + 136), t20);
    }
    {
        {
            void * t22 = gm_l1;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
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
                    gm_t2k9dbsv5ff58 t35 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t31 * 40));
                    F.l8 = t35;
                    {
                        void * t37 = gm_l0;
                        gm_b0_1 t38 = gm_f_vc_stmt_01mcb8k((void *)NULL, cx, NULL, t37, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1007, 5);
me33:;
                }
lc24:;
                {
                    uint64_t t40 = gm_l7;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l7 = t43;
                }
            }
gm_ll28:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
    }
    {
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t49) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma46; case ((uint8_t)2ULL): goto ma45; case ((uint8_t)3ULL): goto ma46; default: goto ma48; }
ma45:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t51 != ((uint8_t)2ULL)) goto mn50;
        {
            void * t54 = gm_l1;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 16));
            void * t56 = t55;
            if (!(GM_LD(void *, ((uint8_t *)&t56)) != 0)) goto ie53;
            void * t57 = GM_LD(void *, ((uint8_t *)&t56));
            gm_l9 = t57;
            {
                void * t59 = gm_l0;
                void * t60 = gm_l9;
                gm_tmn7n0n9raou5 t61;
                GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
                gm_b0_1 t62 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t59, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix53;
ie53:;
            {
                void * t64 = gm_l0;
                gm_tjc2ga47o3k6g t65;
                memset(&t65, 0, sizeof t65);
                GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)75ULL));
                gm_b0_1 t66 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t64, &t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix53:;
        }
        goto me44;
mn50:;
ma46:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t70 != ((uint8_t)1ULL)) goto pa69;
        goto po68;
pa69:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t72 != ((uint8_t)3ULL)) goto pa71;
        goto po68;
pa71:;
        goto mn67;
po68:;
        {
            void * t75 = gm_l1;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 16));
            void * t77 = t76;
            if (!(GM_LD(void *, ((uint8_t *)&t77)) != 0)) goto ie74;
            void * t78 = GM_LD(void *, ((uint8_t *)&t77));
            gm_l10 = t78;
            {
                void * t80 = gm_l0;
                void * t81 = gm_l10;
                gm_tmn7n0n9raou5 t82 = gm_l2;
                gm_b0_1 t83 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t80, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            void * t84 = gm_l0;
            uint32_t t85 = gm_l5;
            gm_tjc2ga47o3k6g t86;
            memset(&t86, 0, sizeof t86);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)68ULL));
            GM_ST(uint32_t, ((uint8_t *)&t86 + 8), t85);
            gm_b0_1 t87 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t84, &t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me44;
mn67:;
ma47:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t89 != ((uint8_t)0ULL)) goto mn88;
        {
            void * t91 = gm_l0;
            uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)t91) + 224));
            gm_l11 = t92;
        }
        {
            void * t94 = gm_l1;
            void * t95 = GM_LD(void *, (((uint8_t *)t94) + 16));
            void * t96 = t95;
            if (!(GM_LD(void *, ((uint8_t *)&t96)) != 0)) goto ie93;
            void * t97 = GM_LD(void *, ((uint8_t *)&t96));
            gm_l12 = t97;
            {
                void * t99 = gm_l0;
                void * t100 = gm_l12;
                gm_tmn7n0n9raou5 t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)0ULL));
                gm_b0_1 t102 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t99, t100, t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix93;
ie93:;
            {
                void * t104 = gm_l0;
                void * t105 = gm_l0;
                uint32_t t106 = gm_f_vc_alloc_3hmfvtt(t105);
                gm_tb31k5l794t7p t107;
                GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t107 + 0), t106);
                GM_ST(uint64_t, ((uint8_t *)&t107 + 8), ((uint64_t)0ULL));
                gm_tjc2ga47o3k6g t108;
                memset(&t108, 0, sizeof t108);
                GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t108 + 8), t107);
                gm_b0_1 t109 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t104, &t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix93:;
        }
        {
            void * t110 = gm_l0;
            uint32_t t111 = gm_l4;
            uint32_t t112 = gm_l11;
            gm_tcc1rq4n1avb2 t113;
            GM_ST(uint32_t, ((uint8_t *)&t113 + 0), t111);
            GM_ST(uint32_t, ((uint8_t *)&t113 + 4), t112);
            gm_tjc2ga47o3k6g t114;
            memset(&t114, 0, sizeof t114);
            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)65ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t114 + 8), t113);
            gm_b0_1 t115 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t110, &t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t116 = gm_l0;
            uint32_t t117 = gm_l5;
            gm_tjc2ga47o3k6g t118;
            memset(&t118, 0, sizeof t118);
            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)68ULL));
            GM_ST(uint32_t, ((uint8_t *)&t118 + 8), t117);
            gm_b0_1 t119 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t116, &t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me44;
mn88:;
ma48:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1008, 5);
me44:;
    }
    {
        void * t120 = gm_l0;
        void * t121 = gm_l0;
        uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)t121) + 136));
        uint32_t r123; bool o124 = __builtin_sub_overflow(t122, ((uint32_t)1ULL), &r123);
        if (o124) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 1023, 5);
        uint32_t t125 = r123;
        GM_ST(uint32_t, (((uint8_t *)t120) + 136), t125);
    }
    {
        void * t126 = gm_l0;
        uint32_t t127;
        uint8_t t129 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t129) goto ie128;
        uint32_t t131 = gm_l3;
        uint32_t r132; bool o133 = __builtin_add_overflow(t131, ((uint32_t)1ULL), &r132);
        if (o133) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1024, 36);
        uint32_t t134 = r132;
        uint32_t t135 = t134;
        t127 = t135;
        goto ix128;
ie128:;
        uint32_t t137 = gm_l3;
        uint32_t t138 = t137;
        t127 = t138;
ix128:;
        gm_f_vc_set_top_h0qmvd1(t126, t127);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_else_9ml9jkf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, void * a4, gm_t9805qtonbsag a5) {
    struct gm_fr_vc_else_9ml9jkf F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* lhs */
    gm_t9805qtonbsag gm_l3; /* capture */
    void * gm_l4; /* rhs */
    gm_t9805qtonbsag gm_l5; /* at */
    gm_tubi2bvif8j7u gm_l6; /* shape */
    uint32_t gm_l7; /* save */
    uint32_t gm_l8; /* la */
    uint32_t gm_l9; /* fail */
    uint32_t gm_l10; /* pa */
    uint32_t gm_l11; /* end */
    uint32_t gm_l12; /* l */
    gm_t88rcucg1b7uk gm_l13; /* err */
    uint32_t gm_l14; /* d */
    GM_STACK_CHECK(&F, 940u);
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
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t0hcjal40h40i t5;
        gm_f_vc_else_shape_n9vc3pg((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0hcjal40h40i t6 = t5;
        gm_tubi2bvif8j7u t7;
        if (((((uint8_t *)&t6))[32] != 0)) {
            gm_tubi2bvif8j7u t8 = GM_LD(gm_tubi2bvif8j7u, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l6 = t7;
    }
    {
        void * t11 = gm_l0;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t11) + 224));
        gm_l7 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l0;
        void * t15 = gm_l2;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 104));
        uint64_t t17 = gm_f_vc_alloc_slot_5eptdbb(t14, t16);
        uint32_t t18 = gm_f_vc_slot_at_rc6jhbj(t13, t17);
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l2;
        uint32_t t21 = gm_l8;
        gm_b0_1 t22 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t23 = gm_l0;
        uint32_t t24 = gm_l8;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
        uint32_t t26 = (uint32_t)t25;
        uint32_t r27; bool o28 = __builtin_add_overflow(t24, t26, &r27);
        if (o28) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1385, 30);
        uint32_t t29 = r27;
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint32_t t31 = (uint32_t)t30;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 16));
        gm_t37c2jujtiadn t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
        GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t29);
        GM_ST(uint32_t, ((uint8_t *)&t33 + 4), t31);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 8), t32);
        GM_ST(uint32_t, ((uint8_t *)&t33 + 12), ((uint32_t)0ULL));
        gm_tjc2ga47o3k6g t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)55ULL));
        GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t34 + 8), t33);
        uint32_t t35 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t23, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t35;
    }
    {
        uint32_t t36 = gm_l8;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 24));
        uint32_t t38 = (uint32_t)t37;
        uint32_t r39; bool o40 = __builtin_add_overflow(t36, t38, &r39);
        if (o40) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1386, 11);
        uint32_t t41 = r39;
        gm_l10 = t41;
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l1;
        uint32_t t44 = gm_l10;
        gm_t9805qtonbsag t45 = gm_l5;
        gm_b0_1 t46 = gm_f_vc_payload_loquhre((void *)NULL, cx, NULL, t42, t43, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t47 = gm_l0;
        gm_tjc2ga47o3k6g t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)38ULL));
        GM_ST(uint32_t, ((uint8_t *)&t48 + 8), ((uint32_t)0ULL));
        uint32_t t49 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t47, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t49;
    }
    {
        void * t50 = gm_l0;
        uint32_t t51 = gm_l9;
        void * t52 = gm_l0;
        uint32_t t53 = gm_f_vc_here_cc4mbj3(t52);
        gm_f_vc_patch_pbmmb3q(t50, t51, t53);
    }
    {
        void * t54 = gm_l0;
        uint32_t t55 = gm_l7;
        GM_ST(uint32_t, (((uint8_t *)t54) + 224), t55);
    }
    {
        if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie56;
        uint32_t t57 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l12 = t57;
        {
            void * t60 = gm_l0;
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
            void * t62 = gm_l2;
            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t62) + 96));
            gm_t53at80pfdus3 t64;
            gm_f_kind_htv6093((void *)&t64, t61, GM_CONV(gm_csguankko4lmhd, t63));
            gm_t53at80pfdus3 t65 = t64;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
            if (t66 != ((uint8_t)14ULL)) goto ie59;
            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t65) + 8) + 0));
            gm_l13 = t67;
            {
                void * t69 = gm_l0;
                uint32_t t70 = gm_l12;
                uint32_t t71 = gm_l10;
                void * t72 = gm_l0;
                void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                gm_t88rcucg1b7uk t74 = gm_l13;
                uint64_t t75 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t76 = (uint32_t)t75;
                void * t77 = gm_l1;
                gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 112));
                gm_b0_1 t79 = gm_f_vc_set_local_slot_l28kec5((void *)NULL, cx, NULL, t69, t70, t71, t76, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix59;
ie59:;
ix59:;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        if (!((((uint8_t *)&gm_l5))[4] != 0)) goto ie80;
        uint32_t t81 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
        gm_l14 = t81;
        {
            void * t83 = gm_l0;
            void * t84 = gm_l4;
            uint32_t t85 = gm_l14;
            gm_b0_1 t86 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t83, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix80;
ie80:;
        {
            void * t88 = gm_l0;
            void * t89 = gm_l4;
            gm_b0_1 t90 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix80:;
    }
    {
        void * t91 = gm_l0;
        uint32_t t92 = gm_l11;
        void * t93 = gm_l0;
        uint32_t t94 = gm_f_vc_here_cc4mbj3(t93);
        gm_f_vc_patch_pbmmb3q(t91, t92, t94);
    }
    {
        void * t95 = gm_l0;
        uint32_t t96;
        uint8_t t98;
        if (((((uint8_t *)&gm_l5))[4] != 0)) goto mn100;
        t98 = ((uint8_t)1);
        goto me99;
mn100:;
        t98 = ((uint8_t)0);
        goto me99;
mn101:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1396, 21);
me99:;
        if (!t98) goto ie97;
        uint32_t t103 = gm_l7;
        uint32_t r104; bool o105 = __builtin_add_overflow(t103, ((uint32_t)1ULL), &r104);
        if (o105) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1396, 34);
        uint32_t t106 = r104;
        uint32_t t107 = t106;
        t96 = t107;
        goto ix97;
ie97:;
        uint32_t t109 = gm_l7;
        uint32_t t110 = t109;
        t96 = t110;
ix97:;
        gm_f_vc_set_top_h0qmvd1(t95, t96);
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
__attribute__((aligned(4))) void gm_f_vc_else_shape_n9vc3pg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vc_else_shape_n9vc3pg F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* lhs */
    void * gm_l3; /* m */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_b16_8 gm_l5; /* offs */
    gm_t88rcucg1b7uk gm_l6; /* ok */
    gm_b16_8 gm_l7; /* offs */
    GM_STACK_CHECK(&F, 828u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l3;
        void * t5 = gm_l2;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 96));
        gm_t53at80pfdus3 t7;
        gm_f_kind_htv6093((void *)&t7, t4, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma10:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t16 != ((uint8_t)13ULL)) goto mn15;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
        gm_l4 = t17;
        {
            void * t20 = gm_l1;
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 96));
            gm_t88rcucg1b7uk t22 = gm_l4;
            gm_t88rcucg1b7uk t23 = t21;
            gm_t88rcucg1b7uk t24 = t22;
            uint8_t t25 = (!gm_eq9m6mvtmgde(((uint8_t *)&t23), ((uint8_t *)&t24)));
            if (!t25) goto ie19;
            {
                gm_t0hcjal40h40i t27;
                memset(&t27, 0, sizeof t27);
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            void * t30 = gm_l3;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 8));
            gm_t88rcucg1b7uk t32 = gm_l4;
            uint8_t t33 = gm_f_nullable_pointer_7qfmn6o(t31, GM_CONV(gm_csguankko4lmhd, t32));
            if (!t33) goto ie29;
            {
                gm_tubi2bvif8j7u t35;
                memset((uint8_t *)&t35 + 17, 0, 7);
                GM_ST(uint64_t, (uint8_t *)&t35 + 0, ((uint64_t)0ULL));
                void * t36 = gm_l3;
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 176));
                GM_ST(uint64_t, (uint8_t *)&t35 + 8, t37);
                GM_ST(uint8_t, (uint8_t *)&t35 + 16, ((uint8_t)0));
                GM_ST(uint64_t, (uint8_t *)&t35 + 24, ((uint64_t)0ULL));
                gm_t0hcjal40h40i t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 36, 0);
                GM_ST(gm_tubi2bvif8j7u, ((uint8_t *)&t38), t35);
                ((uint8_t *)&t38)[32] = 1;
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            void * t40 = gm_l3;
            void * t41 = gm_l2;
            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 96));
            gm_b16_8 t43 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t40, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t43;
        }
        {
            gm_tubi2bvif8j7u t44;
            memset((uint8_t *)&t44 + 17, 0, 7);
            uint64_t t45 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcutmnmd444bu0, 1367, 27);
            uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t45 * 8));
            GM_ST(uint64_t, (uint8_t *)&t44 + 0, t47);
            GM_ST(uint64_t, (uint8_t *)&t44 + 8, ((uint64_t)1ULL));
            GM_ST(uint8_t, (uint8_t *)&t44 + 16, ((uint8_t)0));
            uint64_t t48 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcutmnmd444bu0, 1367, 70);
            uint64_t t50 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t48 * 8));
            GM_ST(uint64_t, (uint8_t *)&t44 + 24, t50);
            gm_t0hcjal40h40i t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 36, 0);
            GM_ST(gm_tubi2bvif8j7u, ((uint8_t *)&t51), t44);
            ((uint8_t *)&t51)[32] = 1;
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto me9;
mn15:;
ma11:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t54 != ((uint8_t)14ULL)) goto mn53;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l6 = t55;
        {
            void * t58 = gm_l1;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + 96));
            gm_t88rcucg1b7uk t60 = gm_l6;
            gm_t88rcucg1b7uk t61 = t59;
            gm_t88rcucg1b7uk t62 = t60;
            uint8_t t63 = (!gm_eq9m6mvtmgde(((uint8_t *)&t61), ((uint8_t *)&t62)));
            if (!t63) goto ie57;
            {
                gm_t0hcjal40h40i t65;
                memset(&t65, 0, sizeof t65);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t67 = gm_l3;
            void * t68 = gm_l2;
            gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 96));
            gm_b16_8 t70 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t67, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t70;
        }
        {
            gm_tubi2bvif8j7u t71;
            memset((uint8_t *)&t71 + 17, 0, 7);
            uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcutmnmd444bu0, 1372, 27);
            uint64_t t74 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t72 * 8));
            GM_ST(uint64_t, (uint8_t *)&t71 + 0, t74);
            GM_ST(uint64_t, (uint8_t *)&t71 + 8, ((uint64_t)1ULL));
            GM_ST(uint8_t, (uint8_t *)&t71 + 16, ((uint8_t)1));
            uint64_t t75 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            if (t75 >= (uintptr_t)(t76)) gm_trap_index(t75, t76, gm_srcutmnmd444bu0, 1372, 69);
            uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t75 * 8));
            GM_ST(uint64_t, (uint8_t *)&t71 + 24, t77);
            gm_t0hcjal40h40i t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 36, 0);
            GM_ST(gm_tubi2bvif8j7u, ((uint8_t *)&t78), t71);
            ((uint8_t *)&t78)[32] = 1;
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto me9;
mn53:;
ma12:;
        gm_t0hcjal40h40i t81;
        memset(&t81, 0, sizeof t81);
        uint8_t t82 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me9;
mn80:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1362, 5);
me9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_emit_phs805s(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_vc_emit_phs805s F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma5; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma5; case ((uint8_t)31ULL): goto ma5; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma5; case ((uint8_t)34ULL): goto ma5; case ((uint8_t)35ULL): goto ma5; case ((uint8_t)36ULL): goto ma5; case ((uint8_t)37ULL): goto ma5; case ((uint8_t)38ULL): goto ma5; case ((uint8_t)39ULL): goto ma5; case ((uint8_t)40ULL): goto ma5; case ((uint8_t)41ULL): goto ma5; case ((uint8_t)42ULL): goto ma5; case ((uint8_t)43ULL): goto ma5; case ((uint8_t)44ULL): goto ma5; case ((uint8_t)45ULL): goto ma5; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma5; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma5; case ((uint8_t)50ULL): goto ma5; case ((uint8_t)51ULL): goto ma5; case ((uint8_t)52ULL): goto ma5; case ((uint8_t)53ULL): goto ma5; case ((uint8_t)54ULL): goto ma5; case ((uint8_t)55ULL): goto ma5; case ((uint8_t)56ULL): goto ma5; case ((uint8_t)57ULL): goto ma5; case ((uint8_t)58ULL): goto ma5; case ((uint8_t)59ULL): goto ma5; case ((uint8_t)60ULL): goto ma5; case ((uint8_t)61ULL): goto ma3; case ((uint8_t)62ULL): goto ma3; case ((uint8_t)63ULL): goto ma3; case ((uint8_t)64ULL): goto ma5; case ((uint8_t)65ULL): goto ma5; case ((uint8_t)66ULL): goto ma5; case ((uint8_t)67ULL): goto ma4; case ((uint8_t)68ULL): goto ma5; case ((uint8_t)69ULL): goto ma3; case ((uint8_t)70ULL): goto ma3; case ((uint8_t)71ULL): goto ma5; case ((uint8_t)72ULL): goto ma5; case ((uint8_t)73ULL): goto ma5; case ((uint8_t)74ULL): goto ma5; case ((uint8_t)75ULL): goto ma5; case ((uint8_t)76ULL): goto ma5; case ((uint8_t)77ULL): goto ma5; case ((uint8_t)78ULL): goto ma5; case ((uint8_t)79ULL): goto ma5; case ((uint8_t)80ULL): goto ma5; case ((uint8_t)81ULL): goto ma5; case ((uint8_t)82ULL): goto ma5; case ((uint8_t)83ULL): goto ma5; case ((uint8_t)84ULL): goto ma5; default: goto ma5; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t11 != ((uint8_t)61ULL)) goto pa10;
        goto po9;
pa10:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t13 != ((uint8_t)62ULL)) goto pa12;
        goto po9;
pa12:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t15 != ((uint8_t)63ULL)) goto pa14;
        goto po9;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t17 != ((uint8_t)69ULL)) goto pa16;
        goto po9;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t19 != ((uint8_t)70ULL)) goto pa18;
        goto po9;
pa18:;
        goto mn8;
po9:;
        {
            void * t21 = gm_l0;
            void * t22 = gm_l0;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t22) + 268));
            uint32_t r24; bool o25 = __builtin_add_overflow(t23, ((uint32_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 102, 13);
            uint32_t t26 = r24;
            GM_ST(uint32_t, (((uint8_t *)t21) + 268), t26);
        }
        {
            void * t27 = gm_l0;
            void * t28 = gm_l0;
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t28) + 272));
            uint32_t r30; bool o31 = __builtin_add_overflow(t29, ((uint32_t)1ULL), &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 103, 13);
            uint32_t t32 = r30;
            GM_ST(uint32_t, (((uint8_t *)t27) + 272), t32);
        }
        goto me2;
mn8:;
ma4:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t34 != ((uint8_t)67ULL)) goto mn33;
        {
            void * t36 = gm_l0;
            void * t37 = gm_l0;
            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t37) + 276));
            uint32_t r39; bool o40 = __builtin_add_overflow(t38, ((uint32_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 105, 24);
            uint32_t t41 = r39;
            GM_ST(uint32_t, (((uint8_t *)t36) + 276), t41);
        }
        goto me2;
mn33:;
ma5:;
        goto me2;
mn42:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 100, 5);
me2:;
    }
    {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 16);
        gm_tftb1odc739t5 t46 = gm_f_push_7g6eqp6(t45, &(*(gm_tjc2ga47o3k6g *)((uint8_t *)&F.l1)));
        gm_tftb1odc739t5 t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcutmnmd444bu0, 108, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_expr_697jn9n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2) {
    struct gm_fr_vc_expr_697jn9n F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tmn7n0n9raou5 gm_l2; /* want */
    void * gm_l3; /* b */
    gm_trl2khmuphncd gm_l4; /* x */
    void * gm_l5; /* scrutinee */
    gm_b16_8 gm_l6; /* arms */
    uint32_t gm_l8; /* levels */
    void * gm_l9; /* value */
    gm_te9hrk0u35rev gm_l10; /* target */
    void * gm_l11; /* value */
    uint32_t gm_l12; /* levels */
    uint32_t gm_l13; /* label */
    uint32_t gm_l14; /* li */
    uint32_t gm_l15; /* at */
    gm_te9hrk0u35rev gm_l16; /* target */
    uint32_t gm_l17; /* levels */
    uint32_t gm_l18; /* label */
    uint32_t gm_l19; /* li */
    uint32_t gm_l20; /* at */
    uint32_t gm_l21; /* f */
    gm_b16_8 gm_l22; /* args */
    void * gm_l23; /* callee */
    gm_b16_8 gm_l24; /* args */
    uint32_t gm_l27; /* r */
    uint32_t gm_l28; /* save */
    uint32_t gm_l29; /* save */
    uint64_t gm_l30; /* slots */
    uint32_t gm_l31; /* at */
    uint32_t gm_l32; /* at */
    GM_STACK_CHECK(&F, 2495u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma15; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma15; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; case ((uint8_t)27ULL): goto ma15; case ((uint8_t)28ULL): goto ma15; case ((uint8_t)29ULL): goto ma15; case ((uint8_t)30ULL): goto ma15; case ((uint8_t)31ULL): goto ma15; case ((uint8_t)32ULL): goto ma15; case ((uint8_t)33ULL): goto ma15; case ((uint8_t)34ULL): goto ma15; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma15; case ((uint8_t)39ULL): goto ma15; case ((uint8_t)40ULL): goto ma15; case ((uint8_t)41ULL): goto ma15; case ((uint8_t)42ULL): goto ma15; case ((uint8_t)43ULL): goto ma15; case ((uint8_t)44ULL): goto ma15; case ((uint8_t)45ULL): goto ma15; case ((uint8_t)46ULL): goto ma15; case ((uint8_t)47ULL): goto ma15; case ((uint8_t)48ULL): goto ma15; case ((uint8_t)49ULL): goto ma15; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma4; case ((uint8_t)52ULL): goto ma5; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma15; case ((uint8_t)58ULL): goto ma15; case ((uint8_t)59ULL): goto ma15; case ((uint8_t)60ULL): goto ma15; case ((uint8_t)61ULL): goto ma14; case ((uint8_t)62ULL): goto ma15; default: goto ma15; }
ma4:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t19 != ((uint8_t)51ULL)) goto mn18;
        void * t20 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l3 = t20;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l3;
            gm_tmn7n0n9raou5 t24 = gm_l2;
            gm_b0_1 t25 = gm_f_vc_block_ql7sru3((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto me3;
mn18:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t29 != ((uint8_t)52ULL)) goto mn28;
        gm_trl2khmuphncd t30 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 0) + 8));
        gm_l4 = t30;
        {
            void * t32 = gm_l0;
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            void * t34 = GM_LD(void *, (((uint8_t *)&gm_l4) + 16));
            void * t35 = GM_LD(void *, (((uint8_t *)&gm_l4) + 24));
            gm_tmn7n0n9raou5 t36 = gm_l2;
            gm_b0_1 t37 = gm_f_vc_if_1vdf5lq((void *)NULL, cx, NULL, t32, t33, t34, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t38;
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
        goto me3;
mn28:;
ma6:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t41 != ((uint8_t)50ULL)) goto mn40;
        void * t42 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l5 = t42;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l6 = t43;
        {
            void * t45 = gm_l0;
            void * t46 = gm_l1;
            void * t47 = gm_l5;
            gm_b16_8 t48 = gm_l6;
            gm_tmn7n0n9raou5 t49 = gm_l2;
            gm_b0_1 t50 = gm_f_vc_match_ikc07jv((void *)NULL, cx, NULL, t45, t46, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t51;
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto me3;
mn40:;
ma7:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t54 != ((uint8_t)53ULL)) goto mn53;
        gm_t710lsrq1m8gk t55 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 0) + 8));
        F.l7 = t55;
        void * t57 = gm_l1;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 104));
        uint8_t t59 = ((t58 == ((uint64_t)0ULL)));
        if (!t59) goto ie56;
        uint8_t t60 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t60) goto ie56;
        {
            void * t62 = gm_l0;
            uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
            void * t65 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
            void * t66 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
            gm_b0_1 t67 = gm_f_vc_loop_upfkot7((void *)NULL, cx, NULL, t62, t63, t64, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t69 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
            if (!t69) goto ie68;
            {
                void * t71 = gm_l0;
                gm_tjc2ga47o3k6g t72;
                memset(&t72, 0, sizeof t72);
                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)75ULL));
                gm_b0_1 t73 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t71, &t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix68;
ie68:;
ix68:;
        }
        {
            gm_b0_1 t74;
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto ix56;
ie56:;
ix56:;
        goto me3;
mn53:;
ma8:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t77 != ((uint8_t)56ULL)) goto mn76;
        uint32_t t78 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l8 = t78;
        void * t79 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l9 = t79;
        uint32_t t81 = gm_l8;
        uint8_t t82 = ((t81 == ((uint32_t)0ULL)));
        if (!t82) goto ie80;
        {
            void * t84 = gm_l0;
            void * t85 = gm_l9;
            gm_tmn7n0n9raou5 t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
            gm_b0_1 t87 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t89 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
            if (!t89) goto ie88;
            {
                {
                    void * t92 = gm_l0;
                    uint32_t t93 = gm_f_vc_alloc_3hmfvtt(t92);
                }
            }
            goto ix88;
ie88:;
ix88:;
        }
        {
            gm_b0_1 t94;
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto ix80;
ie80:;
ix80:;
        goto me3;
mn76:;
ma9:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t97 != ((uint8_t)54ULL)) goto mn96;
        gm_te9hrk0u35rev t98 = GM_LD(gm_te9hrk0u35rev, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l10 = t98;
        void * t99 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 16));
        gm_l11 = t99;
        void * t101 = gm_l11;
        void * t102 = t101;
        uint8_t t103;
        if ((GM_LD(void *, ((uint8_t *)&t102)) != 0)) goto mn105;
        t103 = ((uint8_t)1);
        goto me104;
mn105:;
        t103 = ((uint8_t)0);
        goto me104;
mn106:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 410, 38);
me104:;
        if (!t103) goto ie100;
        {
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            if (t109 != ((uint8_t)0ULL)) goto ie108;
            uint32_t t110 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 4) + 0));
            gm_l12 = t110;
            uint32_t t111 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 4) + 4));
            gm_l13 = t111;
            uint32_t t112 = gm_l12;
            uint8_t t113 = ((t112 == ((uint32_t)0ULL)));
            if (!t113) goto ie108;
            {
                void * t116 = gm_l0;
                uint32_t t117 = gm_l13;
                gm_t9805qtonbsag t118 = gm_f_vc_find_loop_gljp56a(t116, t117);
                gm_t9805qtonbsag t119 = t118;
                if (!((((uint8_t *)&t119))[4] != 0)) goto ie115;
                uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)&t119));
                gm_l14 = t120;
                {
                    void * t122 = gm_l0;
                    uint32_t t123 = gm_l14;
                    gm_b0_1 t124 = gm_f_vc_leave_7oqk8g5((void *)NULL, cx, NULL, t122, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t125 = gm_l0;
                    gm_tjc2ga47o3k6g t126;
                    memset(&t126, 0, sizeof t126);
                    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)38ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t126 + 8), ((uint32_t)0ULL));
                    uint32_t t127 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t125, &t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t127;
                }
                {
                    void * t128 = gm_l0;
                    void * t129 = (void *)(((uint8_t *)t128) + 144);
                    uint32_t t130 = gm_l15;
                    uint32_t t131 = gm_l14;
                    gm_tcc1rq4n1avb2 t132;
                    GM_ST(uint32_t, ((uint8_t *)&t132 + 0), t130);
                    GM_ST(uint32_t, ((uint8_t *)&t132 + 4), t131);
                    gm_tftb1odc739t5 t133 = gm_f_push_nc46kuk(t129, t132);
                    gm_tftb1odc739t5 t134 = t133;
                    if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_srcutmnmd444bu0, 415, 21);
                    gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                }
                {
                    uint8_t t137 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
                    if (!t137) goto ie136;
                    {
                        {
                            void * t140 = gm_l0;
                            uint32_t t141 = gm_f_vc_alloc_3hmfvtt(t140);
                        }
                    }
                    goto ix136;
ie136:;
ix136:;
                }
                {
                    gm_b0_1 t142;
                    uint8_t t143 = 0;
                    F.ret = t142;
                    goto gm_fret;
                }
                goto ix115;
ie115:;
ix115:;
            }
            goto ix108;
ie108:;
ix108:;
        }
        goto ix100;
ie100:;
ix100:;
        goto me3;
mn96:;
ma10:;
        uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t145 != ((uint8_t)55ULL)) goto mn144;
        gm_te9hrk0u35rev t146 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t2) + 0) + 8));
        gm_l16 = t146;
        {
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            if (t149 != ((uint8_t)0ULL)) goto ie148;
            uint32_t t150 = GM_LD(uint32_t, ((((uint8_t *)&gm_l16) + 4) + 0));
            gm_l17 = t150;
            uint32_t t151 = GM_LD(uint32_t, ((((uint8_t *)&gm_l16) + 4) + 4));
            gm_l18 = t151;
            uint32_t t152 = gm_l17;
            uint8_t t153 = ((t152 == ((uint32_t)0ULL)));
            if (!t153) goto ie148;
            {
                void * t156 = gm_l0;
                uint32_t t157 = gm_l18;
                gm_t9805qtonbsag t158 = gm_f_vc_find_loop_gljp56a(t156, t157);
                gm_t9805qtonbsag t159 = t158;
                if (!((((uint8_t *)&t159))[4] != 0)) goto ie155;
                uint32_t t160 = GM_LD(uint32_t, ((uint8_t *)&t159));
                gm_l19 = t160;
                {
                    void * t162 = gm_l0;
                    uint32_t t163 = gm_l19;
                    gm_b0_1 t164 = gm_f_vc_leave_7oqk8g5((void *)NULL, cx, NULL, t162, t163);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t165 = gm_l0;
                    gm_tjc2ga47o3k6g t166;
                    memset(&t166, 0, sizeof t166);
                    GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)38ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t166 + 8), ((uint32_t)0ULL));
                    uint32_t t167 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t165, &t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t167;
                }
                {
                    void * t168 = gm_l0;
                    void * t169 = (void *)(((uint8_t *)t168) + 184);
                    uint32_t t170 = gm_l20;
                    uint32_t t171 = gm_l19;
                    gm_tcc1rq4n1avb2 t172;
                    GM_ST(uint32_t, ((uint8_t *)&t172 + 0), t170);
                    GM_ST(uint32_t, ((uint8_t *)&t172 + 4), t171);
                    gm_tftb1odc739t5 t173 = gm_f_push_nc46kuk(t169, t172);
                    gm_tftb1odc739t5 t174 = t173;
                    if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srcutmnmd444bu0, 426, 21);
                    gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
                }
                {
                    uint8_t t177 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
                    if (!t177) goto ie176;
                    {
                        {
                            void * t180 = gm_l0;
                            uint32_t t181 = gm_f_vc_alloc_3hmfvtt(t180);
                        }
                    }
                    goto ix176;
ie176:;
ix176:;
                }
                {
                    gm_b0_1 t182;
                    uint8_t t183 = 0;
                    F.ret = t182;
                    goto gm_fret;
                }
                goto ix155;
ie155:;
ix155:;
            }
            goto ix148;
ie148:;
ix148:;
        }
        goto me3;
mn144:;
ma11:;
        uint8_t t185 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t185 != ((uint8_t)35ULL)) goto mn184;
        uint32_t t186 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l21 = t186;
        gm_b16_8 t187 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l22 = t187;
        uint8_t t189 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t189) goto ie188;
        {
            void * t191 = gm_l0;
            uint32_t t192 = gm_l21;
            gm_b16_8 t193 = gm_l22;
            gm_tmn7n0n9raou5 t194 = gm_l2;
            gm_tj2m9dbh1ekv6 t195 = gm_f_want_dest_rhqgfel(t194);
            void * t196 = gm_l1;
            gm_b0_1 t197 = gm_f_vc_call_27qg9ft((void *)NULL, cx, NULL, t191, t192, t193, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t199 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
            if (!t199) goto ie198;
            {
                void * t201 = gm_l0;
                gm_tjc2ga47o3k6g t202;
                memset(&t202, 0, sizeof t202);
                GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)75ULL));
                gm_b0_1 t203 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t201, &t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix198;
ie198:;
ix198:;
        }
        {
            gm_b0_1 t204;
            uint8_t t205 = 0;
            F.ret = t204;
            goto gm_fret;
        }
        goto ix188;
ie188:;
ix188:;
        goto me3;
mn184:;
ma12:;
        uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t207 != ((uint8_t)37ULL)) goto mn206;
        void * t208 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l23 = t208;
        gm_b16_8 t209 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l24 = t209;
        uint8_t t211 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t211) goto ie210;
        {
            void * t213 = gm_l0;
            void * t214 = gm_l23;
            gm_b16_8 t215 = gm_l24;
            gm_tmn7n0n9raou5 t216 = gm_l2;
            gm_tj2m9dbh1ekv6 t217 = gm_f_want_dest_rhqgfel(t216);
            void * t218 = gm_l1;
            gm_b0_1 t219 = gm_f_vc_call_value_5j9edsq((void *)NULL, cx, NULL, t213, t214, t215, t217, t218);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t221 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
            if (!t221) goto ie220;
            {
                void * t223 = gm_l0;
                gm_tjc2ga47o3k6g t224;
                memset(&t224, 0, sizeof t224);
                GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)75ULL));
                gm_b0_1 t225 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t223, &t224);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix220;
ie220:;
ix220:;
        }
        {
            gm_b0_1 t226;
            uint8_t t227 = 0;
            F.ret = t226;
            goto gm_fret;
        }
        goto ix210;
ie210:;
ix210:;
        goto me3;
mn206:;
ma13:;
        uint8_t t229 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t229 != ((uint8_t)36ULL)) goto mn228;
        gm_tf4r0415i49d1 t230 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t2) + 0) + 8));
        F.l25 = t230;
        uint8_t t232 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t232) goto ie231;
        {
            void * t234 = gm_l0;
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 32));
            gm_tmn7n0n9raou5 t236 = gm_l2;
            gm_tj2m9dbh1ekv6 t237 = gm_f_want_dest_rhqgfel(t236);
            void * t238 = gm_l1;
            gm_b0_1 t239 = gm_f_vc_perform_qci0ith((void *)NULL, cx, NULL, t234, t235, t237, t238);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t241 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
            if (!t241) goto ie240;
            {
                void * t243 = gm_l0;
                gm_tjc2ga47o3k6g t244;
                memset(&t244, 0, sizeof t244);
                GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)75ULL));
                gm_b0_1 t245 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t243, &t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix240;
ie240:;
ix240:;
        }
        {
            gm_b0_1 t246;
            uint8_t t247 = 0;
            F.ret = t246;
            goto gm_fret;
        }
        goto ix231;
ie231:;
ix231:;
        goto me3;
mn228:;
ma14:;
        uint8_t t249 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t249 != ((uint8_t)61ULL)) goto mn248;
        gm_t710lsrq1m8gk t250 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 0) + 8));
        F.l26 = t250;
        void * t252 = GM_LD(void *, (((uint8_t *)&F.l26) + 24));
        void * t253 = t252;
        uint8_t t254;
        if ((GM_LD(void *, ((uint8_t *)&t253)) != 0)) goto mn256;
        t254 = ((uint8_t)1);
        goto me255;
mn256:;
        t254 = ((uint8_t)0);
        goto me255;
mn257:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 447, 24);
me255:;
        if (!t254) goto ie251;
        uint8_t t258 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t258) goto ie251;
        {
            void * t260 = gm_l0;
            void * t261 = gm_l1;
            gm_tmn7n0n9raou5 t262 = gm_l2;
            gm_b0_1 t263 = gm_f_vc_with_h9bq2rc((void *)NULL, cx, NULL, t260, t261, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l26)), t262);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t264;
            uint8_t t265 = 0;
            F.ret = t264;
            goto gm_fret;
        }
        goto ix251;
ie251:;
ix251:;
        goto me3;
mn248:;
ma15:;
        goto me3;
mn266:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 387, 5);
me3:;
    }
    {
        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t274) { case ((uint8_t)0ULL): goto ma269; case ((uint8_t)1ULL): goto ma270; case ((uint8_t)2ULL): goto ma271; case ((uint8_t)3ULL): goto ma272; default: goto ma273; }
ma269:;
        uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t276 != ((uint8_t)0ULL)) goto mn275;
        {
            void * t279 = gm_l1;
            uint8_t t280 = gm_f_is_word_82qsidt(t279);
            if (!t280) goto ie278;
            uint8_t t281 = 0;
            uint8_t t282 = 0;
            void * t283 = gm_l1;
            uint8_t t284;
            uint8_t t287 = GM_LD(uint8_t, ((((uint8_t *)t283) + 0) + 0));
            if (t287 != ((uint8_t)56ULL)) goto mn286;
            t284 = ((uint8_t)1);
            goto me285;
mn286:;
            t284 = ((uint8_t)0);
            goto me285;
mn288:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 457, 36);
me285:;
            if (!t284) {
                void * t289 = gm_l1;
                uint8_t t290;
                uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)t289) + 0) + 0));
                if (t293 != ((uint8_t)54ULL)) goto mn292;
                t290 = ((uint8_t)1);
                goto me291;
mn292:;
                t290 = ((uint8_t)0);
                goto me291;
mn294:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 457, 58);
me291:;
                t282 = t290;
            } else t282 = 1;
            if (!t282) {
                void * t295 = gm_l1;
                uint8_t t296;
                uint8_t t299 = GM_LD(uint8_t, ((((uint8_t *)t295) + 0) + 0));
                if (t299 != ((uint8_t)55ULL)) goto mn298;
                t296 = ((uint8_t)1);
                goto me297;
mn298:;
                t296 = ((uint8_t)0);
                goto me297;
mn300:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 457, 79);
me297:;
                t281 = t296;
            } else t281 = 1;
            uint8_t t301 = (uint8_t)!t281;
            if (!t301) goto ie278;
            {
                void * t303 = gm_l0;
                void * t304 = gm_l1;
                gm_b0_1 t305 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t303, t304);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix278;
ie278:;
            {
                void * t307 = gm_l0;
                void * t308 = gm_l1;
                gm_tjc2ga47o3k6g t309;
                memset(&t309, 0, sizeof t309);
                GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)63ULL));
                GM_ST(void *, ((uint8_t *)&t309 + 8), t308);
                gm_b0_1 t310 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t307, &t309);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t311 = gm_l0;
                uint32_t t312 = gm_f_vc_alloc_3hmfvtt(t311);
                gm_l27 = t312;
            }
            {
                void * t313 = gm_l0;
                uint32_t t314 = gm_l27;
                gm_tb31k5l794t7p t315;
                GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t315 + 0), t314);
                GM_ST(uint64_t, ((uint8_t *)&t315 + 8), ((uint64_t)0ULL));
                gm_tjc2ga47o3k6g t316;
                memset(&t316, 0, sizeof t316);
                GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t316 + 8), t315);
                gm_b0_1 t317 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t313, &t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix278:;
        }
        goto me268;
mn275:;
ma270:;
        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t319 != ((uint8_t)1ULL)) goto mn318;
        {
            void * t322 = gm_l1;
            uint8_t t323 = gm_f_is_word_82qsidt(t322);
            if (!t323) goto ie321;
            void * t324 = gm_l0;
            void * t325 = gm_l1;
            uint8_t t326 = gm_f_vc_compiles_au5pqeh((void *)NULL, cx, NULL, t324, t325);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t326) goto ie321;
            {
                void * t328 = gm_l0;
                uint32_t t329 = GM_LD(uint32_t, (((uint8_t *)t328) + 224));
                gm_l28 = t329;
            }
            {
                void * t330 = gm_l0;
                void * t331 = gm_l1;
                gm_b0_1 t332 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t330, t331);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t333 = gm_l0;
                uint32_t t334 = gm_l28;
                GM_ST(uint32_t, (((uint8_t *)t333) + 224), t334);
            }
            goto ix321;
ie321:;
            {
                void * t336 = gm_l0;
                void * t337 = gm_l1;
                gm_tjc2ga47o3k6g t338;
                memset(&t338, 0, sizeof t338);
                GM_ST(uint8_t, ((uint8_t *)&t338 + 0), ((uint8_t)63ULL));
                GM_ST(void *, ((uint8_t *)&t338 + 8), t337);
                gm_b0_1 t339 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t336, &t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix321:;
        }
        goto me268;
mn318:;
ma271:;
        uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t341 != ((uint8_t)2ULL)) goto mn340;
        {
            void * t344 = gm_l1;
            uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)t344) + 104));
            uint8_t t346 = ((t345 == ((uint64_t)0ULL)));
            if (!t346) goto ie343;
            {
                void * t348 = gm_l0;
                void * t349 = gm_l1;
                gm_tmn7n0n9raou5 t350;
                GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)1ULL));
                gm_b0_1 t351 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t348, t349, t350);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t352 = gm_l0;
                gm_tjc2ga47o3k6g t353;
                memset(&t353, 0, sizeof t353);
                GM_ST(uint8_t, ((uint8_t *)&t353 + 0), ((uint8_t)75ULL));
                gm_b0_1 t354 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t352, &t353);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix343;
ie343:;
            void * t356 = gm_l1;
            uint8_t t357 = gm_f_is_word_82qsidt(t356);
            if (!t357) goto ie355;
            {
                void * t359 = gm_l0;
                uint32_t t360 = GM_LD(uint32_t, (((uint8_t *)t359) + 224));
                gm_l29 = t360;
            }
            {
                void * t361 = gm_l0;
                void * t362 = gm_l0;
                void * t363 = gm_l1;
                uint32_t t364 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t362, t363, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tjc2ga47o3k6g t365;
                memset(&t365, 0, sizeof t365);
                GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)72ULL));
                GM_ST(uint32_t, ((uint8_t *)&t365 + 8), t364);
                gm_b0_1 t366 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t361, &t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t367 = gm_l0;
                uint32_t t368 = gm_l29;
                GM_ST(uint32_t, (((uint8_t *)t367) + 224), t368);
            }
            goto ix355;
ie355:;
            void * t370 = gm_l0;
            void * t371 = gm_l1;
            uint8_t t372 = gm_f_vc_builds_d9pue9e((void *)NULL, cx, NULL, t370, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t372) goto ie369;
            {
                void * t374 = gm_l0;
                uint64_t t375 = GM_LD(uint64_t, (((uint8_t *)t374) + 248));
                gm_l30 = t375;
            }
            {
                void * t376 = gm_l0;
                void * t377 = gm_l0;
                void * t378 = gm_l1;
                uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)t378) + 104));
                uint64_t t380 = gm_f_vc_alloc_slot_5eptdbb(t377, t379);
                uint32_t t381 = gm_f_vc_slot_at_rc6jhbj(t376, t380);
                gm_l31 = t381;
            }
            {
                void * t382 = gm_l0;
                void * t383 = gm_l1;
                uint32_t t384 = gm_l31;
                gm_b0_1 t385 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t382, t383, t384);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t386 = gm_l0;
                uint32_t t387 = gm_l31;
                void * t388 = gm_l1;
                uint64_t t389 = GM_LD(uint64_t, (((uint8_t *)t388) + 104));
                uint32_t t390 = (uint32_t)t389;
                gm_tcc1rq4n1avb2 t391;
                GM_ST(uint32_t, ((uint8_t *)&t391 + 0), t387);
                GM_ST(uint32_t, ((uint8_t *)&t391 + 4), t390);
                gm_tjc2ga47o3k6g t392;
                memset(&t392, 0, sizeof t392);
                GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)74ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t392 + 8), t391);
                gm_b0_1 t393 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t386, &t392);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t394 = gm_l0;
                uint64_t t395 = gm_l30;
                GM_ST(uint64_t, (((uint8_t *)t394) + 248), t395);
            }
            goto ix369;
ie369:;
            {
                void * t397 = gm_l0;
                void * t398 = gm_l1;
                gm_tjc2ga47o3k6g t399;
                memset(&t399, 0, sizeof t399);
                GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)73ULL));
                GM_ST(void *, ((uint8_t *)&t399 + 8), t398);
                gm_b0_1 t400 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t397, &t399);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix369:;
ix355:;
ix343:;
        }
        goto me268;
mn340:;
ma272:;
        uint8_t t402 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t402 != ((uint8_t)3ULL)) goto mn401;
        uint32_t t403 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_l32 = t403;
        void * t404 = gm_l0;
        void * t405 = gm_l1;
        uint32_t t406 = gm_l32;
        gm_b0_1 t407 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t404, t405, t406);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me268;
mn401:;
ma273:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 453, 5);
me268:;
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
inline __attribute__((aligned(4))) void gm_f_vc_field_root_lb3os0t(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_field_root_lb3os0t F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* l */
    gm_t1j70din2upc4 gm_l3; /* x */
    gm_tb31k5l794t7p gm_l4; /* r */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    gm_tgmjda3f8g6hg t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; case ((uint8_t)59ULL): goto ma6; case ((uint8_t)60ULL): goto ma6; case ((uint8_t)61ULL): goto ma6; case ((uint8_t)62ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t10 != ((uint8_t)6ULL)) goto mn9;
    uint32_t t11 = GM_LD(uint32_t, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l2 = t11;
    uint32_t t12 = gm_l2;
    gm_tb31k5l794t7p t13;
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t12);
    GM_ST(uint64_t, ((uint8_t *)&t13 + 8), ((uint64_t)0ULL));
    gm_tgmjda3f8g6hg t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[16] = 1;
    t2 = t14;
    goto me3;
mn9:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t16 != ((uint8_t)10ULL)) goto mn15;
    gm_t1j70din2upc4 t17 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t1) + 0) + 8));
    gm_l3 = t17;
    gm_tgmjda3f8g6hg t18;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)&gm_l3) + 0));
    gm_tgmjda3f8g6hg t22;
    gm_f_vc_field_root_lb3os0t((void *)&t22, t20, t21);
    gm_tgmjda3f8g6hg t23 = t22;
    if (!((((uint8_t *)&t23))[16] != 0)) goto ie19;
    gm_tb31k5l794t7p t24 = GM_LD(gm_tb31k5l794t7p, ((uint8_t *)&t23));
    gm_l4 = t24;
    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
    uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
    if (o30) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 271, 74);
    uint64_t t31 = r29;
    gm_tb31k5l794t7p t32;
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t26);
    GM_ST(uint64_t, ((uint8_t *)&t32 + 8), t31);
    gm_tb31k5l794t7p t33 = t32;
    gm_tgmjda3f8g6hg t34;
    GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t34), t33);
    ((uint8_t *)&t34)[16] = 1;
    t18 = t34;
    goto ix19;
ie19:;
    gm_tgmjda3f8g6hg t36;
    memset(&t36, 0, sizeof t36);
    gm_tgmjda3f8g6hg t37 = t36;
    t18 = t37;
ix19:;
    t2 = t18;
    goto me3;
mn15:;
ma6:;
    gm_tgmjda3f8g6hg t39;
    memset(&t39, 0, sizeof t39);
    t2 = t39;
    goto me3;
mn38:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 269, 81);
me3:;
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgmjda3f8g6hg gm_f_vc_field_root_lb3os0t_c(void * a0, void * a1) { gm_tgmjda3f8g6hg gm_r; gm_f_vc_field_root_lb3os0t(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_find_loop_gljp56a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_find_loop_gljp56a F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* label */
    uint64_t gm_l2; /* i */
    uint32_t gm_l3; /* li */
    GM_STACK_CHECK_SHALLOW(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 96) + 0) + 8);
        gm_l2 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 1783, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
            }
            {
                uint64_t t13 = gm_l2;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 96) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcutmnmd444bu0, 1784, 15);
                uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 96) + 0)) + t14 * 4));
                gm_l3 = t17;
            }
            {
                uint32_t t19 = gm_l3;
                uint64_t t20 = (uint64_t)t19;
                uint64_t t21 = (uintptr_t)t20;
                void * t22 = gm_l0;
                uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 56) + 0) + 8);
                if (t21 >= (uintptr_t)(t23)) gm_trap_index(t21, t23, gm_srcutmnmd444bu0, 1785, 12);
                uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t22) + 56) + 0)) + t21 * 40) + 0));
                uint32_t t25 = gm_l1;
                uint8_t t26 = ((t24 == t25));
                if (!t26) goto ie18;
                {
                    uint32_t t28 = gm_l3;
                    gm_t9805qtonbsag t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t29), t28);
                    ((uint8_t *)&t29)[4] = 1;
                    uint8_t t30 = 0;
                    F.ret = t29;
                    goto gm_fret;
                }
                goto ix18;
ie18:;
ix18:;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_t9805qtonbsag t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_has_index_b75va8s(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_has_index_b75va8s F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_t1j70din2upc4 gm_l2; /* x */
    GM_STACK_CHECK(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma4; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; case ((uint8_t)59ULL): goto ma6; case ((uint8_t)60ULL): goto ma6; case ((uint8_t)61ULL): goto ma6; case ((uint8_t)62ULL): goto ma6; default: goto ma6; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t10 != ((uint8_t)12ULL)) goto mn9;
    t2 = ((uint8_t)1);
    goto me3;
mn9:;
ma5:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t12 != ((uint8_t)10ULL)) goto mn11;
    gm_t1j70din2upc4 t13 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t1) + 0) + 8));
    gm_l2 = t13;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
    uint8_t t16 = gm_f_vc_has_index_b75va8s(t14, t15);
    t2 = t16;
    goto me3;
mn11:;
ma6:;
    t2 = ((uint8_t)0);
    goto me3;
mn17:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1294, 61);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_vc_here_cc4mbj3(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_if_1vdf5lq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3, gm_tmn7n0n9raou5 a4) {
    struct gm_fr_vc_if_1vdf5lq F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* cond */
    void * gm_l2; /* then */
    void * gm_l3; /* else_ */
    gm_tmn7n0n9raou5 gm_l4; /* want */
    uint32_t gm_l5; /* save */
    uint64_t gm_l6; /* slots */
    gm_t9805qtonbsag gm_l8; /* end */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint32_t gm_l11; /* f */
    void * gm_l12; /* el */
    uint32_t gm_l13; /* e */
    GM_STACK_CHECK(&F, 684u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        gm_f_vc_conds_a1m6u4v((void *)&F.l7, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l2;
        gm_tmn7n0n9raou5 t10 = gm_l4;
        gm_b0_1 t11 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_l5;
        GM_ST(uint32_t, (((uint8_t *)t12) + 224), t13);
    }
    {
        gm_t9805qtonbsag t14;
        memset(&t14, 0, sizeof t14);
        gm_l8 = t14;
    }
    {
        uint8_t t16 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)2ULL)));
        if (!t16) goto ie15;
        {
            void * t18 = gm_l0;
            gm_tjc2ga47o3k6g t19;
            memset(&t19, 0, sizeof t19);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)38ULL));
            GM_ST(uint32_t, ((uint8_t *)&t19 + 8), ((uint32_t)0ULL));
            uint32_t t20 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t18, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[4] = 1;
            gm_l8 = t21;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_l9 = t23;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l10;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l10;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t31 * 4));
                    uint32_t t34 = t33;
                    uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&t34));
                    gm_l11 = t37;
                    {
                        void * t39 = gm_l0;
                        uint32_t t40 = gm_l11;
                        void * t41 = gm_l0;
                        uint32_t t42 = gm_f_vc_here_cc4mbj3(t41);
                        gm_f_vc_patch_pbmmb3q(t39, t40, t42);
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1736, 5);
me35:;
                }
lc24:;
                {
                    uint64_t t44 = gm_l10;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l10 = t47;
                }
            }
lb24:;
        }
    }
    {
        void * t48 = (void *)((uint8_t *)&F.l7);
        gm_f_deinit_padgogk(t48);
    }
    {
        void * t49 = gm_l0;
        uint64_t t50 = gm_l6;
        GM_ST(uint64_t, (((uint8_t *)t49) + 248), t50);
    }
    {
        void * t52 = gm_l3;
        void * t53 = t52;
        if (!(GM_LD(void *, ((uint8_t *)&t53)) != 0)) goto ie51;
        void * t54 = GM_LD(void *, ((uint8_t *)&t53));
        gm_l12 = t54;
        {
            void * t56 = gm_l0;
            void * t57 = gm_l12;
            gm_tmn7n0n9raou5 t58 = gm_l4;
            gm_b0_1 t59 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix51;
ie51:;
        {
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            switch (t66) { case ((uint8_t)0ULL): goto ma62; case ((uint8_t)1ULL): goto ma63; case ((uint8_t)2ULL): goto ma64; case ((uint8_t)3ULL): goto ma63; default: goto ma65; }
ma62:;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            if (t68 != ((uint8_t)0ULL)) goto mn67;
            void * t69 = gm_l0;
            void * t70 = gm_l0;
            uint32_t t71 = gm_f_vc_alloc_3hmfvtt(t70);
            gm_tb31k5l794t7p t72;
            GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t72 + 0), t71);
            GM_ST(uint64_t, ((uint8_t *)&t72 + 8), ((uint64_t)0ULL));
            gm_tjc2ga47o3k6g t73;
            memset(&t73, 0, sizeof t73);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t73 + 8), t72);
            gm_b0_1 t74 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t69, &t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me61;
mn67:;
ma63:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            if (t78 != ((uint8_t)1ULL)) goto pa77;
            goto po76;
pa77:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            if (t80 != ((uint8_t)3ULL)) goto pa79;
            goto po76;
pa79:;
            goto mn75;
po76:;
            goto me61;
mn75:;
ma64:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            if (t83 != ((uint8_t)2ULL)) goto mn82;
            void * t84 = gm_l0;
            gm_tjc2ga47o3k6g t85;
            memset(&t85, 0, sizeof t85);
            GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)75ULL));
            gm_b0_1 t86 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t84, &t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me61;
mn82:;
ma65:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1742, 9);
me61:;
        }
ix51:;
    }
    {
        if (!((((uint8_t *)&gm_l8))[4] != 0)) goto ie87;
        uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
        gm_l13 = t88;
        {
            void * t90 = gm_l0;
            uint32_t t91 = gm_l13;
            void * t92 = gm_l0;
            uint32_t t93 = gm_f_vc_here_cc4mbj3(t92);
            gm_f_vc_patch_pbmmb3q(t90, t91, t93);
        }
        goto ix87;
ie87:;
ix87:;
    }
    {
        void * t94 = gm_l0;
        uint32_t t95;
        uint8_t t97 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)0ULL)));
        if (!t97) goto ie96;
        uint32_t t99 = gm_l5;
        uint32_t r100; bool o101 = __builtin_add_overflow(t99, ((uint32_t)1ULL), &r100);
        if (o101) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1749, 36);
        uint32_t t102 = r100;
        uint32_t t103 = t102;
        t95 = t103;
        goto ix96;
ie96:;
        uint32_t t105 = gm_l5;
        uint32_t t106 = t105;
        t95 = t106;
ix96:;
        gm_f_vc_set_top_h0qmvd1(t94, t95);
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
extern __attribute__((aligned(4))) uint32_t gm_f_vc_jump_f1p9hn7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_leave_7oqk8g5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_vc_leave_7oqk8g5 F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* li */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 56) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcutmnmd444bu0, 1794, 11);
        gm_t98k64j4g45rk t7 = GM_LD(gm_t98k64j4g45rk, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 56) + 0)) + t4 * 40));
        F.l2 = t7;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 136));
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
        uint8_t t12 = (t10 > t11);
        if (!t12) goto ie8;
        {
            void * t14 = gm_l0;
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 12));
            gm_tjc2ga47o3k6g t16;
            memset(&t16, 0, sizeof t16);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)68ULL));
            GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
            gm_b0_1 t17 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t14, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t19 = gm_l0;
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 280));
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 24));
        uint8_t t22 = (t20 > t21);
        if (!t22) goto ie18;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 28));
        uint8_t t24 = (uint8_t)!t23;
        if (!t24) goto ie18;
        {
            void * t26 = gm_l0;
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 20));
            gm_tjc2ga47o3k6g t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)81ULL));
            GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
            gm_b0_1 t29 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t26, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix18;
ie18:;
ix18:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_load_v0jk07v(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, uint64_t a3, uint64_t a4, uint32_t a5, void * a6) {
    struct gm_fr_vc_load_v0jk07v F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* r */
    uint64_t gm_l3; /* extra */
    uint64_t gm_l4; /* size */
    uint32_t gm_l5; /* align */
    void * gm_l6; /* n */
    uint32_t gm_l7; /* levels */
    uint32_t gm_l8; /* l */
    uint64_t gm_l9; /* rel */
    uint8_t gm_l10; /* packed */
    gm_t9805qtonbsag gm_l11; /* d */
    uint32_t gm_l12; /* at */
    uint32_t gm_l13; /* dr */
    uint32_t gm_l14; /* base */
    uint64_t gm_l15; /* off */
    uint8_t gm_l16; /* packed */
    GM_STACK_CHECK(&F, 767u);
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
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t4 != ((uint8_t)0ULL)) goto mn3;
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 0));
        gm_l7 = t5;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 4));
        gm_l8 = t6;
        uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
        gm_l9 = t7;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)a2) + 8) + 16));
        gm_l10 = t8;
        gm_t9805qtonbsag t9 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)a2) + 8) + 20));
        gm_l11 = t9;
        {
            uint32_t t12 = gm_l7;
            uint8_t t13 = (t12 > ((uint32_t)0ULL));
            if (!t13) goto ie11;
            {
                void * t15 = gm_l0;
                gm_tn3qmqcq7vsk9 t16;
                uint32_t t17 = gm_l1;
                GM_ST(uint32_t, (uint8_t *)&t16 + 0, t17);
                uint32_t t18 = gm_l7;
                GM_ST(uint32_t, (uint8_t *)&t16 + 4, t18);
                uint32_t t19 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t16 + 8, t19);
                uint64_t t20 = gm_l9;
                uint64_t t21 = gm_l3;
                uint64_t r22; bool o23 = __builtin_add_overflow(t20, t21, &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 964, 51);
                uint64_t t24 = r22;
                uint32_t t25 = (uint32_t)t24;
                GM_ST(uint32_t, (uint8_t *)&t16 + 12, t25);
                uint64_t t26 = gm_l4;
                uint32_t t27 = (uint32_t)t26;
                GM_ST(uint32_t, (uint8_t *)&t16 + 16, t27);
                uint32_t t28;
                uint8_t t30 = gm_l10;
                if (!t30) goto ie29;
                uint32_t t32 = ((uint32_t)1ULL);
                t28 = t32;
                goto ix29;
ie29:;
                uint32_t t34 = gm_l5;
                uint32_t t35 = t34;
                t28 = t35;
ix29:;
                GM_ST(uint32_t, (uint8_t *)&t16 + 20, t28);
                void * t36 = gm_l6;
                GM_ST(void *, (uint8_t *)&t16 + 24, t36);
                gm_tjc2ga47o3k6g t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t37 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t37 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
                GM_ST(gm_tn3qmqcq7vsk9, ((uint8_t *)&t37 + 8), t16);
                gm_b0_1 t38 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t15, &t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t39;
                uint8_t t40 = 0;
                F.ret = t39;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            uint32_t t41 = gm_l8;
            uint64_t t42 = (uint64_t)t41;
            uint64_t t43 = (uintptr_t)t42;
            void * t44 = gm_l0;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 8));
            uint64_t t46 = GM_LD(uintptr_t, (((uint8_t *)t45) + 72) + 8);
            if (t43 >= (uintptr_t)(t46)) gm_trap_index(t43, t46, gm_srcutmnmd444bu0, 967, 20);
            uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t45) + 72)) + t43 * 8));
            uint64_t t48 = gm_l9;
            uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
            if (o50) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 967, 20);
            uint64_t t51 = r49;
            uint64_t t52 = gm_l3;
            uint64_t r53; bool o54 = __builtin_add_overflow(t51, t52, &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 967, 20);
            uint64_t t55 = r53;
            uint32_t t56 = (uint32_t)t55;
            gm_l12 = t56;
        }
        {
            if (!((((uint8_t *)&gm_l11))[4] != 0)) goto ie57;
            uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
            gm_l13 = t58;
            {
                void * t60 = gm_l0;
                uint32_t t61 = gm_l1;
                uint32_t t62 = gm_l12;
                uint32_t t63 = gm_l13;
                uint64_t t64 = gm_l4;
                uint32_t t65 = (uint32_t)t64;
                gm_thpdbp3q7rdrc t66;
                GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t61);
                GM_ST(uint32_t, ((uint8_t *)&t66 + 4), t62);
                GM_ST(uint32_t, ((uint8_t *)&t66 + 8), t63);
                GM_ST(uint32_t, ((uint8_t *)&t66 + 12), t65);
                gm_tjc2ga47o3k6g t67;
                memset(&t67, 0, sizeof t67);
                GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)6ULL));
                GM_ST(gm_thpdbp3q7rdrc, ((uint8_t *)&t67 + 8), t66);
                gm_b0_1 t68 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t60, &t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t69;
                uint8_t t70 = 0;
                F.ret = t69;
                goto gm_fret;
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t71 = gm_l0;
            uint32_t t72 = gm_l1;
            uint32_t t73 = gm_l12;
            uint64_t t74 = gm_l4;
            uint32_t t75 = (uint32_t)t74;
            gm_tl874t2atnlh3 t76;
            GM_ST(uint32_t, ((uint8_t *)&t76 + 0), t72);
            GM_ST(uint32_t, ((uint8_t *)&t76 + 4), t73);
            GM_ST(uint32_t, ((uint8_t *)&t76 + 8), t75);
            gm_tjc2ga47o3k6g t77;
            memset(&t77, 0, sizeof t77);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)3ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t77 + 8), t76);
            gm_b0_1 t78 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t71, &t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me2;
mn3:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t80 != ((uint8_t)1ULL)) goto mn79;
        uint32_t t81 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 0));
        gm_l14 = t81;
        uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
        gm_l15 = t82;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)a2) + 8) + 16));
        gm_l16 = t83;
        void * t84 = gm_l0;
        gm_t04llc5uen0qo t85;
        memset((uint8_t *)&t85 + 20, 0, 4);
        uint32_t t86 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t85 + 0, t86);
        uint32_t t87 = gm_l14;
        GM_ST(uint32_t, (uint8_t *)&t85 + 4, t87);
        uint64_t t88 = gm_l15;
        uint64_t t89 = gm_l3;
        uint64_t r90; bool o91 = __builtin_add_overflow(t88, t89, &r90);
        if (o91) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 974, 62);
        uint64_t t92 = r90;
        uint32_t t93 = (uint32_t)t92;
        GM_ST(uint32_t, (uint8_t *)&t85 + 8, t93);
        uint64_t t94 = gm_l4;
        uint32_t t95 = (uint32_t)t94;
        GM_ST(uint32_t, (uint8_t *)&t85 + 12, t95);
        uint32_t t96;
        uint8_t t98 = gm_l16;
        if (!t98) goto ie97;
        uint32_t t100 = ((uint32_t)1ULL);
        t96 = t100;
        goto ix97;
ie97:;
        uint32_t t102 = gm_l5;
        uint32_t t103 = t102;
        t96 = t103;
ix97:;
        GM_ST(uint32_t, (uint8_t *)&t85 + 16, t96);
        void * t104 = gm_l6;
        GM_ST(void *, (uint8_t *)&t85 + 24, t104);
        gm_tjc2ga47o3k6g t105;
        GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t105 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)17ULL));
        GM_ST(gm_t04llc5uen0qo, ((uint8_t *)&t105 + 8), t85);
        gm_b0_1 t106 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t84, &t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn79:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 961, 5);
me2:;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_local_reg_bgdr42m(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_local_reg_bgdr42m F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        uint8_t t5 = 0;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t7) + 90));
        uint8_t t9 = (uint8_t)!t8;
        if (!t9) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = (uintptr_t)t10;
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 8));
            uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)t13) + 120) + 8);
            if (t11 >= (uintptr_t)(t14)) gm_trap_index(t11, t14, gm_srcutmnmd444bu0, 205, 31);
            uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t13) + 120)) + t11 * 1));
            t5 = t15;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            gm_t9805qtonbsag t17;
            memset(&t17, 0, sizeof t17);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t19 = gm_l2;
        uint64_t t20 = (uintptr_t)t19;
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 8));
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 88) + 8);
        if (t20 >= (uintptr_t)(t23)) gm_trap_index(t20, t23, gm_srcutmnmd444bu0, 206, 10);
        uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 88)) + t20 * 8));
        gm_l3 = t24;
    }
    {
        uint8_t t26 = 0;
        uint64_t t27 = gm_l3;
        uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
        if (!t28) {
            uint64_t t29 = gm_l3;
            uint8_t t30 = (t29 > ((uint64_t)8ULL));
            t26 = t30;
        } else t26 = 1;
        if (!t26) goto ie25;
        {
            gm_t9805qtonbsag t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        uint64_t t34 = gm_l2;
        uint64_t t35 = (uintptr_t)t34;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 8));
        uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 72) + 8);
        if (t35 >= (uintptr_t)(t38)) gm_trap_index(t35, t38, gm_srcutmnmd444bu0, 208, 13);
        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t37) + 72)) + t35 * 8));
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 208, 13);
        uint64_t t40 = (uint64_t)(t39 / ((uint64_t)8ULL));
        uint32_t t41 = (uint32_t)t40;
        gm_t9805qtonbsag t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[4] = 1;
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_loop_upfkot7(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, void * a4) {
    struct gm_fr_vc_loop_upfkot7 F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* cond */
    void * gm_l3; /* body */
    void * gm_l4; /* step */
    uint32_t gm_l5; /* save */
    uint64_t gm_l6; /* slots */
    uint32_t gm_l7; /* mark */
    uint32_t gm_l8; /* h */
    uint32_t gm_l9; /* cx */
    uint32_t gm_l10; /* li */
    uint32_t gm_l11; /* dh */
    uint32_t gm_l12; /* ndefers */
    uint32_t gm_l13; /* cs */
    uint32_t gm_l14; /* nwiths */
    uint32_t gm_l15; /* leaks_all */
    uint32_t gm_l16; /* leaks */
    uint32_t gm_l17; /* head */
    uint32_t gm_l19; /* cont_at */
    void * gm_l20; /* st */
    uint32_t gm_l21; /* rel */
    uint32_t gm_l22; /* brk */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint32_t gm_l25; /* f */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_tcc1rq4n1avb2 gm_l29; /* x */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_tcc1rq4n1avb2 gm_l33; /* x */
    GM_STACK_CHECK(&F, 2176u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        uint32_t t7 = gm_f_vc_alloc_3hmfvtt(t6);
        gm_l7 = t7;
    }
    {
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_f_vc_alloc_3hmfvtt(t9);
        }
    }
    {
        {
            void * t12 = gm_l0;
            uint32_t t13 = gm_f_vc_alloc_3hmfvtt(t12);
        }
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = gm_f_vc_alloc_3hmfvtt(t14);
        gm_l8 = t15;
    }
    {
        void * t16 = gm_l0;
        uint32_t t17 = gm_f_vc_alloc_3hmfvtt(t16);
        gm_l9 = t17;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 56) + 0) + 8);
        uint32_t t20 = (uint32_t)t19;
        gm_l10 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 56);
        gm_t98k64j4g45rk t23;
        memset((uint8_t *)&t23 + 29, 0, 3);
        uint32_t t24 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t23 + 0, t24);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t23 + 8, ((uint32_t)0ULL));
        uint32_t t25 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t23 + 12, t25);
        void * t26 = gm_l0;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 136));
        GM_ST(uint32_t, (uint8_t *)&t23 + 16, t27);
        uint32_t t28 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t23 + 20, t28);
        void * t29 = gm_l0;
        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t29) + 280));
        GM_ST(uint32_t, (uint8_t *)&t23 + 24, t30);
        GM_ST(uint8_t, (uint8_t *)&t23 + 28, ((uint8_t)0));
        GM_ST(uint32_t, (uint8_t *)&t23 + 32, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t23 + 36, ((uint32_t)0ULL));
        gm_tftb1odc739t5 t31 = gm_f_push_d9f883g(t22, &t23);
        gm_tftb1odc739t5 t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcutmnmd444bu0, 1894, 5);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    {
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 96);
        uint32_t t36 = gm_l10;
        gm_tftb1odc739t5 t37 = gm_f_push_im9d4ro(t35, t36);
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcutmnmd444bu0, 1895, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        void * t40 = gm_l0;
        uint32_t t41 = gm_l8;
        gm_tjc2ga47o3k6g t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)66ULL));
        GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t41);
        uint32_t t43 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t40, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t43;
    }
    {
        void * t44 = gm_l0;
        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t44) + 276));
        gm_l12 = t45;
    }
    {
        void * t46 = gm_l0;
        uint32_t t47 = gm_l9;
        gm_tjc2ga47o3k6g t48;
        memset(&t48, 0, sizeof t48);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)80ULL));
        GM_ST(uint32_t, ((uint8_t *)&t48 + 8), t47);
        uint32_t t49 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t46, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t49;
    }
    {
        void * t50 = gm_l0;
        uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t50) + 284));
        gm_l14 = t51;
    }
    {
        void * t52 = gm_l0;
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 268));
        gm_l15 = t53;
    }
    {
        void * t54 = gm_l0;
        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 272));
        gm_l16 = t55;
    }
    {
        void * t56 = gm_l0;
        uint32_t t57 = gm_l7;
        gm_tjc2ga47o3k6g t58;
        memset(&t58, 0, sizeof t58);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)76ULL));
        GM_ST(uint32_t, ((uint8_t *)&t58 + 8), t57);
        uint32_t t59 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t56, &t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t59;
    }
    {
        void * t60 = gm_l0;
        gm_b16_8 t61 = gm_l2;
        gm_f_vc_conds_a1m6u4v((void *)&F.l18, (void *)NULL, cx, NULL, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t62 = gm_l0;
        void * t63 = gm_l3;
        gm_tmn7n0n9raou5 t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
        gm_b0_1 t65 = gm_f_vc_block_ql7sru3((void *)NULL, cx, NULL, t62, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t66 = gm_l0;
        uint32_t t67 = gm_l7;
        uint32_t r68; bool o69 = __builtin_add_overflow(t67, ((uint32_t)4ULL), &r68);
        if (o69) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1905, 13);
        uint32_t t70 = r68;
        GM_ST(uint32_t, (((uint8_t *)t66) + 224), t70);
    }
    {
        void * t71 = gm_l0;
        uint32_t t72 = gm_f_vc_here_cc4mbj3(t71);
        gm_l19 = t72;
    }
    {
        void * t74 = gm_l4;
        void * t75 = t74;
        if (!(GM_LD(void *, ((uint8_t *)&t75)) != 0)) goto ie73;
        void * t76 = GM_LD(void *, ((uint8_t *)&t75));
        gm_l20 = t76;
        {
            void * t78 = gm_l0;
            void * t79 = gm_l20;
            gm_tmn7n0n9raou5 t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)1ULL));
            gm_b0_1 t81 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        void * t82 = gm_l0;
        uint32_t t83 = gm_l7;
        uint32_t r84; bool o85 = __builtin_add_overflow(t83, ((uint32_t)4ULL), &r84);
        if (o85) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1908, 13);
        uint32_t t86 = r84;
        GM_ST(uint32_t, (((uint8_t *)t82) + 224), t86);
    }
    {
        void * t87 = gm_l0;
        uint32_t t88 = gm_l7;
        gm_tjc2ga47o3k6g t89;
        memset(&t89, 0, sizeof t89);
        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)77ULL));
        GM_ST(uint32_t, ((uint8_t *)&t89 + 8), t88);
        uint32_t t90 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t87, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t90;
    }
    {
        void * t91 = gm_l0;
        uint32_t t92 = gm_l17;
        gm_tjc2ga47o3k6g t93;
        memset(&t93, 0, sizeof t93);
        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)78ULL));
        GM_ST(uint32_t, ((uint8_t *)&t93 + 8), t92);
        gm_b0_1 t94 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t91, &t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t95 = gm_l0;
        uint32_t t96 = gm_l7;
        gm_tjc2ga47o3k6g t97;
        memset(&t97, 0, sizeof t97);
        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)77ULL));
        GM_ST(uint32_t, ((uint8_t *)&t97 + 8), t96);
        uint32_t t98 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t95, &t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t98;
    }
    {
        {
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            gm_l23 = t100;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t102 = gm_l24;
                uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t104 = (t102 < t103);
                if (!t104) goto lb101;
                {
                    uint64_t t107 = gm_l24;
                    uint64_t t108 = (uintptr_t)t107;
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    uint32_t t110 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t108 * 4));
                    uint32_t t111 = t110;
                    uint32_t t114 = GM_LD(uint32_t, ((uint8_t *)&t111));
                    gm_l25 = t114;
                    {
                        void * t116 = gm_l0;
                        uint32_t t117 = gm_l25;
                        uint32_t t118 = gm_l22;
                        gm_f_vc_patch_pbmmb3q(t116, t117, t118);
                    }
                    goto me112;
mn113:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1912, 5);
me112:;
                }
lc101:;
                {
                    uint64_t t120 = gm_l24;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l24 = t123;
                }
            }
lb101:;
        }
    }
    {
        void * t124 = (void *)((uint8_t *)&F.l18);
        gm_f_deinit_padgogk(t124);
    }
    {
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            void * t126 = gm_l0;
            uint64_t t127 = GM_LD(uintptr_t, ((((uint8_t *)t126) + 144) + 0) + 8);
            gm_l26 = t127;
        }
        {
            for (;;) {
                uint64_t t129 = gm_l27;
                uint64_t t130 = gm_l26;
                uint8_t t131 = (t129 < t130);
                if (!t131) goto lb128;
                {
                    uint64_t t134 = gm_l27;
                    uint64_t t135 = t134;
                    uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                    gm_l28 = t138;
                    {
                        uint64_t t140 = gm_l28;
                        uint64_t t141 = (uintptr_t)t140;
                        void * t142 = gm_l0;
                        uint64_t t143 = GM_LD(uintptr_t, ((((uint8_t *)t142) + 144) + 0) + 8);
                        if (t141 >= (uintptr_t)(t143)) gm_trap_index(t141, t143, gm_srcutmnmd444bu0, 1915, 14);
                        gm_tcc1rq4n1avb2 t144 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t142) + 144) + 0)) + t141 * 8));
                        gm_l29 = t144;
                    }
                    {
                        uint32_t t146 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 4));
                        uint32_t t147 = gm_l10;
                        uint8_t t148 = ((t146 == t147));
                        if (!t148) goto ie145;
                        {
                            void * t150 = gm_l0;
                            uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 0));
                            uint32_t t152 = gm_l22;
                            gm_f_vc_patch_pbmmb3q(t150, t151, t152);
                        }
                        goto ix145;
ie145:;
ix145:;
                    }
                    goto me136;
mn137:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1914, 5);
me136:;
                }
lc128:;
                {
                    uint64_t t154 = gm_l27;
                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                    uint64_t t157 = r155;
                    gm_l27 = t157;
                }
            }
lb128:;
        }
    }
    {
        {
            gm_l31 = ((uint64_t)0ULL);
        }
        {
            void * t159 = gm_l0;
            uint64_t t160 = GM_LD(uintptr_t, ((((uint8_t *)t159) + 184) + 0) + 8);
            gm_l30 = t160;
        }
        {
            for (;;) {
                uint64_t t162 = gm_l31;
                uint64_t t163 = gm_l30;
                uint8_t t164 = (t162 < t163);
                if (!t164) goto lb161;
                {
                    uint64_t t167 = gm_l31;
                    uint64_t t168 = t167;
                    uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&t168));
                    gm_l32 = t171;
                    {
                        uint64_t t173 = gm_l32;
                        uint64_t t174 = (uintptr_t)t173;
                        void * t175 = gm_l0;
                        uint64_t t176 = GM_LD(uintptr_t, ((((uint8_t *)t175) + 184) + 0) + 8);
                        if (t174 >= (uintptr_t)(t176)) gm_trap_index(t174, t176, gm_srcutmnmd444bu0, 1919, 14);
                        gm_tcc1rq4n1avb2 t177 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t175) + 184) + 0)) + t174 * 8));
                        gm_l33 = t177;
                    }
                    {
                        uint32_t t179 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 4));
                        uint32_t t180 = gm_l10;
                        uint8_t t181 = ((t179 == t180));
                        if (!t181) goto ie178;
                        {
                            void * t183 = gm_l0;
                            uint32_t t184 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 0));
                            uint32_t t185 = gm_l19;
                            gm_f_vc_patch_pbmmb3q(t183, t184, t185);
                        }
                        goto ix178;
ie178:;
ix178:;
                    }
                    goto me169;
mn170:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1918, 5);
me169:;
                }
lc161:;
                {
                    uint64_t t187 = gm_l31;
                    uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                    uint64_t t190 = r188;
                    gm_l31 = t190;
                }
            }
lb161:;
        }
    }
    {
        void * t192 = gm_l0;
        uint32_t t193 = GM_LD(uint32_t, (((uint8_t *)t192) + 268));
        uint32_t t194 = gm_l15;
        uint8_t t195 = ((t193 == t194));
        if (!t195) goto ie191;
        {
            uint32_t t197 = gm_l17;
            uint64_t t198 = (uint64_t)t197;
            uint64_t t199 = (uintptr_t)t198;
            void * t200 = gm_l0;
            uint64_t t201 = GM_LD(uintptr_t, ((((uint8_t *)t200) + 16) + 0) + 8);
            if (t199 >= (uintptr_t)(t201)) gm_trap_index(t199, t201, gm_srcutmnmd444bu0, 1926, 9);
            gm_tjc2ga47o3k6g t202;
            memset(&t202, 0, sizeof t202);
            GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t200) + 16) + 0)) + t199 * 56), t202);
        }
        {
            uint32_t t203 = gm_l21;
            uint64_t t204 = (uint64_t)t203;
            uint64_t t205 = (uintptr_t)t204;
            void * t206 = gm_l0;
            uint64_t t207 = GM_LD(uintptr_t, ((((uint8_t *)t206) + 16) + 0) + 8);
            if (t205 >= (uintptr_t)(t207)) gm_trap_index(t205, t207, gm_srcutmnmd444bu0, 1927, 9);
            gm_tjc2ga47o3k6g t208;
            memset(&t208, 0, sizeof t208);
            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t206) + 16) + 0)) + t205 * 56), t208);
        }
        {
            uint32_t t209 = gm_l22;
            uint64_t t210 = (uint64_t)t209;
            uint64_t t211 = (uintptr_t)t210;
            void * t212 = gm_l0;
            uint64_t t213 = GM_LD(uintptr_t, ((((uint8_t *)t212) + 16) + 0) + 8);
            if (t211 >= (uintptr_t)(t213)) gm_trap_index(t211, t213, gm_srcutmnmd444bu0, 1928, 9);
            gm_tjc2ga47o3k6g t214;
            memset(&t214, 0, sizeof t214);
            GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t212) + 16) + 0)) + t211 * 56), t214);
        }
        goto ix191;
ie191:;
ix191:;
    }
    {
        void * t216 = gm_l0;
        uint32_t t217 = GM_LD(uint32_t, (((uint8_t *)t216) + 276));
        uint32_t t218 = gm_l12;
        uint8_t t219 = ((t217 == t218));
        if (!t219) goto ie215;
        {
            uint32_t t221 = gm_l11;
            uint64_t t222 = (uint64_t)t221;
            uint64_t t223 = (uintptr_t)t222;
            void * t224 = gm_l0;
            uint64_t t225 = GM_LD(uintptr_t, ((((uint8_t *)t224) + 16) + 0) + 8);
            if (t223 >= (uintptr_t)(t225)) gm_trap_index(t223, t225, gm_srcutmnmd444bu0, 1931, 9);
            gm_tjc2ga47o3k6g t226;
            memset(&t226, 0, sizeof t226);
            GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t224) + 16) + 0)) + t223 * 56), t226);
        }
        {
            uint32_t t227 = gm_l10;
            uint64_t t228 = (uint64_t)t227;
            uint64_t t229 = (uintptr_t)t228;
            void * t230 = gm_l0;
            uint64_t t231 = GM_LD(uintptr_t, ((((uint8_t *)t230) + 56) + 0) + 8);
            if (t229 >= (uintptr_t)(t231)) gm_trap_index(t229, t231, gm_srcutmnmd444bu0, 1932, 9);
            uint32_t t232 = GM_LD(uint32_t, gm_chkfraubo1r);
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t230) + 56) + 0)) + t229 * 40) + 12), t232);
        }
        goto ix215;
ie215:;
ix215:;
    }
    {
        void * t234 = gm_l0;
        uint32_t t235 = GM_LD(uint32_t, (((uint8_t *)t234) + 284));
        uint32_t t236 = gm_l14;
        uint8_t t237 = ((t235 == t236));
        if (!t237) goto ie233;
        {
            uint32_t t239 = gm_l13;
            uint64_t t240 = (uint64_t)t239;
            uint64_t t241 = (uintptr_t)t240;
            void * t242 = gm_l0;
            uint64_t t243 = GM_LD(uintptr_t, ((((uint8_t *)t242) + 16) + 0) + 8);
            if (t241 >= (uintptr_t)(t243)) gm_trap_index(t241, t243, gm_srcutmnmd444bu0, 1935, 9);
            gm_tjc2ga47o3k6g t244;
            memset(&t244, 0, sizeof t244);
            GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t242) + 16) + 0)) + t241 * 56), t244);
        }
        {
            uint32_t t245 = gm_l10;
            uint64_t t246 = (uint64_t)t245;
            uint64_t t247 = (uintptr_t)t246;
            void * t248 = gm_l0;
            uint64_t t249 = GM_LD(uintptr_t, ((((uint8_t *)t248) + 56) + 0) + 8);
            if (t247 >= (uintptr_t)(t249)) gm_trap_index(t247, t249, gm_srcutmnmd444bu0, 1936, 9);
            uint32_t t250 = GM_LD(uint32_t, gm_c7p3n1736t1);
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t248) + 56) + 0)) + t247 * 40) + 20), t250);
        }
        goto ix233;
ie233:;
ix233:;
    }
    {
        void * t251 = gm_l0;
        uint32_t t252 = gm_l16;
        GM_ST(uint32_t, (((uint8_t *)t251) + 272), t252);
    }
    {
        {
            void * t254 = gm_l0;
            void * t255 = (void *)(((uint8_t *)t254) + 96);
            gm_t9805qtonbsag t256 = gm_f_pop_570g79n(t255);
        }
    }
    {
        uint32_t t257 = gm_l10;
        uint64_t t258 = (uint64_t)t257;
        uint64_t t259 = (uintptr_t)t258;
        void * t260 = gm_l0;
        uint64_t t261 = GM_LD(uintptr_t, ((((uint8_t *)t260) + 56) + 0) + 8);
        if (t259 >= (uintptr_t)(t261)) gm_trap_index(t259, t261, gm_srcutmnmd444bu0, 1940, 5);
        uint32_t t262 = gm_l22;
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 56) + 0)) + t259 * 40) + 4), t262);
    }
    {
        uint32_t t263 = gm_l10;
        uint64_t t264 = (uint64_t)t263;
        uint64_t t265 = (uintptr_t)t264;
        void * t266 = gm_l0;
        uint64_t t267 = GM_LD(uintptr_t, ((((uint8_t *)t266) + 56) + 0) + 8);
        if (t265 >= (uintptr_t)(t267)) gm_trap_index(t265, t267, gm_srcutmnmd444bu0, 1941, 5);
        uint32_t t268 = gm_l19;
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t266) + 56) + 0)) + t265 * 40) + 8), t268);
    }
    {
        void * t269 = gm_l0;
        uint32_t t270 = gm_l5;
        GM_ST(uint32_t, (((uint8_t *)t269) + 224), t270);
    }
    {
        void * t271 = gm_l0;
        uint64_t t272 = gm_l6;
        GM_ST(uint64_t, (((uint8_t *)t271) + 248), t272);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_match_ikc07jv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_tmn7n0n9raou5 a4) {
    struct gm_fr_vc_match_ikc07jv F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    gm_tmn7n0n9raou5 gm_l4; /* want */
    uint32_t gm_l5; /* save */
    uint64_t gm_l6; /* slots */
    uint32_t gm_l7; /* at */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l12; /* arm */
    void * gm_l13; /* g */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint32_t gm_l16; /* f */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* e */
    GM_STACK_CHECK(&F, 1033u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 104));
        uint64_t t10 = gm_f_vc_alloc_slot_5eptdbb(t7, t9);
        uint32_t t11 = gm_f_vc_slot_at_rc6jhbj(t6, t10);
        gm_l7 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l2;
        uint32_t t14 = gm_l7;
        gm_b0_1 t15 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t12, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_go2qg0b((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t17 = gm_l3;
            gm_l10 = t17;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l11;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l11;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_tbbqefuhv2r8h t29 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t25 * 24));
                    gm_l12 = t29;
                    {
                        void * t31 = gm_l0;
                        void * t32 = GM_LD(void *, (((uint8_t *)&gm_l12) + 0));
                        uint32_t t33 = gm_l7;
                        void * t34 = gm_l2;
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 104));
                        void * t36 = (void *)((uint8_t *)&F.l9);
                        gm_b0_1 t37 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t31, t32, t33, t35, t36);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                    }
                    {
                        void * t39 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                        void * t40 = t39;
                        if (!(GM_LD(void *, ((uint8_t *)&t40)) != 0)) goto ie38;
                        void * t41 = GM_LD(void *, ((uint8_t *)&t40));
                        gm_l13 = t41;
                        {
                            void * t43 = gm_l0;
                            void * t44 = gm_l13;
                            gm_b0_1 t45 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                        }
                        {
                            void * t46 = (void *)((uint8_t *)&F.l9);
                            void * t47 = gm_l0;
                            uint32_t t48 = gm_l5;
                            gm_tcc1rq4n1avb2 t49;
                            GM_ST(uint32_t, ((uint8_t *)&t49 + 0), t48);
                            GM_ST(uint32_t, ((uint8_t *)&t49 + 4), ((uint32_t)0ULL));
                            gm_tjc2ga47o3k6g t50;
                            memset(&t50, 0, sizeof t50);
                            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)39ULL));
                            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t50 + 8), t49);
                            uint32_t t51 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t47, &t50);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tftb1odc739t5 t52 = gm_f_push_im9d4ro(t46, t51);
                            gm_tftb1odc739t5 t53 = t52;
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcutmnmd444bu0, 1763, 13);
                            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
                        }
                        {
                            void * t55 = gm_l0;
                            uint32_t t56 = gm_l5;
                            GM_ST(uint32_t, (((uint8_t *)t55) + 224), t56);
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        void * t57 = gm_l0;
                        void * t58 = GM_LD(void *, (((uint8_t *)&gm_l12) + 16));
                        gm_tmn7n0n9raou5 t59 = gm_l4;
                        gm_b0_1 t60 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t57, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                    }
                    {
                        void * t61 = gm_l0;
                        uint32_t t62 = gm_l5;
                        GM_ST(uint32_t, (((uint8_t *)t61) + 224), t62);
                    }
                    {
                        uint8_t t64 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)2ULL)));
                        if (!t64) goto ie63;
                        {
                            void * t66 = (void *)((uint8_t *)&F.l8);
                            void * t67 = gm_l0;
                            gm_tjc2ga47o3k6g t68;
                            memset(&t68, 0, sizeof t68);
                            GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)38ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t68 + 8), ((uint32_t)0ULL));
                            uint32_t t69 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t67, &t68);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tftb1odc739t5 t70 = gm_f_push_im9d4ro(t66, t69);
                            gm_tftb1odc739t5 t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcutmnmd444bu0, 1768, 31);
                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    {
                        {
                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_l14 = t74;
                        }
                        {
                            gm_l15 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t76 = gm_l15;
                                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                uint8_t t78 = (t76 < t77);
                                if (!t78) goto lb75;
                                {
                                    uint64_t t81 = gm_l15;
                                    uint64_t t82 = (uintptr_t)t81;
                                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                                    uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t82 * 4));
                                    uint32_t t85 = t84;
                                    uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)&t85));
                                    gm_l16 = t88;
                                    {
                                        void * t90 = gm_l0;
                                        uint32_t t91 = gm_l16;
                                        void * t92 = gm_l0;
                                        uint32_t t93 = gm_f_vc_here_cc4mbj3(t92);
                                        gm_f_vc_patch_pbmmb3q(t90, t91, t93);
                                    }
                                    goto me86;
mn87:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1769, 9);
me86:;
                                }
lc75:;
                                {
                                    uint64_t t95 = gm_l15;
                                    uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                                    uint64_t t98 = r96;
                                    gm_l15 = t98;
                                }
                            }
lb75:;
                        }
                    }
                    {
                        void * t99 = (void *)((uint8_t *)&F.l9);
                        gm_f_clear_4s443ju(t99);
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1759, 5);
me27:;
                }
lc18:;
                {
                    uint64_t t101 = gm_l11;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l11 = t104;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
        }
    }
    {
        void * t105 = (void *)((uint8_t *)&F.l9);
        gm_f_deinit_padgogk(t105);
    }
    {
        void * t106 = gm_l0;
        void * t107 = gm_l1;
        gm_tjc2ga47o3k6g t108;
        memset(&t108, 0, sizeof t108);
        GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)71ULL));
        GM_ST(void *, ((uint8_t *)&t108 + 8), t107);
        gm_b0_1 t109 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t106, &t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_l17 = t111;
        }
        {
            gm_l18 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t113 = gm_l18;
                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                uint8_t t115 = (t113 < t114);
                if (!t115) goto lb112;
                {
                    uint64_t t118 = gm_l18;
                    uint64_t t119 = (uintptr_t)t118;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint32_t t121 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t119 * 4));
                    uint32_t t122 = t121;
                    uint32_t t125 = GM_LD(uint32_t, ((uint8_t *)&t122));
                    gm_l19 = t125;
                    {
                        void * t127 = gm_l0;
                        uint32_t t128 = gm_l19;
                        void * t129 = gm_l0;
                        uint32_t t130 = gm_f_vc_here_cc4mbj3(t129);
                        gm_f_vc_patch_pbmmb3q(t127, t128, t130);
                    }
                    goto me123;
mn124:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1774, 5);
me123:;
                }
lc112:;
                {
                    uint64_t t132 = gm_l18;
                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                    uint64_t t135 = r133;
                    gm_l18 = t135;
                }
            }
lb112:;
        }
    }
    {
        void * t136 = (void *)((uint8_t *)&F.l8);
        gm_f_deinit_padgogk(t136);
    }
    {
        void * t137 = gm_l0;
        uint64_t t138 = gm_l6;
        GM_ST(uint64_t, (((uint8_t *)t137) + 248), t138);
    }
    {
        void * t139 = gm_l0;
        uint32_t t140;
        uint8_t t142 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)0ULL)));
        if (!t142) goto ie141;
        uint32_t t144 = gm_l5;
        uint32_t r145; bool o146 = __builtin_add_overflow(t144, ((uint32_t)1ULL), &r145);
        if (o146) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1777, 36);
        uint32_t t147 = r145;
        uint32_t t148 = t147;
        t140 = t148;
        goto ix141;
ie141:;
        uint32_t t150 = gm_l5;
        uint32_t t151 = t150;
        t140 = t151;
ix141:;
        gm_f_vc_set_top_h0qmvd1(t139, t140);
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
inline __attribute__((aligned(4))) uint32_t gm_f_vc_operand_nmt6qjg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_vc_operand_nmt6qjg F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint8_t gm_l2; /* late */
    uint32_t gm_l3; /* r */
    uint64_t gm_l4; /* v */
    uint32_t gm_l5; /* r */
    uint32_t gm_l6; /* r */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        if (!t3) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l1;
            gm_t9805qtonbsag t8 = gm_f_vc_place_reg_e0pc35g(t6, t7);
            gm_t9805qtonbsag t9 = t8;
            if (!((((uint8_t *)&t9))[4] != 0)) goto ie5;
            uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&t9));
            gm_l3 = t10;
            {
                uint32_t t12 = gm_l3;
                uint8_t t13 = 0;
                F.ret = t12;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l1;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 0) + 0));
        if (t16 != ((uint8_t)0ULL)) goto ie14;
        uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 0) + 8));
        gm_l4 = t17;
        {
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 288);
            uint64_t t22 = gm_l4;
            gm_t9805qtonbsag t23 = gm_f_get_bjkgton(t21, t22);
            gm_t9805qtonbsag t24 = t23;
            if (!((((uint8_t *)&t24))[4] != 0)) goto ie19;
            uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&t24));
            gm_l5 = t25;
            {
                uint32_t t27 = gm_l5;
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
            }
            goto ix19;
ie19:;
ix19:;
        }
        {
            uint64_t t30 = gm_l4;
            void * t31 = gm_l0;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((((uint8_t *)t31) + 344) + 0));
            gm_b16_8 t33 = t32;
            uint8_t t34 = 0;
            uint64_t t35 = t30;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t33) + 8)); i++) if (gm_eqb2nno7n67r(((uint8_t *)GM_LD(void *, ((uint8_t *)&t33))) + i * 8, ((uint8_t *)&t35))) { t34 = 1; break; }
            uint8_t t36 = (uint8_t)(!t34);
            if (!t36) goto ie29;
            {
                void * t38 = gm_l0;
                void * t39 = (void *)(((uint8_t *)t38) + 344);
                uint64_t t40 = gm_l4;
                gm_tftb1odc739t5 t41 = gm_f_push_7o6oubd(t39, t40);
                gm_tftb1odc739t5 t42 = t41;
                if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcutmnmd444bu0, 285, 37);
                gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
            }
            goto ix29;
ie29:;
ix29:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t44 = gm_l0;
        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t44) + 224));
        gm_l6 = t45;
    }
    {
        void * t46 = gm_l0;
        void * t47 = gm_l1;
        gm_b0_1 t48 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t49 = gm_l6;
        uint8_t t50 = 0;
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
inline __attribute__((aligned(4))) void gm_f_vc_opt_shape_of1700t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_vc_opt_shape_of1700t F;
    void * gm_l0; /* c */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* m */
    gm_t88rcucg1b7uk gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_b16_8 gm_l5; /* offs */
    GM_STACK_CHECK(&F, 457u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_htv6093((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        gm_t88rcucg1b7uk t8;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t11 != ((uint8_t)13ULL)) goto mn10;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l3 = t12;
        gm_t88rcucg1b7uk t13 = gm_l3;
        t8 = t13;
        goto me9;
mn10:;
        gm_tk6jsoi855f62 t15;
        memset(&t15, 0, sizeof t15);
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
        goto me9;
mn14:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1719, 14);
me9:;
        gm_l4 = t8;
    }
    {
        void * t18 = gm_l2;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
        gm_t88rcucg1b7uk t20 = gm_l4;
        uint8_t t21 = gm_f_nullable_pointer_7qfmn6o(t19, GM_CONV(gm_csguankko4lmhd, t20));
        if (!t21) goto ie17;
        {
            gm_tkpb4kplftgaa t23;
            GM_ST(uint64_t, (uint8_t *)&t23 + 0, ((uint64_t)0ULL));
            void * t24 = gm_l2;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 176));
            GM_ST(uint64_t, (uint8_t *)&t23 + 8, t25);
            GM_ST(uint64_t, (uint8_t *)&t23 + 16, ((uint64_t)0ULL));
            gm_tk6jsoi855f62 t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t26 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t26 + 28, 0);
            GM_ST(gm_tkpb4kplftgaa, ((uint8_t *)&t26), t23);
            ((uint8_t *)&t26)[24] = 1;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t28 = gm_l2;
        gm_t88rcucg1b7uk t29 = gm_l1;
        gm_b16_8 t30 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t30;
    }
    {
        gm_tkpb4kplftgaa t31;
        uint64_t t32 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcutmnmd444bu0, 1725, 19);
        uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t32 * 8));
        GM_ST(uint64_t, (uint8_t *)&t31 + 0, t34);
        GM_ST(uint64_t, (uint8_t *)&t31 + 8, ((uint64_t)1ULL));
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srcutmnmd444bu0, 1725, 46);
        uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t35 * 8));
        GM_ST(uint64_t, (uint8_t *)&t31 + 16, t37);
        gm_tk6jsoi855f62 t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 28, 0);
        GM_ST(gm_tkpb4kplftgaa, ((uint8_t *)&t38), t31);
        ((uint8_t *)&t38)[24] = 1;
        uint8_t t39 = 0;
        F.ret = t38;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_pat_d3t2bfu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint64_t a3, void * a4) {
    struct gm_fr_vc_pat_d3t2bfu F;
    void * gm_l0; /* c */
    void * gm_l1; /* pat */
    uint32_t gm_l2; /* at */
    uint64_t gm_l3; /* size */
    void * gm_l4; /* fails */
    void * gm_l5; /* m */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* e */
    uint32_t gm_l8; /* save */
    uint32_t gm_l9; /* r */
    gm_tmn7n0n9raou5 gm_l10; /* how */
    gm_tkpb4kplftgaa gm_l11; /* shape */
    void * gm_l12; /* p */
    void * gm_l13; /* p */
    void * gm_l14; /* p */
    gm_b16_8 gm_l15; /* offs */
    void * gm_l17; /* p */
    gm_b16_8 gm_l18; /* fields */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l21; /* f */
    gm_tfbnciphf3261 gm_l22; /* fl */
    GM_STACK_CHECK(&F, 2018u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint8_t t5 = gm_f_vc_pat_ok_be3j97k((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            void * t8 = gm_l4;
            void * t9 = gm_l0;
            gm_tpkdfv1vgiqij t10;
            memset((uint8_t *)&t10 + 20, 0, 4);
            void * t11 = gm_l1;
            GM_ST(void *, (uint8_t *)&t10 + 0, t11);
            uint32_t t12 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t10 + 8, t12);
            uint64_t t13 = gm_l3;
            uint32_t t14 = (uint32_t)t13;
            GM_ST(uint32_t, (uint8_t *)&t10 + 12, t14);
            GM_ST(uint32_t, (uint8_t *)&t10 + 16, ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)70ULL));
            GM_ST(gm_tpkdfv1vgiqij, ((uint8_t *)&t15 + 8), t10);
            uint32_t t16 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t9, &t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t17 = gm_f_push_im9d4ro(t8, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcutmnmd444bu0, 1649, 9);
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        {
            gm_b0_1 t20;
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
        gm_l5 = t23;
    }
    {
        void * t24 = gm_l1;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        switch (t35) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma28; case ((uint8_t)3ULL): goto ma33; case ((uint8_t)4ULL): goto ma29; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma31; case ((uint8_t)9ULL): goto ma32; case ((uint8_t)10ULL): goto ma33; default: goto ma34; }
ma26:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t37 != ((uint8_t)0ULL)) goto mn36;
        goto me25;
mn36:;
ma27:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t24) + 16) + 16));
        gm_l6 = t41;
        void * t42 = gm_l0;
        uint32_t t43 = gm_l6;
        uint32_t t44 = gm_l2;
        uint64_t t45 = gm_l3;
        uint32_t t46 = (uint32_t)t45;
        void * t47 = gm_l1;
        gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 144));
        gm_b0_1 t49 = gm_f_vc_set_local_slot_l28kec5((void *)NULL, cx, NULL, t42, t43, t44, t46, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me25;
mn39:;
ma28:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t51 != ((uint8_t)2ULL)) goto mn50;
        void * t52 = GM_LD(void *, ((((uint8_t *)t24) + 16) + 16));
        gm_l7 = t52;
        {
            void * t54 = gm_l0;
            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)t54) + 224));
            gm_l8 = t55;
        }
        {
            void * t56 = gm_l0;
            void * t57 = gm_l7;
            gm_b0_1 t58 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t59 = gm_l0;
            uint32_t t60 = gm_f_vc_alloc_3hmfvtt(t59);
            gm_l9 = t60;
        }
        {
            void * t61 = gm_l0;
            uint32_t t62 = gm_l9;
            uint32_t t63 = gm_l2;
            uint64_t t64 = gm_l3;
            uint32_t t65 = (uint32_t)t64;
            gm_tl874t2atnlh3 t66;
            GM_ST(uint32_t, ((uint8_t *)&t66 + 0), t62);
            GM_ST(uint32_t, ((uint8_t *)&t66 + 4), t63);
            GM_ST(uint32_t, ((uint8_t *)&t66 + 8), t65);
            gm_tjc2ga47o3k6g t67;
            memset(&t67, 0, sizeof t67);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)53ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t67 + 8), t66);
            gm_b0_1 t68 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t61, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tmn7n0n9raou5 t69;
            void * t71 = gm_l5;
            void * t72 = gm_l1;
            gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
            gm_t53at80pfdus3 t74;
            gm_f_kind_htv6093((void *)&t74, t71, GM_CONV(gm_csguankko4lmhd, t73));
            gm_t53at80pfdus3 t75 = t74;
            uint8_t t76 = ((GM_LD(uint8_t, ((uint8_t *)&t75) + 0) == ((uint8_t)4ULL)));
            if (!t76) goto ie70;
            gm_tmn7n0n9raou5 t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)4ULL));
            gm_tmn7n0n9raou5 t79 = t78;
            t69 = t79;
            goto ix70;
ie70:;
            gm_tmn7n0n9raou5 t81;
            GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
            gm_tmn7n0n9raou5 t82 = t81;
            t69 = t82;
ix70:;
            gm_l10 = t69;
        }
        {
            void * t83 = gm_l4;
            void * t84 = gm_l0;
            gm_tufm7evi61pnh t85;
            memset((uint8_t *)&t85 + 9, 0, 3);
            gm_tmn7n0n9raou5 t86 = gm_l10;
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t85 + 0, t86);
            GM_ST(uint8_t, (uint8_t *)&t85 + 8, ((uint8_t)0));
            uint32_t t87 = gm_l8;
            GM_ST(uint32_t, (uint8_t *)&t85 + 12, t87);
            uint32_t t88 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t85 + 16, t88);
            GM_ST(uint32_t, (uint8_t *)&t85 + 20, ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t89;
            GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t89 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)29ULL));
            GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t89 + 8), t85);
            uint32_t t90 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t84, &t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t91 = gm_f_push_im9d4ro(t83, t90);
            gm_tftb1odc739t5 t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcutmnmd444bu0, 1662, 13);
            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
        }
        {
            void * t94 = gm_l0;
            uint32_t t95 = gm_l8;
            GM_ST(uint32_t, (((uint8_t *)t94) + 224), t95);
        }
        goto me25;
mn50:;
ma29:;
        uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t99 != ((uint8_t)4ULL)) goto pa98;
        goto po97;
pa98:;
        uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t101 != ((uint8_t)5ULL)) goto pa100;
        goto po97;
pa100:;
        goto mn96;
po97:;
        {
            void * t103 = gm_l0;
            void * t104 = gm_l1;
            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 0));
            gm_tk6jsoi855f62 t106;
            gm_f_vc_opt_shape_of1700t((void *)&t106, (void *)NULL, cx, NULL, t103, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tk6jsoi855f62 t107 = t106;
            gm_tkpb4kplftgaa t108;
            if (((((uint8_t *)&t107))[24] != 0)) {
                gm_tkpb4kplftgaa t109 = GM_LD(gm_tkpb4kplftgaa, (((uint8_t *)&t107) + 0));
                t108 = t109;
            } else {
                gm_b0_1 t110;
                uint8_t t111 = 0;
                F.ret = t110;
                goto gm_fret;
            }
            gm_l11 = t108;
        }
        {
            void * t112 = gm_l4;
            void * t113 = gm_l0;
            uint32_t t114 = gm_l2;
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 0));
            uint32_t t116 = (uint32_t)t115;
            uint32_t r117; bool o118 = __builtin_add_overflow(t114, t116, &r117);
            if (o118) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1667, 41);
            uint32_t t119 = r117;
            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint32_t t121 = (uint32_t)t120;
            void * t122 = gm_l1;
            uint8_t t123 = ((GM_LD(uint8_t, (((uint8_t *)t122) + 16) + 0) == ((uint8_t)4ULL)));
            gm_t37c2jujtiadn t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t124 + 10, 0);
            GM_ST(uint32_t, ((uint8_t *)&t124 + 0), t119);
            GM_ST(uint32_t, ((uint8_t *)&t124 + 4), t121);
            GM_ST(uint8_t, ((uint8_t *)&t124 + 8), t123);
            GM_ST(uint32_t, ((uint8_t *)&t124 + 12), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t125;
            memset(&t125, 0, sizeof t125);
            GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)55ULL));
            GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t125 + 8), t124);
            uint32_t t126 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t113, &t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t127 = gm_f_push_im9d4ro(t112, t126);
            gm_tftb1odc739t5 t128 = t127;
            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srcutmnmd444bu0, 1667, 13);
            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
        }
        {
            void * t131 = gm_l1;
            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t131) + 16) + 0));
            if (t132 != ((uint8_t)5ULL)) goto ie130;
            void * t133 = GM_LD(void *, ((((uint8_t *)t131) + 16) + 16));
            gm_l12 = t133;
            {
                void * t135 = gm_l0;
                void * t136 = gm_l12;
                uint32_t t137 = gm_l2;
                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 16));
                uint32_t t139 = (uint32_t)t138;
                uint32_t r140; bool o141 = __builtin_add_overflow(t137, t139, &r140);
                if (o141) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1668, 51);
                uint32_t t142 = r140;
                void * t143 = gm_l5;
                void * t144 = gm_l12;
                gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t144) + 0));
                uint64_t t146 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t143, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t147 = gm_l4;
                gm_b0_1 t148 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t135, t136, t142, t146, t147);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix130;
ie130:;
ix130:;
        }
        goto me25;
mn96:;
ma30:;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t152 != ((uint8_t)6ULL)) goto pa151;
        void * t153 = GM_LD(void *, ((((uint8_t *)t24) + 16) + 16));
        gm_l13 = t153;
        goto po150;
pa151:;
        uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t155 != ((uint8_t)7ULL)) goto pa154;
        void * t156 = GM_LD(void *, ((((uint8_t *)t24) + 16) + 16));
        gm_l13 = t156;
        goto po150;
pa154:;
        goto mn149;
po150:;
        {
            void * t158 = gm_l5;
            void * t159 = gm_l1;
            gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t159) + 0));
            gm_b16_8 t161 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t158, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t161;
        }
        {
            void * t162 = gm_l4;
            void * t163 = gm_l0;
            uint32_t t164 = gm_l2;
            uint64_t t165 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t166 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t165 >= (uintptr_t)(t166)) gm_trap_index(t165, t166, gm_srcutmnmd444bu0, 1672, 46);
            uint64_t t167 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t165 * 8));
            uint32_t t168 = (uint32_t)t167;
            uint32_t r169; bool o170 = __builtin_add_overflow(t164, t168, &r169);
            if (o170) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1672, 41);
            uint32_t t171 = r169;
            void * t172 = gm_l1;
            uint8_t t173;
            uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t172) + 16) + 0));
            if (t176 != ((uint8_t)6ULL)) goto mn175;
            t173 = ((uint8_t)1);
            goto me174;
mn175:;
            t173 = ((uint8_t)0);
            goto me174;
mn177:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1672, 69);
me174:;
            gm_t37c2jujtiadn t178;
            GM_ST(uint8_t, (uint8_t *)&t178 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t178 + 10, 0);
            GM_ST(uint32_t, ((uint8_t *)&t178 + 0), t171);
            GM_ST(uint32_t, ((uint8_t *)&t178 + 4), ((uint32_t)1ULL));
            GM_ST(uint8_t, ((uint8_t *)&t178 + 8), t173);
            GM_ST(uint32_t, ((uint8_t *)&t178 + 12), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t179;
            memset(&t179, 0, sizeof t179);
            GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)55ULL));
            GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t179 + 8), t178);
            uint32_t t180 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t163, &t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t181 = gm_f_push_im9d4ro(t162, t180);
            gm_tftb1odc739t5 t182 = t181;
            if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srcutmnmd444bu0, 1672, 13);
            gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
        }
        {
            void * t184 = gm_l0;
            void * t185 = gm_l13;
            uint32_t t186 = gm_l2;
            uint64_t t187 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t188 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t187 >= (uintptr_t)(t188)) gm_trap_index(t187, t188, gm_srcutmnmd444bu0, 1673, 30);
            uint64_t t189 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t187 * 8));
            uint32_t t190 = (uint32_t)t189;
            uint32_t r191; bool o192 = __builtin_add_overflow(t186, t190, &r191);
            if (o192) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1673, 25);
            uint32_t t193 = r191;
            void * t194 = gm_l5;
            void * t195 = gm_l13;
            gm_t88rcucg1b7uk t196 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t195) + 0));
            uint64_t t197 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t194, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t198 = gm_l4;
            gm_b0_1 t199 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t184, t185, t193, t197, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me25;
mn149:;
ma31:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t201 != ((uint8_t)8ULL)) goto mn200;
        gm_t4nomjfe3hlif t202 = GM_LD(gm_t4nomjfe3hlif, ((((uint8_t *)t24) + 16) + 16));
        F.l16 = t202;
        {
            void * t204 = gm_l4;
            void * t205 = gm_l0;
            gm_tjqgc9rpvqnkt t206;
            memset((uint8_t *)&t206 + 20, 0, 4);
            uint32_t t207 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t206 + 0, t207);
            uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
            uint32_t t209 = (uint32_t)t208;
            GM_ST(uint32_t, (uint8_t *)&t206 + 4, t209);
            uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 32));
            GM_ST(uint64_t, (uint8_t *)&t206 + 8, t210);
            GM_ST(uint32_t, (uint8_t *)&t206 + 16, ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t211;
            GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t211 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)60ULL));
            GM_ST(gm_tjqgc9rpvqnkt, ((uint8_t *)&t211 + 8), t206);
            uint32_t t212 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t205, &t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t213 = gm_f_push_im9d4ro(t204, t212);
            gm_tftb1odc739t5 t214 = t213;
            if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 1, gm_srcutmnmd444bu0, 1676, 13);
            gm_b0_1 t215 = GM_LD(gm_b0_1, (((uint8_t *)&t214) + 1));
        }
        {
            void * t217 = GM_LD(void *, (((uint8_t *)&F.l16) + 56));
            void * t218 = t217;
            if (!(GM_LD(void *, ((uint8_t *)&t218)) != 0)) goto ie216;
            void * t219 = GM_LD(void *, ((uint8_t *)&t218));
            gm_l17 = t219;
            {
                void * t221 = gm_l0;
                void * t222 = gm_l17;
                uint32_t t223 = gm_l2;
                uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 40));
                uint32_t t225 = (uint32_t)t224;
                uint32_t r226; bool o227 = __builtin_add_overflow(t223, t225, &r226);
                if (o227) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1677, 45);
                uint32_t t228 = r226;
                uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 48));
                void * t230 = gm_l4;
                gm_b0_1 t231 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t221, t222, t228, t229, t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix216;
ie216:;
ix216:;
        }
        goto me25;
mn200:;
ma32:;
        uint8_t t233 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t233 != ((uint8_t)9ULL)) goto mn232;
        gm_b16_8 t234 = GM_LD(gm_b16_8, ((((uint8_t *)t24) + 16) + 16));
        gm_l18 = t234;
        {
            gm_b16_8 t236 = gm_l18;
            gm_l19 = t236;
        }
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t238 = gm_l20;
                uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                uint8_t t240 = (t238 < t239);
                if (!t240) goto lb237;
                {
                    uint64_t t243 = gm_l20;
                    uint64_t t244 = (uintptr_t)t243;
                    uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    gm_ttggq7ng4kp3p t248 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t244 * 16));
                    gm_l21 = t248;
                    {
                        void * t250 = gm_l5;
                        void * t251 = gm_l1;
                        gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t251) + 0));
                        uint32_t t253 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 0));
                        gm_tfbnciphf3261 t254 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t250, t252, t253);
                        if (gm_unw.kind) {
                            goto gm_ll241;
                        }
                        gm_l22 = t254;
                    }
                    {
                        void * t255 = gm_l0;
                        void * t256 = GM_LD(void *, (((uint8_t *)&gm_l21) + 8));
                        uint32_t t257 = gm_l2;
                        uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        uint32_t t259 = (uint32_t)t258;
                        uint32_t r260; bool o261 = __builtin_add_overflow(t257, t259, &r260);
                        if (o261) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1681, 29);
                        uint32_t t262 = r260;
                        void * t263 = gm_l5;
                        gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 0));
                        uint64_t t265 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t263, t264);
                        if (gm_unw.kind) {
                            goto gm_ll241;
                        }
                        void * t266 = gm_l4;
                        gm_b0_1 t267 = gm_f_vc_pat_d3t2bfu((void *)NULL, cx, NULL, t255, t256, t262, t265, t266);
                        if (gm_unw.kind) {
                            goto gm_ll241;
                        }
                    }
                    goto me246;
mn247:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1679, 28);
me246:;
                }
lc237:;
                {
                    uint64_t t269 = gm_l20;
                    uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                    uint64_t t272 = r270;
                    gm_l20 = t272;
                }
            }
gm_ll241:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb237; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc237; }
            }
            goto gm_lad;
lb237:;
        }
        goto me25;
mn232:;
ma33:;
        uint8_t t276 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t276 != ((uint8_t)3ULL)) goto pa275;
        goto po274;
pa275:;
        uint8_t t278 = GM_LD(uint8_t, ((((uint8_t *)t24) + 16) + 0));
        if (t278 != ((uint8_t)10ULL)) goto pa277;
        goto po274;
pa277:;
        goto mn273;
po274:;
        goto me25;
mn273:;
ma34:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1653, 5);
me25:;
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
__attribute__((aligned(4))) uint8_t gm_f_vc_pat_ok_be3j97k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_pat_ok_be3j97k F;
    void * gm_l0; /* c */
    void * gm_l1; /* pat */
    void * gm_l2; /* m */
    void * gm_l3; /* e */
    void * gm_l4; /* p */
    void * gm_l5; /* p */
    void * gm_l6; /* p */
    void * gm_l8; /* p */
    gm_b16_8 gm_l9; /* fields */
    uint32_t gm_l10; /* id */
    uint8_t gm_l11; /* plain */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l14; /* f */
    GM_STACK_CHECK(&F, 1270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t5;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma14; default: goto ma15; }
ma7:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t20 != ((uint8_t)0ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t22 != ((uint8_t)1ULL)) goto pa21;
        goto po18;
pa21:;
        goto mn17;
po18:;
        t5 = ((uint8_t)1);
        goto me6;
mn17:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t24 != ((uint8_t)2ULL)) goto mn23;
        void * t25 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l3 = t25;
        uint8_t t26 = 0;
        uint8_t t27 = 0;
        void * t28 = gm_l3;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 96));
        void * t30 = gm_l1;
        gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t30) + 0));
        gm_t88rcucg1b7uk t32 = t29;
        gm_t88rcucg1b7uk t33 = t31;
        uint8_t t34 = (gm_eq9m6mvtmgde(((uint8_t *)&t32), ((uint8_t *)&t33)));
        if (t34) {
            void * t35 = gm_l3;
            uint8_t t36 = gm_f_is_word_82qsidt(t35);
            t27 = t36;
        } else t27 = 0;
        if (t27) {
            uint8_t t37 = 0;
            void * t38 = gm_l2;
            void * t39 = gm_l1;
            gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
            gm_t53at80pfdus3 t41;
            gm_f_kind_htv6093((void *)&t41, t38, GM_CONV(gm_csguankko4lmhd, t40));
            gm_t53at80pfdus3 t42 = t41;
            uint8_t t43 = ((GM_LD(uint8_t, ((uint8_t *)&t42) + 0) == ((uint8_t)4ULL)));
            if (!t43) {
                void * t44 = gm_l2;
                void * t45 = gm_l1;
                gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t45) + 0));
                gm_tg5lql6itufk7 t47 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t44, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tg5lql6itufk7 t48 = t47;
                uint8_t t49;
                if (((((uint8_t *)&t48))[8] != 0)) goto mn51;
                t49 = ((uint8_t)0);
                goto me50;
mn51:;
                t49 = ((uint8_t)1);
                goto me50;
mn52:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1694, 84);
me50:;
                t37 = t49;
            } else t37 = 1;
            t26 = t37;
        } else t26 = 0;
        t5 = t26;
        goto me6;
mn23:;
ma9:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t54 != ((uint8_t)4ULL)) goto mn53;
        void * t55 = gm_l0;
        void * t56 = gm_l1;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 0));
        gm_tk6jsoi855f62 t58;
        gm_f_vc_opt_shape_of1700t((void *)&t58, (void *)NULL, cx, NULL, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tk6jsoi855f62 t59 = t58;
        uint8_t t60;
        if (((((uint8_t *)&t59))[24] != 0)) goto mn62;
        t60 = ((uint8_t)0);
        goto me61;
mn62:;
        t60 = ((uint8_t)1);
        goto me61;
mn63:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1695, 18);
me61:;
        t5 = t60;
        goto me6;
mn53:;
ma10:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t65 != ((uint8_t)5ULL)) goto mn64;
        void * t66 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l4 = t66;
        uint8_t t67 = 0;
        void * t68 = gm_l0;
        void * t69 = gm_l1;
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
        gm_tk6jsoi855f62 t71;
        gm_f_vc_opt_shape_of1700t((void *)&t71, (void *)NULL, cx, NULL, t68, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tk6jsoi855f62 t72 = t71;
        uint8_t t73;
        if (((((uint8_t *)&t72))[24] != 0)) goto mn75;
        t73 = ((uint8_t)0);
        goto me74;
mn75:;
        t73 = ((uint8_t)1);
        goto me74;
mn76:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1696, 21);
me74:;
        if (t73) {
            void * t77 = gm_l0;
            void * t78 = gm_l4;
            uint8_t t79 = gm_f_vc_pat_ok_be3j97k((void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t67 = t79;
        } else t67 = 0;
        t5 = t67;
        goto me6;
mn64:;
ma11:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t83 != ((uint8_t)6ULL)) goto pa82;
        void * t84 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l5 = t84;
        goto po81;
pa82:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t86 != ((uint8_t)7ULL)) goto pa85;
        void * t87 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l5 = t87;
        goto po81;
pa85:;
        goto mn80;
po81:;
        uint8_t t88 = 0;
        void * t89 = gm_l2;
        void * t90 = gm_l1;
        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t90) + 0));
        gm_t53at80pfdus3 t92;
        gm_f_kind_htv6093((void *)&t92, t89, GM_CONV(gm_csguankko4lmhd, t91));
        gm_t53at80pfdus3 t93 = t92;
        uint8_t t94;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t97 != ((uint8_t)14ULL)) goto mn96;
        t94 = ((uint8_t)1);
        goto me95;
mn96:;
        t94 = ((uint8_t)0);
        goto me95;
mn98:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1697, 28);
me95:;
        if (t94) {
            void * t99 = gm_l0;
            void * t100 = gm_l5;
            uint8_t t101 = gm_f_vc_pat_ok_be3j97k((void *)NULL, cx, NULL, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t88 = t101;
        } else t88 = 0;
        t5 = t88;
        goto me6;
mn80:;
ma12:;
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t103 != ((uint8_t)8ULL)) goto mn102;
        gm_t4nomjfe3hlif t104 = GM_LD(gm_t4nomjfe3hlif, ((((uint8_t *)t4) + 16) + 16));
        F.l7 = t104;
        uint8_t t105 = 0;
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
        uint8_t t107 = (t106 <= ((uint64_t)8ULL));
        if (t107) {
            uint8_t t108;
            void * t110 = GM_LD(void *, (((uint8_t *)&F.l7) + 56));
            void * t111 = t110;
            if (!(GM_LD(void *, ((uint8_t *)&t111)) != 0)) goto ie109;
            void * t112 = GM_LD(void *, ((uint8_t *)&t111));
            gm_l8 = t112;
            void * t114 = gm_l0;
            void * t115 = gm_l8;
            uint8_t t116 = gm_f_vc_pat_ok_be3j97k((void *)NULL, cx, NULL, t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t117 = t116;
            t108 = t117;
            goto ix109;
ie109:;
            uint8_t t119 = ((uint8_t)1);
            t108 = t119;
ix109:;
            t105 = t108;
        } else t105 = 0;
        t5 = t105;
        goto me6;
mn102:;
ma13:;
        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t121 != ((uint8_t)9ULL)) goto mn120;
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 16) + 16));
        gm_l9 = t122;
        {
            void * t125 = gm_l2;
            void * t126 = gm_l1;
            gm_t88rcucg1b7uk t127 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t126) + 0));
            gm_tkn42u7na3t81 t128 = gm_f_bits_struct_ty_22ue20s(t125, GM_CONV(gm_csguankko4lmhd, t127));
            gm_tkn42u7na3t81 t129 = t128;
            uint8_t t130;
            if (((((uint8_t *)&t129))[4] != 0)) goto mn132;
            t130 = ((uint8_t)0);
            goto me131;
mn132:;
            t130 = ((uint8_t)1);
            goto me131;
mn133:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1700, 16);
me131:;
            if (!t130) goto ie124;
            {
                uint8_t t135 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix124;
ie124:;
ix124:;
        }
        {
            void * t136 = gm_l2;
            void * t137 = gm_l1;
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + 0));
            gm_t53at80pfdus3 t139;
            gm_f_kind_htv6093((void *)&t139, t136, GM_CONV(gm_csguankko4lmhd, t138));
            gm_t53at80pfdus3 t140 = t139;
            uint8_t t141;
            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
            switch (t147) { case ((uint8_t)0ULL): goto ma145; case ((uint8_t)1ULL): goto ma145; case ((uint8_t)2ULL): goto ma145; case ((uint8_t)3ULL): goto ma145; case ((uint8_t)4ULL): goto ma145; case ((uint8_t)5ULL): goto ma145; case ((uint8_t)6ULL): goto ma145; case ((uint8_t)7ULL): goto ma145; case ((uint8_t)8ULL): goto ma145; case ((uint8_t)9ULL): goto ma145; case ((uint8_t)10ULL): goto ma145; case ((uint8_t)11ULL): goto ma145; case ((uint8_t)12ULL): goto ma143; case ((uint8_t)13ULL): goto ma145; case ((uint8_t)14ULL): goto ma145; case ((uint8_t)15ULL): goto ma145; case ((uint8_t)16ULL): goto ma145; case ((uint8_t)17ULL): goto ma144; case ((uint8_t)18ULL): goto ma145; case ((uint8_t)19ULL): goto ma145; case ((uint8_t)20ULL): goto ma145; case ((uint8_t)21ULL): goto ma145; case ((uint8_t)22ULL): goto ma145; case ((uint8_t)23ULL): goto ma145; case ((uint8_t)24ULL): goto ma145; case ((uint8_t)25ULL): goto ma145; case ((uint8_t)26ULL): goto ma145; default: goto ma145; }
ma143:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
            if (t149 != ((uint8_t)12ULL)) goto mn148;
            t141 = ((uint8_t)1);
            goto me142;
mn148:;
ma144:;
            uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t140) + 0));
            if (t151 != ((uint8_t)17ULL)) goto mn150;
            uint32_t t152 = GM_LD(uint32_t, ((((uint8_t *)&t140) + 8) + 0));
            gm_l10 = t152;
            void * t153 = gm_l2;
            void * t154 = GM_LD(void *, (((uint8_t *)t153) + 8));
            gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)t154) + 1088) + 0));
            gm_b16_8 t156 = t155;
            uint32_t t157 = gm_l10;
            uint64_t t158 = (uint64_t)t157;
            uint64_t t159 = gm_f_us_cuv01fp(t158);
            uint64_t t160 = (uintptr_t)t159;
            gm_b16_8 t161 = t156;
            uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&t161) + 8);
            if (t160 >= (uintptr_t)(t162)) gm_trap_index(t160, t162, gm_srcutmnmd444bu0, 1703, 32);
            uint8_t t163;
            uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t161)) + t160 * 184) + 24) + 0));
            if (t166 != ((uint8_t)0ULL)) goto mn165;
            t163 = ((uint8_t)1);
            goto me164;
mn165:;
            t163 = ((uint8_t)0);
            goto me164;
mn167:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1703, 32);
me164:;
            t141 = t163;
            goto me142;
mn150:;
ma145:;
            t141 = ((uint8_t)0);
            goto me142;
mn168:;
ma146:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1701, 22);
me142:;
            gm_l11 = t141;
        }
        {
            uint8_t t170 = gm_l11;
            uint8_t t171 = (uint8_t)!t170;
            if (!t171) goto ie169;
            {
                uint8_t t173 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix169;
ie169:;
ix169:;
        }
        {
            {
                gm_b16_8 t175 = gm_l9;
                gm_l12 = t175;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t177 = gm_l13;
                    uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t179 = (t177 < t178);
                    if (!t179) goto lb176;
                    {
                        uint64_t t182 = gm_l13;
                        uint64_t t183 = (uintptr_t)t182;
                        uint64_t t184 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        gm_ttggq7ng4kp3p t187 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t183 * 16));
                        gm_l14 = t187;
                        {
                            void * t190 = gm_l0;
                            void * t191 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t192 = gm_f_vc_pat_ok_be3j97k((void *)NULL, cx, NULL, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_ll180;
                            }
                            uint8_t t193 = (uint8_t)!t192;
                            if (!t193) goto ie189;
                            {
                                uint8_t t195 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix189;
ie189:;
ix189:;
                        }
                        goto me185;
mn186:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1707, 13);
me185:;
                    }
lc176:;
                    {
                        uint64_t t197 = gm_l13;
                        uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                        uint64_t t200 = r198;
                        gm_l13 = t200;
                    }
                }
gm_ll180:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb176; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc176; }
                }
                goto gm_lad;
lb176:;
            }
        }
        uint8_t t201 = ((uint8_t)1);
        t5 = t201;
        goto me6;
mn120:;
ma14:;
        uint8_t t205 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t205 != ((uint8_t)3ULL)) goto pa204;
        goto po203;
pa204:;
        uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t207 != ((uint8_t)10ULL)) goto pa206;
        goto po203;
pa206:;
        goto mn202;
po203:;
        t5 = ((uint8_t)0);
        goto me6;
mn202:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1692, 12);
me6:;
        uint8_t t208 = 0;
        F.ret = t5;
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
__attribute__((aligned(4))) void gm_f_vc_patch_pbmmb3q(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_patch_pbmmb3q F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* at */
    uint32_t gm_l2; /* to */
    void * gm_l3; /* i */
    uint32_t gm_l4; /* r */
    uint32_t gm_l5; /* r */
    gm_tpkdfv1vgiqij gm_l6; /* x */
    gm_tnkql1rjn1vbj gm_l7; /* x */
    gm_tufm7evi61pnh gm_l8; /* x */
    gm_t37c2jujtiadn gm_l9; /* x */
    gm_tjqgc9rpvqnkt gm_l10; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 1110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t3 = gm_l2;
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 264));
        uint8_t t6 = (t3 > t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            uint32_t t9 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t8) + 264), t9);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t10 = gm_l1;
        uint64_t t11 = (uint64_t)t10;
        uint64_t t12 = (uintptr_t)t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 16) + 0) + 8);
        if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcutmnmd444bu0, 164, 11);
        void * t15 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 16) + 0)) + t12 * 56);
        gm_l3 = t15;
    }
    {
        void * t16 = gm_l3;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma26; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma26; case ((uint8_t)28ULL): goto ma22; case ((uint8_t)29ULL): goto ma23; case ((uint8_t)30ULL): goto ma26; case ((uint8_t)31ULL): goto ma26; case ((uint8_t)32ULL): goto ma26; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma26; case ((uint8_t)35ULL): goto ma26; case ((uint8_t)36ULL): goto ma26; case ((uint8_t)37ULL): goto ma26; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma19; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma26; case ((uint8_t)42ULL): goto ma26; case ((uint8_t)43ULL): goto ma26; case ((uint8_t)44ULL): goto ma26; case ((uint8_t)45ULL): goto ma26; case ((uint8_t)46ULL): goto ma26; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma26; case ((uint8_t)49ULL): goto ma26; case ((uint8_t)50ULL): goto ma26; case ((uint8_t)51ULL): goto ma26; case ((uint8_t)52ULL): goto ma26; case ((uint8_t)53ULL): goto ma26; case ((uint8_t)54ULL): goto ma26; case ((uint8_t)55ULL): goto ma24; case ((uint8_t)56ULL): goto ma26; case ((uint8_t)57ULL): goto ma26; case ((uint8_t)58ULL): goto ma26; case ((uint8_t)59ULL): goto ma26; case ((uint8_t)60ULL): goto ma25; case ((uint8_t)61ULL): goto ma26; case ((uint8_t)62ULL): goto ma26; case ((uint8_t)63ULL): goto ma26; case ((uint8_t)64ULL): goto ma26; case ((uint8_t)65ULL): goto ma26; case ((uint8_t)66ULL): goto ma26; case ((uint8_t)67ULL): goto ma26; case ((uint8_t)68ULL): goto ma26; case ((uint8_t)69ULL): goto ma26; case ((uint8_t)70ULL): goto ma21; case ((uint8_t)71ULL): goto ma26; case ((uint8_t)72ULL): goto ma26; case ((uint8_t)73ULL): goto ma26; case ((uint8_t)74ULL): goto ma26; case ((uint8_t)75ULL): goto ma26; case ((uint8_t)76ULL): goto ma26; case ((uint8_t)77ULL): goto ma26; case ((uint8_t)78ULL): goto ma26; case ((uint8_t)79ULL): goto ma26; case ((uint8_t)80ULL): goto ma26; case ((uint8_t)81ULL): goto ma26; case ((uint8_t)82ULL): goto ma26; case ((uint8_t)83ULL): goto ma26; case ((uint8_t)84ULL): goto ma26; default: goto ma26; }
ma18:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t30 != ((uint8_t)38ULL)) goto mn29;
        {
            void * t32 = gm_l3;
            uint32_t t33 = gm_l2;
            gm_tjc2ga47o3k6g t34;
            memset(&t34, 0, sizeof t34);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)38ULL));
            GM_ST(uint32_t, ((uint8_t *)&t34 + 8), t33);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t32), t34);
        }
        goto me17;
mn29:;
ma19:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t36 != ((uint8_t)39ULL)) goto mn35;
        uint32_t t37 = GM_LD(uint32_t, ((((uint8_t *)t16) + 8) + 0));
        gm_l4 = t37;
        {
            void * t39 = gm_l3;
            uint32_t t40 = gm_l4;
            uint32_t t41 = gm_l2;
            gm_tcc1rq4n1avb2 t42;
            GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t40);
            GM_ST(uint32_t, ((uint8_t *)&t42 + 4), t41);
            gm_tjc2ga47o3k6g t43;
            memset(&t43, 0, sizeof t43);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)39ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t43 + 8), t42);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t39), t43);
        }
        goto me17;
mn35:;
ma20:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t45 != ((uint8_t)40ULL)) goto mn44;
        uint32_t t46 = GM_LD(uint32_t, ((((uint8_t *)t16) + 8) + 0));
        gm_l5 = t46;
        {
            void * t48 = gm_l3;
            uint32_t t49 = gm_l5;
            uint32_t t50 = gm_l2;
            gm_tcc1rq4n1avb2 t51;
            GM_ST(uint32_t, ((uint8_t *)&t51 + 0), t49);
            GM_ST(uint32_t, ((uint8_t *)&t51 + 4), t50);
            gm_tjc2ga47o3k6g t52;
            memset(&t52, 0, sizeof t52);
            GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)40ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t52 + 8), t51);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t48), t52);
        }
        goto me17;
mn44:;
ma21:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t54 != ((uint8_t)70ULL)) goto mn53;
        gm_tpkdfv1vgiqij t55 = GM_LD(gm_tpkdfv1vgiqij, (((uint8_t *)t16) + 8));
        gm_l6 = t55;
        {
            void * t57 = gm_l3;
            gm_tpkdfv1vgiqij t58;
            memset((uint8_t *)&t58 + 20, 0, 4);
            void * t59 = GM_LD(void *, (((uint8_t *)&gm_l6) + 0));
            GM_ST(void *, (uint8_t *)&t58 + 0, t59);
            uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            GM_ST(uint32_t, (uint8_t *)&t58 + 8, t60);
            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 12));
            GM_ST(uint32_t, (uint8_t *)&t58 + 12, t61);
            uint32_t t62 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t58 + 16, t62);
            gm_tjc2ga47o3k6g t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t63 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)70ULL));
            GM_ST(gm_tpkdfv1vgiqij, ((uint8_t *)&t63 + 8), t58);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t57), t63);
        }
        goto me17;
mn53:;
ma22:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t65 != ((uint8_t)28ULL)) goto mn64;
        gm_tnkql1rjn1vbj t66 = GM_LD(gm_tnkql1rjn1vbj, (((uint8_t *)t16) + 8));
        gm_l7 = t66;
        {
            void * t68 = gm_l3;
            gm_tnkql1rjn1vbj t69;
            memset((uint8_t *)&t69 + 2, 0, 2);
            gm_ts6oqppjh0ba3 t70 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l7) + 0));
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t69 + 0, t70);
            gm_tmn7n0n9raou5 t71 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l7) + 4));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t69 + 4, t71);
            uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 12));
            GM_ST(uint32_t, (uint8_t *)&t69 + 12, t72);
            uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 16));
            GM_ST(uint32_t, (uint8_t *)&t69 + 16, t73);
            uint32_t t74 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t69 + 20, t74);
            gm_tjc2ga47o3k6g t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t75 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)28ULL));
            GM_ST(gm_tnkql1rjn1vbj, ((uint8_t *)&t75 + 8), t69);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t68), t75);
        }
        goto me17;
mn64:;
ma23:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t77 != ((uint8_t)29ULL)) goto mn76;
        gm_tufm7evi61pnh t78 = GM_LD(gm_tufm7evi61pnh, (((uint8_t *)t16) + 8));
        gm_l8 = t78;
        {
            void * t80 = gm_l3;
            gm_tufm7evi61pnh t81;
            memset((uint8_t *)&t81 + 9, 0, 3);
            gm_tmn7n0n9raou5 t82 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l8) + 0));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t81 + 0, t82);
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
            GM_ST(uint8_t, (uint8_t *)&t81 + 8, t83);
            uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 12));
            GM_ST(uint32_t, (uint8_t *)&t81 + 12, t84);
            uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 16));
            GM_ST(uint32_t, (uint8_t *)&t81 + 16, t85);
            uint32_t t86 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t81 + 20, t86);
            gm_tjc2ga47o3k6g t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)29ULL));
            GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t87 + 8), t81);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t80), t87);
        }
        goto me17;
mn76:;
ma24:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t89 != ((uint8_t)55ULL)) goto mn88;
        gm_t37c2jujtiadn t90 = GM_LD(gm_t37c2jujtiadn, (((uint8_t *)t16) + 8));
        gm_l9 = t90;
        {
            void * t92 = gm_l3;
            uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 0));
            uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 8));
            uint32_t t96 = gm_l2;
            gm_t37c2jujtiadn t97;
            GM_ST(uint8_t, (uint8_t *)&t97 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t97 + 10, 0);
            GM_ST(uint32_t, ((uint8_t *)&t97 + 0), t93);
            GM_ST(uint32_t, ((uint8_t *)&t97 + 4), t94);
            GM_ST(uint8_t, ((uint8_t *)&t97 + 8), t95);
            GM_ST(uint32_t, ((uint8_t *)&t97 + 12), t96);
            gm_tjc2ga47o3k6g t98;
            memset(&t98, 0, sizeof t98);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)55ULL));
            GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t98 + 8), t97);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t92), t98);
        }
        goto me17;
mn88:;
ma25:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)t16) + 0));
        if (t100 != ((uint8_t)60ULL)) goto mn99;
        gm_tjqgc9rpvqnkt t101 = GM_LD(gm_tjqgc9rpvqnkt, (((uint8_t *)t16) + 8));
        gm_l10 = t101;
        {
            void * t103 = gm_l3;
            gm_tjqgc9rpvqnkt t104;
            memset((uint8_t *)&t104 + 20, 0, 4);
            uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
            GM_ST(uint32_t, (uint8_t *)&t104 + 0, t105);
            uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
            GM_ST(uint32_t, (uint8_t *)&t104 + 4, t106);
            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            GM_ST(uint64_t, (uint8_t *)&t104 + 8, t107);
            uint32_t t108 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t104 + 16, t108);
            gm_tjc2ga47o3k6g t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)60ULL));
            GM_ST(gm_tjqgc9rpvqnkt, ((uint8_t *)&t109 + 8), t104);
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t103), t109);
        }
        goto me17;
mn99:;
ma26:;
        goto me17;
mn110:;
ma27:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 165, 5);
me17:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_payload_loquhre(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_t9805qtonbsag a3) {
    struct gm_fr_vc_payload_loquhre F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* pa */
    gm_t9805qtonbsag gm_l3; /* at */
    uint32_t gm_l4; /* d */
    GM_STACK_CHECK(&F, 378u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie2;
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l4 = t3;
        {
            void * t6 = gm_l1;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) goto ie5;
            {
                void * t10 = gm_l0;
                uint32_t t11 = gm_l4;
                uint32_t t12 = gm_l2;
                void * t13 = gm_l1;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 104));
                uint32_t t15 = (uint32_t)t14;
                gm_tl874t2atnlh3 t16;
                GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t11);
                GM_ST(uint32_t, ((uint8_t *)&t16 + 4), t12);
                GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
                gm_tjc2ga47o3k6g t17;
                memset(&t17, 0, sizeof t17);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)52ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t17 + 8), t16);
                gm_b0_1 t18 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t10, &t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 104));
        uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
        if (!t22) goto ie19;
        {
            void * t24 = gm_l0;
            void * t25 = gm_l0;
            uint32_t t26 = gm_f_vc_alloc_3hmfvtt(t25);
            gm_tb31k5l794t7p t27;
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
            GM_ST(uint64_t, ((uint8_t *)&t27 + 8), ((uint64_t)0ULL));
            gm_tjc2ga47o3k6g t28;
            memset(&t28, 0, sizeof t28);
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t28 + 8), t27);
            gm_b0_1 t29 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t24, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix19;
ie19:;
        {
            void * t31 = gm_l0;
            void * t32 = gm_l0;
            uint32_t t33 = gm_f_vc_alloc_3hmfvtt(t32);
            uint32_t t34 = gm_l2;
            void * t35 = gm_l1;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 104));
            uint32_t t37 = (uint32_t)t36;
            gm_tl874t2atnlh3 t38;
            GM_ST(uint32_t, ((uint8_t *)&t38 + 0), t33);
            GM_ST(uint32_t, ((uint8_t *)&t38 + 4), t34);
            GM_ST(uint32_t, ((uint8_t *)&t38 + 8), t37);
            gm_tjc2ga47o3k6g t39;
            memset(&t39, 0, sizeof t39);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)53ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t39 + 8), t38);
            gm_b0_1 t40 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t31, &t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix19:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_perform_qci0ith(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tj2m9dbh1ekv6 a2, void * a3) {
    struct gm_fr_vc_perform_qci0ith F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* args */
    gm_tj2m9dbh1ekv6 gm_l2; /* dst */
    void * gm_l3; /* n */
    uint32_t gm_l4; /* save */
    gm_b16_8 gm_l5; /* out */
    GM_STACK_CHECK(&F, 274u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        gm_b16_8 t6 = gm_f_vc_args_j2k3nul((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_tbeu0qv6bbs2s t8;
        gm_b16_8 t9 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
        gm_tj2m9dbh1ekv6 t10 = gm_l2;
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t8 + 16, t10);
        void * t11 = gm_l3;
        GM_ST(void *, (uint8_t *)&t8 + 24, t11);
        gm_tjc2ga47o3k6g t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t12 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t12 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)50ULL));
        GM_ST(gm_tbeu0qv6bbs2s, ((uint8_t *)&t12 + 8), t8);
        gm_b0_1 t13 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t7, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        uint32_t t15;
        uint8_t t17;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        t17 = ((uint8_t)1);
        goto me18;
mn19:;
        t17 = ((uint8_t)0);
        goto me18;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1060, 21);
me18:;
        if (!t17) goto ie16;
        uint32_t t23 = gm_l4;
        uint32_t r24; bool o25 = __builtin_add_overflow(t23, ((uint32_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1060, 38);
        uint32_t t26 = r24;
        uint32_t t27 = t26;
        t15 = t27;
        goto ix16;
ie16:;
        uint32_t t29 = gm_l4;
        uint32_t t30 = t29;
        t15 = t30;
ix16:;
        gm_f_vc_set_top_h0qmvd1(t14, t15);
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
__attribute__((aligned(4))) void gm_f_vc_place_i1sksai(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_place_i1sksai F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* l */
    uint32_t gm_l3; /* r */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* l */
    gm_t1j70din2upc4 gm_l6; /* x */
    uint32_t gm_l7; /* levels */
    uint32_t gm_l8; /* l */
    uint64_t gm_l9; /* rel */
    uint8_t gm_l10; /* packed */
    gm_t9805qtonbsag gm_l11; /* d */
    uint32_t gm_l12; /* r */
    uint64_t gm_l13; /* off */
    uint8_t gm_l14; /* packed */
    void * gm_l15; /* ptr */
    uint32_t gm_l16; /* r */
    uint32_t gm_l19; /* rp */
    uint32_t gm_l20; /* rl */
    uint64_t gm_l21; /* pb */
    uint32_t gm_l22; /* ri */
    uint32_t gm_l23; /* rp */
    uint32_t gm_l24; /* ri */
    uint32_t gm_l25; /* rp */
    uint64_t gm_l26; /* off */
    uint8_t gm_l27; /* packed */
    uint32_t gm_l28; /* ri */
    uint32_t gm_l29; /* levels */
    uint32_t gm_l30; /* l */
    uint64_t gm_l31; /* rel */
    uint8_t gm_l32; /* packed */
    gm_t9805qtonbsag gm_l33; /* d */
    uint32_t gm_l34; /* ri */
    uint32_t gm_l35; /* dr */
    GM_STACK_CHECK(&F, 2826u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; case ((uint8_t)27ULL): goto ma9; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma9; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma9; case ((uint8_t)32ULL): goto ma9; case ((uint8_t)33ULL): goto ma9; case ((uint8_t)34ULL): goto ma9; case ((uint8_t)35ULL): goto ma9; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma9; case ((uint8_t)41ULL): goto ma9; case ((uint8_t)42ULL): goto ma9; case ((uint8_t)43ULL): goto ma9; case ((uint8_t)44ULL): goto ma9; case ((uint8_t)45ULL): goto ma9; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma9; case ((uint8_t)48ULL): goto ma9; case ((uint8_t)49ULL): goto ma9; case ((uint8_t)50ULL): goto ma9; case ((uint8_t)51ULL): goto ma9; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma9; case ((uint8_t)54ULL): goto ma9; case ((uint8_t)55ULL): goto ma9; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma9; case ((uint8_t)59ULL): goto ma9; case ((uint8_t)60ULL): goto ma9; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma9; default: goto ma9; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t13 != ((uint8_t)6ULL)) goto mn12;
        uint32_t t14 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l2 = t14;
        {
            uint32_t t17 = gm_l2;
            uint64_t t18 = (uint64_t)t17;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
            uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 120) + 8);
            if (t19 >= (uintptr_t)(t22)) gm_trap_index(t19, t22, gm_srcutmnmd444bu0, 895, 16);
            uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 120)) + t19 * 1));
            if (!t23) goto ie16;
            {
                void * t25 = gm_l0;
                uint32_t t26 = gm_f_vc_alloc_3hmfvtt(t25);
                gm_l3 = t26;
            }
            {
                void * t27 = gm_l0;
                uint32_t t28 = gm_l3;
                uint32_t t29 = gm_l2;
                void * t30 = gm_l1;
                gm_to8e1ootf3q7f t31;
                GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t28);
                GM_ST(uint32_t, ((uint8_t *)&t31 + 4), t29);
                GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
                gm_tjc2ga47o3k6g t32;
                memset(&t32, 0, sizeof t32);
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)59ULL));
                GM_ST(gm_to8e1ootf3q7f, ((uint8_t *)&t32 + 8), t31);
                gm_b0_1 t33 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t27, &t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t2cg11r39sm0e t34;
                memset((uint8_t *)&t34 + 4, 0, 4);
                memset((uint8_t *)&t34 + 17, 0, 7);
                uint32_t t35 = gm_l3;
                GM_ST(uint32_t, (uint8_t *)&t34 + 0, t35);
                GM_ST(uint64_t, (uint8_t *)&t34 + 8, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&t34 + 16, ((uint8_t)0));
                gm_tiutk3b96cnlp t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t36 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t36 + 8), t34);
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
            goto ix16;
ie16:;
ix16:;
        }
        {
            gm_t6r4nedrp9ovi t38;
            memset((uint8_t *)&t38 + 17, 0, 3);
            memset((uint8_t *)&t38 + 28, 0, 4);
            GM_ST(uint32_t, (uint8_t *)&t38 + 0, ((uint32_t)0ULL));
            uint32_t t39 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, t39);
            GM_ST(uint64_t, (uint8_t *)&t38 + 8, ((uint64_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&t38 + 16, ((uint8_t)0));
            gm_t9805qtonbsag t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&t38 + 20, t40);
            gm_tiutk3b96cnlp t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t6r4nedrp9ovi, ((uint8_t *)&t41 + 8), t38);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto me3;
mn12:;
ma5:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t44 != ((uint8_t)7ULL)) goto mn43;
        uint32_t t45 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l4 = t45;
        uint32_t t46 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 4));
        gm_l5 = t46;
        gm_t6r4nedrp9ovi t47;
        memset((uint8_t *)&t47 + 17, 0, 3);
        memset((uint8_t *)&t47 + 28, 0, 4);
        uint32_t t48 = gm_l4;
        GM_ST(uint32_t, (uint8_t *)&t47 + 0, t48);
        uint32_t t49 = gm_l5;
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, t49);
        GM_ST(uint64_t, (uint8_t *)&t47 + 8, ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&t47 + 16, ((uint8_t)0));
        gm_t9805qtonbsag t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t47 + 20, t50);
        gm_tiutk3b96cnlp t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t6r4nedrp9ovi, ((uint8_t *)&t51 + 8), t47);
        uint8_t t52 = 0;
        F.ret = t51;
        goto gm_fret;
        goto me3;
mn43:;
ma6:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t54 != ((uint8_t)10ULL)) goto mn53;
        gm_t1j70din2upc4 t55 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t2) + 0) + 8));
        gm_l6 = t55;
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, (((uint8_t *)&gm_l6) + 0));
        gm_tiutk3b96cnlp t58;
        gm_f_vc_place_i1sksai((void *)&t58, (void *)NULL, cx, NULL, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tiutk3b96cnlp t59 = t58;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
        if (t62 != ((uint8_t)0ULL)) goto mn61;
        uint32_t t63 = GM_LD(uint32_t, ((((uint8_t *)&t59) + 8) + 0));
        gm_l7 = t63;
        uint32_t t64 = GM_LD(uint32_t, ((((uint8_t *)&t59) + 8) + 4));
        gm_l8 = t64;
        uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)&t59) + 8) + 8));
        gm_l9 = t65;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)&t59) + 8) + 16));
        gm_l10 = t66;
        gm_t9805qtonbsag t67 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)&t59) + 8) + 20));
        gm_l11 = t67;
        gm_t6r4nedrp9ovi t68;
        memset((uint8_t *)&t68 + 17, 0, 3);
        memset((uint8_t *)&t68 + 28, 0, 4);
        uint32_t t69 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t68 + 0, t69);
        uint32_t t70 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, t70);
        uint64_t t71 = gm_l9;
        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
        uint64_t r73; bool o74 = __builtin_add_overflow(t71, t72, &r73);
        if (o74) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 904, 76);
        uint64_t t75 = r73;
        GM_ST(uint64_t, (uint8_t *)&t68 + 8, t75);
        uint8_t t76 = 0;
        uint8_t t77 = gm_l10;
        if (!t77) {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
            t76 = t78;
        } else t76 = 1;
        GM_ST(uint8_t, (uint8_t *)&t68 + 16, t76);
        gm_t9805qtonbsag t79 = gm_l11;
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t68 + 20, t79);
        gm_tiutk3b96cnlp t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t6r4nedrp9ovi, ((uint8_t *)&t80 + 8), t68);
        uint8_t t81 = 0;
        F.ret = t80;
        goto gm_fret;
        goto me60;
mn61:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
        if (t83 != ((uint8_t)1ULL)) goto mn82;
        uint32_t t84 = GM_LD(uint32_t, ((((uint8_t *)&t59) + 8) + 0));
        gm_l12 = t84;
        uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)&t59) + 8) + 8));
        gm_l13 = t85;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)&t59) + 8) + 16));
        gm_l14 = t86;
        gm_t2cg11r39sm0e t87;
        memset((uint8_t *)&t87 + 4, 0, 4);
        memset((uint8_t *)&t87 + 17, 0, 7);
        uint32_t t88 = gm_l12;
        GM_ST(uint32_t, (uint8_t *)&t87 + 0, t88);
        uint64_t t89 = gm_l13;
        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
        uint64_t r91; bool o92 = __builtin_add_overflow(t89, t90, &r91);
        if (o92) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 905, 52);
        uint64_t t93 = r91;
        GM_ST(uint64_t, (uint8_t *)&t87 + 8, t93);
        uint8_t t94 = 0;
        uint8_t t95 = gm_l14;
        if (!t95) {
            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
            t94 = t96;
        } else t94 = 1;
        GM_ST(uint8_t, (uint8_t *)&t87 + 16, t94);
        gm_tiutk3b96cnlp t97;
        GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t97 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t97 + 8), t87);
        uint8_t t98 = 0;
        F.ret = t97;
        goto gm_fret;
        goto me60;
mn82:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 903, 22);
me60:;
        goto me3;
mn53:;
ma7:;
        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t100 != ((uint8_t)9ULL)) goto mn99;
        void * t101 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l15 = t101;
        {
            void * t103 = gm_l0;
            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)t103) + 224));
            gm_l16 = t104;
        }
        {
            void * t105 = gm_l0;
            void * t106 = gm_l15;
            gm_b0_1 t107 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t2cg11r39sm0e t108;
            memset((uint8_t *)&t108 + 4, 0, 4);
            memset((uint8_t *)&t108 + 17, 0, 7);
            uint32_t t109 = gm_l16;
            GM_ST(uint32_t, (uint8_t *)&t108 + 0, t109);
            GM_ST(uint64_t, (uint8_t *)&t108 + 8, ((uint64_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&t108 + 16, ((uint8_t)0));
            gm_tiutk3b96cnlp t110;
            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t110 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t110 + 8), t108);
            uint8_t t111 = 0;
            F.ret = t110;
            goto gm_fret;
        }
        goto me3;
mn99:;
ma8:;
        uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t113 != ((uint8_t)12ULL)) goto mn112;
        gm_tp0fa5qv7rqnn t114 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t2) + 0) + 8));
        F.l17 = t114;
        {
            uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 16) + 0));
            switch (t121) { case ((uint8_t)0ULL): goto ma119; case ((uint8_t)1ULL): goto ma117; case ((uint8_t)2ULL): goto ma118; case ((uint8_t)3ULL): goto ma118; default: goto ma120; }
ma117:;
            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 16) + 0));
            if (t123 != ((uint8_t)1ULL)) goto mn122;
            {
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)&F.l17) + 0));
                gm_f_vc_place_i1sksai((void *)&F.l18, (void *)NULL, cx, NULL, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t127 = gm_l0;
                uint32_t t128 = gm_f_vc_alloc_3hmfvtt(t127);
                gm_l19 = t128;
            }
            {
                void * t129 = gm_l0;
                uint32_t t130 = gm_f_vc_alloc_3hmfvtt(t129);
                gm_l20 = t130;
            }
            {
                void * t131 = gm_l0;
                void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
                uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)t132) + 176));
                gm_l21 = t133;
            }
            {
                void * t134 = gm_l0;
                uint32_t t135 = gm_l19;
                gm_tiutk3b96cnlp t136 = F.l18;
                uint64_t t137 = gm_l21;
                uint64_t t138 = gm_l21;
                uint32_t t139 = (uint32_t)t138;
                void * t140 = GM_LD(void *, (((uint8_t *)&F.l17) + 0));
                gm_b0_1 t141 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t134, t135, &t136, ((uint64_t)0ULL), t137, t139, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t142 = gm_l0;
                uint32_t t143 = gm_l20;
                gm_tiutk3b96cnlp t144 = F.l18;
                uint64_t t145 = gm_l21;
                uint64_t t146 = gm_l21;
                uint64_t t147 = gm_l21;
                uint32_t t148 = (uint32_t)t147;
                void * t149 = GM_LD(void *, (((uint8_t *)&F.l17) + 0));
                gm_b0_1 t150 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t142, t143, &t144, t145, t146, t148, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t151 = gm_l0;
                void * t152 = GM_LD(void *, (((uint8_t *)&F.l17) + 8));
                uint32_t t153 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t151, t152, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t153;
            }
            {
                void * t154 = gm_l0;
                gm_t3a21dm0j52qn t155;
                memset((uint8_t *)&t155 + 12, 0, 4);
                uint32_t t156 = gm_l19;
                GM_ST(uint32_t, (uint8_t *)&t155 + 0, t156);
                uint32_t t157 = gm_l20;
                GM_ST(uint32_t, (uint8_t *)&t155 + 4, t157);
                uint32_t t158 = gm_l22;
                GM_ST(uint32_t, (uint8_t *)&t155 + 8, t158);
                uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 24));
                GM_ST(uint64_t, (uint8_t *)&t155 + 16, t159);
                void * t160 = gm_l1;
                GM_ST(void *, (uint8_t *)&t155 + 24, t160);
                gm_tjc2ga47o3k6g t161;
                GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t161 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t161 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)19ULL));
                GM_ST(gm_t3a21dm0j52qn, ((uint8_t *)&t161 + 8), t155);
                gm_b0_1 t162 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t154, &t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_t2cg11r39sm0e t163;
                memset((uint8_t *)&t163 + 4, 0, 4);
                memset((uint8_t *)&t163 + 17, 0, 7);
                uint32_t t164 = gm_l19;
                GM_ST(uint32_t, (uint8_t *)&t163 + 0, t164);
                GM_ST(uint64_t, (uint8_t *)&t163 + 8, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&t163 + 16, ((uint8_t)0));
                gm_tiutk3b96cnlp t165;
                GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t165 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t165 + 8), t163);
                uint8_t t166 = 0;
                F.ret = t165;
                goto gm_fret;
            }
            goto me116;
mn122:;
ma118:;
            uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 16) + 0));
            if (t170 != ((uint8_t)2ULL)) goto pa169;
            goto po168;
pa169:;
            uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 16) + 0));
            if (t172 != ((uint8_t)3ULL)) goto pa171;
            goto po168;
pa171:;
            goto mn167;
po168:;
            {
                void * t174 = gm_l0;
                uint32_t t175 = GM_LD(uint32_t, (((uint8_t *)t174) + 224));
                gm_l23 = t175;
            }
            {
                void * t176 = gm_l0;
                void * t177 = GM_LD(void *, (((uint8_t *)&F.l17) + 0));
                gm_b0_1 t178 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t176, t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t179 = gm_l0;
                void * t180 = GM_LD(void *, (((uint8_t *)&F.l17) + 8));
                uint32_t t181 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t179, t180, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t181;
            }
            {
                uint8_t t183 = ((GM_LD(uint8_t, (((uint8_t *)&F.l17) + 16) + 0) == ((uint8_t)2ULL)));
                if (!t183) goto ie182;
                {
                    void * t185 = gm_l0;
                    uint32_t t186 = gm_l23;
                    uint32_t t187 = gm_l24;
                    uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 24));
                    gm_t4h1llua034m2 t189;
                    GM_ST(uint32_t, ((uint8_t *)&t189 + 0), t186);
                    GM_ST(uint32_t, ((uint8_t *)&t189 + 4), t187);
                    GM_ST(uint64_t, ((uint8_t *)&t189 + 8), t188);
                    gm_tjc2ga47o3k6g t190;
                    memset(&t190, 0, sizeof t190);
                    GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_t4h1llua034m2, ((uint8_t *)&t190 + 8), t189);
                    gm_b0_1 t191 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t185, &t190);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix182;
ie182:;
                {
                    void * t193 = gm_l0;
                    gm_tu97pd4fjd5oe t194;
                    uint32_t t195 = gm_l23;
                    GM_ST(uint32_t, (uint8_t *)&t194 + 0, t195);
                    uint32_t t196 = gm_l24;
                    GM_ST(uint32_t, (uint8_t *)&t194 + 4, t196);
                    uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 32));
                    GM_ST(uint64_t, (uint8_t *)&t194 + 8, t197);
                    uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 24));
                    GM_ST(uint64_t, (uint8_t *)&t194 + 16, t198);
                    void * t199 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t194 + 24, t199);
                    gm_tjc2ga47o3k6g t200;
                    GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t200 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t200 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tu97pd4fjd5oe, ((uint8_t *)&t200 + 8), t194);
                    gm_b0_1 t201 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t193, &t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix182:;
            }
            {
                gm_t2cg11r39sm0e t202;
                memset((uint8_t *)&t202 + 4, 0, 4);
                memset((uint8_t *)&t202 + 17, 0, 7);
                uint32_t t203 = gm_l23;
                GM_ST(uint32_t, (uint8_t *)&t202 + 0, t203);
                GM_ST(uint64_t, (uint8_t *)&t202 + 8, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&t202 + 16, ((uint8_t)0));
                gm_tiutk3b96cnlp t204;
                GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t204 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t204 + 8), t202);
                uint8_t t205 = 0;
                F.ret = t204;
                goto gm_fret;
            }
            goto me116;
mn167:;
ma119:;
            uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)&F.l17) + 16) + 0));
            if (t207 != ((uint8_t)0ULL)) goto mn206;
            {
                void * t209 = gm_l0;
                void * t210 = GM_LD(void *, (((uint8_t *)&F.l17) + 0));
                gm_tiutk3b96cnlp t211;
                gm_f_vc_place_i1sksai((void *)&t211, (void *)NULL, cx, NULL, t209, t210);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tiutk3b96cnlp t212 = t211;
                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
                if (t215 != ((uint8_t)1ULL)) goto mn214;
                uint32_t t216 = GM_LD(uint32_t, ((((uint8_t *)&t212) + 8) + 0));
                gm_l25 = t216;
                uint64_t t217 = GM_LD(uint64_t, ((((uint8_t *)&t212) + 8) + 8));
                gm_l26 = t217;
                uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)&t212) + 8) + 16));
                gm_l27 = t218;
                {
                    uint64_t t221 = gm_l26;
                    uint8_t t222 = (!(t221 == ((uint64_t)0ULL)));
                    if (!t222) goto ie220;
                    {
                        void * t224 = gm_l0;
                        uint32_t t225 = gm_l25;
                        uint64_t t226 = gm_l26;
                        gm_tb31k5l794t7p t227;
                        GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t227 + 0), t225);
                        GM_ST(uint64_t, ((uint8_t *)&t227 + 8), t226);
                        gm_tjc2ga47o3k6g t228;
                        memset(&t228, 0, sizeof t228);
                        GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)24ULL));
                        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t228 + 8), t227);
                        gm_b0_1 t229 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t224, &t228);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix220;
ie220:;
ix220:;
                }
                {
                    void * t230 = gm_l0;
                    void * t231 = GM_LD(void *, (((uint8_t *)&F.l17) + 8));
                    uint32_t t232 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t230, t231, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t232;
                }
                {
                    void * t233 = gm_l0;
                    gm_tu97pd4fjd5oe t234;
                    uint32_t t235 = gm_l25;
                    GM_ST(uint32_t, (uint8_t *)&t234 + 0, t235);
                    uint32_t t236 = gm_l28;
                    GM_ST(uint32_t, (uint8_t *)&t234 + 4, t236);
                    uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 32));
                    GM_ST(uint64_t, (uint8_t *)&t234 + 8, t237);
                    uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 24));
                    GM_ST(uint64_t, (uint8_t *)&t234 + 16, t238);
                    void * t239 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t234 + 24, t239);
                    gm_tjc2ga47o3k6g t240;
                    GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t240 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t240 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t240 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_tu97pd4fjd5oe, ((uint8_t *)&t240 + 8), t234);
                    gm_b0_1 t241 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t233, &t240);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t2cg11r39sm0e t242;
                    memset((uint8_t *)&t242 + 4, 0, 4);
                    memset((uint8_t *)&t242 + 17, 0, 7);
                    uint32_t t243 = gm_l25;
                    GM_ST(uint32_t, (uint8_t *)&t242 + 0, t243);
                    GM_ST(uint64_t, (uint8_t *)&t242 + 8, ((uint64_t)0ULL));
                    uint8_t t244 = gm_l27;
                    GM_ST(uint8_t, (uint8_t *)&t242 + 16, t244);
                    gm_tiutk3b96cnlp t245;
                    GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t245 + 32, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_t2cg11r39sm0e, ((uint8_t *)&t245 + 8), t242);
                    uint8_t t246 = 0;
                    F.ret = t245;
                    goto gm_fret;
                }
                goto me213;
mn214:;
                uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&t212) + 0));
                if (t248 != ((uint8_t)0ULL)) goto mn247;
                uint32_t t249 = GM_LD(uint32_t, ((((uint8_t *)&t212) + 8) + 0));
                gm_l29 = t249;
                uint32_t t250 = GM_LD(uint32_t, ((((uint8_t *)&t212) + 8) + 4));
                gm_l30 = t250;
                uint64_t t251 = GM_LD(uint64_t, ((((uint8_t *)&t212) + 8) + 8));
                gm_l31 = t251;
                uint8_t t252 = GM_LD(uint8_t, ((((uint8_t *)&t212) + 8) + 16));
                gm_l32 = t252;
                gm_t9805qtonbsag t253 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)&t212) + 8) + 20));
                gm_l33 = t253;
                {
                    void * t255 = gm_l0;
                    uint32_t t256 = GM_LD(uint32_t, (((uint8_t *)t255) + 224));
                    gm_l34 = t256;
                }
                {
                    void * t257 = gm_l0;
                    void * t258 = GM_LD(void *, (((uint8_t *)&F.l17) + 8));
                    gm_b0_1 t259 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t257, t258);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t260 = gm_l0;
                    gm_tg0sdqqpruvqq t261;
                    memset((uint8_t *)&t261 + 4, 0, 4);
                    uint32_t t262 = gm_l34;
                    GM_ST(uint32_t, (uint8_t *)&t261 + 0, t262);
                    uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 32));
                    GM_ST(uint64_t, (uint8_t *)&t261 + 8, t263);
                    uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 24));
                    GM_ST(uint64_t, (uint8_t *)&t261 + 16, t264);
                    void * t265 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t261 + 24, t265);
                    gm_tjc2ga47o3k6g t266;
                    GM_ST(uint8_t, (uint8_t *)&t266 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t266 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t266 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t266 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t266 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)8ULL));
                    GM_ST(gm_tg0sdqqpruvqq, ((uint8_t *)&t266 + 8), t261);
                    gm_b0_1 t267 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t260, &t266);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    if (!((((uint8_t *)&gm_l33))[4] != 0)) goto ie268;
                    uint32_t t269 = GM_LD(uint32_t, ((uint8_t *)&gm_l33));
                    gm_l35 = t269;
                    {
                        void * t271 = gm_l0;
                        uint32_t t272 = gm_l35;
                        uint32_t t273 = gm_l34;
                        gm_tcc1rq4n1avb2 t274;
                        GM_ST(uint32_t, ((uint8_t *)&t274 + 0), t272);
                        GM_ST(uint32_t, ((uint8_t *)&t274 + 4), t273);
                        gm_tjc2ga47o3k6g t275;
                        memset(&t275, 0, sizeof t275);
                        GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)9ULL));
                        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t275 + 8), t274);
                        gm_b0_1 t276 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t271, &t275);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_t6r4nedrp9ovi t277;
                        memset((uint8_t *)&t277 + 17, 0, 3);
                        memset((uint8_t *)&t277 + 28, 0, 4);
                        uint32_t t278 = gm_l29;
                        GM_ST(uint32_t, (uint8_t *)&t277 + 0, t278);
                        uint32_t t279 = gm_l30;
                        GM_ST(uint32_t, (uint8_t *)&t277 + 4, t279);
                        uint64_t t280 = gm_l31;
                        GM_ST(uint64_t, (uint8_t *)&t277 + 8, t280);
                        uint8_t t281 = gm_l32;
                        GM_ST(uint8_t, (uint8_t *)&t277 + 16, t281);
                        uint32_t t282 = gm_l35;
                        gm_t9805qtonbsag t283;
                        GM_ST(uint8_t, (uint8_t *)&t283 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t283 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t283), t282);
                        ((uint8_t *)&t283)[4] = 1;
                        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t277 + 20, t283);
                        gm_tiutk3b96cnlp t284;
                        GM_ST(uint8_t, (uint8_t *)&t284 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t284 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t284 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t6r4nedrp9ovi, ((uint8_t *)&t284 + 8), t277);
                        uint8_t t285 = 0;
                        F.ret = t284;
                        goto gm_fret;
                    }
                    goto ix268;
ie268:;
ix268:;
                }
                {
                    gm_t6r4nedrp9ovi t286;
                    memset((uint8_t *)&t286 + 17, 0, 3);
                    memset((uint8_t *)&t286 + 28, 0, 4);
                    uint32_t t287 = gm_l29;
                    GM_ST(uint32_t, (uint8_t *)&t286 + 0, t287);
                    uint32_t t288 = gm_l30;
                    GM_ST(uint32_t, (uint8_t *)&t286 + 4, t288);
                    uint64_t t289 = gm_l31;
                    GM_ST(uint64_t, (uint8_t *)&t286 + 8, t289);
                    uint8_t t290 = gm_l32;
                    GM_ST(uint8_t, (uint8_t *)&t286 + 16, t290);
                    uint32_t t291 = gm_l34;
                    gm_t9805qtonbsag t292;
                    GM_ST(uint8_t, (uint8_t *)&t292 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t292 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t292), t291);
                    ((uint8_t *)&t292)[4] = 1;
                    GM_ST(gm_t9805qtonbsag, (uint8_t *)&t286 + 20, t292);
                    gm_tiutk3b96cnlp t293;
                    GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t6r4nedrp9ovi, ((uint8_t *)&t293 + 8), t286);
                    uint8_t t294 = 0;
                    F.ret = t293;
                    goto gm_fret;
                }
                goto me213;
mn247:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 933, 21);
me213:;
            }
            goto me116;
mn206:;
ma120:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 913, 13);
me116:;
        }
        goto me3;
mn112:;
ma9:;
        gm_trap("reached `@unreachable()`", gm_srcutmnmd444bu0, 954, 14);
        goto me3;
mn295:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 892, 5);
me3:;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_vc_place_reg_e0pc35g(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_place_reg_e0pc35g F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* l */
    gm_tb31k5l794t7p gm_l3; /* root */
    uint64_t gm_l4; /* off */
    void * gm_l5; /* x */
    gm_tcc1rq4n1avb2 gm_l6; /* r */
    void * gm_l7; /* x */
    gm_tcc1rq4n1avb2 gm_l8; /* r */
    GM_STACK_CHECK(&F, 653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; case ((uint8_t)59ULL): goto ma8; case ((uint8_t)60ULL): goto ma8; case ((uint8_t)61ULL): goto ma8; case ((uint8_t)62ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t12 != ((uint8_t)6ULL)) goto mn11;
        uint32_t t13 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l2 = t13;
        void * t15 = gm_l1;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 104));
        uint32_t t17 = gm_l2;
        uint64_t t18 = (uint64_t)t17;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 88) + 8);
        if (t19 >= (uintptr_t)(t22)) gm_trap_index(t19, t22, gm_srcutmnmd444bu0, 215, 38);
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 88)) + t19 * 8));
        uint8_t t24 = ((t16 == t23));
        if (!t24) goto ie14;
        {
            void * t26 = gm_l0;
            uint32_t t27 = gm_l2;
            gm_t9805qtonbsag t28 = gm_f_vc_local_reg_bgdr42m(t26, t27);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
        goto me3;
mn11:;
ma5:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t31 != ((uint8_t)10ULL)) goto mn30;
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t34) + 90));
        if (!t35) goto ie32;
        void * t36 = gm_l1;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 104));
        uint8_t t38 = ((t37 == ((uint64_t)8ULL)));
        if (!t38) goto ie32;
        {
            void * t40 = gm_l0;
            void * t41 = gm_l1;
            gm_tgmjda3f8g6hg t42;
            gm_f_vc_field_root_lb3os0t((void *)&t42, t40, t41);
            gm_tgmjda3f8g6hg t43 = t42;
            gm_tb31k5l794t7p t44;
            if (((((uint8_t *)&t43))[16] != 0)) {
                gm_tb31k5l794t7p t45 = GM_LD(gm_tb31k5l794t7p, (((uint8_t *)&t43) + 0));
                t44 = t45;
            } else {
                gm_t9805qtonbsag t46;
                memset(&t46, 0, sizeof t46);
                uint8_t t47 = 0;
                F.ret = t46;
                goto gm_fret;
            }
            gm_l3 = t44;
        }
        {
            uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t50 = (uint64_t)t49;
            uint64_t t51 = (uintptr_t)t50;
            void * t52 = gm_l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 8));
            uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)t53) + 120) + 8);
            if (t51 >= (uintptr_t)(t54)) gm_trap_index(t51, t54, gm_srcutmnmd444bu0, 218, 16);
            uint8_t t55 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t53) + 120)) + t51 * 1));
            if (!t55) goto ie48;
            {
                gm_t9805qtonbsag t57;
                memset(&t57, 0, sizeof t57);
                uint8_t t58 = 0;
                F.ret = t57;
                goto gm_fret;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t60 = (uint64_t)t59;
            uint64_t t61 = (uintptr_t)t60;
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 8));
            uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)t63) + 72) + 8);
            if (t61 >= (uintptr_t)(t64)) gm_trap_index(t61, t64, gm_srcutmnmd444bu0, 219, 20);
            uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t63) + 72)) + t61 * 8));
            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t r67; bool o68 = __builtin_add_overflow(t65, t66, &r67);
            if (o68) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 219, 20);
            uint64_t t69 = r67;
            gm_l4 = t69;
        }
        {
            uint64_t t71 = gm_l4;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 220, 16);
            uint64_t t72 = (uint64_t)(t71 % ((uint64_t)8ULL));
            uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
            if (!t73) goto ie70;
            {
                uint64_t t75 = gm_l4;
                if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 220, 39);
                uint64_t t76 = (uint64_t)(t75 / ((uint64_t)8ULL));
                uint32_t t77 = (uint32_t)t76;
                gm_t9805qtonbsag t78;
                GM_ST(uint8_t, (uint8_t *)&t78 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t78), t77);
                ((uint8_t *)&t78)[4] = 1;
                uint8_t t79 = 0;
                F.ret = t78;
                goto gm_fret;
            }
            goto ix70;
ie70:;
ix70:;
        }
        goto ix32;
ie32:;
ix32:;
        goto me3;
mn30:;
ma6:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t81 != ((uint8_t)14ULL)) goto mn80;
        void * t82 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l5 = t82;
        void * t84 = gm_l0;
        void * t85 = gm_l5;
        gm_t736djc66po0l t86 = gm_f_vc_slice_regs_en7lrqs(t84, t85);
        gm_t736djc66po0l t87 = t86;
        if (!((((uint8_t *)&t87))[8] != 0)) goto ie83;
        gm_tcc1rq4n1avb2 t88 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t87));
        gm_l6 = t88;
        {
            uint32_t t90 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            gm_t9805qtonbsag t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t91), t90);
            ((uint8_t *)&t91)[4] = 1;
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix83;
ie83:;
ix83:;
        goto me3;
mn80:;
ma7:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t94 != ((uint8_t)15ULL)) goto mn93;
        void * t95 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l7 = t95;
        void * t97 = gm_l0;
        void * t98 = gm_l7;
        gm_t736djc66po0l t99 = gm_f_vc_slice_regs_en7lrqs(t97, t98);
        gm_t736djc66po0l t100 = t99;
        if (!((((uint8_t *)&t100))[8] != 0)) goto ie96;
        gm_tcc1rq4n1avb2 t101 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t100));
        gm_l8 = t101;
        {
            uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
            gm_t9805qtonbsag t104;
            GM_ST(uint8_t, (uint8_t *)&t104 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t104 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t104), t103);
            ((uint8_t *)&t104)[4] = 1;
            uint8_t t105 = 0;
            F.ret = t104;
            goto gm_fret;
        }
        goto ix96;
ie96:;
ix96:;
        goto me3;
mn93:;
ma8:;
        goto me3;
mn106:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 214, 5);
me3:;
    }
    {
        gm_t9805qtonbsag t108;
        memset(&t108, 0, sizeof t108);
        uint8_t t109 = 0;
        F.ret = t108;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_vc_place_word_only_b4oj6ia(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_place_word_only_b4oj6ia F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_t1j70din2upc4 gm_l2; /* x */
    void * gm_l3; /* ptr */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; case ((uint8_t)59ULL): goto ma8; case ((uint8_t)60ULL): goto ma8; case ((uint8_t)61ULL): goto ma8; case ((uint8_t)62ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t14 != ((uint8_t)6ULL)) goto pa13;
    goto po12;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t16 != ((uint8_t)7ULL)) goto pa15;
    goto po12;
pa15:;
    goto mn11;
po12:;
    t2 = ((uint8_t)1);
    goto me3;
mn11:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t18 != ((uint8_t)10ULL)) goto mn17;
    gm_t1j70din2upc4 t19 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t1) + 0) + 8));
    gm_l2 = t19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
    uint8_t t22 = gm_f_vc_place_word_only_b4oj6ia(t20, t21);
    t2 = t22;
    goto me3;
mn17:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t24 != ((uint8_t)9ULL)) goto mn23;
    void * t25 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l3 = t25;
    void * t26 = gm_l0;
    void * t27 = gm_l3;
    uint8_t t28 = gm_f_vc_word_only_rg8dfe5(t26, t27);
    t2 = t28;
    goto me3;
mn23:;
ma7:;
    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t30 != ((uint8_t)12ULL)) goto mn29;
    gm_tp0fa5qv7rqnn t31 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t1) + 0) + 8));
    F.l4 = t31;
    uint8_t t32;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 16) + 0));
    if (t37 != ((uint8_t)1ULL)) goto pa36;
    goto po35;
pa36:;
    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 16) + 0));
    if (t39 != ((uint8_t)0ULL)) goto pa38;
    goto po35;
pa38:;
    goto mn34;
po35:;
    uint8_t t40 = 0;
    void * t41 = gm_l0;
    void * t42 = GM_LD(void *, (((uint8_t *)&F.l4) + 0));
    uint8_t t43 = gm_f_vc_place_word_only_b4oj6ia(t41, t42);
    if (t43) {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)&F.l4) + 8));
        uint8_t t46 = gm_f_vc_word_only_rg8dfe5(t44, t45);
        t40 = t46;
    } else t40 = 0;
    t32 = t40;
    goto me33;
mn34:;
    uint8_t t48 = 0;
    void * t49 = gm_l0;
    void * t50 = GM_LD(void *, (((uint8_t *)&F.l4) + 0));
    uint8_t t51 = gm_f_vc_word_only_rg8dfe5(t49, t50);
    if (t51) {
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, (((uint8_t *)&F.l4) + 8));
        uint8_t t54 = gm_f_vc_word_only_rg8dfe5(t52, t53);
        t48 = t54;
    } else t48 = 0;
    t32 = t48;
    goto me33;
mn47:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1433, 18);
me33:;
    t2 = t32;
    goto me3;
mn29:;
ma8:;
    t2 = ((uint8_t)0);
    goto me3;
mn55:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1429, 67);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_vc_placeable_ip26ve2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_placeable_ip26ve2 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_t1j70din2upc4 gm_l2; /* x */
    GM_STACK_CHECK(&F, 245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    gm_t9qkh0rsooc8d t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; case ((uint8_t)59ULL): goto ma8; case ((uint8_t)60ULL): goto ma8; case ((uint8_t)61ULL): goto ma8; case ((uint8_t)62ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t14 != ((uint8_t)6ULL)) goto pa13;
    goto po12;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t16 != ((uint8_t)7ULL)) goto pa15;
    goto po12;
pa15:;
    goto mn11;
po12:;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
    t2 = t17;
    goto me3;
mn11:;
ma5:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t19 != ((uint8_t)10ULL)) goto mn18;
    gm_t1j70din2upc4 t20 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t1) + 0) + 8));
    gm_l2 = t20;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
    gm_t9qkh0rsooc8d t23 = gm_f_vc_placeable_ip26ve2(t21, t22);
    t2 = t23;
    goto me3;
mn18:;
ma6:;
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t25 != ((uint8_t)9ULL)) goto mn24;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
    t2 = t26;
    goto me3;
mn24:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t28 != ((uint8_t)12ULL)) goto mn27;
    gm_tp0fa5qv7rqnn t29 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t1) + 0) + 8));
    F.l3 = t29;
    gm_t9qkh0rsooc8d t30;
    void * t32 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
    uint8_t t33 = gm_f_is_word_82qsidt(t32);
    uint8_t t34 = (uint8_t)!t33;
    if (!t34) goto ie31;
    gm_t9qkh0rsooc8d t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t37 = t36;
    t30 = t37;
    goto ix31;
ie31:;
    gm_t9qkh0rsooc8d t39;
    uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 16) + 0));
    switch (t45) { case ((uint8_t)0ULL): goto ma43; case ((uint8_t)1ULL): goto ma41; case ((uint8_t)2ULL): goto ma42; case ((uint8_t)3ULL): goto ma42; default: goto ma44; }
ma41:;
    uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 16) + 0));
    if (t47 != ((uint8_t)1ULL)) goto mn46;
    gm_t9qkh0rsooc8d t48;
    void * t50 = gm_l0;
    void * t51 = GM_LD(void *, (((uint8_t *)&F.l3) + 0));
    gm_t9qkh0rsooc8d t52 = gm_f_vc_placeable_ip26ve2(t50, t51);
    gm_t9qkh0rsooc8d t53 = t52;
    uint8_t t54 = ((GM_LD(uint8_t, ((uint8_t *)&t53) + 0) == ((uint8_t)0ULL)));
    if (!t54) goto ie49;
    gm_t9qkh0rsooc8d t56;
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t57 = t56;
    t48 = t57;
    goto ix49;
ie49:;
    gm_t9qkh0rsooc8d t59;
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)2ULL));
    gm_t9qkh0rsooc8d t60 = t59;
    t48 = t60;
ix49:;
    t39 = t48;
    goto me40;
mn46:;
ma42:;
    uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 16) + 0));
    if (t64 != ((uint8_t)2ULL)) goto pa63;
    goto po62;
pa63:;
    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 16) + 0));
    if (t66 != ((uint8_t)3ULL)) goto pa65;
    goto po62;
pa65:;
    goto mn61;
po62:;
    gm_t9qkh0rsooc8d t67;
    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)2ULL));
    t39 = t67;
    goto me40;
mn61:;
ma43:;
    uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 16) + 0));
    if (t69 != ((uint8_t)0ULL)) goto mn68;
    void * t70 = gm_l0;
    void * t71 = GM_LD(void *, (((uint8_t *)&F.l3) + 0));
    gm_t9qkh0rsooc8d t72 = gm_f_vc_placeable_ip26ve2(t70, t71);
    gm_t9qkh0rsooc8d t73 = t72;
    gm_t9qkh0rsooc8d t74;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
    switch (t80) { case ((uint8_t)0ULL): goto ma78; case ((uint8_t)1ULL): goto ma77; case ((uint8_t)2ULL): goto ma76; default: goto ma79; }
ma76:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
    if (t82 != ((uint8_t)2ULL)) goto mn81;
    gm_t9qkh0rsooc8d t83;
    GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)2ULL));
    t74 = t83;
    goto me75;
mn81:;
ma77:;
    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
    if (t85 != ((uint8_t)1ULL)) goto mn84;
    gm_t9qkh0rsooc8d t86;
    void * t88 = GM_LD(void *, (((uint8_t *)&F.l3) + 0));
    uint32_t t89 = gm_f_root_levels_umkq7vp(t88);
    uint8_t t90 = ((t89 == ((uint32_t)0ULL)));
    if (!t90) goto ie87;
    gm_t9qkh0rsooc8d t92;
    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t93 = t92;
    t86 = t93;
    goto ix87;
ie87:;
    gm_t9qkh0rsooc8d t95;
    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t96 = t95;
    t86 = t96;
ix87:;
    t74 = t86;
    goto me75;
mn84:;
ma78:;
    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
    if (t98 != ((uint8_t)0ULL)) goto mn97;
    gm_t9qkh0rsooc8d t99;
    GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)0ULL));
    t74 = t99;
    goto me75;
mn97:;
ma79:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 855, 19);
me75:;
    t39 = t74;
    goto me40;
mn68:;
ma44:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 852, 57);
me40:;
    gm_t9qkh0rsooc8d t100 = t39;
    t30 = t100;
ix31:;
    t2 = t30;
    goto me3;
mn27:;
ma8:;
    gm_t9qkh0rsooc8d t102;
    GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)0ULL));
    t2 = t102;
    goto me3;
mn101:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 848, 66);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_vc_plain_bs0ofeb(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_put_byte_ner3v2q(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_vc_put_byte_ner3v2q F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* at */
    uint64_t gm_l2; /* v */
    uint32_t gm_l3; /* save */
    uint32_t gm_l4; /* r */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint32_t t5 = gm_f_vc_alloc_3hmfvtt(t4);
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        uint32_t t7 = gm_l4;
        uint64_t t8 = gm_l2;
        gm_tb31k5l794t7p t9;
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t7);
        GM_ST(uint64_t, ((uint8_t *)&t9 + 8), t8);
        gm_tjc2ga47o3k6g t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t10 + 8), t9);
        gm_b0_1 t11 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t6, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_l1;
        uint32_t t14 = gm_l4;
        gm_tl874t2atnlh3 t15;
        GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t13);
        GM_ST(uint32_t, ((uint8_t *)&t15 + 4), ((uint32_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t15 + 8), t14);
        gm_tjc2ga47o3k6g t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)54ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t16 + 8), t15);
        gm_b0_1 t17 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t12, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = gm_l0;
        uint32_t t19 = gm_l3;
        GM_ST(uint32_t, (((uint8_t *)t18) + 224), t19);
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
__attribute__((aligned(4))) uint8_t gm_f_vc_reads_only_vncte32(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_reads_only_vncte32 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tkm39klvnn4cr gm_l3; /* x */
    gm_ta6jn77i9rmqf gm_l4; /* x */
    gm_tuggr5sml9o52 gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* l */
    void * gm_l8; /* r */
    void * gm_l9; /* x */
    void * gm_l10; /* x */
    void * gm_l12; /* x */
    GM_STACK_CHECK(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma15; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma16; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma16; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma16; case ((uint8_t)31ULL): goto ma16; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma16; case ((uint8_t)34ULL): goto ma16; case ((uint8_t)35ULL): goto ma16; case ((uint8_t)36ULL): goto ma16; case ((uint8_t)37ULL): goto ma16; case ((uint8_t)38ULL): goto ma16; case ((uint8_t)39ULL): goto ma16; case ((uint8_t)40ULL): goto ma16; case ((uint8_t)41ULL): goto ma16; case ((uint8_t)42ULL): goto ma16; case ((uint8_t)43ULL): goto ma16; case ((uint8_t)44ULL): goto ma16; case ((uint8_t)45ULL): goto ma16; case ((uint8_t)46ULL): goto ma16; case ((uint8_t)47ULL): goto ma16; case ((uint8_t)48ULL): goto ma16; case ((uint8_t)49ULL): goto ma16; case ((uint8_t)50ULL): goto ma16; case ((uint8_t)51ULL): goto ma16; case ((uint8_t)52ULL): goto ma16; case ((uint8_t)53ULL): goto ma16; case ((uint8_t)54ULL): goto ma16; case ((uint8_t)55ULL): goto ma16; case ((uint8_t)56ULL): goto ma16; case ((uint8_t)57ULL): goto ma16; case ((uint8_t)58ULL): goto ma16; case ((uint8_t)59ULL): goto ma16; case ((uint8_t)60ULL): goto ma16; case ((uint8_t)61ULL): goto ma16; case ((uint8_t)62ULL): goto ma16; default: goto ma16; }
ma4:;
    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t22 != ((uint8_t)0ULL)) goto pa21;
    goto po20;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t24 != ((uint8_t)6ULL)) goto pa23;
    goto po20;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t26 != ((uint8_t)7ULL)) goto pa25;
    goto po20;
pa25:;
    goto mn19;
po20:;
    t2 = ((uint8_t)1);
    goto me3;
mn19:;
ma5:;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t28 != ((uint8_t)21ULL)) goto mn27;
    gm_t0pc2c7dafsjt t29 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t1) + 0) + 8));
    F.l2 = t29;
    uint8_t t30 = 0;
    void * t31 = gm_l0;
    void * t32 = GM_LD(void *, (((uint8_t *)&F.l2) + 24));
    uint8_t t33 = gm_f_vc_reads_only_vncte32(t31, t32);
    if (t33) {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)&F.l2) + 32));
        uint8_t t36 = gm_f_vc_reads_only_vncte32(t34, t35);
        t30 = t36;
    } else t30 = 0;
    t2 = t30;
    goto me3;
mn27:;
ma6:;
    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t38 != ((uint8_t)23ULL)) goto mn37;
    gm_tkm39klvnn4cr t39 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t1) + 0) + 8));
    gm_l3 = t39;
    uint8_t t40 = 0;
    void * t41 = gm_l0;
    void * t42 = GM_LD(void *, (((uint8_t *)&gm_l3) + 16));
    uint8_t t43 = gm_f_vc_reads_only_vncte32(t41, t42);
    if (t43) {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)&gm_l3) + 24));
        uint8_t t46 = gm_f_vc_reads_only_vncte32(t44, t45);
        t40 = t46;
    } else t40 = 0;
    t2 = t40;
    goto me3;
mn37:;
ma7:;
    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t48 != ((uint8_t)24ULL)) goto mn47;
    gm_ta6jn77i9rmqf t49 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t1) + 0) + 8));
    gm_l4 = t49;
    uint8_t t50 = 0;
    void * t51 = gm_l0;
    void * t52 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    uint8_t t53 = gm_f_vc_reads_only_vncte32(t51, t52);
    if (t53) {
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, (((uint8_t *)&gm_l4) + 16));
        uint8_t t56 = gm_f_vc_reads_only_vncte32(t54, t55);
        t50 = t56;
    } else t50 = 0;
    t2 = t50;
    goto me3;
mn47:;
ma8:;
    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t58 != ((uint8_t)22ULL)) goto mn57;
    gm_tuggr5sml9o52 t59 = GM_LD(gm_tuggr5sml9o52, ((((uint8_t *)t1) + 0) + 8));
    gm_l5 = t59;
    uint8_t t60 = 0;
    void * t61 = gm_l0;
    void * t62 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
    uint8_t t63 = gm_f_vc_reads_only_vncte32(t61, t62);
    if (t63) {
        void * t64 = gm_l0;
        void * t65 = GM_LD(void *, (((uint8_t *)&gm_l5) + 16));
        uint8_t t66 = gm_f_vc_reads_only_vncte32(t64, t65);
        t60 = t66;
    } else t60 = 0;
    t2 = t60;
    goto me3;
mn57:;
ma9:;
    uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t68 != ((uint8_t)25ULL)) goto mn67;
    void * t69 = GM_LD(void *, ((((uint8_t *)t1) + 0) + 8));
    gm_l6 = t69;
    void * t70 = gm_l0;
    void * t71 = gm_l6;
    uint8_t t72 = gm_f_vc_reads_only_vncte32(t70, t71);
    t2 = t72;
    goto me3;
mn67:;
ma10:;
    uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t74 != ((uint8_t)32ULL)) goto mn73;
    void * t75 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 8));
    gm_l7 = t75;
    void * t76 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l8 = t76;
    uint8_t t77 = 0;
    void * t78 = gm_l0;
    void * t79 = gm_l7;
    uint8_t t80 = gm_f_vc_reads_only_vncte32(t78, t79);
    if (t80) {
        void * t81 = gm_l0;
        void * t82 = gm_l8;
        uint8_t t83 = gm_f_vc_reads_only_vncte32(t81, t82);
        t77 = t83;
    } else t77 = 0;
    t2 = t77;
    goto me3;
mn73:;
ma11:;
    uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t85 != ((uint8_t)26ULL)) goto mn84;
    void * t86 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l9 = t86;
    void * t87 = gm_l0;
    void * t88 = gm_l9;
    uint8_t t89 = gm_f_vc_reads_only_vncte32(t87, t88);
    t2 = t89;
    goto me3;
mn84:;
ma12:;
    uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t91 != ((uint8_t)27ULL)) goto mn90;
    void * t92 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 32));
    gm_l10 = t92;
    void * t93 = gm_l0;
    void * t94 = gm_l10;
    uint8_t t95 = gm_f_vc_reads_only_vncte32(t93, t94);
    t2 = t95;
    goto me3;
mn90:;
ma13:;
    uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t97 != ((uint8_t)28ULL)) goto mn96;
    gm_tscalq56heofe t98 = GM_LD(gm_tscalq56heofe, ((((uint8_t *)t1) + 0) + 8));
    F.l11 = t98;
    uint8_t t99 = 0;
    void * t100 = gm_l0;
    void * t101 = GM_LD(void *, (((uint8_t *)&F.l11) + 24));
    uint8_t t102 = gm_f_vc_reads_only_vncte32(t100, t101);
    if (t102) {
        void * t103 = gm_l0;
        void * t104 = GM_LD(void *, (((uint8_t *)&F.l11) + 32));
        uint8_t t105 = gm_f_vc_reads_only_vncte32(t103, t104);
        t99 = t105;
    } else t99 = 0;
    t2 = t99;
    goto me3;
mn96:;
ma14:;
    uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t107 != ((uint8_t)29ULL)) goto mn106;
    void * t108 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 24));
    gm_l12 = t108;
    void * t109 = gm_l0;
    void * t110 = gm_l12;
    uint8_t t111 = gm_f_vc_reads_only_vncte32(t109, t110);
    t2 = t111;
    goto me3;
mn106:;
ma15:;
    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t115 != ((uint8_t)10ULL)) goto pa114;
    goto po113;
pa114:;
    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t117 != ((uint8_t)9ULL)) goto pa116;
    goto po113;
pa116:;
    uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t119 != ((uint8_t)12ULL)) goto pa118;
    goto po113;
pa118:;
    uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t121 != ((uint8_t)14ULL)) goto pa120;
    goto po113;
pa120:;
    uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t123 != ((uint8_t)15ULL)) goto pa122;
    goto po113;
pa122:;
    goto mn112;
po113:;
    void * t124 = gm_l0;
    void * t125 = gm_l1;
    uint8_t t126 = gm_f_vc_word_only_rg8dfe5(t124, t125);
    t2 = t126;
    goto me3;
mn112:;
ma16:;
    t2 = ((uint8_t)0);
    goto me3;
mn127:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 294, 62);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_vc_retarget_cjve41g(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_retarget_cjve41g F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* t */
    uint32_t gm_l2; /* d */
    uint64_t gm_l3; /* n */
    void * gm_l4; /* i */
    uint32_t gm_l5; /* r */
    uint64_t gm_l6; /* v */
    uint32_t gm_l7; /* r */
    uint32_t gm_l8; /* x */
    gm_ti4ei62u4i1d9 gm_l9; /* x */
    gm_te4o1hd8337m3 gm_l10; /* x */
    uint8_t gm_l12; /* op */
    uint32_t gm_l13; /* r */
    uint32_t gm_l14; /* a */
    uint32_t gm_l15; /* b */
    gm_ts6oqppjh0ba3 gm_l18; /* op */
    gm_tmn7n0n9raou5 gm_l19; /* how */
    uint32_t gm_l20; /* r */
    uint32_t gm_l21; /* a */
    uint32_t gm_l22; /* b */
    gm_tufm7evi61pnh gm_l23; /* x */
    uint32_t gm_l24; /* r */
    uint32_t gm_l25; /* a */
    gm_to7n9gnqtgp47 gm_l26; /* x */
    gm_tqj4rgf34oslv gm_l28; /* x */
    gm_tfd1kqjeh4les gm_l29; /* x */
    uint32_t gm_l30; /* r */
    uint32_t gm_l31; /* at */
    uint32_t gm_l32; /* size */
    gm_t04llc5uen0qo gm_l33; /* x */
    uint32_t gm_l35; /* r */
    uint32_t gm_l36; /* at */
    uint32_t gm_l37; /* size */
    uint32_t gm_l38; /* r */
    void * gm_l39; /* e */
    uint32_t gm_l41; /* r */
    uint32_t gm_l43; /* r */
    gm_tbeu0qv6bbs2s gm_l44; /* x */
    uint32_t gm_l45; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 11433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 16) + 0) + 8);
        gm_l3 = t3;
    }
    {
        uint8_t t5 = 0;
        uint64_t t6 = gm_l3;
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) {
            void * t8 = gm_l0;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 264));
            uint64_t t10 = (uint64_t)t9;
            uint64_t t11 = gm_l3;
            uint8_t t12 = (t10 >= t11);
            t5 = t12;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t15 = gm_l3;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 316, 23);
        uint64_t t18 = r16;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 16) + 0) + 8);
        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcutmnmd444bu0, 316, 11);
        void * t22 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 16) + 0)) + t19 * 56);
        gm_l4 = t22;
    }
    {
        void * t23 = gm_l4;
        gm_tk8sioamep3gv t24;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        switch (t51) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma49; case ((uint8_t)2ULL): goto ma49; case ((uint8_t)3ULL): goto ma41; case ((uint8_t)4ULL): goto ma49; case ((uint8_t)5ULL): goto ma49; case ((uint8_t)6ULL): goto ma49; case ((uint8_t)7ULL): goto ma49; case ((uint8_t)8ULL): goto ma49; case ((uint8_t)9ULL): goto ma49; case ((uint8_t)10ULL): goto ma31; case ((uint8_t)11ULL): goto ma32; case ((uint8_t)12ULL): goto ma33; case ((uint8_t)13ULL): goto ma49; case ((uint8_t)14ULL): goto ma49; case ((uint8_t)15ULL): goto ma49; case ((uint8_t)16ULL): goto ma49; case ((uint8_t)17ULL): goto ma42; case ((uint8_t)18ULL): goto ma49; case ((uint8_t)19ULL): goto ma49; case ((uint8_t)20ULL): goto ma49; case ((uint8_t)21ULL): goto ma43; case ((uint8_t)22ULL): goto ma49; case ((uint8_t)23ULL): goto ma49; case ((uint8_t)24ULL): goto ma49; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma28; case ((uint8_t)27ULL): goto ma29; case ((uint8_t)28ULL): goto ma49; case ((uint8_t)29ULL): goto ma49; case ((uint8_t)30ULL): goto ma34; case ((uint8_t)31ULL): goto ma35; case ((uint8_t)32ULL): goto ma36; case ((uint8_t)33ULL): goto ma49; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma39; case ((uint8_t)37ULL): goto ma40; case ((uint8_t)38ULL): goto ma49; case ((uint8_t)39ULL): goto ma49; case ((uint8_t)40ULL): goto ma49; case ((uint8_t)41ULL): goto ma49; case ((uint8_t)42ULL): goto ma49; case ((uint8_t)43ULL): goto ma49; case ((uint8_t)44ULL): goto ma49; case ((uint8_t)45ULL): goto ma49; case ((uint8_t)46ULL): goto ma49; case ((uint8_t)47ULL): goto ma46; case ((uint8_t)48ULL): goto ma47; case ((uint8_t)49ULL): goto ma49; case ((uint8_t)50ULL): goto ma48; case ((uint8_t)51ULL): goto ma49; case ((uint8_t)52ULL): goto ma49; case ((uint8_t)53ULL): goto ma44; case ((uint8_t)54ULL): goto ma49; case ((uint8_t)55ULL): goto ma49; case ((uint8_t)56ULL): goto ma49; case ((uint8_t)57ULL): goto ma49; case ((uint8_t)58ULL): goto ma49; case ((uint8_t)59ULL): goto ma49; case ((uint8_t)60ULL): goto ma49; case ((uint8_t)61ULL): goto ma45; case ((uint8_t)62ULL): goto ma49; case ((uint8_t)63ULL): goto ma49; case ((uint8_t)64ULL): goto ma49; case ((uint8_t)65ULL): goto ma27; case ((uint8_t)66ULL): goto ma49; case ((uint8_t)67ULL): goto ma49; case ((uint8_t)68ULL): goto ma49; case ((uint8_t)69ULL): goto ma49; case ((uint8_t)70ULL): goto ma49; case ((uint8_t)71ULL): goto ma49; case ((uint8_t)72ULL): goto ma49; case ((uint8_t)73ULL): goto ma49; case ((uint8_t)74ULL): goto ma49; case ((uint8_t)75ULL): goto ma49; case ((uint8_t)76ULL): goto ma49; case ((uint8_t)77ULL): goto ma49; case ((uint8_t)78ULL): goto ma49; case ((uint8_t)79ULL): goto ma49; case ((uint8_t)80ULL): goto ma49; case ((uint8_t)81ULL): goto ma49; case ((uint8_t)82ULL): goto ma49; case ((uint8_t)83ULL): goto ma49; case ((uint8_t)84ULL): goto ma49; default: goto ma49; }
ma26:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t53 != ((uint8_t)0ULL)) goto mn52;
        uint32_t t54 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l5 = t54;
        uint64_t t55 = GM_LD(uint64_t, ((((uint8_t *)t23) + 8) + 8));
        gm_l6 = t55;
        gm_tk8sioamep3gv t56;
        uint32_t t58 = gm_l5;
        uint32_t t59 = gm_l1;
        uint8_t t60 = ((t58 == t59));
        if (!t60) goto ie57;
        uint32_t t62 = gm_l2;
        uint64_t t63 = gm_l6;
        gm_tb31k5l794t7p t64;
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t64 + 0), t62);
        GM_ST(uint64_t, ((uint8_t *)&t64 + 8), t63);
        gm_tjc2ga47o3k6g t65;
        memset(&t65, 0, sizeof t65);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t65 + 8), t64);
        gm_tjc2ga47o3k6g t66 = t65;
        gm_tk8sioamep3gv t67;
        GM_ST(uint8_t, (uint8_t *)&t67 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t67 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t67 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t67), t66);
        ((uint8_t *)&t67)[56] = 1;
        t56 = t67;
        goto ix57;
ie57:;
        gm_tk8sioamep3gv t69;
        memset(&t69, 0, sizeof t69);
        gm_tk8sioamep3gv t70 = t69;
        t56 = t70;
ix57:;
        t24 = t56;
        goto me25;
mn52:;
ma27:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t72 != ((uint8_t)65ULL)) goto mn71;
        uint32_t t73 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l7 = t73;
        uint32_t t74 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 4));
        gm_l8 = t74;
        gm_tk8sioamep3gv t75;
        uint32_t t77 = gm_l7;
        uint32_t t78 = gm_l1;
        uint8_t t79 = ((t77 == t78));
        if (!t79) goto ie76;
        uint32_t t81 = gm_l2;
        uint32_t t82 = gm_l8;
        gm_tcc1rq4n1avb2 t83;
        GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t81);
        GM_ST(uint32_t, ((uint8_t *)&t83 + 4), t82);
        gm_tjc2ga47o3k6g t84;
        memset(&t84, 0, sizeof t84);
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)65ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t84 + 8), t83);
        gm_tjc2ga47o3k6g t85 = t84;
        gm_tk8sioamep3gv t86;
        GM_ST(uint8_t, (uint8_t *)&t86 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t86 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t86 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t86), t85);
        ((uint8_t *)&t86)[56] = 1;
        t75 = t86;
        goto ix76;
ie76:;
        gm_tk8sioamep3gv t88;
        memset(&t88, 0, sizeof t88);
        gm_tk8sioamep3gv t89 = t88;
        t75 = t89;
ix76:;
        t24 = t75;
        goto me25;
mn71:;
ma28:;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t91 != ((uint8_t)26ULL)) goto mn90;
        gm_ti4ei62u4i1d9 t92 = GM_LD(gm_ti4ei62u4i1d9, (((uint8_t *)t23) + 8));
        gm_l9 = t92;
        gm_tk8sioamep3gv t93;
        uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
        uint32_t t96 = gm_l1;
        uint8_t t97 = ((t95 == t96));
        if (!t97) goto ie94;
        gm_ti4ei62u4i1d9 t99;
        memset((uint8_t *)&t99 + 3, 0, 1);
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
        GM_ST(uint8_t, (uint8_t *)&t99 + 0, t100);
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 1));
        GM_ST(uint8_t, (uint8_t *)&t99 + 1, t101);
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 2));
        GM_ST(uint8_t, (uint8_t *)&t99 + 2, t102);
        uint32_t t103 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t99 + 4, t103);
        uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
        GM_ST(uint32_t, (uint8_t *)&t99 + 8, t104);
        uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 12));
        GM_ST(uint32_t, (uint8_t *)&t99 + 12, t105);
        void * t106 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
        GM_ST(void *, (uint8_t *)&t99 + 16, t106);
        gm_tjc2ga47o3k6g t107;
        GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t107 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t107 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t107 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)26ULL));
        GM_ST(gm_ti4ei62u4i1d9, ((uint8_t *)&t107 + 8), t99);
        gm_tjc2ga47o3k6g t108 = t107;
        gm_tk8sioamep3gv t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t109), t108);
        ((uint8_t *)&t109)[56] = 1;
        t93 = t109;
        goto ix94;
ie94:;
        gm_tk8sioamep3gv t111;
        memset(&t111, 0, sizeof t111);
        gm_tk8sioamep3gv t112 = t111;
        t93 = t112;
ix94:;
        t24 = t93;
        goto me25;
mn90:;
ma29:;
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t114 != ((uint8_t)27ULL)) goto mn113;
        gm_te4o1hd8337m3 t115 = GM_LD(gm_te4o1hd8337m3, (((uint8_t *)t23) + 8));
        gm_l10 = t115;
        gm_tk8sioamep3gv t116;
        uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
        uint32_t t119 = gm_l1;
        uint8_t t120 = ((t118 == t119));
        if (!t120) goto ie117;
        gm_te4o1hd8337m3 t122;
        memset((uint8_t *)&t122 + 3, 0, 1);
        memset((uint8_t *)&t122 + 20, 0, 4);
        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
        GM_ST(uint8_t, (uint8_t *)&t122 + 0, t123);
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 1));
        GM_ST(uint8_t, (uint8_t *)&t122 + 1, t124);
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 2));
        GM_ST(uint8_t, (uint8_t *)&t122 + 2, t125);
        uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
        GM_ST(uint32_t, (uint8_t *)&t122 + 4, t126);
        uint32_t t127 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t122 + 8, t127);
        uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 12));
        GM_ST(uint32_t, (uint8_t *)&t122 + 12, t128);
        uint32_t t129 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 16));
        GM_ST(uint32_t, (uint8_t *)&t122 + 16, t129);
        void * t130 = GM_LD(void *, (((uint8_t *)&gm_l10) + 24));
        GM_ST(void *, (uint8_t *)&t122 + 24, t130);
        gm_tjc2ga47o3k6g t131;
        GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t131 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t131 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)27ULL));
        GM_ST(gm_te4o1hd8337m3, ((uint8_t *)&t131 + 8), t122);
        gm_tjc2ga47o3k6g t132 = t131;
        gm_tk8sioamep3gv t133;
        GM_ST(uint8_t, (uint8_t *)&t133 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t133 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t133 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t133), t132);
        ((uint8_t *)&t133)[56] = 1;
        t116 = t133;
        goto ix117;
ie117:;
        gm_tk8sioamep3gv t135;
        memset(&t135, 0, sizeof t135);
        gm_tk8sioamep3gv t136 = t135;
        t116 = t136;
ix117:;
        t24 = t116;
        goto me25;
mn113:;
ma30:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t138 != ((uint8_t)25ULL)) goto mn137;
        gm_t06kd2nmcgb3u t139 = GM_LD(gm_t06kd2nmcgb3u, (((uint8_t *)t23) + 8));
        F.l11 = t139;
        gm_tk8sioamep3gv t140;
        uint32_t t142 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 20));
        uint32_t t143 = gm_l1;
        uint8_t t144 = ((t142 == t143));
        if (!t144) goto ie141;
        gm_t06kd2nmcgb3u t146;
        memset((uint8_t *)&t146 + 2, 0, 2);
        gm_ts6oqppjh0ba3 t147 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t146 + 0, t147);
        gm_ttovhfee8mf28 t148 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l11) + 4));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t146 + 4, t148);
        gm_ttovhfee8mf28 t149 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l11) + 12));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t146 + 12, t149);
        uint32_t t150 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t146 + 20, t150);
        uint32_t t151 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 24));
        GM_ST(uint32_t, (uint8_t *)&t146 + 24, t151);
        uint32_t t152 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 28));
        GM_ST(uint32_t, (uint8_t *)&t146 + 28, t152);
        void * t153 = GM_LD(void *, (((uint8_t *)&F.l11) + 32));
        GM_ST(void *, (uint8_t *)&t146 + 32, t153);
        gm_tjc2ga47o3k6g t154;
        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t154 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)25ULL));
        GM_ST(gm_t06kd2nmcgb3u, ((uint8_t *)&t154 + 8), t146);
        gm_tjc2ga47o3k6g t155 = t154;
        gm_tk8sioamep3gv t156;
        GM_ST(uint8_t, (uint8_t *)&t156 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t156 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t156 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t156), t155);
        ((uint8_t *)&t156)[56] = 1;
        t140 = t156;
        goto ix141;
ie141:;
        gm_tk8sioamep3gv t158;
        memset(&t158, 0, sizeof t158);
        gm_tk8sioamep3gv t159 = t158;
        t140 = t159;
ix141:;
        t24 = t140;
        goto me25;
mn137:;
ma31:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t161 != ((uint8_t)10ULL)) goto mn160;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l12 = t162;
        uint32_t t163 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 4));
        gm_l13 = t163;
        uint32_t t164 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 8));
        gm_l14 = t164;
        uint32_t t165 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 12));
        gm_l15 = t165;
        gm_tk8sioamep3gv t166;
        uint32_t t168 = gm_l13;
        uint32_t t169 = gm_l1;
        uint8_t t170 = ((t168 == t169));
        if (!t170) goto ie167;
        uint8_t t172 = gm_l12;
        uint32_t t173 = gm_l2;
        uint32_t t174 = gm_l14;
        uint32_t t175 = gm_l15;
        gm_t51d05oujnurh t176;
        GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t176 + 0), t172);
        GM_ST(uint32_t, ((uint8_t *)&t176 + 4), t173);
        GM_ST(uint32_t, ((uint8_t *)&t176 + 8), t174);
        GM_ST(uint32_t, ((uint8_t *)&t176 + 12), t175);
        gm_tjc2ga47o3k6g t177;
        memset(&t177, 0, sizeof t177);
        GM_ST(uint8_t, ((uint8_t *)&t177 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t51d05oujnurh, ((uint8_t *)&t177 + 8), t176);
        gm_tjc2ga47o3k6g t178 = t177;
        gm_tk8sioamep3gv t179;
        GM_ST(uint8_t, (uint8_t *)&t179 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t179 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t179 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t179), t178);
        ((uint8_t *)&t179)[56] = 1;
        t166 = t179;
        goto ix167;
ie167:;
        gm_tk8sioamep3gv t181;
        memset(&t181, 0, sizeof t181);
        gm_tk8sioamep3gv t182 = t181;
        t166 = t182;
ix167:;
        t24 = t166;
        goto me25;
mn160:;
ma32:;
        uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t184 != ((uint8_t)11ULL)) goto mn183;
        gm_tiag95i83438g t185 = GM_LD(gm_tiag95i83438g, (((uint8_t *)t23) + 8));
        F.l16 = t185;
        gm_tk8sioamep3gv t186;
        uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 16));
        uint32_t t189 = gm_l1;
        uint8_t t190 = ((t188 == t189));
        if (!t190) goto ie187;
        gm_tiag95i83438g t192;
        memset((uint8_t *)&t192 + 28, 0, 4);
        gm_ttovhfee8mf28 t193 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t192 + 0, t193);
        gm_ttovhfee8mf28 t194 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l16) + 8));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t192 + 8, t194);
        uint32_t t195 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t192 + 16, t195);
        uint32_t t196 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 20));
        GM_ST(uint32_t, (uint8_t *)&t192 + 20, t196);
        uint32_t t197 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 24));
        GM_ST(uint32_t, (uint8_t *)&t192 + 24, t197);
        void * t198 = GM_LD(void *, (((uint8_t *)&F.l16) + 32));
        GM_ST(void *, (uint8_t *)&t192 + 32, t198);
        gm_tjc2ga47o3k6g t199;
        GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t199 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)11ULL));
        GM_ST(gm_tiag95i83438g, ((uint8_t *)&t199 + 8), t192);
        gm_tjc2ga47o3k6g t200 = t199;
        gm_tk8sioamep3gv t201;
        GM_ST(uint8_t, (uint8_t *)&t201 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t201 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t201 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t201), t200);
        ((uint8_t *)&t201)[56] = 1;
        t186 = t201;
        goto ix187;
ie187:;
        gm_tk8sioamep3gv t203;
        memset(&t203, 0, sizeof t203);
        gm_tk8sioamep3gv t204 = t203;
        t186 = t204;
ix187:;
        t24 = t186;
        goto me25;
mn183:;
ma33:;
        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t206 != ((uint8_t)12ULL)) goto mn205;
        gm_thji9nptko1bl t207 = GM_LD(gm_thji9nptko1bl, (((uint8_t *)t23) + 8));
        F.l17 = t207;
        gm_tk8sioamep3gv t208;
        uint32_t t210 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 20));
        uint32_t t211 = gm_l1;
        uint8_t t212 = ((t210 == t211));
        if (!t212) goto ie209;
        gm_thji9nptko1bl t214;
        memset((uint8_t *)&t214 + 17, 0, 3);
        gm_ttovhfee8mf28 t215 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l17) + 0));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t214 + 0, t215);
        gm_ttovhfee8mf28 t216 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l17) + 8));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t214 + 8, t216);
        uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 16));
        GM_ST(uint8_t, (uint8_t *)&t214 + 16, t217);
        uint32_t t218 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t214 + 20, t218);
        uint32_t t219 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 24));
        GM_ST(uint32_t, (uint8_t *)&t214 + 24, t219);
        uint32_t t220 = GM_LD(uint32_t, (((uint8_t *)&F.l17) + 28));
        GM_ST(uint32_t, (uint8_t *)&t214 + 28, t220);
        void * t221 = GM_LD(void *, (((uint8_t *)&F.l17) + 32));
        GM_ST(void *, (uint8_t *)&t214 + 32, t221);
        gm_tjc2ga47o3k6g t222;
        GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t222 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)12ULL));
        GM_ST(gm_thji9nptko1bl, ((uint8_t *)&t222 + 8), t214);
        gm_tjc2ga47o3k6g t223 = t222;
        gm_tk8sioamep3gv t224;
        GM_ST(uint8_t, (uint8_t *)&t224 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t224 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t224 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t224), t223);
        ((uint8_t *)&t224)[56] = 1;
        t208 = t224;
        goto ix209;
ie209:;
        gm_tk8sioamep3gv t226;
        memset(&t226, 0, sizeof t226);
        gm_tk8sioamep3gv t227 = t226;
        t208 = t227;
ix209:;
        t24 = t208;
        goto me25;
mn205:;
ma34:;
        uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t229 != ((uint8_t)30ULL)) goto mn228;
        gm_ts6oqppjh0ba3 t230 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t23) + 8) + 0));
        gm_l18 = t230;
        gm_tmn7n0n9raou5 t231 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t23) + 8) + 4));
        gm_l19 = t231;
        uint32_t t232 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 12));
        gm_l20 = t232;
        uint32_t t233 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 16));
        gm_l21 = t233;
        uint32_t t234 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 20));
        gm_l22 = t234;
        gm_tk8sioamep3gv t235;
        uint32_t t237 = gm_l20;
        uint32_t t238 = gm_l1;
        uint8_t t239 = ((t237 == t238));
        if (!t239) goto ie236;
        gm_tnkql1rjn1vbj t241;
        memset((uint8_t *)&t241 + 2, 0, 2);
        gm_ts6oqppjh0ba3 t242 = gm_l18;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t241 + 0, t242);
        gm_tmn7n0n9raou5 t243 = gm_l19;
        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t241 + 4, t243);
        uint32_t t244 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t241 + 12, t244);
        uint32_t t245 = gm_l21;
        GM_ST(uint32_t, (uint8_t *)&t241 + 16, t245);
        uint32_t t246 = gm_l22;
        GM_ST(uint32_t, (uint8_t *)&t241 + 20, t246);
        gm_tjc2ga47o3k6g t247;
        GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)30ULL));
        GM_ST(gm_tnkql1rjn1vbj, ((uint8_t *)&t247 + 8), t241);
        gm_tjc2ga47o3k6g t248 = t247;
        gm_tk8sioamep3gv t249;
        GM_ST(uint8_t, (uint8_t *)&t249 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t249 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t249 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t249), t248);
        ((uint8_t *)&t249)[56] = 1;
        t235 = t249;
        goto ix236;
ie236:;
        gm_tk8sioamep3gv t251;
        memset(&t251, 0, sizeof t251);
        gm_tk8sioamep3gv t252 = t251;
        t235 = t252;
ix236:;
        t24 = t235;
        goto me25;
mn228:;
ma35:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t254 != ((uint8_t)31ULL)) goto mn253;
        gm_tufm7evi61pnh t255 = GM_LD(gm_tufm7evi61pnh, (((uint8_t *)t23) + 8));
        gm_l23 = t255;
        gm_tk8sioamep3gv t256;
        uint32_t t258 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 12));
        uint32_t t259 = gm_l1;
        uint8_t t260 = ((t258 == t259));
        if (!t260) goto ie257;
        gm_tufm7evi61pnh t262;
        memset((uint8_t *)&t262 + 9, 0, 3);
        gm_tmn7n0n9raou5 t263 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l23) + 0));
        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t262 + 0, t263);
        uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 8));
        GM_ST(uint8_t, (uint8_t *)&t262 + 8, t264);
        uint32_t t265 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t262 + 12, t265);
        uint32_t t266 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 16));
        GM_ST(uint32_t, (uint8_t *)&t262 + 16, t266);
        uint32_t t267 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 20));
        GM_ST(uint32_t, (uint8_t *)&t262 + 20, t267);
        gm_tjc2ga47o3k6g t268;
        GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t268 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t268 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t268 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t268 + 8), t262);
        gm_tjc2ga47o3k6g t269 = t268;
        gm_tk8sioamep3gv t270;
        GM_ST(uint8_t, (uint8_t *)&t270 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t270 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t270 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t270), t269);
        ((uint8_t *)&t270)[56] = 1;
        t256 = t270;
        goto ix257;
ie257:;
        gm_tk8sioamep3gv t272;
        memset(&t272, 0, sizeof t272);
        gm_tk8sioamep3gv t273 = t272;
        t256 = t273;
ix257:;
        t24 = t256;
        goto me25;
mn253:;
ma36:;
        uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t275 != ((uint8_t)32ULL)) goto mn274;
        uint32_t t276 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l24 = t276;
        uint32_t t277 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 4));
        gm_l25 = t277;
        gm_tk8sioamep3gv t278;
        uint32_t t280 = gm_l24;
        uint32_t t281 = gm_l1;
        uint8_t t282 = ((t280 == t281));
        if (!t282) goto ie279;
        uint32_t t284 = gm_l2;
        uint32_t t285 = gm_l25;
        gm_tcc1rq4n1avb2 t286;
        GM_ST(uint32_t, ((uint8_t *)&t286 + 0), t284);
        GM_ST(uint32_t, ((uint8_t *)&t286 + 4), t285);
        gm_tjc2ga47o3k6g t287;
        memset(&t287, 0, sizeof t287);
        GM_ST(uint8_t, ((uint8_t *)&t287 + 0), ((uint8_t)32ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t287 + 8), t286);
        gm_tjc2ga47o3k6g t288 = t287;
        gm_tk8sioamep3gv t289;
        GM_ST(uint8_t, (uint8_t *)&t289 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t289 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t289 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t289), t288);
        ((uint8_t *)&t289)[56] = 1;
        t278 = t289;
        goto ix279;
ie279:;
        gm_tk8sioamep3gv t291;
        memset(&t291, 0, sizeof t291);
        gm_tk8sioamep3gv t292 = t291;
        t278 = t292;
ix279:;
        t24 = t278;
        goto me25;
mn274:;
ma37:;
        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t294 != ((uint8_t)34ULL)) goto mn293;
        gm_to7n9gnqtgp47 t295 = GM_LD(gm_to7n9gnqtgp47, (((uint8_t *)t23) + 8));
        gm_l26 = t295;
        gm_tk8sioamep3gv t296;
        uint32_t t298 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 12));
        uint32_t t299 = gm_l1;
        uint8_t t300 = ((t298 == t299));
        if (!t300) goto ie297;
        gm_to7n9gnqtgp47 t302;
        memset((uint8_t *)&t302 + 1, 0, 3);
        memset((uint8_t *)&t302 + 20, 0, 4);
        gm_t9qkh0rsooc8d t303 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l26) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t302 + 0, t303);
        gm_ttovhfee8mf28 t304 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&gm_l26) + 4));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t302 + 4, t304);
        uint32_t t305 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t302 + 12, t305);
        uint32_t t306 = GM_LD(uint32_t, (((uint8_t *)&gm_l26) + 16));
        GM_ST(uint32_t, (uint8_t *)&t302 + 16, t306);
        void * t307 = GM_LD(void *, (((uint8_t *)&gm_l26) + 24));
        GM_ST(void *, (uint8_t *)&t302 + 24, t307);
        gm_tjc2ga47o3k6g t308;
        GM_ST(uint8_t, (uint8_t *)&t308 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t308 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t308 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t308 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t308 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to7n9gnqtgp47, ((uint8_t *)&t308 + 8), t302);
        gm_tjc2ga47o3k6g t309 = t308;
        gm_tk8sioamep3gv t310;
        GM_ST(uint8_t, (uint8_t *)&t310 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t310 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t310 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t310), t309);
        ((uint8_t *)&t310)[56] = 1;
        t296 = t310;
        goto ix297;
ie297:;
        gm_tk8sioamep3gv t312;
        memset(&t312, 0, sizeof t312);
        gm_tk8sioamep3gv t313 = t312;
        t296 = t313;
ix297:;
        t24 = t296;
        goto me25;
mn293:;
ma38:;
        uint8_t t315 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t315 != ((uint8_t)35ULL)) goto mn314;
        gm_t78m5u6j4a3pq t316 = GM_LD(gm_t78m5u6j4a3pq, (((uint8_t *)t23) + 8));
        F.l27 = t316;
        gm_tk8sioamep3gv t317;
        uint32_t t319 = GM_LD(uint32_t, (((uint8_t *)&F.l27) + 28));
        uint32_t t320 = gm_l1;
        uint8_t t321 = ((t319 == t320));
        if (!t321) goto ie318;
        gm_t78m5u6j4a3pq t323;
        gm_tvdg94bk7aa22 t324 = GM_LD(gm_tvdg94bk7aa22, (((uint8_t *)&F.l27) + 0));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t323 + 0, t324);
        gm_ttovhfee8mf28 t325 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l27) + 12));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t323 + 12, t325);
        gm_ttovhfee8mf28 t326 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l27) + 20));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t323 + 20, t326);
        uint32_t t327 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t323 + 28, t327);
        uint32_t t328 = GM_LD(uint32_t, (((uint8_t *)&F.l27) + 32));
        GM_ST(uint32_t, (uint8_t *)&t323 + 32, t328);
        gm_tjc2ga47o3k6g t329;
        GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t329 + 44, 0);
        GM_ST(uint64_t, (uint8_t *)&t329 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t329 + 0), ((uint8_t)35ULL));
        GM_ST(gm_t78m5u6j4a3pq, ((uint8_t *)&t329 + 8), t323);
        gm_tjc2ga47o3k6g t330 = t329;
        gm_tk8sioamep3gv t331;
        GM_ST(uint8_t, (uint8_t *)&t331 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t331 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t331 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t331), t330);
        ((uint8_t *)&t331)[56] = 1;
        t317 = t331;
        goto ix318;
ie318:;
        gm_tk8sioamep3gv t333;
        memset(&t333, 0, sizeof t333);
        gm_tk8sioamep3gv t334 = t333;
        t317 = t334;
ix318:;
        t24 = t317;
        goto me25;
mn314:;
ma39:;
        uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t336 != ((uint8_t)36ULL)) goto mn335;
        gm_tqj4rgf34oslv t337 = GM_LD(gm_tqj4rgf34oslv, (((uint8_t *)t23) + 8));
        gm_l28 = t337;
        gm_tk8sioamep3gv t338;
        uint32_t t340 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 20));
        uint32_t t341 = gm_l1;
        uint8_t t342 = ((t340 == t341));
        if (!t342) goto ie339;
        gm_tqj4rgf34oslv t344;
        gm_tvdg94bk7aa22 t345 = GM_LD(gm_tvdg94bk7aa22, (((uint8_t *)&gm_l28) + 0));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t344 + 0, t345);
        gm_ttovhfee8mf28 t346 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&gm_l28) + 12));
        GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t344 + 12, t346);
        uint32_t t347 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t344 + 20, t347);
        uint32_t t348 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 24));
        GM_ST(uint32_t, (uint8_t *)&t344 + 24, t348);
        uint32_t t349 = GM_LD(uint32_t, (((uint8_t *)&gm_l28) + 28));
        GM_ST(uint32_t, (uint8_t *)&t344 + 28, t349);
        gm_tjc2ga47o3k6g t350;
        GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t350 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t350 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)36ULL));
        GM_ST(gm_tqj4rgf34oslv, ((uint8_t *)&t350 + 8), t344);
        gm_tjc2ga47o3k6g t351 = t350;
        gm_tk8sioamep3gv t352;
        GM_ST(uint8_t, (uint8_t *)&t352 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t352 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t352 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t352), t351);
        ((uint8_t *)&t352)[56] = 1;
        t338 = t352;
        goto ix339;
ie339:;
        gm_tk8sioamep3gv t354;
        memset(&t354, 0, sizeof t354);
        gm_tk8sioamep3gv t355 = t354;
        t338 = t355;
ix339:;
        t24 = t338;
        goto me25;
mn335:;
ma40:;
        uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t357 != ((uint8_t)37ULL)) goto mn356;
        gm_tfd1kqjeh4les t358 = GM_LD(gm_tfd1kqjeh4les, (((uint8_t *)t23) + 8));
        gm_l29 = t358;
        gm_tk8sioamep3gv t359;
        uint32_t t361 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 4));
        uint32_t t362 = gm_l1;
        uint8_t t363 = ((t361 == t362));
        if (!t363) goto ie360;
        gm_ts6oqppjh0ba3 t365 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l29) + 0));
        uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 2));
        uint32_t t367 = gm_l2;
        uint32_t t368 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 8));
        uint32_t t369 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 12));
        gm_tfd1kqjeh4les t370;
        GM_ST(uint8_t, (uint8_t *)&t370 + 3, 0);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t370 + 0), t365);
        GM_ST(uint8_t, ((uint8_t *)&t370 + 2), t366);
        GM_ST(uint32_t, ((uint8_t *)&t370 + 4), t367);
        GM_ST(uint32_t, ((uint8_t *)&t370 + 8), t368);
        GM_ST(uint32_t, ((uint8_t *)&t370 + 12), t369);
        gm_tjc2ga47o3k6g t371;
        memset(&t371, 0, sizeof t371);
        GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)37ULL));
        GM_ST(gm_tfd1kqjeh4les, ((uint8_t *)&t371 + 8), t370);
        gm_tjc2ga47o3k6g t372 = t371;
        gm_tk8sioamep3gv t373;
        GM_ST(uint8_t, (uint8_t *)&t373 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t373 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t373 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t373), t372);
        ((uint8_t *)&t373)[56] = 1;
        t359 = t373;
        goto ix360;
ie360:;
        gm_tk8sioamep3gv t375;
        memset(&t375, 0, sizeof t375);
        gm_tk8sioamep3gv t376 = t375;
        t359 = t376;
ix360:;
        t24 = t359;
        goto me25;
mn356:;
ma41:;
        uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t378 != ((uint8_t)3ULL)) goto mn377;
        uint32_t t379 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l30 = t379;
        uint32_t t380 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 4));
        gm_l31 = t380;
        uint32_t t381 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 8));
        gm_l32 = t381;
        gm_tk8sioamep3gv t382;
        uint32_t t384 = gm_l30;
        uint32_t t385 = gm_l1;
        uint8_t t386 = ((t384 == t385));
        if (!t386) goto ie383;
        uint32_t t388 = gm_l2;
        uint32_t t389 = gm_l31;
        uint32_t t390 = gm_l32;
        gm_tl874t2atnlh3 t391;
        GM_ST(uint32_t, ((uint8_t *)&t391 + 0), t388);
        GM_ST(uint32_t, ((uint8_t *)&t391 + 4), t389);
        GM_ST(uint32_t, ((uint8_t *)&t391 + 8), t390);
        gm_tjc2ga47o3k6g t392;
        memset(&t392, 0, sizeof t392);
        GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)3ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t392 + 8), t391);
        gm_tjc2ga47o3k6g t393 = t392;
        gm_tk8sioamep3gv t394;
        GM_ST(uint8_t, (uint8_t *)&t394 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t394 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t394 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t394), t393);
        ((uint8_t *)&t394)[56] = 1;
        t382 = t394;
        goto ix383;
ie383:;
        gm_tk8sioamep3gv t396;
        memset(&t396, 0, sizeof t396);
        gm_tk8sioamep3gv t397 = t396;
        t382 = t397;
ix383:;
        t24 = t382;
        goto me25;
mn377:;
ma42:;
        uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t399 != ((uint8_t)17ULL)) goto mn398;
        gm_t04llc5uen0qo t400 = GM_LD(gm_t04llc5uen0qo, (((uint8_t *)t23) + 8));
        gm_l33 = t400;
        gm_tk8sioamep3gv t401;
        uint32_t t403 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 0));
        uint32_t t404 = gm_l1;
        uint8_t t405 = ((t403 == t404));
        if (!t405) goto ie402;
        gm_t04llc5uen0qo t407;
        memset((uint8_t *)&t407 + 20, 0, 4);
        uint32_t t408 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t407 + 0, t408);
        uint32_t t409 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 4));
        GM_ST(uint32_t, (uint8_t *)&t407 + 4, t409);
        uint32_t t410 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 8));
        GM_ST(uint32_t, (uint8_t *)&t407 + 8, t410);
        uint32_t t411 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 12));
        GM_ST(uint32_t, (uint8_t *)&t407 + 12, t411);
        uint32_t t412 = GM_LD(uint32_t, (((uint8_t *)&gm_l33) + 16));
        GM_ST(uint32_t, (uint8_t *)&t407 + 16, t412);
        void * t413 = GM_LD(void *, (((uint8_t *)&gm_l33) + 24));
        GM_ST(void *, (uint8_t *)&t407 + 24, t413);
        gm_tjc2ga47o3k6g t414;
        GM_ST(uint8_t, (uint8_t *)&t414 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t414 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t414 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t414 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t414 + 0), ((uint8_t)17ULL));
        GM_ST(gm_t04llc5uen0qo, ((uint8_t *)&t414 + 8), t407);
        gm_tjc2ga47o3k6g t415 = t414;
        gm_tk8sioamep3gv t416;
        GM_ST(uint8_t, (uint8_t *)&t416 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t416 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t416 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t416), t415);
        ((uint8_t *)&t416)[56] = 1;
        t401 = t416;
        goto ix402;
ie402:;
        gm_tk8sioamep3gv t418;
        memset(&t418, 0, sizeof t418);
        gm_tk8sioamep3gv t419 = t418;
        t401 = t419;
ix402:;
        t24 = t401;
        goto me25;
mn398:;
ma43:;
        uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t421 != ((uint8_t)21ULL)) goto mn420;
        gm_t1lvkimu9u0hv t422 = GM_LD(gm_t1lvkimu9u0hv, (((uint8_t *)t23) + 8));
        F.l34 = t422;
        gm_tk8sioamep3gv t423;
        uint32_t t425 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 0));
        uint32_t t426 = gm_l1;
        uint8_t t427 = ((t425 == t426));
        if (!t427) goto ie424;
        gm_t1lvkimu9u0hv t429;
        uint32_t t430 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t429 + 0, t430);
        uint32_t t431 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 4));
        GM_ST(uint32_t, (uint8_t *)&t429 + 4, t431);
        uint32_t t432 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 8));
        GM_ST(uint32_t, (uint8_t *)&t429 + 8, t432);
        uint32_t t433 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 12));
        GM_ST(uint32_t, (uint8_t *)&t429 + 12, t433);
        uint64_t t434 = GM_LD(uint64_t, (((uint8_t *)&F.l34) + 16));
        GM_ST(uint64_t, (uint8_t *)&t429 + 16, t434);
        uint32_t t435 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 24));
        GM_ST(uint32_t, (uint8_t *)&t429 + 24, t435);
        uint32_t t436 = GM_LD(uint32_t, (((uint8_t *)&F.l34) + 28));
        GM_ST(uint32_t, (uint8_t *)&t429 + 28, t436);
        void * t437 = GM_LD(void *, (((uint8_t *)&F.l34) + 32));
        GM_ST(void *, (uint8_t *)&t429 + 32, t437);
        gm_tjc2ga47o3k6g t438;
        GM_ST(uint8_t, (uint8_t *)&t438 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t438 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t438 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t438 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t438 + 0), ((uint8_t)21ULL));
        GM_ST(gm_t1lvkimu9u0hv, ((uint8_t *)&t438 + 8), t429);
        gm_tjc2ga47o3k6g t439 = t438;
        gm_tk8sioamep3gv t440;
        GM_ST(uint8_t, (uint8_t *)&t440 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t440 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t440 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t440), t439);
        ((uint8_t *)&t440)[56] = 1;
        t423 = t440;
        goto ix424;
ie424:;
        gm_tk8sioamep3gv t442;
        memset(&t442, 0, sizeof t442);
        gm_tk8sioamep3gv t443 = t442;
        t423 = t443;
ix424:;
        t24 = t423;
        goto me25;
mn420:;
ma44:;
        uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t445 != ((uint8_t)53ULL)) goto mn444;
        uint32_t t446 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l35 = t446;
        uint32_t t447 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 4));
        gm_l36 = t447;
        uint32_t t448 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 8));
        gm_l37 = t448;
        gm_tk8sioamep3gv t449;
        uint32_t t451 = gm_l35;
        uint32_t t452 = gm_l1;
        uint8_t t453 = ((t451 == t452));
        if (!t453) goto ie450;
        uint32_t t455 = gm_l2;
        uint32_t t456 = gm_l36;
        uint32_t t457 = gm_l37;
        gm_tl874t2atnlh3 t458;
        GM_ST(uint32_t, ((uint8_t *)&t458 + 0), t455);
        GM_ST(uint32_t, ((uint8_t *)&t458 + 4), t456);
        GM_ST(uint32_t, ((uint8_t *)&t458 + 8), t457);
        gm_tjc2ga47o3k6g t459;
        memset(&t459, 0, sizeof t459);
        GM_ST(uint8_t, ((uint8_t *)&t459 + 0), ((uint8_t)53ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t459 + 8), t458);
        gm_tjc2ga47o3k6g t460 = t459;
        gm_tk8sioamep3gv t461;
        GM_ST(uint8_t, (uint8_t *)&t461 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t461 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t461 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t461), t460);
        ((uint8_t *)&t461)[56] = 1;
        t449 = t461;
        goto ix450;
ie450:;
        gm_tk8sioamep3gv t463;
        memset(&t463, 0, sizeof t463);
        gm_tk8sioamep3gv t464 = t463;
        t449 = t464;
ix450:;
        t24 = t449;
        goto me25;
mn444:;
ma45:;
        uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t466 != ((uint8_t)61ULL)) goto mn465;
        uint32_t t467 = GM_LD(uint32_t, ((((uint8_t *)t23) + 8) + 0));
        gm_l38 = t467;
        void * t468 = GM_LD(void *, ((((uint8_t *)t23) + 8) + 8));
        gm_l39 = t468;
        gm_tk8sioamep3gv t469;
        uint32_t t471 = gm_l38;
        uint32_t t472 = gm_l1;
        uint8_t t473 = ((t471 == t472));
        if (!t473) goto ie470;
        uint32_t t475 = gm_l2;
        void * t476 = gm_l39;
        gm_ttggq7ng4kp3p t477;
        GM_ST(uint32_t, (uint8_t *)&t477 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t477 + 0), t475);
        GM_ST(void *, ((uint8_t *)&t477 + 8), t476);
        gm_tjc2ga47o3k6g t478;
        memset(&t478, 0, sizeof t478);
        GM_ST(uint8_t, ((uint8_t *)&t478 + 0), ((uint8_t)61ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t478 + 8), t477);
        gm_tjc2ga47o3k6g t479 = t478;
        gm_tk8sioamep3gv t480;
        GM_ST(uint8_t, (uint8_t *)&t480 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t480 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t480 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t480), t479);
        ((uint8_t *)&t480)[56] = 1;
        t469 = t480;
        goto ix470;
ie470:;
        gm_tk8sioamep3gv t482;
        memset(&t482, 0, sizeof t482);
        gm_tk8sioamep3gv t483 = t482;
        t469 = t483;
ix470:;
        t24 = t469;
        goto me25;
mn465:;
ma46:;
        uint8_t t485 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t485 != ((uint8_t)47ULL)) goto mn484;
        gm_thaddei6om6bp t486 = GM_LD(gm_thaddei6om6bp, (((uint8_t *)t23) + 8));
        F.l40 = t486;
        gm_tk8sioamep3gv t487;
        uint8_t t489 = GM_LD(uint8_t, ((((uint8_t *)&F.l40) + 24) + 0));
        if (t489 != ((uint8_t)1ULL)) goto ie488;
        uint32_t t490 = GM_LD(uint32_t, ((((uint8_t *)&F.l40) + 24) + 4));
        gm_l41 = t490;
        uint32_t t491 = gm_l41;
        uint32_t t492 = gm_l1;
        uint8_t t493 = ((t491 == t492));
        if (!t493) goto ie488;
        gm_thaddei6om6bp t495;
        memset((uint8_t *)&t495 + 4, 0, 4);
        uint32_t t496 = GM_LD(uint32_t, (((uint8_t *)&F.l40) + 0));
        GM_ST(uint32_t, (uint8_t *)&t495 + 0, t496);
        gm_b16_8 t497 = GM_LD(gm_b16_8, (((uint8_t *)&F.l40) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t495 + 8, t497);
        uint32_t t498 = gm_l2;
        gm_tj2m9dbh1ekv6 t499;
        GM_ST(uint8_t, (uint8_t *)&t499 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t499 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t499 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t499 + 4), t498);
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t495 + 24, t499);
        void * t500 = GM_LD(void *, (((uint8_t *)&F.l40) + 32));
        GM_ST(void *, (uint8_t *)&t495 + 32, t500);
        gm_tjc2ga47o3k6g t501;
        GM_ST(uint8_t, (uint8_t *)&t501 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t501 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t501 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t501 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t501 + 0), ((uint8_t)47ULL));
        GM_ST(gm_thaddei6om6bp, ((uint8_t *)&t501 + 8), t495);
        gm_tjc2ga47o3k6g t502 = t501;
        gm_tk8sioamep3gv t503;
        GM_ST(uint8_t, (uint8_t *)&t503 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t503 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t503 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t503), t502);
        ((uint8_t *)&t503)[56] = 1;
        t487 = t503;
        goto ix488;
ie488:;
        gm_tk8sioamep3gv t505;
        memset(&t505, 0, sizeof t505);
        gm_tk8sioamep3gv t506 = t505;
        t487 = t506;
ix488:;
        t24 = t487;
        goto me25;
mn484:;
ma47:;
        uint8_t t508 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t508 != ((uint8_t)48ULL)) goto mn507;
        gm_t9ka92sj5labh t509 = GM_LD(gm_t9ka92sj5labh, (((uint8_t *)t23) + 8));
        F.l42 = t509;
        gm_tk8sioamep3gv t510;
        uint8_t t512 = GM_LD(uint8_t, ((((uint8_t *)&F.l42) + 32) + 0));
        if (t512 != ((uint8_t)1ULL)) goto ie511;
        uint32_t t513 = GM_LD(uint32_t, ((((uint8_t *)&F.l42) + 32) + 4));
        gm_l43 = t513;
        uint32_t t514 = gm_l43;
        uint32_t t515 = gm_l1;
        uint8_t t516 = ((t514 == t515));
        if (!t516) goto ie511;
        gm_t9ka92sj5labh t518;
        memset((uint8_t *)&t518 + 4, 0, 4);
        uint32_t t519 = GM_LD(uint32_t, (((uint8_t *)&F.l42) + 0));
        GM_ST(uint32_t, (uint8_t *)&t518 + 0, t519);
        void * t520 = GM_LD(void *, (((uint8_t *)&F.l42) + 8));
        GM_ST(void *, (uint8_t *)&t518 + 8, t520);
        gm_b16_8 t521 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t518 + 16, t521);
        uint32_t t522 = gm_l2;
        gm_tj2m9dbh1ekv6 t523;
        GM_ST(uint8_t, (uint8_t *)&t523 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t523 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t523 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t523 + 4), t522);
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t518 + 32, t523);
        void * t524 = GM_LD(void *, (((uint8_t *)&F.l42) + 40));
        GM_ST(void *, (uint8_t *)&t518 + 40, t524);
        gm_tjc2ga47o3k6g t525;
        GM_ST(uint8_t, (uint8_t *)&t525 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t525 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t525 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t525 + 0), ((uint8_t)48ULL));
        GM_ST(gm_t9ka92sj5labh, ((uint8_t *)&t525 + 8), t518);
        gm_tjc2ga47o3k6g t526 = t525;
        gm_tk8sioamep3gv t527;
        GM_ST(uint8_t, (uint8_t *)&t527 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t527 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t527 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t527), t526);
        ((uint8_t *)&t527)[56] = 1;
        t510 = t527;
        goto ix511;
ie511:;
        gm_tk8sioamep3gv t529;
        memset(&t529, 0, sizeof t529);
        gm_tk8sioamep3gv t530 = t529;
        t510 = t530;
ix511:;
        t24 = t510;
        goto me25;
mn507:;
ma48:;
        uint8_t t532 = GM_LD(uint8_t, (((uint8_t *)t23) + 0));
        if (t532 != ((uint8_t)50ULL)) goto mn531;
        gm_tbeu0qv6bbs2s t533 = GM_LD(gm_tbeu0qv6bbs2s, (((uint8_t *)t23) + 8));
        gm_l44 = t533;
        gm_tk8sioamep3gv t534;
        uint8_t t536 = GM_LD(uint8_t, ((((uint8_t *)&gm_l44) + 16) + 0));
        if (t536 != ((uint8_t)1ULL)) goto ie535;
        uint32_t t537 = GM_LD(uint32_t, ((((uint8_t *)&gm_l44) + 16) + 4));
        gm_l45 = t537;
        uint32_t t538 = gm_l45;
        uint32_t t539 = gm_l1;
        uint8_t t540 = ((t538 == t539));
        if (!t540) goto ie535;
        gm_tbeu0qv6bbs2s t542;
        gm_b16_8 t543 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t542 + 0, t543);
        uint32_t t544 = gm_l2;
        gm_tj2m9dbh1ekv6 t545;
        GM_ST(uint8_t, (uint8_t *)&t545 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t545 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t545 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t545 + 4), t544);
        GM_ST(gm_tj2m9dbh1ekv6, (uint8_t *)&t542 + 16, t545);
        void * t546 = GM_LD(void *, (((uint8_t *)&gm_l44) + 24));
        GM_ST(void *, (uint8_t *)&t542 + 24, t546);
        gm_tjc2ga47o3k6g t547;
        GM_ST(uint8_t, (uint8_t *)&t547 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t547 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t547 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t547 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t547 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t547 + 0), ((uint8_t)50ULL));
        GM_ST(gm_tbeu0qv6bbs2s, ((uint8_t *)&t547 + 8), t542);
        gm_tjc2ga47o3k6g t548 = t547;
        gm_tk8sioamep3gv t549;
        GM_ST(uint8_t, (uint8_t *)&t549 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t549 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t549 + 60, 0);
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)&t549), t548);
        ((uint8_t *)&t549)[56] = 1;
        t534 = t549;
        goto ix535;
ie535:;
        gm_tk8sioamep3gv t551;
        memset(&t551, 0, sizeof t551);
        gm_tk8sioamep3gv t552 = t551;
        t534 = t552;
ix535:;
        t24 = t534;
        goto me25;
mn531:;
ma49:;
        gm_tk8sioamep3gv t554;
        memset(&t554, 0, sizeof t554);
        t24 = t554;
        goto me25;
mn553:;
ma50:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 317, 16);
me25:;
        F.l46 = t24;
    }
    {
        gm_tk8sioamep3gv t555 = F.l46;
        gm_tk8sioamep3gv t556 = t555;
        gm_tjc2ga47o3k6g t557;
        if (((((uint8_t *)&t556))[56] != 0)) {
            gm_tjc2ga47o3k6g t558 = GM_LD(gm_tjc2ga47o3k6g, (((uint8_t *)&t556) + 0));
            t557 = t558;
        } else {
            uint8_t t559 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l47 = t557;
    }
    {
        void * t560 = gm_l4;
        gm_tjc2ga47o3k6g t561 = F.l47;
        GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)t560), t561);
    }
    {
        uint8_t t562 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_local_8ov1tn5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_vc_set_local_8ov1tn5 F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* value */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* size */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l3 = t3;
    }
    {
        uint64_t t4 = gm_l3;
        uint64_t t5 = (uintptr_t)t4;
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 8));
        uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)t7) + 88) + 8);
        if (t5 >= (uintptr_t)(t8)) gm_trap_index(t5, t8, gm_srcutmnmd444bu0, 1526, 13);
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t7) + 88)) + t5 * 8));
        gm_l4 = t9;
    }
    {
        uint64_t t11 = gm_l4;
        uint8_t t12 = (t11 <= ((uint64_t)8ULL));
        if (!t12) goto ie10;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 104));
        uint64_t t15 = gm_l4;
        uint8_t t16 = ((t14 == t15));
        if (!t16) goto ie10;
        {
            void * t18 = gm_l0;
            uint32_t t19 = gm_l1;
            void * t20 = gm_l2;
            gm_b0_1 t21 = gm_f_vc_set_word_local_p05lnpr((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t22;
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t25 = gm_l2;
        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 104));
        uint64_t t27 = gm_l4;
        uint8_t t28 = ((t26 == t27));
        if (!t28) goto ie24;
        void * t29 = gm_l0;
        void * t30 = gm_l2;
        uint8_t t31 = gm_f_vc_builds_d9pue9e((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t31) goto ie24;
        {
            void * t33 = gm_l0;
            uint32_t t34 = gm_l1;
            void * t35 = gm_l2;
            gm_b0_1 t36 = gm_f_vc_call_to_local_qd1fddh((void *)NULL, cx, NULL, t33, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t37;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t39 = gm_l0;
        uint32_t t40 = gm_l1;
        void * t41 = gm_l2;
        gm_ttggq7ng4kp3p t42;
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t40);
        GM_ST(void *, ((uint8_t *)&t42 + 8), t41);
        gm_t2k9dbsv5ff58 t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t43 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t43 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)2ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t43 + 8), t42);
        gm_tjc2ga47o3k6g t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t44 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)64ULL));
        GM_ST(gm_t2k9dbsv5ff58, ((uint8_t *)&t44 + 8), t43);
        gm_b0_1 t45 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t39, &t44);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_local_slot_l28kec5(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vc_set_local_slot_l28kec5 F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* l */
    uint32_t gm_l2; /* at */
    uint32_t gm_l3; /* size */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l5 = t3;
    }
    {
        uint64_t t5 = gm_l5;
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 8));
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 120) + 8);
        if (t6 >= (uintptr_t)(t9)) gm_trap_index(t6, t9, gm_srcutmnmd444bu0, 1559, 8);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 120)) + t6 * 1));
        if (!t10) goto ie4;
        {
            void * t12 = gm_l0;
            gm_tc9l77ul3aqid t13;
            uint32_t t14 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t13 + 0, t14);
            uint32_t t15 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, t15);
            uint32_t t16 = gm_l3;
            GM_ST(uint32_t, (uint8_t *)&t13 + 8, t16);
            gm_tl874t2atnlh3 t17 = gm_l4;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t13 + 12, t17);
            gm_tjc2ga47o3k6g t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)51ULL));
            GM_ST(gm_tc9l77ul3aqid, ((uint8_t *)&t18 + 8), t13);
            gm_b0_1 t19 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t12, &t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix4;
ie4:;
        {
            void * t21 = gm_l0;
            uint64_t t22 = gm_l5;
            uint64_t t23 = (uintptr_t)t22;
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 8));
            uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 72) + 8);
            if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_srcutmnmd444bu0, 1562, 29);
            uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 72)) + t23 * 8));
            uint32_t t28 = (uint32_t)t27;
            uint32_t t29 = gm_l2;
            uint32_t t30 = gm_l3;
            gm_tl874t2atnlh3 t31;
            GM_ST(uint32_t, ((uint8_t *)&t31 + 0), t28);
            GM_ST(uint32_t, ((uint8_t *)&t31 + 4), t29);
            GM_ST(uint32_t, ((uint8_t *)&t31 + 8), t30);
            gm_tjc2ga47o3k6g t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)52ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t32 + 8), t31);
            gm_b0_1 t33 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t21, &t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix4:;
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
extern __attribute__((aligned(4))) void gm_f_vc_set_top_h0qmvd1(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_set_word_local_p05lnpr(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_vc_set_word_local_p05lnpr F;
    void * gm_l0; /* c */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* value */
    uint32_t gm_l3; /* r */
    uint64_t gm_l4; /* i */
    uint32_t gm_l5; /* save */
    uint32_t gm_l6; /* v */
    uint32_t gm_l7; /* size */
    GM_STACK_CHECK(&F, 542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_vc_local_reg_bgdr42m(t3, t4);
        gm_t9805qtonbsag t6 = t5;
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            void * t9 = gm_l0;
            void * t10 = gm_l2;
            uint32_t t11 = gm_l3;
            gm_b0_1 t12 = gm_f_vc_word_to_fgjor3m((void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
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
        uint32_t t15 = gm_l1;
        uint64_t t16 = (uint64_t)t15;
        gm_l4 = t16;
    }
    {
        void * t17 = gm_l0;
        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 224));
        gm_l5 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l2;
        uint32_t t21 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t19, t20, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t21;
    }
    {
        uint64_t t22 = gm_l4;
        uint64_t t23 = (uintptr_t)t22;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 8));
        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 88) + 8);
        if (t23 >= (uintptr_t)(t26)) gm_trap_index(t23, t26, gm_srcutmnmd444bu0, 1547, 13);
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 88)) + t23 * 8));
        uint32_t t28 = (uint32_t)t27;
        gm_l7 = t28;
    }
    {
        uint64_t t30 = gm_l4;
        uint64_t t31 = (uintptr_t)t30;
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 120) + 8);
        if (t31 >= (uintptr_t)(t34)) gm_trap_index(t31, t34, gm_srcutmnmd444bu0, 1548, 8);
        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 120)) + t31 * 1));
        if (!t35) goto ie29;
        {
            void * t37 = gm_l0;
            gm_t0m68qtp7uj1u t38;
            uint32_t t39 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t38 + 0, t39);
            uint64_t t40 = gm_l4;
            uint64_t t41 = (uintptr_t)t40;
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 8));
            uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 72) + 8);
            if (t41 >= (uintptr_t)(t44)) gm_trap_index(t41, t44, gm_srcutmnmd444bu0, 1549, 32);
            uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 72)) + t41 * 8));
            uint32_t t46 = (uint32_t)t45;
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, t46);
            uint32_t t47 = gm_l7;
            GM_ST(uint32_t, (uint8_t *)&t38 + 8, t47);
            uint32_t t48 = gm_l6;
            GM_ST(uint32_t, (uint8_t *)&t38 + 12, t48);
            void * t49 = gm_l2;
            GM_ST(void *, (uint8_t *)&t38 + 16, t49);
            gm_tjc2ga47o3k6g t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t0m68qtp7uj1u, ((uint8_t *)&t50 + 8), t38);
            gm_b0_1 t51 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t37, &t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix29;
ie29:;
        {
            void * t53 = gm_l0;
            uint64_t t54 = gm_l4;
            uint64_t t55 = (uintptr_t)t54;
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, (((uint8_t *)t56) + 8));
            uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)t57) + 72) + 8);
            if (t55 >= (uintptr_t)(t58)) gm_trap_index(t55, t58, gm_srcutmnmd444bu0, 1551, 31);
            uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t57) + 72)) + t55 * 8));
            uint32_t t60 = (uint32_t)t59;
            uint32_t t61 = gm_l7;
            uint32_t t62 = gm_l6;
            gm_tl874t2atnlh3 t63;
            GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t60);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 4), t61);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 8), t62);
            gm_tjc2ga47o3k6g t64;
            memset(&t64, 0, sizeof t64);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)4ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t64 + 8), t63);
            gm_b0_1 t65 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t53, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix29:;
    }
    {
        void * t66 = gm_l0;
        uint32_t t67 = gm_l5;
        GM_ST(uint32_t, (((uint8_t *)t66) + 224), t67);
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
inline __attribute__((aligned(4))) uint8_t gm_f_vc_slice_builds_2cdjc7j(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_slice_builds_2cdjc7j F;
    void * gm_l0; /* c */
    void * gm_l2; /* l */
    void * gm_l3; /* h */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, (((uint8_t *)a1) + 8));
        void * t4 = t3;
        if (!(GM_LD(void *, ((uint8_t *)&t4)) != 0)) goto ie2;
        void * t5 = GM_LD(void *, ((uint8_t *)&t4));
        gm_l2 = t5;
        void * t6 = gm_l2;
        uint8_t t7 = gm_f_is_word_82qsidt(t6);
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie2;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = GM_LD(void *, (((uint8_t *)a1) + 16));
        void * t13 = t12;
        if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie11;
        void * t14 = GM_LD(void *, ((uint8_t *)&t13));
        gm_l3 = t14;
        void * t15 = gm_l3;
        uint8_t t16 = gm_f_is_word_82qsidt(t15);
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie11;
        {
            uint8_t t19 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint8_t t20;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        goto po23;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t27 != ((uint8_t)2ULL)) goto pa26;
        goto po23;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t29 != ((uint8_t)3ULL)) goto pa28;
        goto po23;
pa28:;
        goto mn22;
po23:;
        t20 = ((uint8_t)1);
        goto me21;
mn22:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t31 != ((uint8_t)0ULL)) goto mn30;
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)a1) + 0));
        gm_t9qkh0rsooc8d t34 = gm_f_vc_placeable_ip26ve2(t32, t33);
        gm_t9qkh0rsooc8d t35 = t34;
        uint8_t t36;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        switch (t42) { case ((uint8_t)0ULL): goto ma40; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma38; default: goto ma41; }
ma38:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t44 != ((uint8_t)2ULL)) goto mn43;
        t36 = ((uint8_t)1);
        goto me37;
mn43:;
ma39:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t46 != ((uint8_t)1ULL)) goto mn45;
        uint8_t t47 = 0;
        void * t48 = GM_LD(void *, (((uint8_t *)a1) + 0));
        uint32_t t49 = gm_f_root_levels_umkq7vp(t48);
        uint8_t t50 = ((t49 == ((uint32_t)0ULL)));
        if (t50) {
            void * t51 = gm_l0;
            void * t52 = GM_LD(void *, (((uint8_t *)a1) + 0));
            uint8_t t53 = gm_f_vc_has_index_b75va8s(t51, t52);
            uint8_t t54 = (uint8_t)!t53;
            t47 = t54;
        } else t47 = 0;
        t36 = t47;
        goto me37;
mn45:;
ma40:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t56 != ((uint8_t)0ULL)) goto mn55;
        t36 = ((uint8_t)0);
        goto me37;
mn55:;
ma41:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1285, 19);
me37:;
        t20 = t36;
        goto me21;
mn30:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1283, 12);
me21:;
        uint8_t t57 = 0;
        F.ret = t20;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_vc_slice_builds_2cdjc7j_c(void * a0, gm_tt49maei14cl6 a1) { return gm_f_vc_slice_builds_2cdjc7j(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_slice_get_klv78kv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, uint64_t a4) {
    struct gm_fr_vc_slice_get_klv78kv F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* base */
    void * gm_l3; /* index */
    uint64_t gm_l4; /* esz */
    uint32_t gm_l5; /* save */
    gm_tcc1rq4n1avb2 gm_l6; /* sl */
    uint32_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 325u);
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
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        void * t6 = gm_l0;
        void * t7 = gm_l3;
        uint8_t t8 = gm_f_vc_reads_only_vncte32(t6, t7);
        gm_tcc1rq4n1avb2 t9 = gm_f_vc_slice_ops_35to7ql((void *)NULL, cx, NULL, t4, t5, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t9;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l3;
        uint32_t t12 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t10, t11, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t12;
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_l5;
        GM_ST(uint32_t, (((uint8_t *)t13) + 224), t14);
    }
    {
        void * t15 = gm_l0;
        gm_t1lvkimu9u0hv t16;
        void * t17 = gm_l0;
        uint32_t t18 = gm_f_vc_alloc_3hmfvtt(t17);
        GM_ST(uint32_t, (uint8_t *)&t16 + 0, t18);
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, t19);
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        GM_ST(uint32_t, (uint8_t *)&t16 + 8, t20);
        uint32_t t21 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t16 + 12, t21);
        uint64_t t22 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t16 + 16, t22);
        void * t23 = gm_l1;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 104));
        uint32_t t25 = (uint32_t)t24;
        GM_ST(uint32_t, (uint8_t *)&t16 + 24, t25);
        void * t26 = gm_l0;
        void * t27 = gm_l1;
        uint32_t t28 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint32_t, (uint8_t *)&t16 + 28, t28);
        void * t29 = gm_l1;
        GM_ST(void *, (uint8_t *)&t16 + 32, t29);
        gm_tjc2ga47o3k6g t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)21ULL));
        GM_ST(gm_t1lvkimu9u0hv, ((uint8_t *)&t30 + 8), t16);
        gm_b0_1 t31 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t15, &t30);
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
inline __attribute__((aligned(4))) gm_tcc1rq4n1avb2 gm_f_vc_slice_ops_35to7ql(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_vc_slice_ops_35to7ql F;
    void * gm_l0; /* c */
    void * gm_l1; /* base */
    uint8_t gm_l2; /* late */
    gm_tcc1rq4n1avb2 gm_l3; /* r */
    uint32_t gm_l5; /* p */
    uint32_t gm_l6; /* l */
    uint64_t gm_l7; /* pb */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        if (!t3) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l1;
            gm_t736djc66po0l t8 = gm_f_vc_slice_regs_en7lrqs(t6, t7);
            gm_t736djc66po0l t9 = t8;
            if (!((((uint8_t *)&t9))[8] != 0)) goto ie5;
            gm_tcc1rq4n1avb2 t10 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)&t9));
            gm_l3 = t10;
            {
                gm_tcc1rq4n1avb2 t12 = gm_l3;
                uint8_t t13 = 0;
                F.ret = t12;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_f_vc_place_i1sksai((void *)&F.l4, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = gm_l0;
        uint32_t t17 = gm_f_vc_alloc_3hmfvtt(t16);
        gm_l5 = t17;
    }
    {
        void * t18 = gm_l0;
        uint32_t t19 = gm_f_vc_alloc_3hmfvtt(t18);
        gm_l6 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 176));
        gm_l7 = t22;
    }
    {
        void * t23 = gm_l0;
        uint32_t t24 = gm_l5;
        gm_tiutk3b96cnlp t25 = F.l4;
        uint64_t t26 = gm_l7;
        uint64_t t27 = gm_l7;
        uint32_t t28 = (uint32_t)t27;
        void * t29 = gm_l1;
        gm_b0_1 t30 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t23, t24, &t25, ((uint64_t)0ULL), t26, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_l6;
        gm_tiutk3b96cnlp t33 = F.l4;
        uint64_t t34 = gm_l7;
        uint64_t t35 = gm_l7;
        uint64_t t36 = gm_l7;
        uint32_t t37 = (uint32_t)t36;
        void * t38 = gm_l1;
        gm_b0_1 t39 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t31, t32, &t33, t34, t35, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t40 = gm_l5;
        uint32_t t41 = gm_l6;
        gm_tcc1rq4n1avb2 t42;
        GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t40);
        GM_ST(uint32_t, ((uint8_t *)&t42 + 4), t41);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_slice_qr9et9a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint32_t a3) {
    struct gm_fr_vc_slice_qr9et9a F;
    void * gm_l0; /* c */
    void * gm_l2; /* n */
    uint32_t gm_l3; /* at */
    uint32_t gm_l4; /* save */
    uint64_t gm_l5; /* pb */
    uint32_t gm_l6; /* rp */
    uint32_t gm_l7; /* rl */
    uint8_t gm_l8; /* has_len */
    uint32_t gm_l9; /* sa */
    uint32_t gm_l11; /* v */
    uint32_t gm_l12; /* r */
    uint64_t gm_l13; /* off */
    uint32_t gm_l14; /* l */
    uint64_t gm_l15; /* rel */
    void * gm_l16; /* h */
    uint8_t gm_l17; /* late */
    uint32_t gm_l18; /* lo */
    void * gm_l19; /* l */
    uint32_t gm_l20; /* hi */
    void * gm_l21; /* h */
    GM_STACK_CHECK(&F, 1879u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        uint32_t t8 = gm_f_vc_alloc_3hmfvtt(t7);
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_vc_alloc_3hmfvtt(t9);
        gm_l7 = t10;
    }
    {
        gm_l8 = ((uint8_t)1);
    }
    {
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; default: goto ma15; }
ma12:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t18 != ((uint8_t)1ULL)) goto mn17;
        {
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, (((uint8_t *)a1) + 0));
            gm_t9qkh0rsooc8d t23 = gm_f_vc_placeable_ip26ve2(t21, t22);
            gm_t9qkh0rsooc8d t24 = t23;
            uint8_t t25 = ((GM_LD(uint8_t, ((uint8_t *)&t24) + 0) == ((uint8_t)0ULL)));
            if (!t25) goto ie20;
            {
                void * t27 = gm_l0;
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, (((uint8_t *)a1) + 0));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 104));
                uint64_t t31 = gm_f_vc_alloc_slot_5eptdbb(t28, t30);
                uint32_t t32 = gm_f_vc_slot_at_rc6jhbj(t27, t31);
                gm_l9 = t32;
            }
            {
                void * t33 = gm_l0;
                void * t34 = GM_LD(void *, (((uint8_t *)a1) + 0));
                uint32_t t35 = gm_l9;
                gm_b0_1 t36 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t33, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t37 = gm_l0;
                uint32_t t38 = gm_l6;
                uint32_t t39 = gm_l9;
                uint64_t t40 = gm_l5;
                uint32_t t41 = (uint32_t)t40;
                gm_tl874t2atnlh3 t42;
                GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t38);
                GM_ST(uint32_t, ((uint8_t *)&t42 + 4), t39);
                GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t41);
                gm_tjc2ga47o3k6g t43;
                memset(&t43, 0, sizeof t43);
                GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)53ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t43 + 8), t42);
                gm_b0_1 t44 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t37, &t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t45 = gm_l0;
                uint32_t t46 = gm_l7;
                uint32_t t47 = gm_l9;
                uint64_t t48 = gm_l5;
                uint32_t t49 = (uint32_t)t48;
                uint32_t r50; bool o51 = __builtin_add_overflow(t47, t49, &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1314, 38);
                uint32_t t52 = r50;
                uint64_t t53 = gm_l5;
                uint32_t t54 = (uint32_t)t53;
                gm_tl874t2atnlh3 t55;
                GM_ST(uint32_t, ((uint8_t *)&t55 + 0), t46);
                GM_ST(uint32_t, ((uint8_t *)&t55 + 4), t52);
                GM_ST(uint32_t, ((uint8_t *)&t55 + 8), t54);
                gm_tjc2ga47o3k6g t56;
                memset(&t56, 0, sizeof t56);
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)53ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t56 + 8), t55);
                gm_b0_1 t57 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t45, &t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix20;
ie20:;
            {
                void * t59 = gm_l0;
                void * t60 = GM_LD(void *, (((uint8_t *)a1) + 0));
                gm_f_vc_place_i1sksai((void *)&F.l10, (void *)NULL, cx, NULL, t59, t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t61 = gm_l0;
                uint32_t t62 = gm_l6;
                gm_tiutk3b96cnlp t63 = F.l10;
                uint64_t t64 = gm_l5;
                uint64_t t65 = gm_l5;
                uint32_t t66 = (uint32_t)t65;
                void * t67 = GM_LD(void *, (((uint8_t *)a1) + 0));
                gm_b0_1 t68 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t61, t62, &t63, ((uint64_t)0ULL), t64, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t69 = gm_l0;
                uint32_t t70 = gm_l7;
                gm_tiutk3b96cnlp t71 = F.l10;
                uint64_t t72 = gm_l5;
                uint64_t t73 = gm_l5;
                uint64_t t74 = gm_l5;
                uint32_t t75 = (uint32_t)t74;
                void * t76 = GM_LD(void *, (((uint8_t *)a1) + 0));
                gm_b0_1 t77 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t69, t70, &t71, t72, t73, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix20:;
        }
        goto me11;
mn17:;
ma13:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t81 != ((uint8_t)2ULL)) goto pa80;
        goto po79;
pa80:;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t83 != ((uint8_t)3ULL)) goto pa82;
        goto po79;
pa82:;
        goto mn78;
po79:;
        {
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, (((uint8_t *)a1) + 0));
            uint32_t t87 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t85, t86, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t87;
        }
        {
            void * t88 = gm_l0;
            uint32_t t89 = gm_l6;
            uint32_t t90 = gm_l11;
            gm_tcc1rq4n1avb2 t91;
            GM_ST(uint32_t, ((uint8_t *)&t91 + 0), t89);
            GM_ST(uint32_t, ((uint8_t *)&t91 + 4), t90);
            gm_tjc2ga47o3k6g t92;
            memset(&t92, 0, sizeof t92);
            GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)65ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t92 + 8), t91);
            gm_b0_1 t93 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t88, &t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t94 = gm_l0;
            uint32_t t95 = gm_l4;
            uint32_t r96; bool o97 = __builtin_add_overflow(t95, ((uint32_t)2ULL), &r96);
            if (o97) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1324, 26);
            uint32_t t98 = r96;
            gm_f_vc_set_top_h0qmvd1(t94, t98);
        }
        {
            uint8_t t99 = ((GM_LD(uint8_t, (((uint8_t *)a1) + 25) + 0) == ((uint8_t)3ULL)));
            gm_l8 = t99;
        }
        {
            void * t100 = gm_l0;
            uint32_t t101 = gm_l7;
            uint64_t t102;
            uint8_t t104 = gm_l8;
            if (!t104) goto ie103;
            uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)a1) + 40));
            uint64_t t107 = t106;
            t102 = t107;
            goto ix103;
ie103:;
            uint64_t t109 = ((uint64_t)0ULL);
            t102 = t109;
ix103:;
            gm_tb31k5l794t7p t110;
            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t110 + 0), t101);
            GM_ST(uint64_t, ((uint8_t *)&t110 + 8), t102);
            gm_tjc2ga47o3k6g t111;
            memset(&t111, 0, sizeof t111);
            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t111 + 8), t110);
            gm_b0_1 t112 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t100, &t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me11;
mn78:;
ma14:;
        uint8_t t114 = GM_LD(uint8_t, ((((uint8_t *)a1) + 25) + 0));
        if (t114 != ((uint8_t)0ULL)) goto mn113;
        {
            void * t116 = gm_l0;
            void * t117 = GM_LD(void *, (((uint8_t *)a1) + 0));
            gm_tiutk3b96cnlp t118;
            gm_f_vc_place_i1sksai((void *)&t118, (void *)NULL, cx, NULL, t116, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tiutk3b96cnlp t119 = t118;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t119) + 0));
            if (t122 != ((uint8_t)1ULL)) goto mn121;
            uint32_t t123 = GM_LD(uint32_t, ((((uint8_t *)&t119) + 8) + 0));
            gm_l12 = t123;
            uint64_t t124 = GM_LD(uint64_t, ((((uint8_t *)&t119) + 8) + 8));
            gm_l13 = t124;
            {
                void * t126 = gm_l0;
                uint32_t t127 = gm_l6;
                uint32_t t128 = gm_l12;
                gm_tcc1rq4n1avb2 t129;
                GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t127);
                GM_ST(uint32_t, ((uint8_t *)&t129 + 4), t128);
                gm_tjc2ga47o3k6g t130;
                memset(&t130, 0, sizeof t130);
                GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)65ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t130 + 8), t129);
                gm_b0_1 t131 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t126, &t130);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t133 = gm_l13;
                uint8_t t134 = (!(t133 == ((uint64_t)0ULL)));
                if (!t134) goto ie132;
                {
                    void * t136 = gm_l0;
                    uint32_t t137 = gm_l6;
                    uint64_t t138 = gm_l13;
                    gm_tb31k5l794t7p t139;
                    GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t139 + 0), t137);
                    GM_ST(uint64_t, ((uint8_t *)&t139 + 8), t138);
                    gm_tjc2ga47o3k6g t140;
                    memset(&t140, 0, sizeof t140);
                    GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)24ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t140 + 8), t139);
                    gm_b0_1 t141 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t136, &t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix132;
ie132:;
ix132:;
            }
            goto me120;
mn121:;
            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t119) + 0));
            if (t143 != ((uint8_t)0ULL)) goto mn142;
            uint32_t t144 = GM_LD(uint32_t, ((((uint8_t *)&t119) + 8) + 4));
            gm_l14 = t144;
            uint64_t t145 = GM_LD(uint64_t, ((((uint8_t *)&t119) + 8) + 8));
            gm_l15 = t145;
            {
                void * t147 = gm_l0;
                uint32_t t148 = gm_l6;
                uint32_t t149 = gm_l14;
                void * t150 = gm_l2;
                gm_to8e1ootf3q7f t151;
                GM_ST(uint32_t, ((uint8_t *)&t151 + 0), t148);
                GM_ST(uint32_t, ((uint8_t *)&t151 + 4), t149);
                GM_ST(void *, ((uint8_t *)&t151 + 8), t150);
                gm_tjc2ga47o3k6g t152;
                memset(&t152, 0, sizeof t152);
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)59ULL));
                GM_ST(gm_to8e1ootf3q7f, ((uint8_t *)&t152 + 8), t151);
                gm_b0_1 t153 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t147, &t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t155 = gm_l15;
                uint8_t t156 = (!(t155 == ((uint64_t)0ULL)));
                if (!t156) goto ie154;
                {
                    void * t158 = gm_l0;
                    uint32_t t159 = gm_l6;
                    uint64_t t160 = gm_l15;
                    gm_tb31k5l794t7p t161;
                    GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t161 + 0), t159);
                    GM_ST(uint64_t, ((uint8_t *)&t161 + 8), t160);
                    gm_tjc2ga47o3k6g t162;
                    memset(&t162, 0, sizeof t162);
                    GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)24ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t162 + 8), t161);
                    gm_b0_1 t163 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t158, &t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix154;
ie154:;
ix154:;
            }
            goto me120;
mn142:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1329, 13);
me120:;
        }
        {
            void * t164 = gm_l0;
            uint32_t t165 = gm_l4;
            uint32_t r166; bool o167 = __builtin_add_overflow(t165, ((uint32_t)2ULL), &r166);
            if (o167) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1339, 26);
            uint32_t t168 = r166;
            gm_f_vc_set_top_h0qmvd1(t164, t168);
        }
        {
            void * t169 = gm_l0;
            uint32_t t170 = gm_l7;
            uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)a1) + 40));
            gm_tb31k5l794t7p t172;
            GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t172 + 0), t170);
            GM_ST(uint64_t, ((uint8_t *)&t172 + 8), t171);
            gm_tjc2ga47o3k6g t173;
            memset(&t173, 0, sizeof t173);
            GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t173 + 8), t172);
            gm_b0_1 t174 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t169, &t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me11;
mn113:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1308, 5);
me11:;
    }
    {
        uint8_t t175;
        void * t177 = GM_LD(void *, (((uint8_t *)a1) + 16));
        void * t178 = t177;
        if (!(GM_LD(void *, ((uint8_t *)&t178)) != 0)) goto ie176;
        void * t179 = GM_LD(void *, ((uint8_t *)&t178));
        gm_l16 = t179;
        void * t181 = gm_l0;
        void * t182 = gm_l16;
        uint8_t t183 = gm_f_vc_reads_only_vncte32(t181, t182);
        uint8_t t184 = t183;
        t175 = t184;
        goto ix176;
ie176:;
        uint8_t t186 = ((uint8_t)1);
        t175 = t186;
ix176:;
        gm_l17 = t175;
    }
    {
        uint32_t t187 = gm_l7;
        gm_l18 = t187;
    }
    {
        void * t189 = GM_LD(void *, (((uint8_t *)a1) + 8));
        void * t190 = t189;
        if (!(GM_LD(void *, ((uint8_t *)&t190)) != 0)) goto ie188;
        void * t191 = GM_LD(void *, ((uint8_t *)&t190));
        gm_l19 = t191;
        {
            void * t193 = gm_l0;
            void * t194 = gm_l19;
            uint8_t t195 = gm_l17;
            uint32_t t196 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t193, t194, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t196;
        }
        goto ix188;
ie188:;
        {
            void * t198 = gm_l0;
            uint32_t t199 = gm_f_vc_alloc_3hmfvtt(t198);
            gm_l18 = t199;
        }
        {
            void * t200 = gm_l0;
            uint32_t t201 = gm_l18;
            gm_tb31k5l794t7p t202;
            GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t202 + 0), t201);
            GM_ST(uint64_t, ((uint8_t *)&t202 + 8), ((uint64_t)0ULL));
            gm_tjc2ga47o3k6g t203;
            memset(&t203, 0, sizeof t203);
            GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t203 + 8), t202);
            gm_b0_1 t204 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t200, &t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix188:;
    }
    {
        uint32_t t205 = gm_l7;
        gm_l20 = t205;
    }
    {
        void * t207 = GM_LD(void *, (((uint8_t *)a1) + 16));
        void * t208 = t207;
        if (!(GM_LD(void *, ((uint8_t *)&t208)) != 0)) goto ie206;
        void * t209 = GM_LD(void *, ((uint8_t *)&t208));
        gm_l21 = t209;
        {
            void * t211 = gm_l0;
            void * t212 = gm_l21;
            uint32_t t213 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t211, t212, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t213;
        }
        goto ix206;
ie206:;
ix206:;
    }
    {
        void * t214 = gm_l0;
        gm_td7fbab3thbb2 t215;
        memset((uint8_t *)&t215 + 22, 0, 2);
        uint32_t t216 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t215 + 0, t216);
        uint32_t t217 = gm_l6;
        GM_ST(uint32_t, (uint8_t *)&t215 + 4, t217);
        uint32_t t218 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t215 + 8, t218);
        uint32_t t219 = gm_l18;
        GM_ST(uint32_t, (uint8_t *)&t215 + 12, t219);
        uint32_t t220 = gm_l20;
        GM_ST(uint32_t, (uint8_t *)&t215 + 16, t220);
        uint8_t t221 = gm_l8;
        GM_ST(uint8_t, (uint8_t *)&t215 + 20, t221);
        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)a1) + 24));
        GM_ST(uint8_t, (uint8_t *)&t215 + 21, t222);
        uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)a1) + 32));
        GM_ST(uint64_t, (uint8_t *)&t215 + 24, t223);
        void * t224 = gm_l2;
        GM_ST(void *, (uint8_t *)&t215 + 32, t224);
        gm_tjc2ga47o3k6g t225;
        GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t225 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)58ULL));
        GM_ST(gm_td7fbab3thbb2, ((uint8_t *)&t225 + 8), t215);
        gm_b0_1 t226 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t214, &t225);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t227 = gm_l0;
        uint32_t t228 = gm_l4;
        gm_f_vc_set_top_h0qmvd1(t227, t228);
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
inline __attribute__((aligned(4))) gm_t736djc66po0l gm_f_vc_slice_regs_en7lrqs(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_slice_regs_en7lrqs F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tb31k5l794t7p gm_l2; /* root */
    uint64_t gm_l3; /* off */
    GM_STACK_CHECK(&F, 455u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 90));
        uint8_t t7 = (uint8_t)!t6;
        if (!t7) {
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 176));
            uint8_t t11 = (!(t10 == ((uint64_t)8ULL)));
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_t736djc66po0l t13;
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
        void * t15 = gm_l0;
        void * t16 = gm_l1;
        gm_tgmjda3f8g6hg t17;
        gm_f_vc_field_root_lb3os0t((void *)&t17, t15, t16);
        gm_tgmjda3f8g6hg t18 = t17;
        gm_tb31k5l794t7p t19;
        if (((((uint8_t *)&t18))[16] != 0)) {
            gm_tb31k5l794t7p t20 = GM_LD(gm_tb31k5l794t7p, (((uint8_t *)&t18) + 0));
            t19 = t20;
        } else {
            gm_t736djc66po0l t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l2 = t19;
    }
    {
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint64_t t25 = (uint64_t)t24;
        uint64_t t26 = (uintptr_t)t25;
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 8));
        uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)t28) + 120) + 8);
        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_srcutmnmd444bu0, 234, 8);
        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t28) + 120)) + t26 * 1));
        if (!t30) goto ie23;
        {
            gm_t736djc66po0l t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        uint64_t t35 = (uint64_t)t34;
        uint64_t t36 = (uintptr_t)t35;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 8));
        uint64_t t39 = GM_LD(uintptr_t, (((uint8_t *)t38) + 72) + 8);
        if (t36 >= (uintptr_t)(t39)) gm_trap_index(t36, t39, gm_srcutmnmd444bu0, 235, 12);
        uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t38) + 72)) + t36 * 8));
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 235, 12);
        uint64_t t44 = r42;
        gm_l3 = t44;
    }
    {
        uint64_t t46 = gm_l3;
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 236, 8);
        uint64_t t47 = (uint64_t)(t46 % ((uint64_t)8ULL));
        uint8_t t48 = (!(t47 == ((uint64_t)0ULL)));
        if (!t48) goto ie45;
        {
            gm_t736djc66po0l t50;
            memset(&t50, 0, sizeof t50);
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        uint64_t t52 = gm_l3;
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 237, 17);
        uint64_t t53 = (uint64_t)(t52 / ((uint64_t)8ULL));
        uint32_t t54 = (uint32_t)t53;
        uint64_t t55 = gm_l3;
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 237, 44);
        uint64_t t56 = (uint64_t)(t55 / ((uint64_t)8ULL));
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 237, 44);
        uint64_t t59 = r57;
        uint32_t t60 = (uint32_t)t59;
        gm_tcc1rq4n1avb2 t61;
        GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t54);
        GM_ST(uint32_t, ((uint8_t *)&t61 + 4), t60);
        gm_t736djc66po0l t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 10, 0);
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t62), t61);
        ((uint8_t *)&t62)[8] = 1;
        uint8_t t63 = 0;
        F.ret = t62;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_vc_slot_at_rc6jhbj(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_stmt_01mcb8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_vc_stmt_01mcb8k F;
    void * gm_l0; /* c */
    uint32_t gm_l2; /* save */
    uint64_t gm_l3; /* slots */
    uint32_t gm_l4; /* leaks */
    uint8_t gm_l5; /* mark */
    uint32_t gm_l6; /* m_reg */
    uint32_t gm_l7; /* m_at */
    void * gm_l8; /* e */
    uint32_t gm_l9; /* l */
    void * gm_l10; /* value */
    uint32_t gm_l11; /* l */
    uint64_t gm_l12; /* size */
    void * gm_l13; /* init */
    void * gm_l14; /* x */
    void * gm_l15; /* x */
    uint8_t gm_l16; /* is_err */
    void * gm_l17; /* body */
    void * gm_l18; /* place */
    void * gm_l19; /* value */
    gm_tcc1rq4n1avb2 gm_l21; /* sl */
    uint32_t gm_l22; /* i */
    uint32_t gm_l23; /* v */
    uint32_t gm_l25; /* v */
    uint32_t gm_l26; /* size */
    uint32_t gm_l27; /* levels */
    uint32_t gm_l28; /* l */
    uint64_t gm_l29; /* rel */
    uint8_t gm_l30; /* packed */
    gm_t9805qtonbsag gm_l31; /* d */
    uint32_t gm_l32; /* dr */
    uint32_t gm_l33; /* base */
    uint64_t gm_l34; /* off */
    uint8_t gm_l35; /* packed */
    uint32_t gm_l37; /* at */
    uint32_t gm_l38; /* size */
    uint32_t gm_l39; /* l */
    uint64_t gm_l40; /* rel */
    uint32_t gm_l41; /* base */
    uint64_t gm_l42; /* off */
    uint8_t gm_l43; /* packed */
    GM_STACK_CHECK(&F, 3230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 272));
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l0;
        uint8_t t9 = gm_f_vc_uses_scratch_9rt07d8(t8, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)a1)));
        gm_l5 = t9;
    }
    {
        gm_l6 = ((uint32_t)0ULL);
    }
    {
        gm_l7 = ((uint32_t)0ULL);
    }
    {
        uint8_t t11 = gm_l5;
        if (!t11) goto ie10;
        {
            void * t13 = gm_l0;
            uint32_t t14 = gm_f_vc_alloc_3hmfvtt(t13);
            gm_l6 = t14;
        }
        {
            {
                void * t16 = gm_l0;
                uint32_t t17 = gm_f_vc_alloc_3hmfvtt(t16);
            }
        }
        {
            {
                void * t19 = gm_l0;
                uint32_t t20 = gm_f_vc_alloc_3hmfvtt(t19);
            }
        }
        {
            void * t21 = gm_l0;
            uint32_t t22 = gm_l6;
            gm_tjc2ga47o3k6g t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)76ULL));
            GM_ST(uint32_t, ((uint8_t *)&t23 + 8), t22);
            uint32_t t24 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t21, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t24;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t33) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma31; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma29; default: goto ma31; }
ma26:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t35 != ((uint8_t)5ULL)) goto mn34;
        void * t36 = GM_LD(void *, (((uint8_t *)a1) + 8));
        gm_l8 = t36;
        void * t37 = gm_l0;
        void * t38 = gm_l8;
        gm_tmn7n0n9raou5 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
        gm_b0_1 t40 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me25;
mn34:;
ma27:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        uint32_t t43 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 0));
        gm_l9 = t43;
        void * t44 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
        gm_l10 = t44;
        void * t45 = gm_l0;
        uint32_t t46 = gm_l9;
        void * t47 = gm_l10;
        gm_b0_1 t48 = gm_f_vc_set_local_8ov1tn5((void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me25;
mn41:;
ma28:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t50 != ((uint8_t)0ULL)) goto mn49;
        uint32_t t51 = GM_LD(uint32_t, ((((uint8_t *)a1) + 8) + 0));
        gm_l11 = t51;
        uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 16));
        gm_l12 = t52;
        void * t53 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 24));
        gm_l13 = t53;
        {
            void * t56 = gm_l13;
            void * t57 = t56;
            if (!(GM_LD(void *, ((uint8_t *)&t57)) != 0)) goto ie55;
            void * t58 = GM_LD(void *, ((uint8_t *)&t57));
            gm_l14 = t58;
            uint64_t t59 = gm_l12;
            uint8_t t60 = (t59 <= ((uint64_t)8ULL));
            if (!t60) goto ie55;
            void * t61 = gm_l14;
            uint8_t t62 = gm_f_is_word_82qsidt(t61);
            if (!t62) goto ie55;
            uint64_t t63 = gm_l12;
            void * t64 = gm_l14;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 104));
            uint8_t t66 = ((t63 == t65));
            if (!t66) goto ie55;
            {
                void * t68 = gm_l0;
                uint32_t t69 = gm_l11;
                void * t70 = gm_l14;
                gm_b0_1 t71 = gm_f_vc_set_word_local_p05lnpr((void *)NULL, cx, NULL, t68, t69, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix55;
ie55:;
            void * t73 = gm_l13;
            void * t74 = t73;
            if (!(GM_LD(void *, ((uint8_t *)&t74)) != 0)) goto ie72;
            void * t75 = GM_LD(void *, ((uint8_t *)&t74));
            gm_l15 = t75;
            uint64_t t76 = gm_l12;
            void * t77 = gm_l15;
            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 104));
            uint8_t t79 = ((t76 == t78));
            if (!t79) goto ie72;
            void * t80 = gm_l0;
            void * t81 = gm_l15;
            uint8_t t82 = gm_f_vc_builds_d9pue9e((void *)NULL, cx, NULL, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t82) goto ie72;
            {
                void * t84 = gm_l0;
                uint32_t t85 = gm_l11;
                void * t86 = gm_l15;
                gm_b0_1 t87 = gm_f_vc_call_to_local_qd1fddh((void *)NULL, cx, NULL, t84, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix72;
ie72:;
            {
                void * t89 = gm_l0;
                gm_t2k9dbsv5ff58 t90 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)a1));
                gm_tjc2ga47o3k6g t91;
                GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t91 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)64ULL));
                GM_ST(gm_t2k9dbsv5ff58, ((uint8_t *)&t91 + 8), t90);
                gm_b0_1 t92 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t89, &t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix72:;
ix55:;
        }
        goto me25;
mn49:;
ma29:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t94 != ((uint8_t)6ULL)) goto mn93;
        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)a1) + 8) + 0));
        gm_l16 = t95;
        void * t96 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
        gm_l17 = t96;
        void * t97 = gm_l0;
        uint8_t t98 = gm_l16;
        void * t99 = gm_l17;
        gm_tgtub0v94d5u5 t100;
        GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), t98);
        GM_ST(void *, ((uint8_t *)&t100 + 8), t99);
        gm_tjc2ga47o3k6g t101;
        memset(&t101, 0, sizeof t101);
        GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)67ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t101 + 8), t100);
        gm_b0_1 t102 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t97, &t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me25;
mn93:;
ma30:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t104 != ((uint8_t)3ULL)) goto mn103;
        void * t105 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 0));
        gm_l18 = t105;
        void * t106 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
        gm_l19 = t106;
        {
            void * t109 = gm_l19;
            uint8_t t110 = gm_f_is_word_82qsidt(t109);
            if (!t110) goto ie108;
            void * t111 = gm_l19;
            uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)t111) + 104));
            void * t113 = gm_l18;
            uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)t113) + 104));
            uint8_t t115 = ((t112 == t114));
            if (!t115) goto ie108;
            void * t116 = gm_l0;
            void * t117 = gm_l18;
            gm_t9805qtonbsag t118 = gm_f_vc_place_reg_e0pc35g(t116, t117);
            gm_t9805qtonbsag t119 = t118;
            uint8_t t120;
            if (((((uint8_t *)&t119))[4] != 0)) goto mn122;
            t120 = ((uint8_t)0);
            goto me121;
mn122:;
            t120 = ((uint8_t)1);
            goto me121;
mn123:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1467, 64);
me121:;
            if (!t120) goto ie108;
            {
                void * t125 = gm_l0;
                void * t126 = gm_l19;
                void * t127 = gm_l0;
                void * t128 = gm_l18;
                gm_t9805qtonbsag t129 = gm_f_vc_place_reg_e0pc35g(t127, t128);
                gm_t9805qtonbsag t130 = t129;
                uint32_t t131;
                if (((((uint8_t *)&t130))[4] != 0)) {
                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&t130) + 0));
                    t131 = t132;
                } else {
                    t131 = ((uint32_t)0ULL);
                }
                gm_b0_1 t133 = gm_f_vc_word_to_fgjor3m((void *)NULL, cx, NULL, t125, t126, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix108;
ie108:;
            void * t135 = gm_l18;
            uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t135) + 0) + 0));
            if (t136 != ((uint8_t)12ULL)) goto ie134;
            gm_tp0fa5qv7rqnn t137 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t135) + 0) + 8));
            F.l20 = t137;
            uint8_t t138 = ((GM_LD(uint8_t, (((uint8_t *)&F.l20) + 16) + 0) == ((uint8_t)1ULL)));
            if (!t138) goto ie134;
            void * t139 = gm_l19;
            uint8_t t140 = gm_f_is_word_82qsidt(t139);
            if (!t140) goto ie134;
            void * t141 = gm_l19;
            uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 104));
            void * t143 = gm_l18;
            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)t143) + 104));
            uint8_t t145 = ((t142 == t144));
            if (!t145) goto ie134;
            void * t146 = gm_l0;
            void * t147 = gm_l19;
            uint8_t t148 = gm_f_vc_plain_bs0ofeb(t146, t147);
            if (!t148) goto ie134;
            void * t149 = gm_l0;
            void * t150 = gm_l18;
            gm_t9qkh0rsooc8d t151 = gm_f_vc_placeable_ip26ve2(t149, t150);
            gm_t9qkh0rsooc8d t152 = t151;
            uint8_t t153 = (!(GM_LD(uint8_t, ((uint8_t *)&t152) + 0) == ((uint8_t)0ULL)));
            if (!t153) goto ie134;
            {
                void * t155 = gm_l0;
                void * t156 = GM_LD(void *, (((uint8_t *)&F.l20) + 0));
                void * t157 = gm_l0;
                void * t158 = GM_LD(void *, (((uint8_t *)&F.l20) + 8));
                uint8_t t159 = gm_f_vc_reads_only_vncte32(t157, t158);
                gm_tcc1rq4n1avb2 t160 = gm_f_vc_slice_ops_35to7ql((void *)NULL, cx, NULL, t155, t156, t159);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t160;
            }
            {
                void * t161 = gm_l0;
                void * t162 = GM_LD(void *, (((uint8_t *)&F.l20) + 8));
                uint32_t t163 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t161, t162, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t163;
            }
            {
                void * t164 = gm_l0;
                void * t165 = gm_l19;
                uint32_t t166 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t164, t165, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t166;
            }
            {
                void * t167 = gm_l0;
                gm_thbsr19fadfjo t168;
                memset((uint8_t *)&t168 + 12, 0, 4);
                memset((uint8_t *)&t168 + 36, 0, 4);
                uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 0));
                GM_ST(uint32_t, (uint8_t *)&t168 + 0, t169);
                uint32_t t170 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 4));
                GM_ST(uint32_t, (uint8_t *)&t168 + 4, t170);
                uint32_t t171 = gm_l22;
                GM_ST(uint32_t, (uint8_t *)&t168 + 8, t171);
                uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 24));
                GM_ST(uint64_t, (uint8_t *)&t168 + 16, t172);
                void * t173 = gm_l18;
                uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)t173) + 104));
                uint32_t t175 = (uint32_t)t174;
                GM_ST(uint32_t, (uint8_t *)&t168 + 24, t175);
                void * t176 = gm_l0;
                void * t177 = gm_l18;
                uint32_t t178 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t176, t177);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint32_t, (uint8_t *)&t168 + 28, t178);
                uint32_t t179 = gm_l23;
                GM_ST(uint32_t, (uint8_t *)&t168 + 32, t179);
                void * t180 = gm_l18;
                GM_ST(void *, (uint8_t *)&t168 + 40, t180);
                gm_tjc2ga47o3k6g t181;
                GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t181 + 0), ((uint8_t)22ULL));
                GM_ST(gm_thbsr19fadfjo, ((uint8_t *)&t181 + 8), t168);
                gm_b0_1 t182 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t167, &t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix134;
ie134:;
            void * t184 = gm_l19;
            uint8_t t185 = gm_f_is_word_82qsidt(t184);
            if (!t185) goto ie183;
            void * t186 = gm_l19;
            uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)t186) + 104));
            void * t188 = gm_l18;
            uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)t188) + 104));
            uint8_t t190 = ((t187 == t189));
            if (!t190) goto ie183;
            void * t191 = gm_l0;
            void * t192 = gm_l18;
            gm_t9qkh0rsooc8d t193 = gm_f_vc_placeable_ip26ve2(t191, t192);
            gm_t9qkh0rsooc8d t194 = t193;
            uint8_t t195 = (!(GM_LD(uint8_t, ((uint8_t *)&t194) + 0) == ((uint8_t)0ULL)));
            if (!t195) goto ie183;
            {
                void * t197 = gm_l0;
                void * t198 = gm_l18;
                gm_f_vc_place_i1sksai((void *)&F.l24, (void *)NULL, cx, NULL, t197, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t199 = gm_l0;
                void * t200 = gm_l19;
                uint32_t t201 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t199, t200, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t201;
            }
            {
                void * t202 = gm_l18;
                uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)t202) + 104));
                uint32_t t204 = (uint32_t)t203;
                gm_l26 = t204;
            }
            {
                uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                if (t207 != ((uint8_t)0ULL)) goto mn206;
                uint32_t t208 = GM_LD(uint32_t, ((((uint8_t *)&F.l24) + 8) + 0));
                gm_l27 = t208;
                uint32_t t209 = GM_LD(uint32_t, ((((uint8_t *)&F.l24) + 8) + 4));
                gm_l28 = t209;
                uint64_t t210 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 8) + 8));
                gm_l29 = t210;
                uint8_t t211 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 8) + 16));
                gm_l30 = t211;
                gm_t9805qtonbsag t212 = GM_LD(gm_t9805qtonbsag, ((((uint8_t *)&F.l24) + 8) + 20));
                gm_l31 = t212;
                {
                    uint32_t t215 = gm_l27;
                    uint8_t t216 = (t215 > ((uint32_t)0ULL));
                    if (!t216) goto ie214;
                    {
                        void * t218 = gm_l0;
                        gm_tn3qmqcq7vsk9 t219;
                        uint32_t t220 = gm_l27;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 0, t220);
                        uint32_t t221 = gm_l28;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 4, t221);
                        uint64_t t222 = gm_l29;
                        uint32_t t223 = (uint32_t)t222;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 8, t223);
                        uint32_t t224 = gm_l26;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 12, t224);
                        uint32_t t225;
                        uint8_t t227 = gm_l30;
                        if (!t227) goto ie226;
                        uint32_t t229 = ((uint32_t)1ULL);
                        t225 = t229;
                        goto ix226;
ie226:;
                        void * t231 = gm_l0;
                        void * t232 = gm_l18;
                        uint32_t t233 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t231, t232);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint32_t t234 = t233;
                        t225 = t234;
ix226:;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 16, t225);
                        uint32_t t235 = gm_l25;
                        GM_ST(uint32_t, (uint8_t *)&t219 + 20, t235);
                        void * t236 = gm_l18;
                        GM_ST(void *, (uint8_t *)&t219 + 24, t236);
                        gm_tjc2ga47o3k6g t237;
                        GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t237 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t237 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)13ULL));
                        GM_ST(gm_tn3qmqcq7vsk9, ((uint8_t *)&t237 + 8), t219);
                        gm_b0_1 t238 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t218, &t237);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix214;
ie214:;
                    if (!((((uint8_t *)&gm_l31))[4] != 0)) goto ie239;
                    uint32_t t240 = GM_LD(uint32_t, ((uint8_t *)&gm_l31));
                    gm_l32 = t240;
                    {
                        void * t242 = gm_l0;
                        uint32_t t243 = gm_l28;
                        uint64_t t244 = (uint64_t)t243;
                        uint64_t t245 = (uintptr_t)t244;
                        void * t246 = gm_l0;
                        void * t247 = GM_LD(void *, (((uint8_t *)t246) + 8));
                        uint64_t t248 = GM_LD(uintptr_t, (((uint8_t *)t247) + 72) + 8);
                        if (t245 >= (uintptr_t)(t248)) gm_trap_index(t245, t248, gm_srcutmnmd444bu0, 1485, 53);
                        uint64_t t249 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t247) + 72)) + t245 * 8));
                        uint64_t t250 = gm_l29;
                        uint64_t r251; bool o252 = __builtin_add_overflow(t249, t250, &r251);
                        if (o252) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1485, 53);
                        uint64_t t253 = r251;
                        uint32_t t254 = (uint32_t)t253;
                        uint32_t t255 = gm_l32;
                        uint32_t t256 = gm_l26;
                        uint32_t t257 = gm_l25;
                        gm_thpdbp3q7rdrc t258;
                        GM_ST(uint32_t, ((uint8_t *)&t258 + 0), t254);
                        GM_ST(uint32_t, ((uint8_t *)&t258 + 4), t255);
                        GM_ST(uint32_t, ((uint8_t *)&t258 + 8), t256);
                        GM_ST(uint32_t, ((uint8_t *)&t258 + 12), t257);
                        gm_tjc2ga47o3k6g t259;
                        memset(&t259, 0, sizeof t259);
                        GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_thpdbp3q7rdrc, ((uint8_t *)&t259 + 8), t258);
                        gm_b0_1 t260 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t242, &t259);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix239;
ie239:;
                    {
                        void * t262 = gm_l0;
                        uint32_t t263 = gm_l28;
                        uint64_t t264 = (uint64_t)t263;
                        uint64_t t265 = (uintptr_t)t264;
                        void * t266 = gm_l0;
                        void * t267 = GM_LD(void *, (((uint8_t *)t266) + 8));
                        uint64_t t268 = GM_LD(uintptr_t, (((uint8_t *)t267) + 72) + 8);
                        if (t265 >= (uintptr_t)(t268)) gm_trap_index(t265, t268, gm_srcutmnmd444bu0, 1487, 52);
                        uint64_t t269 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t267) + 72)) + t265 * 8));
                        uint64_t t270 = gm_l29;
                        uint64_t r271; bool o272 = __builtin_add_overflow(t269, t270, &r271);
                        if (o272) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1487, 52);
                        uint64_t t273 = r271;
                        uint32_t t274 = (uint32_t)t273;
                        uint32_t t275 = gm_l26;
                        uint32_t t276 = gm_l25;
                        gm_tl874t2atnlh3 t277;
                        GM_ST(uint32_t, ((uint8_t *)&t277 + 0), t274);
                        GM_ST(uint32_t, ((uint8_t *)&t277 + 4), t275);
                        GM_ST(uint32_t, ((uint8_t *)&t277 + 8), t276);
                        gm_tjc2ga47o3k6g t278;
                        memset(&t278, 0, sizeof t278);
                        GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t278 + 8), t277);
                        gm_b0_1 t279 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t262, &t278);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
ix239:;
ix214:;
                }
                goto me205;
mn206:;
                uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&F.l24) + 0));
                if (t281 != ((uint8_t)1ULL)) goto mn280;
                uint32_t t282 = GM_LD(uint32_t, ((((uint8_t *)&F.l24) + 8) + 0));
                gm_l33 = t282;
                uint64_t t283 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 8) + 8));
                gm_l34 = t283;
                uint8_t t284 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 8) + 16));
                gm_l35 = t284;
                void * t285 = gm_l0;
                gm_t04llc5uen0qo t286;
                memset((uint8_t *)&t286 + 20, 0, 4);
                uint32_t t287 = gm_l33;
                GM_ST(uint32_t, (uint8_t *)&t286 + 0, t287);
                uint64_t t288 = gm_l34;
                uint32_t t289 = (uint32_t)t288;
                GM_ST(uint32_t, (uint8_t *)&t286 + 4, t289);
                uint32_t t290 = gm_l26;
                GM_ST(uint32_t, (uint8_t *)&t286 + 8, t290);
                uint32_t t291;
                uint8_t t293 = gm_l35;
                if (!t293) goto ie292;
                uint32_t t295 = ((uint32_t)1ULL);
                t291 = t295;
                goto ix292;
ie292:;
                void * t297 = gm_l0;
                void * t298 = gm_l18;
                uint32_t t299 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t297, t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t300 = t299;
                t291 = t300;
ix292:;
                GM_ST(uint32_t, (uint8_t *)&t286 + 12, t291);
                uint32_t t301 = gm_l25;
                GM_ST(uint32_t, (uint8_t *)&t286 + 16, t301);
                void * t302 = gm_l18;
                GM_ST(void *, (uint8_t *)&t286 + 24, t302);
                gm_tjc2ga47o3k6g t303;
                GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t303 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t303 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)18ULL));
                GM_ST(gm_t04llc5uen0qo, ((uint8_t *)&t303 + 8), t286);
                gm_b0_1 t304 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t285, &t303);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me205;
mn280:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1480, 17);
me205:;
            }
            goto ix183;
ie183:;
            void * t306 = gm_l19;
            uint8_t t307 = gm_f_is_word_82qsidt(t306);
            if (!t307) goto ie305;
            {
                void * t309 = gm_l0;
                void * t310 = gm_l18;
                void * t311 = gm_l19;
                gm_t3mf58gv9kf3n t312;
                GM_ST(void *, ((uint8_t *)&t312 + 0), t310);
                GM_ST(void *, ((uint8_t *)&t312 + 8), t311);
                gm_tjc2ga47o3k6g t313;
                memset(&t313, 0, sizeof t313);
                GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)69ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t313 + 8), t312);
                gm_b0_1 t314 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t309, &t313);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix305;
ie305:;
            void * t316 = gm_l19;
            uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)t316) + 104));
            void * t318 = gm_l18;
            uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)t318) + 104));
            uint8_t t320 = ((t317 == t319));
            if (!t320) goto ie315;
            void * t321 = gm_l0;
            void * t322 = gm_l19;
            uint8_t t323 = gm_f_vc_builds_d9pue9e((void *)NULL, cx, NULL, t321, t322);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t323) goto ie315;
            void * t324 = gm_l0;
            void * t325 = gm_l18;
            uint8_t t326 = gm_f_vc_copies_glmdmll(t324, t325);
            if (!t326) goto ie315;
            {
                void * t328 = gm_l0;
                void * t329 = gm_l18;
                gm_f_vc_place_i1sksai((void *)&F.l36, (void *)NULL, cx, NULL, t328, t329);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t330 = gm_l0;
                void * t331 = gm_l0;
                void * t332 = gm_l19;
                uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)t332) + 104));
                uint64_t t334 = gm_f_vc_alloc_slot_5eptdbb(t331, t333);
                uint32_t t335 = gm_f_vc_slot_at_rc6jhbj(t330, t334);
                gm_l37 = t335;
            }
            {
                void * t336 = gm_l0;
                void * t337 = gm_l19;
                uint32_t t338 = gm_l37;
                gm_b0_1 t339 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t336, t337, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t340 = gm_l18;
                uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)t340) + 104));
                uint32_t t342 = (uint32_t)t341;
                gm_l38 = t342;
            }
            {
                uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&F.l36) + 0));
                if (t345 != ((uint8_t)0ULL)) goto mn344;
                uint32_t t346 = GM_LD(uint32_t, ((((uint8_t *)&F.l36) + 8) + 4));
                gm_l39 = t346;
                uint64_t t347 = GM_LD(uint64_t, ((((uint8_t *)&F.l36) + 8) + 8));
                gm_l40 = t347;
                void * t348 = gm_l0;
                uint32_t t349 = gm_l39;
                uint64_t t350 = (uint64_t)t349;
                uint64_t t351 = (uintptr_t)t350;
                void * t352 = gm_l0;
                void * t353 = GM_LD(void *, (((uint8_t *)t352) + 8));
                uint64_t t354 = GM_LD(uintptr_t, (((uint8_t *)t353) + 72) + 8);
                if (t351 >= (uintptr_t)(t354)) gm_trap_index(t351, t354, gm_srcutmnmd444bu0, 1501, 69);
                uint64_t t355 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t353) + 72)) + t351 * 8));
                uint64_t t356 = gm_l40;
                uint64_t r357; bool o358 = __builtin_add_overflow(t355, t356, &r357);
                if (o358) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1501, 69);
                uint64_t t359 = r357;
                uint32_t t360 = (uint32_t)t359;
                uint32_t t361 = gm_l37;
                uint32_t t362 = gm_l38;
                gm_tl874t2atnlh3 t363;
                GM_ST(uint32_t, ((uint8_t *)&t363 + 0), t360);
                GM_ST(uint32_t, ((uint8_t *)&t363 + 4), t361);
                GM_ST(uint32_t, ((uint8_t *)&t363 + 8), t362);
                gm_tjc2ga47o3k6g t364;
                memset(&t364, 0, sizeof t364);
                GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)52ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t364 + 8), t363);
                gm_b0_1 t365 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t348, &t364);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me343;
mn344:;
                uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&F.l36) + 0));
                if (t367 != ((uint8_t)1ULL)) goto mn366;
                uint32_t t368 = GM_LD(uint32_t, ((((uint8_t *)&F.l36) + 8) + 0));
                gm_l41 = t368;
                uint64_t t369 = GM_LD(uint64_t, ((((uint8_t *)&F.l36) + 8) + 8));
                gm_l42 = t369;
                uint8_t t370 = GM_LD(uint8_t, ((((uint8_t *)&F.l36) + 8) + 16));
                gm_l43 = t370;
                void * t371 = gm_l0;
                gm_t04llc5uen0qo t372;
                memset((uint8_t *)&t372 + 20, 0, 4);
                uint32_t t373 = gm_l41;
                GM_ST(uint32_t, (uint8_t *)&t372 + 0, t373);
                uint64_t t374 = gm_l42;
                uint32_t t375 = (uint32_t)t374;
                GM_ST(uint32_t, (uint8_t *)&t372 + 4, t375);
                uint32_t t376 = gm_l37;
                GM_ST(uint32_t, (uint8_t *)&t372 + 8, t376);
                uint32_t t377 = gm_l38;
                GM_ST(uint32_t, (uint8_t *)&t372 + 12, t377);
                uint32_t t378;
                uint8_t t380 = gm_l43;
                if (!t380) goto ie379;
                uint32_t t382 = ((uint32_t)1ULL);
                t378 = t382;
                goto ix379;
ie379:;
                void * t384 = gm_l0;
                void * t385 = gm_l18;
                uint32_t t386 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t384, t385);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t387 = t386;
                t378 = t387;
ix379:;
                GM_ST(uint32_t, (uint8_t *)&t372 + 16, t378);
                void * t388 = gm_l18;
                GM_ST(void *, (uint8_t *)&t372 + 24, t388);
                gm_tjc2ga47o3k6g t389;
                GM_ST(uint8_t, (uint8_t *)&t389 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t389 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t389 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t389 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t389 + 0), ((uint8_t)42ULL));
                GM_ST(gm_t04llc5uen0qo, ((uint8_t *)&t389 + 8), t372);
                gm_b0_1 t390 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t371, &t389);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me343;
mn366:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1500, 17);
me343:;
            }
            goto ix315;
ie315:;
            {
                void * t392 = gm_l0;
                gm_t2k9dbsv5ff58 t393 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)a1));
                gm_tjc2ga47o3k6g t394;
                GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t394 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)64ULL));
                GM_ST(gm_t2k9dbsv5ff58, ((uint8_t *)&t394 + 8), t393);
                gm_b0_1 t395 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t392, &t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix315:;
ix305:;
ix183:;
ix134:;
ix108:;
        }
        goto me25;
mn103:;
ma31:;
        void * t397 = gm_l0;
        gm_t2k9dbsv5ff58 t398 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)a1));
        gm_tjc2ga47o3k6g t399;
        GM_ST(uint8_t, (uint8_t *)&t399 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t399 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t399 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t399 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)64ULL));
        GM_ST(gm_t2k9dbsv5ff58, ((uint8_t *)&t399 + 8), t398);
        gm_b0_1 t400 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t397, &t399);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me25;
mn396:;
ma32:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1453, 5);
me25:;
    }
    {
        uint8_t t402 = gm_l5;
        if (!t402) goto ie401;
        {
            void * t405 = gm_l0;
            uint32_t t406 = GM_LD(uint32_t, (((uint8_t *)t405) + 272));
            uint32_t t407 = gm_l4;
            uint8_t t408 = ((t406 == t407));
            if (!t408) goto ie404;
            {
                uint32_t t410 = gm_l7;
                uint64_t t411 = (uint64_t)t410;
                uint64_t t412 = (uintptr_t)t411;
                void * t413 = gm_l0;
                uint64_t t414 = GM_LD(uintptr_t, ((((uint8_t *)t413) + 16) + 0) + 8);
                if (t412 >= (uintptr_t)(t414)) gm_trap_index(t412, t414, gm_srcutmnmd444bu0, 1514, 13);
                gm_tjc2ga47o3k6g t415;
                memset(&t415, 0, sizeof t415);
                GM_ST(uint8_t, ((uint8_t *)&t415 + 0), ((uint8_t)79ULL));
                GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t413) + 16) + 0)) + t412 * 56), t415);
            }
            goto ix404;
ie404:;
            {
                void * t417 = gm_l0;
                uint32_t t418 = gm_l6;
                gm_tjc2ga47o3k6g t419;
                memset(&t419, 0, sizeof t419);
                GM_ST(uint8_t, ((uint8_t *)&t419 + 0), ((uint8_t)77ULL));
                GM_ST(uint32_t, ((uint8_t *)&t419 + 8), t418);
                gm_b0_1 t420 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t417, &t419);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix404:;
        }
        goto ix401;
ie401:;
ix401:;
    }
    {
        void * t421 = gm_l0;
        uint32_t t422 = gm_l4;
        GM_ST(uint32_t, (((uint8_t *)t421) + 272), t422);
    }
    {
        void * t423 = gm_l0;
        uint32_t t424 = gm_l2;
        GM_ST(uint32_t, (((uint8_t *)t423) + 224), t424);
    }
    {
        void * t425 = gm_l0;
        uint64_t t426 = gm_l3;
        GM_ST(uint64_t, (((uint8_t *)t425) + 248), t426);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_to_slot_70oqqr5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_vc_to_slot_70oqqr5 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* at */
    uint32_t gm_l3; /* save */
    uint32_t gm_l4; /* f */
    gm_b16_8 gm_l5; /* args */
    void * gm_l6; /* callee */
    gm_b16_8 gm_l7; /* args */
    void * gm_l9; /* lhs */
    gm_t9805qtonbsag gm_l10; /* capture */
    void * gm_l11; /* rhs */
    void * gm_l12; /* b */
    gm_trl2khmuphncd gm_l13; /* x */
    void * gm_l14; /* scrutinee */
    gm_b16_8 gm_l15; /* arms */
    gm_b16_8 gm_l16; /* b */
    uint32_t gm_l17; /* levels */
    uint32_t gm_l18; /* l */
    uint32_t gm_l19; /* f */
    gm_ts6oqppjh0ba3 gm_l20; /* op */
    void * gm_l21; /* l */
    void * gm_l22; /* r */
    uint64_t gm_l23; /* slots */
    uint32_t gm_l24; /* la */
    uint32_t gm_l25; /* lb */
    uint8_t gm_l26; /* wide */
    gm_ttovhfee8mf28 gm_l27; /* t */
    gm_b16_8 gm_l28; /* items */
    gm_b16_8 gm_l29; /* offs */
    uint64_t gm_l30; /* total */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    void * gm_l33; /* x */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    gm_tmn7n0n9raou5 gm_l37; /* kind */
    void * gm_l38; /* x */
    uint32_t gm_l39; /* r */
    void * gm_l40; /* m */
    gm_t88rcucg1b7uk gm_l41; /* inner */
    gm_b16_8 gm_l42; /* offs */
    void * gm_l43; /* x */
    gm_b16_8 gm_l44; /* offs */
    uint32_t gm_l45; /* variant */
    void * gm_l46; /* payload */
    void * gm_l47; /* m */
    gm_tcmkompbl09il gm_l48; /* u */
    uint32_t gm_l49; /* r */
    void * gm_l50; /* p */
    gm_t083kd7b1kkag gm_l53; /* x */
    void * gm_l54; /* x */
    uint8_t gm_l55; /* is_result */
    gm_tvdg94bk7aa22 gm_l56; /* op */
    gm_b16_8 gm_l57; /* args */
    GM_STACK_CHECK(&F, 5388u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma27; case ((uint8_t)6ULL): goto ma27; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma27; case ((uint8_t)12ULL): goto ma27; case ((uint8_t)13ULL): goto ma22; case ((uint8_t)14ULL): goto ma27; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma27; case ((uint8_t)18ULL): goto ma27; case ((uint8_t)19ULL): goto ma16; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma27; case ((uint8_t)22ULL): goto ma27; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma27; case ((uint8_t)26ULL): goto ma27; case ((uint8_t)27ULL): goto ma27; case ((uint8_t)28ULL): goto ma27; case ((uint8_t)29ULL): goto ma27; case ((uint8_t)30ULL): goto ma27; case ((uint8_t)31ULL): goto ma17; case ((uint8_t)32ULL): goto ma27; case ((uint8_t)33ULL): goto ma19; case ((uint8_t)34ULL): goto ma26; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma18; case ((uint8_t)39ULL): goto ma27; case ((uint8_t)40ULL): goto ma27; case ((uint8_t)41ULL): goto ma27; case ((uint8_t)42ULL): goto ma27; case ((uint8_t)43ULL): goto ma21; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma25; case ((uint8_t)46ULL): goto ma9; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma27; case ((uint8_t)49ULL): goto ma27; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma11; case ((uint8_t)53ULL): goto ma27; case ((uint8_t)54ULL): goto ma27; case ((uint8_t)55ULL): goto ma27; case ((uint8_t)56ULL): goto ma27; case ((uint8_t)57ULL): goto ma27; case ((uint8_t)58ULL): goto ma27; case ((uint8_t)59ULL): goto ma27; case ((uint8_t)60ULL): goto ma27; case ((uint8_t)61ULL): goto ma23; case ((uint8_t)62ULL): goto ma27; default: goto ma27; }
ma6:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t31 != ((uint8_t)35ULL)) goto mn30;
        uint32_t t32 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l4 = t32;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l5 = t33;
        {
            void * t35 = gm_l0;
            uint32_t t36 = gm_l4;
            gm_b16_8 t37 = gm_l5;
            uint32_t t38 = gm_l2;
            gm_tj2m9dbh1ekv6 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)2ULL));
            GM_ST(uint32_t, ((uint8_t *)&t39 + 4), t38);
            void * t40 = gm_l1;
            gm_b0_1 t41 = gm_f_vc_call_27qg9ft((void *)NULL, cx, NULL, t35, t36, t37, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t42;
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto me5;
mn30:;
ma7:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t45 != ((uint8_t)37ULL)) goto mn44;
        void * t46 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l6 = t46;
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l7 = t47;
        {
            void * t49 = gm_l0;
            void * t50 = gm_l6;
            gm_b16_8 t51 = gm_l7;
            uint32_t t52 = gm_l2;
            gm_tj2m9dbh1ekv6 t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)2ULL));
            GM_ST(uint32_t, ((uint8_t *)&t53 + 4), t52);
            void * t54 = gm_l1;
            gm_b0_1 t55 = gm_f_vc_call_value_5j9edsq((void *)NULL, cx, NULL, t49, t50, t51, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t56;
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto me5;
mn44:;
ma8:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t59 != ((uint8_t)36ULL)) goto mn58;
        gm_tf4r0415i49d1 t60 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t4) + 0) + 8));
        F.l8 = t60;
        {
            void * t62 = gm_l0;
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 32));
            uint32_t t64 = gm_l2;
            gm_tj2m9dbh1ekv6 t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)2ULL));
            GM_ST(uint32_t, ((uint8_t *)&t65 + 4), t64);
            void * t66 = gm_l1;
            gm_b0_1 t67 = gm_f_vc_perform_qci0ith((void *)NULL, cx, NULL, t62, t63, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t68;
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto me5;
mn58:;
ma9:;
        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t71 != ((uint8_t)46ULL)) goto mn70;
        void * t72 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l9 = t72;
        gm_t9805qtonbsag t73 = GM_LD(gm_t9805qtonbsag, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l10 = t73;
        void * t74 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l11 = t74;
        void * t76 = gm_l0;
        void * t77 = gm_l1;
        void * t78 = gm_l9;
        gm_t0hcjal40h40i t79;
        gm_f_vc_else_shape_n9vc3pg((void *)&t79, (void *)NULL, cx, NULL, t76, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0hcjal40h40i t80 = t79;
        uint8_t t81;
        if (((((uint8_t *)&t80))[32] != 0)) goto mn83;
        t81 = ((uint8_t)0);
        goto me82;
mn83:;
        t81 = ((uint8_t)1);
        goto me82;
mn84:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1100, 41);
me82:;
        if (!t81) goto ie75;
        {
            void * t86 = gm_l0;
            void * t87 = gm_l1;
            void * t88 = gm_l9;
            gm_t9805qtonbsag t89 = gm_l10;
            void * t90 = gm_l11;
            uint32_t t91 = gm_l2;
            gm_t9805qtonbsag t92;
            GM_ST(uint8_t, (uint8_t *)&t92 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t92 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t92), t91);
            ((uint8_t *)&t92)[4] = 1;
            gm_b0_1 t93 = gm_f_vc_else_9ml9jkf((void *)NULL, cx, NULL, t86, t87, t88, t89, t90, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t94;
            uint8_t t95 = 0;
            F.ret = t94;
            goto gm_fret;
        }
        goto ix75;
ie75:;
ix75:;
        goto me5;
mn70:;
ma10:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t97 != ((uint8_t)51ULL)) goto mn96;
        void * t98 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l12 = t98;
        {
            void * t100 = gm_l0;
            void * t101 = gm_l12;
            uint32_t t102 = gm_l2;
            gm_tmn7n0n9raou5 t103;
            GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t103 + 4), t102);
            gm_b0_1 t104 = gm_f_vc_block_ql7sru3((void *)NULL, cx, NULL, t100, t101, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t105;
            uint8_t t106 = 0;
            F.ret = t105;
            goto gm_fret;
        }
        goto me5;
mn96:;
ma11:;
        uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t108 != ((uint8_t)52ULL)) goto mn107;
        gm_trl2khmuphncd t109 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t4) + 0) + 8));
        gm_l13 = t109;
        {
            void * t111 = gm_l0;
            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
            void * t113 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
            void * t114 = GM_LD(void *, (((uint8_t *)&gm_l13) + 24));
            uint32_t t115 = gm_l2;
            gm_tmn7n0n9raou5 t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t116 + 4), t115);
            gm_b0_1 t117 = gm_f_vc_if_1vdf5lq((void *)NULL, cx, NULL, t111, t112, t113, t114, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t118;
            uint8_t t119 = 0;
            F.ret = t118;
            goto gm_fret;
        }
        goto me5;
mn107:;
ma12:;
        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t121 != ((uint8_t)50ULL)) goto mn120;
        void * t122 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l14 = t122;
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l15 = t123;
        {
            void * t125 = gm_l0;
            void * t126 = gm_l1;
            void * t127 = gm_l14;
            gm_b16_8 t128 = gm_l15;
            uint32_t t129 = gm_l2;
            gm_tmn7n0n9raou5 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t130 + 4), t129);
            gm_b0_1 t131 = gm_f_vc_match_ikc07jv((void *)NULL, cx, NULL, t125, t126, t127, t128, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t132;
            uint8_t t133 = 0;
            F.ret = t132;
            goto gm_fret;
        }
        goto me5;
mn120:;
ma13:;
        uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t135 != ((uint8_t)1ULL)) goto mn134;
        gm_b16_8 t136 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 0) + 8));
        gm_l16 = t136;
        {
            void * t138 = gm_l0;
            gm_tiilmfd16v3fc t139;
            memset((uint8_t *)&t139 + 4, 0, 4);
            uint32_t t140 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t139 + 0, t140);
            gm_b16_8 t141 = gm_l16;
            gm_b16_8 t142 = t141;
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 8, t143);
            gm_tjc2ga47o3k6g t144;
            GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t144 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t144 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t144 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)56ULL));
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t144 + 8), t139);
            gm_b0_1 t145 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t138, &t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t146;
            uint8_t t147 = 0;
            F.ret = t146;
            goto gm_fret;
        }
        goto me5;
mn134:;
ma14:;
        uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t149 != ((uint8_t)2ULL)) goto mn148;
        {
            void * t151 = gm_l0;
            uint32_t t152 = gm_l2;
            void * t153 = gm_l1;
            gm_ttggq7ng4kp3p t154;
            GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t154 + 0), t152);
            GM_ST(void *, ((uint8_t *)&t154 + 8), t153);
            gm_tjc2ga47o3k6g t155;
            memset(&t155, 0, sizeof t155);
            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)46ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t155 + 8), t154);
            gm_b0_1 t156 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t151, &t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t157;
            uint8_t t158 = 0;
            F.ret = t157;
            goto gm_fret;
        }
        goto me5;
mn148:;
ma15:;
        uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t160 != ((uint8_t)7ULL)) goto mn159;
        uint32_t t161 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l17 = t161;
        uint32_t t162 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 4));
        gm_l18 = t162;
        void * t164 = gm_l1;
        uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 104));
        void * t166 = gm_l0;
        void * t167 = GM_LD(void *, (((uint8_t *)t166) + 0));
        void * t168 = gm_l1;
        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t168) + 96));
        uint64_t t170 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t167, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t171 = ((t165 == t170));
        if (!t171) goto ie163;
        uint32_t t172 = gm_l17;
        uint8_t t173 = (t172 > ((uint32_t)0ULL));
        if (!t173) goto ie163;
        {
            void * t175 = gm_l0;
            gm_t0m68qtp7uj1u t176;
            uint32_t t177 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t176 + 0, t177);
            uint32_t t178 = gm_l17;
            GM_ST(uint32_t, (uint8_t *)&t176 + 4, t178);
            uint32_t t179 = gm_l18;
            GM_ST(uint32_t, (uint8_t *)&t176 + 8, t179);
            void * t180 = gm_l1;
            uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)t180) + 104));
            uint32_t t182 = (uint32_t)t181;
            GM_ST(uint32_t, (uint8_t *)&t176 + 12, t182);
            void * t183 = gm_l1;
            GM_ST(void *, (uint8_t *)&t176 + 16, t183);
            gm_tjc2ga47o3k6g t184;
            GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t184 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t184 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t184 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)14ULL));
            GM_ST(gm_t0m68qtp7uj1u, ((uint8_t *)&t184 + 8), t176);
            gm_b0_1 t185 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t175, &t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t186;
            uint8_t t187 = 0;
            F.ret = t186;
            goto gm_fret;
        }
        goto ix163;
ie163:;
ix163:;
        goto me5;
mn159:;
ma16:;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t189 != ((uint8_t)19ULL)) goto mn188;
        uint32_t t190 = GM_LD(uint32_t, ((((uint8_t *)t4) + 0) + 8));
        gm_l19 = t190;
        {
            void * t192 = gm_l0;
            uint32_t t193 = gm_l2;
            uint32_t t194 = gm_l19;
            gm_tcc1rq4n1avb2 t195;
            GM_ST(uint32_t, ((uint8_t *)&t195 + 0), t193);
            GM_ST(uint32_t, ((uint8_t *)&t195 + 4), t194);
            gm_tjc2ga47o3k6g t196;
            memset(&t196, 0, sizeof t196);
            GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)84ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t196 + 8), t195);
            gm_b0_1 t197 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t192, &t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t198;
            uint8_t t199 = 0;
            F.ret = t198;
            goto gm_fret;
        }
        goto me5;
mn188:;
ma17:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t201 != ((uint8_t)31ULL)) goto mn200;
        gm_ts6oqppjh0ba3 t202 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l20 = t202;
        void * t203 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l21 = t203;
        void * t204 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l22 = t204;
        {
            void * t206 = gm_l0;
            uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)t206) + 248));
            gm_l23 = t207;
        }
        {
            void * t208 = gm_l0;
            void * t209 = gm_l0;
            void * t210 = gm_l21;
            uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)t210) + 104));
            uint64_t t212 = gm_f_vc_alloc_slot_5eptdbb(t209, t211);
            uint32_t t213 = gm_f_vc_slot_at_rc6jhbj(t208, t212);
            gm_l24 = t213;
        }
        {
            void * t214 = gm_l0;
            void * t215 = gm_l21;
            uint32_t t216 = gm_l24;
            gm_b0_1 t217 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t214, t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t218 = gm_l0;
            void * t219 = gm_l0;
            void * t220 = gm_l22;
            uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)t220) + 104));
            uint64_t t222 = gm_f_vc_alloc_slot_5eptdbb(t219, t221);
            uint32_t t223 = gm_f_vc_slot_at_rc6jhbj(t218, t222);
            gm_l25 = t223;
        }
        {
            void * t224 = gm_l0;
            void * t225 = gm_l22;
            uint32_t t226 = gm_l25;
            gm_b0_1 t227 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t224, t225, t226);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l26 = ((uint8_t)0ULL);
        }
        {
            void * t229 = gm_l0;
            void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
            uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)t230) + 90));
            if (!t231) goto ie228;
            void * t232 = gm_l1;
            uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)t232) + 104));
            uint8_t t234 = ((t233 == ((uint64_t)16ULL)));
            if (!t234) goto ie228;
            {
                void * t237 = gm_l0;
                void * t238 = GM_LD(void *, (((uint8_t *)t237) + 0));
                void * t239 = gm_l1;
                gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t239) + 96));
                gm_tg5lql6itufk7 t241 = gm_f_int_info_ulcevdv(t238, GM_CONV(gm_csguankko4lmhd, t240));
                gm_tg5lql6itufk7 t242 = t241;
                if (!((((uint8_t *)&t242))[8] != 0)) goto ie236;
                gm_ttovhfee8mf28 t243 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t242));
                gm_l27 = t243;
                uint32_t t244 = GM_LD(uint32_t, (((uint8_t *)&gm_l27) + 4));
                uint8_t t245 = ((t244 == ((uint32_t)128ULL)));
                if (!t245) goto ie236;
                void * t246 = gm_l0;
                void * t247 = GM_LD(void *, (((uint8_t *)t246) + 0));
                void * t248 = gm_l21;
                gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t248) + 96));
                gm_tg5lql6itufk7 t250 = gm_f_int_info_ulcevdv(t247, GM_CONV(gm_csguankko4lmhd, t249));
                gm_ttovhfee8mf28 t251 = gm_l27;
                gm_tg5lql6itufk7 t252;
                GM_ST(uint8_t, (uint8_t *)&t252 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t252 + 10, 0);
                GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t252), t251);
                ((uint8_t *)&t252)[8] = 1;
                gm_tg5lql6itufk7 t253 = t250;
                gm_tg5lql6itufk7 t254 = t252;
                uint8_t t255 = (gm_eq651u8j42jd(((uint8_t *)&t253), ((uint8_t *)&t254)));
                if (!t255) goto ie236;
                void * t256 = gm_l0;
                void * t257 = GM_LD(void *, (((uint8_t *)t256) + 0));
                void * t258 = gm_l22;
                gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t258) + 96));
                gm_tg5lql6itufk7 t260 = gm_f_int_info_ulcevdv(t257, GM_CONV(gm_csguankko4lmhd, t259));
                gm_ttovhfee8mf28 t261 = gm_l27;
                gm_tg5lql6itufk7 t262;
                GM_ST(uint8_t, (uint8_t *)&t262 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t262 + 10, 0);
                GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t262), t261);
                ((uint8_t *)&t262)[8] = 1;
                gm_tg5lql6itufk7 t263 = t260;
                gm_tg5lql6itufk7 t264 = t262;
                uint8_t t265 = (gm_eq651u8j42jd(((uint8_t *)&t263), ((uint8_t *)&t264)));
                if (!t265) goto ie236;
                {
                    uint8_t t267;
                    uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                    if (!t269) goto ie268;
                    uint8_t t271 = ((uint8_t)2ULL);
                    t267 = t271;
                    goto ix268;
ie268:;
                    uint8_t t273 = ((uint8_t)1ULL);
                    t267 = t273;
ix268:;
                    gm_l26 = t267;
                }
                goto ix236;
ie236:;
ix236:;
            }
            goto ix228;
ie228:;
ix228:;
        }
        {
            void * t274 = gm_l0;
            gm_tl948htoiqjcu t275;
            memset((uint8_t *)&t275 + 3, 0, 1);
            gm_ts6oqppjh0ba3 t276 = gm_l20;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t275 + 0, t276);
            uint8_t t277 = gm_l26;
            GM_ST(uint8_t, (uint8_t *)&t275 + 2, t277);
            uint32_t t278 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t275 + 4, t278);
            uint32_t t279 = gm_l24;
            GM_ST(uint32_t, (uint8_t *)&t275 + 8, t279);
            uint32_t t280 = gm_l25;
            GM_ST(uint32_t, (uint8_t *)&t275 + 12, t280);
            void * t281 = gm_l1;
            GM_ST(void *, (uint8_t *)&t275 + 16, t281);
            void * t282 = gm_l21;
            GM_ST(void *, (uint8_t *)&t275 + 24, t282);
            void * t283 = gm_l22;
            GM_ST(void *, (uint8_t *)&t275 + 32, t283);
            gm_tjc2ga47o3k6g t284;
            GM_ST(uint8_t, (uint8_t *)&t284 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t284 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t284 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t284 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t284 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tl948htoiqjcu, ((uint8_t *)&t284 + 8), t275);
            gm_b0_1 t285 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t274, &t284);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t286 = gm_l0;
            uint64_t t287 = gm_l23;
            GM_ST(uint64_t, (((uint8_t *)t286) + 248), t287);
        }
        {
            gm_b0_1 t288;
            uint8_t t289 = 0;
            F.ret = t288;
            goto gm_fret;
        }
        goto me5;
mn200:;
ma18:;
        uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t291 != ((uint8_t)38ULL)) goto mn290;
        gm_b16_8 t292 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l28 = t292;
        gm_b16_8 t293 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l29 = t293;
        {
            gm_l30 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t296 = gm_l28;
                gm_l31 = t296;
            }
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t298 = gm_l32;
                    uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                    uint8_t t300 = (t298 < t299);
                    if (!t300) goto lb297;
                    {
                        uint64_t t303 = gm_l32;
                        uint64_t t304 = (uintptr_t)t303;
                        uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        void * t306 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t304 * 8));
                        void * t307 = t306;
                        void * t310 = GM_LD(void *, ((uint8_t *)&t307));
                        gm_l33 = t310;
                        {
                            uint64_t t312 = gm_l30;
                            void * t313 = gm_l33;
                            uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)t313) + 104));
                            uint64_t r315; bool o316 = __builtin_add_overflow(t312, t314, &r315);
                            if (o316) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1148, 30);
                            uint64_t t317 = r315;
                            gm_l30 = t317;
                        }
                        goto me308;
mn309:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1148, 13);
me308:;
                    }
lc297:;
                    {
                        uint64_t t319 = gm_l32;
                        uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                        uint64_t t322 = r320;
                        gm_l32 = t322;
                    }
                }
lb297:;
            }
        }
        {
            uint64_t t324 = gm_l30;
            void * t325 = gm_l1;
            uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)t325) + 104));
            uint8_t t327 = (t324 < t326);
            if (!t327) goto ie323;
            {
                void * t329 = gm_l0;
                uint32_t t330 = gm_l2;
                void * t331 = gm_l1;
                uint64_t t332 = GM_LD(uint64_t, (((uint8_t *)t331) + 104));
                uint32_t t333 = (uint32_t)t332;
                gm_tcc1rq4n1avb2 t334;
                GM_ST(uint32_t, ((uint8_t *)&t334 + 0), t330);
                GM_ST(uint32_t, ((uint8_t *)&t334 + 4), t333);
                gm_tjc2ga47o3k6g t335;
                memset(&t335, 0, sizeof t335);
                GM_ST(uint8_t, ((uint8_t *)&t335 + 0), ((uint8_t)57ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t335 + 8), t334);
                gm_b0_1 t336 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t329, &t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix323;
ie323:;
ix323:;
        }
        {
            {
                gm_l35 = ((uint64_t)0ULL);
            }
            {
                uint64_t t338 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                gm_l34 = t338;
            }
            {
                for (;;) {
                    uint64_t t340 = gm_l35;
                    uint64_t t341 = gm_l34;
                    uint8_t t342 = (t340 < t341);
                    if (!t342) goto lb339;
                    {
                        uint64_t t345 = gm_l35;
                        uint64_t t346 = t345;
                        uint64_t t349 = GM_LD(uint64_t, ((uint8_t *)&t346));
                        gm_l36 = t349;
                        {
                            void * t351 = gm_l0;
                            uint64_t t352 = gm_l36;
                            uint64_t t353 = (uintptr_t)t352;
                            uint64_t t354 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            if (t353 >= (uintptr_t)(t354)) gm_trap_index(t353, t354, gm_srcutmnmd444bu0, 1150, 50);
                            void * t355 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t353 * 8));
                            uint32_t t356 = gm_l2;
                            uint64_t t357 = gm_l36;
                            uint64_t t358 = (uintptr_t)t357;
                            uint64_t t359 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            if (t358 >= (uintptr_t)(t359)) gm_trap_index(t358, t359, gm_srcutmnmd444bu0, 1150, 65);
                            uint64_t t360 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t358 * 8));
                            uint32_t t361 = (uint32_t)t360;
                            uint32_t r362; bool o363 = __builtin_add_overflow(t356, t361, &r362);
                            if (o363) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1150, 60);
                            uint32_t t364 = r362;
                            gm_b0_1 t365 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t351, t355, t364);
                            if (gm_unw.kind) {
                                goto gm_ll343;
                            }
                        }
                        goto me347;
mn348:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1150, 13);
me347:;
                    }
lc339:;
                    {
                        uint64_t t367 = gm_l35;
                        uint64_t r368; bool o369 = __builtin_add_overflow(t367, ((uint64_t)1ULL), &r368);
                        uint64_t t370 = r368;
                        gm_l35 = t370;
                    }
                }
gm_ll343:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb339; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc339; }
                }
                goto gm_lad;
lb339:;
            }
        }
        {
            gm_b0_1 t371;
            uint8_t t372 = 0;
            F.ret = t371;
            goto gm_fret;
        }
        goto me5;
mn290:;
ma19:;
        uint8_t t374 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t374 != ((uint8_t)33ULL)) goto mn373;
        gm_tmn7n0n9raou5 t375 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l37 = t375;
        void * t376 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l38 = t376;
        uint8_t t378 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l37) + 0) == ((uint8_t)0ULL)));
        if (!t378) goto ie377;
        void * t379 = gm_l38;
        uint8_t t380 = gm_f_is_word_82qsidt(t379);
        if (!t380) goto ie377;
        void * t381 = gm_l1;
        uint64_t t382 = GM_LD(uint64_t, (((uint8_t *)t381) + 104));
        uint8_t t383 = (t382 > ((uint64_t)8ULL));
        if (!t383) goto ie377;
        {
            void * t385 = gm_l0;
            void * t386 = gm_l38;
            uint32_t t387 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t385, t386, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l39 = t387;
        }
        {
            void * t388 = gm_l0;
            gm_t0mrruemedngl t389;
            uint32_t t390 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t389 + 0, t390);
            uint32_t t391 = gm_l39;
            GM_ST(uint32_t, (uint8_t *)&t389 + 4, t391);
            void * t392 = gm_l38;
            GM_ST(void *, (uint8_t *)&t389 + 8, t392);
            void * t393 = gm_l1;
            GM_ST(void *, (uint8_t *)&t389 + 16, t393);
            gm_tjc2ga47o3k6g t394;
            GM_ST(uint8_t, (uint8_t *)&t394 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t394 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t394 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t394 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t394 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t394 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t394 + 0), ((uint8_t)16ULL));
            GM_ST(gm_t0mrruemedngl, ((uint8_t *)&t394 + 8), t389);
            gm_b0_1 t395 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t388, &t394);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t396 = gm_l0;
            uint32_t t397 = gm_l3;
            gm_f_vc_set_top_h0qmvd1(t396, t397);
        }
        {
            gm_b0_1 t398;
            uint8_t t399 = 0;
            F.ret = t398;
            goto gm_fret;
        }
        goto ix377;
ie377:;
        void * t401 = gm_l0;
        gm_tmn7n0n9raou5 t402 = gm_l37;
        void * t403 = gm_l1;
        uint8_t t404 = gm_f_vc_coerce_builds_j86oug9((void *)NULL, cx, NULL, t401, t402, t403);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t404) goto ie400;
        {
            void * t406 = gm_l0;
            void * t407 = GM_LD(void *, (((uint8_t *)t406) + 0));
            gm_l40 = t407;
        }
        {
            uint8_t t413 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
            switch (t413) { case ((uint8_t)0ULL): goto ma411; case ((uint8_t)1ULL): goto ma411; case ((uint8_t)2ULL): goto ma409; case ((uint8_t)3ULL): goto ma410; case ((uint8_t)4ULL): goto ma411; case ((uint8_t)5ULL): goto ma411; case ((uint8_t)6ULL): goto ma411; case ((uint8_t)7ULL): goto ma411; case ((uint8_t)8ULL): goto ma411; case ((uint8_t)9ULL): goto ma411; case ((uint8_t)10ULL): goto ma411; case ((uint8_t)11ULL): goto ma411; case ((uint8_t)12ULL): goto ma411; default: goto ma411; }
ma409:;
            uint8_t t415 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
            if (t415 != ((uint8_t)2ULL)) goto mn414;
            {
                void * t417 = gm_l40;
                void * t418 = gm_l1;
                gm_t88rcucg1b7uk t419 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t418) + 96));
                gm_t88rcucg1b7uk t420 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t417, GM_CONV(gm_csguankko4lmhd, t419)));
                gm_l41 = t420;
            }
            {
                void * t422 = gm_l40;
                void * t423 = GM_LD(void *, (((uint8_t *)t422) + 8));
                gm_t88rcucg1b7uk t424 = gm_l41;
                uint8_t t425 = gm_f_nullable_pointer_7qfmn6o(t423, GM_CONV(gm_csguankko4lmhd, t424));
                if (!t425) goto ie421;
                {
                    void * t427 = gm_l0;
                    void * t428 = gm_l38;
                    uint32_t t429 = gm_l2;
                    gm_b0_1 t430 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t427, t428, t429);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix421;
ie421:;
                {
                    void * t432 = gm_l40;
                    void * t433 = gm_l1;
                    gm_t88rcucg1b7uk t434 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t433) + 96));
                    gm_b16_8 t435 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t432, t434);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l42 = t435;
                }
                {
                    void * t436 = gm_l0;
                    uint32_t t437 = gm_l2;
                    void * t438 = gm_l1;
                    uint64_t t439 = GM_LD(uint64_t, (((uint8_t *)t438) + 104));
                    uint32_t t440 = (uint32_t)t439;
                    gm_tcc1rq4n1avb2 t441;
                    GM_ST(uint32_t, ((uint8_t *)&t441 + 0), t437);
                    GM_ST(uint32_t, ((uint8_t *)&t441 + 4), t440);
                    gm_tjc2ga47o3k6g t442;
                    memset(&t442, 0, sizeof t442);
                    GM_ST(uint8_t, ((uint8_t *)&t442 + 0), ((uint8_t)57ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t442 + 8), t441);
                    gm_b0_1 t443 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t436, &t442);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t444 = gm_l0;
                    void * t445 = gm_l38;
                    uint32_t t446 = gm_l2;
                    uint64_t t447 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t448 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                    if (t447 >= (uintptr_t)(t448)) gm_trap_index(t447, t448, gm_srcutmnmd444bu0, 1168, 46);
                    uint64_t t449 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t447 * 8));
                    uint32_t t450 = (uint32_t)t449;
                    uint32_t r451; bool o452 = __builtin_add_overflow(t446, t450, &r451);
                    if (o452) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1168, 41);
                    uint32_t t453 = r451;
                    gm_b0_1 t454 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t444, t445, t453);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t455 = gm_l0;
                    uint32_t t456 = gm_l2;
                    uint64_t t457 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t458 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                    if (t457 >= (uintptr_t)(t458)) gm_trap_index(t457, t458, gm_srcutmnmd444bu0, 1169, 44);
                    uint64_t t459 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t457 * 8));
                    uint32_t t460 = (uint32_t)t459;
                    uint32_t r461; bool o462 = __builtin_add_overflow(t456, t460, &r461);
                    if (o462) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1169, 39);
                    uint32_t t463 = r461;
                    gm_b0_1 t464 = gm_f_vc_put_byte_ner3v2q((void *)NULL, cx, NULL, t455, t463, ((uint64_t)1ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix421:;
            }
            goto me408;
mn414:;
ma410:;
            uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
            if (t466 != ((uint8_t)3ULL)) goto mn465;
            {
                void * t468 = gm_l0;
                uint32_t t469 = gm_l2;
                void * t470 = gm_l1;
                uint64_t t471 = GM_LD(uint64_t, (((uint8_t *)t470) + 104));
                uint32_t t472 = (uint32_t)t471;
                gm_tcc1rq4n1avb2 t473;
                GM_ST(uint32_t, ((uint8_t *)&t473 + 0), t469);
                GM_ST(uint32_t, ((uint8_t *)&t473 + 4), t472);
                gm_tjc2ga47o3k6g t474;
                memset(&t474, 0, sizeof t474);
                GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)57ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t474 + 8), t473);
                gm_b0_1 t475 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t468, &t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t476 = gm_l0;
                void * t477 = gm_l38;
                uint32_t t478 = gm_l2;
                uint64_t t479 = (uintptr_t)((uint64_t)1ULL);
                void * t480 = gm_l40;
                void * t481 = gm_l1;
                gm_t88rcucg1b7uk t482 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t481) + 96));
                gm_b16_8 t483 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t480, t482);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t484 = t483;
                uint64_t t485 = GM_LD(uintptr_t, ((uint8_t *)&t484) + 8);
                if (t479 >= (uintptr_t)(t485)) gm_trap_index(t479, t485, gm_srcutmnmd444bu0, 1174, 42);
                uint64_t t486 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t484)) + t479 * 8));
                uint32_t t487 = (uint32_t)t486;
                uint32_t r488; bool o489 = __builtin_add_overflow(t478, t487, &r488);
                if (o489) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1174, 37);
                uint32_t t490 = r488;
                gm_b0_1 t491 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t476, t477, t490);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me408;
mn465:;
ma411:;
            void * t493 = gm_l0;
            void * t494 = gm_l38;
            uint32_t t495 = gm_l2;
            gm_b0_1 t496 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t493, t494, t495);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me408;
mn492:;
ma412:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1160, 13);
me408:;
        }
        {
            gm_b0_1 t497;
            uint8_t t498 = 0;
            F.ret = t497;
            goto gm_fret;
        }
        goto ix400;
ie400:;
ix400:;
ix377:;
        goto me5;
mn373:;
ma20:;
        uint8_t t500 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t500 != ((uint8_t)44ULL)) goto mn499;
        void * t501 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l43 = t501;
        void * t503 = gm_l0;
        void * t504 = GM_LD(void *, (((uint8_t *)t503) + 0));
        void * t505 = gm_l1;
        gm_t88rcucg1b7uk t506 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t505) + 96));
        gm_t53at80pfdus3 t507;
        gm_f_kind_htv6093((void *)&t507, t504, GM_CONV(gm_csguankko4lmhd, t506));
        gm_t53at80pfdus3 t508 = t507;
        uint8_t t509;
        uint8_t t512 = GM_LD(uint8_t, (((uint8_t *)&t508) + 0));
        if (t512 != ((uint8_t)14ULL)) goto mn511;
        t509 = ((uint8_t)1);
        goto me510;
mn511:;
        t509 = ((uint8_t)0);
        goto me510;
mn513:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1180, 27);
me510:;
        if (!t509) goto ie502;
        {
            void * t515 = gm_l0;
            void * t516 = GM_LD(void *, (((uint8_t *)t515) + 0));
            void * t517 = gm_l1;
            gm_t88rcucg1b7uk t518 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t517) + 96));
            gm_b16_8 t519 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t516, t518);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t519;
        }
        {
            void * t520 = gm_l0;
            uint32_t t521 = gm_l2;
            void * t522 = gm_l1;
            uint64_t t523 = GM_LD(uint64_t, (((uint8_t *)t522) + 104));
            uint32_t t524 = (uint32_t)t523;
            gm_tcc1rq4n1avb2 t525;
            GM_ST(uint32_t, ((uint8_t *)&t525 + 0), t521);
            GM_ST(uint32_t, ((uint8_t *)&t525 + 4), t524);
            gm_tjc2ga47o3k6g t526;
            memset(&t526, 0, sizeof t526);
            GM_ST(uint8_t, ((uint8_t *)&t526 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t526 + 8), t525);
            gm_b0_1 t527 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t520, &t526);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t528 = gm_l0;
            void * t529 = gm_l43;
            uint32_t t530 = gm_l2;
            uint64_t t531 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t532 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
            if (t531 >= (uintptr_t)(t532)) gm_trap_index(t531, t532, gm_srcutmnmd444bu0, 1183, 34);
            uint64_t t533 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t531 * 8));
            uint32_t t534 = (uint32_t)t533;
            uint32_t r535; bool o536 = __builtin_add_overflow(t530, t534, &r535);
            if (o536) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1183, 29);
            uint32_t t537 = r535;
            gm_b0_1 t538 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t528, t529, t537);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t539 = gm_l0;
            uint32_t t540 = gm_l2;
            uint64_t t541 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t542 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
            if (t541 >= (uintptr_t)(t542)) gm_trap_index(t541, t542, gm_srcutmnmd444bu0, 1184, 32);
            uint64_t t543 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t541 * 8));
            uint32_t t544 = (uint32_t)t543;
            uint32_t r545; bool o546 = __builtin_add_overflow(t540, t544, &r545);
            if (o546) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1184, 27);
            uint32_t t547 = r545;
            gm_b0_1 t548 = gm_f_vc_put_byte_ner3v2q((void *)NULL, cx, NULL, t539, t547, ((uint64_t)1ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t549;
            uint8_t t550 = 0;
            F.ret = t549;
            goto gm_fret;
        }
        goto ix502;
ie502:;
ix502:;
        goto me5;
mn499:;
ma21:;
        uint8_t t552 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t552 != ((uint8_t)43ULL)) goto mn551;
        uint32_t t553 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l45 = t553;
        void * t554 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l46 = t554;
        void * t556 = gm_l0;
        void * t557 = gm_l1;
        gm_t88rcucg1b7uk t558 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t557) + 96));
        uint8_t t559 = gm_f_vc_variant_builds_hjfpsfe((void *)NULL, cx, NULL, t556, t558);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t559) goto ie555;
        {
            void * t561 = gm_l0;
            void * t562 = GM_LD(void *, (((uint8_t *)t561) + 0));
            gm_l47 = t562;
        }
        {
            void * t563 = gm_l47;
            void * t564 = gm_l1;
            gm_t88rcucg1b7uk t565 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t564) + 96));
            gm_t57euh612ligr t566;
            gm_f_union_def_h8mvf4t((void *)&t566, t563, GM_CONV(gm_csguankko4lmhd, t565));
            gm_t57euh612ligr t567 = t566;
            gm_tcmkompbl09il t568;
            if (((((uint8_t *)&t567))[24] != 0)) {
                gm_tcmkompbl09il t569 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t567) + 0));
                t568 = t569;
            } else {
                gm_b0_1 t570;
                uint8_t t571 = 0;
                F.ret = t570;
                goto gm_fret;
            }
            gm_l48 = t568;
        }
        {
            void * t572 = gm_l0;
            uint32_t t573 = gm_l2;
            void * t574 = gm_l1;
            uint64_t t575 = GM_LD(uint64_t, (((uint8_t *)t574) + 104));
            uint32_t t576 = (uint32_t)t575;
            gm_tcc1rq4n1avb2 t577;
            GM_ST(uint32_t, ((uint8_t *)&t577 + 0), t573);
            GM_ST(uint32_t, ((uint8_t *)&t577 + 4), t576);
            gm_tjc2ga47o3k6g t578;
            memset(&t578, 0, sizeof t578);
            GM_ST(uint8_t, ((uint8_t *)&t578 + 0), ((uint8_t)57ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t578 + 8), t577);
            gm_b0_1 t579 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t572, &t578);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t580 = gm_l0;
            uint32_t t581 = gm_f_vc_alloc_3hmfvtt(t580);
            gm_l49 = t581;
        }
        {
            void * t582 = gm_l0;
            uint32_t t583 = gm_l49;
            gm_b16_8 t584 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
            gm_b16_8 t585 = t584;
            uint32_t t586 = gm_l45;
            uint64_t t587 = (uint64_t)t586;
            uint64_t t588 = gm_f_us_cuv01fp(t587);
            uint64_t t589 = (uintptr_t)t588;
            gm_b16_8 t590 = t585;
            uint64_t t591 = GM_LD(uintptr_t, ((uint8_t *)&t590) + 8);
            if (t589 >= (uintptr_t)(t591)) gm_trap_index(t589, t591, gm_srcutmnmd444bu0, 1192, 48);
            gm_tbci5ospk5nnn t592 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t590)) + t589 * 72) + 24));
            unsigned __int128 t593 = gm_f_bits128_jigkui4(&t592);
            uint64_t t594 = (uint64_t)t593;
            gm_tb31k5l794t7p t595;
            GM_ST(uint32_t, (uint8_t *)&t595 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t595 + 0), t583);
            GM_ST(uint64_t, ((uint8_t *)&t595 + 8), t594);
            gm_tjc2ga47o3k6g t596;
            memset(&t596, 0, sizeof t596);
            GM_ST(uint8_t, ((uint8_t *)&t596 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t596 + 8), t595);
            gm_b0_1 t597 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t582, &t596);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t598 = gm_l0;
            uint32_t t599 = gm_l2;
            void * t600 = gm_l47;
            gm_t88rcucg1b7uk t601 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l48) + 0));
            uint64_t t602 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t600, t601);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t603 = (uint32_t)t602;
            uint32_t t604 = gm_l49;
            gm_tl874t2atnlh3 t605;
            GM_ST(uint32_t, ((uint8_t *)&t605 + 0), t599);
            GM_ST(uint32_t, ((uint8_t *)&t605 + 4), t603);
            GM_ST(uint32_t, ((uint8_t *)&t605 + 8), t604);
            gm_tjc2ga47o3k6g t606;
            memset(&t606, 0, sizeof t606);
            GM_ST(uint8_t, ((uint8_t *)&t606 + 0), ((uint8_t)54ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t606 + 8), t605);
            gm_b0_1 t607 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t598, &t606);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t608 = gm_l0;
            uint32_t t609 = gm_l3;
            GM_ST(uint32_t, (((uint8_t *)t608) + 224), t609);
        }
        {
            void * t611 = gm_l46;
            void * t612 = t611;
            if (!(GM_LD(void *, ((uint8_t *)&t612)) != 0)) goto ie610;
            void * t613 = GM_LD(void *, ((uint8_t *)&t612));
            gm_l50 = t613;
            {
                void * t615 = gm_l0;
                void * t616 = gm_l50;
                uint32_t t617 = gm_l2;
                void * t618 = gm_l47;
                void * t619 = gm_l1;
                gm_t88rcucg1b7uk t620 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t619) + 96));
                uint64_t t621 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t618, t620);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t622 = (uint32_t)t621;
                uint32_t r623; bool o624 = __builtin_add_overflow(t617, t622, &r623);
                if (o624) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1195, 47);
                uint32_t t625 = r623;
                gm_b0_1 t626 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t615, t616, t625);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix610;
ie610:;
ix610:;
        }
        {
            gm_b0_1 t627;
            uint8_t t628 = 0;
            F.ret = t627;
            goto gm_fret;
        }
        goto ix555;
ie555:;
ix555:;
        goto me5;
mn551:;
ma22:;
        uint8_t t630 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t630 != ((uint8_t)13ULL)) goto mn629;
        gm_tt49maei14cl6 t631 = GM_LD(gm_tt49maei14cl6, ((((uint8_t *)t4) + 0) + 8));
        F.l51 = t631;
        void * t633 = gm_l0;
        uint8_t t634 = gm_f_vc_slice_builds_2cdjc7j(t633, &(*(gm_tt49maei14cl6 *)((uint8_t *)&F.l51)));
        if (!t634) goto ie632;
        {
            void * t636 = gm_l0;
            void * t637 = gm_l1;
            uint32_t t638 = gm_l2;
            gm_b0_1 t639 = gm_f_vc_slice_qr9et9a((void *)NULL, cx, NULL, t636, &(*(gm_tt49maei14cl6 *)((uint8_t *)&F.l51)), t637, t638);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t640;
            uint8_t t641 = 0;
            F.ret = t640;
            goto gm_fret;
        }
        goto ix632;
ie632:;
ix632:;
        goto me5;
mn629:;
ma23:;
        uint8_t t643 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t643 != ((uint8_t)61ULL)) goto mn642;
        gm_t710lsrq1m8gk t644 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 0) + 8));
        F.l52 = t644;
        void * t646 = GM_LD(void *, (((uint8_t *)&F.l52) + 24));
        void * t647 = t646;
        uint8_t t648;
        if ((GM_LD(void *, ((uint8_t *)&t647)) != 0)) goto mn650;
        t648 = ((uint8_t)1);
        goto me649;
mn650:;
        t648 = ((uint8_t)0);
        goto me649;
mn651:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1202, 24);
me649:;
        if (!t648) goto ie645;
        {
            void * t653 = gm_l0;
            void * t654 = gm_l1;
            uint32_t t655 = gm_l2;
            gm_tmn7n0n9raou5 t656;
            GM_ST(uint8_t, (uint8_t *)&t656 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t656 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t656 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t656 + 4), t655);
            gm_b0_1 t657 = gm_f_vc_with_h9bq2rc((void *)NULL, cx, NULL, t653, t654, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l52)), t656);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t658;
            uint8_t t659 = 0;
            F.ret = t658;
            goto gm_fret;
        }
        goto ix645;
ie645:;
ix645:;
        goto me5;
mn642:;
ma24:;
        uint8_t t661 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t661 != ((uint8_t)47ULL)) goto mn660;
        gm_t083kd7b1kkag t662 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t4) + 0) + 8));
        gm_l53 = t662;
        void * t664 = gm_l1;
        uint64_t t665 = GM_LD(uint64_t, (((uint8_t *)t664) + 104));
        uint8_t t666 = (t665 > ((uint64_t)8ULL));
        if (!t666) goto ie663;
        void * t667 = gm_l0;
        void * t668 = gm_l1;
        void * t669 = GM_LD(void *, (((uint8_t *)&gm_l53) + 0));
        gm_t9805qtonbsag t670 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l53) + 8));
        uint32_t t671 = GM_LD(uint32_t, (((uint8_t *)&gm_l53) + 16));
        uint8_t t672 = gm_f_vc_try_ok_ng7fba1((void *)NULL, cx, NULL, t667, t668, t669, t670, t671);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t672) goto ie663;
        {
            void * t674 = gm_l0;
            void * t675 = gm_l1;
            void * t676 = GM_LD(void *, (((uint8_t *)&gm_l53) + 0));
            gm_t9805qtonbsag t677 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l53) + 8));
            uint32_t t678 = gm_l2;
            gm_t9805qtonbsag t679;
            GM_ST(uint8_t, (uint8_t *)&t679 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t679 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t679), t678);
            ((uint8_t *)&t679)[4] = 1;
            gm_b0_1 t680 = gm_f_vc_try_tbirnd2((void *)NULL, cx, NULL, t674, t675, t676, t677, t679);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t681;
            uint8_t t682 = 0;
            F.ret = t681;
            goto gm_fret;
        }
        goto ix663;
ie663:;
ix663:;
        goto me5;
mn660:;
ma25:;
        uint8_t t684 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t684 != ((uint8_t)45ULL)) goto mn683;
        void * t685 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l54 = t685;
        uint8_t t686 = GM_LD(uint8_t, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l55 = t686;
        void * t688 = gm_l1;
        uint64_t t689 = GM_LD(uint64_t, (((uint8_t *)t688) + 104));
        uint8_t t690 = (t689 > ((uint64_t)8ULL));
        if (!t690) goto ie687;
        void * t691 = gm_l0;
        void * t692 = gm_l1;
        void * t693 = gm_l54;
        uint8_t t694 = gm_f_vc_unwrap_ok_tb3qnv4((void *)NULL, cx, NULL, t691, t692, t693);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t694) goto ie687;
        {
            void * t696 = gm_l0;
            void * t697 = gm_l1;
            void * t698 = gm_l54;
            uint8_t t699 = gm_l55;
            uint32_t t700 = gm_l2;
            gm_t9805qtonbsag t701;
            GM_ST(uint8_t, (uint8_t *)&t701 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t701 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t701), t700);
            ((uint8_t *)&t701)[4] = 1;
            gm_b0_1 t702 = gm_f_vc_unwrap_ffrmkv5((void *)NULL, cx, NULL, t696, t697, t698, t699, t701);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t703;
            uint8_t t704 = 0;
            F.ret = t703;
            goto gm_fret;
        }
        goto ix687;
ie687:;
ix687:;
        goto me5;
mn683:;
ma26:;
        uint8_t t706 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t706 != ((uint8_t)34ULL)) goto mn705;
        gm_tvdg94bk7aa22 t707 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l56 = t707;
        gm_b16_8 t708 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l57 = t708;
        void * t710 = gm_l1;
        uint64_t t711 = GM_LD(uint64_t, (((uint8_t *)t710) + 104));
        uint8_t t712 = (t711 > ((uint64_t)8ULL));
        if (!t712) goto ie709;
        gm_tvdg94bk7aa22 t713 = gm_l56;
        gm_b16_8 t714 = gm_l57;
        uint8_t t715 = gm_f_vc_builtin_ok_kbscem0(t713, t714);
        if (!t715) goto ie709;
        {
            void * t717 = gm_l0;
            gm_b16_8 t718 = gm_l57;
            uint32_t t719 = gm_l2;
            gm_tj2m9dbh1ekv6 t720;
            GM_ST(uint8_t, (uint8_t *)&t720 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t720 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t720 + 0), ((uint8_t)2ULL));
            GM_ST(uint32_t, ((uint8_t *)&t720 + 4), t719);
            void * t721 = gm_l1;
            gm_b0_1 t722 = gm_f_vc_builtin_9d4kon2((void *)NULL, cx, NULL, t717, t718, t720, t721);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t723;
            uint8_t t724 = 0;
            F.ret = t723;
            goto gm_fret;
        }
        goto ix709;
ie709:;
ix709:;
        goto me5;
mn705:;
ma27:;
        goto me5;
mn725:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1087, 5);
me5:;
    }
    {
        void * t728 = gm_l1;
        uint64_t t729 = GM_LD(uint64_t, (((uint8_t *)t728) + 104));
        uint8_t t730 = ((t729 == ((uint64_t)0ULL)));
        if (!t730) goto ie727;
        {
            void * t732 = gm_l0;
            void * t733 = gm_l1;
            gm_tmn7n0n9raou5 t734;
            GM_ST(uint8_t, (uint8_t *)&t734 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t734 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t734 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t734 + 0), ((uint8_t)1ULL));
            gm_b0_1 t735 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t732, t733, t734);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t736;
            uint8_t t737 = 0;
            F.ret = t736;
            goto gm_fret;
        }
        goto ix727;
ie727:;
ix727:;
    }
    {
        void * t739 = gm_l1;
        uint8_t t740 = gm_f_is_word_82qsidt(t739);
        if (!t740) goto ie738;
        {
            void * t742 = gm_l0;
            uint32_t t743 = gm_l2;
            void * t744 = gm_l1;
            uint64_t t745 = GM_LD(uint64_t, (((uint8_t *)t744) + 104));
            uint32_t t746 = (uint32_t)t745;
            void * t747 = gm_l0;
            void * t748 = gm_l1;
            uint32_t t749 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t747, t748, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t750;
            GM_ST(uint32_t, ((uint8_t *)&t750 + 0), t743);
            GM_ST(uint32_t, ((uint8_t *)&t750 + 4), t746);
            GM_ST(uint32_t, ((uint8_t *)&t750 + 8), t749);
            gm_tjc2ga47o3k6g t751;
            memset(&t751, 0, sizeof t751);
            GM_ST(uint8_t, ((uint8_t *)&t751 + 0), ((uint8_t)54ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t751 + 8), t750);
            gm_b0_1 t752 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t742, &t751);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t753 = gm_l0;
            uint32_t t754 = gm_l3;
            gm_f_vc_set_top_h0qmvd1(t753, t754);
        }
        {
            gm_b0_1 t755;
            uint8_t t756 = 0;
            F.ret = t755;
            goto gm_fret;
        }
        goto ix738;
ie738:;
ix738:;
    }
    {
        void * t758 = gm_l0;
        void * t759 = gm_l1;
        uint8_t t760 = gm_f_vc_copies_glmdmll(t758, t759);
        if (!t760) goto ie757;
        {
            void * t762 = gm_l0;
            void * t763 = gm_l1;
            uint32_t t764 = gm_l2;
            gm_b0_1 t765 = gm_f_vc_copy_place_8dhmkji((void *)NULL, cx, NULL, t762, t763, t764);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t766;
            uint8_t t767 = 0;
            F.ret = t766;
            goto gm_fret;
        }
        goto ix757;
ie757:;
ix757:;
    }
    {
        void * t768 = gm_l0;
        uint32_t t769 = gm_l2;
        void * t770 = gm_l1;
        gm_ttggq7ng4kp3p t771;
        GM_ST(uint32_t, (uint8_t *)&t771 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t771 + 0), t769);
        GM_ST(void *, ((uint8_t *)&t771 + 8), t770);
        gm_tjc2ga47o3k6g t772;
        memset(&t772, 0, sizeof t772);
        GM_ST(uint8_t, ((uint8_t *)&t772 + 0), ((uint8_t)62ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t772 + 8), t771);
        gm_b0_1 t773 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t768, &t772);
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
__attribute__((aligned(4))) uint8_t gm_f_vc_try_ok_ng7fba1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4) {
    struct gm_fr_vc_try_ok_ng7fba1 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* inner */
    gm_t9805qtonbsag gm_l3; /* lift */
    uint32_t gm_l4; /* levels */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    void * gm_l6; /* m */
    GM_STACK_CHECK(&F, 526u);
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
        uint32_t t4 = gm_l4;
        uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
        if (!t5) {
            void * t6 = gm_l0;
            void * t7 = gm_l1;
            void * t8 = gm_l2;
            gm_t0hcjal40h40i t9;
            gm_f_vc_else_shape_n9vc3pg((void *)&t9, (void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0hcjal40h40i t10 = t9;
            uint8_t t11;
            if (((((uint8_t *)&t10))[32] != 0)) goto mn13;
            t11 = ((uint8_t)1);
            goto me12;
mn13:;
            t11 = ((uint8_t)0);
            goto me12;
mn14:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 555, 27);
me12:;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
        gm_tkn42u7na3t81 t19 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)t18) + 48));
        gm_tkn42u7na3t81 t20 = t19;
        gm_t88rcucg1b7uk t21;
        if (((((uint8_t *)&t20))[4] != 0)) {
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            uint8_t t23 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l5 = t21;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        gm_l6 = t25;
    }
    {
        void * t27 = gm_l6;
        void * t28 = gm_l2;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 96));
        gm_t53at80pfdus3 t30;
        gm_f_kind_htv6093((void *)&t30, t27, GM_CONV(gm_csguankko4lmhd, t29));
        gm_t53at80pfdus3 t31 = t30;
        uint8_t t32;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t35 != ((uint8_t)13ULL)) goto mn34;
        t32 = ((uint8_t)1);
        goto me33;
mn34:;
        t32 = ((uint8_t)0);
        goto me33;
mn36:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 558, 8);
me33:;
        if (!t32) goto ie26;
        {
            uint8_t t38 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t40 = gm_l6;
        gm_t88rcucg1b7uk t41 = gm_l5;
        gm_t53at80pfdus3 t42;
        gm_f_kind_htv6093((void *)&t42, t40, GM_CONV(gm_csguankko4lmhd, t41));
        gm_t53at80pfdus3 t43 = t42;
        uint8_t t44;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
        if (t47 != ((uint8_t)14ULL)) goto mn46;
        t44 = ((uint8_t)1);
        goto me45;
mn46:;
        t44 = ((uint8_t)0);
        goto me45;
mn48:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 559, 13);
me45:;
        uint8_t t49 = (uint8_t)!t44;
        if (!t49) goto ie39;
        {
            uint8_t t51 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint8_t t53;
        if (((((uint8_t *)&gm_l3))[4] != 0)) goto mn55;
        t53 = ((uint8_t)0);
        goto me54;
mn55:;
        t53 = ((uint8_t)1);
        goto me54;
mn56:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 560, 8);
me54:;
        if (!t53) goto ie52;
        {
            void * t58 = gm_l0;
            void * t59 = gm_l6;
            gm_t88rcucg1b7uk t60 = gm_l5;
            gm_tgglga8ro3isc t61 = gm_f_res_types_pg7rvck(t59, GM_CONV(gm_csguankko4lmhd, t60));
            gm_tgglga8ro3isc t62 = t61;
            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t62) + 0));
            uint8_t t64 = gm_f_vc_variant_builds_hjfpsfe((void *)NULL, cx, NULL, t58, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        uint8_t t66 = 0;
        F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_try_tbirnd2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, gm_t9805qtonbsag a4) {
    struct gm_fr_vc_try_tbirnd2 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* inner */
    gm_t9805qtonbsag gm_l3; /* lift */
    gm_t9805qtonbsag gm_l4; /* at */
    gm_tubi2bvif8j7u gm_l5; /* shape */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    void * gm_l7; /* m */
    uint32_t gm_l8; /* save */
    uint64_t gm_l9; /* slots */
    uint32_t gm_l10; /* la */
    uint32_t gm_l11; /* ok */
    uint64_t gm_l12; /* rsize */
    uint32_t gm_l13; /* ra */
    gm_t88rcucg1b7uk gm_l14; /* err */
    gm_b16_8 gm_l15; /* offs */
    uint64_t gm_l16; /* esz */
    uint32_t gm_l17; /* to */
    uint64_t gm_l18; /* room */
    uint32_t gm_l19; /* v */
    gm_t88rcucg1b7uk gm_l20; /* rerr */
    gm_tcmkompbl09il gm_l21; /* u */
    uint32_t gm_l22; /* r */
    uint64_t gm_l23; /* po */
    uint64_t gm_l24; /* k */
    uint64_t gm_l25; /* k */
    GM_STACK_CHECK(&F, 2152u);
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
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t0hcjal40h40i t5;
        gm_f_vc_else_shape_n9vc3pg((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0hcjal40h40i t6 = t5;
        gm_tubi2bvif8j7u t7;
        if (((((uint8_t *)&t6))[32] != 0)) {
            gm_tubi2bvif8j7u t8 = GM_LD(gm_tubi2bvif8j7u, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 8));
        gm_tkn42u7na3t81 t13 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)t12) + 48));
        gm_tkn42u7na3t81 t14 = t13;
        gm_t88rcucg1b7uk t15;
        if (((((uint8_t *)&t14))[4] != 0)) {
            gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            gm_b0_1 t17;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        gm_l6 = t15;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_l7 = t20;
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t21) + 224));
        gm_l8 = t22;
    }
    {
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 248));
        gm_l9 = t24;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l0;
        void * t27 = gm_l2;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 104));
        uint64_t t29 = gm_f_vc_alloc_slot_5eptdbb(t26, t28);
        uint32_t t30 = gm_f_vc_slot_at_rc6jhbj(t25, t29);
        gm_l10 = t30;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l2;
        uint32_t t33 = gm_l10;
        gm_b0_1 t34 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t35 = gm_l0;
        uint32_t t36 = gm_l10;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint32_t t38 = (uint32_t)t37;
        uint32_t r39; bool o40 = __builtin_add_overflow(t36, t38, &r39);
        if (o40) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 575, 28);
        uint32_t t41 = r39;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint32_t t43 = (uint32_t)t42;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 16));
        uint8_t t45 = (uint8_t)!t44;
        gm_t37c2jujtiadn t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
        GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t41);
        GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t43);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 8), t45);
        GM_ST(uint32_t, ((uint8_t *)&t46 + 12), ((uint32_t)0ULL));
        gm_tjc2ga47o3k6g t47;
        memset(&t47, 0, sizeof t47);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)55ULL));
        GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t47 + 8), t46);
        uint32_t t48 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t35, &t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t48;
    }
    {
        void * t49 = gm_l7;
        gm_t88rcucg1b7uk t50 = gm_l6;
        uint64_t t51 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t51;
    }
    {
        void * t52 = gm_l0;
        void * t53 = gm_l0;
        uint64_t t54 = gm_l12;
        uint64_t t55 = gm_f_vc_alloc_slot_5eptdbb(t53, t54);
        uint32_t t56 = gm_f_vc_slot_at_rc6jhbj(t52, t55);
        gm_l13 = t56;
    }
    {
        void * t57 = gm_l0;
        uint32_t t58 = gm_l13;
        uint64_t t59 = gm_l12;
        uint32_t t60 = (uint32_t)t59;
        gm_tcc1rq4n1avb2 t61;
        GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t58);
        GM_ST(uint32_t, ((uint8_t *)&t61 + 4), t60);
        gm_tjc2ga47o3k6g t62;
        memset(&t62, 0, sizeof t62);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t62 + 8), t61);
        gm_b0_1 t63 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t57, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = gm_l7;
        void * t66 = gm_l2;
        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 96));
        gm_t53at80pfdus3 t68;
        gm_f_kind_htv6093((void *)&t68, t65, GM_CONV(gm_csguankko4lmhd, t67));
        gm_t53at80pfdus3 t69 = t68;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
        if (t70 != ((uint8_t)14ULL)) goto ie64;
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t69) + 8) + 0));
        gm_l14 = t71;
        {
            void * t73 = gm_l7;
            gm_t88rcucg1b7uk t74 = gm_l6;
            gm_b16_8 t75 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t75;
        }
        {
            void * t76 = gm_l7;
            gm_t88rcucg1b7uk t77 = gm_l14;
            uint64_t t78 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t78;
        }
        {
            uint32_t t79 = gm_l13;
            uint64_t t80 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcutmnmd444bu0, 583, 20);
            uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t80 * 8));
            uint32_t t83 = (uint32_t)t82;
            uint32_t r84; bool o85 = __builtin_add_overflow(t79, t83, &r84);
            if (o85) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 583, 15);
            uint32_t t86 = r84;
            gm_l17 = t86;
        }
        {
            uint64_t t87 = gm_l12;
            uint64_t t88 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcutmnmd444bu0, 584, 25);
            uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t88 * 8));
            uint64_t r91; bool o92 = __builtin_sub_overflow(t87, t90, &r91);
            if (o92) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 584, 17);
            uint64_t t93 = r91;
            gm_l18 = t93;
        }
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie94;
            uint32_t t95 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            gm_l19 = t95;
            {
                void * t97 = gm_l7;
                gm_t88rcucg1b7uk t98 = gm_l6;
                gm_tgglga8ro3isc t99 = gm_f_res_types_pg7rvck(t97, GM_CONV(gm_csguankko4lmhd, t98));
                gm_tgglga8ro3isc t100 = t99;
                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t100) + 0));
                gm_l20 = t101;
            }
            {
                void * t103 = gm_l7;
                gm_t88rcucg1b7uk t104 = gm_l20;
                gm_t57euh612ligr t105;
                gm_f_union_def_h8mvf4t((void *)&t105, t103, GM_CONV(gm_csguankko4lmhd, t104));
                gm_t57euh612ligr t106 = t105;
                if (!((((uint8_t *)&t106))[24] != 0)) goto ie102;
                gm_tcmkompbl09il t107 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t106));
                gm_l21 = t107;
                {
                    void * t109 = gm_l0;
                    uint32_t t110 = gm_f_vc_alloc_3hmfvtt(t109);
                    gm_l22 = t110;
                }
                {
                    void * t111 = gm_l0;
                    uint32_t t112 = gm_l22;
                    gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                    gm_b16_8 t114 = t113;
                    uint32_t t115 = gm_l19;
                    uint64_t t116 = (uint64_t)t115;
                    uint64_t t117 = gm_f_us_cuv01fp(t116);
                    uint64_t t118 = (uintptr_t)t117;
                    gm_b16_8 t119 = t114;
                    uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&t119) + 8);
                    if (t118 >= (uintptr_t)(t120)) gm_trap_index(t118, t120, gm_srcutmnmd444bu0, 589, 52);
                    gm_tbci5ospk5nnn t121 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t119)) + t118 * 72) + 24));
                    unsigned __int128 t122 = gm_f_bits128_jigkui4(&t121);
                    uint64_t t123 = (uint64_t)t122;
                    gm_tb31k5l794t7p t124;
                    GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t124 + 0), t112);
                    GM_ST(uint64_t, ((uint8_t *)&t124 + 8), t123);
                    gm_tjc2ga47o3k6g t125;
                    memset(&t125, 0, sizeof t125);
                    GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t125 + 8), t124);
                    gm_b0_1 t126 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t111, &t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t127 = gm_l0;
                    uint32_t t128 = gm_l17;
                    void * t129 = gm_l7;
                    gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 0));
                    uint64_t t131 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t129, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t132 = (uint32_t)t131;
                    uint32_t t133 = gm_l22;
                    gm_tl874t2atnlh3 t134;
                    GM_ST(uint32_t, ((uint8_t *)&t134 + 0), t128);
                    GM_ST(uint32_t, ((uint8_t *)&t134 + 4), t132);
                    GM_ST(uint32_t, ((uint8_t *)&t134 + 8), t133);
                    gm_tjc2ga47o3k6g t135;
                    memset(&t135, 0, sizeof t135);
                    GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)54ULL));
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t135 + 8), t134);
                    gm_b0_1 t136 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t127, &t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t137 = gm_l0;
                    uint32_t t138 = gm_l8;
                    GM_ST(uint32_t, (((uint8_t *)t137) + 224), t138);
                }
                goto ix102;
ie102:;
ix102:;
            }
            {
                void * t139 = gm_l7;
                gm_t88rcucg1b7uk t140 = gm_l20;
                uint64_t t141 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t139, t140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t141;
            }
            {
                uint64_t t142 = gm_l16;
                void * t143 = gm_l7;
                gm_t88rcucg1b7uk t144 = gm_l20;
                uint64_t t145 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t143, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t146 = gm_l23;
                uint64_t r147; bool o148 = __builtin_sub_overflow(t145, t146, &r147);
                if (o148) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 594, 26);
                uint64_t t149 = r147;
                uint64_t t150 = (t142 < t149 ? t142 : t149);
                gm_l24 = t150;
            }
            {
                uint64_t t152 = gm_l24;
                uint8_t t153 = (t152 > ((uint64_t)0ULL));
                if (!t153) goto ie151;
                {
                    void * t155 = gm_l0;
                    uint32_t t156 = gm_l17;
                    uint64_t t157 = gm_l23;
                    uint32_t t158 = (uint32_t)t157;
                    uint32_t r159; bool o160 = __builtin_add_overflow(t156, t158, &r159);
                    if (o160) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 595, 44);
                    uint32_t t161 = r159;
                    uint32_t t162 = gm_l10;
                    uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 24));
                    uint32_t t164 = (uint32_t)t163;
                    uint32_t r165; bool o166 = __builtin_add_overflow(t162, t164, &r165);
                    if (o166) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 595, 64);
                    uint32_t t167 = r165;
                    uint64_t t168 = gm_l24;
                    uint32_t t169 = (uint32_t)t168;
                    gm_tl874t2atnlh3 t170;
                    GM_ST(uint32_t, ((uint8_t *)&t170 + 0), t161);
                    GM_ST(uint32_t, ((uint8_t *)&t170 + 4), t167);
                    GM_ST(uint32_t, ((uint8_t *)&t170 + 8), t169);
                    gm_tjc2ga47o3k6g t171;
                    memset(&t171, 0, sizeof t171);
                    GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)52ULL));
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t171 + 8), t170);
                    gm_b0_1 t172 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t155, &t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix151;
ie151:;
ix151:;
            }
            goto ix94;
ie94:;
            {
                uint64_t t174 = gm_l16;
                uint64_t t175 = gm_l18;
                uint64_t t176 = (t174 < t175 ? t174 : t175);
                gm_l25 = t176;
            }
            {
                uint64_t t178 = gm_l25;
                uint8_t t179 = (t178 > ((uint64_t)0ULL));
                if (!t179) goto ie177;
                {
                    void * t181 = gm_l0;
                    uint32_t t182 = gm_l17;
                    uint32_t t183 = gm_l10;
                    uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 24));
                    uint32_t t185 = (uint32_t)t184;
                    uint32_t r186; bool o187 = __builtin_add_overflow(t183, t185, &r186);
                    if (o187) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 598, 48);
                    uint32_t t188 = r186;
                    uint64_t t189 = gm_l25;
                    uint32_t t190 = (uint32_t)t189;
                    gm_tl874t2atnlh3 t191;
                    GM_ST(uint32_t, ((uint8_t *)&t191 + 0), t182);
                    GM_ST(uint32_t, ((uint8_t *)&t191 + 4), t188);
                    GM_ST(uint32_t, ((uint8_t *)&t191 + 8), t190);
                    gm_tjc2ga47o3k6g t192;
                    memset(&t192, 0, sizeof t192);
                    GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)52ULL));
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t192 + 8), t191);
                    gm_b0_1 t193 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t181, &t192);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix177;
ie177:;
ix177:;
            }
ix94:;
        }
        {
            void * t194 = gm_l0;
            uint32_t t195 = gm_l13;
            uint64_t t196 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_srcutmnmd444bu0, 600, 28);
            uint64_t t198 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t196 * 8));
            uint32_t t199 = (uint32_t)t198;
            uint32_t r200; bool o201 = __builtin_add_overflow(t195, t199, &r200);
            if (o201) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 600, 23);
            uint32_t t202 = r200;
            gm_b0_1 t203 = gm_f_vc_put_byte_ner3v2q((void *)NULL, cx, NULL, t194, t202, ((uint64_t)1ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        void * t204 = gm_l0;
        uint32_t t205 = gm_l13;
        uint64_t t206 = gm_l12;
        uint32_t t207 = (uint32_t)t206;
        gm_tcc1rq4n1avb2 t208;
        GM_ST(uint32_t, ((uint8_t *)&t208 + 0), t205);
        GM_ST(uint32_t, ((uint8_t *)&t208 + 4), t207);
        gm_tjc2ga47o3k6g t209;
        memset(&t209, 0, sizeof t209);
        GM_ST(uint8_t, ((uint8_t *)&t209 + 0), ((uint8_t)74ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t209 + 8), t208);
        gm_b0_1 t210 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t204, &t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t211 = gm_l0;
        uint32_t t212 = gm_l11;
        void * t213 = gm_l0;
        uint32_t t214 = gm_f_vc_here_cc4mbj3(t213);
        gm_f_vc_patch_pbmmb3q(t211, t212, t214);
    }
    {
        void * t215 = gm_l0;
        void * t216 = gm_l1;
        uint32_t t217 = gm_l10;
        uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 24));
        uint32_t t219 = (uint32_t)t218;
        uint32_t r220; bool o221 = __builtin_add_overflow(t217, t219, &r220);
        if (o221) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 604, 21);
        uint32_t t222 = r220;
        gm_t9805qtonbsag t223 = gm_l4;
        gm_b0_1 t224 = gm_f_vc_payload_loquhre((void *)NULL, cx, NULL, t215, t216, t222, t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t225 = gm_l0;
        uint64_t t226 = gm_l9;
        GM_ST(uint64_t, (((uint8_t *)t225) + 248), t226);
    }
    {
        void * t227 = gm_l0;
        uint32_t t228;
        uint8_t t230;
        if (((((uint8_t *)&gm_l4))[4] != 0)) goto mn232;
        t230 = ((uint8_t)1);
        goto me231;
mn232:;
        t230 = ((uint8_t)0);
        goto me231;
mn233:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 606, 21);
me231:;
        if (!t230) goto ie229;
        uint32_t t235 = gm_l8;
        uint32_t r236; bool o237 = __builtin_add_overflow(t235, ((uint32_t)1ULL), &r236);
        if (o237) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 606, 34);
        uint32_t t238 = r236;
        uint32_t t239 = t238;
        t228 = t239;
        goto ix229;
ie229:;
        uint32_t t241 = gm_l8;
        uint32_t t242 = t241;
        t228 = t242;
ix229:;
        gm_f_vc_set_top_h0qmvd1(t227, t228);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_unwrap_ffrmkv5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_t9805qtonbsag a4) {
    struct gm_fr_vc_unwrap_ffrmkv5 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    void * gm_l2; /* x */
    uint8_t gm_l3; /* is_result */
    gm_t9805qtonbsag gm_l4; /* at */
    gm_tubi2bvif8j7u gm_l5; /* shape */
    uint32_t gm_l6; /* save */
    uint64_t gm_l7; /* slots */
    uint32_t gm_l8; /* la */
    uint32_t gm_l9; /* ok */
    GM_STACK_CHECK(&F, 694u);
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
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t0hcjal40h40i t5;
        gm_f_vc_else_shape_n9vc3pg((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0hcjal40h40i t6 = t5;
        gm_tubi2bvif8j7u t7;
        if (((((uint8_t *)&t6))[32] != 0)) {
            gm_tubi2bvif8j7u t8 = GM_LD(gm_tubi2bvif8j7u, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_b0_1 t9;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l5 = t7;
    }
    {
        void * t11 = gm_l0;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t11) + 224));
        gm_l6 = t12;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 248));
        gm_l7 = t14;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        void * t17 = gm_l2;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 104));
        uint64_t t19 = gm_f_vc_alloc_slot_5eptdbb(t16, t18);
        uint32_t t20 = gm_f_vc_slot_at_rc6jhbj(t15, t19);
        gm_l8 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l2;
        uint32_t t23 = gm_l8;
        gm_b0_1 t24 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = gm_l0;
        uint32_t t26 = gm_l8;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint32_t t28 = (uint32_t)t27;
        uint32_t r29; bool o30 = __builtin_add_overflow(t26, t28, &r29);
        if (o30) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 633, 28);
        uint32_t t31 = r29;
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint32_t t33 = (uint32_t)t32;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 16));
        uint8_t t35 = (uint8_t)!t34;
        gm_t37c2jujtiadn t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 10, 0);
        GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t31);
        GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t33);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 8), t35);
        GM_ST(uint32_t, ((uint8_t *)&t36 + 12), ((uint32_t)0ULL));
        gm_tjc2ga47o3k6g t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)55ULL));
        GM_ST(gm_t37c2jujtiadn, ((uint8_t *)&t37 + 8), t36);
        uint32_t t38 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t25, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t38;
    }
    {
        void * t39 = gm_l0;
        gm_tpbd74u9dfumj t40;
        memset((uint8_t *)&t40 + 9, 0, 7);
        uint32_t t41 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t40 + 0, t41);
        void * t42 = gm_l2;
        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 96));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t40 + 4, t43);
        uint8_t t44 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t40 + 8, t44);
        void * t45 = gm_l1;
        GM_ST(void *, (uint8_t *)&t40 + 16, t45);
        gm_tjc2ga47o3k6g t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tpbd74u9dfumj, ((uint8_t *)&t46 + 8), t40);
        gm_b0_1 t47 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t39, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t48 = gm_l0;
        uint32_t t49 = gm_l9;
        void * t50 = gm_l0;
        uint32_t t51 = gm_f_vc_here_cc4mbj3(t50);
        gm_f_vc_patch_pbmmb3q(t48, t49, t51);
    }
    {
        void * t52 = gm_l0;
        void * t53 = gm_l1;
        uint32_t t54 = gm_l8;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 24));
        uint32_t t56 = (uint32_t)t55;
        uint32_t r57; bool o58 = __builtin_add_overflow(t54, t56, &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 636, 21);
        uint32_t t59 = r57;
        gm_t9805qtonbsag t60 = gm_l4;
        gm_b0_1 t61 = gm_f_vc_payload_loquhre((void *)NULL, cx, NULL, t52, t53, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t62 = gm_l0;
        uint64_t t63 = gm_l7;
        GM_ST(uint64_t, (((uint8_t *)t62) + 248), t63);
    }
    {
        void * t64 = gm_l0;
        uint32_t t65;
        uint8_t t67;
        if (((((uint8_t *)&gm_l4))[4] != 0)) goto mn69;
        t67 = ((uint8_t)1);
        goto me68;
mn69:;
        t67 = ((uint8_t)0);
        goto me68;
mn70:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 638, 21);
me68:;
        if (!t67) goto ie66;
        uint32_t t72 = gm_l6;
        uint32_t r73; bool o74 = __builtin_add_overflow(t72, ((uint32_t)1ULL), &r73);
        if (o74) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 638, 34);
        uint32_t t75 = r73;
        uint32_t t76 = t75;
        t65 = t76;
        goto ix66;
ie66:;
        uint32_t t78 = gm_l6;
        uint32_t t79 = t78;
        t65 = t79;
ix66:;
        gm_f_vc_set_top_h0qmvd1(t64, t65);
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
extern __attribute__((aligned(4))) uint8_t gm_f_vc_unwrap_ok_tb3qnv4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_vc_uses_scratch_9rt07d8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_uses_scratch_9rt07d8 F;
    void * gm_l0; /* c */
    void * gm_l2; /* e */
    void * gm_l3; /* value */
    uint64_t gm_l4; /* size */
    void * gm_l5; /* init */
    void * gm_l6; /* x */
    GM_STACK_CHECK(&F, 209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma3; case ((uint8_t)6ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t11 != ((uint8_t)5ULL)) goto mn10;
    void * t12 = GM_LD(void *, (((uint8_t *)a1) + 8));
    gm_l2 = t12;
    uint8_t t13 = 0;
    void * t14 = gm_l2;
    uint8_t t15 = gm_f_is_word_82qsidt(t14);
    if (t15) {
        void * t16 = gm_l0;
        void * t17 = gm_l2;
        uint8_t t18 = gm_f_vc_word_only_rg8dfe5(t16, t17);
        t13 = t18;
    } else t13 = 0;
    uint8_t t19 = (uint8_t)!t13;
    t1 = t19;
    goto me2;
mn10:;
ma4:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t21 != ((uint8_t)2ULL)) goto mn20;
    void * t22 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 8));
    gm_l3 = t22;
    uint8_t t23 = 0;
    void * t24 = gm_l3;
    uint8_t t25 = gm_f_is_word_82qsidt(t24);
    if (t25) {
        void * t26 = gm_l0;
        void * t27 = gm_l3;
        uint8_t t28 = gm_f_vc_word_only_rg8dfe5(t26, t27);
        t23 = t28;
    } else t23 = 0;
    uint8_t t29 = (uint8_t)!t23;
    t1 = t29;
    goto me2;
mn20:;
ma5:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t31 != ((uint8_t)0ULL)) goto mn30;
    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 16));
    gm_l4 = t32;
    void * t33 = GM_LD(void *, ((((uint8_t *)a1) + 8) + 24));
    gm_l5 = t33;
    uint8_t t34;
    void * t36 = gm_l5;
    void * t37 = t36;
    if (!(GM_LD(void *, ((uint8_t *)&t37)) != 0)) goto ie35;
    void * t38 = GM_LD(void *, ((uint8_t *)&t37));
    gm_l6 = t38;
    uint8_t t40 = 0;
    uint8_t t41 = 0;
    uint64_t t42 = gm_l4;
    uint8_t t43 = (t42 <= ((uint64_t)8ULL));
    if (t43) {
        void * t44 = gm_l6;
        uint8_t t45 = gm_f_is_word_82qsidt(t44);
        t41 = t45;
    } else t41 = 0;
    if (t41) {
        void * t46 = gm_l0;
        void * t47 = gm_l6;
        uint8_t t48 = gm_f_vc_word_only_rg8dfe5(t46, t47);
        t40 = t48;
    } else t40 = 0;
    uint8_t t49 = (uint8_t)!t40;
    uint8_t t50 = t49;
    t34 = t50;
    goto ix35;
ie35:;
    uint8_t t52 = ((uint8_t)0);
    t34 = t52;
ix35:;
    t1 = t34;
    goto me2;
mn30:;
ma6:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t54 != ((uint8_t)6ULL)) goto mn53;
    t1 = ((uint8_t)0);
    goto me2;
mn53:;
ma7:;
    t1 = ((uint8_t)1);
    goto me2;
mn55:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1028, 56);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_vc_uses_scratch_9rt07d8_c(void * a0, gm_t2k9dbsv5ff58 a1) { return gm_f_vc_uses_scratch_9rt07d8(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_vc_variant_builds_hjfpsfe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_vc_variant_builds_hjfpsfe F;
    void * gm_l0; /* c */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tcmkompbl09il gm_l2; /* u */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t57euh612ligr t5;
        gm_f_union_def_h8mvf4t((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t57euh612ligr t6 = t5;
        gm_tcmkompbl09il t7;
        if (((((uint8_t *)&t6))[24] != 0)) {
            gm_tcmkompbl09il t8 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            uint8_t t9 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 0));
        uint64_t t13 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = (t13 <= ((uint64_t)8ULL));
        uint8_t t15 = 0;
        F.ret = t14;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_with_h9bq2rc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tmn7n0n9raou5 a3) {
    struct gm_fr_vc_with_h9bq2rc F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tmn7n0n9raou5 gm_l3; /* want */
    uint32_t gm_l4; /* save */
    uint64_t gm_l5; /* slots */
    uint32_t gm_l6; /* cx */
    uint32_t gm_l7; /* h */
    uint32_t gm_l8; /* size */
    uint32_t gm_l9; /* res */
    uint32_t gm_l10; /* at */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t4lnf71mruqpt gm_l13; /* hd */
    void * gm_l14; /* x */
    uint32_t gm_l15; /* i */
    uint32_t gm_l16; /* at */
    uint32_t gm_l17; /* id */
    uint32_t gm_l18; /* i */
    gm_b16_8 gm_l19; /* decls */
    void * gm_l20; /* value */
    uint32_t gm_l21; /* at */
    uint32_t gm_l22; /* li */
    uint32_t gm_l23; /* dh */
    uint32_t gm_l24; /* ndefers */
    uint32_t gm_l25; /* exit */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_tcc1rq4n1avb2 gm_l29; /* x */
    uint32_t gm_l30; /* r */
    GM_STACK_CHECK(&F, 2599u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 248));
        gm_l5 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)t7) + 284));
        uint32_t r9; bool o10 = __builtin_add_overflow(t8, ((uint32_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1812, 5);
        uint32_t t11 = r9;
        GM_ST(uint32_t, (((uint8_t *)t6) + 284), t11);
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_f_vc_alloc_3hmfvtt(t12);
        gm_l6 = t13;
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = gm_f_vc_alloc_3hmfvtt(t14);
        gm_l7 = t15;
    }
    {
        void * t16 = gm_l0;
        uint32_t t17 = gm_l6;
        gm_tjc2ga47o3k6g t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)80ULL));
        GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
        gm_b0_1 t19 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t16, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 104));
        uint32_t t22 = (uint32_t)t21;
        gm_l8 = t22;
    }
    {
        gm_l9 = ((uint32_t)0ULL);
    }
    {
        uint32_t t24 = gm_l8;
        uint8_t t25 = (t24 > ((uint32_t)0ULL));
        if (!t25) goto ie23;
        {
            uint32_t t27;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            if (t29 != ((uint8_t)3ULL)) goto ie28;
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            gm_l10 = t30;
            uint32_t t32 = gm_l10;
            uint32_t t33 = t32;
            t27 = t33;
            goto ix28;
ie28:;
            void * t35 = gm_l0;
            void * t36 = gm_l0;
            void * t37 = gm_l1;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 104));
            uint64_t t39 = gm_f_vc_alloc_slot_5eptdbb(t36, t38);
            uint32_t t40 = gm_f_vc_slot_at_rc6jhbj(t35, t39);
            uint32_t t41 = t40;
            t27 = t41;
ix28:;
            gm_l9 = t27;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        {
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l11 = t43;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t45 = gm_l12;
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t47 = (t45 < t46);
                if (!t47) goto lb44;
                {
                    uint64_t t50 = gm_l12;
                    uint64_t t51 = (uintptr_t)t50;
                    uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t4lnf71mruqpt t55 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t51 * 32));
                    gm_l13 = t55;
                    {
                        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                        if (t59 != ((uint8_t)0ULL)) goto mn58;
                        void * t60 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
                        gm_l14 = t60;
                        {
                            void * t63 = gm_l14;
                            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t63) + 0) + 0));
                            if (t64 != ((uint8_t)20ULL)) goto ie62;
                            uint32_t t65 = GM_LD(uint32_t, ((((uint8_t *)t63) + 0) + 8));
                            gm_l15 = t65;
                            {
                                void * t67 = gm_l0;
                                uint32_t t68 = gm_l15;
                                uint32_t t69 = GM_LD(uint32_t, gm_cl7gtp51edk);
                                uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                                gm_tl874t2atnlh3 t71;
                                GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t68);
                                GM_ST(uint32_t, ((uint8_t *)&t71 + 4), t69);
                                GM_ST(uint32_t, ((uint8_t *)&t71 + 8), t70);
                                gm_tjc2ga47o3k6g t72;
                                memset(&t72, 0, sizeof t72);
                                GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)82ULL));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t72 + 8), t71);
                                gm_b0_1 t73 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t67, &t72);
                                if (gm_unw.kind) {
                                    goto gm_ll48;
                                }
                            }
                            goto ix62;
ie62:;
                            {
                                void * t75 = gm_l0;
                                void * t76 = gm_l0;
                                void * t77 = gm_l14;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 104));
                                uint64_t t79 = gm_f_vc_alloc_slot_5eptdbb(t76, t78);
                                uint32_t t80 = gm_f_vc_slot_at_rc6jhbj(t75, t79);
                                gm_l16 = t80;
                            }
                            {
                                void * t81 = gm_l0;
                                void * t82 = gm_l14;
                                uint32_t t83 = gm_l16;
                                gm_b0_1 t84 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t81, t82, t83);
                                if (gm_unw.kind) {
                                    goto gm_ll48;
                                }
                            }
                            {
                                gm_l17 = ((uint32_t)0ULL);
                            }
                            {
                                void * t86 = gm_l0;
                                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                                void * t88 = gm_l14;
                                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 96));
                                gm_t53at80pfdus3 t90;
                                gm_f_kind_htv6093((void *)&t90, t87, GM_CONV(gm_csguankko4lmhd, t89));
                                gm_t53at80pfdus3 t91 = t90;
                                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                                if (t92 != ((uint8_t)25ULL)) goto ie85;
                                uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&t91) + 8));
                                gm_l18 = t93;
                                {
                                    uint32_t t95 = gm_l18;
                                    gm_l17 = t95;
                                }
                                goto ix85;
ie85:;
ix85:;
                            }
                            {
                                void * t96 = gm_l0;
                                uint32_t t97 = gm_l17;
                                uint32_t t98 = gm_l16;
                                uint32_t t99 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                                gm_tl874t2atnlh3 t100;
                                GM_ST(uint32_t, ((uint8_t *)&t100 + 0), t97);
                                GM_ST(uint32_t, ((uint8_t *)&t100 + 4), t98);
                                GM_ST(uint32_t, ((uint8_t *)&t100 + 8), t99);
                                gm_tjc2ga47o3k6g t101;
                                memset(&t101, 0, sizeof t101);
                                GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)82ULL));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t101 + 8), t100);
                                gm_b0_1 t102 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t96, &t101);
                                if (gm_unw.kind) {
                                    goto gm_ll48;
                                }
                            }
ix62:;
                        }
                        goto me57;
mn58:;
                        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                        if (t104 != ((uint8_t)1ULL)) goto mn103;
                        gm_b16_8 t105 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l13) + 8) + 0));
                        gm_l19 = t105;
                        void * t106 = GM_LD(void *, ((((uint8_t *)&gm_l13) + 8) + 16));
                        gm_l20 = t106;
                        {
                            void * t108 = gm_l0;
                            void * t109 = gm_l0;
                            void * t110 = gm_l20;
                            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)t110) + 104));
                            uint64_t t112 = gm_f_vc_alloc_slot_5eptdbb(t109, t111);
                            uint32_t t113 = gm_f_vc_slot_at_rc6jhbj(t108, t112);
                            gm_l21 = t113;
                        }
                        {
                            void * t114 = gm_l0;
                            void * t115 = gm_l20;
                            uint32_t t116 = gm_l21;
                            gm_b0_1 t117 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t114, t115, t116);
                            if (gm_unw.kind) {
                                goto gm_ll48;
                            }
                        }
                        {
                            void * t118 = gm_l0;
                            gm_taohonfcrgrgb t119;
                            memset((uint8_t *)&t119 + 28, 0, 4);
                            gm_b16_8 t120 = gm_l19;
                            gm_b16_8 t121 = t120;
                            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                            GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t122);
                            uint32_t t123 = gm_l21;
                            GM_ST(uint32_t, (uint8_t *)&t119 + 16, t123);
                            void * t124 = gm_l20;
                            uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 104));
                            uint32_t t126 = (uint32_t)t125;
                            GM_ST(uint32_t, (uint8_t *)&t119 + 20, t126);
                            uint32_t t127 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
                            GM_ST(uint32_t, (uint8_t *)&t119 + 24, t127);
                            gm_tjc2ga47o3k6g t128;
                            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t128 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t128 + 48, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)83ULL));
                            GM_ST(gm_taohonfcrgrgb, ((uint8_t *)&t128 + 8), t119);
                            gm_b0_1 t129 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t118, &t128);
                            if (gm_unw.kind) {
                                goto gm_ll48;
                            }
                        }
                        goto me57;
mn103:;
                        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1820, 9);
me57:;
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1819, 5);
me53:;
                }
lc44:;
                {
                    uint64_t t131 = gm_l12;
                    uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                    uint64_t t134 = r132;
                    gm_l12 = t134;
                }
            }
gm_ll48:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb44; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc44; }
            }
            goto gm_lad;
lb44:;
        }
    }
    {
        void * t135 = gm_l0;
        uint64_t t136 = GM_LD(uintptr_t, ((((uint8_t *)t135) + 56) + 0) + 8);
        uint32_t t137 = (uint32_t)t136;
        gm_l22 = t137;
    }
    {
        void * t138 = gm_l0;
        void * t139 = (void *)(((uint8_t *)t138) + 56);
        gm_t98k64j4g45rk t140;
        memset((uint8_t *)&t140 + 29, 0, 3);
        uint32_t t141 = GM_LD(uint32_t, (((uint8_t *)a2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t140 + 0, t141);
        GM_ST(uint32_t, (uint8_t *)&t140 + 4, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&t140 + 8, ((uint32_t)0ULL));
        uint32_t t142 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t140 + 12, t142);
        void * t143 = gm_l0;
        uint32_t t144 = GM_LD(uint32_t, (((uint8_t *)t143) + 136));
        GM_ST(uint32_t, (uint8_t *)&t140 + 16, t144);
        uint32_t t145 = GM_LD(uint32_t, gm_c7p3n1736t1);
        GM_ST(uint32_t, (uint8_t *)&t140 + 20, t145);
        void * t146 = gm_l0;
        uint32_t t147 = GM_LD(uint32_t, (((uint8_t *)t146) + 280));
        GM_ST(uint32_t, (uint8_t *)&t140 + 24, t147);
        GM_ST(uint8_t, (uint8_t *)&t140 + 28, ((uint8_t)1));
        uint32_t t148 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t140 + 32, t148);
        uint32_t t149 = gm_l8;
        GM_ST(uint32_t, (uint8_t *)&t140 + 36, t149);
        gm_tftb1odc739t5 t150 = gm_f_push_d9f883g(t139, &t140);
        gm_tftb1odc739t5 t151 = t150;
        if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srcutmnmd444bu0, 1843, 5);
        gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
    }
    {
        void * t153 = gm_l0;
        void * t154 = (void *)(((uint8_t *)t153) + 96);
        uint32_t t155 = gm_l22;
        gm_tftb1odc739t5 t156 = gm_f_push_im9d4ro(t154, t155);
        gm_tftb1odc739t5 t157 = t156;
        if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srcutmnmd444bu0, 1844, 5);
        gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
    }
    {
        void * t159 = gm_l0;
        uint32_t t160 = gm_l7;
        gm_tjc2ga47o3k6g t161;
        memset(&t161, 0, sizeof t161);
        GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)66ULL));
        GM_ST(uint32_t, ((uint8_t *)&t161 + 8), t160);
        uint32_t t162 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t159, &t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t162;
    }
    {
        void * t163 = gm_l0;
        uint32_t t164 = GM_LD(uint32_t, (((uint8_t *)t163) + 276));
        gm_l24 = t164;
    }
    {
        void * t165 = gm_l0;
        void * t166 = gm_l0;
        uint32_t t167 = GM_LD(uint32_t, (((uint8_t *)t166) + 280));
        uint32_t r168; bool o169 = __builtin_add_overflow(t167, ((uint32_t)1ULL), &r168);
        if (o169) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1847, 5);
        uint32_t t170 = r168;
        GM_ST(uint32_t, (((uint8_t *)t165) + 280), t170);
    }
    {
        uint32_t t172 = gm_l8;
        uint8_t t173 = (t172 > ((uint32_t)0ULL));
        if (!t173) goto ie171;
        {
            void * t175 = gm_l0;
            void * t176 = GM_LD(void *, (((uint8_t *)a2) + 32));
            uint32_t t177 = gm_l9;
            gm_b0_1 t178 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t175, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix171;
ie171:;
        {
            void * t180 = gm_l0;
            void * t181 = GM_LD(void *, (((uint8_t *)a2) + 32));
            gm_tmn7n0n9raou5 t182;
            GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)1ULL));
            gm_b0_1 t183 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t180, t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix171:;
    }
    {
        void * t184 = gm_l0;
        void * t185 = gm_l0;
        uint32_t t186 = GM_LD(uint32_t, (((uint8_t *)t185) + 280));
        uint32_t r187; bool o188 = __builtin_sub_overflow(t186, ((uint32_t)1ULL), &r187);
        if (o188) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 1849, 5);
        uint32_t t189 = r187;
        GM_ST(uint32_t, (((uint8_t *)t184) + 280), t189);
    }
    {
        void * t190 = gm_l0;
        uint32_t t191 = gm_l7;
        uint32_t r192; bool o193 = __builtin_add_overflow(t191, ((uint32_t)1ULL), &r192);
        if (o193) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1850, 13);
        uint32_t t194 = r192;
        GM_ST(uint32_t, (((uint8_t *)t190) + 224), t194);
    }
    {
        void * t195 = gm_l0;
        uint32_t t196 = gm_l6;
        gm_tjc2ga47o3k6g t197;
        memset(&t197, 0, sizeof t197);
        GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)81ULL));
        GM_ST(uint32_t, ((uint8_t *)&t197 + 8), t196);
        uint32_t t198 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t195, &t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t198;
    }
    {
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            void * t200 = gm_l0;
            uint64_t t201 = GM_LD(uintptr_t, ((((uint8_t *)t200) + 144) + 0) + 8);
            gm_l26 = t201;
        }
        {
            for (;;) {
                uint64_t t203 = gm_l27;
                uint64_t t204 = gm_l26;
                uint8_t t205 = (t203 < t204);
                if (!t205) goto lb202;
                {
                    uint64_t t208 = gm_l27;
                    uint64_t t209 = t208;
                    uint64_t t212 = GM_LD(uint64_t, ((uint8_t *)&t209));
                    gm_l28 = t212;
                    {
                        uint64_t t214 = gm_l28;
                        uint64_t t215 = (uintptr_t)t214;
                        void * t216 = gm_l0;
                        uint64_t t217 = GM_LD(uintptr_t, ((((uint8_t *)t216) + 144) + 0) + 8);
                        if (t215 >= (uintptr_t)(t217)) gm_trap_index(t215, t217, gm_srcutmnmd444bu0, 1853, 14);
                        gm_tcc1rq4n1avb2 t218 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t216) + 144) + 0)) + t215 * 8));
                        gm_l29 = t218;
                    }
                    {
                        uint32_t t220 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 4));
                        uint32_t t221 = gm_l22;
                        uint8_t t222 = ((t220 == t221));
                        if (!t222) goto ie219;
                        {
                            void * t224 = gm_l0;
                            uint32_t t225 = GM_LD(uint32_t, (((uint8_t *)&gm_l29) + 0));
                            uint32_t t226 = gm_l25;
                            gm_f_vc_patch_pbmmb3q(t224, t225, t226);
                        }
                        goto ix219;
ie219:;
ix219:;
                    }
                    goto me210;
mn211:;
                    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1852, 5);
me210:;
                }
lc202:;
                {
                    uint64_t t228 = gm_l27;
                    uint64_t r229; bool o230 = __builtin_add_overflow(t228, ((uint64_t)1ULL), &r229);
                    uint64_t t231 = r229;
                    gm_l27 = t231;
                }
            }
lb202:;
        }
    }
    {
        void * t233 = gm_l0;
        uint32_t t234 = GM_LD(uint32_t, (((uint8_t *)t233) + 276));
        uint32_t t235 = gm_l24;
        uint8_t t236 = ((t234 == t235));
        if (!t236) goto ie232;
        {
            uint32_t t238 = gm_l23;
            uint64_t t239 = (uint64_t)t238;
            uint64_t t240 = (uintptr_t)t239;
            void * t241 = gm_l0;
            uint64_t t242 = GM_LD(uintptr_t, ((((uint8_t *)t241) + 16) + 0) + 8);
            if (t240 >= (uintptr_t)(t242)) gm_trap_index(t240, t242, gm_srcutmnmd444bu0, 1857, 9);
            gm_tjc2ga47o3k6g t243;
            memset(&t243, 0, sizeof t243);
            GM_ST(uint8_t, ((uint8_t *)&t243 + 0), ((uint8_t)79ULL));
            GM_ST(gm_tjc2ga47o3k6g, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t241) + 16) + 0)) + t240 * 56), t243);
        }
        {
            uint32_t t244 = gm_l22;
            uint64_t t245 = (uint64_t)t244;
            uint64_t t246 = (uintptr_t)t245;
            void * t247 = gm_l0;
            uint64_t t248 = GM_LD(uintptr_t, ((((uint8_t *)t247) + 56) + 0) + 8);
            if (t246 >= (uintptr_t)(t248)) gm_trap_index(t246, t248, gm_srcutmnmd444bu0, 1858, 9);
            uint32_t t249 = GM_LD(uint32_t, gm_chkfraubo1r);
            GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t247) + 56) + 0)) + t246 * 40) + 12), t249);
        }
        goto ix232;
ie232:;
ix232:;
    }
    {
        {
            void * t251 = gm_l0;
            void * t252 = (void *)(((uint8_t *)t251) + 96);
            gm_t9805qtonbsag t253 = gm_f_pop_570g79n(t252);
        }
    }
    {
        uint32_t t254 = gm_l22;
        uint64_t t255 = (uint64_t)t254;
        uint64_t t256 = (uintptr_t)t255;
        void * t257 = gm_l0;
        uint64_t t258 = GM_LD(uintptr_t, ((((uint8_t *)t257) + 56) + 0) + 8);
        if (t256 >= (uintptr_t)(t258)) gm_trap_index(t256, t258, gm_srcutmnmd444bu0, 1861, 5);
        uint32_t t259 = gm_l25;
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t257) + 56) + 0)) + t256 * 40) + 4), t259);
    }
    {
        uint32_t t260 = gm_l22;
        uint64_t t261 = (uint64_t)t260;
        uint64_t t262 = (uintptr_t)t261;
        void * t263 = gm_l0;
        uint64_t t264 = GM_LD(uintptr_t, ((((uint8_t *)t263) + 56) + 0) + 8);
        if (t262 >= (uintptr_t)(t264)) gm_trap_index(t262, t264, gm_srcutmnmd444bu0, 1862, 5);
        uint32_t t265 = gm_l25;
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t263) + 56) + 0)) + t262 * 40) + 8), t265);
    }
    {
        void * t266 = gm_l0;
        uint32_t t267 = gm_l4;
        GM_ST(uint32_t, (((uint8_t *)t266) + 224), t267);
    }
    {
        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        switch (t274) { case ((uint8_t)0ULL): goto ma269; case ((uint8_t)1ULL): goto ma270; case ((uint8_t)2ULL): goto ma272; case ((uint8_t)3ULL): goto ma271; default: goto ma273; }
ma269:;
        uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t276 != ((uint8_t)0ULL)) goto mn275;
        {
            void * t278 = gm_l0;
            uint32_t t279 = gm_f_vc_alloc_3hmfvtt(t278);
            gm_l30 = t279;
        }
        {
            uint32_t t281 = gm_l8;
            uint8_t t282 = (t281 > ((uint32_t)0ULL));
            if (!t282) goto ie280;
            {
                void * t284 = gm_l0;
                uint32_t t285 = gm_l30;
                uint32_t t286 = gm_l9;
                uint32_t t287 = gm_l8;
                gm_tl874t2atnlh3 t288;
                GM_ST(uint32_t, ((uint8_t *)&t288 + 0), t285);
                GM_ST(uint32_t, ((uint8_t *)&t288 + 4), t286);
                GM_ST(uint32_t, ((uint8_t *)&t288 + 8), t287);
                gm_tjc2ga47o3k6g t289;
                memset(&t289, 0, sizeof t289);
                GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)53ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t289 + 8), t288);
                gm_b0_1 t290 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t284, &t289);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix280;
ie280:;
            {
                void * t292 = gm_l0;
                uint32_t t293 = gm_l30;
                gm_tb31k5l794t7p t294;
                GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t294 + 0), t293);
                GM_ST(uint64_t, ((uint8_t *)&t294 + 8), ((uint64_t)0ULL));
                gm_tjc2ga47o3k6g t295;
                memset(&t295, 0, sizeof t295);
                GM_ST(uint8_t, ((uint8_t *)&t295 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t295 + 8), t294);
                gm_b0_1 t296 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t292, &t295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix280:;
        }
        goto me268;
mn275:;
ma270:;
        uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t298 != ((uint8_t)1ULL)) goto mn297;
        goto me268;
mn297:;
ma271:;
        uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t301 != ((uint8_t)3ULL)) goto mn300;
        goto me268;
mn300:;
ma272:;
        uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t304 != ((uint8_t)2ULL)) goto mn303;
        void * t305 = gm_l0;
        gm_tjc2ga47o3k6g t306;
        uint32_t t308 = gm_l8;
        uint8_t t309 = (t308 > ((uint32_t)0ULL));
        if (!t309) goto ie307;
        uint32_t t311 = gm_l9;
        uint32_t t312 = gm_l8;
        gm_tcc1rq4n1avb2 t313;
        GM_ST(uint32_t, ((uint8_t *)&t313 + 0), t311);
        GM_ST(uint32_t, ((uint8_t *)&t313 + 4), t312);
        gm_tjc2ga47o3k6g t314;
        memset(&t314, 0, sizeof t314);
        GM_ST(uint8_t, ((uint8_t *)&t314 + 0), ((uint8_t)74ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t314 + 8), t313);
        gm_tjc2ga47o3k6g t315 = t314;
        t306 = t315;
        goto ix307;
ie307:;
        gm_tjc2ga47o3k6g t317;
        memset(&t317, 0, sizeof t317);
        GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)75ULL));
        gm_tjc2ga47o3k6g t318 = t317;
        t306 = t318;
ix307:;
        gm_b0_1 t319 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t305, &t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me268;
mn303:;
ma273:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1864, 5);
me268:;
    }
    {
        void * t320 = gm_l0;
        uint64_t t321 = gm_l5;
        GM_ST(uint64_t, (((uint8_t *)t320) + 248), t321);
    }
    {
        void * t322 = gm_l0;
        uint32_t t323;
        uint8_t t325 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l3) + 0) == ((uint8_t)0ULL)));
        if (!t325) goto ie324;
        uint32_t t327 = gm_l4;
        uint32_t r328; bool o329 = __builtin_add_overflow(t327, ((uint32_t)1ULL), &r328);
        if (o329) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 1874, 36);
        uint32_t t330 = r328;
        uint32_t t331 = t330;
        t323 = t331;
        goto ix324;
ie324:;
        uint32_t t333 = gm_l4;
        uint32_t t334 = t333;
        t323 = t334;
ix324:;
        gm_f_vc_set_top_h0qmvd1(t322, t323);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_11r57ch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_word_11r57ch F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint32_t gm_l2; /* save */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* off */
    uint32_t gm_l6; /* r */
    uint32_t gm_l8; /* a */
    uint32_t gm_l9; /* b */
    uint32_t gm_l10; /* r */
    gm_tqs5p4n2tq19v gm_l11; /* k */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_tkm39klvnn4cr gm_l13; /* x */
    uint32_t gm_l14; /* a */
    uint32_t gm_l15; /* b */
    gm_ta6jn77i9rmqf gm_l16; /* x */
    uint32_t gm_l17; /* a */
    uint32_t gm_l18; /* b */
    void * gm_l19; /* x */
    uint32_t gm_l20; /* a */
    uint8_t gm_l21; /* is_and */
    void * gm_l22; /* l */
    void * gm_l23; /* r */
    uint32_t gm_l24; /* at */
    gm_t9qkh0rsooc8d gm_l25; /* op */
    gm_ttovhfee8mf28 gm_l26; /* t */
    void * gm_l27; /* x */
    uint32_t gm_l28; /* a */
    gm_tvdg94bk7aa22 gm_l29; /* op */
    gm_ttovhfee8mf28 gm_l30; /* from */
    gm_ttovhfee8mf28 gm_l31; /* to */
    void * gm_l32; /* x */
    uint32_t gm_l33; /* a */
    uint32_t gm_l35; /* a */
    uint32_t gm_l36; /* b */
    gm_tvdg94bk7aa22 gm_l37; /* op */
    gm_ttovhfee8mf28 gm_l38; /* t */
    void * gm_l39; /* x */
    uint32_t gm_l40; /* a */
    gm_tuggr5sml9o52 gm_l41; /* x */
    uint32_t gm_l42; /* a */
    uint32_t gm_l43; /* b */
    uint32_t gm_l44; /* f */
    gm_b16_8 gm_l45; /* args */
    void * gm_l46; /* callee */
    gm_b16_8 gm_l47; /* args */
    void * gm_l49; /* lhs */
    gm_t9805qtonbsag gm_l50; /* capture */
    void * gm_l51; /* rhs */
    void * gm_l52; /* b */
    gm_trl2khmuphncd gm_l53; /* x */
    void * gm_l54; /* scrutinee */
    gm_b16_8 gm_l55; /* arms */
    void * gm_l57; /* x */
    uint32_t gm_l58; /* r */
    uint64_t gm_l59; /* off */
    void * gm_l63; /* x */
    void * gm_l64; /* x */
    uint64_t gm_l66; /* pb */
    gm_tmn7n0n9raou5 gm_l67; /* kind */
    void * gm_l68; /* x */
    gm_tvdg94bk7aa22 gm_l69; /* op */
    gm_b16_8 gm_l70; /* args */
    uint64_t gm_l71; /* al */
    uint32_t gm_l72; /* ok */
    uint64_t gm_l73; /* slots */
    uint32_t gm_l74; /* at */
    gm_t083kd7b1kkag gm_l75; /* x */
    void * gm_l76; /* x */
    uint8_t gm_l77; /* is_result */
    GM_STACK_CHECK(&F, 8441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 224));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma29; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma29; case ((uint8_t)10ULL): goto ma29; case ((uint8_t)11ULL): goto ma35; case ((uint8_t)12ULL): goto ma28; case ((uint8_t)13ULL): goto ma35; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma35; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma35; case ((uint8_t)20ULL): goto ma35; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma15; case ((uint8_t)29ULL): goto ma16; case ((uint8_t)30ULL): goto ma35; case ((uint8_t)31ULL): goto ma35; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma31; case ((uint8_t)34ULL): goto ma32; case ((uint8_t)35ULL): goto ma18; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma19; case ((uint8_t)38ULL): goto ma35; case ((uint8_t)39ULL): goto ma35; case ((uint8_t)40ULL): goto ma35; case ((uint8_t)41ULL): goto ma35; case ((uint8_t)42ULL): goto ma35; case ((uint8_t)43ULL): goto ma35; case ((uint8_t)44ULL): goto ma35; case ((uint8_t)45ULL): goto ma34; case ((uint8_t)46ULL): goto ma21; case ((uint8_t)47ULL): goto ma33; case ((uint8_t)48ULL): goto ma35; case ((uint8_t)49ULL): goto ma35; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma22; case ((uint8_t)52ULL): goto ma23; case ((uint8_t)53ULL): goto ma35; case ((uint8_t)54ULL): goto ma25; case ((uint8_t)55ULL): goto ma25; case ((uint8_t)56ULL): goto ma25; case ((uint8_t)57ULL): goto ma35; case ((uint8_t)58ULL): goto ma35; case ((uint8_t)59ULL): goto ma35; case ((uint8_t)60ULL): goto ma35; case ((uint8_t)61ULL): goto ma26; case ((uint8_t)62ULL): goto ma35; default: goto ma35; }
ma6:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t39 != ((uint8_t)0ULL)) goto mn38;
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t4) + 0) + 8));
        gm_l3 = t40;
        void * t41 = gm_l0;
        void * t42 = gm_l0;
        uint32_t t43 = gm_f_vc_alloc_3hmfvtt(t42);
        uint64_t t44 = gm_l3;
        gm_tb31k5l794t7p t45;
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t45 + 0), t43);
        GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t44);
        gm_tjc2ga47o3k6g t46;
        memset(&t46, 0, sizeof t46);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t46 + 8), t45);
        gm_b0_1 t47 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t41, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn38:;
ma7:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t49 != ((uint8_t)6ULL)) goto mn48;
        uint32_t t50 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l4 = t50;
        uint64_t t51 = GM_LD(uint64_t, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l5 = t51;
        {
            void * t54 = gm_l0;
            uint32_t t55 = gm_l4;
            gm_t9805qtonbsag t56 = gm_f_vc_local_reg_bgdr42m(t54, t55);
            gm_t9805qtonbsag t57 = t56;
            if (!((((uint8_t *)&t57))[4] != 0)) goto ie53;
            uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&t57));
            gm_l6 = t58;
            void * t59 = gm_l1;
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 104));
            uint32_t t61 = gm_l4;
            uint64_t t62 = (uint64_t)t61;
            uint64_t t63 = (uintptr_t)t62;
            void * t64 = gm_l0;
            void * t65 = GM_LD(void *, (((uint8_t *)t64) + 8));
            uint64_t t66 = GM_LD(uintptr_t, (((uint8_t *)t65) + 88) + 8);
            if (t63 >= (uintptr_t)(t66)) gm_trap_index(t63, t66, gm_srcutmnmd444bu0, 647, 53);
            uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t65) + 88)) + t63 * 8));
            uint8_t t68 = ((t60 == t67));
            if (!t68) goto ie53;
            {
                void * t70 = gm_l0;
                void * t71 = gm_l0;
                uint32_t t72 = gm_f_vc_alloc_3hmfvtt(t71);
                uint32_t t73 = gm_l6;
                gm_tcc1rq4n1avb2 t74;
                GM_ST(uint32_t, ((uint8_t *)&t74 + 0), t72);
                GM_ST(uint32_t, ((uint8_t *)&t74 + 4), t73);
                gm_tjc2ga47o3k6g t75;
                memset(&t75, 0, sizeof t75);
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)65ULL));
                GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t75 + 8), t74);
                gm_b0_1 t76 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t70, &t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix53;
ie53:;
            uint32_t t78 = gm_l4;
            uint64_t t79 = (uint64_t)t78;
            uint64_t t80 = (uintptr_t)t79;
            void * t81 = gm_l0;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 8));
            uint64_t t83 = GM_LD(uintptr_t, (((uint8_t *)t82) + 120) + 8);
            if (t80 >= (uintptr_t)(t83)) gm_trap_index(t80, t83, gm_srcutmnmd444bu0, 649, 27);
            uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t82) + 120)) + t80 * 1));
            uint8_t t85 = (uint8_t)!t84;
            if (!t85) goto ie77;
            {
                void * t87 = gm_l0;
                void * t88 = gm_l0;
                uint32_t t89 = gm_f_vc_alloc_3hmfvtt(t88);
                uint64_t t90 = gm_l5;
                uint32_t t91 = (uint32_t)t90;
                void * t92 = gm_l1;
                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 104));
                uint32_t t94 = (uint32_t)t93;
                gm_tl874t2atnlh3 t95;
                GM_ST(uint32_t, ((uint8_t *)&t95 + 0), t89);
                GM_ST(uint32_t, ((uint8_t *)&t95 + 4), t91);
                GM_ST(uint32_t, ((uint8_t *)&t95 + 8), t94);
                gm_tjc2ga47o3k6g t96;
                memset(&t96, 0, sizeof t96);
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)3ULL));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t96 + 8), t95);
                gm_b0_1 t97 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t87, &t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix77;
ie77:;
            {
                void * t99 = gm_l0;
                void * t100 = gm_l0;
                uint32_t t101 = gm_f_vc_alloc_3hmfvtt(t100);
                uint32_t t102 = gm_l4;
                uint64_t t103 = gm_l5;
                uint32_t t104 = (uint32_t)t103;
                void * t105 = gm_l1;
                uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)t105) + 104));
                uint32_t t107 = (uint32_t)t106;
                gm_thpdbp3q7rdrc t108;
                GM_ST(uint32_t, ((uint8_t *)&t108 + 0), t101);
                GM_ST(uint32_t, ((uint8_t *)&t108 + 4), t102);
                GM_ST(uint32_t, ((uint8_t *)&t108 + 8), t104);
                GM_ST(uint32_t, ((uint8_t *)&t108 + 12), t107);
                gm_tjc2ga47o3k6g t109;
                memset(&t109, 0, sizeof t109);
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)1ULL));
                GM_ST(gm_thpdbp3q7rdrc, ((uint8_t *)&t109 + 8), t108);
                gm_b0_1 t110 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t99, &t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix77:;
ix53:;
        }
        goto me5;
mn48:;
ma8:;
        uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t112 != ((uint8_t)21ULL)) goto mn111;
        gm_t0pc2c7dafsjt t113 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t4) + 0) + 8));
        F.l7 = t113;
        {
            void * t115 = gm_l0;
            void * t116 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
            void * t117 = gm_l0;
            void * t118 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
            uint8_t t119 = gm_f_vc_reads_only_vncte32(t117, t118);
            uint32_t t120 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t115, t116, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t120;
        }
        {
            void * t121 = gm_l0;
            void * t122 = GM_LD(void *, (((uint8_t *)&F.l7) + 32));
            uint32_t t123 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t121, t122, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t123;
        }
        {
            void * t124 = gm_l0;
            uint32_t t125 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t124) + 224), t125);
        }
        {
            void * t126 = gm_l0;
            uint32_t t127 = gm_f_vc_alloc_3hmfvtt(t126);
            gm_l10 = t127;
        }
        {
            gm_ts6oqppjh0ba3 t129 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l7) + 0));
            gm_t9av822btjq16 t130 = gm_f_arith_kind_muj23pr(t129);
            gm_t9av822btjq16 t131 = t130;
            if (!((((uint8_t *)&t131))[2] != 0)) goto ie128;
            gm_tqs5p4n2tq19v t132 = GM_LD(gm_tqs5p4n2tq19v, ((uint8_t *)&t131));
            gm_l11 = t132;
            {
                uint32_t t135 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 4) + 4));
                uint8_t t136 = ((t135 == ((uint32_t)64ULL)));
                if (!t136) goto ie134;
                {
                    void * t138 = gm_l0;
                    gm_ti4ei62u4i1d9 t139;
                    memset((uint8_t *)&t139 + 3, 0, 1);
                    uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                    GM_ST(uint8_t, (uint8_t *)&t139 + 0, t140);
                    uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 4) + 0));
                    GM_ST(uint8_t, (uint8_t *)&t139 + 1, t141);
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 1));
                    GM_ST(uint8_t, (uint8_t *)&t139 + 2, t142);
                    uint32_t t143 = gm_l10;
                    GM_ST(uint32_t, (uint8_t *)&t139 + 4, t143);
                    uint32_t t144 = gm_l8;
                    GM_ST(uint32_t, (uint8_t *)&t139 + 8, t144);
                    uint32_t t145 = gm_l9;
                    GM_ST(uint32_t, (uint8_t *)&t139 + 12, t145);
                    void * t146 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t139 + 16, t146);
                    gm_tjc2ga47o3k6g t147;
                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t147 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t147 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t147 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)26ULL));
                    GM_ST(gm_ti4ei62u4i1d9, ((uint8_t *)&t147 + 8), t139);
                    gm_b0_1 t148 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t138, &t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix134;
ie134:;
                {
                    void * t150 = gm_l0;
                    gm_te4o1hd8337m3 t151;
                    memset((uint8_t *)&t151 + 3, 0, 1);
                    memset((uint8_t *)&t151 + 20, 0, 4);
                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                    GM_ST(uint8_t, (uint8_t *)&t151 + 0, t152);
                    uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 4) + 0));
                    GM_ST(uint8_t, (uint8_t *)&t151 + 1, t153);
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 1));
                    GM_ST(uint8_t, (uint8_t *)&t151 + 2, t154);
                    uint32_t t155 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 4) + 4));
                    GM_ST(uint32_t, (uint8_t *)&t151 + 4, t155);
                    uint32_t t156 = gm_l10;
                    GM_ST(uint32_t, (uint8_t *)&t151 + 8, t156);
                    uint32_t t157 = gm_l8;
                    GM_ST(uint32_t, (uint8_t *)&t151 + 12, t157);
                    uint32_t t158 = gm_l9;
                    GM_ST(uint32_t, (uint8_t *)&t151 + 16, t158);
                    void * t159 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t151 + 24, t159);
                    gm_tjc2ga47o3k6g t160;
                    GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t160 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t160 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)27ULL));
                    GM_ST(gm_te4o1hd8337m3, ((uint8_t *)&t160 + 8), t151);
                    gm_b0_1 t161 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t150, &t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix134:;
            }
            goto ix128;
ie128:;
            {
                uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                switch (t171) { case ((uint8_t)0ULL): goto ma169; case ((uint8_t)1ULL): goto ma169; case ((uint8_t)2ULL): goto ma169; case ((uint8_t)3ULL): goto ma168; case ((uint8_t)4ULL): goto ma169; case ((uint8_t)5ULL): goto ma169; case ((uint8_t)6ULL): goto ma167; case ((uint8_t)7ULL): goto ma164; case ((uint8_t)8ULL): goto ma165; case ((uint8_t)9ULL): goto ma166; case ((uint8_t)10ULL): goto ma169; case ((uint8_t)11ULL): goto ma169; case ((uint8_t)12ULL): goto ma169; case ((uint8_t)13ULL): goto ma169; default: goto ma169; }
ma164:;
                uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                if (t173 != ((uint8_t)7ULL)) goto mn172;
                void * t174 = gm_l0;
                uint32_t t175 = gm_l10;
                uint32_t t176 = gm_l8;
                uint32_t t177 = gm_l9;
                gm_t51d05oujnurh t178;
                GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t178 + 4), t175);
                GM_ST(uint32_t, ((uint8_t *)&t178 + 8), t176);
                GM_ST(uint32_t, ((uint8_t *)&t178 + 12), t177);
                gm_tjc2ga47o3k6g t179;
                memset(&t179, 0, sizeof t179);
                GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)10ULL));
                GM_ST(gm_t51d05oujnurh, ((uint8_t *)&t179 + 8), t178);
                gm_b0_1 t180 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t174, &t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me163;
mn172:;
ma165:;
                uint8_t t182 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                if (t182 != ((uint8_t)8ULL)) goto mn181;
                void * t183 = gm_l0;
                uint32_t t184 = gm_l10;
                uint32_t t185 = gm_l8;
                uint32_t t186 = gm_l9;
                gm_t51d05oujnurh t187;
                GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t187 + 0), ((uint8_t)1ULL));
                GM_ST(uint32_t, ((uint8_t *)&t187 + 4), t184);
                GM_ST(uint32_t, ((uint8_t *)&t187 + 8), t185);
                GM_ST(uint32_t, ((uint8_t *)&t187 + 12), t186);
                gm_tjc2ga47o3k6g t188;
                memset(&t188, 0, sizeof t188);
                GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)10ULL));
                GM_ST(gm_t51d05oujnurh, ((uint8_t *)&t188 + 8), t187);
                gm_b0_1 t189 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t183, &t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me163;
mn181:;
ma166:;
                uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                if (t191 != ((uint8_t)9ULL)) goto mn190;
                void * t192 = gm_l0;
                uint32_t t193 = gm_l10;
                uint32_t t194 = gm_l8;
                uint32_t t195 = gm_l9;
                gm_t51d05oujnurh t196;
                GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)2ULL));
                GM_ST(uint32_t, ((uint8_t *)&t196 + 4), t193);
                GM_ST(uint32_t, ((uint8_t *)&t196 + 8), t194);
                GM_ST(uint32_t, ((uint8_t *)&t196 + 12), t195);
                gm_tjc2ga47o3k6g t197;
                memset(&t197, 0, sizeof t197);
                GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)10ULL));
                GM_ST(gm_t51d05oujnurh, ((uint8_t *)&t197 + 8), t196);
                gm_b0_1 t198 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t192, &t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me163;
mn190:;
ma167:;
                uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                if (t200 != ((uint8_t)6ULL)) goto mn199;
                void * t201 = gm_l0;
                gm_tiag95i83438g t202;
                memset((uint8_t *)&t202 + 28, 0, 4);
                gm_ttovhfee8mf28 t203 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 4));
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t202 + 0, t203);
                gm_ttovhfee8mf28 t204 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 12));
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t202 + 8, t204);
                uint32_t t205 = gm_l10;
                GM_ST(uint32_t, (uint8_t *)&t202 + 16, t205);
                uint32_t t206 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t202 + 20, t206);
                uint32_t t207 = gm_l9;
                GM_ST(uint32_t, (uint8_t *)&t202 + 24, t207);
                void * t208 = gm_l1;
                GM_ST(void *, (uint8_t *)&t202 + 32, t208);
                gm_tjc2ga47o3k6g t209;
                GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t209 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t209 + 0), ((uint8_t)11ULL));
                GM_ST(gm_tiag95i83438g, ((uint8_t *)&t209 + 8), t202);
                gm_b0_1 t210 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t201, &t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me163;
mn199:;
ma168:;
                uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                if (t212 != ((uint8_t)3ULL)) goto mn211;
                gm_t9qkh0rsooc8d t213 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l7) + 0) + 1));
                gm_l12 = t213;
                gm_t9qkh0rsooc8d t215 = gm_l12;
                gm_t9qkh0rsooc8d t216;
                GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)0ULL));
                gm_t9qkh0rsooc8d t217;
                GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)1ULL));
                gm_t0am46iohlpol t218;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t218 + 0), t216);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t218 + 1), t217);
                gm_t0am46iohlpol t219 = t218;
                uint8_t t220 = 0;
                gm_t9qkh0rsooc8d t221 = t215;
                if (gm_eqehhdtpeti1(((uint8_t *)&t219) + 0, ((uint8_t *)&t221))) t220 = 1;
                if (gm_eqehhdtpeti1(((uint8_t *)&t219) + 1, ((uint8_t *)&t221))) t220 = 1;
                uint8_t t222 = (uint8_t)(t220);
                if (!t222) goto ie214;
                {
                    void * t224 = gm_l0;
                    gm_thji9nptko1bl t225;
                    memset((uint8_t *)&t225 + 17, 0, 3);
                    gm_ttovhfee8mf28 t226 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 4));
                    GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t225 + 0, t226);
                    gm_ttovhfee8mf28 t227 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 12));
                    GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t225 + 8, t227);
                    uint8_t t228 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l12) + 0) == ((uint8_t)1ULL)));
                    GM_ST(uint8_t, (uint8_t *)&t225 + 16, t228);
                    uint32_t t229 = gm_l10;
                    GM_ST(uint32_t, (uint8_t *)&t225 + 20, t229);
                    uint32_t t230 = gm_l8;
                    GM_ST(uint32_t, (uint8_t *)&t225 + 24, t230);
                    uint32_t t231 = gm_l9;
                    GM_ST(uint32_t, (uint8_t *)&t225 + 28, t231);
                    void * t232 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t225 + 32, t232);
                    gm_tjc2ga47o3k6g t233;
                    GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t233 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)12ULL));
                    GM_ST(gm_thji9nptko1bl, ((uint8_t *)&t233 + 8), t225);
                    gm_b0_1 t234 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t224, &t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix214;
ie214:;
                {
                    void * t236 = gm_l0;
                    gm_t06kd2nmcgb3u t237;
                    memset((uint8_t *)&t237 + 2, 0, 2);
                    gm_ts6oqppjh0ba3 t238 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l7) + 0));
                    GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t237 + 0, t238);
                    gm_ttovhfee8mf28 t239 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 4));
                    GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t237 + 4, t239);
                    gm_ttovhfee8mf28 t240 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 12));
                    GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t237 + 12, t240);
                    uint32_t t241 = gm_l10;
                    GM_ST(uint32_t, (uint8_t *)&t237 + 20, t241);
                    uint32_t t242 = gm_l8;
                    GM_ST(uint32_t, (uint8_t *)&t237 + 24, t242);
                    uint32_t t243 = gm_l9;
                    GM_ST(uint32_t, (uint8_t *)&t237 + 28, t243);
                    void * t244 = gm_l1;
                    GM_ST(void *, (uint8_t *)&t237 + 32, t244);
                    gm_tjc2ga47o3k6g t245;
                    GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t245 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)25ULL));
                    GM_ST(gm_t06kd2nmcgb3u, ((uint8_t *)&t245 + 8), t237);
                    gm_b0_1 t246 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t236, &t245);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix214:;
                goto me163;
mn211:;
ma169:;
                void * t248 = gm_l0;
                gm_t06kd2nmcgb3u t249;
                memset((uint8_t *)&t249 + 2, 0, 2);
                gm_ts6oqppjh0ba3 t250 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l7) + 0));
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t249 + 0, t250);
                gm_ttovhfee8mf28 t251 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 4));
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t249 + 4, t251);
                gm_ttovhfee8mf28 t252 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l7) + 12));
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t249 + 12, t252);
                uint32_t t253 = gm_l10;
                GM_ST(uint32_t, (uint8_t *)&t249 + 20, t253);
                uint32_t t254 = gm_l8;
                GM_ST(uint32_t, (uint8_t *)&t249 + 24, t254);
                uint32_t t255 = gm_l9;
                GM_ST(uint32_t, (uint8_t *)&t249 + 28, t255);
                void * t256 = gm_l1;
                GM_ST(void *, (uint8_t *)&t249 + 32, t256);
                gm_tjc2ga47o3k6g t257;
                GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t257 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)25ULL));
                GM_ST(gm_t06kd2nmcgb3u, ((uint8_t *)&t257 + 8), t249);
                gm_b0_1 t258 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t248, &t257);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto me163;
mn247:;
ma170:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 667, 17);
me163:;
            }
ix128:;
        }
        goto me5;
mn111:;
ma9:;
        uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t260 != ((uint8_t)23ULL)) goto mn259;
        gm_tkm39klvnn4cr t261 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t4) + 0) + 8));
        gm_l13 = t261;
        {
            void * t263 = gm_l0;
            void * t264 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
            void * t265 = gm_l0;
            void * t266 = GM_LD(void *, (((uint8_t *)&gm_l13) + 24));
            uint8_t t267 = gm_f_vc_reads_only_vncte32(t265, t266);
            uint32_t t268 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t263, t264, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t268;
        }
        {
            void * t269 = gm_l0;
            void * t270 = GM_LD(void *, (((uint8_t *)&gm_l13) + 24));
            uint32_t t271 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t269, t270, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t271;
        }
        {
            void * t272 = gm_l0;
            uint32_t t273 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t272) + 224), t273);
        }
        {
            void * t274 = gm_l0;
            gm_tnkql1rjn1vbj t275;
            memset((uint8_t *)&t275 + 2, 0, 2);
            gm_ts6oqppjh0ba3 t276 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l13) + 0));
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t275 + 0, t276);
            gm_tmn7n0n9raou5 t277 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l13) + 4));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t275 + 4, t277);
            void * t278 = gm_l0;
            uint32_t t279 = gm_f_vc_alloc_3hmfvtt(t278);
            GM_ST(uint32_t, (uint8_t *)&t275 + 12, t279);
            uint32_t t280 = gm_l14;
            GM_ST(uint32_t, (uint8_t *)&t275 + 16, t280);
            uint32_t t281 = gm_l15;
            GM_ST(uint32_t, (uint8_t *)&t275 + 20, t281);
            gm_tjc2ga47o3k6g t282;
            GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t282 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t282 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t282 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)30ULL));
            GM_ST(gm_tnkql1rjn1vbj, ((uint8_t *)&t282 + 8), t275);
            gm_b0_1 t283 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t274, &t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn259:;
ma10:;
        uint8_t t285 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t285 != ((uint8_t)24ULL)) goto mn284;
        gm_ta6jn77i9rmqf t286 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t4) + 0) + 8));
        gm_l16 = t286;
        {
            void * t288 = gm_l0;
            void * t289 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
            void * t290 = gm_l0;
            void * t291 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
            uint8_t t292 = gm_f_vc_reads_only_vncte32(t290, t291);
            uint32_t t293 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t288, t289, t292);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t293;
        }
        {
            void * t294 = gm_l0;
            void * t295 = GM_LD(void *, (((uint8_t *)&gm_l16) + 16));
            uint32_t t296 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t294, t295, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t296;
        }
        {
            void * t297 = gm_l0;
            uint32_t t298 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t297) + 224), t298);
        }
        {
            void * t299 = gm_l0;
            gm_tufm7evi61pnh t300;
            memset((uint8_t *)&t300 + 9, 0, 3);
            gm_tmn7n0n9raou5 t301 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l16) + 0));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t300 + 0, t301);
            uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 24));
            GM_ST(uint8_t, (uint8_t *)&t300 + 8, t302);
            void * t303 = gm_l0;
            uint32_t t304 = gm_f_vc_alloc_3hmfvtt(t303);
            GM_ST(uint32_t, (uint8_t *)&t300 + 12, t304);
            uint32_t t305 = gm_l17;
            GM_ST(uint32_t, (uint8_t *)&t300 + 16, t305);
            uint32_t t306 = gm_l18;
            GM_ST(uint32_t, (uint8_t *)&t300 + 20, t306);
            gm_tjc2ga47o3k6g t307;
            GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t307 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t307 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t307 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t307 + 0), ((uint8_t)31ULL));
            GM_ST(gm_tufm7evi61pnh, ((uint8_t *)&t307 + 8), t300);
            gm_b0_1 t308 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t299, &t307);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn284:;
ma11:;
        uint8_t t310 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t310 != ((uint8_t)25ULL)) goto mn309;
        void * t311 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l19 = t311;
        {
            void * t313 = gm_l0;
            void * t314 = gm_l19;
            uint32_t t315 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t313, t314, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t315;
        }
        {
            void * t316 = gm_l0;
            uint32_t t317 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t316) + 224), t317);
        }
        {
            void * t318 = gm_l0;
            void * t319 = gm_l0;
            uint32_t t320 = gm_f_vc_alloc_3hmfvtt(t319);
            uint32_t t321 = gm_l20;
            gm_tcc1rq4n1avb2 t322;
            GM_ST(uint32_t, ((uint8_t *)&t322 + 0), t320);
            GM_ST(uint32_t, ((uint8_t *)&t322 + 4), t321);
            gm_tjc2ga47o3k6g t323;
            memset(&t323, 0, sizeof t323);
            GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)32ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t323 + 8), t322);
            gm_b0_1 t324 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t318, &t323);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn309:;
ma12:;
        uint8_t t326 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t326 != ((uint8_t)32ULL)) goto mn325;
        uint8_t t327 = GM_LD(uint8_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l21 = t327;
        void * t328 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l22 = t328;
        void * t329 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l23 = t329;
        {
            void * t331 = gm_l0;
            void * t332 = gm_l22;
            gm_b0_1 t333 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t331, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t334;
            uint8_t t336 = gm_l21;
            if (!t336) goto ie335;
            void * t338 = gm_l0;
            uint32_t t339 = gm_l2;
            gm_tcc1rq4n1avb2 t340;
            GM_ST(uint32_t, ((uint8_t *)&t340 + 0), t339);
            GM_ST(uint32_t, ((uint8_t *)&t340 + 4), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t341;
            memset(&t341, 0, sizeof t341);
            GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)39ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t341 + 8), t340);
            uint32_t t342 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t338, &t341);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t343 = t342;
            t334 = t343;
            goto ix335;
ie335:;
            void * t345 = gm_l0;
            uint32_t t346 = gm_l2;
            gm_tcc1rq4n1avb2 t347;
            GM_ST(uint32_t, ((uint8_t *)&t347 + 0), t346);
            GM_ST(uint32_t, ((uint8_t *)&t347 + 4), ((uint32_t)0ULL));
            gm_tjc2ga47o3k6g t348;
            memset(&t348, 0, sizeof t348);
            GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)40ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t348 + 8), t347);
            uint32_t t349 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t345, &t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t350 = t349;
            t334 = t350;
ix335:;
            gm_l24 = t334;
        }
        {
            void * t351 = gm_l0;
            uint32_t t352 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t351) + 224), t352);
        }
        {
            void * t353 = gm_l0;
            void * t354 = gm_l23;
            gm_b0_1 t355 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t353, t354);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t356 = gm_l0;
            uint32_t t357 = gm_l24;
            void * t358 = gm_l0;
            uint32_t t359 = gm_f_vc_here_cc4mbj3(t358);
            gm_f_vc_patch_pbmmb3q(t356, t357, t359);
        }
        {
            void * t360 = gm_l0;
            uint32_t t361 = gm_l2;
            gm_tjc2ga47o3k6g t362;
            memset(&t362, 0, sizeof t362);
            GM_ST(uint8_t, ((uint8_t *)&t362 + 0), ((uint8_t)33ULL));
            GM_ST(uint32_t, ((uint8_t *)&t362 + 8), t361);
            gm_b0_1 t363 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t360, &t362);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn325:;
ma13:;
        uint8_t t365 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t365 != ((uint8_t)26ULL)) goto mn364;
        gm_t9qkh0rsooc8d t366 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l25 = t366;
        gm_ttovhfee8mf28 t367 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t4) + 0) + 8) + 4));
        gm_l26 = t367;
        void * t368 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l27 = t368;
        {
            void * t370 = gm_l0;
            void * t371 = gm_l27;
            uint32_t t372 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t370, t371, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t372;
        }
        {
            void * t373 = gm_l0;
            uint32_t t374 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t373) + 224), t374);
        }
        {
            void * t375 = gm_l0;
            gm_to7n9gnqtgp47 t376;
            memset((uint8_t *)&t376 + 1, 0, 3);
            memset((uint8_t *)&t376 + 20, 0, 4);
            gm_t9qkh0rsooc8d t377 = gm_l25;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t376 + 0, t377);
            gm_ttovhfee8mf28 t378 = gm_l26;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t376 + 4, t378);
            void * t379 = gm_l0;
            uint32_t t380 = gm_f_vc_alloc_3hmfvtt(t379);
            GM_ST(uint32_t, (uint8_t *)&t376 + 12, t380);
            uint32_t t381 = gm_l28;
            GM_ST(uint32_t, (uint8_t *)&t376 + 16, t381);
            void * t382 = gm_l1;
            GM_ST(void *, (uint8_t *)&t376 + 24, t382);
            gm_tjc2ga47o3k6g t383;
            GM_ST(uint8_t, (uint8_t *)&t383 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t383 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t383 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t383 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t383 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t383 + 0), ((uint8_t)34ULL));
            GM_ST(gm_to7n9gnqtgp47, ((uint8_t *)&t383 + 8), t376);
            gm_b0_1 t384 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t375, &t383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn364:;
ma14:;
        uint8_t t386 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t386 != ((uint8_t)27ULL)) goto mn385;
        gm_tvdg94bk7aa22 t387 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l29 = t387;
        gm_ttovhfee8mf28 t388 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t4) + 0) + 8) + 12));
        gm_l30 = t388;
        gm_ttovhfee8mf28 t389 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t4) + 0) + 8) + 20));
        gm_l31 = t389;
        void * t390 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 32));
        gm_l32 = t390;
        {
            void * t392 = gm_l0;
            void * t393 = gm_l32;
            uint32_t t394 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t392, t393, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t394;
        }
        {
            void * t395 = gm_l0;
            uint32_t t396 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t395) + 224), t396);
        }
        {
            void * t397 = gm_l0;
            gm_t78m5u6j4a3pq t398;
            gm_tvdg94bk7aa22 t399 = gm_l29;
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t398 + 0, t399);
            gm_ttovhfee8mf28 t400 = gm_l30;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t398 + 12, t400);
            gm_ttovhfee8mf28 t401 = gm_l31;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t398 + 20, t401);
            void * t402 = gm_l0;
            uint32_t t403 = gm_f_vc_alloc_3hmfvtt(t402);
            GM_ST(uint32_t, (uint8_t *)&t398 + 28, t403);
            uint32_t t404 = gm_l33;
            GM_ST(uint32_t, (uint8_t *)&t398 + 32, t404);
            gm_tjc2ga47o3k6g t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 44, 0);
            GM_ST(uint64_t, (uint8_t *)&t405 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t405 + 0), ((uint8_t)35ULL));
            GM_ST(gm_t78m5u6j4a3pq, ((uint8_t *)&t405 + 8), t398);
            gm_b0_1 t406 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t397, &t405);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn385:;
ma15:;
        uint8_t t408 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t408 != ((uint8_t)28ULL)) goto mn407;
        gm_tscalq56heofe t409 = GM_LD(gm_tscalq56heofe, ((((uint8_t *)t4) + 0) + 8));
        F.l34 = t409;
        {
            void * t411 = gm_l0;
            void * t412 = GM_LD(void *, (((uint8_t *)&F.l34) + 24));
            void * t413 = gm_l0;
            void * t414 = GM_LD(void *, (((uint8_t *)&F.l34) + 32));
            uint8_t t415 = gm_f_vc_reads_only_vncte32(t413, t414);
            uint32_t t416 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t411, t412, t415);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l35 = t416;
        }
        {
            void * t417 = gm_l0;
            void * t418 = GM_LD(void *, (((uint8_t *)&F.l34) + 32));
            uint32_t t419 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t417, t418, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l36 = t419;
        }
        {
            void * t420 = gm_l0;
            uint32_t t421 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t420) + 224), t421);
        }
        {
            void * t422 = gm_l0;
            gm_tqj4rgf34oslv t423;
            gm_tvdg94bk7aa22 t424 = GM_LD(gm_tvdg94bk7aa22, (((uint8_t *)&F.l34) + 0));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t423 + 0, t424);
            gm_ttovhfee8mf28 t425 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l34) + 12));
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t423 + 12, t425);
            void * t426 = gm_l0;
            uint32_t t427 = gm_f_vc_alloc_3hmfvtt(t426);
            GM_ST(uint32_t, (uint8_t *)&t423 + 20, t427);
            uint32_t t428 = gm_l35;
            GM_ST(uint32_t, (uint8_t *)&t423 + 24, t428);
            uint32_t t429 = gm_l36;
            GM_ST(uint32_t, (uint8_t *)&t423 + 28, t429);
            gm_tjc2ga47o3k6g t430;
            GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t430 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t430 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t430 + 0), ((uint8_t)36ULL));
            GM_ST(gm_tqj4rgf34oslv, ((uint8_t *)&t430 + 8), t423);
            gm_b0_1 t431 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t422, &t430);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn407:;
ma16:;
        uint8_t t433 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t433 != ((uint8_t)29ULL)) goto mn432;
        gm_tvdg94bk7aa22 t434 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l37 = t434;
        gm_ttovhfee8mf28 t435 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t4) + 0) + 8) + 12));
        gm_l38 = t435;
        void * t436 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 24));
        gm_l39 = t436;
        {
            void * t438 = gm_l0;
            void * t439 = gm_l39;
            uint32_t t440 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t438, t439, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l40 = t440;
        }
        {
            void * t441 = gm_l0;
            uint32_t t442 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t441) + 224), t442);
        }
        {
            void * t443 = gm_l0;
            gm_tqj4rgf34oslv t444;
            gm_tvdg94bk7aa22 t445 = gm_l37;
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t444 + 0, t445);
            gm_ttovhfee8mf28 t446 = gm_l38;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t444 + 12, t446);
            void * t447 = gm_l0;
            uint32_t t448 = gm_f_vc_alloc_3hmfvtt(t447);
            GM_ST(uint32_t, (uint8_t *)&t444 + 20, t448);
            uint32_t t449 = gm_l40;
            GM_ST(uint32_t, (uint8_t *)&t444 + 24, t449);
            uint32_t t450 = gm_l40;
            GM_ST(uint32_t, (uint8_t *)&t444 + 28, t450);
            gm_tjc2ga47o3k6g t451;
            GM_ST(uint8_t, (uint8_t *)&t451 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t451 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t451 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t451 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t451 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t451 + 0), ((uint8_t)36ULL));
            GM_ST(gm_tqj4rgf34oslv, ((uint8_t *)&t451 + 8), t444);
            gm_b0_1 t452 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t443, &t451);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn432:;
ma17:;
        uint8_t t454 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t454 != ((uint8_t)22ULL)) goto mn453;
        gm_tuggr5sml9o52 t455 = GM_LD(gm_tuggr5sml9o52, ((((uint8_t *)t4) + 0) + 8));
        gm_l41 = t455;
        {
            void * t457 = gm_l0;
            void * t458 = GM_LD(void *, (((uint8_t *)&gm_l41) + 8));
            void * t459 = gm_l0;
            void * t460 = GM_LD(void *, (((uint8_t *)&gm_l41) + 16));
            uint8_t t461 = gm_f_vc_reads_only_vncte32(t459, t460);
            uint32_t t462 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t457, t458, t461);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t462;
        }
        {
            void * t463 = gm_l0;
            void * t464 = GM_LD(void *, (((uint8_t *)&gm_l41) + 16));
            uint32_t t465 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t463, t464, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t465;
        }
        {
            void * t466 = gm_l0;
            uint32_t t467 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t466) + 224), t467);
        }
        {
            void * t468 = gm_l0;
            gm_ts6oqppjh0ba3 t469 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l41) + 0));
            uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&gm_l41) + 2));
            void * t471 = gm_l0;
            uint32_t t472 = gm_f_vc_alloc_3hmfvtt(t471);
            uint32_t t473 = gm_l42;
            uint32_t t474 = gm_l43;
            gm_tfd1kqjeh4les t475;
            GM_ST(uint8_t, (uint8_t *)&t475 + 3, 0);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t475 + 0), t469);
            GM_ST(uint8_t, ((uint8_t *)&t475 + 2), t470);
            GM_ST(uint32_t, ((uint8_t *)&t475 + 4), t472);
            GM_ST(uint32_t, ((uint8_t *)&t475 + 8), t473);
            GM_ST(uint32_t, ((uint8_t *)&t475 + 12), t474);
            gm_tjc2ga47o3k6g t476;
            memset(&t476, 0, sizeof t476);
            GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)37ULL));
            GM_ST(gm_tfd1kqjeh4les, ((uint8_t *)&t476 + 8), t475);
            gm_b0_1 t477 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t468, &t476);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn453:;
ma18:;
        uint8_t t479 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t479 != ((uint8_t)35ULL)) goto mn478;
        uint32_t t480 = GM_LD(uint32_t, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l44 = t480;
        gm_b16_8 t481 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l45 = t481;
        void * t482 = gm_l0;
        uint32_t t483 = gm_l44;
        gm_b16_8 t484 = gm_l45;
        uint32_t t485 = gm_l2;
        gm_tj2m9dbh1ekv6 t486;
        GM_ST(uint8_t, (uint8_t *)&t486 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t486 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t486 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t486 + 4), t485);
        void * t487 = gm_l1;
        gm_b0_1 t488 = gm_f_vc_call_27qg9ft((void *)NULL, cx, NULL, t482, t483, t484, t486, t487);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn478:;
ma19:;
        uint8_t t490 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t490 != ((uint8_t)37ULL)) goto mn489;
        void * t491 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l46 = t491;
        gm_b16_8 t492 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l47 = t492;
        void * t493 = gm_l0;
        void * t494 = gm_l46;
        gm_b16_8 t495 = gm_l47;
        uint32_t t496 = gm_l2;
        gm_tj2m9dbh1ekv6 t497;
        GM_ST(uint8_t, (uint8_t *)&t497 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t497 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t497 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t497 + 4), t496);
        void * t498 = gm_l1;
        gm_b0_1 t499 = gm_f_vc_call_value_5j9edsq((void *)NULL, cx, NULL, t493, t494, t495, t497, t498);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn489:;
ma20:;
        uint8_t t501 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t501 != ((uint8_t)36ULL)) goto mn500;
        gm_tf4r0415i49d1 t502 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t4) + 0) + 8));
        F.l48 = t502;
        void * t503 = gm_l0;
        gm_b16_8 t504 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 32));
        uint32_t t505 = gm_l2;
        gm_tj2m9dbh1ekv6 t506;
        GM_ST(uint8_t, (uint8_t *)&t506 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t506 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t506 + 4), t505);
        void * t507 = gm_l1;
        gm_b0_1 t508 = gm_f_vc_perform_qci0ith((void *)NULL, cx, NULL, t503, t504, t506, t507);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn500:;
ma21:;
        uint8_t t510 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t510 != ((uint8_t)46ULL)) goto mn509;
        void * t511 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l49 = t511;
        gm_t9805qtonbsag t512 = GM_LD(gm_t9805qtonbsag, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l50 = t512;
        void * t513 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l51 = t513;
        {
            void * t516 = gm_l0;
            void * t517 = gm_l1;
            void * t518 = gm_l49;
            gm_t0hcjal40h40i t519;
            gm_f_vc_else_shape_n9vc3pg((void *)&t519, (void *)NULL, cx, NULL, t516, t517, t518);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0hcjal40h40i t520 = t519;
            uint8_t t521;
            if (((((uint8_t *)&t520))[32] != 0)) goto mn523;
            t521 = ((uint8_t)1);
            goto me522;
mn523:;
            t521 = ((uint8_t)0);
            goto me522;
mn524:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 737, 16);
me522:;
            if (!t521) goto ie515;
            {
                void * t526 = gm_l0;
                void * t527 = gm_l0;
                uint32_t t528 = gm_f_vc_alloc_3hmfvtt(t527);
                void * t529 = gm_l1;
                gm_ttggq7ng4kp3p t530;
                GM_ST(uint32_t, (uint8_t *)&t530 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t530 + 0), t528);
                GM_ST(void *, ((uint8_t *)&t530 + 8), t529);
                gm_tjc2ga47o3k6g t531;
                memset(&t531, 0, sizeof t531);
                GM_ST(uint8_t, ((uint8_t *)&t531 + 0), ((uint8_t)61ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t531 + 8), t530);
                gm_b0_1 t532 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t526, &t531);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix515;
ie515:;
            {
                void * t534 = gm_l0;
                void * t535 = gm_l1;
                void * t536 = gm_l49;
                gm_t9805qtonbsag t537 = gm_l50;
                void * t538 = gm_l51;
                gm_t9805qtonbsag t539;
                memset(&t539, 0, sizeof t539);
                gm_b0_1 t540 = gm_f_vc_else_9ml9jkf((void *)NULL, cx, NULL, t534, t535, t536, t537, t538, t539);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix515:;
        }
        goto me5;
mn509:;
ma22:;
        uint8_t t542 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t542 != ((uint8_t)51ULL)) goto mn541;
        void * t543 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l52 = t543;
        void * t544 = gm_l0;
        void * t545 = gm_l52;
        gm_tmn7n0n9raou5 t546;
        GM_ST(uint8_t, (uint8_t *)&t546 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t546 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t546 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t546 + 0), ((uint8_t)0ULL));
        gm_b0_1 t547 = gm_f_vc_block_ql7sru3((void *)NULL, cx, NULL, t544, t545, t546);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn541:;
ma23:;
        uint8_t t549 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t549 != ((uint8_t)52ULL)) goto mn548;
        gm_trl2khmuphncd t550 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t4) + 0) + 8));
        gm_l53 = t550;
        void * t551 = gm_l0;
        gm_b16_8 t552 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 0));
        void * t553 = GM_LD(void *, (((uint8_t *)&gm_l53) + 16));
        void * t554 = GM_LD(void *, (((uint8_t *)&gm_l53) + 24));
        gm_tmn7n0n9raou5 t555;
        GM_ST(uint8_t, (uint8_t *)&t555 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t555 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t555 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t555 + 0), ((uint8_t)0ULL));
        gm_b0_1 t556 = gm_f_vc_if_1vdf5lq((void *)NULL, cx, NULL, t551, t552, t553, t554, t555);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn548:;
ma24:;
        uint8_t t558 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t558 != ((uint8_t)50ULL)) goto mn557;
        void * t559 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l54 = t559;
        gm_b16_8 t560 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l55 = t560;
        void * t561 = gm_l0;
        void * t562 = gm_l1;
        void * t563 = gm_l54;
        gm_b16_8 t564 = gm_l55;
        gm_tmn7n0n9raou5 t565;
        GM_ST(uint8_t, (uint8_t *)&t565 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t565 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t565 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t565 + 0), ((uint8_t)0ULL));
        gm_b0_1 t566 = gm_f_vc_match_ikc07jv((void *)NULL, cx, NULL, t561, t562, t563, t564, t565);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn557:;
ma25:;
        uint8_t t570 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t570 != ((uint8_t)56ULL)) goto pa569;
        goto po568;
pa569:;
        uint8_t t572 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t572 != ((uint8_t)54ULL)) goto pa571;
        goto po568;
pa571:;
        uint8_t t574 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t574 != ((uint8_t)55ULL)) goto pa573;
        goto po568;
pa573:;
        goto mn567;
po568:;
        void * t575 = gm_l0;
        void * t576 = gm_l1;
        gm_tmn7n0n9raou5 t577;
        GM_ST(uint8_t, (uint8_t *)&t577 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t577 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t577 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t577 + 0), ((uint8_t)0ULL));
        gm_b0_1 t578 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t575, t576, t577);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn567:;
ma26:;
        uint8_t t580 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t580 != ((uint8_t)61ULL)) goto mn579;
        gm_t710lsrq1m8gk t581 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 0) + 8));
        F.l56 = t581;
        void * t583 = GM_LD(void *, (((uint8_t *)&F.l56) + 24));
        void * t584 = t583;
        uint8_t t585;
        if ((GM_LD(void *, ((uint8_t *)&t584)) != 0)) goto mn587;
        t585 = ((uint8_t)1);
        goto me586;
mn587:;
        t585 = ((uint8_t)0);
        goto me586;
mn588:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 747, 24);
me586:;
        if (!t585) goto ie582;
        {
            void * t590 = gm_l0;
            void * t591 = gm_l1;
            gm_tmn7n0n9raou5 t592;
            GM_ST(uint8_t, (uint8_t *)&t592 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t592 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t592 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t592 + 0), ((uint8_t)0ULL));
            gm_b0_1 t593 = gm_f_vc_with_h9bq2rc((void *)NULL, cx, NULL, t590, t591, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l56)), t592);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix582;
ie582:;
        {
            void * t595 = gm_l0;
            void * t596 = gm_l0;
            uint32_t t597 = gm_f_vc_alloc_3hmfvtt(t596);
            void * t598 = gm_l1;
            gm_ttggq7ng4kp3p t599;
            GM_ST(uint32_t, (uint8_t *)&t599 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t599 + 0), t597);
            GM_ST(void *, ((uint8_t *)&t599 + 8), t598);
            gm_tjc2ga47o3k6g t600;
            memset(&t600, 0, sizeof t600);
            GM_ST(uint8_t, ((uint8_t *)&t600 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t600 + 8), t599);
            gm_b0_1 t601 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t595, &t600);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix582:;
        goto me5;
mn579:;
ma27:;
        uint8_t t603 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t603 != ((uint8_t)16ULL)) goto mn602;
        void * t604 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l57 = t604;
        void * t606 = gm_l0;
        void * t607 = gm_l57;
        uint8_t t608 = gm_f_vc_addressable_ltobf02(t606, t607);
        if (!t608) goto ie605;
        {
            void * t610 = gm_l0;
            void * t611 = gm_l57;
            gm_tiutk3b96cnlp t612;
            gm_f_vc_place_i1sksai((void *)&t612, (void *)NULL, cx, NULL, t610, t611);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tiutk3b96cnlp t613 = t612;
            uint8_t t616 = GM_LD(uint8_t, (((uint8_t *)&t613) + 0));
            if (t616 != ((uint8_t)1ULL)) goto mn615;
            uint32_t t617 = GM_LD(uint32_t, ((((uint8_t *)&t613) + 8) + 0));
            gm_l58 = t617;
            uint64_t t618 = GM_LD(uint64_t, ((((uint8_t *)&t613) + 8) + 8));
            gm_l59 = t618;
            {
                uint32_t t621 = gm_l58;
                uint32_t t622 = gm_l2;
                uint8_t t623 = (!(t621 == t622));
                if (!t623) goto ie620;
                {
                    void * t625 = gm_l0;
                    uint32_t t626 = gm_l2;
                    uint32_t t627 = gm_l58;
                    gm_tcc1rq4n1avb2 t628;
                    GM_ST(uint32_t, ((uint8_t *)&t628 + 0), t626);
                    GM_ST(uint32_t, ((uint8_t *)&t628 + 4), t627);
                    gm_tjc2ga47o3k6g t629;
                    memset(&t629, 0, sizeof t629);
                    GM_ST(uint8_t, ((uint8_t *)&t629 + 0), ((uint8_t)65ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t629 + 8), t628);
                    gm_b0_1 t630 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t625, &t629);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix620;
ie620:;
ix620:;
            }
            {
                uint64_t t632 = gm_l59;
                uint8_t t633 = (!(t632 == ((uint64_t)0ULL)));
                if (!t633) goto ie631;
                {
                    void * t635 = gm_l0;
                    uint32_t t636 = gm_l2;
                    uint64_t t637 = gm_l59;
                    gm_tb31k5l794t7p t638;
                    GM_ST(uint32_t, (uint8_t *)&t638 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t638 + 0), t636);
                    GM_ST(uint64_t, ((uint8_t *)&t638 + 8), t637);
                    gm_tjc2ga47o3k6g t639;
                    memset(&t639, 0, sizeof t639);
                    GM_ST(uint8_t, ((uint8_t *)&t639 + 0), ((uint8_t)24ULL));
                    GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t639 + 8), t638);
                    gm_b0_1 t640 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t635, &t639);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix631;
ie631:;
ix631:;
            }
            goto me614;
mn615:;
            uint8_t t642 = GM_LD(uint8_t, (((uint8_t *)&t613) + 0));
            if (t642 != ((uint8_t)0ULL)) goto mn641;
            gm_trap("reached `@unreachable()`", gm_srcutmnmd444bu0, 754, 30);
            goto me614;
mn641:;
            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 749, 13);
me614:;
        }
        goto ix605;
ie605:;
        {
            void * t644 = gm_l0;
            void * t645 = gm_l0;
            uint32_t t646 = gm_f_vc_alloc_3hmfvtt(t645);
            void * t647 = gm_l1;
            gm_ttggq7ng4kp3p t648;
            GM_ST(uint32_t, (uint8_t *)&t648 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t648 + 0), t646);
            GM_ST(void *, ((uint8_t *)&t648 + 8), t647);
            gm_tjc2ga47o3k6g t649;
            memset(&t649, 0, sizeof t649);
            GM_ST(uint8_t, ((uint8_t *)&t649 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t649 + 8), t648);
            gm_b0_1 t650 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t644, &t649);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix605:;
        goto me5;
mn602:;
ma28:;
        uint8_t t652 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t652 != ((uint8_t)12ULL)) goto mn651;
        gm_tp0fa5qv7rqnn t653 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t4) + 0) + 8));
        F.l60 = t653;
        uint8_t t655 = ((GM_LD(uint8_t, (((uint8_t *)&F.l60) + 16) + 0) == ((uint8_t)1ULL)));
        if (!t655) goto ie654;
        void * t656 = gm_l0;
        void * t657 = gm_l1;
        gm_t9qkh0rsooc8d t658 = gm_f_vc_placeable_ip26ve2(t656, t657);
        gm_t9qkh0rsooc8d t659 = t658;
        uint8_t t660 = (!(GM_LD(uint8_t, ((uint8_t *)&t659) + 0) == ((uint8_t)0ULL)));
        if (!t660) goto ie654;
        {
            void * t662 = gm_l0;
            void * t663 = gm_l1;
            void * t664 = GM_LD(void *, (((uint8_t *)&F.l60) + 0));
            void * t665 = GM_LD(void *, (((uint8_t *)&F.l60) + 8));
            uint64_t t666 = GM_LD(uint64_t, (((uint8_t *)&F.l60) + 24));
            gm_b0_1 t667 = gm_f_vc_slice_get_klv78kv((void *)NULL, cx, NULL, t662, t663, t664, t665, t666);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix654;
ie654:;
        void * t669 = gm_l0;
        void * t670 = gm_l1;
        gm_t9qkh0rsooc8d t671 = gm_f_vc_placeable_ip26ve2(t669, t670);
        gm_t9qkh0rsooc8d t672 = t671;
        uint8_t t673 = ((GM_LD(uint8_t, ((uint8_t *)&t672) + 0) == ((uint8_t)0ULL)));
        if (!t673) goto ie668;
        {
            void * t675 = gm_l0;
            void * t676 = gm_l0;
            uint32_t t677 = gm_f_vc_alloc_3hmfvtt(t676);
            void * t678 = gm_l1;
            gm_ttggq7ng4kp3p t679;
            GM_ST(uint32_t, (uint8_t *)&t679 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t679 + 0), t677);
            GM_ST(void *, ((uint8_t *)&t679 + 8), t678);
            gm_tjc2ga47o3k6g t680;
            memset(&t680, 0, sizeof t680);
            GM_ST(uint8_t, ((uint8_t *)&t680 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t680 + 8), t679);
            gm_b0_1 t681 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t675, &t680);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix668;
ie668:;
        {
            void * t683 = gm_l0;
            void * t684 = gm_l1;
            gm_f_vc_place_i1sksai((void *)&F.l61, (void *)NULL, cx, NULL, t683, t684);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t685 = gm_l0;
            uint32_t t686 = gm_l2;
            gm_tiutk3b96cnlp t687 = F.l61;
            void * t688 = gm_l1;
            uint64_t t689 = GM_LD(uint64_t, (((uint8_t *)t688) + 104));
            void * t690 = gm_l0;
            void * t691 = gm_l1;
            uint32_t t692 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t690, t691);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t693 = gm_l1;
            gm_b0_1 t694 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t685, t686, &t687, ((uint64_t)0ULL), t689, t692, t693);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix668:;
ix654:;
        goto me5;
mn651:;
ma29:;
        uint8_t t698 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t698 != ((uint8_t)7ULL)) goto pa697;
        goto po696;
pa697:;
        uint8_t t700 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t700 != ((uint8_t)10ULL)) goto pa699;
        goto po696;
pa699:;
        uint8_t t702 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t702 != ((uint8_t)9ULL)) goto pa701;
        goto po696;
pa701:;
        goto mn695;
po696:;
        {
            void * t705 = gm_l0;
            void * t706 = gm_l1;
            gm_t9qkh0rsooc8d t707 = gm_f_vc_placeable_ip26ve2(t705, t706);
            gm_t9qkh0rsooc8d t708 = t707;
            uint8_t t709 = ((GM_LD(uint8_t, ((uint8_t *)&t708) + 0) == ((uint8_t)0ULL)));
            if (!t709) goto ie704;
            {
                void * t711 = gm_l0;
                void * t712 = gm_l0;
                uint32_t t713 = gm_f_vc_alloc_3hmfvtt(t712);
                void * t714 = gm_l1;
                gm_ttggq7ng4kp3p t715;
                GM_ST(uint32_t, (uint8_t *)&t715 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t715 + 0), t713);
                GM_ST(void *, ((uint8_t *)&t715 + 8), t714);
                gm_tjc2ga47o3k6g t716;
                memset(&t716, 0, sizeof t716);
                GM_ST(uint8_t, ((uint8_t *)&t716 + 0), ((uint8_t)61ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t716 + 8), t715);
                gm_b0_1 t717 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t711, &t716);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix704;
ie704:;
            {
                void * t719 = gm_l0;
                void * t720 = gm_l1;
                gm_f_vc_place_i1sksai((void *)&F.l62, (void *)NULL, cx, NULL, t719, t720);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t721 = gm_l0;
                uint32_t t722 = gm_l2;
                gm_tiutk3b96cnlp t723 = F.l62;
                void * t724 = gm_l1;
                uint64_t t725 = GM_LD(uint64_t, (((uint8_t *)t724) + 104));
                void * t726 = gm_l0;
                void * t727 = gm_l1;
                uint32_t t728 = gm_f_vc_align_na1h2on((void *)NULL, cx, NULL, t726, t727);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t729 = gm_l1;
                gm_b0_1 t730 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t721, t722, &t723, ((uint64_t)0ULL), t725, t728, t729);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix704:;
        }
        goto me5;
mn695:;
ma30:;
        uint8_t t734 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t734 != ((uint8_t)14ULL)) goto pa733;
        void * t735 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l63 = t735;
        goto po732;
pa733:;
        uint8_t t737 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t737 != ((uint8_t)15ULL)) goto pa736;
        void * t738 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l63 = t738;
        goto po732;
pa736:;
        goto mn731;
po732:;
        {
            void * t741 = gm_l0;
            void * t742 = gm_l63;
            gm_t9qkh0rsooc8d t743 = gm_f_vc_placeable_ip26ve2(t741, t742);
            gm_t9qkh0rsooc8d t744 = t743;
            uint8_t t745 = ((GM_LD(uint8_t, ((uint8_t *)&t744) + 0) == ((uint8_t)0ULL)));
            if (!t745) goto ie740;
            {
                void * t747 = gm_l0;
                void * t748 = gm_l0;
                uint32_t t749 = gm_f_vc_alloc_3hmfvtt(t748);
                void * t750 = gm_l1;
                gm_ttggq7ng4kp3p t751;
                GM_ST(uint32_t, (uint8_t *)&t751 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t751 + 0), t749);
                GM_ST(void *, ((uint8_t *)&t751 + 8), t750);
                gm_tjc2ga47o3k6g t752;
                memset(&t752, 0, sizeof t752);
                GM_ST(uint8_t, ((uint8_t *)&t752 + 0), ((uint8_t)61ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t752 + 8), t751);
                gm_b0_1 t753 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t747, &t752);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix740;
ie740:;
            {
                void * t755 = gm_l0;
                void * t756 = gm_l63;
                gm_f_vc_place_i1sksai((void *)&F.l65, (void *)NULL, cx, NULL, t755, t756);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t757 = gm_l0;
                void * t758 = GM_LD(void *, (((uint8_t *)t757) + 0));
                uint64_t t759 = GM_LD(uint64_t, (((uint8_t *)t758) + 176));
                gm_l66 = t759;
            }
            {
                void * t760 = gm_l0;
                uint32_t t761 = gm_l2;
                gm_tiutk3b96cnlp t762 = F.l65;
                uint64_t t763;
                void * t765 = gm_l1;
                uint8_t t766;
                uint8_t t769 = GM_LD(uint8_t, ((((uint8_t *)t765) + 0) + 0));
                if (t769 != ((uint8_t)14ULL)) goto mn768;
                t766 = ((uint8_t)1);
                goto me767;
mn768:;
                t766 = ((uint8_t)0);
                goto me767;
mn770:;
                gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 781, 40);
me767:;
                if (!t766) goto ie764;
                uint64_t t772 = gm_l66;
                uint64_t t773 = t772;
                t763 = t773;
                goto ix764;
ie764:;
                uint64_t t775 = ((uint64_t)0ULL);
                t763 = t775;
ix764:;
                uint64_t t776 = gm_l66;
                uint64_t t777 = gm_l66;
                uint32_t t778 = (uint32_t)t777;
                void * t779 = gm_l1;
                gm_b0_1 t780 = gm_f_vc_load_v0jk07v((void *)NULL, cx, NULL, t760, t761, &t762, t763, t776, t778, t779);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix740:;
        }
        goto me5;
mn731:;
ma31:;
        uint8_t t782 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t782 != ((uint8_t)33ULL)) goto mn781;
        gm_tmn7n0n9raou5 t783 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l67 = t783;
        void * t784 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l68 = t784;
        {
            gm_tmn7n0n9raou5 t787 = gm_l67;
            gm_tnj2fm376v8gd t788;
            gm_tmn7n0n9raou5 t789;
            GM_ST(uint8_t, (uint8_t *)&t789 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t789 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t789 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t789 + 0), ((uint8_t)6ULL));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t788 + 0, t789);
            gm_tmn7n0n9raou5 t790;
            GM_ST(uint8_t, (uint8_t *)&t790 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t790 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t790 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t790 + 0), ((uint8_t)8ULL));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t788 + 8, t790);
            gm_tmn7n0n9raou5 t791;
            GM_ST(uint8_t, (uint8_t *)&t791 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t791 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t791 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t791 + 0), ((uint8_t)10ULL));
            GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t788 + 16, t791);
            gm_tnj2fm376v8gd t792 = t788;
            uint8_t t793 = 0;
            gm_tmn7n0n9raou5 t794 = t787;
            if (gm_eqcmo75g2ssd(((uint8_t *)&t792) + 0, ((uint8_t *)&t794))) t793 = 1;
            if (gm_eqcmo75g2ssd(((uint8_t *)&t792) + 8, ((uint8_t *)&t794))) t793 = 1;
            if (gm_eqcmo75g2ssd(((uint8_t *)&t792) + 16, ((uint8_t *)&t794))) t793 = 1;
            uint8_t t795 = (uint8_t)(t793);
            if (!t795) goto ie786;
            void * t796 = gm_l68;
            uint64_t t797 = GM_LD(uint64_t, (((uint8_t *)t796) + 104));
            void * t798 = gm_l1;
            uint64_t t799 = GM_LD(uint64_t, (((uint8_t *)t798) + 104));
            uint8_t t800 = ((t797 == t799));
            if (!t800) goto ie786;
            {
                void * t802 = gm_l0;
                void * t803 = gm_l68;
                gm_b0_1 t804 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t802, t803);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix786;
ie786:;
            {
                void * t806 = gm_l0;
                void * t807 = gm_l1;
                gm_b0_1 t808 = gm_f_vc_word_via_slot_snr97k7((void *)NULL, cx, NULL, t806, t807);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix786:;
        }
        goto me5;
mn781:;
ma32:;
        uint8_t t810 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t810 != ((uint8_t)34ULL)) goto mn809;
        gm_tvdg94bk7aa22 t811 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l69 = t811;
        gm_b16_8 t812 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l70 = t812;
        {
            gm_tvdg94bk7aa22 t815 = gm_l69;
            gm_t0v9ekh8mobe3 t816;
            gm_tvdg94bk7aa22 t817;
            GM_ST(uint8_t, (uint8_t *)&t817 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t817 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t817 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t817 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t817 + 0), ((uint8_t)8ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t816 + 0, t817);
            gm_tvdg94bk7aa22 t818;
            GM_ST(uint8_t, (uint8_t *)&t818 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t818 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t818 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t818 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t818 + 0), ((uint8_t)9ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t816 + 12, t818);
            gm_tvdg94bk7aa22 t819;
            GM_ST(uint8_t, (uint8_t *)&t819 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t819 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t819 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t819 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t819 + 0), ((uint8_t)13ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t816 + 24, t819);
            gm_tvdg94bk7aa22 t820;
            GM_ST(uint8_t, (uint8_t *)&t820 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t820 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t820 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t820 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t820 + 0), ((uint8_t)35ULL));
            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t816 + 36, t820);
            gm_t0v9ekh8mobe3 t821 = t816;
            uint8_t t822 = 0;
            gm_tvdg94bk7aa22 t823 = t815;
            if (gm_eqg63o53ar52(((uint8_t *)&t821) + 0, ((uint8_t *)&t823))) t822 = 1;
            if (gm_eqg63o53ar52(((uint8_t *)&t821) + 12, ((uint8_t *)&t823))) t822 = 1;
            if (gm_eqg63o53ar52(((uint8_t *)&t821) + 24, ((uint8_t *)&t823))) t822 = 1;
            if (gm_eqg63o53ar52(((uint8_t *)&t821) + 36, ((uint8_t *)&t823))) t822 = 1;
            uint8_t t824 = (uint8_t)(t822);
            if (!t824) goto ie814;
            uint64_t t825 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
            uint8_t t826 = ((t825 == ((uint64_t)1ULL)));
            if (!t826) goto ie814;
            uint64_t t827 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t828 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
            if (t827 >= (uintptr_t)(t828)) gm_trap_index(t827, t828, gm_srcutmnmd444bu0, 793, 89);
            void * t829 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t827 * 8));
            uint64_t t830 = GM_LD(uint64_t, (((uint8_t *)t829) + 104));
            void * t831 = gm_l1;
            uint64_t t832 = GM_LD(uint64_t, (((uint8_t *)t831) + 104));
            uint8_t t833 = ((t830 == t832));
            if (!t833) goto ie814;
            {
                void * t835 = gm_l0;
                uint64_t t836 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t837 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                if (t836 >= (uintptr_t)(t837)) gm_trap_index(t836, t837, gm_srcutmnmd444bu0, 794, 27);
                void * t838 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t836 * 8));
                gm_b0_1 t839 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t835, t838);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint8_t t841 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l69) + 0) == ((uint8_t)8ULL)));
                if (!t841) goto ie840;
                void * t842 = gm_l0;
                void * t843 = GM_LD(void *, (((uint8_t *)t842) + 0));
                uint8_t t844 = gm_f_fast_23em7sv(t843);
                uint8_t t845 = (uint8_t)!t844;
                if (!t845) goto ie840;
                {
                    void * t847 = gm_l0;
                    void * t848 = GM_LD(void *, (((uint8_t *)t847) + 0));
                    void * t849 = gm_l1;
                    gm_t88rcucg1b7uk t850 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t849) + 96));
                    uint64_t t851 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t848, t850);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l71 = t851;
                }
                {
                    uint64_t t853 = gm_l71;
                    uint8_t t854 = (t853 > ((uint64_t)1ULL));
                    if (!t854) goto ie852;
                    void * t855 = gm_l0;
                    void * t856 = GM_LD(void *, (((uint8_t *)t855) + 0));
                    uint64_t t857 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t858 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                    if (t857 >= (uintptr_t)(t858)) gm_trap_index(t857, t858, gm_srcutmnmd444bu0, 798, 49);
                    void * t859 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t857 * 8));
                    gm_t88rcucg1b7uk t860 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t859) + 96));
                    uint64_t t861 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t856, t860);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t862 = gm_l71;
                    uint8_t t863 = (t861 < t862);
                    if (!t863) goto ie852;
                    {
                        void * t865 = gm_l0;
                        gm_t5r2drn9pjqkt t866;
                        memset((uint8_t *)&t866 + 4, 0, 4);
                        uint32_t t867 = gm_l2;
                        GM_ST(uint32_t, (uint8_t *)&t866 + 0, t867);
                        uint64_t t868 = gm_l71;
                        GM_ST(uint64_t, (uint8_t *)&t866 + 8, t868);
                        void * t869 = gm_l1;
                        GM_ST(void *, (uint8_t *)&t866 + 16, t869);
                        gm_tjc2ga47o3k6g t870;
                        GM_ST(uint8_t, (uint8_t *)&t870 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t870 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t870 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t870 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t870 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t870 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t870 + 0), ((uint8_t)43ULL));
                        GM_ST(gm_t5r2drn9pjqkt, ((uint8_t *)&t870 + 8), t866);
                        gm_b0_1 t871 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t865, &t870);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix852;
ie852:;
ix852:;
                }
                goto ix840;
ie840:;
ix840:;
            }
            goto ix814;
ie814:;
            gm_tvdg94bk7aa22 t873 = gm_l69;
            gm_b16_8 t874 = gm_l70;
            uint8_t t875 = gm_f_vc_builtin_ok_kbscem0(t873, t874);
            if (!t875) goto ie872;
            {
                void * t877 = gm_l0;
                gm_b16_8 t878 = gm_l70;
                uint32_t t879 = gm_l2;
                gm_tj2m9dbh1ekv6 t880;
                GM_ST(uint8_t, (uint8_t *)&t880 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t880 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t880 + 0), ((uint8_t)1ULL));
                GM_ST(uint32_t, ((uint8_t *)&t880 + 4), t879);
                void * t881 = gm_l1;
                gm_b0_1 t882 = gm_f_vc_builtin_9d4kon2((void *)NULL, cx, NULL, t877, t878, t880, t881);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix872;
ie872:;
            gm_tvdg94bk7aa22 t884 = gm_l69;
            gm_b16_8 t885 = gm_l70;
            uint8_t t886 = gm_f_vc_assert_ok_6n0rc7j(t884, t885);
            if (!t886) goto ie883;
            {
                void * t889 = gm_l0;
                void * t890 = GM_LD(void *, (((uint8_t *)t889) + 0));
                uint8_t t891 = gm_f_fast_23em7sv(t890);
                uint8_t t892 = (uint8_t)!t891;
                if (!t892) goto ie888;
                {
                    void * t894 = gm_l0;
                    uint64_t t895 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t896 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                    if (t895 >= (uintptr_t)(t896)) gm_trap_index(t895, t896, gm_srcutmnmd444bu0, 805, 31);
                    void * t897 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t895 * 8));
                    gm_b0_1 t898 = gm_f_vc_word_11r57ch((void *)NULL, cx, NULL, t894, t897);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t899 = gm_l0;
                    uint32_t t900 = gm_l2;
                    gm_tcc1rq4n1avb2 t901;
                    GM_ST(uint32_t, ((uint8_t *)&t901 + 0), t900);
                    GM_ST(uint32_t, ((uint8_t *)&t901 + 4), ((uint32_t)0ULL));
                    gm_tjc2ga47o3k6g t902;
                    memset(&t902, 0, sizeof t902);
                    GM_ST(uint8_t, ((uint8_t *)&t902 + 0), ((uint8_t)40ULL));
                    GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t902 + 8), t901);
                    uint32_t t903 = gm_f_vc_jump_f1p9hn7((void *)NULL, cx, NULL, t899, &t902);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l72 = t903;
                }
                {
                    uint64_t t905 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                    uint8_t t906 = ((t905 == ((uint64_t)2ULL)));
                    if (!t906) goto ie904;
                    {
                        void * t908 = gm_l0;
                        uint64_t t909 = GM_LD(uint64_t, (((uint8_t *)t908) + 248));
                        gm_l73 = t909;
                    }
                    {
                        void * t910 = gm_l0;
                        void * t911 = gm_l0;
                        uint64_t t912 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t913 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                        if (t912 >= (uintptr_t)(t913)) gm_trap_index(t912, t913, gm_srcutmnmd444bu0, 809, 60);
                        void * t914 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t912 * 8));
                        uint64_t t915 = GM_LD(uint64_t, (((uint8_t *)t914) + 104));
                        uint64_t t916 = gm_f_vc_alloc_slot_5eptdbb(t911, t915);
                        uint32_t t917 = gm_f_vc_slot_at_rc6jhbj(t910, t916);
                        gm_l74 = t917;
                    }
                    {
                        void * t918 = gm_l0;
                        uint64_t t919 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t920 = GM_LD(uintptr_t, ((uint8_t *)&gm_l70) + 8);
                        if (t919 >= (uintptr_t)(t920)) gm_trap_index(t919, t920, gm_srcutmnmd444bu0, 810, 38);
                        void * t921 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l70)) + t919 * 8));
                        uint32_t t922 = gm_l74;
                        gm_b0_1 t923 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t918, t921, t922);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t924 = gm_l0;
                        uint32_t t925 = gm_l74;
                        void * t926 = gm_l1;
                        gm_t3hcdie243ee3 t927;
                        GM_ST(uint8_t, (uint8_t *)&t927 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t927 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t927 + 0), t925);
                        GM_ST(uint8_t, ((uint8_t *)&t927 + 4), ((uint8_t)1));
                        GM_ST(void *, ((uint8_t *)&t927 + 8), t926);
                        gm_tjc2ga47o3k6g t928;
                        memset(&t928, 0, sizeof t928);
                        GM_ST(uint8_t, ((uint8_t *)&t928 + 0), ((uint8_t)49ULL));
                        GM_ST(gm_t3hcdie243ee3, ((uint8_t *)&t928 + 8), t927);
                        gm_b0_1 t929 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t924, &t928);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        void * t930 = gm_l0;
                        uint64_t t931 = gm_l73;
                        GM_ST(uint64_t, (((uint8_t *)t930) + 248), t931);
                    }
                    goto ix904;
ie904:;
                    {
                        void * t933 = gm_l0;
                        void * t934 = gm_l1;
                        gm_t3hcdie243ee3 t935;
                        GM_ST(uint8_t, (uint8_t *)&t935 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t935 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t935 + 0), ((uint32_t)0ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t935 + 4), ((uint8_t)0));
                        GM_ST(void *, ((uint8_t *)&t935 + 8), t934);
                        gm_tjc2ga47o3k6g t936;
                        memset(&t936, 0, sizeof t936);
                        GM_ST(uint8_t, ((uint8_t *)&t936 + 0), ((uint8_t)49ULL));
                        GM_ST(gm_t3hcdie243ee3, ((uint8_t *)&t936 + 8), t935);
                        gm_b0_1 t937 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t933, &t936);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
ix904:;
                }
                {
                    void * t938 = gm_l0;
                    uint32_t t939 = gm_l72;
                    void * t940 = gm_l0;
                    uint32_t t941 = gm_f_vc_here_cc4mbj3(t940);
                    gm_f_vc_patch_pbmmb3q(t938, t939, t941);
                }
                {
                    void * t942 = gm_l0;
                    uint32_t t943 = gm_l2;
                    GM_ST(uint32_t, (((uint8_t *)t942) + 224), t943);
                }
                goto ix888;
ie888:;
ix888:;
            }
            {
                void * t944 = gm_l0;
                void * t945 = gm_l0;
                uint32_t t946 = gm_f_vc_alloc_3hmfvtt(t945);
                gm_tb31k5l794t7p t947;
                GM_ST(uint32_t, (uint8_t *)&t947 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t947 + 0), t946);
                GM_ST(uint64_t, ((uint8_t *)&t947 + 8), ((uint64_t)0ULL));
                gm_tjc2ga47o3k6g t948;
                memset(&t948, 0, sizeof t948);
                GM_ST(uint8_t, ((uint8_t *)&t948 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t948 + 8), t947);
                gm_b0_1 t949 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t944, &t948);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix883;
ie883:;
            {
                void * t951 = gm_l0;
                void * t952 = gm_l0;
                uint32_t t953 = gm_f_vc_alloc_3hmfvtt(t952);
                void * t954 = gm_l1;
                gm_ttggq7ng4kp3p t955;
                GM_ST(uint32_t, (uint8_t *)&t955 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t955 + 0), t953);
                GM_ST(void *, ((uint8_t *)&t955 + 8), t954);
                gm_tjc2ga47o3k6g t956;
                memset(&t956, 0, sizeof t956);
                GM_ST(uint8_t, ((uint8_t *)&t956 + 0), ((uint8_t)61ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t956 + 8), t955);
                gm_b0_1 t957 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t951, &t956);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix883:;
ix872:;
ix814:;
        }
        goto me5;
mn809:;
ma33:;
        uint8_t t959 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t959 != ((uint8_t)47ULL)) goto mn958;
        gm_t083kd7b1kkag t960 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t4) + 0) + 8));
        gm_l75 = t960;
        void * t962 = gm_l0;
        void * t963 = gm_l1;
        void * t964 = GM_LD(void *, (((uint8_t *)&gm_l75) + 0));
        gm_t9805qtonbsag t965 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l75) + 8));
        uint32_t t966 = GM_LD(uint32_t, (((uint8_t *)&gm_l75) + 16));
        uint8_t t967 = gm_f_vc_try_ok_ng7fba1((void *)NULL, cx, NULL, t962, t963, t964, t965, t966);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t967) goto ie961;
        {
            void * t969 = gm_l0;
            void * t970 = gm_l1;
            void * t971 = GM_LD(void *, (((uint8_t *)&gm_l75) + 0));
            gm_t9805qtonbsag t972 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l75) + 8));
            gm_t9805qtonbsag t973;
            memset(&t973, 0, sizeof t973);
            gm_b0_1 t974 = gm_f_vc_try_tbirnd2((void *)NULL, cx, NULL, t969, t970, t971, t972, t973);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix961;
ie961:;
        {
            void * t976 = gm_l0;
            void * t977 = gm_l0;
            uint32_t t978 = gm_f_vc_alloc_3hmfvtt(t977);
            void * t979 = gm_l1;
            gm_ttggq7ng4kp3p t980;
            GM_ST(uint32_t, (uint8_t *)&t980 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t980 + 0), t978);
            GM_ST(void *, ((uint8_t *)&t980 + 8), t979);
            gm_tjc2ga47o3k6g t981;
            memset(&t981, 0, sizeof t981);
            GM_ST(uint8_t, ((uint8_t *)&t981 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t981 + 8), t980);
            gm_b0_1 t982 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t976, &t981);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix961:;
        goto me5;
mn958:;
ma34:;
        uint8_t t984 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t984 != ((uint8_t)45ULL)) goto mn983;
        void * t985 = GM_LD(void *, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l76 = t985;
        uint8_t t986 = GM_LD(uint8_t, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l77 = t986;
        void * t988 = gm_l0;
        void * t989 = gm_l1;
        void * t990 = gm_l76;
        uint8_t t991 = gm_f_vc_unwrap_ok_tb3qnv4((void *)NULL, cx, NULL, t988, t989, t990);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t991) goto ie987;
        {
            void * t993 = gm_l0;
            void * t994 = gm_l1;
            void * t995 = gm_l76;
            uint8_t t996 = gm_l77;
            gm_t9805qtonbsag t997;
            memset(&t997, 0, sizeof t997);
            gm_b0_1 t998 = gm_f_vc_unwrap_ffrmkv5((void *)NULL, cx, NULL, t993, t994, t995, t996, t997);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix987;
ie987:;
        {
            void * t1000 = gm_l0;
            void * t1001 = gm_l0;
            uint32_t t1002 = gm_f_vc_alloc_3hmfvtt(t1001);
            void * t1003 = gm_l1;
            gm_ttggq7ng4kp3p t1004;
            GM_ST(uint32_t, (uint8_t *)&t1004 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t1004 + 0), t1002);
            GM_ST(void *, ((uint8_t *)&t1004 + 8), t1003);
            gm_tjc2ga47o3k6g t1005;
            memset(&t1005, 0, sizeof t1005);
            GM_ST(uint8_t, ((uint8_t *)&t1005 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t1005 + 8), t1004);
            gm_b0_1 t1006 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t1000, &t1005);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix987:;
        goto me5;
mn983:;
ma35:;
        void * t1008 = gm_l0;
        void * t1009 = gm_l1;
        gm_b0_1 t1010 = gm_f_vc_word_via_slot_snr97k7((void *)NULL, cx, NULL, t1008, t1009);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn1007:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 644, 5);
me5:;
    }
    {
        void * t1011 = gm_l0;
        uint32_t t1012 = gm_l2;
        uint32_t r1013; bool o1014 = __builtin_add_overflow(t1012, ((uint32_t)1ULL), &r1013);
        if (o1014) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 828, 18);
        uint32_t t1015 = r1013;
        gm_f_vc_set_top_h0qmvd1(t1011, t1015);
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
__attribute__((aligned(4))) uint8_t gm_f_vc_word_only_rg8dfe5(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vc_word_only_rg8dfe5 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    gm_tkm39klvnn4cr gm_l3; /* x */
    gm_ta6jn77i9rmqf gm_l4; /* x */
    gm_tuggr5sml9o52 gm_l5; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* l */
    void * gm_l8; /* r */
    void * gm_l9; /* x */
    void * gm_l10; /* x */
    void * gm_l12; /* x */
    gm_b16_8 gm_l13; /* args */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    void * gm_l18; /* x */
    GM_STACK_CHECK(&F, 1061u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t23) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma21; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma21; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma17; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma21; case ((uint8_t)31ULL): goto ma21; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma21; case ((uint8_t)34ULL): goto ma21; case ((uint8_t)35ULL): goto ma15; case ((uint8_t)36ULL): goto ma16; case ((uint8_t)37ULL): goto ma21; case ((uint8_t)38ULL): goto ma21; case ((uint8_t)39ULL): goto ma21; case ((uint8_t)40ULL): goto ma21; case ((uint8_t)41ULL): goto ma21; case ((uint8_t)42ULL): goto ma21; case ((uint8_t)43ULL): goto ma21; case ((uint8_t)44ULL): goto ma21; case ((uint8_t)45ULL): goto ma21; case ((uint8_t)46ULL): goto ma21; case ((uint8_t)47ULL): goto ma21; case ((uint8_t)48ULL): goto ma21; case ((uint8_t)49ULL): goto ma21; case ((uint8_t)50ULL): goto ma21; case ((uint8_t)51ULL): goto ma21; case ((uint8_t)52ULL): goto ma21; case ((uint8_t)53ULL): goto ma21; case ((uint8_t)54ULL): goto ma21; case ((uint8_t)55ULL): goto ma21; case ((uint8_t)56ULL): goto ma21; case ((uint8_t)57ULL): goto ma21; case ((uint8_t)58ULL): goto ma21; case ((uint8_t)59ULL): goto ma21; case ((uint8_t)60ULL): goto ma21; case ((uint8_t)61ULL): goto ma21; case ((uint8_t)62ULL): goto ma21; default: goto ma21; }
ma4:;
    uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t27 != ((uint8_t)0ULL)) goto pa26;
    goto po25;
pa26:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t29 != ((uint8_t)6ULL)) goto pa28;
    goto po25;
pa28:;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t31 != ((uint8_t)7ULL)) goto pa30;
    goto po25;
pa30:;
    goto mn24;
po25:;
    t2 = ((uint8_t)1);
    goto me3;
mn24:;
ma5:;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t33 != ((uint8_t)21ULL)) goto mn32;
    gm_t0pc2c7dafsjt t34 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t1) + 0) + 8));
    F.l2 = t34;
    uint8_t t35 = 0;
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, (((uint8_t *)&F.l2) + 24));
    uint8_t t38 = gm_f_vc_word_only_rg8dfe5(t36, t37);
    if (t38) {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)&F.l2) + 32));
        uint8_t t41 = gm_f_vc_word_only_rg8dfe5(t39, t40);
        t35 = t41;
    } else t35 = 0;
    t2 = t35;
    goto me3;
mn32:;
ma6:;
    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t43 != ((uint8_t)23ULL)) goto mn42;
    gm_tkm39klvnn4cr t44 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t1) + 0) + 8));
    gm_l3 = t44;
    uint8_t t45 = 0;
    void * t46 = gm_l0;
    void * t47 = GM_LD(void *, (((uint8_t *)&gm_l3) + 16));
    uint8_t t48 = gm_f_vc_word_only_rg8dfe5(t46, t47);
    if (t48) {
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)&gm_l3) + 24));
        uint8_t t51 = gm_f_vc_word_only_rg8dfe5(t49, t50);
        t45 = t51;
    } else t45 = 0;
    t2 = t45;
    goto me3;
mn42:;
ma7:;
    uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t53 != ((uint8_t)24ULL)) goto mn52;
    gm_ta6jn77i9rmqf t54 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t1) + 0) + 8));
    gm_l4 = t54;
    uint8_t t55 = 0;
    void * t56 = gm_l0;
    void * t57 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    uint8_t t58 = gm_f_vc_word_only_rg8dfe5(t56, t57);
    if (t58) {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)&gm_l4) + 16));
        uint8_t t61 = gm_f_vc_word_only_rg8dfe5(t59, t60);
        t55 = t61;
    } else t55 = 0;
    t2 = t55;
    goto me3;
mn52:;
ma8:;
    uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t63 != ((uint8_t)22ULL)) goto mn62;
    gm_tuggr5sml9o52 t64 = GM_LD(gm_tuggr5sml9o52, ((((uint8_t *)t1) + 0) + 8));
    gm_l5 = t64;
    uint8_t t65 = 0;
    void * t66 = gm_l0;
    void * t67 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
    uint8_t t68 = gm_f_vc_word_only_rg8dfe5(t66, t67);
    if (t68) {
        void * t69 = gm_l0;
        void * t70 = GM_LD(void *, (((uint8_t *)&gm_l5) + 16));
        uint8_t t71 = gm_f_vc_word_only_rg8dfe5(t69, t70);
        t65 = t71;
    } else t65 = 0;
    t2 = t65;
    goto me3;
mn62:;
ma9:;
    uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t73 != ((uint8_t)25ULL)) goto mn72;
    void * t74 = GM_LD(void *, ((((uint8_t *)t1) + 0) + 8));
    gm_l6 = t74;
    void * t75 = gm_l0;
    void * t76 = gm_l6;
    uint8_t t77 = gm_f_vc_word_only_rg8dfe5(t75, t76);
    t2 = t77;
    goto me3;
mn72:;
ma10:;
    uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t79 != ((uint8_t)32ULL)) goto mn78;
    void * t80 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 8));
    gm_l7 = t80;
    void * t81 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l8 = t81;
    uint8_t t82 = 0;
    void * t83 = gm_l0;
    void * t84 = gm_l7;
    uint8_t t85 = gm_f_vc_word_only_rg8dfe5(t83, t84);
    if (t85) {
        void * t86 = gm_l0;
        void * t87 = gm_l8;
        uint8_t t88 = gm_f_vc_word_only_rg8dfe5(t86, t87);
        t82 = t88;
    } else t82 = 0;
    t2 = t82;
    goto me3;
mn78:;
ma11:;
    uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t90 != ((uint8_t)26ULL)) goto mn89;
    void * t91 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 16));
    gm_l9 = t91;
    void * t92 = gm_l0;
    void * t93 = gm_l9;
    uint8_t t94 = gm_f_vc_word_only_rg8dfe5(t92, t93);
    t2 = t94;
    goto me3;
mn89:;
ma12:;
    uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t96 != ((uint8_t)27ULL)) goto mn95;
    void * t97 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 32));
    gm_l10 = t97;
    void * t98 = gm_l0;
    void * t99 = gm_l10;
    uint8_t t100 = gm_f_vc_word_only_rg8dfe5(t98, t99);
    t2 = t100;
    goto me3;
mn95:;
ma13:;
    uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t102 != ((uint8_t)28ULL)) goto mn101;
    gm_tscalq56heofe t103 = GM_LD(gm_tscalq56heofe, ((((uint8_t *)t1) + 0) + 8));
    F.l11 = t103;
    uint8_t t104 = 0;
    void * t105 = gm_l0;
    void * t106 = GM_LD(void *, (((uint8_t *)&F.l11) + 24));
    uint8_t t107 = gm_f_vc_word_only_rg8dfe5(t105, t106);
    if (t107) {
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, (((uint8_t *)&F.l11) + 32));
        uint8_t t110 = gm_f_vc_word_only_rg8dfe5(t108, t109);
        t104 = t110;
    } else t104 = 0;
    t2 = t104;
    goto me3;
mn101:;
ma14:;
    uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t112 != ((uint8_t)29ULL)) goto mn111;
    void * t113 = GM_LD(void *, (((((uint8_t *)t1) + 0) + 8) + 24));
    gm_l12 = t113;
    void * t114 = gm_l0;
    void * t115 = gm_l12;
    uint8_t t116 = gm_f_vc_word_only_rg8dfe5(t114, t115);
    t2 = t116;
    goto me3;
mn111:;
ma15:;
    uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t118 != ((uint8_t)35ULL)) goto mn117;
    gm_b16_8 t119 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 0) + 8) + 8));
    gm_l13 = t119;
    void * t120 = gm_l0;
    gm_b16_8 t121 = gm_l13;
    uint8_t t122 = gm_f_vc_all_word_only_4ufgbu6(t120, t121);
    t2 = t122;
    goto me3;
mn117:;
ma16:;
    uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t124 != ((uint8_t)36ULL)) goto mn123;
    gm_tf4r0415i49d1 t125 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t1) + 0) + 8));
    F.l14 = t125;
    void * t126 = gm_l0;
    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 32));
    uint8_t t128 = gm_f_vc_all_word_only_4ufgbu6(t126, t127);
    t2 = t128;
    goto me3;
mn123:;
ma17:;
    uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t132 != ((uint8_t)10ULL)) goto pa131;
    goto po130;
pa131:;
    uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t134 != ((uint8_t)9ULL)) goto pa133;
    goto po130;
pa133:;
    goto mn129;
po130:;
    uint8_t t135 = 0;
    void * t136 = gm_l0;
    void * t137 = gm_l1;
    gm_t9qkh0rsooc8d t138 = gm_f_vc_placeable_ip26ve2(t136, t137);
    gm_t9qkh0rsooc8d t139 = t138;
    uint8_t t140 = (!(GM_LD(uint8_t, ((uint8_t *)&t139) + 0) == ((uint8_t)0ULL)));
    if (t140) {
        void * t141 = gm_l0;
        void * t142 = gm_l1;
        uint8_t t143 = gm_f_vc_place_word_only_b4oj6ia(t141, t142);
        t135 = t143;
    } else t135 = 0;
    t2 = t135;
    goto me3;
mn129:;
ma18:;
    uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t145 != ((uint8_t)12ULL)) goto mn144;
    gm_tp0fa5qv7rqnn t146 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t1) + 0) + 8));
    F.l15 = t146;
    uint8_t t147 = 0;
    uint8_t t148 = 0;
    void * t149 = gm_l0;
    void * t150 = gm_l1;
    gm_t9qkh0rsooc8d t151 = gm_f_vc_placeable_ip26ve2(t149, t150);
    gm_t9qkh0rsooc8d t152 = t151;
    uint8_t t153 = (!(GM_LD(uint8_t, ((uint8_t *)&t152) + 0) == ((uint8_t)0ULL)));
    if (t153) {
        void * t154 = gm_l0;
        void * t155 = gm_l1;
        uint8_t t156 = gm_f_vc_place_word_only_b4oj6ia(t154, t155);
        t148 = t156;
    } else t148 = 0;
    if (t148) {
        void * t157 = gm_l0;
        void * t158 = GM_LD(void *, (((uint8_t *)&F.l15) + 8));
        uint8_t t159 = gm_f_vc_word_only_rg8dfe5(t157, t158);
        t147 = t159;
    } else t147 = 0;
    t2 = t147;
    goto me3;
mn144:;
ma19:;
    uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t163 != ((uint8_t)14ULL)) goto pa162;
    void * t164 = GM_LD(void *, ((((uint8_t *)t1) + 0) + 8));
    gm_l16 = t164;
    goto po161;
pa162:;
    uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t166 != ((uint8_t)15ULL)) goto pa165;
    void * t167 = GM_LD(void *, ((((uint8_t *)t1) + 0) + 8));
    gm_l16 = t167;
    goto po161;
pa165:;
    goto mn160;
po161:;
    uint8_t t168 = 0;
    void * t169 = gm_l0;
    void * t170 = gm_l16;
    gm_t9qkh0rsooc8d t171 = gm_f_vc_placeable_ip26ve2(t169, t170);
    gm_t9qkh0rsooc8d t172 = t171;
    uint8_t t173 = (!(GM_LD(uint8_t, ((uint8_t *)&t172) + 0) == ((uint8_t)0ULL)));
    if (t173) {
        void * t174 = gm_l0;
        void * t175 = gm_l16;
        uint8_t t176 = gm_f_vc_place_word_only_b4oj6ia(t174, t175);
        t168 = t176;
    } else t168 = 0;
    t2 = t168;
    goto me3;
mn160:;
ma20:;
    uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t178 != ((uint8_t)16ULL)) goto mn177;
    void * t179 = GM_LD(void *, ((((uint8_t *)t1) + 0) + 8));
    gm_l18 = t179;
    uint8_t t180 = 0;
    void * t181 = gm_l0;
    void * t182 = gm_l18;
    uint8_t t183 = gm_f_vc_addressable_ltobf02(t181, t182);
    if (t183) {
        void * t184 = gm_l0;
        void * t185 = gm_l18;
        uint8_t t186 = gm_f_vc_place_word_only_b4oj6ia(t184, t185);
        t180 = t186;
    } else t180 = 0;
    t2 = t180;
    goto me3;
mn177:;
ma21:;
    t2 = ((uint8_t)0);
    goto me3;
mn187:;
ma22:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 1400, 61);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_to_fgjor3m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_vc_word_to_fgjor3m F;
    void * gm_l0; /* c */
    void * gm_l1; /* value */
    uint32_t gm_l2; /* d */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* save */
    uint32_t gm_l5; /* v */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
        if (t4 != ((uint8_t)0ULL)) goto ie2;
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 0) + 8));
        gm_l3 = t5;
        {
            void * t7 = gm_l0;
            uint32_t t8 = gm_l2;
            uint64_t t9 = gm_l3;
            gm_tb31k5l794t7p t10;
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t8);
            GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
            gm_tjc2ga47o3k6g t11;
            memset(&t11, 0, sizeof t11);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t11 + 8), t10);
            gm_b0_1 t12 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t7, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
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
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 224));
        gm_l4 = t16;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        uint32_t t19 = gm_f_vc_operand_nmt6qjg((void *)NULL, cx, NULL, t17, t18, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t19;
    }
    {
        uint32_t t21 = gm_l5;
        uint32_t t22 = gm_l2;
        uint8_t t23 = (!(t21 == t22));
        if (!t23) goto ie20;
        uint8_t t24 = 0;
        uint32_t t25 = gm_l5;
        uint32_t t26 = gm_l4;
        uint8_t t27 = ((t25 == t26));
        if (t27) {
            void * t28 = gm_l0;
            uint32_t t29 = gm_l5;
            uint32_t t30 = gm_l2;
            uint8_t t31 = gm_f_vc_retarget_cjve41g(t28, t29, t30);
            t24 = t31;
        } else t24 = 0;
        uint8_t t32 = (uint8_t)!t24;
        if (!t32) goto ie20;
        {
            void * t34 = gm_l0;
            uint32_t t35 = gm_l2;
            uint32_t t36 = gm_l5;
            gm_tcc1rq4n1avb2 t37;
            GM_ST(uint32_t, ((uint8_t *)&t37 + 0), t35);
            GM_ST(uint32_t, ((uint8_t *)&t37 + 4), t36);
            gm_tjc2ga47o3k6g t38;
            memset(&t38, 0, sizeof t38);
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)65ULL));
            GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t38 + 8), t37);
            gm_b0_1 t39 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t34, &t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t40 = gm_l0;
        uint32_t t41 = gm_l4;
        GM_ST(uint32_t, (((uint8_t *)t40) + 224), t41);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_vc_word_via_slot_snr97k7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_vc_word_via_slot_snr97k7 F;
    void * gm_l0; /* c */
    void * gm_l1; /* n */
    uint64_t gm_l2; /* slots */
    uint32_t gm_l3; /* at */
    GM_STACK_CHECK(&F, 370u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        if (!t5) goto ie2;
        void * t6 = gm_l0;
        void * t7 = gm_l1;
        uint8_t t8 = gm_f_vc_builds_d9pue9e((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 248));
            gm_l2 = t11;
        }
        {
            void * t12 = gm_l0;
            void * t13 = gm_l0;
            void * t14 = gm_l1;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 104));
            uint64_t t16 = gm_f_vc_alloc_slot_5eptdbb(t13, t15);
            uint32_t t17 = gm_f_vc_slot_at_rc6jhbj(t12, t16);
            gm_l3 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = gm_l1;
            uint32_t t20 = gm_l3;
            gm_b0_1 t21 = gm_f_vc_to_slot_70oqqr5((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t22 = gm_l0;
            void * t23 = gm_l0;
            uint32_t t24 = gm_f_vc_alloc_3hmfvtt(t23);
            uint32_t t25 = gm_l3;
            void * t26 = gm_l1;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 104));
            uint32_t t28 = (uint32_t)t27;
            gm_tl874t2atnlh3 t29;
            GM_ST(uint32_t, ((uint8_t *)&t29 + 0), t24);
            GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t25);
            GM_ST(uint32_t, ((uint8_t *)&t29 + 8), t28);
            gm_tjc2ga47o3k6g t30;
            memset(&t30, 0, sizeof t30);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)53ULL));
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t30 + 8), t29);
            gm_b0_1 t31 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t22, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l2;
            GM_ST(uint64_t, (((uint8_t *)t32) + 248), t33);
        }
        goto ix2;
ie2:;
        {
            void * t35 = gm_l0;
            void * t36 = gm_l0;
            uint32_t t37 = gm_f_vc_alloc_3hmfvtt(t36);
            void * t38 = gm_l1;
            gm_ttggq7ng4kp3p t39;
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t37);
            GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
            gm_tjc2ga47o3k6g t40;
            memset(&t40, 0, sizeof t40);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)61ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t40 + 8), t39);
            gm_b0_1 t41 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t35, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
__attribute__((aligned(4))) void * gm_f_vm_compile_gef97rj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vm_compile_gef97rj F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    void * gm_l2; /* body */
    uint64_t gm_l3; /* sbase */
    uint64_t gm_l4; /* slots */
    uint32_t gm_l6; /* rbase */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* v */
    uint32_t gm_l12; /* first */
    uint64_t gm_l14; /* regs_off */
    uint64_t gm_l15; /* nregs */
    gm_b16_8 gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    void * gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    GM_STACK_CHECK(&F, 1549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 136));
        uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)7ULL), &r4);
        if (o5) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 63, 15);
        uint64_t t6 = r4;
        if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 63, 14);
        uint64_t t7 = (uint64_t)(t6 / ((uint64_t)8ULL));
        uint64_t r8; bool o9 = __builtin_mul_overflow(t7, ((uint64_t)8ULL), &r8);
        if (o9) gm_trap("integer overflow in `*`", gm_srcutmnmd444bu0, 63, 14);
        uint64_t t10 = r8;
        gm_l3 = t10;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_f_list_kl0t9m0((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb11;
            {
                uint64_t t14 = gm_l3;
                uint64_t t15 = gm_l4;
                uint64_t r16; bool o17 = __builtin_add_overflow(t14, t15, &r16);
                if (o17) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 70, 20);
                uint64_t t18 = r16;
                if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 70, 19);
                uint64_t t19 = (uint64_t)(t18 / ((uint64_t)8ULL));
                uint32_t t20 = (uint32_t)t19;
                gm_l6 = t20;
            }
            {
                gm_f_hash_map_7dqhv02((void *)&F.l7, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                {
                    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                    gm_b16_8 t23 = t22;
                    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                    gm_l8 = t24;
                }
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t26 = gm_l9;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t28 = (t26 < t27);
                        if (!t28) goto lb25;
                        {
                            uint64_t t31 = gm_l9;
                            uint64_t t32 = gm_l9;
                            uint64_t t33 = (uintptr_t)t32;
                            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t33 * 8));
                            gm_t2lqu8jsj511d t36;
                            GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t31);
                            GM_ST(uint64_t, ((uint8_t *)&t36 + 8), t35);
                            gm_t2lqu8jsj511d t37 = t36;
                            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&t37) + 0));
                            gm_l10 = t40;
                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t37) + 8));
                            gm_l11 = t41;
                            {
                                void * t43 = (void *)((uint8_t *)&F.l7);
                                uint64_t t44 = gm_l11;
                                uint32_t t45 = gm_l6;
                                uint64_t t46 = gm_l10;
                                uint32_t t47 = (uint32_t)t46;
                                uint32_t r48; bool o49 = __builtin_add_overflow(t45, t47, &r48);
                                if (o49) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 72, 63);
                                uint32_t t50 = r48;
                                gm_tftb1odc739t5 t51 = gm_f_put_6j13ahd(t43, t44, t50);
                                gm_tftb1odc739t5 t52 = t51;
                                if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcutmnmd444bu0, 72, 47);
                                gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                            }
                            goto me38;
mn39:;
                            gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 72, 9);
me38:;
                        }
lc25:;
                        {
                            uint64_t t55 = gm_l9;
                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                            uint64_t t58 = r56;
                            gm_l9 = t58;
                        }
                    }
lb25:;
                }
            }
            {
                uint32_t t59 = gm_l6;
                uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                uint32_t t61 = (uint32_t)t60;
                uint32_t r62; bool o63 = __builtin_add_overflow(t59, t61, &r62);
                if (o63) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 73, 18);
                uint32_t t64 = r62;
                gm_l12 = t64;
            }
            {
                memset((uint8_t *)&F.l13 + 140, 0, 4);
                memset((uint8_t *)&F.l13 + 236, 0, 4);
                void * t65 = gm_l0;
                GM_ST(void *, (uint8_t *)&F.l13 + 0, t65);
                void * t66 = gm_l1;
                GM_ST(void *, (uint8_t *)&F.l13 + 8, t66);
                gm_f_list_m6i5ef0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 16)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                gm_f_list_bf8dj7f((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 56)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 96)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 136, ((uint32_t)0ULL));
                gm_f_list_pcuroj5((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 144)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                gm_f_list_pcuroj5((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 184)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                uint32_t t67 = gm_l12;
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 224, t67);
                uint32_t t68 = gm_l12;
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 228, t68);
                uint32_t t69 = gm_l6;
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 232, t69);
                uint64_t t70 = gm_l3;
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 240, t70);
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 248, ((uint64_t)0ULL));
                GM_ST(uint64_t, (uint8_t *)&F.l13 + 256, ((uint64_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 264, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 268, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 272, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 276, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 280, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&F.l13 + 284, ((uint32_t)0ULL));
                memcpy((uint8_t *)&F.l13 + 288, ((uint8_t *)&F.l7), 56);
                gm_f_list_kl0t9m0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l13 + 344)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                void * t71 = (void *)((uint8_t *)&F.l13);
                void * t72 = gm_l2;
                gm_tmn7n0n9raou5 t73;
                GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)2ULL));
                gm_b0_1 t74 = gm_f_vc_expr_697jn9n((void *)NULL, cx, NULL, t71, t72, t73);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                void * t75 = (void *)((uint8_t *)&F.l13);
                gm_tjc2ga47o3k6g t76;
                memset(&t76, 0, sizeof t76);
                GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)75ULL));
                gm_b0_1 t77 = gm_f_vc_emit_phs805s((void *)NULL, cx, NULL, t75, &t76);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                void * t78 = (void *)(((uint8_t *)&F.l13) + 96);
                gm_f_deinit_padgogk(t78);
            }
            {
                void * t79 = (void *)(((uint8_t *)&F.l13) + 144);
                gm_f_deinit_sk5botm(t79);
            }
            {
                void * t80 = (void *)(((uint8_t *)&F.l13) + 184);
                gm_f_deinit_sk5botm(t80);
            }
            {
                void * t81 = (void *)(((uint8_t *)&F.l13) + 288);
                gm_f_deinit_s7049sc(t81);
            }
            {
                uint8_t t83 = 0;
                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 256));
                uint64_t t85 = gm_l4;
                uint8_t t86 = (t84 > t85);
                if (!t86) {
                    uint64_t t87 = GM_LD(uintptr_t, ((((uint8_t *)&F.l13) + 344) + 0) + 8);
                    uint8_t t88 = (t87 > ((uint64_t)0ULL));
                    t83 = t88;
                } else t83 = 1;
                if (!t83) goto ie82;
                {
                    uint64_t t90 = gm_l4;
                    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 256));
                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)7ULL), &r92);
                    if (o93) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 83, 32);
                    uint64_t t94 = r92;
                    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcutmnmd444bu0, 83, 31);
                    uint64_t t95 = (uint64_t)(t94 / ((uint64_t)8ULL));
                    uint64_t r96; bool o97 = __builtin_mul_overflow(t95, ((uint64_t)8ULL), &r96);
                    if (o97) gm_trap("integer overflow in `*`", gm_srcutmnmd444bu0, 83, 31);
                    uint64_t t98 = r96;
                    uint64_t t99 = (t90 > t98 ? t90 : t98);
                    gm_l4 = t99;
                }
                {
                    void * t100 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l13) + 344) + 0));
                    gm_b16_8 t102 = t101;
                    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                    gm_tftb1odc739t5 t104 = gm_f_extend_ll5819n(t100, t103);
                    gm_tftb1odc739t5 t105 = t104;
                    if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srcutmnmd444bu0, 84, 13);
                    gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
                }
                {
                    void * t107 = (void *)(((uint8_t *)&F.l13) + 344);
                    gm_f_deinit_v2ik7pl(t107);
                }
                {
                    void * t108 = (void *)(((uint8_t *)&F.l13) + 16);
                    gm_f_deinit_tbneotd(t108);
                }
                {
                    void * t109 = (void *)(((uint8_t *)&F.l13) + 56);
                    gm_f_deinit_bgn5i9p(t109);
                }
                {
                    goto lc11;
                }
                goto ix82;
ie82:;
ix82:;
            }
            {
                void * t110 = (void *)(((uint8_t *)&F.l13) + 344);
                gm_f_deinit_v2ik7pl(t110);
            }
            {
                void * t111 = (void *)((uint8_t *)&F.l13);
                gm_b0_1 t112 = gm_f_vc_compact_8tukqc5((void *)NULL, cx, NULL, t111);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
            {
                uint32_t t113 = gm_l6;
                uint64_t t114 = (uint64_t)t113;
                uint64_t r115; bool o116 = __builtin_mul_overflow(t114, ((uint64_t)8ULL), &r115);
                if (o116) gm_trap("integer overflow in `*`", gm_srcutmnmd444bu0, 92, 21);
                uint64_t t117 = r115;
                gm_l14 = t117;
            }
            {
                uint32_t t118 = GM_LD(uint32_t, (((uint8_t *)&F.l13) + 228));
                uint32_t t119 = gm_l6;
                uint32_t r120; bool o121 = __builtin_sub_overflow(t118, t119, &r120);
                if (o121) gm_trap("integer overflow in `-`", gm_srcutmnmd444bu0, 94, 23);
                uint32_t t122 = r120;
                uint32_t t123 = (t122 > ((uint32_t)1ULL) ? t122 : ((uint32_t)1ULL));
                uint64_t t124 = (uint64_t)t123;
                gm_l15 = t124;
            }
            {
                {
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l13) + 16) + 0));
                    gm_l16 = t126;
                }
                {
                    gm_b16_8 t127 = gm_f_empty_slice_8nfeia6();
                    gm_l17 = t127;
                }
                {
                    void * t128 = gm_l1;
                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 136));
                    gm_l18 = t129;
                }
                {
                    gm_b16_8 t130 = gm_f_empty_slice_d914i5u();
                    gm_l19 = t130;
                }
                {
                    void * t131 = gm_l1;
                    gm_l20 = t131;
                }
                {
                    gm_b16_8 t132 = gm_f_empty_slice_dg6bggq();
                    gm_l21 = t132;
                }
                {
                    uint64_t t133 = gm_l14;
                    gm_l22 = t133;
                }
                {
                    uint64_t t134 = gm_l14;
                    uint64_t t135 = gm_l15;
                    uint64_t r136; bool o137 = __builtin_mul_overflow(((uint64_t)8ULL), t135, &r136);
                    if (o137) gm_trap("integer overflow in `*`", gm_srcutmnmd444bu0, 95, 234);
                    uint64_t t138 = r136;
                    uint64_t r139; bool o140 = __builtin_add_overflow(t134, t138, &r139);
                    if (o140) gm_trap("integer overflow in `+`", gm_srcutmnmd444bu0, 95, 223);
                    uint64_t t141 = r139;
                    gm_l23 = t141;
                }
                {
                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l13) + 56) + 0));
                    gm_l24 = t142;
                }
                {
                    gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                    gm_l25 = t143;
                }
                gm_tedlrckoa1cir t144;
                memset((uint8_t *)&t144 + 33, 0, 7);
                memset((uint8_t *)&t144 + 92, 0, 4);
                gm_b16_8 t145 = gm_l16;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
                gm_b16_8 t146 = gm_l17;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 16, t146);
                GM_ST(uint8_t, (uint8_t *)&t144 + 32, ((uint8_t)0));
                uint64_t t147 = gm_l18;
                GM_ST(uint64_t, (uint8_t *)&t144 + 40, t147);
                gm_b16_8 t148 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 48, t148);
                void * t149 = gm_l20;
                GM_ST(void *, (uint8_t *)&t144 + 64, t149);
                gm_b16_8 t150 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 72, t150);
                uint32_t t151 = GM_LD(uint32_t, gm_cl7gtp51edk);
                GM_ST(uint32_t, (uint8_t *)&t144 + 88, t151);
                uint64_t t152 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t144 + 96, t152);
                uint64_t t153 = gm_l23;
                GM_ST(uint64_t, (uint8_t *)&t144 + 104, t153);
                gm_b16_8 t154 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 112, t154);
                gm_b16_8 t155 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 128, t155);
                gm_tedlrckoa1cir t156 = t144;
                gm_tq7s98ihonkju t157 = gm_f_new_uiusob6((void *)NULL, cx, NULL, &t156, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkuae0e6qb8)));
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                gm_tq7s98ihonkju t158 = t157;
                if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 8, gm_srcutmnmd444bu0, 95, 16);
                void * t159 = GM_LD(void *, (((uint8_t *)&t158) + 8));
                uint8_t t160 = 0;
                F.ret = t159;
                goto gm_fret;
            }
lc11:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
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
inline __attribute__((aligned(4))) gm_tj2m9dbh1ekv6 gm_f_want_dest_rhqgfel(gm_tmn7n0n9raou5 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_want_dest_rhqgfel F;
    gm_tmn7n0n9raou5 gm_l0; /* want */
    uint32_t gm_l1; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 71u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tj2m9dbh1ekv6 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma4; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    gm_tj2m9dbh1ekv6 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)3ULL));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)3ULL)) goto mn11;
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_l1 = t13;
    uint32_t t14 = gm_l1;
    gm_tj2m9dbh1ekv6 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
    GM_ST(uint32_t, ((uint8_t *)&t15 + 4), t14);
    t1 = t15;
    goto me2;
mn11:;
ma5:;
    gm_tj2m9dbh1ekv6 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
    t1 = t17;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srcutmnmd444bu0, 15, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
