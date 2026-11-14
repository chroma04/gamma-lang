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
typedef struct gm_cs2rc1d61u3qan { uint64_t f0; uint64_t f1; int32_t f2; _Bool f3; } gm_cs2rc1d61u3qan;
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_cseqep6akjq2jh { uint64_t f0; int32_t f1; } gm_cseqep6akjq2jh;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; int32_t f1; uint8_t pe[4]; } gm_t43v1gep754th;
typedef struct { uint32_t a[40]; } gm_t602t587brq3n;
typedef struct { uint64_t f0; uint64_t f1; int32_t f2; uint8_t f3; uint8_t pe[3]; } gm_t88486mk7vrvo;
typedef struct { uint8_t a[1100]; } gm_t91rshbm1iu79;
typedef struct { uint32_t a[37]; } gm_tss0v68n9gqca;
_Static_assert(sizeof(gm_cs2rc1d61u3qan) == 24 && _Alignof(gm_cs2rc1d61u3qan) == 8 && offsetof(gm_cs2rc1d61u3qan, f0) == 0 && offsetof(gm_cs2rc1d61u3qan, f1) == 8 && offsetof(gm_cs2rc1d61u3qan, f2) == 16 && offsetof(gm_cs2rc1d61u3qan, f3) == 20, "layout of `SciDigits`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseqep6akjq2jh) == 16 && _Alignof(gm_cseqep6akjq2jh) == 8 && offsetof(gm_cseqep6akjq2jh, f0) == 0 && offsetof(gm_cseqep6akjq2jh, f1) == 8, "layout of `Parts`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t43v1gep754th) == 16 && _Alignof(gm_t43v1gep754th) == 8 && offsetof(gm_t43v1gep754th, f0) == 0 && offsetof(gm_t43v1gep754th, f1) == 8, "layout of `(n: usize, exp: i32)`");
_Static_assert(sizeof(gm_t602t587brq3n) == 160 && _Alignof(gm_t602t587brq3n) == 4 && offsetof(gm_t602t587brq3n, a) == 0, "layout of `[40]u32`");
_Static_assert(sizeof(gm_t88486mk7vrvo) == 24 && _Alignof(gm_t88486mk7vrvo) == 8 && offsetof(gm_t88486mk7vrvo, f0) == 0 && offsetof(gm_t88486mk7vrvo, f1) == 8 && offsetof(gm_t88486mk7vrvo, f2) == 16 && offsetof(gm_t88486mk7vrvo, f3) == 20, "layout of `SciDigits`");
_Static_assert(sizeof(gm_t91rshbm1iu79) == 1100 && _Alignof(gm_t91rshbm1iu79) == 1 && offsetof(gm_t91rshbm1iu79, a) == 0, "layout of `[1100]u8`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tss0v68n9gqca) == 148 && _Alignof(gm_tss0v68n9gqca) == 4 && offsetof(gm_tss0v68n9gqca, a) == 0, "layout of `[37]u32`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
struct gm_fr_check_base_ulm9qq6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_decompose_c1606vm {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_digit_at_gkhhkiq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_digit_char_ie4khce {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_expand_7rpkqt1 {
    gm_frame h;
    gm_t602t587brq3n l5; /* limbs */
    gm_tss0v68n9gqca l24; /* limbs */
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_fixed_buf_len_gor7a4d {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fixed_rppim5r {
    gm_frame h;
    gm_t91rshbm1iu79 l4; /* d */
    gm_b16_8 ret;
};
struct gm_fr_put_u64_ant0p2p {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_round_digits_4thelh5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sci_buf_len_tedn1r4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_sci_digit_sqvlt0a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sci_digits_0sasrt8 {
    gm_frame h;
    gm_t88486mk7vrvo ret;
};
struct gm_fr_sci_oikoiqs {
    gm_frame h;
    gm_t91rshbm1iu79 l4; /* d */
    gm_b16_8 ret;
};
struct gm_fr_special_meo3u57 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_to_front_b98pl8o {
    gm_frame h;
    gm_b16_8 ret;
};
inline __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cseqep6akjq2jh gm_f_decompose_c1606vm(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_digit_char_ie4khce(uint8_t a0, uint8_t a1);
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_expand_7rpkqt1(uint64_t a0, int32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_put_u64_ant0p2p(uint64_t a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_round_digits_4thelh5(gm_b16_8 a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a(const void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a_c(gm_cs2rc1d61u3qan a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_sci_digits_0sasrt8(void *gm_out, double a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_cs2rc1d61u3qan gm_f_sci_digits_0sasrt8_c(double a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_special_meo3u57(void *gm_out, double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_special_meo3u57_c(double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_to_front_b98pl8o(gm_b16_8 a0, uint64_t a1, uint64_t a2);
extern const uint8_t gm_sa954a25u62up[46];
extern const uint8_t gm_scdb74to7b5sl[39];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sl2sr5fl46c73[48];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcrsgf2gf2b1dq[];
inline __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_check_base_ulm9qq6 F;
    uint32_t gm_l0; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint32_t t4 = gm_l0;
        uint8_t t5 = (t4 < ((uint32_t)2ULL));
        if (!t5) {
            uint32_t t6 = gm_l0;
            uint8_t t7 = (t6 > ((uint32_t)36ULL));
            t3 = t7;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_scdb74to7b5sl));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(38));
            gm_b16_8 t10 = t9;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t10) + 8), gm_srcrsgf2gf2b1dq, 94, 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_b16_8 t6 = t5;
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t6)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = gm_l1;
        gm_b16_8 t10 = t9;
        if (GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t8)), GM_LD(void *, ((uint8_t *)&t10)), GM_LD(uintptr_t, ((uint8_t *)&t8) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_at_gkhhkiq F;
    gm_b16_8 gm_l0; /* d */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l2;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (t3 < t4);
        if (!t5) goto ie2;
        {
            uint64_t t7 = gm_l2;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcrsgf2gf2b1dq, 432, 23);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)0ULL);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_buf_len_gor7a4d F;
    uint32_t gm_l0; /* decimals */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)312ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 447, 52);
    uint64_t t5 = r3;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_buf_len_tedn1r4 F;
    uint32_t gm_l0; /* digits */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    uint64_t r3; bool o4 = __builtin_add_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 451, 48);
    uint64_t t5 = r3;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_check_base_ulm9qq6(uint32_t a0);
inline __attribute__((aligned(4))) gm_cseqep6akjq2jh gm_f_decompose_c1606vm(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decompose_c1606vm F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* bits */
    uint64_t gm_l2; /* be */
    uint64_t gm_l3; /* frac */
    GM_STACK_CHECK_SHALLOW(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t2 = gm_l0;
        double t3 = t2;
        uint64_t t4;
        memset(&t4, 0, sizeof t4); memcpy(&t4, ((uint8_t *)&t3), sizeof t4 < 8 ? sizeof t4 : 8);
        gm_l1 = t4;
    }
    {
        uint64_t t5 = gm_l1;
        unsigned __int128 n6 = (unsigned __int128)((uint64_t)52ULL);
        if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_srcrsgf2gf2b1dq, 385, 12);
        uint64_t t7 = (uint64_t)(t5 >> (int)n6);
        uint64_t t8 = (uint64_t)(t7 & ((uint64_t)2047ULL));
        gm_l2 = t8;
    }
    {
        uint64_t t9 = gm_l1;
        uint64_t t10 = (uint64_t)(t9 & ((uint64_t)4503599627370495ULL));
        gm_l3 = t10;
    }
    {
        uint64_t t12 = gm_l2;
        uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
        if (!t13) goto ie11;
        {
            uint64_t t15 = gm_l3;
            gm_t43v1gep754th t16;
            GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t16 + 0), t15);
            GM_ST(int32_t, ((uint8_t *)&t16 + 8), ((int32_t)-1074LL));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        uint64_t t18 = gm_l3;
        unsigned __int128 n19 = (unsigned __int128)((uint64_t)52ULL);
        if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_srcrsgf2gf2b1dq, 388, 29);
        uint64_t r20 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n19);
        bool o21 = (uint64_t)(r20 >> (int)n19) != ((uint64_t)1ULL);
        if (o21) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 388, 29);
        uint64_t t22 = r20;
        uint64_t t23 = (uint64_t)(t18 | t22);
        uint64_t t24 = gm_l2;
        int32_t t25 = (int32_t)t24;
        int32_t r26; bool o27 = __builtin_sub_overflow(t25, ((int32_t)1075LL), &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 388, 42);
        int32_t t28 = r26;
        gm_t43v1gep754th t29;
        GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t23);
        GM_ST(int32_t, ((uint8_t *)&t29 + 8), t28);
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_cseqep6akjq2jh, F.ret);
gm_fprop:
    return GM_CONV(gm_cseqep6akjq2jh, F.ret);
}
extern __attribute__((aligned(4))) uint8_t gm_f_digit_at_gkhhkiq(gm_b16_8 a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_digit_char_ie4khce(uint8_t a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_char_ie4khce F;
    uint8_t gm_l0; /* d */
    uint8_t gm_l1; /* upper */
    GM_STACK_CHECK_SHALLOW(&F, 17u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = gm_l0;
        uint8_t t4 = (t3 < ((uint8_t)10ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = gm_l0;
            uint8_t r7; bool o8 = __builtin_add_overflow(((uint8_t)48ULL), t6, &r7);
            if (o8) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 39, 24);
            uint8_t t9 = r7;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t11;
        uint8_t t13 = gm_l1;
        if (!t13) goto ie12;
        uint8_t t15 = ((uint8_t)65ULL);
        t11 = t15;
        goto ix12;
ie12:;
        uint8_t t17 = ((uint8_t)97ULL);
        t11 = t17;
ix12:;
        uint8_t t18 = gm_l0;
        uint8_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint8_t)10ULL), &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 40, 47);
        uint8_t t21 = r19;
        uint8_t r22; bool o23 = __builtin_add_overflow(t11, t21, &r22);
        if (o23) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 40, 12);
        uint8_t t24 = r22;
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_expand_7rpkqt1(uint64_t a0, int32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_expand_7rpkqt1 F;
    uint64_t gm_l0; /* m0 */
    int32_t gm_l1; /* e0 */
    gm_b16_8 gm_l2; /* d */
    uint64_t gm_l3; /* m */
    int32_t gm_l4; /* e */
    uint64_t gm_l6; /* q */
    uint32_t gm_l7; /* s */
    uint64_t gm_l8; /* w0 */
    uint64_t gm_l9; /* w1 */
    uint64_t gm_l10; /* top */
    uint64_t gm_l11; /* count */
    uint64_t gm_l12; /* rem */
    uint64_t gm_l13; /* i */
    uint64_t gm_l14; /* cur */
    uint64_t gm_l15; /* k */
    uint64_t gm_l16; /* i */
    uint64_t gm_l17; /* j */
    uint8_t gm_l18; /* c */
    uint32_t gm_l19; /* k */
    uint64_t gm_l20; /* point */
    uint64_t gm_l21; /* n */
    uint64_t gm_l22; /* q */
    uint32_t gm_l23; /* s */
    uint64_t gm_l25; /* frac */
    uint64_t gm_l26; /* lowest */
    uint64_t gm_l27; /* carry */
    uint64_t gm_l28; /* i */
    uint64_t gm_l29; /* cur */
    uint64_t gm_l30; /* window */
    uint8_t gm_l31; /* any */
    GM_STACK_CHECK_SHALLOW(&F, 2154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l0;
        gm_l3 = t2;
    }
    {
        int32_t t3 = gm_l1;
        gm_l4 = t3;
    }
    {
        uint64_t t5 = gm_l3;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t2lqu8jsj511d t8;
            GM_ST(uint64_t, ((uint8_t *)&t8 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t8 + 8), ((uint64_t)0ULL));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        for (;;) {
            int32_t t11 = gm_l4;
            uint8_t t12 = (t11 < ((int32_t)0LL));
            if (!t12) goto lb10;
            uint64_t t13 = gm_l3;
            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 290, 21);
            uint64_t t14 = (uint64_t)(t13 % ((uint64_t)2ULL));
            uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
            if (!t15) goto lb10;
            {
                uint64_t t18 = gm_l3;
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 291, 9);
                uint64_t t19 = (uint64_t)(t18 / ((uint64_t)2ULL));
                gm_l3 = t19;
            }
            {
                int32_t t20 = gm_l4;
                int32_t r21; bool o22 = __builtin_add_overflow(t20, ((int32_t)1LL), &r21);
                if (o22) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 292, 9);
                int32_t t23 = r21;
                gm_l4 = t23;
            }
lc10:;
        }
lb10:;
    }
    {
        int32_t t25 = gm_l4;
        uint8_t t26 = (t25 >= ((int32_t)0LL));
        if (!t26) goto ie24;
        {
            gm_t602t587brq3n t28;
            for (uint64_t i = 0; i < 40; i++) GM_ST(uint32_t, (uint8_t *)&t28 + i * 4, ((uint32_t)0ULL));
            F.l5 = t28;
        }
        {
            int32_t t29 = gm_l4;
            if (((int32_t)32LL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 297, 15);
            if (t29 == ((int32_t)-2147483648LL) && ((int32_t)32LL) == (int32_t)-1) gm_trap("integer overflow in `/`", gm_srcrsgf2gf2b1dq, 297, 15);
            int32_t t30 = (int32_t)(t29 / ((int32_t)32LL));
            uint64_t t31 = (uint64_t)t30;
            gm_l6 = t31;
        }
        {
            int32_t t32 = gm_l4;
            if (((int32_t)32LL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 298, 15);
            if (t32 == ((int32_t)-2147483648LL) && ((int32_t)32LL) == (int32_t)-1) gm_trap("integer overflow in `%`", gm_srcrsgf2gf2b1dq, 298, 15);
            int32_t t33 = (int32_t)(t32 % ((int32_t)32LL));
            uint32_t t34 = (uint32_t)t33;
            gm_l7 = t34;
        }
        {
            uint64_t t35 = gm_l3;
            uint64_t t36 = (uint64_t)(t35 & ((uint64_t)4294967295ULL));
            uint32_t t37 = gm_l7;
            unsigned __int128 n38 = (unsigned __int128)t37;
            if (n38 >= 64) gm_trap_shift((uint64_t)n38, "u64", gm_srcrsgf2gf2b1dq, 299, 15);
            uint64_t r39 = (uint64_t)((uint64_t)t36 << (int)n38);
            bool o40 = (uint64_t)(r39 >> (int)n38) != t36;
            if (o40) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 299, 15);
            uint64_t t41 = r39;
            gm_l8 = t41;
        }
        {
            uint64_t t42 = gm_l6;
            uint64_t t43 = (uintptr_t)t42;
            if (t43 >= (uintptr_t)(40)) gm_trap_index(t43, 40, gm_srcrsgf2gf2b1dq, 300, 9);
            uint64_t t44 = gm_l8;
            uint64_t t45 = (uint64_t)(t44 & ((uint64_t)4294967295ULL));
            uint32_t t46 = (uint32_t)t45;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + t43 * 4), t46);
        }
        {
            uint64_t t47 = gm_l3;
            unsigned __int128 n48 = (unsigned __int128)((uint64_t)32ULL);
            if (n48 >= 64) gm_trap_shift((uint64_t)n48, "u64", gm_srcrsgf2gf2b1dq, 301, 17);
            uint64_t t49 = (uint64_t)(t47 >> (int)n48);
            uint32_t t50 = gm_l7;
            unsigned __int128 n51 = (unsigned __int128)t50;
            if (n51 >= 64) gm_trap_shift((uint64_t)n51, "u64", gm_srcrsgf2gf2b1dq, 301, 16);
            uint64_t r52 = (uint64_t)((uint64_t)t49 << (int)n51);
            bool o53 = (uint64_t)(r52 >> (int)n51) != t49;
            if (o53) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 301, 16);
            uint64_t t54 = r52;
            uint64_t t55 = gm_l8;
            unsigned __int128 n56 = (unsigned __int128)((uint64_t)32ULL);
            if (n56 >= 64) gm_trap_shift((uint64_t)n56, "u64", gm_srcrsgf2gf2b1dq, 301, 35);
            uint64_t t57 = (uint64_t)(t55 >> (int)n56);
            uint64_t r58; bool o59 = __builtin_add_overflow(t54, t57, &r58);
            if (o59) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 301, 15);
            uint64_t t60 = r58;
            gm_l9 = t60;
        }
        {
            uint64_t t61 = gm_l6;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            if (o63) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 302, 15);
            uint64_t t64 = r62;
            uint64_t t65 = (uintptr_t)t64;
            if (t65 >= (uintptr_t)(40)) gm_trap_index(t65, 40, gm_srcrsgf2gf2b1dq, 302, 9);
            uint64_t t66 = gm_l9;
            uint64_t t67 = (uint64_t)(t66 & ((uint64_t)4294967295ULL));
            uint32_t t68 = (uint32_t)t67;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + t65 * 4), t68);
        }
        {
            uint64_t t69 = gm_l6;
            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)2ULL), &r70);
            if (o71) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 303, 15);
            uint64_t t72 = r70;
            uint64_t t73 = (uintptr_t)t72;
            if (t73 >= (uintptr_t)(40)) gm_trap_index(t73, 40, gm_srcrsgf2gf2b1dq, 303, 9);
            uint64_t t74 = gm_l9;
            unsigned __int128 n75 = (unsigned __int128)((uint64_t)32ULL);
            if (n75 >= 64) gm_trap_shift((uint64_t)n75, "u64", gm_srcrsgf2gf2b1dq, 303, 25);
            uint64_t t76 = (uint64_t)(t74 >> (int)n75);
            uint32_t t77 = (uint32_t)t76;
            GM_ST(uint32_t, (((uint8_t *)&F.l5) + t73 * 4), t77);
        }
        {
            uint64_t t78 = gm_l6;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)3ULL), &r79);
            if (o80) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 304, 16);
            uint64_t t81 = r79;
            gm_l10 = t81;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t83 = gm_l10;
                uint8_t t84 = (t83 > ((uint64_t)0ULL));
                if (!t84) goto lb82;
                {
                    gm_l12 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t87 = gm_l10;
                    gm_l13 = t87;
                }
                {
                    for (;;) {
                        uint64_t t89 = gm_l13;
                        uint8_t t90 = (t89 > ((uint64_t)0ULL));
                        if (!t90) goto lb88;
                        {
                            uint64_t t93 = gm_l13;
                            uint64_t r94; bool o95 = __builtin_sub_overflow(t93, ((uint64_t)1ULL), &r94);
                            if (o95) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 310, 17);
                            uint64_t t96 = r94;
                            gm_l13 = t96;
                        }
                        {
                            uint64_t t97 = gm_l12;
                            unsigned __int128 n98 = (unsigned __int128)((uint64_t)32ULL);
                            if (n98 >= 64) gm_trap_shift((uint64_t)n98, "u64", gm_srcrsgf2gf2b1dq, 311, 25);
                            uint64_t r99 = (uint64_t)((uint64_t)t97 << (int)n98);
                            bool o100 = (uint64_t)(r99 >> (int)n98) != t97;
                            if (o100) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 311, 25);
                            uint64_t t101 = r99;
                            uint64_t t102 = gm_l13;
                            uint64_t t103 = (uintptr_t)t102;
                            if (t103 >= (uintptr_t)(40)) gm_trap_index(t103, 40, gm_srcrsgf2gf2b1dq, 311, 42);
                            uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + t103 * 4));
                            uint64_t t105 = (uint64_t)t104;
                            uint64_t t106 = (uint64_t)(t101 | t105);
                            gm_l14 = t106;
                        }
                        {
                            uint64_t t107 = gm_l13;
                            uint64_t t108 = (uintptr_t)t107;
                            if (t108 >= (uintptr_t)(40)) gm_trap_index(t108, 40, gm_srcrsgf2gf2b1dq, 312, 17);
                            uint64_t t109 = gm_l14;
                            if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 312, 29);
                            uint64_t t110 = (uint64_t)(t109 / ((uint64_t)1000000000ULL));
                            uint32_t t111 = (uint32_t)t110;
                            GM_ST(uint32_t, (((uint8_t *)&F.l5) + t108 * 4), t111);
                        }
                        {
                            uint64_t t112 = gm_l14;
                            if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 313, 23);
                            uint64_t t113 = (uint64_t)(t112 % ((uint64_t)1000000000ULL));
                            gm_l12 = t113;
                        }
lc88:;
                    }
lb88:;
                }
                {
                    for (;;) {
                        uint64_t t115 = gm_l10;
                        uint8_t t116 = (t115 > ((uint64_t)0ULL));
                        if (!t116) goto lb114;
                        uint64_t t117 = gm_l10;
                        uint64_t r118; bool o119 = __builtin_sub_overflow(t117, ((uint64_t)1ULL), &r118);
                        if (o119) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 315, 37);
                        uint64_t t120 = r118;
                        uint64_t t121 = (uintptr_t)t120;
                        if (t121 >= (uintptr_t)(40)) gm_trap_index(t121, 40, gm_srcrsgf2gf2b1dq, 315, 31);
                        uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + t121 * 4));
                        uint8_t t123 = ((t122 == ((uint32_t)0ULL)));
                        if (!t123) goto lb114;
                        {
                            uint64_t t126 = gm_l10;
                            uint64_t r127; bool o128 = __builtin_sub_overflow(t126, ((uint64_t)1ULL), &r127);
                            if (o128) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 315, 53);
                            uint64_t t129 = r127;
                            gm_l10 = t129;
                        }
lc114:;
                    }
lb114:;
                }
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t131 = gm_l15;
                        uint8_t t132 = (t131 < ((uint64_t)9ULL));
                        if (!t132) goto lb130;
                        uint8_t t133 = 0;
                        uint64_t t134 = gm_l10;
                        uint8_t t135 = (t134 > ((uint64_t)0ULL));
                        if (!t135) {
                            uint64_t t136 = gm_l12;
                            uint8_t t137 = (!(t136 == ((uint64_t)0ULL)));
                            t133 = t137;
                        } else t133 = 1;
                        if (!t133) goto lb130;
                        {
                            uint64_t t140 = gm_l11;
                            uint64_t t141 = (uintptr_t)t140;
                            uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_srcrsgf2gf2b1dq, 319, 17);
                            uint64_t t143 = gm_l12;
                            if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 319, 29);
                            uint64_t t144 = (uint64_t)(t143 % ((uint64_t)10ULL));
                            uint8_t t145 = (uint8_t)t144;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t141 * 1), t145);
                        }
                        {
                            uint64_t t146 = gm_l12;
                            if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 320, 17);
                            uint64_t t147 = (uint64_t)(t146 / ((uint64_t)10ULL));
                            gm_l12 = t147;
                        }
                        {
                            uint64_t t148 = gm_l11;
                            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                            if (o150) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 321, 17);
                            uint64_t t151 = r149;
                            gm_l11 = t151;
                        }
                        {
                            uint64_t t152 = gm_l15;
                            uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                            if (o154) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 322, 17);
                            uint64_t t155 = r153;
                            gm_l15 = t155;
                        }
lc130:;
                    }
lb130:;
                }
lc82:;
            }
lb82:;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t156 = gm_l11;
            gm_l17 = t156;
        }
        {
            for (;;) {
                uint64_t t158 = gm_l16;
                uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                if (o160) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 327, 15);
                uint64_t t161 = r159;
                uint64_t t162 = gm_l17;
                uint8_t t163 = (t161 < t162);
                if (!t163) goto lb157;
                {
                    uint64_t t166 = gm_l17;
                    uint64_t r167; bool o168 = __builtin_sub_overflow(t166, ((uint64_t)1ULL), &r167);
                    if (o168) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 328, 13);
                    uint64_t t169 = r167;
                    gm_l17 = t169;
                }
                {
                    uint64_t t170 = gm_l16;
                    uint64_t t171 = (uintptr_t)t170;
                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t171 >= (uintptr_t)(t172)) gm_trap_index(t171, t172, gm_srcrsgf2gf2b1dq, 329, 18);
                    uint8_t t173 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t171 * 1));
                    gm_l18 = t173;
                }
                {
                    uint64_t t174 = gm_l16;
                    uint64_t t175 = (uintptr_t)t174;
                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srcrsgf2gf2b1dq, 330, 13);
                    uint64_t t177 = gm_l17;
                    uint64_t t178 = (uintptr_t)t177;
                    uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t178 >= (uintptr_t)(t179)) gm_trap_index(t178, t179, gm_srcrsgf2gf2b1dq, 330, 20);
                    uint8_t t180 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t178 * 1));
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t175 * 1), t180);
                }
                {
                    uint64_t t181 = gm_l17;
                    uint64_t t182 = (uintptr_t)t181;
                    uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t182 >= (uintptr_t)(t183)) gm_trap_index(t182, t183, gm_srcrsgf2gf2b1dq, 331, 13);
                    uint8_t t184 = gm_l18;
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t182 * 1), t184);
                }
                {
                    uint64_t t185 = gm_l16;
                    uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                    if (o187) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 332, 13);
                    uint64_t t188 = r186;
                    gm_l16 = t188;
                }
lc157:;
            }
lb157:;
        }
        {
            uint64_t t189 = gm_l11;
            uint64_t t190 = gm_l11;
            gm_t2lqu8jsj511d t191;
            GM_ST(uint64_t, ((uint8_t *)&t191 + 0), t189);
            GM_ST(uint64_t, ((uint8_t *)&t191 + 8), t190);
            uint8_t t192 = 0;
            F.ret = t191;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        int32_t t193 = gm_l4;
        int32_t r194; bool o195 = __builtin_sub_overflow(((int32_t)0LL), t193, &r194);
        if (o195) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 337, 11);
        int32_t t196 = r194;
        uint32_t t197 = (uint32_t)t196;
        gm_l19 = t197;
    }
    {
        gm_l20 = ((uint64_t)0ULL);
    }
    {
        uint32_t t199 = gm_l19;
        uint8_t t200 = (t199 < ((uint32_t)64ULL));
        if (!t200) goto ie198;
        {
            uint64_t t202 = gm_l3;
            uint32_t t203 = gm_l19;
            unsigned __int128 n204 = (unsigned __int128)t203;
            if (n204 >= 64) gm_trap_shift((uint64_t)n204, "u64", gm_srcrsgf2gf2b1dq, 339, 33);
            uint64_t t205 = (uint64_t)(t202 >> (int)n204);
            gm_b16_8 t206 = gm_l2;
            uint64_t t207 = gm_f_put_u64_ant0p2p(t205, t206, ((uint64_t)0ULL));
            gm_l20 = t207;
        }
        goto ix198;
ie198:;
ix198:;
    }
    {
        uint64_t t208 = gm_l20;
        gm_l21 = t208;
    }
    {
        uint32_t t209 = gm_l19;
        if (((uint32_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 343, 11);
        uint32_t t210 = (uint32_t)(t209 / ((uint32_t)32ULL));
        uint64_t t211 = (uint64_t)t210;
        gm_l22 = t211;
    }
    {
        uint32_t t212 = gm_l19;
        if (((uint32_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 344, 10);
        uint32_t t213 = (uint32_t)(t212 % ((uint32_t)32ULL));
        gm_l23 = t213;
    }
    {
        gm_tss0v68n9gqca t214;
        for (uint64_t i = 0; i < 37; i++) GM_ST(uint32_t, (uint8_t *)&t214 + i * 4, ((uint32_t)0ULL));
        F.l24 = t214;
    }
    {
        uint64_t t215;
        uint32_t t217 = gm_l19;
        uint8_t t218 = (t217 >= ((uint32_t)64ULL));
        if (!t218) goto ie216;
        uint64_t t220 = gm_l3;
        uint64_t t221 = t220;
        t215 = t221;
        goto ix216;
ie216:;
        uint64_t t223 = gm_l3;
        uint32_t t224 = gm_l19;
        unsigned __int128 n225 = (unsigned __int128)t224;
        if (n225 >= 64) gm_trap_shift((uint64_t)n225, "u64", gm_srcrsgf2gf2b1dq, 346, 43);
        uint64_t r226 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n225);
        bool o227 = (uint64_t)(r226 >> (int)n225) != ((uint64_t)1ULL);
        if (o227) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 346, 43);
        uint64_t t228 = r226;
        uint64_t r229; bool o230 = __builtin_sub_overflow(t228, ((uint64_t)1ULL), &r229);
        if (o230) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 346, 42);
        uint64_t t231 = r229;
        uint64_t t232 = (uint64_t)(t223 & t231);
        uint64_t t233 = t232;
        t215 = t233;
ix216:;
        gm_l25 = t215;
    }
    {
        uint64_t t234 = (uintptr_t)((uint64_t)0ULL);
        if (t234 >= (uintptr_t)(37)) gm_trap_index(t234, 37, gm_srcrsgf2gf2b1dq, 347, 5);
        uint64_t t235 = gm_l25;
        uint64_t t236 = (uint64_t)(t235 & ((uint64_t)4294967295ULL));
        uint32_t t237 = (uint32_t)t236;
        GM_ST(uint32_t, (((uint8_t *)&F.l24) + t234 * 4), t237);
    }
    {
        uint64_t t238 = (uintptr_t)((uint64_t)1ULL);
        if (t238 >= (uintptr_t)(37)) gm_trap_index(t238, 37, gm_srcrsgf2gf2b1dq, 348, 5);
        uint64_t t239 = gm_l25;
        unsigned __int128 n240 = (unsigned __int128)((uint64_t)32ULL);
        if (n240 >= 64) gm_trap_shift((uint64_t)n240, "u64", gm_srcrsgf2gf2b1dq, 348, 17);
        uint64_t t241 = (uint64_t)(t239 >> (int)n240);
        uint32_t t242 = (uint32_t)t241;
        GM_ST(uint32_t, (((uint8_t *)&F.l24) + t238 * 4), t242);
    }
    {
        gm_l26 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb243;
            {
                gm_l27 = ((uint64_t)0ULL);
            }
            {
                uint64_t t246 = gm_l26;
                gm_l28 = t246;
            }
            {
                for (;;) {
                    uint64_t t248 = gm_l28;
                    uint64_t t249 = gm_l22;
                    uint8_t t250 = (t248 <= t249);
                    if (!t250) goto lb247;
                    {
                        uint64_t t253 = gm_l28;
                        uint64_t t254 = (uintptr_t)t253;
                        if (t254 >= (uintptr_t)(37)) gm_trap_index(t254, 37, gm_srcrsgf2gf2b1dq, 354, 24);
                        uint32_t t255 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + t254 * 4));
                        uint64_t t256 = (uint64_t)t255;
                        uint64_t r257; bool o258 = __builtin_mul_overflow(t256, ((uint64_t)10ULL), &r257);
                        if (o258) gm_trap("integer overflow in `*`", gm_srcrsgf2gf2b1dq, 354, 20);
                        uint64_t t259 = r257;
                        uint64_t t260 = gm_l27;
                        uint64_t r261; bool o262 = __builtin_add_overflow(t259, t260, &r261);
                        if (o262) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 354, 20);
                        uint64_t t263 = r261;
                        gm_l29 = t263;
                    }
                    {
                        uint64_t t264 = gm_l28;
                        uint64_t t265 = (uintptr_t)t264;
                        if (t265 >= (uintptr_t)(37)) gm_trap_index(t265, 37, gm_srcrsgf2gf2b1dq, 355, 13);
                        uint64_t t266 = gm_l29;
                        uint64_t t267 = (uint64_t)(t266 & ((uint64_t)4294967295ULL));
                        uint32_t t268 = (uint32_t)t267;
                        GM_ST(uint32_t, (((uint8_t *)&F.l24) + t265 * 4), t268);
                    }
                    {
                        uint64_t t269 = gm_l29;
                        unsigned __int128 n270 = (unsigned __int128)((uint64_t)32ULL);
                        if (n270 >= 64) gm_trap_shift((uint64_t)n270, "u64", gm_srcrsgf2gf2b1dq, 356, 21);
                        uint64_t t271 = (uint64_t)(t269 >> (int)n270);
                        gm_l27 = t271;
                    }
                    {
                        uint64_t t272 = gm_l28;
                        uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                        if (o274) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 357, 13);
                        uint64_t t275 = r273;
                        gm_l28 = t275;
                    }
lc247:;
                }
lb247:;
            }
            {
                uint64_t t276 = gm_l22;
                uint64_t r277; bool o278 = __builtin_add_overflow(t276, ((uint64_t)1ULL), &r277);
                if (o278) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 359, 15);
                uint64_t t279 = r277;
                uint64_t t280 = (uintptr_t)t279;
                if (t280 >= (uintptr_t)(37)) gm_trap_index(t280, 37, gm_srcrsgf2gf2b1dq, 359, 9);
                uint64_t t281 = gm_l27;
                uint32_t t282 = (uint32_t)t281;
                GM_ST(uint32_t, (((uint8_t *)&F.l24) + t280 * 4), t282);
            }
            {
                uint64_t t283 = gm_l22;
                uint64_t t284 = (uintptr_t)t283;
                if (t284 >= (uintptr_t)(37)) gm_trap_index(t284, 37, gm_srcrsgf2gf2b1dq, 360, 23);
                uint32_t t285 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + t284 * 4));
                uint64_t t286 = (uint64_t)t285;
                uint64_t t287 = gm_l22;
                uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                if (o289) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 360, 46);
                uint64_t t290 = r288;
                uint64_t t291 = (uintptr_t)t290;
                if (t291 >= (uintptr_t)(37)) gm_trap_index(t291, 37, gm_srcrsgf2gf2b1dq, 360, 40);
                uint32_t t292 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + t291 * 4));
                uint64_t t293 = (uint64_t)t292;
                unsigned __int128 n294 = (unsigned __int128)((uint64_t)32ULL);
                if (n294 >= 64) gm_trap_shift((uint64_t)n294, "u64", gm_srcrsgf2gf2b1dq, 360, 36);
                uint64_t r295 = (uint64_t)((uint64_t)t293 << (int)n294);
                bool o296 = (uint64_t)(r295 >> (int)n294) != t293;
                if (o296) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 360, 36);
                uint64_t t297 = r295;
                uint64_t t298 = (uint64_t)(t286 | t297);
                gm_l30 = t298;
            }
            {
                uint64_t t299 = gm_l21;
                uint64_t t300 = (uintptr_t)t299;
                uint64_t t301 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t300 >= (uintptr_t)(t301)) gm_trap_index(t300, t301, gm_srcrsgf2gf2b1dq, 361, 9);
                uint64_t t302 = gm_l30;
                uint32_t t303 = gm_l23;
                unsigned __int128 n304 = (unsigned __int128)t303;
                if (n304 >= 64) gm_trap_shift((uint64_t)n304, "u64", gm_srcrsgf2gf2b1dq, 361, 17);
                uint64_t t305 = (uint64_t)(t302 >> (int)n304);
                uint8_t t306 = (uint8_t)t305;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t300 * 1), t306);
            }
            {
                uint64_t t307 = gm_l21;
                uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                if (o309) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 362, 9);
                uint64_t t310 = r308;
                gm_l21 = t310;
            }
            {
                uint64_t t311 = gm_l22;
                uint64_t t312 = (uintptr_t)t311;
                if (t312 >= (uintptr_t)(37)) gm_trap_index(t312, 37, gm_srcrsgf2gf2b1dq, 363, 9);
                uint64_t t313 = gm_l22;
                uint64_t t314 = (uintptr_t)t313;
                if (t314 >= (uintptr_t)(37)) gm_trap_index(t314, 37, gm_srcrsgf2gf2b1dq, 363, 20);
                uint32_t t315 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + t314 * 4));
                uint32_t t316 = gm_l23;
                unsigned __int128 n317 = (unsigned __int128)t316;
                if (n317 >= 32) gm_trap_shift((uint64_t)n317, "u32", gm_srcrsgf2gf2b1dq, 363, 33);
                uint32_t r318 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n317);
                bool o319 = (uint32_t)(r318 >> (int)n317) != ((uint32_t)1ULL);
                if (o319) gm_trap("integer overflow in `<<`", gm_srcrsgf2gf2b1dq, 363, 33);
                uint32_t t320 = r318;
                uint32_t r321; bool o322 = __builtin_sub_overflow(t320, ((uint32_t)1ULL), &r321);
                if (o322) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 363, 32);
                uint32_t t323 = r321;
                uint32_t t324 = (uint32_t)(t315 & t323);
                GM_ST(uint32_t, (((uint8_t *)&F.l24) + t312 * 4), t324);
            }
            {
                uint64_t t325 = gm_l22;
                uint64_t r326; bool o327 = __builtin_add_overflow(t325, ((uint64_t)1ULL), &r326);
                if (o327) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 364, 15);
                uint64_t t328 = r326;
                uint64_t t329 = (uintptr_t)t328;
                if (t329 >= (uintptr_t)(37)) gm_trap_index(t329, 37, gm_srcrsgf2gf2b1dq, 364, 9);
                GM_ST(uint32_t, (((uint8_t *)&F.l24) + t329 * 4), ((uint32_t)0ULL));
            }
            {
                gm_l31 = ((uint8_t)0);
            }
            {
                uint64_t t330 = gm_l26;
                gm_l28 = t330;
            }
            {
                for (;;) {
                    uint64_t t332 = gm_l28;
                    uint64_t t333 = gm_l22;
                    uint8_t t334 = (t332 <= t333);
                    if (!t334) goto lb331;
                    {
                        uint64_t t338 = gm_l28;
                        uint64_t t339 = (uintptr_t)t338;
                        if (t339 >= (uintptr_t)(37)) gm_trap_index(t339, 37, gm_srcrsgf2gf2b1dq, 369, 16);
                        uint32_t t340 = GM_LD(uint32_t, (((uint8_t *)&F.l24) + t339 * 4));
                        uint8_t t341 = (!(t340 == ((uint32_t)0ULL)));
                        if (!t341) goto ie337;
                        {
                            gm_l31 = ((uint8_t)1);
                        }
                        {
                            goto lb331;
                        }
                        goto ix337;
ie337:;
ix337:;
                    }
                    {
                        uint64_t t343 = gm_l28;
                        uint64_t r344; bool o345 = __builtin_add_overflow(t343, ((uint64_t)1ULL), &r344);
                        if (o345) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 373, 13);
                        uint64_t t346 = r344;
                        gm_l28 = t346;
                    }
lc331:;
                }
lb331:;
            }
            {
                uint8_t t348 = gm_l31;
                uint8_t t349 = (uint8_t)!t348;
                if (!t349) goto ie347;
                {
                    goto lb243;
                }
                goto ix347;
ie347:;
ix347:;
            }
            {
                uint64_t t351 = gm_l21;
                uint64_t t352 = gm_l20;
                uint64_t r353; bool o354 = __builtin_sub_overflow(t351, t352, &r353);
                if (o354) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 377, 19);
                uint64_t t355 = r353;
                if (((uint64_t)32ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 377, 18);
                uint64_t t356 = (uint64_t)(t355 / ((uint64_t)32ULL));
                gm_l26 = t356;
            }
lc243:;
        }
lb243:;
    }
    {
        uint64_t t357 = gm_l21;
        uint64_t t358 = gm_l20;
        gm_t2lqu8jsj511d t359;
        GM_ST(uint64_t, ((uint8_t *)&t359 + 0), t357);
        GM_ST(uint64_t, ((uint8_t *)&t359 + 8), t358);
        uint8_t t360 = 0;
        F.ret = t359;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
extern __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_rppim5r F;
    double gm_l0; /* x */
    uint32_t gm_l1; /* decimals */
    gm_b16_8 gm_l2; /* buf */
    gm_b16_8 gm_l3; /* sp */
    gm_t43v1gep754th gm_l5; /* parts */
    gm_t2lqu8jsj511d gm_l6; /* ex */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* point */
    uint64_t gm_l9; /* places */
    uint8_t gm_l10; /* carry */
    uint8_t gm_l11; /* neg */
    uint64_t gm_l12; /* int_digits */
    uint64_t gm_l13; /* total */
    uint64_t gm_l14; /* out */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 2367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        double t3 = gm_l0;
        gm_b16_8 t4 = gm_l2;
        gm_th2lqbo77pvsc t5;
        gm_f_special_meo3u57((void *)&t5, t3, t4);
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t91rshbm1iu79 t11;
        for (uint64_t i = 0; i < 1100; i++) GM_ST(uint8_t, (uint8_t *)&t11 + i * 1, ((uint8_t)0ULL));
        F.l4 = t11;
    }
    {
        double t12 = gm_l0;
        gm_t43v1gep754th t13 = GM_CONV(gm_t43v1gep754th, gm_f_decompose_c1606vm(t12));
        gm_l5 = t13;
    }
    {
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        int32_t t15 = GM_LD(int32_t, (((uint8_t *)&gm_l5) + 8));
        void * t16 = ((uint8_t *)&F.l4);
        if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 469, 36);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t16 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
        gm_t2lqu8jsj511d t18 = GM_CONV(gm_t2lqu8jsj511d, gm_f_expand_7rpkqt1(t14, t15, t17));
        gm_l6 = t18;
    }
    {
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
        gm_l7 = t19;
    }
    {
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        gm_l8 = t20;
    }
    {
        uint32_t t21 = gm_l1;
        uint64_t t22 = (uint64_t)t21;
        gm_l9 = t22;
    }
    {
        void * t23 = ((uint8_t *)&F.l4);
        if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 473, 27);
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t23 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
        uint64_t t25 = gm_l7;
        uint64_t t26 = gm_l8;
        uint64_t t27 = gm_l9;
        uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 473, 37);
        uint64_t t30 = r28;
        uint8_t t31 = gm_f_round_digits_4thelh5(t24, t25, t30);
        gm_l10 = t31;
    }
    {
        double t32 = gm_l0;
        uint8_t t33 = (uint8_t)(__builtin_signbit(t32) != 0);
        gm_l11 = t33;
    }
    {
        uint64_t t34;
        uint8_t t36 = gm_l10;
        if (!t36) goto ie35;
        uint64_t t38 = gm_l8;
        uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
        if (o40) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 475, 30);
        uint64_t t41 = r39;
        uint64_t t42 = t41;
        t34 = t42;
        goto ix35;
ie35:;
        uint64_t t43;
        uint64_t t45 = gm_l8;
        uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
        if (!t46) goto ie44;
        uint64_t t48 = ((uint64_t)1ULL);
        t43 = t48;
        goto ix44;
ie44:;
        uint64_t t50 = gm_l8;
        uint64_t t51 = t50;
        t43 = t51;
ix44:;
        t34 = t43;
ix35:;
        gm_l12 = t34;
    }
    {
        uint64_t t52 = gm_l12;
        gm_l13 = t52;
    }
    {
        uint8_t t54 = gm_l11;
        if (!t54) goto ie53;
        {
            uint64_t t56 = gm_l13;
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            if (o58) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 477, 14);
            uint64_t t59 = r57;
            gm_l13 = t59;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        uint64_t t61 = gm_l9;
        uint8_t t62 = (t61 > ((uint64_t)0ULL));
        if (!t62) goto ie60;
        {
            uint64_t t64 = gm_l13;
            uint64_t t65 = gm_l9;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            if (o67) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 478, 30);
            uint64_t t68 = r66;
            uint64_t r69; bool o70 = __builtin_add_overflow(t64, t68, &r69);
            if (o70) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 478, 21);
            uint64_t t71 = r69;
            gm_l13 = t71;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        uint64_t t73 = gm_l13;
        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t75 = (t73 > t74);
        if (!t75) goto ie72;
        {
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sa954a25u62up));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(45));
            gm_b16_8 t78 = t77;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t78)), GM_LD(uintptr_t, ((uint8_t *)&t78) + 8), gm_srcrsgf2gf2b1dq, 479, 26);
        }
        goto ix72;
ie72:;
ix72:;
    }
    {
        gm_l14 = ((uint64_t)0ULL);
    }
    {
        uint8_t t80 = gm_l11;
        if (!t80) goto ie79;
        {
            uint64_t t82 = gm_l14;
            uint64_t t83 = (uintptr_t)t82;
            uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcrsgf2gf2b1dq, 482, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t83 * 1), ((uint8_t)45ULL));
        }
        {
            uint64_t t85 = gm_l14;
            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
            if (o87) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 483, 9);
            uint64_t t88 = r86;
            gm_l14 = t88;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        uint8_t t90 = gm_l10;
        if (!t90) goto ie89;
        {
            uint64_t t92 = gm_l14;
            uint64_t t93 = (uintptr_t)t92;
            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_srcrsgf2gf2b1dq, 486, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t93 * 1), ((uint8_t)49ULL));
        }
        {
            uint64_t t95 = gm_l14;
            uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
            if (o97) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 487, 9);
            uint64_t t98 = r96;
            gm_l14 = t98;
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        uint64_t t100 = gm_l8;
        uint8_t t101 = ((t100 == ((uint64_t)0ULL)));
        if (!t101) goto ie99;
        uint8_t t102 = gm_l10;
        uint8_t t103 = (uint8_t)!t102;
        if (!t103) goto ie99;
        {
            uint64_t t105 = gm_l14;
            uint64_t t106 = (uintptr_t)t105;
            uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcrsgf2gf2b1dq, 490, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t106 * 1), ((uint8_t)48ULL));
        }
        {
            uint64_t t108 = gm_l14;
            uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
            if (o110) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 491, 9);
            uint64_t t111 = r109;
            gm_l14 = t111;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            uint64_t t113 = gm_l8;
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
                    uint64_t t121 = t120;
                    uint64_t t124 = GM_LD(uint64_t, ((uint8_t *)&t121));
                    gm_l17 = t124;
                    {
                        uint64_t t126 = gm_l14;
                        uint64_t t127 = (uintptr_t)t126;
                        uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcrsgf2gf2b1dq, 494, 9);
                        uint64_t t129 = gm_l17;
                        uint64_t t130 = (uintptr_t)t129;
                        if (t130 >= (uintptr_t)(1100)) gm_trap_index(t130, 1100, gm_srcrsgf2gf2b1dq, 494, 26);
                        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + t130 * 1));
                        uint8_t r132; bool o133 = __builtin_add_overflow(((uint8_t)48ULL), t131, &r132);
                        if (o133) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 494, 20);
                        uint8_t t134 = r132;
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t127 * 1), t134);
                    }
                    {
                        uint64_t t135 = gm_l14;
                        uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                        if (o137) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 495, 9);
                        uint64_t t138 = r136;
                        gm_l14 = t138;
                    }
                    goto me122;
mn123:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 493, 5);
me122:;
                }
lc114:;
                {
                    uint64_t t140 = gm_l16;
                    uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                    uint64_t t143 = r141;
                    gm_l16 = t143;
                }
            }
lb114:;
        }
    }
    {
        uint64_t t145 = gm_l9;
        uint8_t t146 = (t145 > ((uint64_t)0ULL));
        if (!t146) goto ie144;
        {
            uint64_t t148 = gm_l14;
            uint64_t t149 = (uintptr_t)t148;
            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t149 >= (uintptr_t)(t150)) gm_trap_index(t149, t150, gm_srcrsgf2gf2b1dq, 498, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t149 * 1), ((uint8_t)46ULL));
        }
        {
            uint64_t t151 = gm_l14;
            uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
            if (o153) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 499, 9);
            uint64_t t154 = r152;
            gm_l14 = t154;
        }
        {
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                uint64_t t156 = gm_l9;
                gm_l18 = t156;
            }
            {
                for (;;) {
                    uint64_t t158 = gm_l19;
                    uint64_t t159 = gm_l18;
                    uint8_t t160 = (t158 < t159);
                    if (!t160) goto lb157;
                    {
                        uint64_t t163 = gm_l19;
                        uint64_t t164 = t163;
                        uint64_t t167 = GM_LD(uint64_t, ((uint8_t *)&t164));
                        gm_l20 = t167;
                        {
                            uint64_t t169 = gm_l14;
                            uint64_t t170 = (uintptr_t)t169;
                            uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_srcrsgf2gf2b1dq, 501, 13);
                            uint8_t *t172 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t170 * 1);
                            void * t173 = ((uint8_t *)&F.l4);
                            if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 501, 39);
                            gm_b16_8 t174;
                            GM_ST(void *, (uint8_t *)&t174, (void *)(((uint8_t *)t173 + (uintptr_t)0 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
                            gm_b16_8 t175 = t174;
                            gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                            uint64_t t177 = gm_l7;
                            uint64_t t178 = gm_l8;
                            uint64_t t179 = gm_l20;
                            uint64_t r180; bool o181 = __builtin_add_overflow(t178, t179, &r180);
                            if (o181) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 501, 49);
                            uint64_t t182 = r180;
                            uint8_t t183 = gm_f_digit_at_gkhhkiq(t176, t177, t182);
                            uint8_t r184; bool o185 = __builtin_add_overflow(((uint8_t)48ULL), t183, &r184);
                            if (o185) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 501, 24);
                            uint8_t t186 = r184;
                            GM_ST(uint8_t, t172, t186);
                        }
                        {
                            uint64_t t187 = gm_l14;
                            uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                            if (o189) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 502, 13);
                            uint64_t t190 = r188;
                            gm_l14 = t190;
                        }
                        goto me165;
mn166:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 500, 9);
me165:;
                    }
lc157:;
                    {
                        uint64_t t192 = gm_l19;
                        uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                        uint64_t t195 = r193;
                        gm_l19 = t195;
                    }
                }
lb157:;
            }
        }
        goto ix144;
ie144:;
ix144:;
    }
    {
        uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t197 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t198 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t199 = gm_l14;
        uint64_t t200 = (uintptr_t)t199;
        if (t198 > t200 || t200 > (uintptr_t)t196) gm_trap_slice(t198, t200, t196, gm_srcrsgf2gf2b1dq, 505, 12);
        gm_b16_8 t201;
        GM_ST(void *, (uint8_t *)&t201, (void *)(((uint8_t *)t197 + t198 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(t200 - t198));
        gm_b16_8 t202 = t201;
        gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
        uint8_t t204 = 0;
        F.ret = t203;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_put_u64_ant0p2p(uint64_t a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_ant0p2p F;
    uint64_t gm_l0; /* v */
    gm_b16_8 gm_l1; /* d */
    uint64_t gm_l2; /* at */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* t */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* j */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = gm_l0;
        gm_l4 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l4;
            uint8_t t5 = (!(t4 == ((uint64_t)0ULL)));
            if (!t5) goto lb3;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = gm_l3;
                uint64_t r10; bool o11 = __builtin_add_overflow(t8, t9, &r10);
                if (o11) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 267, 11);
                uint64_t t12 = r10;
                uint64_t t13 = (uintptr_t)t12;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcrsgf2gf2b1dq, 267, 9);
                uint64_t t15 = gm_l4;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 267, 22);
                uint64_t t16 = (uint64_t)(t15 % ((uint64_t)10ULL));
                uint8_t t17 = (uint8_t)t16;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t13 * 1), t17);
            }
            {
                uint64_t t18 = gm_l4;
                if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 268, 9);
                uint64_t t19 = (uint64_t)(t18 / ((uint64_t)10ULL));
                gm_l4 = t19;
            }
            {
                uint64_t t20 = gm_l3;
                uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 269, 9);
                uint64_t t23 = r21;
                gm_l3 = t23;
            }
lc3:;
        }
lb3:;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        uint64_t t24 = gm_l3;
        gm_l6 = t24;
    }
    {
        for (;;) {
            uint64_t t26 = gm_l5;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            if (o28) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 273, 11);
            uint64_t t29 = r27;
            uint64_t t30 = gm_l6;
            uint8_t t31 = (t29 < t30);
            if (!t31) goto lb25;
            {
                uint64_t t34 = gm_l6;
                uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 274, 9);
                uint64_t t37 = r35;
                gm_l6 = t37;
            }
            {
                uint64_t t38 = gm_l2;
                uint64_t t39 = gm_l5;
                uint64_t r40; bool o41 = __builtin_add_overflow(t38, t39, &r40);
                if (o41) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 275, 16);
                uint64_t t42 = r40;
                uint64_t t43 = (uintptr_t)t42;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcrsgf2gf2b1dq, 275, 14);
                uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t43 * 1));
                gm_l7 = t45;
            }
            {
                uint64_t t46 = gm_l2;
                uint64_t t47 = gm_l5;
                uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 276, 11);
                uint64_t t50 = r48;
                uint64_t t51 = (uintptr_t)t50;
                uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_srcrsgf2gf2b1dq, 276, 9);
                uint64_t t53 = gm_l2;
                uint64_t t54 = gm_l6;
                uint64_t r55; bool o56 = __builtin_add_overflow(t53, t54, &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 276, 23);
                uint64_t t57 = r55;
                uint64_t t58 = (uintptr_t)t57;
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_srcrsgf2gf2b1dq, 276, 21);
                uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t58 * 1));
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t51 * 1), t60);
            }
            {
                uint64_t t61 = gm_l2;
                uint64_t t62 = gm_l6;
                uint64_t r63; bool o64 = __builtin_add_overflow(t61, t62, &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 277, 11);
                uint64_t t65 = r63;
                uint64_t t66 = (uintptr_t)t65;
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcrsgf2gf2b1dq, 277, 9);
                uint8_t t68 = gm_l7;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t66 * 1), t68);
            }
            {
                uint64_t t69 = gm_l5;
                uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                if (o71) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 278, 9);
                uint64_t t72 = r70;
                gm_l5 = t72;
            }
lc25:;
        }
lb25:;
    }
    {
        uint64_t t73 = gm_l3;
        uint8_t t74 = 0;
        F.ret = t73;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_round_digits_4thelh5(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_round_digits_4thelh5 F;
    gm_b16_8 gm_l0; /* d */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* keep */
    uint8_t gm_l3; /* up */
    uint8_t gm_l4; /* next */
    uint8_t gm_l5; /* rest */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l2;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (t3 >= t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        uint64_t t8 = gm_l2;
        uint64_t t9 = (uintptr_t)t8;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srcrsgf2gf2b1dq, 397, 13);
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
        gm_l4 = t11;
    }
    {
        uint8_t t13 = gm_l4;
        uint8_t t14 = (t13 > ((uint8_t)5ULL));
        if (!t14) goto ie12;
        {
            gm_l3 = ((uint8_t)1);
        }
        goto ix12;
ie12:;
        uint8_t t17 = gm_l4;
        uint8_t t18 = ((t17 == ((uint8_t)5ULL)));
        if (!t18) goto ie16;
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            uint64_t t20 = gm_l2;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            if (o22) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 402, 14);
            uint64_t t23 = r21;
            gm_l6 = t23;
        }
        {
            for (;;) {
                uint64_t t25 = gm_l6;
                uint64_t t26 = gm_l1;
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t31 = gm_l6;
                    uint64_t t32 = (uintptr_t)t31;
                    uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcrsgf2gf2b1dq, 404, 16);
                    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t32 * 1));
                    uint8_t t35 = (!(t34 == ((uint8_t)0ULL)));
                    if (!t35) goto ie30;
                    {
                        gm_l5 = ((uint8_t)1);
                    }
                    {
                        goto lb24;
                    }
                    goto ix30;
ie30:;
ix30:;
                }
                {
                    uint64_t t37 = gm_l6;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    if (o39) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 408, 13);
                    uint64_t t40 = r38;
                    gm_l6 = t40;
                }
lc24:;
            }
lb24:;
        }
        {
            uint8_t t42 = gm_l5;
            if (!t42) goto ie41;
            {
                gm_l3 = ((uint8_t)1);
            }
            goto ix41;
ie41:;
            uint64_t t45 = gm_l2;
            uint8_t t46 = (t45 > ((uint64_t)0ULL));
            if (!t46) goto ie44;
            {
                uint64_t t48 = gm_l2;
                uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                if (o50) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 413, 20);
                uint64_t t51 = r49;
                uint64_t t52 = (uintptr_t)t51;
                uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_srcrsgf2gf2b1dq, 413, 18);
                uint8_t t54 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t52 * 1));
                if (((uint8_t)2ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 413, 18);
                uint8_t t55 = (uint8_t)(t54 % ((uint8_t)2ULL));
                uint8_t t56 = ((t55 == ((uint8_t)1ULL)));
                gm_l3 = t56;
            }
            goto ix44;
ie44:;
ix44:;
ix41:;
        }
        goto ix16;
ie16:;
ix16:;
ix12:;
    }
    {
        uint8_t t58 = gm_l3;
        uint8_t t59 = (uint8_t)!t58;
        if (!t59) goto ie57;
        {
            uint8_t t61 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        uint64_t t62 = gm_l2;
        gm_l7 = t62;
    }
    {
        for (;;) {
            uint64_t t64 = gm_l7;
            uint8_t t65 = (t64 > ((uint64_t)0ULL));
            if (!t65) goto lb63;
            {
                uint64_t t68 = gm_l7;
                uint64_t r69; bool o70 = __builtin_sub_overflow(t68, ((uint64_t)1ULL), &r69);
                if (o70) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 419, 9);
                uint64_t t71 = r69;
                gm_l7 = t71;
            }
            {
                uint64_t t73 = gm_l7;
                uint64_t t74 = (uintptr_t)t73;
                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_srcrsgf2gf2b1dq, 420, 12);
                uint8_t t76 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t74 * 1));
                uint8_t t77 = ((t76 == ((uint8_t)9ULL)));
                if (!t77) goto ie72;
                {
                    uint64_t t79 = gm_l7;
                    uint64_t t80 = (uintptr_t)t79;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcrsgf2gf2b1dq, 421, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t80 * 1), ((uint8_t)0ULL));
                }
                goto ix72;
ie72:;
                {
                    {
                        uint64_t t84 = gm_l7;
                        uint64_t t85 = (uintptr_t)t84;
                        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t85 >= (uintptr_t)(t86)) gm_trap_index(t85, t86, gm_srcrsgf2gf2b1dq, 423, 13);
                        void * t87 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t85 * 1);
                        gm_l8 = t87;
                    }
                    {
                        void * t88 = gm_l8;
                        void * t89 = gm_l8;
                        uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)t89));
                        uint8_t r91; bool o92 = __builtin_add_overflow(t90, ((uint8_t)1ULL), &r91);
                        if (o92) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 423, 13);
                        uint8_t t93 = r91;
                        GM_ST(uint8_t, ((uint8_t *)t88), t93);
                    }
                }
                {
                    uint8_t t94 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
ix72:;
            }
lc63:;
        }
lb63:;
    }
    {
        uint8_t t95 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a(const void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_digit_sqvlt0a F;
    gm_t88486mk7vrvo gm_l0; /* sd */
    gm_b16_8 gm_l1; /* d */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 20));
        if (!t3) goto ie2;
        {
            uint8_t t5;
            uint64_t t7 = gm_l2;
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (!t8) goto ie6;
            uint8_t t10 = ((uint8_t)1ULL);
            t5 = t10;
            goto ix6;
ie6:;
            uint8_t t12 = ((uint8_t)0ULL);
            t5 = t12;
ix6:;
            uint8_t t13 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t15 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)a0) + 0));
        uint64_t t17 = (uintptr_t)t16;
        if (t17 > (uintptr_t)t14 || (uintptr_t)t14 > (uintptr_t)t14) gm_trap_slice(t17, (uintptr_t)t14, t14, gm_srcrsgf2gf2b1dq, 535, 21);
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t15 + t17 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)((uintptr_t)t14 - t17));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
        uint64_t t20 = gm_l2;
        uint8_t t21 = gm_f_digit_at_gkhhkiq(t18, t19, t20);
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_sci_digit_sqvlt0a_c(gm_cs2rc1d61u3qan a0, gm_b16_8 a1, uint64_t a2) { return gm_f_sci_digit_sqvlt0a(&a0, a1, a2); }
inline __attribute__((aligned(4))) void gm_f_sci_digits_0sasrt8(void *gm_out, double a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_digits_0sasrt8 F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* sig */
    gm_b16_8 gm_l2; /* d */
    gm_t43v1gep754th gm_l3; /* parts */
    gm_t2lqu8jsj511d gm_l4; /* ex */
    gm_t88486mk7vrvo gm_l5; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        double t2 = gm_l0;
        gm_t43v1gep754th t3 = GM_CONV(gm_t43v1gep754th, gm_f_decompose_c1606vm(t2));
        gm_l3 = t3;
    }
    {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        int32_t t5 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t6 = gm_l2;
        gm_t2lqu8jsj511d t7 = GM_CONV(gm_t2lqu8jsj511d, gm_f_expand_7rpkqt1(t4, t5, t6));
        gm_l4 = t7;
    }
    {
        memset((uint8_t *)&gm_l5 + 21, 0, 3);
        GM_ST(uint64_t, (uint8_t *)&gm_l5 + 0, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&gm_l5 + 8, ((uint64_t)0ULL));
        GM_ST(int32_t, (uint8_t *)&gm_l5 + 16, ((int32_t)0LL));
        GM_ST(uint8_t, (uint8_t *)&gm_l5 + 20, ((uint8_t)0));
    }
    {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
        uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
        if (!t10) goto ie8;
        {
            memmove(gm_out, ((uint8_t *)&gm_l5), 24);
            goto gm_fdone;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        for (;;) {
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
            uint64_t t14 = (uintptr_t)t13;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcrsgf2gf2b1dq, 524, 11);
            uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 1));
            uint8_t t17 = ((t16 == ((uint8_t)0ULL)));
            if (!t17) goto lb12;
            {
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
                uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                if (o22) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 524, 29);
                uint64_t t23 = r21;
                GM_ST(uint64_t, (((uint8_t *)&gm_l5) + 0), t23);
            }
lc12:;
        }
lb12:;
    }
    {
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t r26; bool o27 = __builtin_sub_overflow(t24, t25, &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 525, 15);
        uint64_t t28 = r26;
        GM_ST(uint64_t, (((uint8_t *)&gm_l5) + 8), t28);
    }
    {
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        int32_t t30 = (int32_t)t29;
        int32_t r31; bool o32 = __builtin_sub_overflow(t30, ((int32_t)1LL), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 526, 13);
        int32_t t33 = r31;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        int32_t t35 = (int32_t)t34;
        int32_t r36; bool o37 = __builtin_sub_overflow(t33, t35, &r36);
        if (o37) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 526, 13);
        int32_t t38 = r36;
        GM_ST(int32_t, (((uint8_t *)&gm_l5) + 16), t38);
    }
    {
        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t t42 = (uintptr_t)t41;
        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 0));
        uint64_t t44 = (uintptr_t)t43;
        if (t42 > t44 || t44 > (uintptr_t)t39) gm_trap_slice(t42, t44, t39, gm_srcrsgf2gf2b1dq, 527, 28);
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(((uint8_t *)t40 + t42 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(t44 - t42));
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t47 = gm_l1;
        uint8_t t48 = gm_f_round_digits_4thelh5(t45, t46, t47);
        GM_ST(uint8_t, (((uint8_t *)&gm_l5) + 20), t48);
    }
    {
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 20));
        if (!t50) goto ie49;
        {
            int32_t t52 = GM_LD(int32_t, (((uint8_t *)&gm_l5) + 16));
            int32_t r53; bool o54 = __builtin_add_overflow(t52, ((int32_t)1LL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 528, 18);
            int32_t t55 = r53;
            GM_ST(int32_t, (((uint8_t *)&gm_l5) + 16), t55);
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_cs2rc1d61u3qan gm_f_sci_digits_0sasrt8_c(double a0, uint64_t a1, gm_b16_8 a2) { gm_t88486mk7vrvo gm_r; gm_f_sci_digits_0sasrt8(&gm_r, a0, a1, a2); return GM_CONV(gm_cs2rc1d61u3qan, gm_r); }
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_oikoiqs F;
    double gm_l0; /* x */
    uint32_t gm_l1; /* digits */
    gm_b16_8 gm_l2; /* buf */
    gm_b16_8 gm_l3; /* sp */
    uint64_t gm_l5; /* places */
    gm_t88486mk7vrvo gm_l6; /* sd */
    uint8_t gm_l7; /* neg */
    uint32_t gm_l8; /* ae */
    uint64_t gm_l9; /* exp_digits */
    uint64_t gm_l10; /* total */
    uint64_t gm_l11; /* out */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint64_t gm_l15; /* k */
    uint32_t gm_l16; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 2319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        double t3 = gm_l0;
        gm_b16_8 t4 = gm_l2;
        gm_th2lqbo77pvsc t5;
        gm_f_special_meo3u57((void *)&t5, t3, t4);
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t91rshbm1iu79 t11;
        for (uint64_t i = 0; i < 1100; i++) GM_ST(uint8_t, (uint8_t *)&t11 + i * 1, ((uint8_t)0ULL));
        F.l4 = t11;
    }
    {
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        gm_l5 = t13;
    }
    {
        double t14 = gm_l0;
        uint64_t t15 = gm_l5;
        uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 547, 25);
        uint64_t t18 = r16;
        void * t19 = ((uint8_t *)&F.l4);
        if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 547, 37);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t19 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
        gm_f_sci_digits_0sasrt8((void *)&gm_l6, t14, t18, t20);
    }
    {
        double t21 = gm_l0;
        uint8_t t22 = (uint8_t)(__builtin_signbit(t21) != 0);
        gm_l7 = t22;
    }
    {
        int32_t t23;
        int32_t t25 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        uint8_t t26 = (t25 < ((int32_t)0LL));
        if (!t26) goto ie24;
        int32_t t28 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        int32_t r29; bool o30 = __builtin_sub_overflow(((int32_t)0LL), t28, &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 549, 28);
        int32_t t31 = r29;
        int32_t t32 = t31;
        t23 = t32;
        goto ix24;
ie24:;
        int32_t t34 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        int32_t t35 = t34;
        t23 = t35;
ix24:;
        uint32_t t36 = (uint32_t)t23;
        gm_l8 = t36;
    }
    {
        uint64_t t37;
        uint32_t t39 = gm_l8;
        uint8_t t40 = (t39 >= ((uint32_t)100ULL));
        if (!t40) goto ie38;
        uint64_t t42 = ((uint64_t)3ULL);
        t37 = t42;
        goto ix38;
ie38:;
        uint64_t t44 = ((uint64_t)2ULL);
        t37 = t44;
ix38:;
        gm_l9 = t37;
    }
    {
        uint64_t r45; bool o46 = __builtin_add_overflow(((uint64_t)1ULL), ((uint64_t)2ULL), &r45);
        if (o46) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 551, 14);
        uint64_t t47 = r45;
        uint64_t t48 = gm_l9;
        uint64_t r49; bool o50 = __builtin_add_overflow(t47, t48, &r49);
        if (o50) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 551, 14);
        uint64_t t51 = r49;
        gm_l10 = t51;
    }
    {
        uint8_t t53 = gm_l7;
        if (!t53) goto ie52;
        {
            uint64_t t55 = gm_l10;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            if (o57) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 552, 14);
            uint64_t t58 = r56;
            gm_l10 = t58;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        uint64_t t60 = gm_l5;
        uint8_t t61 = (t60 > ((uint64_t)0ULL));
        if (!t61) goto ie59;
        {
            uint64_t t63 = gm_l10;
            uint64_t t64 = gm_l5;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            if (o66) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 553, 30);
            uint64_t t67 = r65;
            uint64_t r68; bool o69 = __builtin_add_overflow(t63, t67, &r68);
            if (o69) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 553, 21);
            uint64_t t70 = r68;
            gm_l10 = t70;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        uint64_t t72 = gm_l10;
        uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t74 = (t72 > t73);
        if (!t74) goto ie71;
        {
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sa954a25u62up));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(45));
            gm_b16_8 t77 = t76;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t77)), GM_LD(uintptr_t, ((uint8_t *)&t77) + 8), gm_srcrsgf2gf2b1dq, 554, 26);
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        uint8_t t79 = gm_l7;
        if (!t79) goto ie78;
        {
            uint64_t t81 = gm_l11;
            uint64_t t82 = (uintptr_t)t81;
            uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcrsgf2gf2b1dq, 557, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t82 * 1), ((uint8_t)45ULL));
        }
        {
            uint64_t t84 = gm_l11;
            uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
            if (o86) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 558, 9);
            uint64_t t87 = r85;
            gm_l11 = t87;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        uint64_t t88 = gm_l11;
        uint64_t t89 = (uintptr_t)t88;
        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t89 >= (uintptr_t)(t90)) gm_trap_index(t89, t90, gm_srcrsgf2gf2b1dq, 560, 5);
        uint8_t *t91 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t89 * 1);
        void * t92 = ((uint8_t *)&F.l4);
        if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 560, 36);
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t92 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
        gm_b16_8 t94 = t93;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
        uint8_t t96 = gm_f_sci_digit_sqvlt0a(&(*(gm_t88486mk7vrvo *)((uint8_t *)&gm_l6)), t95, ((uint64_t)0ULL));
        uint8_t r97; bool o98 = __builtin_add_overflow(((uint8_t)48ULL), t96, &r97);
        if (o98) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 560, 16);
        uint8_t t99 = r97;
        GM_ST(uint8_t, t91, t99);
    }
    {
        uint64_t t100 = gm_l11;
        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
        if (o102) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 561, 5);
        uint64_t t103 = r101;
        gm_l11 = t103;
    }
    {
        uint64_t t105 = gm_l5;
        uint8_t t106 = (t105 > ((uint64_t)0ULL));
        if (!t106) goto ie104;
        {
            uint64_t t108 = gm_l11;
            uint64_t t109 = (uintptr_t)t108;
            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srcrsgf2gf2b1dq, 563, 9);
            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t109 * 1), ((uint8_t)46ULL));
        }
        {
            uint64_t t111 = gm_l11;
            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
            if (o113) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 564, 9);
            uint64_t t114 = r112;
            gm_l11 = t114;
        }
        {
            {
                gm_l13 = ((uint64_t)1ULL);
            }
            {
                uint64_t t116 = gm_l5;
                uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                if (o118) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 565, 21);
                uint64_t t119 = r117;
                gm_l12 = t119;
            }
            {
                for (;;) {
                    uint64_t t121 = gm_l13;
                    uint64_t t122 = gm_l12;
                    uint8_t t123 = (t121 < t122);
                    if (!t123) goto lb120;
                    {
                        uint64_t t126 = gm_l13;
                        uint64_t t127 = t126;
                        uint64_t t130 = GM_LD(uint64_t, ((uint8_t *)&t127));
                        gm_l14 = t130;
                        {
                            uint64_t t132 = gm_l11;
                            uint64_t t133 = (uintptr_t)t132;
                            uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t133 >= (uintptr_t)(t134)) gm_trap_index(t133, t134, gm_srcrsgf2gf2b1dq, 566, 13);
                            uint8_t *t135 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t133 * 1);
                            void * t136 = ((uint8_t *)&F.l4);
                            if ((uintptr_t)0 > (uintptr_t)1100 || (uintptr_t)1100 > (uintptr_t)1100) gm_trap_slice((uintptr_t)0, (uintptr_t)1100, 1100, gm_srcrsgf2gf2b1dq, 566, 44);
                            gm_b16_8 t137;
                            GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t136 + (uintptr_t)0 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)((uintptr_t)1100 - (uintptr_t)0));
                            gm_b16_8 t138 = t137;
                            gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                            uint64_t t140 = gm_l14;
                            uint8_t t141 = gm_f_sci_digit_sqvlt0a(&(*(gm_t88486mk7vrvo *)((uint8_t *)&gm_l6)), t139, t140);
                            uint8_t r142; bool o143 = __builtin_add_overflow(((uint8_t)48ULL), t141, &r142);
                            if (o143) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 566, 24);
                            uint8_t t144 = r142;
                            GM_ST(uint8_t, t135, t144);
                        }
                        {
                            uint64_t t145 = gm_l11;
                            uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                            if (o147) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 567, 13);
                            uint64_t t148 = r146;
                            gm_l11 = t148;
                        }
                        goto me128;
mn129:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrsgf2gf2b1dq, 565, 9);
me128:;
                    }
lc120:;
                    {
                        uint64_t t150 = gm_l13;
                        uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                        uint64_t t153 = r151;
                        gm_l13 = t153;
                    }
                }
lb120:;
            }
        }
        goto ix104;
ie104:;
ix104:;
    }
    {
        uint64_t t154 = gm_l11;
        uint64_t t155 = (uintptr_t)t154;
        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t155 >= (uintptr_t)(t156)) gm_trap_index(t155, t156, gm_srcrsgf2gf2b1dq, 570, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t155 * 1), ((uint8_t)101ULL));
    }
    {
        uint64_t t157 = gm_l11;
        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
        if (o159) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 571, 9);
        uint64_t t160 = r158;
        uint64_t t161 = (uintptr_t)t160;
        uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t161 >= (uintptr_t)(t162)) gm_trap_index(t161, t162, gm_srcrsgf2gf2b1dq, 571, 5);
        uint8_t *t163 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t161 * 1);
        uint8_t t164;
        int32_t t166 = GM_LD(int32_t, (((uint8_t *)&gm_l6) + 16));
        uint8_t t167 = (t166 < ((int32_t)0LL));
        if (!t167) goto ie165;
        uint8_t t169 = ((uint8_t)45ULL);
        t164 = t169;
        goto ix165;
ie165:;
        uint8_t t171 = ((uint8_t)43ULL);
        t164 = t171;
ix165:;
        GM_ST(uint8_t, t163, t164);
    }
    {
        uint64_t t172 = gm_l11;
        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)2ULL), &r173);
        if (o174) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 572, 5);
        uint64_t t175 = r173;
        gm_l11 = t175;
    }
    {
        uint64_t t176 = gm_l9;
        gm_l15 = t176;
    }
    {
        uint32_t t177 = gm_l8;
        gm_l16 = t177;
    }
    {
        for (;;) {
            uint64_t t179 = gm_l15;
            uint8_t t180 = (t179 > ((uint64_t)0ULL));
            if (!t180) goto lb178;
            {
                uint64_t t183 = gm_l15;
                uint64_t r184; bool o185 = __builtin_sub_overflow(t183, ((uint64_t)1ULL), &r184);
                if (o185) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 576, 9);
                uint64_t t186 = r184;
                gm_l15 = t186;
            }
            {
                uint64_t t187 = gm_l11;
                uint64_t t188 = gm_l15;
                uint64_t r189; bool o190 = __builtin_add_overflow(t187, t188, &r189);
                if (o190) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 577, 13);
                uint64_t t191 = r189;
                uint64_t t192 = (uintptr_t)t191;
                uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_srcrsgf2gf2b1dq, 577, 9);
                uint32_t t194 = gm_l16;
                if (((uint32_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 577, 31);
                uint32_t t195 = (uint32_t)(t194 % ((uint32_t)10ULL));
                uint8_t t196 = (uint8_t)t195;
                uint8_t r197; bool o198 = __builtin_add_overflow(((uint8_t)48ULL), t196, &r197);
                if (o198) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 577, 24);
                uint8_t t199 = r197;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t192 * 1), t199);
            }
            {
                uint32_t t200 = gm_l16;
                if (((uint32_t)10ULL) == 0) gm_trap("division by zero", gm_srcrsgf2gf2b1dq, 578, 9);
                uint32_t t201 = (uint32_t)(t200 / ((uint32_t)10ULL));
                gm_l16 = t201;
            }
lc178:;
        }
lb178:;
    }
    {
        uint64_t t202 = gm_l11;
        uint64_t t203 = gm_l9;
        uint64_t r204; bool o205 = __builtin_add_overflow(t202, t203, &r204);
        if (o205) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 580, 5);
        uint64_t t206 = r204;
        gm_l11 = t206;
    }
    {
        uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t208 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t209 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t210 = gm_l11;
        uint64_t t211 = (uintptr_t)t210;
        if (t209 > t211 || t211 > (uintptr_t)t207) gm_trap_slice(t209, t211, t207, gm_srcrsgf2gf2b1dq, 581, 12);
        gm_b16_8 t212;
        GM_ST(void *, (uint8_t *)&t212, (void *)(((uint8_t *)t208 + t209 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(t211 - t209));
        gm_b16_8 t213 = t212;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
        uint8_t t215 = 0;
        F.ret = t214;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_special_meo3u57(void *gm_out, double a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_special_meo3u57 F;
    double gm_l0; /* x */
    gm_b16_8 gm_l1; /* buf */
    gm_b16_8 gm_l2; /* text */
    GM_STACK_CHECK_SHALLOW(&F, 527u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        double t4 = gm_l0;
        uint8_t t5 = (uint8_t)__builtin_isnan(t4);
        if (!t5) goto ie3;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sfa86erpod86s));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
        gm_b16_8 t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t9;
        double t11 = gm_l0;
        uint8_t t12 = (uint8_t)__builtin_isinf(t11);
        if (!t12) goto ie10;
        gm_b16_8 t14;
        double t16 = gm_l0;
        uint8_t t17 = (uint8_t)(__builtin_signbit(t16) != 0);
        if (!t17) goto ie15;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_segch6a5dme9u));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
        gm_b16_8 t20 = t19;
        t14 = t20;
        goto ix15;
ie15:;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sns75reqq32d2));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
        gm_b16_8 t23 = t22;
        t14 = t23;
ix15:;
        gm_b16_8 t24 = t14;
        t9 = t24;
        goto ix10;
ie10:;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(0));
        gm_b16_8 t27 = t26;
        t9 = t27;
ix10:;
        t2 = t9;
ix3:;
        gm_l2 = t2;
    }
    {
        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t30 = ((t29 == ((uint64_t)0ULL)));
        if (!t30) goto ie28;
        {
            gm_th2lqbo77pvsc t32;
            memset(&t32, 0, sizeof t32);
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t37 = (t35 > t36);
        if (!t37) goto ie34;
        {
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sa954a25u62up));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(45));
            gm_b16_8 t40 = t39;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t40)), GM_LD(uintptr_t, ((uint8_t *)&t40) + 8), gm_srcrsgf2gf2b1dq, 440, 29);
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t43 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t45 = (uintptr_t)t44;
        if (t43 > t45 || t45 > (uintptr_t)t41) gm_trap_slice(t43, t45, t41, gm_srcrsgf2gf2b1dq, 441, 5);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t42 + t43 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t43));
        gm_b16_8 t47 = gm_l2;
        gm_f_copy_from_f7j6p05(t46, t47);
    }
    {
        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
        uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t52 = (uintptr_t)t51;
        if (t50 > t52 || t52 > (uintptr_t)t48) gm_trap_slice(t50, t52, t48, gm_srcrsgf2gf2b1dq, 442, 12);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t49 + t50 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t50));
        gm_b16_8 t54 = t53;
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
        gm_th2lqbo77pvsc t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t56), t55);
        ((uint8_t *)&t56)[16] = 1;
        uint8_t t57 = 0;
        F.ret = t56;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_special_meo3u57_c(double a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_special_meo3u57(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_front_b98pl8o(gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_front_b98pl8o F;
    gm_b16_8 gm_l0; /* buf */
    uint64_t gm_l1; /* at */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 426u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l2;
        uint64_t r5; bool o6 = __builtin_add_overflow(t3, t4, &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 88, 8);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t9 = (t7 > t8);
        if (!t9) goto ie2;
        {
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sl2sr5fl46c73));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(47));
            gm_b16_8 t12 = t11;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t12)), GM_LD(uintptr_t, ((uint8_t *)&t12) + 8), gm_srcrsgf2gf2b1dq, 88, 27);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t15 = gm_l1;
        uint64_t t16 = (uintptr_t)t15;
        uint64_t t17 = gm_l1;
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 89, 13);
        uint64_t t21 = r19;
        uint64_t t22 = (uintptr_t)t21;
        if (t16 > t22 || t22 > (uintptr_t)t13) gm_trap_slice(t16, t22, t13, gm_srcrsgf2gf2b1dq, 89, 5);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t14 + t16 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(t22 - t16));
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_sub_overflow(t26, t27, &r28);
        if (o29) gm_trap("integer overflow in `-`", gm_srcrsgf2gf2b1dq, 89, 35);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t33 = (uintptr_t)t32;
        if (t31 > t33 || t33 > (uintptr_t)t24) gm_trap_slice(t31, t33, t24, gm_srcrsgf2gf2b1dq, 89, 31);
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t25 + t31 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t31));
        gm_b16_8 t35 = t34;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_f_copy_from_f7j6p05(t23, t36);
    }
    {
        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l1;
        uint64_t t41 = gm_l2;
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 90, 19);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        if (t39 > t45 || t45 > (uintptr_t)t37) gm_trap_slice(t39, t45, t37, gm_srcrsgf2gf2b1dq, 90, 12);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
