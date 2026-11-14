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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csfd6et11lk21p { _Bool f0; uint32_t f1; } gm_csfd6et11lk21p;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csfd6et11lk21p) == 8 && _Alignof(gm_csfd6et11lk21p) == 4 && offsetof(gm_csfd6et11lk21p, f0) == 0 && offsetof(gm_csfd6et11lk21p, f1) == 4, "layout of `IntW`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t88rcucg1b7uk f2; uint32_t f3; gm_t88rcucg1b7uk f4; uint8_t pe[4]; } gm_t0h5lk09e9p5r;
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_ta6jn77i9rmqf;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tdvcgvrf81sbd;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[6]; uint64_t f5; uint64_t f6; } gm_tt49maei14cl6;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t0h5lk09e9p5r) == 40 && _Alignof(gm_t0h5lk09e9p5r) == 8 && offsetof(gm_t0h5lk09e9p5r, f0) == 0 && offsetof(gm_t0h5lk09e9p5r, f1) == 16 && offsetof(gm_t0h5lk09e9p5r, f2) == 24 && offsetof(gm_t0h5lk09e9p5r, f3) == 28 && offsetof(gm_t0h5lk09e9p5r, f4) == 32, "layout of `(host: []u8, addr: u64, whole: Ty, off: u32, fty: Ty)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tbl8il92l97ut v1; } f1; } gm_t9b5r56q9jk9c;
_Static_assert(sizeof(gm_ta6jn77i9rmqf) == 32 && _Alignof(gm_ta6jn77i9rmqf) == 8 && offsetof(gm_ta6jn77i9rmqf, f0) == 0 && offsetof(gm_ta6jn77i9rmqf, f1) == 8 && offsetof(gm_ta6jn77i9rmqf, f2) == 16 && offsetof(gm_ta6jn77i9rmqf, f3) == 24, "layout of `(how: WordCmp, l: ^Node, r: ^Node, negated: bool)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdvcgvrf81sbd) == 24 && _Alignof(gm_tdvcgvrf81sbd) == 8 && offsetof(gm_tdvcgvrf81sbd, f0) == 0 && offsetof(gm_tdvcgvrf81sbd, f1) == 8 && offsetof(gm_tdvcgvrf81sbd, f2) == 12 && offsetof(gm_tdvcgvrf81sbd, f3) == 16, "layout of `(base: ^Node, index: u32, bit_off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_tf4r0415i49d1;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tkm39klvnn4cr;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint64_t v1; uint64_t v2; gm_t0h5lk09e9p5r v3; } f1; } gm_tou12vuc19e0s;
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tscalq56heofe;
_Static_assert(sizeof(gm_tt49maei14cl6) == 48 && _Alignof(gm_tt49maei14cl6) == 8 && offsetof(gm_tt49maei14cl6, f0) == 0 && offsetof(gm_tt49maei14cl6, f1) == 8 && offsetof(gm_tt49maei14cl6, f2) == 16 && offsetof(gm_tt49maei14cl6, f3) == 24 && offsetof(gm_tt49maei14cl6, f4) == 25 && offsetof(gm_tt49maei14cl6, f5) == 32 && offsetof(gm_tt49maei14cl6, f6) == 40, "layout of `(base: ^Node, lo: ?^Node, hi: ?^Node, inclusive: bool, seq: SeqKind, esz: u64, len: u64)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[5]; void * f2; void * f3; } gm_tuggr5sml9o52;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t0pc2c7dafsjt) == 40 && _Alignof(gm_t0pc2c7dafsjt) == 8 && offsetof(gm_t0pc2c7dafsjt, f0) == 0 && offsetof(gm_t0pc2c7dafsjt, f1) == 4 && offsetof(gm_t0pc2c7dafsjt, f2) == 12 && offsetof(gm_t0pc2c7dafsjt, f3) == 24 && offsetof(gm_t0pc2c7dafsjt, f4) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_t9b5r56q9jk9c) == 32 && _Alignof(gm_t9b5r56q9jk9c) == 8 && offsetof(gm_t9b5r56q9jk9c, f0) == 0 && offsetof(gm_t9b5r56q9jk9c, f1) == 8, "layout of `Flow!(frame: ^Frame, label: u32)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tf4r0415i49d1) == 48 && _Alignof(gm_tf4r0415i49d1) == 8 && offsetof(gm_tf4r0415i49d1, f0) == 0 && offsetof(gm_tf4r0415i49d1, f1) == 24 && offsetof(gm_tf4r0415i49d1, f2) == 32, "layout of `(effect_: EffectRef, op: u32, args: []^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tkm39klvnn4cr) == 32 && _Alignof(gm_tkm39klvnn4cr) == 8 && offsetof(gm_tkm39klvnn4cr, f0) == 0 && offsetof(gm_tkm39klvnn4cr, f1) == 4 && offsetof(gm_tkm39klvnn4cr, f2) == 16 && offsetof(gm_tkm39klvnn4cr, f3) == 24, "layout of `(op: BinOp, how: WordCmp, l: ^Node, r: ^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tou12vuc19e0s v1; } f1; } gm_tl1r7a2fn1ev6;
_Static_assert(sizeof(gm_tou12vuc19e0s) == 48 && _Alignof(gm_tou12vuc19e0s) == 8 && offsetof(gm_tou12vuc19e0s, f0) == 0 && offsetof(gm_tou12vuc19e0s, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tscalq56heofe) == 40 && _Alignof(gm_tscalq56heofe) == 8 && offsetof(gm_tscalq56heofe, f0) == 0 && offsetof(gm_tscalq56heofe, f1) == 12 && offsetof(gm_tscalq56heofe, f2) == 24 && offsetof(gm_tscalq56heofe, f3) == 32, "layout of `(op: Builtin, t: IntW, x: ^Node, k: ^Node)`");
_Static_assert(sizeof(gm_tuggr5sml9o52) == 24 && _Alignof(gm_tuggr5sml9o52) == 8 && offsetof(gm_tuggr5sml9o52, f0) == 0 && offsetof(gm_tuggr5sml9o52, f1) == 2 && offsetof(gm_tuggr5sml9o52, f2) == 8 && offsetof(gm_tuggr5sml9o52, f3) == 16, "layout of `(op: BinOp, wide: bool, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tl1r7a2fn1ev6) == 56 && _Alignof(gm_tl1r7a2fn1ev6) == 8 && offsetof(gm_tl1r7a2fn1ev6, f0) == 0 && offsetof(gm_tl1r7a2fn1ev6, f1) == 8, "layout of `Flow!Place`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_coerce_im8vs3u {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_cond_v8r7708 {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_else__7epkk2p {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ev_25jfdar {
    gm_frame h;
    gm_tou12vuc19e0s l16; /* p */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_args_j1qj6gl {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_builtin_86a9q3q {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* vals */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_discard_8c4a8jg {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_index_bie377m {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_ev_return_8ggbvjj {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ev_slow_hdak0k9 {
    gm_frame h;
    gm_tou12vuc19e0s l13; /* p */
    gm_tou12vuc19e0s l15; /* p */
    gm_tou12vuc19e0s l20; /* p */
    gm_tp0fa5qv7rqnn l23; /* x */
    gm_tou12vuc19e0s l28; /* p */
    gm_tt49maei14cl6 l29; /* x */
    gm_tf4r0415i49d1 l58; /* x */
    gm_t710lsrq1m8gk l117; /* x */
    gm_tntn2tf15puib l134; /* x */
    gm_t710lsrq1m8gk l135; /* w */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ew_c5kil07 {
    gm_frame h;
    gm_t0pc2c7dafsjt l9; /* x */
    gm_tscalq56heofe l30; /* x */
    gm_t2k9dbsv5ff58 l49; /* st */
    gm_t833u93mr4iob ret;
};
struct gm_fr_ew_slow_0jkgi8d {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_builtin_s6t151o {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_is_place_expr_qlfi6ot {
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
struct gm_fr_list_fufbgf7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_string_ttuk4gr {
    gm_frame h;
    gm_tpekhfs32op0f l4; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sext_g6f00aa {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_try__q6sv04f {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_unwrap_f7c53ka {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* out */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_word_eq_8jc0uot {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_word_less_l13biee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_temp_9cmh124(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_binary_6rg6s7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits_raw_t6dqs65(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_block_5b89ev3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_call_node_a1b30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_cancel__tb288ki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_coerce_im8vs3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tmn7n0n9raou5 a2, void * a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_cond_v8r7708(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_conds_30ust77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_contains_nugmipn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
__attribute__((aligned(4))) void gm_f_else__7epkk2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, void * a4, void * a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_ev_builtin_86a9q3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_ev_call_value_f4tfqs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_ev_discard_8c4a8jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ev_index_bie377m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ev_perform_ovt4j0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, uint32_t a3, gm_b16_8 a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_ev_return_8ggbvjj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_ev_slice_i5uin87(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4, void * a5, uint8_t a6, gm_t9qkh0rsooc8d a7, uint64_t a8, uint64_t a9, gm_b16_8 a10);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ev_slow_hdak0k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ew_slow_0jkgi8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_exec_stmt_4jh6tlr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_fiber_builtin_rpc9l9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_2hd15t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e1vik51(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_r6pm0oj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) uint32_t gm_f_frame_handle_k12gn88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_frame_up_ghkkuqg(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_builtin_s6t151o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_int_op_5g0lv3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, gm_ttovhfee8mf28 a3, gm_ttovhfee8mf28 a4, void * a5, void * a6, void * a7);
__attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) uint8_t gm_f_is_place_expr_qlfi6ot(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_label_dst_b5cat1e(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_load_word_ud3iuha(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_loop__3bo7tki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) void gm_f_match__tg8e3g3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_place_47rfqe5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_put_word_tfn443i(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_local_n8sc5ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_place_fcc38bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_resolve_target_bdf5i1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_te9hrk0u35rev a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_resume__mi7hk2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_set_local_ug1vgfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_string_literal_337m6cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_sys_call_6jqv32o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_try__q6sv04f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_unary_sorkbs7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_t9qkh0rsooc8d a2, void * a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_unwrap_f7c53ka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_with__u1jp2ib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write__eua2bmh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, void * a6);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_knna6g3th5t[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0lfbgij46fm3[74];
extern const uint8_t gm_s1kvbdtols1cc[53];
extern const uint8_t gm_s76i2uidi0pjj[54];
extern const uint8_t gm_sar5aak6f8jpo[44];
extern const uint8_t gm_sco971hnpq7nk[49];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sd5r0412dj6fb[15];
extern const uint8_t gm_sds80esh09gni[51];
extern const uint8_t gm_sen05m4ui88rt[58];
extern const uint8_t gm_sl4daef1gs04o[37];
extern const uint8_t gm_sl9b2am1i9pmg[52];
extern const uint8_t gm_snh5ve0osdp08[7];
extern const uint8_t gm_sob69bvkm3aek[33];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_spic5gjomd7lu[62];
extern const uint8_t gm_sqnuqut1dl3fi[48];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src0jbci4ff2esa[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgr2i687arao[75];
extern const uint8_t gm_ss1mi142k161m[41];
extern const uint8_t gm_ssgce158bfb9m[55];
extern const uint8_t gm_su2imns6t7mh5[41];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
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
    GM_STACK_CHECK(&F, 88u);
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint8_t t4 = (t3 <= ((uint64_t)1ULL));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
        uint64_t t12 = r10;
        uint64_t t13 = (uint64_t)(t8 & t12);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie7;
        {
            uint64_t t16 = gm_l0;
            uint64_t t17 = gm_l1;
            uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
            uint64_t t20 = r18;
            uint64_t t21 = (uint64_t)(t16 & t20);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t24 = gm_l0;
        uint64_t t25 = gm_l1;
        if (t25 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
        uint64_t t26 = (uint64_t)(t24 % t25);
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2p7d2kv F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_addr_odbbf6a F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = GM_LD(uint64_t, gm_cmiriali67r);
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_mul_overflow(((uint64_t)16ULL), t3, &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src6hq50r4rk7e6, 5, 64);
    uint64_t t6 = r4;
    uint64_t r7; bool o8 = __builtin_add_overflow(t1, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 5, 52);
    uint64_t t9 = r7;
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
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
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
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
            gm_b16_8 t11 = gm_f_empty_slice_2p7d2kv();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_bslikk9(t10, ((uint64_t)1ULL), t12);
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
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t10, t11);
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            uint64_t t8 = gm_l2;
            gm_f_wr_word_npte9fs(t6, t7, t8);
        }
        goto ix2;
ie2:;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            uint64_t t12 = gm_l2;
            unsigned __int128 t13 = (unsigned __int128)t12;
            gm_f_put_uint_cddin99(t10, t11, t13);
        }
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = gm_l2;
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint64_t t5 = gm_l3;
        uint8_t t6 = (t5 <= ((uint64_t)1ULL));
        if (!t6) goto ie2;
        {
            gm_b16_8 t8 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l3;
        uint8_t t12 = gm_f_aligned_q6sor4h(t10, t11);
        if (!t12) goto ie9;
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            uint64_t t17 = gm_l2;
            void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
            if (!(GM_LD(void *, ((uint8_t *)&t18)) != 0)) goto ie14;
            void * t19 = GM_LD(void *, ((uint8_t *)&t18));
            gm_l4 = t19;
            {
                void * t21 = gm_l4;
                void * t22 = ((uint8_t *)t21);
                uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t24 = gm_l2;
                uint64_t t25 = gm_f_us_cuv01fp(t24);
                uint64_t t26 = (uintptr_t)t25;
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + t23 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26 - t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t27);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t28 = gm_l0;
        uint64_t t29 = gm_l1;
        uint64_t t30 = gm_l2;
        uint64_t t31 = gm_l3;
        gm_tpekhfs32op0f t32;
        gm_f_fault_u31b92l((void *)&t32, t28, t29, t30, t31, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
    if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)) + t3 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)) + t10 * 1));
    uint8_t t13 = (!(t12 == ((uint8_t)0ULL)));
    F.ret = t13;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sext_g6f00aa F;
    uint64_t gm_l0; /* v */
    uint32_t gm_l1; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    int64_t t2;
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint32_t)8ULL))) goto mn4;
    uint64_t t6 = gm_l0;
    uint8_t t7 = (uint8_t)t6;
    int8_t t8;
    memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 1 ? sizeof t8 : 1);
    int64_t t9 = (int64_t)t8;
    t2 = t9;
    goto me3;
mn4:;
    uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t11 == ((uint32_t)16ULL))) goto mn10;
    uint64_t t12 = gm_l0;
    uint16_t t13 = (uint16_t)t12;
    int16_t t14;
    memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 2 ? sizeof t14 : 2);
    int64_t t15 = (int64_t)t14;
    t2 = t15;
    goto me3;
mn10:;
    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&t1));
    if (!(t17 == ((uint32_t)32ULL))) goto mn16;
    uint64_t t18 = gm_l0;
    uint32_t t19 = (uint32_t)t18;
    int32_t t20;
    memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 4 ? sizeof t20 : 4);
    int64_t t21 = (int64_t)t20;
    t2 = t21;
    goto me3;
mn16:;
    uint64_t t23 = gm_l0;
    int64_t t24;
    memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 8 ? sizeof t24 : 8);
    t2 = t24;
    goto me3;
mn22:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 11, 41);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 561u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l4 = t10;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        uint64_t t15 = (uint64_t)t14;
        uint8_t t16 = (t12 >= t15);
        if (!t16) goto ie11;
        {
            uint8_t t18 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((((uint8_t *)t19) + 0) + 0));
        void * t21 = t20;
        uint64_t t22 = gm_l4;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        gm_l5 = t25;
    }
    {
        void * t26 = gm_l5;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28;
        if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
            void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            uint8_t t30 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l6 = t28;
    }
    {
        uint64_t t31 = gm_l1;
        void * t32 = gm_l5;
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
        uint64_t r34; bool o35 = __builtin_sub_overflow(t31, t33, &r34);
        if (o35) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
        uint64_t t36 = r34;
        gm_l7 = t36;
    }
    {
        uint64_t t37 = gm_l7;
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 16));
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 211, 10);
        uint64_t t41 = (uint64_t)(t37 >> (int)n40);
        gm_l8 = t41;
    }
    {
        void * t42 = gm_l6;
        void * t43 = t42;
        uint64_t t44 = gm_l8;
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        uint64_t t46 = (uintptr_t)t45;
        void * t47 = (void *)((uint8_t *)t43 + t46 * 24);
        gm_l9 = t47;
    }
    {
        uint64_t t48 = gm_l7;
        uint64_t t49 = gm_l8;
        void * t50 = gm_l5;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 16));
        unsigned __int128 n52 = (unsigned __int128)t51;
        if (n52 >= 64) gm_trap_shift((uint64_t)n52, "u64", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t r53 = (uint64_t)((uint64_t)t49 << (int)n52);
        bool o54 = (uint64_t)(r53 >> (int)n52) != t49;
        if (o54) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
        uint64_t t55 = r53;
        uint64_t r56; bool o57 = __builtin_sub_overflow(t48, t55, &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
        uint64_t t58 = r56;
        gm_l10 = t58;
    }
    {
        uint8_t t60 = 0;
        uint8_t t61 = 0;
        uint8_t t62 = 0;
        void * t63 = gm_l9;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t63) + 17));
        uint8_t t65 = (uint8_t)!t64;
        if (!t65) {
            uint64_t t66 = gm_l2;
            void * t67 = gm_l9;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 8));
            uint8_t t69 = (t66 > t68);
            t62 = t69;
        } else t62 = 1;
        if (!t62) {
            uint64_t t70 = gm_l10;
            void * t71 = gm_l9;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
            uint64_t t73 = gm_l2;
            uint64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
            if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
            uint64_t t76 = r74;
            uint8_t t77 = (t70 > t76);
            t61 = t77;
        } else t61 = 1;
        if (!t61) {
            uint8_t t78 = 0;
            uint8_t t79 = gm_l3;
            if (t79) {
                void * t80 = gm_l9;
                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 18));
                uint8_t t82 = (uint8_t)!t81;
                t78 = t82;
            } else t78 = 0;
            t60 = t78;
        } else t60 = 1;
        if (!t60) goto ie59;
        {
            uint8_t t84 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t85 = gm_l9;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        void * t87 = t86;
        uint64_t t88 = gm_l10;
        uint64_t t89 = gm_f_us_cuv01fp(t88);
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = (void *)((uint8_t *)t87 + t90 * 1);
        void * t92 = t91;
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_eq_8jc0uot F;
    gm_tmn7n0n9raou5 gm_l0; /* how */
    uint64_t gm_l1; /* p */
    uint64_t gm_l2; /* q */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)3ULL)) goto mn9;
    uint64_t t11 = gm_l1;
    double t12;
    memset(&t12, 0, sizeof t12); memcpy(&t12, ((uint8_t *)&t11), sizeof t12 < 8 ? sizeof t12 : 8);
    uint64_t t13 = gm_l2;
    double t14;
    memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 8 ? sizeof t14 : 8);
    uint8_t t15 = ((t12 == t14));
    t1 = t15;
    goto me2;
mn9:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto mn16;
    uint64_t t18 = gm_l1;
    uint32_t t19 = (uint32_t)t18;
    float t20;
    memset(&t20, 0, sizeof t20); memcpy(&t20, ((uint8_t *)&t19), sizeof t20 < 4 ? sizeof t20 : 4);
    uint64_t t21 = gm_l2;
    uint32_t t22 = (uint32_t)t21;
    float t23;
    memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 4 ? sizeof t23 : 4);
    uint8_t t24 = ((t20 == t23));
    t1 = t24;
    goto me2;
mn16:;
ma5:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)4ULL)) goto mn25;
    uint64_t t27 = gm_l1;
    uint8_t t28 = (!(t27 == ((uint64_t)0ULL)));
    uint64_t t29 = gm_l2;
    uint8_t t30 = (!(t29 == ((uint64_t)0ULL)));
    uint8_t t31 = ((t28 == t30));
    t1 = t31;
    goto me2;
mn25:;
ma6:;
    uint64_t t33 = gm_l1;
    uint64_t t34 = gm_l2;
    uint8_t t35 = ((t33 == t34));
    t1 = t35;
    goto me2;
mn32:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 70, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_less_l13biee F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_tmn7n0n9raou5 gm_l1; /* how */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    int64_t gm_l5; /* x */
    int64_t gm_l6; /* y */
    double gm_l7; /* x */
    double gm_l8; /* y */
    float gm_l9; /* x */
    float gm_l10; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l4 = t11;
        {
            uint64_t t13 = gm_l2;
            uint32_t t14 = gm_l4;
            int64_t t15 = gm_f_sext_g6f00aa(t13, t14);
            gm_l5 = t15;
        }
        {
            uint64_t t16 = gm_l3;
            uint32_t t17 = gm_l4;
            int64_t t18 = gm_f_sext_g6f00aa(t16, t17);
            gm_l6 = t18;
        }
        {
            uint8_t t19;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma24; default: goto ma24; }
ma21:;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t28 != ((uint8_t)10ULL)) goto mn27;
            int64_t t29 = gm_l5;
            int64_t t30 = gm_l6;
            uint8_t t31 = (t29 < t30);
            t19 = t31;
            goto me20;
mn27:;
ma22:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t33 != ((uint8_t)11ULL)) goto mn32;
            int64_t t34 = gm_l5;
            int64_t t35 = gm_l6;
            uint8_t t36 = (t34 <= t35);
            t19 = t36;
            goto me20;
mn32:;
ma23:;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t38 != ((uint8_t)12ULL)) goto mn37;
            int64_t t39 = gm_l5;
            int64_t t40 = gm_l6;
            uint8_t t41 = (t39 > t40);
            t19 = t41;
            goto me20;
mn37:;
ma24:;
            int64_t t43 = gm_l5;
            int64_t t44 = gm_l6;
            uint8_t t45 = (t43 >= t44);
            t19 = t45;
            goto me20;
mn42:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 32, 20);
me20:;
            uint8_t t46 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto me2;
mn9:;
ma4:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t48 != ((uint8_t)3ULL)) goto mn47;
        {
            uint64_t t50 = gm_l2;
            double t51;
            memset(&t51, 0, sizeof t51); memcpy(&t51, ((uint8_t *)&t50), sizeof t51 < 8 ? sizeof t51 : 8);
            gm_l7 = t51;
        }
        {
            uint64_t t52 = gm_l3;
            double t53;
            memset(&t53, 0, sizeof t53); memcpy(&t53, ((uint8_t *)&t52), sizeof t53 < 8 ? sizeof t53 : 8);
            gm_l8 = t53;
        }
        {
            uint8_t t54;
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t61) { case ((uint8_t)0ULL): goto ma59; case ((uint8_t)1ULL): goto ma59; case ((uint8_t)2ULL): goto ma59; case ((uint8_t)3ULL): goto ma59; case ((uint8_t)4ULL): goto ma59; case ((uint8_t)5ULL): goto ma59; case ((uint8_t)6ULL): goto ma59; case ((uint8_t)7ULL): goto ma59; case ((uint8_t)8ULL): goto ma59; case ((uint8_t)9ULL): goto ma59; case ((uint8_t)10ULL): goto ma56; case ((uint8_t)11ULL): goto ma57; case ((uint8_t)12ULL): goto ma58; case ((uint8_t)13ULL): goto ma59; default: goto ma59; }
ma56:;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t63 != ((uint8_t)10ULL)) goto mn62;
            double t64 = gm_l7;
            double t65 = gm_l8;
            uint8_t t66 = (t64 < t65);
            t54 = t66;
            goto me55;
mn62:;
ma57:;
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t68 != ((uint8_t)11ULL)) goto mn67;
            double t69 = gm_l7;
            double t70 = gm_l8;
            uint8_t t71 = (t69 <= t70);
            t54 = t71;
            goto me55;
mn67:;
ma58:;
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t73 != ((uint8_t)12ULL)) goto mn72;
            double t74 = gm_l7;
            double t75 = gm_l8;
            uint8_t t76 = (t74 > t75);
            t54 = t76;
            goto me55;
mn72:;
ma59:;
            double t78 = gm_l7;
            double t79 = gm_l8;
            uint8_t t80 = (t78 >= t79);
            t54 = t80;
            goto me55;
mn77:;
ma60:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 42, 20);
me55:;
            uint8_t t81 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto me2;
mn47:;
ma5:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t83 != ((uint8_t)2ULL)) goto mn82;
        {
            uint64_t t85 = gm_l2;
            uint32_t t86 = (uint32_t)t85;
            float t87;
            memset(&t87, 0, sizeof t87); memcpy(&t87, ((uint8_t *)&t86), sizeof t87 < 4 ? sizeof t87 : 4);
            gm_l9 = t87;
        }
        {
            uint64_t t88 = gm_l3;
            uint32_t t89 = (uint32_t)t88;
            float t90;
            memset(&t90, 0, sizeof t90); memcpy(&t90, ((uint8_t *)&t89), sizeof t90 < 4 ? sizeof t90 : 4);
            gm_l10 = t90;
        }
        {
            uint8_t t91;
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t98) { case ((uint8_t)0ULL): goto ma96; case ((uint8_t)1ULL): goto ma96; case ((uint8_t)2ULL): goto ma96; case ((uint8_t)3ULL): goto ma96; case ((uint8_t)4ULL): goto ma96; case ((uint8_t)5ULL): goto ma96; case ((uint8_t)6ULL): goto ma96; case ((uint8_t)7ULL): goto ma96; case ((uint8_t)8ULL): goto ma96; case ((uint8_t)9ULL): goto ma96; case ((uint8_t)10ULL): goto ma93; case ((uint8_t)11ULL): goto ma94; case ((uint8_t)12ULL): goto ma95; case ((uint8_t)13ULL): goto ma96; default: goto ma96; }
ma93:;
            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t100 != ((uint8_t)10ULL)) goto mn99;
            float t101 = gm_l9;
            float t102 = gm_l10;
            uint8_t t103 = (t101 < t102);
            t91 = t103;
            goto me92;
mn99:;
ma94:;
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t105 != ((uint8_t)11ULL)) goto mn104;
            float t106 = gm_l9;
            float t107 = gm_l10;
            uint8_t t108 = (t106 <= t107);
            t91 = t108;
            goto me92;
mn104:;
ma95:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t110 != ((uint8_t)12ULL)) goto mn109;
            float t111 = gm_l9;
            float t112 = gm_l10;
            uint8_t t113 = (t111 > t112);
            t91 = t113;
            goto me92;
mn109:;
ma96:;
            float t115 = gm_l9;
            float t116 = gm_l10;
            uint8_t t117 = (t115 >= t116);
            t91 = t117;
            goto me92;
mn114:;
ma97:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 52, 20);
me92:;
            uint8_t t118 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto me2;
mn82:;
ma6:;
        uint8_t t120;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t127) { case ((uint8_t)0ULL): goto ma125; case ((uint8_t)1ULL): goto ma125; case ((uint8_t)2ULL): goto ma125; case ((uint8_t)3ULL): goto ma125; case ((uint8_t)4ULL): goto ma125; case ((uint8_t)5ULL): goto ma125; case ((uint8_t)6ULL): goto ma125; case ((uint8_t)7ULL): goto ma125; case ((uint8_t)8ULL): goto ma125; case ((uint8_t)9ULL): goto ma125; case ((uint8_t)10ULL): goto ma122; case ((uint8_t)11ULL): goto ma123; case ((uint8_t)12ULL): goto ma124; case ((uint8_t)13ULL): goto ma125; default: goto ma125; }
ma122:;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t129 != ((uint8_t)10ULL)) goto mn128;
        uint64_t t130 = gm_l2;
        uint64_t t131 = gm_l3;
        uint8_t t132 = (t130 < t131);
        t120 = t132;
        goto me121;
mn128:;
ma123:;
        uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t134 != ((uint8_t)11ULL)) goto mn133;
        uint64_t t135 = gm_l2;
        uint64_t t136 = gm_l3;
        uint8_t t137 = (t135 <= t136);
        t120 = t137;
        goto me121;
mn133:;
ma124:;
        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t139 != ((uint8_t)12ULL)) goto mn138;
        uint64_t t140 = gm_l2;
        uint64_t t141 = gm_l3;
        uint8_t t142 = (t140 > t141);
        t120 = t142;
        goto me121;
mn138:;
ma125:;
        uint64_t t144 = gm_l2;
        uint64_t t145 = gm_l3;
        uint8_t t146 = (t144 >= t145);
        t120 = t146;
        goto me121;
mn143:;
ma126:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 59, 21);
me121:;
        uint8_t t147 = 0;
        F.ret = t120;
        goto gm_fret;
        goto me2;
mn119:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 28, 5);
me2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_coerce_im8vs3u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tmn7n0n9raou5 a2, void * a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_coerce_im8vs3u F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    void * gm_l3; /* x */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* b */
    uint8_t gm_l8; /* to_fn */
    uint8_t gm_l9; /* from_fn */
    uint64_t gm_l10; /* addr */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* b */
    GM_STACK_CHECK(&F, 1491u);
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
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; default: goto ma7; }
ma4:;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t11 != ((uint8_t)5ULL)) goto mn10;
            {
                void * t13 = gm_l0;
                gm_tbl8il92l97ut t14 = gm_l1;
                void * t15 = gm_l3;
                gm_tcfa6l7reoosn t16;
                gm_f_ev_discard_8c4a8jg((void *)&t16, (void *)NULL, cx, NULL, t13, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t16))[0] != 0)) {
                    gm_tcfa6l7reoosn t17;
                    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                    ((uint8_t *)&t17)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t17 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t16) + 8)));
                    F.ret = t17;
                    goto gm_fret;
                }
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            }
            {
                void * t19 = gm_l0;
                void * t20 = gm_l4;
                gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 112));
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s76i2uidi0pjj));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(53));
                gm_t76123gfet73g t23;
                gm_f_trap_qh84l9d((void *)&t23, (void *)NULL, cx, NULL, t19, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t23);
                goto gm_fdone;
            }
            goto me3;
mn10:;
ma5:;
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t25 != ((uint8_t)9ULL)) goto mn24;
            {
                void * t27 = gm_l0;
                gm_t2lqu8jsj511d t28 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t27));
                gm_l6 = t28;
            }
            {
                void * t29 = gm_l0;
                void * t30 = gm_l3;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 104));
                gm_b16_8 t32 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t29, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t32;
            }
            {
                void * t33 = gm_l0;
                gm_tbl8il92l97ut t34 = gm_l1;
                void * t35 = gm_l3;
                gm_b16_8 t36 = gm_l7;
                gm_tcfa6l7reoosn t37;
                gm_f_ev_25jfdar((void *)&t37, (void *)NULL, cx, NULL, t33, t34, t35, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t37))[0] != 0)) {
                    gm_tcfa6l7reoosn t38;
                    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                    ((uint8_t *)&t38)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t38 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t37) + 8)));
                    F.ret = t38;
                    goto gm_fret;
                }
                gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
            }
            {
                void * t40 = gm_l0;
                void * t41 = gm_l4;
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 96));
                gm_t53at80pfdus3 t43;
                gm_f_kind_htv6093((void *)&t43, t40, GM_CONV(gm_csguankko4lmhd, t42));
                uint8_t t44;
                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
                if (t47 != ((uint8_t)15ULL)) goto mn46;
                t44 = ((uint8_t)1);
                goto me45;
mn46:;
                t44 = ((uint8_t)0);
                goto me45;
mn48:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 535, 22);
me45:;
                gm_l8 = t44;
            }
            {
                void * t49 = gm_l0;
                void * t50 = gm_l3;
                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 96));
                gm_t53at80pfdus3 t52;
                gm_f_kind_htv6093((void *)&t52, t49, GM_CONV(gm_csguankko4lmhd, t51));
                uint8_t t53;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
                if (t56 != ((uint8_t)15ULL)) goto mn55;
                t53 = ((uint8_t)1);
                goto me54;
mn55:;
                t53 = ((uint8_t)0);
                goto me54;
mn57:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 536, 24);
me54:;
                gm_l9 = t53;
            }
            {
                uint8_t t59 = gm_l8;
                if (!t59) goto ie58;
                uint8_t t60 = gm_l9;
                uint8_t t61 = (uint8_t)!t60;
                if (!t61) goto ie58;
                {
                    void * t63 = gm_l0;
                    gm_b16_8 t64 = gm_l7;
                    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                    uint64_t t66 = gm_f_ptr_of_b77b4c9(t63, t65);
                    gm_l10 = t66;
                }
                {
                    void * t68 = gm_l0;
                    uint64_t t69 = gm_l10;
                    gm_t9805qtonbsag t70 = gm_f_fn_from_addr_7a6bpqd(t68, t69);
                    uint8_t t71;
                    if (((((uint8_t *)&t70))[4] != 0)) goto mn73;
                    t71 = ((uint8_t)1);
                    goto me72;
mn73:;
                    t71 = ((uint8_t)0);
                    goto me72;
mn74:;
                    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 539, 20);
me72:;
                    if (!t71) goto ie67;
                    {
                        void * t76 = gm_l0;
                        void * t77 = gm_l4;
                        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 112));
                        gm_b16_8 t79;
                        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_ss1mi142k161m));
                        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(40));
                        gm_t76123gfet73g t80;
                        gm_f_trap_qh84l9d((void *)&t80, (void *)NULL, cx, NULL, t76, t78, t79);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t80);
                        goto gm_fdone;
                    }
                    goto ix67;
ie67:;
ix67:;
                }
                {
                    void * t81 = gm_l0;
                    gm_b16_8 t82 = gm_l5;
                    uint64_t t83 = gm_l10;
                    gm_f_put_word_tfn443i(t81, t82, ((uint64_t)0ULL), t83);
                }
                {
                    void * t84 = gm_l0;
                    gm_b16_8 t85 = gm_l5;
                    gm_f_put_word_tfn443i(t84, t85, ((uint64_t)1ULL), ((uint64_t)0ULL));
                }
                {
                    void * t86 = gm_l0;
                    gm_b16_8 t87 = gm_l5;
                    uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t89 = (uint64_t)t88;
                    gm_f_put_word_tfn443i(t86, t87, ((uint64_t)2ULL), t89);
                }
                goto ix58;
ie58:;
                uint8_t t91 = gm_l8;
                uint8_t t92 = (uint8_t)!t91;
                if (!t92) goto ie90;
                uint8_t t93 = gm_l9;
                if (!t93) goto ie90;
                {
                    void * t95 = gm_l0;
                    gm_b16_8 t96 = gm_l5;
                    void * t97 = gm_l0;
                    gm_b16_8 t98 = gm_l7;
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    uint64_t t100 = gm_f_word_o2rcrhs(t97, t99, ((uint64_t)0ULL));
                    gm_f_put_ptr_188p3mm(t95, t96, t100);
                }
                goto ix90;
ie90:;
                {
                    gm_b16_8 t102 = gm_l5;
                    gm_b16_8 t103 = gm_l7;
                    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                    gm_f_copy_from_f7j6p05(t102, t104);
                }
ix90:;
ix58:;
            }
            {
                void * t105 = gm_l0;
                gm_t2lqu8jsj511d t106 = gm_l6;
                gm_f_release_jn5kqdi(t105, GM_CONV(gm_csc7lc6jlkqkuq, t106));
            }
            goto me3;
mn24:;
ma6:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t110 != ((uint8_t)6ULL)) goto pa109;
            goto po108;
pa109:;
            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t112 != ((uint8_t)8ULL)) goto pa111;
            goto po108;
pa111:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t114 != ((uint8_t)10ULL)) goto pa113;
            goto po108;
pa113:;
            goto mn107;
po108:;
            void * t115 = gm_l0;
            gm_tbl8il92l97ut t116 = gm_l1;
            void * t117 = gm_l3;
            gm_b16_8 t118 = gm_l5;
            gm_tcfa6l7reoosn t119;
            gm_f_ev_25jfdar((void *)&t119, (void *)NULL, cx, NULL, t115, t116, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t119))[0] != 0)) {
                gm_tcfa6l7reoosn t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                ((uint8_t *)&t120)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t120 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t119) + 8)));
                F.ret = t120;
                goto gm_fret;
            }
            gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 8));
            goto me3;
mn107:;
ma7:;
            {
                void * t124 = gm_l0;
                gm_t2lqu8jsj511d t125 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t124));
                gm_l11 = t125;
            }
            {
                void * t126 = gm_l0;
                void * t127 = gm_l3;
                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t127) + 104));
                gm_b16_8 t129 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t126, t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t129;
            }
            {
                void * t130 = gm_l0;
                gm_tbl8il92l97ut t131 = gm_l1;
                void * t132 = gm_l3;
                gm_b16_8 t133 = gm_l12;
                gm_tcfa6l7reoosn t134;
                gm_f_ev_25jfdar((void *)&t134, (void *)NULL, cx, NULL, t130, t131, t132, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t134))[0] != 0)) {
                    gm_tcfa6l7reoosn t135;
                    GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                    ((uint8_t *)&t135)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t135 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t134) + 8)));
                    F.ret = t135;
                    goto gm_fret;
                }
                gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 8));
            }
            {
                void * t137 = gm_l0;
                void * t138 = gm_l4;
                gm_tl874t2atnlh3 t139 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t138) + 112));
                gm_tmn7n0n9raou5 t140 = gm_l2;
                void * t141 = gm_l3;
                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + 96));
                void * t143 = gm_l4;
                gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t143) + 96));
                gm_b16_8 t145 = gm_l12;
                gm_b16_8 t146 = gm_l5;
                gm_tcfa6l7reoosn t147;
                gm_f_coerce_bytes_9kf3u24((void *)&t147, (void *)NULL, cx, NULL, t137, t139, t140, t142, t144, t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t147))[0] != 0)) {
                    gm_tcfa6l7reoosn t148;
                    GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                    ((uint8_t *)&t148)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t148 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t147) + 8)));
                    F.ret = t148;
                    goto gm_fret;
                }
                gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 8));
            }
            {
                void * t150 = gm_l0;
                gm_t2lqu8jsj511d t151 = gm_l11;
                gm_f_release_jn5kqdi(t150, GM_CONV(gm_csc7lc6jlkqkuq, t151));
            }
            goto me3;
mn122:;
ma8:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 526, 5);
me3:;
        }
    }
    gm_b0_1 t152;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t152);
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
inline __attribute__((aligned(4))) void gm_f_cond_v8r7708(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_cond_v8r7708 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tbl8il92l97ut t2 = gm_l1;
    void * t3 = gm_l2;
    gm_t833u93mr4iob t4;
    gm_f_ew_c5kil07((void *)&t4, (void *)NULL, cx, NULL, t1, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) {
        gm_t1ec8br9ddkav t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
        ((uint8_t *)&t5)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t5 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t4) + 8)));
        F.ret = t5;
        goto gm_fret;
    }
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
    uint8_t t7 = (!(t6 == ((uint64_t)0ULL)));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t7);
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
__attribute__((aligned(4))) void gm_f_else__7epkk2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, void * a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_else__7epkk2p F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* lhs */
    gm_t9805qtonbsag gm_l3; /* capture */
    void * gm_l4; /* rhs */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* dst */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    uint8_t gm_l9; /* present */
    gm_t88rcucg1b7uk gm_l10; /* payload_ty */
    gm_b16_8 gm_l11; /* payload */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint8_t gm_l14; /* is_err */
    GM_STACK_CHECK(&F, 1314u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l7 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l8;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_tcfa6l7reoosn t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t13) + 8)));
                F.ret = t14;
                goto gm_fret;
            }
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
        }
        {
            gm_l9 = ((uint8_t)0);
        }
        {
            void * t16 = gm_l2;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 96));
            gm_l10 = t17;
        }
        {
            gm_b16_8 t18 = gm_f_empty_slice_ijtp9lo();
            gm_l11 = t18;
        }
        {
            void * t19 = gm_l0;
            void * t20 = gm_l2;
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 96));
            gm_t53at80pfdus3 t22;
            gm_f_kind_htv6093((void *)&t22, t19, GM_CONV(gm_csguankko4lmhd, t21));
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma25; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; default: goto ma26; }
ma24:;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t30 != ((uint8_t)13ULL)) goto mn29;
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t22) + 8));
            gm_l12 = t31;
            {
                void * t33 = gm_l0;
                void * t34 = gm_l2;
                gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 96));
                gm_b16_8 t36 = gm_l8;
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                uint8_t t38 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t33, t35, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t38;
            }
            {
                gm_t88rcucg1b7uk t39 = gm_l12;
                gm_l10 = t39;
            }
            {
                uint8_t t41 = gm_l9;
                if (!t41) goto ie40;
                {
                    void * t43 = gm_l0;
                    void * t44 = gm_l2;
                    gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 96));
                    gm_b16_8 t46 = gm_l8;
                    gm_b16_8 t47 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t43, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t47;
                }
                goto ix40;
ie40:;
ix40:;
            }
            goto me23;
mn29:;
ma25:;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
            if (t49 != ((uint8_t)14ULL)) goto mn48;
            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t22) + 8) + 4));
            gm_l13 = t50;
            {
                void * t52 = gm_l0;
                void * t53 = gm_l2;
                gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 96));
                gm_b16_8 t55 = gm_l8;
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                uint8_t t57 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t52, t54, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t57;
            }
            {
                void * t58 = gm_l0;
                void * t59 = gm_l2;
                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + 96));
                gm_b16_8 t61 = gm_l8;
                gm_b16_8 t62 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t58, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t62;
            }
            {
                uint8_t t64 = gm_l14;
                if (!t64) goto ie63;
                uint8_t t65;
                if (((((uint8_t *)&gm_l3))[4] != 0)) goto mn67;
                t65 = ((uint8_t)0);
                goto me66;
mn67:;
                t65 = ((uint8_t)1);
                goto me66;
mn68:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 460, 27);
me66:;
                if (!t65) goto ie63;
                {
                    void * t70 = gm_l0;
                    void * t71 = gm_l5;
                    gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 112));
                    void * t73 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                    gm_t9805qtonbsag t74 = gm_l3;
                    uint32_t t75;
                    if (((((uint8_t *)&t74))[4] != 0)) {
                        uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)&t74) + 0));
                        t75 = t76;
                    } else {
                        t75 = ((uint32_t)0ULL);
                    }
                    gm_b16_8 t77 = gm_l11;
                    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                    gm_tcfa6l7reoosn t79;
                    gm_f_set_local_ug1vgfu((void *)&t79, (void *)NULL, cx, NULL, t70, t72, t73, t75, t78);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t79))[0] != 0)) {
                        gm_tcfa6l7reoosn t80;
                        GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                        ((uint8_t *)&t80)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t80 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t79) + 8)));
                        F.ret = t80;
                        goto gm_fret;
                    }
                    gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 8));
                }
                goto ix63;
ie63:;
ix63:;
            }
            {
                uint8_t t82 = gm_l14;
                uint8_t t83 = (uint8_t)!t82;
                gm_l9 = t83;
            }
            {
                gm_t88rcucg1b7uk t84 = gm_l13;
                gm_l10 = t84;
            }
            goto me23;
mn48:;
ma26:;
            void * t86 = gm_l0;
            void * t87 = gm_l5;
            gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 112));
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_srgr2i687arao));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(74));
            gm_t76123gfet73g t90;
            gm_f_trap_qh84l9d((void *)&t90, (void *)NULL, cx, NULL, t86, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t90);
            goto gm_fdone;
            goto me23;
mn85:;
ma27:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 451, 5);
me23:;
        }
        {
            uint8_t t92 = gm_l9;
            uint8_t t93 = (uint8_t)!t92;
            if (!t93) goto ie91;
            {
                void * t95 = gm_l0;
                gm_t2lqu8jsj511d t96 = gm_l7;
                gm_f_release_jn5kqdi(t95, GM_CONV(gm_csc7lc6jlkqkuq, t96));
            }
            {
                void * t97 = gm_l0;
                gm_tbl8il92l97ut t98 = gm_l1;
                void * t99 = gm_l4;
                gm_b16_8 t100 = gm_l6;
                gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t97, t98, t99, t100);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix91;
ie91:;
ix91:;
        }
        {
            void * t102 = gm_l5;
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 96));
            gm_t88rcucg1b7uk t104 = gm_l10;
            uint8_t t105 = (gm_eq9m6mvtmgde(((uint8_t *)&t103), ((uint8_t *)&t104)));
            if (!t105) goto ie101;
            {
                gm_b16_8 t107 = gm_l6;
                gm_b16_8 t108 = gm_l11;
                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                gm_f_copy_from_f7j6p05(t107, t109);
            }
            goto ix101;
ie101:;
            void * t111 = gm_l5;
            gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 96));
            void * t113 = gm_l2;
            gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t113) + 96));
            uint8_t t115 = (gm_eq9m6mvtmgde(((uint8_t *)&t112), ((uint8_t *)&t114)));
            if (!t115) goto ie110;
            {
                gm_b16_8 t117 = gm_l6;
                gm_b16_8 t118 = gm_l8;
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                gm_f_copy_from_f7j6p05(t117, t119);
            }
            goto ix110;
ie110:;
            void * t121 = gm_l0;
            void * t122 = gm_l5;
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t122) + 96));
            gm_t53at80pfdus3 t124;
            gm_f_kind_htv6093((void *)&t124, t121, GM_CONV(gm_csguankko4lmhd, t123));
            uint8_t t125;
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t124) + 0));
            if (t128 != ((uint8_t)13ULL)) goto mn127;
            t125 = ((uint8_t)1);
            goto me126;
mn127:;
            t125 = ((uint8_t)0);
            goto me126;
mn129:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 474, 15);
me126:;
            if (!t125) goto ie120;
            {
                void * t131 = gm_l0;
                void * t132 = gm_l5;
                gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t132) + 96));
                gm_b16_8 t134 = gm_l6;
                gm_b16_8 t135 = gm_l11;
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                gm_b0_1 t137 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t131, t133, t134, t136);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix120;
ie120:;
            {
                void * t139 = gm_l0;
                void * t140 = gm_l5;
                gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t140) + 96));
                gm_b16_8 t142 = gm_l6;
                gm_b16_8 t143 = gm_l11;
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                gm_b0_1 t145 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t139, t141, t142, ((uint8_t)0), t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix120:;
ix110:;
ix101:;
        }
        {
            void * t146 = gm_l0;
            gm_t2lqu8jsj511d t147 = gm_l7;
            gm_f_release_jn5kqdi(t146, GM_CONV(gm_csc7lc6jlkqkuq, t147));
        }
    }
    gm_b0_1 t148;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t148);
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
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_ev_25jfdar F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_b16_8 gm_l3; /* dst */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* off */
    void * gm_l6; /* fr */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* o */
    uint32_t gm_l9; /* f */
    gm_b16_8 gm_l10; /* args */
    void * gm_l11; /* b */
    gm_trl2khmuphncd gm_l12; /* x */
    void * gm_l13; /* el */
    uint32_t gm_l14; /* levels */
    void * gm_l15; /* value */
    GM_STACK_CHECK(&F, 1399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l2;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma9; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; case ((uint8_t)59ULL): goto ma12; case ((uint8_t)60ULL): goto ma12; case ((uint8_t)61ULL): goto ma12; case ((uint8_t)62ULL): goto ma12; default: goto ma12; }
ma5:;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t16 != ((uint8_t)6ULL)) goto mn15;
            uint32_t t17 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l4 = t17;
            uint64_t t18 = GM_LD(uint64_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l5 = t18;
            {
                void * t20 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                gm_l6 = t20;
            }
            {
                uint32_t t21 = gm_l4;
                uint64_t t22 = (uint64_t)t21;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                gm_l7 = t23;
            }
            {
                uint64_t t25 = gm_l7;
                uint64_t t26 = (uintptr_t)t25;
                void * t27 = gm_l6;
                uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 64) + 8);
                if (t26 >= (uintptr_t)(t28)) gm_trap_index(t26, t28, gm_src0jbci4ff2esa, 11, 16);
                uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t27) + 64)) + t26 * 8));
                uint8_t t30 = (!(t29 == ((uint64_t)0ULL)));
                if (!t30) goto ie24;
                {
                    void * t32 = gm_l0;
                    void * t33 = gm_l2;
                    gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 112));
                    uint64_t t35 = gm_l7;
                    uint64_t t36 = (uintptr_t)t35;
                    void * t37 = gm_l6;
                    uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 64) + 8);
                    if (t36 >= (uintptr_t)(t38)) gm_trap_index(t36, t38, gm_src0jbci4ff2esa, 11, 62);
                    uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t37) + 64)) + t36 * 8));
                    uint64_t t40 = gm_l7;
                    uint64_t t41 = (uintptr_t)t40;
                    void * t42 = gm_l6;
                    void * t43 = GM_LD(void *, (((uint8_t *)t42) + 16));
                    uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 104) + 8);
                    if (t41 >= (uintptr_t)(t44)) gm_trap_index(t41, t44, gm_src0jbci4ff2esa, 11, 73);
                    uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 104)) + t41 * 8));
                    gm_b16_8 t46 = gm_l3;
                    gm_f_load_at_6u81jcc((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t32, t34, t39, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix24;
ie24:;
ix24:;
            }
            {
                uint64_t t47 = gm_l5;
                uint64_t t48 = gm_f_us_cuv01fp(t47);
                gm_l8 = t48;
            }
            {
                gm_b16_8 t49 = gm_l3;
                void * t50 = gm_l6;
                uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)t50) + 48) + 8);
                void * t52 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t50) + 48)));
                uint64_t t53 = gm_l8;
                uint64_t t54 = (uintptr_t)t53;
                uint64_t t55 = gm_l8;
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r57; bool o58 = __builtin_add_overflow(t55, t56, &r57);
                if (o58) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 13, 39);
                uint64_t t59 = r57;
                uint64_t t60 = (uintptr_t)t59;
                if (t54 > t60 || t60 > (uintptr_t)t51) gm_trap_slice(t54, t60, t51, gm_src0jbci4ff2esa, 13, 27);
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t52 + t54 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60 - t54));
                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                gm_f_copy_from_f7j6p05(t49, t62);
            }
            goto me4;
mn15:;
ma6:;
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t64 != ((uint8_t)35ULL)) goto mn63;
            uint32_t t65 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l9 = t65;
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l10 = t66;
            void * t67 = gm_l0;
            gm_tbl8il92l97ut t68 = gm_l1;
            uint32_t t69 = gm_l9;
            gm_b16_8 t70 = gm_l10;
            void * t71 = gm_l2;
            gm_b16_8 t72 = gm_l3;
            gm_f_call_node_a1b30k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t67, t68, t69, t70, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn63:;
ma7:;
            uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t74 != ((uint8_t)51ULL)) goto mn73;
            void * t75 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l11 = t75;
            void * t76 = gm_l0;
            gm_tbl8il92l97ut t77 = gm_l1;
            void * t78 = gm_l11;
            gm_b16_8 t79 = gm_l3;
            gm_f_block_5b89ev3((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t76, t77, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn73:;
ma8:;
            uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t81 != ((uint8_t)52ULL)) goto mn80;
            gm_trl2khmuphncd t82 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t3) + 0) + 8));
            gm_l12 = t82;
            {
                void * t85 = gm_l0;
                gm_tbl8il92l97ut t86 = gm_l1;
                gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_t1ec8br9ddkav t88;
                gm_f_conds_30ust77((void *)&t88, (void *)NULL, cx, NULL, t85, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t88))[0] != 0)) {
                    gm_tcfa6l7reoosn t89;
                    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                    ((uint8_t *)&t89)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t89 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t88) + 8)));
                    F.ret = t89;
                    goto gm_fret;
                }
                uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t88) + 8));
                if (!t90) goto ie84;
                {
                    void * t92 = gm_l0;
                    gm_tbl8il92l97ut t93 = gm_l1;
                    void * t94 = GM_LD(void *, (((uint8_t *)&gm_l12) + 16));
                    gm_b16_8 t95 = gm_l3;
                    gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t92, t93, t94, t95);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix84;
ie84:;
ix84:;
            }
            {
                void * t97 = GM_LD(void *, (((uint8_t *)&gm_l12) + 24));
                if (!(GM_LD(void *, ((uint8_t *)&t97)) != 0)) goto ie96;
                void * t98 = GM_LD(void *, ((uint8_t *)&t97));
                gm_l13 = t98;
                {
                    void * t100 = gm_l0;
                    gm_tbl8il92l97ut t101 = gm_l1;
                    void * t102 = gm_l13;
                    gm_b16_8 t103 = gm_l3;
                    gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t100, t101, t102, t103);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix96;
ie96:;
ix96:;
            }
            goto me4;
mn80:;
ma9:;
            uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t105 != ((uint8_t)56ULL)) goto mn104;
            uint32_t t106 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l14 = t106;
            void * t107 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l15 = t107;
            void * t108 = gm_l0;
            gm_tbl8il92l97ut t109 = gm_l1;
            uint32_t t110 = gm_l14;
            void * t111 = gm_l15;
            void * t112 = gm_l2;
            gm_f_ev_return_8ggbvjj((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t108, t109, t110, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn104:;
ma10:;
            uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t116 != ((uint8_t)0ULL)) goto pa115;
            goto po114;
pa115:;
            uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t118 != ((uint8_t)21ULL)) goto pa117;
            goto po114;
pa117:;
            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t120 != ((uint8_t)23ULL)) goto pa119;
            goto po114;
pa119:;
            uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t122 != ((uint8_t)24ULL)) goto pa121;
            goto po114;
pa121:;
            uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t124 != ((uint8_t)25ULL)) goto pa123;
            goto po114;
pa123:;
            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t126 != ((uint8_t)32ULL)) goto pa125;
            goto po114;
pa125:;
            uint8_t t128 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t128 != ((uint8_t)26ULL)) goto pa127;
            goto po114;
pa127:;
            uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t130 != ((uint8_t)27ULL)) goto pa129;
            goto po114;
pa129:;
            uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t132 != ((uint8_t)28ULL)) goto pa131;
            goto po114;
pa131:;
            uint8_t t134 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t134 != ((uint8_t)29ULL)) goto pa133;
            goto po114;
pa133:;
            uint8_t t136 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t136 != ((uint8_t)22ULL)) goto pa135;
            goto po114;
pa135:;
            goto mn113;
po114:;
            void * t137 = gm_l0;
            gm_b16_8 t138 = gm_l3;
            void * t139 = gm_l0;
            gm_tbl8il92l97ut t140 = gm_l1;
            void * t141 = gm_l2;
            gm_t833u93mr4iob t142;
            gm_f_ew_c5kil07((void *)&t142, (void *)NULL, cx, NULL, t139, t140, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t142))[0] != 0)) {
                gm_tcfa6l7reoosn t143;
                GM_ST(uint8_t, (uint8_t *)&t143 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t143 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t143 + 4, 0);
                ((uint8_t *)&t143)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t143 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t142) + 8)));
                F.ret = t143;
                goto gm_fret;
            }
            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&t142) + 8));
            gm_f_wr_word_npte9fs(t137, t138, t144);
            goto me4;
mn113:;
ma11:;
            uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t148 != ((uint8_t)10ULL)) goto pa147;
            goto po146;
pa147:;
            uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t150 != ((uint8_t)12ULL)) goto pa149;
            goto po146;
pa149:;
            uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t152 != ((uint8_t)9ULL)) goto pa151;
            goto po146;
pa151:;
            goto mn145;
po146:;
            {
                void * t155 = gm_l2;
                uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)t155) + 124));
                if (!t156) goto ie154;
                {
                    void * t158 = gm_l0;
                    gm_tbl8il92l97ut t159 = gm_l1;
                    void * t160 = gm_l2;
                    gm_tl1r7a2fn1ev6 t161;
                    gm_f_place_47rfqe5((void *)&t161, (void *)NULL, cx, NULL, t158, t159, t160, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t161))[0] != 0)) {
                        gm_tcfa6l7reoosn t162;
                        GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                        ((uint8_t *)&t162)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t162 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t161) + 8)));
                        F.ret = t162;
                        goto gm_fret;
                    }
                    F.l16 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t161) + 8));
                }
                {
                    void * t163 = gm_l0;
                    void * t164 = gm_l2;
                    gm_tl874t2atnlh3 t165 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t164) + 112));
                    void * t166 = gm_l2;
                    gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t166) + 96));
                    gm_b16_8 t168 = gm_l3;
                    gm_f_read_place_fcc38bb((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t163, t165, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l16)), t167, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix154;
ie154:;
ix154:;
            }
            {
                void * t169 = gm_l0;
                gm_tbl8il92l97ut t170 = gm_l1;
                void * t171 = gm_l2;
                gm_b16_8 t172 = gm_l3;
                gm_f_ev_slow_hdak0k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t169, t170, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto me4;
mn145:;
ma12:;
            void * t174 = gm_l0;
            gm_tbl8il92l97ut t175 = gm_l1;
            void * t176 = gm_l2;
            gm_b16_8 t177 = gm_l3;
            gm_f_ev_slow_hdak0k9((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t174, t175, t176, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me4;
mn173:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 7, 5);
me4:;
        }
    }
    gm_b0_1 t178;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t178);
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
inline __attribute__((aligned(4))) void gm_f_ev_args_j1qj6gl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_ev_args_j1qj6gl F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* vals */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* x */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 363u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            {
                gm_b16_8 t4 = gm_l2;
                gm_l4 = t4;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t6 = gm_l5;
                    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t8 = (t6 < t7);
                    if (!t8) goto lb5;
                    {
                        uint64_t t11 = gm_l5;
                        uint64_t t12 = (uintptr_t)t11;
                        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        void * t14 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 8));
                        void * t17 = GM_LD(void *, ((uint8_t *)&t14));
                        gm_l6 = t17;
                        {
                            void * t19 = gm_l0;
                            void * t20 = gm_l6;
                            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 104));
                            gm_b16_8 t22 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t19, t21);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_l7 = t22;
                        }
                        {
                            void * t23 = gm_l0;
                            gm_tbl8il92l97ut t24 = gm_l1;
                            void * t25 = gm_l6;
                            gm_b16_8 t26 = gm_l7;
                            gm_tcfa6l7reoosn t27;
                            gm_f_ev_25jfdar((void *)&t27, (void *)NULL, cx, NULL, t23, t24, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t27))[0] != 0)) {
                                gm_tcfa6l7reoosn t28;
                                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                                ((uint8_t *)&t28)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t27) + 8)));
                                F.ret = t28;
                                goto gm_fret;
                            }
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
                        }
                        {
                            void * t30 = gm_l3;
                            gm_b16_8 t31 = gm_l7;
                            gm_tftb1odc739t5 t32 = gm_f_push_rluch9r(t30, t31);
                            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_src0jbci4ff2esa, 570, 9);
                            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
                        }
                        goto me15;
mn16:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 567, 5);
me15:;
                    }
lc5:;
                    {
                        uint64_t t35 = gm_l5;
                        uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                        uint64_t t38 = r36;
                        gm_l5 = t38;
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
    }
    gm_b0_1 t39;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t39);
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
__attribute__((aligned(4))) void gm_f_ev_builtin_86a9q3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_ev_builtin_86a9q3q F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_b16_8 gm_l6; /* msg */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    gm_t9qkh0rsooc8d gm_l9; /* sop */
    gm_t2lqu8jsj511d gm_l10; /* mk */
    gm_t88rcucg1b7uk gm_l12; /* t0 */
    gm_t88rcucg1b7uk gm_l13; /* t1 */
    GM_STACK_CHECK(&F, 1813u);
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
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma12; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma9; case ((uint8_t)37ULL): goto ma9; case ((uint8_t)38ULL): goto ma9; case ((uint8_t)39ULL): goto ma9; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; case ((uint8_t)51ULL): goto ma12; case ((uint8_t)52ULL): goto ma12; case ((uint8_t)53ULL): goto ma12; case ((uint8_t)54ULL): goto ma12; case ((uint8_t)55ULL): goto ma12; case ((uint8_t)56ULL): goto ma12; case ((uint8_t)57ULL): goto ma12; case ((uint8_t)58ULL): goto ma12; case ((uint8_t)59ULL): goto ma12; case ((uint8_t)60ULL): goto ma12; case ((uint8_t)61ULL): goto ma4; case ((uint8_t)62ULL): goto ma12; case ((uint8_t)63ULL): goto ma6; case ((uint8_t)64ULL): goto ma12; case ((uint8_t)65ULL): goto ma12; case ((uint8_t)66ULL): goto ma12; case ((uint8_t)67ULL): goto ma12; case ((uint8_t)68ULL): goto ma12; case ((uint8_t)69ULL): goto ma12; case ((uint8_t)70ULL): goto ma12; case ((uint8_t)71ULL): goto ma12; case ((uint8_t)72ULL): goto ma8; case ((uint8_t)73ULL): goto ma7; case ((uint8_t)74ULL): goto ma12; case ((uint8_t)75ULL): goto ma12; case ((uint8_t)76ULL): goto ma12; case ((uint8_t)77ULL): goto ma12; case ((uint8_t)78ULL): goto ma12; case ((uint8_t)79ULL): goto ma10; case ((uint8_t)80ULL): goto ma11; case ((uint8_t)81ULL): goto ma5; default: goto ma12; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)61ULL)) goto mn15;
            {
                void * t19 = gm_l0;
                uint8_t t20 = gm_f_fast_23em7sv(t19);
                if (!t20) goto ie18;
                {
                    gm_b0_1 t22;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t22);
                    goto gm_fdone;
                }
                goto ix18;
ie18:;
ix18:;
            }
            {
                void * t24 = gm_l0;
                gm_tbl8il92l97ut t25 = gm_l1;
                uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_src0jbci4ff2esa, 578, 30);
                void * t28 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t26 * 8));
                gm_t1ec8br9ddkav t29;
                gm_f_cond_v8r7708((void *)&t29, (void *)NULL, cx, NULL, t24, t25, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t29))[0] != 0)) {
                    gm_tcfa6l7reoosn t30;
                    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                    ((uint8_t *)&t30)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t30 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t29) + 8)));
                    F.ret = t30;
                    goto gm_fret;
                }
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t29) + 8));
                if (!t31) goto ie23;
                {
                    gm_b0_1 t33;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t33);
                    goto gm_fdone;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_spi8n2atkg603));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(16));
                gm_l6 = t34;
            }
            {
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t37 = (t36 > ((uint64_t)1ULL));
                if (!t37) goto ie35;
                {
                    void * t39 = gm_l0;
                    gm_t2lqu8jsj511d t40 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t39));
                    gm_l7 = t40;
                }
                {
                    void * t41 = gm_l0;
                    uint64_t t42 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src0jbci4ff2esa, 582, 28);
                    void * t44 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t42 * 8));
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 104));
                    gm_b16_8 t46 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t41, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t46;
                }
                {
                    void * t47 = gm_l0;
                    gm_tbl8il92l97ut t48 = gm_l1;
                    uint64_t t49 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_src0jbci4ff2esa, 583, 29);
                    void * t51 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 8));
                    gm_b16_8 t52 = gm_l8;
                    gm_tcfa6l7reoosn t53;
                    gm_f_ev_25jfdar((void *)&t53, (void *)NULL, cx, NULL, t47, t48, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t53))[0] != 0)) {
                        gm_tcfa6l7reoosn t54;
                        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                        ((uint8_t *)&t54)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t54 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t53) + 8)));
                        F.ret = t54;
                        goto gm_fret;
                    }
                    gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 8));
                }
                {
                    void * t56 = gm_l0;
                    void * t57 = gm_l4;
                    gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 112));
                    gm_b16_8 t59 = gm_l8;
                    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                    gm_td0ept12e7q8b t61;
                    gm_f_read_string_ttuk4gr((void *)&t61, (void *)NULL, cx, NULL, t56, t58, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t61))[0] != 0)) {
                        gm_tcfa6l7reoosn t62;
                        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                        ((uint8_t *)&t62)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t62 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t61) + 8)));
                        F.ret = t62;
                        goto gm_fret;
                    }
                    gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                    gm_l6 = t63;
                }
                {
                    void * t64 = gm_l0;
                    gm_t2lqu8jsj511d t65 = gm_l7;
                    gm_f_release_jn5kqdi(t64, GM_CONV(gm_csc7lc6jlkqkuq, t65));
                }
                goto ix35;
ie35:;
ix35:;
            }
            {
                uint8_t t67 = (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8) == 16) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l6)), gm_spi8n2atkg603, 16) == 0));
                if (!t67) goto ie66;
                {
                    gm_b16_8 t69 = gm_l6;
                    gm_tnpifv20irnph t70;
                    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                    gm_tabb710d9mq2e t71;
                    gm_f_format_e1vik51((void *)&t71, (void *)NULL, cx, NULL, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_src0jbci4ff2esa, 587, 54);
                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                    gm_l6 = t73;
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                void * t74 = gm_l0;
                void * t75 = gm_l4;
                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 112));
                gm_b16_8 t77 = gm_l6;
                gm_t76123gfet73g t78;
                gm_f_trap_qh84l9d((void *)&t78, (void *)NULL, cx, NULL, t74, t76, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t78);
                goto gm_fdone;
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t80 != ((uint8_t)81ULL)) goto mn79;
            gm_b0_1 t81;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t81);
            goto gm_fdone;
            goto me3;
mn79:;
ma6:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t83 != ((uint8_t)63ULL)) goto mn82;
            void * t84 = gm_l0;
            void * t85 = gm_l4;
            gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t85) + 112));
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sd3b7a2bnmpef));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(24));
            gm_t76123gfet73g t88;
            gm_f_trap_qh84l9d((void *)&t88, (void *)NULL, cx, NULL, t84, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t88);
            goto gm_fdone;
            goto me3;
mn82:;
ma7:;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t90 != ((uint8_t)73ULL)) goto mn89;
            void * t91 = gm_l0;
            gm_tbl8il92l97ut t92 = gm_l1;
            gm_b16_8 t93 = gm_l3;
            void * t94 = gm_l4;
            gm_b16_8 t95 = gm_l5;
            gm_f_fiber_builtin_rpc9l9s((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t91, t92, t93, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn89:;
ma8:;
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t97 != ((uint8_t)72ULL)) goto mn96;
            gm_t9qkh0rsooc8d t98 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l9 = t98;
            void * t99 = gm_l0;
            gm_tbl8il92l97ut t100 = gm_l1;
            gm_t9qkh0rsooc8d t101 = gm_l9;
            gm_b16_8 t102 = gm_l3;
            void * t103 = gm_l4;
            gm_b16_8 t104 = gm_l5;
            gm_f_sys_call_6jqv32o((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t99, t100, t101, t102, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn96:;
ma9:;
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t108 != ((uint8_t)36ULL)) goto pa107;
            goto po106;
pa107:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t110 != ((uint8_t)37ULL)) goto pa109;
            goto po106;
pa109:;
            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t112 != ((uint8_t)38ULL)) goto pa111;
            goto po106;
pa111:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t114 != ((uint8_t)39ULL)) goto pa113;
            goto po106;
pa113:;
            goto mn105;
po106:;
            void * t115 = gm_l0;
            gm_tbl8il92l97ut t116 = gm_l1;
            gm_tvdg94bk7aa22 t117 = gm_l2;
            gm_b16_8 t118 = gm_l3;
            void * t119 = gm_l4;
            gm_b16_8 t120 = gm_l5;
            gm_f_get_builtin_s6t151o((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t115, t116, t117, t118, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn105:;
ma10:;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t122 != ((uint8_t)79ULL)) goto mn121;
            void * t123 = gm_l0;
            void * t124 = gm_l4;
            gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t124) + 112));
            gm_b16_8 t126;
            GM_ST(void *, (uint8_t *)&t126, (void *)(gm_snh5ve0osdp08));
            GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(6));
            gm_t76123gfet73g t127;
            gm_f_unsupported_70vvk1e((void *)&t127, (void *)NULL, cx, NULL, t123, t125, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t127);
            goto gm_fdone;
            goto me3;
mn121:;
ma11:;
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t129 != ((uint8_t)80ULL)) goto mn128;
            gm_b0_1 t130;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t130);
            goto gm_fdone;
            goto me3;
mn128:;
ma12:;
            goto me3;
mn131:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 575, 5);
me3:;
        }
        {
            void * t133 = gm_l0;
            gm_t2lqu8jsj511d t134 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t133));
            gm_l10 = t134;
        }
        {
            gm_f_list_fufbgf7((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t136 = gm_l0;
            gm_tbl8il92l97ut t137 = gm_l1;
            gm_b16_8 t138 = gm_l3;
            void * t139 = (void *)((uint8_t *)&F.l11);
            gm_tcfa6l7reoosn t140;
            gm_f_ev_args_j1qj6gl((void *)&t140, (void *)NULL, cx, NULL, t136, t137, t138, t139);
            if (gm_unw.kind) {
                goto gm_ld2_1;
            }
            if (((((uint8_t *)&t140))[0] != 0)) {
                gm_tcfa6l7reoosn t141;
                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                ((uint8_t *)&t141)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t141 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t140) + 8)));
                F.ret = t141;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld2_1;
            }
            gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 8));
        }
        {
            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t147 != ((uint8_t)74ULL)) goto pa146;
            goto po145;
pa146:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t149 != ((uint8_t)75ULL)) goto pa148;
            goto po145;
pa148:;
            uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t151 != ((uint8_t)76ULL)) goto pa150;
            goto po145;
pa150:;
            uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t153 != ((uint8_t)77ULL)) goto pa152;
            goto po145;
pa152:;
            uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t155 != ((uint8_t)78ULL)) goto pa154;
            goto po145;
pa154:;
            uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t157 != ((uint8_t)64ULL)) goto pa156;
            goto po145;
pa156:;
            uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t159 != ((uint8_t)65ULL)) goto pa158;
            goto po145;
pa158:;
            uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t161 != ((uint8_t)66ULL)) goto pa160;
            goto po145;
pa160:;
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t163 != ((uint8_t)67ULL)) goto pa162;
            goto po145;
pa162:;
            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t165 != ((uint8_t)68ULL)) goto pa164;
            goto po145;
pa164:;
            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t167 != ((uint8_t)70ULL)) goto pa166;
            goto po145;
pa166:;
            uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t169 != ((uint8_t)71ULL)) goto pa168;
            goto po145;
pa168:;
            uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t171 != ((uint8_t)69ULL)) goto pa170;
            goto po145;
pa170:;
            uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t173 != ((uint8_t)60ULL)) goto pa172;
            goto po145;
pa172:;
            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t175 != ((uint8_t)58ULL)) goto pa174;
            goto po145;
pa174:;
            uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t177 != ((uint8_t)59ULL)) goto pa176;
            goto po145;
pa176:;
            goto mn144;
po145:;
            void * t178 = gm_l0;
            void * t179 = gm_l4;
            gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t179) + 112));
            gm_tvdg94bk7aa22 t181 = gm_l2;
            gm_b16_8 t182 = gm_l3;
            gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            void * t184 = gm_l4;
            gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t184) + 96));
            gm_b16_8 t186 = gm_l5;
            gm_tcfa6l7reoosn t187;
            gm_f_memory_builtin_9s47dk8((void *)&t187, (void *)NULL, cx, NULL, t178, t180, t181, t182, t183, t185, t186);
            if (gm_unw.kind) {
                goto gm_ld2_1;
            }
            if (((((uint8_t *)&t187))[0] != 0)) {
                gm_tcfa6l7reoosn t188;
                GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
                ((uint8_t *)&t188)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t188 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t187) + 8)));
                F.ret = t188;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld2_1;
            }
            gm_b0_1 t189 = GM_LD(gm_b0_1, (((uint8_t *)&t187) + 8));
            goto me143;
mn144:;
            {
                gm_t88rcucg1b7uk t192;
                uint64_t t194 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t195 = (t194 > ((uint64_t)0ULL));
                if (!t195) goto ie193;
                uint64_t t197 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t198 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t197 >= (uintptr_t)(t198)) gm_trap_index(t197, t198, gm_src0jbci4ff2esa, 606, 37);
                void * t199 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t197 * 8));
                gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t199) + 96));
                gm_t88rcucg1b7uk t201 = t200;
                t192 = t201;
                goto ix193;
ie193:;
                void * t203 = gm_l4;
                gm_t88rcucg1b7uk t204 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t203) + 96));
                gm_t88rcucg1b7uk t205 = t204;
                t192 = t205;
ix193:;
                gm_l12 = t192;
            }
            {
                gm_t88rcucg1b7uk t206;
                uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t209 = (t208 > ((uint64_t)1ULL));
                if (!t209) goto ie207;
                uint64_t t211 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t211 >= (uintptr_t)(t212)) gm_trap_index(t211, t212, gm_src0jbci4ff2esa, 607, 37);
                void * t213 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t211 * 8));
                gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t213) + 96));
                gm_t88rcucg1b7uk t215 = t214;
                t206 = t215;
                goto ix207;
ie207:;
                gm_t88rcucg1b7uk t217 = gm_l12;
                gm_t88rcucg1b7uk t218 = t217;
                t206 = t218;
ix207:;
                gm_l13 = t206;
            }
            {
                void * t219 = gm_l0;
                void * t220 = gm_l4;
                gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t220) + 112));
                gm_tvdg94bk7aa22 t222 = gm_l2;
                void * t223 = gm_l4;
                gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t223) + 96));
                gm_t88rcucg1b7uk t225 = gm_l12;
                gm_t88rcucg1b7uk t226 = gm_l13;
                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t228 = gm_l5;
                gm_tcfa6l7reoosn t229;
                gm_f_apply_builtin_41dffti((void *)&t229, (void *)NULL, cx, NULL, t219, t221, t222, t224, t225, t226, t227, t228);
                if (gm_unw.kind) {
                    goto gm_ld2_1;
                }
                if (((((uint8_t *)&t229))[0] != 0)) {
                    gm_tcfa6l7reoosn t230;
                    GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                    ((uint8_t *)&t230)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t230 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t229) + 8)));
                    F.ret = t230;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld2_1;
                }
                gm_b0_1 t231 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 8));
            }
            goto me143;
mn190:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 603, 5);
me143:;
        }
        {
            void * t232 = gm_l0;
            gm_t2lqu8jsj511d t233 = gm_l10;
            gm_f_release_jn5kqdi(t232, GM_CONV(gm_csc7lc6jlkqkuq, t233));
        }
        {
            void * t234 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t234);
        }
        goto gm_ls235;
gm_ld2_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t236 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_tq06jtm(t236);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls235:;
    }
    gm_b0_1 t237;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t237);
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
inline __attribute__((aligned(4))) void gm_f_ev_discard_8c4a8jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ev_discard_8c4a8jg F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t2lqu8jsj511d gm_l3; /* mk */
    gm_b16_8 gm_l4; /* b */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t4 = gm_l2;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 104));
            uint8_t t6 = (t5 <= ((uint64_t)8ULL));
            if (!t6) goto ie3;
            {
                {
                    void * t9 = gm_l0;
                    gm_tbl8il92l97ut t10 = gm_l1;
                    void * t11 = gm_l2;
                    gm_t833u93mr4iob t12;
                    gm_f_ew_c5kil07((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t12))[0] != 0)) {
                        gm_tcfa6l7reoosn t13;
                        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                        ((uint8_t *)&t13)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t13 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t12) + 8)));
                        F.ret = t13;
                        goto gm_fret;
                    }
                    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t12) + 8));
                }
            }
            {
                gm_b0_1 t15;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t15);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t16 = gm_l0;
            gm_t2lqu8jsj511d t17 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t16));
            gm_l3 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = gm_l2;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 104));
            gm_b16_8 t21 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t18, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t21;
        }
        {
            void * t22 = gm_l0;
            gm_tbl8il92l97ut t23 = gm_l1;
            void * t24 = gm_l2;
            gm_b16_8 t25 = gm_l4;
            gm_tcfa6l7reoosn t26;
            gm_f_ev_25jfdar((void *)&t26, (void *)NULL, cx, NULL, t22, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t26))[0] != 0)) {
                gm_tcfa6l7reoosn t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                ((uint8_t *)&t27)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t26) + 8)));
                F.ret = t27;
                goto gm_fret;
            }
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
        }
        {
            void * t29 = gm_l0;
            gm_t2lqu8jsj511d t30 = gm_l3;
            gm_f_release_jn5kqdi(t29, GM_CONV(gm_csc7lc6jlkqkuq, t30));
        }
    }
    gm_b0_1 t31;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t31);
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
inline __attribute__((aligned(4))) void gm_f_ev_index_bie377m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ev_index_bie377m F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t51b2en4qp02s gm_l3; /* b */
    uint64_t gm_l4; /* k */
    unsigned __int128 gm_l5; /* v */
    GM_STACK_CHECK(&F, 611u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l2;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
        uint8_t t5 = (t4 <= ((uint64_t)8ULL));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_tbl8il92l97ut t8 = gm_l1;
            void * t9 = gm_l2;
            gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t7, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
    }
    {
        void * t10 = gm_l2;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 104));
        uint64_t t12 = gm_f_us_cuv01fp(t11);
        gm_l4 = t12;
    }
    {
        void * t13 = ((uint8_t *)&gm_l3);
        uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t15 = gm_l4;
        uint64_t t16 = (uintptr_t)t15;
        if (t14 > t16 || t16 > (uintptr_t)16) gm_trap_slice(t14, t16, 16, gm_src0jbci4ff2esa, 384, 5);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + t14 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16 - t14));
        gm_f_fill_abmf5g5(t17, ((uint8_t)0ULL));
    }
    {
        void * t18 = gm_l0;
        gm_tbl8il92l97ut t19 = gm_l1;
        void * t20 = gm_l2;
        void * t21 = ((uint8_t *)&gm_l3);
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l4;
        uint64_t t24 = (uintptr_t)t23;
        if (t22 > t24 || t24 > (uintptr_t)16) gm_trap_slice(t22, t24, 16, gm_src0jbci4ff2esa, 385, 20);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
        gm_tcfa6l7reoosn t26;
        gm_f_ev_25jfdar((void *)&t26, (void *)NULL, cx, NULL, t18, t19, t20, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) {
            gm_t833u93mr4iob t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t26) + 8)));
            F.ret = t27;
            goto gm_fret;
        }
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
    }
    {
        void * t29 = gm_l0;
        void * t30 = ((uint8_t *)&gm_l3);
        uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t32 = gm_l4;
        uint64_t t33 = (uintptr_t)t32;
        if (t31 > t33 || t33 > (uintptr_t)16) gm_trap_slice(t31, t33, 16, gm_src0jbci4ff2esa, 386, 21);
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t30 + t31 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33 - t31));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        unsigned __int128 t36 = gm_f_get_uint_bq8c4b7(t29, t35);
        gm_l5 = t36;
    }
    {
        gm_t833u93mr4iob t37;
        unsigned __int128 t39 = gm_l5;
        uint64_t t40 = (uint64_t)~((uint64_t)0ULL);
        unsigned __int128 t41 = (unsigned __int128)t40;
        uint8_t t42 = (t39 > t41);
        if (!t42) goto ie38;
        uint64_t t44 = (uint64_t)~((uint64_t)0ULL);
        uint64_t t45 = t44;
        gm_t833u93mr4iob t46;
        GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t46 + 24, 0);
        ((uint8_t *)&t46)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t46 + 8), t45);
        t37 = t46;
        goto ix38;
ie38:;
        unsigned __int128 t48 = gm_l5;
        uint64_t t49 = (uint64_t)t48;
        uint64_t t50 = t49;
        gm_t833u93mr4iob t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t51 + 8), t50);
        t37 = t51;
ix38:;
        uint8_t t52 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t37;
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
inline __attribute__((aligned(4))) void gm_f_ev_return_8ggbvjj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3, void * a4) {
    struct gm_fr_ev_return_8ggbvjj F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* levels */
    void * gm_l3; /* value */
    void * gm_l4; /* n */
    void * gm_l5; /* target */
    uint64_t gm_l6; /* o */
    uint8_t gm_l7; /* is_err */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint32_t t3 = gm_l2;
        void * t4 = gm_f_frame_up_ghkkuqg(t2, t3);
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            void * t8 = gm_l4;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 112));
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sar5aak6f8jpo));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(43));
            gm_t76123gfet73g t11;
            gm_f_trap_qh84l9d((void *)&t11, (void *)NULL, cx, NULL, t7, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t11);
            goto gm_fdone;
        }
        gm_l5 = t5;
    }
    {
        void * t12 = gm_l0;
        gm_tbl8il92l97ut t13 = gm_l1;
        void * t14 = gm_l3;
        void * t15 = gm_l5;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 24));
        gm_tcfa6l7reoosn t17;
        gm_f_ev_25jfdar((void *)&t17, (void *)NULL, cx, NULL, t12, t13, t14, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t17))[0] != 0)) {
            gm_tcfa6l7reoosn t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t18 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t17) + 8)));
            F.ret = t18;
            goto gm_fret;
        }
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
    }
    {
        uint8_t t20;
        void * t22 = gm_l5;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 16));
        if (!(((((uint8_t *)t23) + 32))[8] != 0)) goto ie21;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 32));
        gm_l6 = t24;
        void * t26 = gm_l5;
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 24));
        gm_b16_8 t28 = t27;
        uint64_t t29 = gm_l6;
        uint64_t t30 = gm_f_us_cuv01fp(t29);
        uint64_t t31 = (uintptr_t)t30;
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&t28) + 8);
        if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_src0jbci4ff2esa, 127, 46);
        uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t28)) + t31 * 1));
        uint8_t t34 = (!(t33 == ((uint8_t)0ULL)));
        uint8_t t35 = t34;
        t20 = t35;
        goto ix21;
ie21:;
        uint8_t t37 = ((uint8_t)0);
        t20 = t37;
ix21:;
        gm_l7 = t20;
    }
    {
        void * t38 = gm_l5;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 0));
        uint8_t t40 = gm_l7;
        gm_tf5srh4mp4b3i t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t39);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 8), t40);
        gm_t76123gfet73g t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t42 + 8), t41);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t42);
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
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ev_slow_hdak0k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_ev_slow_hdak0k9 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_b16_8 gm_l3; /* dst */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* bytes */
    uint64_t gm_l6; /* addr */
    gm_b16_8 gm_l7; /* bytes */
    uint64_t gm_l8; /* addr */
    uint32_t gm_l9; /* l */
    uint32_t gm_l10; /* levels */
    uint32_t gm_l11; /* l */
    void * gm_l12; /* fr */
    gm_t1j70din2upc4 gm_l14; /* x */
    gm_t2lqu8jsj511d gm_l16; /* mk */
    gm_b16_8 gm_l17; /* b */
    uint64_t gm_l18; /* o */
    gm_tdvcgvrf81sbd gm_l19; /* x */
    gm_t2lqu8jsj511d gm_l21; /* mk */
    gm_b16_8 gm_l22; /* b */
    gm_t2lqu8jsj511d gm_l24; /* mk */
    gm_b16_8 gm_l25; /* b */
    uint64_t gm_l26; /* i */
    uint64_t gm_l27; /* off */
    void * gm_l30; /* x */
    gm_t2lqu8jsj511d gm_l31; /* mk */
    gm_b16_8 gm_l32; /* b */
    void * gm_l33; /* x */
    gm_t2lqu8jsj511d gm_l34; /* mk */
    gm_b16_8 gm_l35; /* b */
    void * gm_l36; /* x */
    uint64_t gm_l37; /* addr */
    uint64_t gm_l38; /* addr */
    void * gm_l39; /* x */
    uint64_t gm_l40; /* align */
    gm_t2lqu8jsj511d gm_l41; /* mk */
    gm_b16_8 gm_l42; /* b */
    uint64_t gm_l43; /* addr */
    uint32_t gm_l44; /* f */
    uint8_t gm_l45; /* second */
    uint32_t gm_l46; /* f */
    gm_t9qkh0rsooc8d gm_l47; /* op */
    void * gm_l48; /* x */
    gm_ts6oqppjh0ba3 gm_l49; /* op */
    void * gm_l50; /* l */
    void * gm_l51; /* r */
    gm_tmn7n0n9raou5 gm_l52; /* kind */
    void * gm_l53; /* x */
    gm_tvdg94bk7aa22 gm_l54; /* op */
    gm_b16_8 gm_l55; /* args */
    uint32_t gm_l56; /* f */
    gm_b16_8 gm_l57; /* args */
    void * gm_l59; /* callee */
    gm_b16_8 gm_l60; /* args */
    gm_b16_8 gm_l61; /* items */
    gm_b16_8 gm_l62; /* offs */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* i */
    uint64_t gm_l66; /* o */
    void * gm_l67; /* x */
    void * gm_l68; /* v */
    gm_b16_8 gm_l69; /* items */
    gm_t88rcucg1b7uk gm_l70; /* u */
    gm_t2lqu8jsj511d gm_l71; /* mk */
    unsigned __int128 gm_l72; /* acc */
    uint64_t gm_l73; /* <tmp> */
    uint64_t gm_l74; /* <tmp> */
    uint64_t gm_l75; /* i */
    gm_b16_8 gm_l76; /* b */
    gm_tplpbqf3duvn3 gm_l77; /* bf */
    gm_b16_8 gm_l78; /* items */
    uint64_t gm_l79; /* k */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    uint64_t gm_l82; /* i */
    void * gm_l83; /* x */
    uint64_t gm_l84; /* k */
    uint64_t gm_l85; /* j */
    uint32_t gm_l86; /* variant */
    void * gm_l87; /* payload */
    void * gm_l88; /* p */
    gm_t2lqu8jsj511d gm_l89; /* mk */
    gm_b16_8 gm_l90; /* b */
    void * gm_l91; /* x */
    gm_t2lqu8jsj511d gm_l92; /* mk */
    gm_b16_8 gm_l93; /* b */
    void * gm_l94; /* x */
    uint8_t gm_l95; /* is_result */
    gm_t3dcoc7077t1q gm_l96; /* x */
    gm_t083kd7b1kkag gm_l97; /* x */
    void * gm_l98; /* l */
    void * gm_l99; /* r */
    uint8_t gm_l100; /* negated */
    gm_t2lqu8jsj511d gm_l101; /* mk */
    gm_b16_8 gm_l102; /* x */
    gm_b16_8 gm_l103; /* y */
    uint8_t gm_l104; /* v */
    void * gm_l105; /* elem */
    void * gm_l106; /* container */
    uint8_t gm_l107; /* negated */
    gm_t2lqu8jsj511d gm_l108; /* mk */
    gm_b16_8 gm_l109; /* x */
    gm_b16_8 gm_l110; /* c */
    uint8_t gm_l111; /* v */
    void * gm_l112; /* scrutinee */
    gm_b16_8 gm_l113; /* arms */
    void * gm_l114; /* b */
    gm_trl2khmuphncd gm_l115; /* x */
    void * gm_l116; /* el */
    gm_te9hrk0u35rev gm_l118; /* target */
    void * gm_l119; /* value */
    gm_tbl8il92l97ut gm_l120; /* t */
    void * gm_l121; /* v */
    gm_b16_8 gm_l122; /* d */
    gm_te9hrk0u35rev gm_l123; /* target */
    gm_tbl8il92l97ut gm_l124; /* t */
    uint32_t gm_l125; /* levels */
    void * gm_l126; /* value */
    void * gm_l127; /* msg */
    gm_t2lqu8jsj511d gm_l128; /* mk */
    gm_b16_8 gm_l129; /* b */
    gm_b16_8 gm_l130; /* s */
    void * gm_l131; /* k */
    void * gm_l132; /* v */
    void * gm_l133; /* k */
    GM_STACK_CHECK(&F, 14837u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l2;
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            switch (t57) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma16; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma22; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma5; case ((uint8_t)23ULL): goto ma5; case ((uint8_t)24ULL): goto ma5; case ((uint8_t)25ULL): goto ma5; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma5; case ((uint8_t)28ULL): goto ma5; case ((uint8_t)29ULL): goto ma5; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma25; case ((uint8_t)32ULL): goto ma5; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma27; case ((uint8_t)35ULL): goto ma28; case ((uint8_t)36ULL): goto ma29; case ((uint8_t)37ULL): goto ma30; case ((uint8_t)38ULL): goto ma31; case ((uint8_t)39ULL): goto ma32; case ((uint8_t)40ULL): goto ma33; case ((uint8_t)41ULL): goto ma34; case ((uint8_t)42ULL): goto ma35; case ((uint8_t)43ULL): goto ma36; case ((uint8_t)44ULL): goto ma37; case ((uint8_t)45ULL): goto ma38; case ((uint8_t)46ULL): goto ma39; case ((uint8_t)47ULL): goto ma40; case ((uint8_t)48ULL): goto ma41; case ((uint8_t)49ULL): goto ma42; case ((uint8_t)50ULL): goto ma43; case ((uint8_t)51ULL): goto ma44; case ((uint8_t)52ULL): goto ma45; case ((uint8_t)53ULL): goto ma46; case ((uint8_t)54ULL): goto ma47; case ((uint8_t)55ULL): goto ma48; case ((uint8_t)56ULL): goto ma49; case ((uint8_t)57ULL): goto ma50; case ((uint8_t)58ULL): goto ma51; case ((uint8_t)59ULL): goto ma52; case ((uint8_t)60ULL): goto ma53; case ((uint8_t)61ULL): goto ma54; case ((uint8_t)62ULL): goto ma55; default: goto ma56; }
ma5:;
            uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t61 != ((uint8_t)0ULL)) goto pa60;
            goto po59;
pa60:;
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t63 != ((uint8_t)21ULL)) goto pa62;
            goto po59;
pa62:;
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t65 != ((uint8_t)23ULL)) goto pa64;
            goto po59;
pa64:;
            uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t67 != ((uint8_t)24ULL)) goto pa66;
            goto po59;
pa66:;
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t69 != ((uint8_t)25ULL)) goto pa68;
            goto po59;
pa68:;
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t71 != ((uint8_t)32ULL)) goto pa70;
            goto po59;
pa70:;
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t73 != ((uint8_t)26ULL)) goto pa72;
            goto po59;
pa72:;
            uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t75 != ((uint8_t)27ULL)) goto pa74;
            goto po59;
pa74:;
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t77 != ((uint8_t)28ULL)) goto pa76;
            goto po59;
pa76:;
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t79 != ((uint8_t)29ULL)) goto pa78;
            goto po59;
pa78:;
            uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t81 != ((uint8_t)22ULL)) goto pa80;
            goto po59;
pa80:;
            goto mn58;
po59:;
            void * t82 = gm_l0;
            gm_b16_8 t83 = gm_l3;
            void * t84 = gm_l0;
            gm_tbl8il92l97ut t85 = gm_l1;
            void * t86 = gm_l2;
            gm_t833u93mr4iob t87;
            gm_f_ew_c5kil07((void *)&t87, (void *)NULL, cx, NULL, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t87))[0] != 0)) {
                gm_tcfa6l7reoosn t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                ((uint8_t *)&t88)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t88 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t87) + 8)));
                F.ret = t88;
                goto gm_fret;
            }
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&t87) + 8));
            gm_f_wr_word_npte9fs(t82, t83, t89);
            goto me4;
mn58:;
ma6:;
            uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t91 != ((uint8_t)1ULL)) goto mn90;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l4 = t92;
            gm_b16_8 t93 = gm_l3;
            gm_b16_8 t94 = gm_l4;
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
            gm_f_copy_from_f7j6p05(t93, t95);
            goto me4;
mn90:;
ma7:;
            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t97 != ((uint8_t)2ULL)) goto mn96;
            gm_b16_8 t98 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l5 = t98;
            {
                void * t100 = gm_l0;
                void * t101 = gm_l2;
                gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t101) + 112));
                gm_b16_8 t103 = gm_l5;
                gm_t833u93mr4iob t104;
                gm_f_string_literal_337m6cd((void *)&t104, (void *)NULL, cx, NULL, t100, t102, t103, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t104))[0] != 0)) {
                    gm_tcfa6l7reoosn t105;
                    GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                    ((uint8_t *)&t105)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t105 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t104) + 8)));
                    F.ret = t105;
                    goto gm_fret;
                }
                uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&t104) + 8));
                gm_l6 = t106;
            }
            {
                void * t107 = gm_l0;
                gm_b16_8 t108 = gm_l3;
                uint64_t t109 = gm_l6;
                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t111 = (uint64_t)t110;
                gm_f_put_slice_ko14loi(t107, t108, t109, t111);
            }
            goto me4;
mn96:;
ma8:;
            uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t113 != ((uint8_t)3ULL)) goto mn112;
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l7 = t114;
            {
                void * t116 = gm_l0;
                void * t117 = gm_l2;
                gm_tl874t2atnlh3 t118 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t117) + 112));
                gm_b16_8 t119 = gm_l7;
                gm_t833u93mr4iob t120;
                gm_f_string_literal_337m6cd((void *)&t120, (void *)NULL, cx, NULL, t116, t118, t119, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t120))[0] != 0)) {
                    gm_tcfa6l7reoosn t121;
                    GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                    ((uint8_t *)&t121)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t121 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t120) + 8)));
                    F.ret = t121;
                    goto gm_fret;
                }
                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&t120) + 8));
                gm_l8 = t122;
            }
            {
                void * t123 = gm_l0;
                gm_b16_8 t124 = gm_l3;
                uint64_t t125 = gm_l8;
                gm_f_put_ptr_188p3mm(t123, t124, t125);
            }
            goto me4;
mn112:;
ma9:;
            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t127 != ((uint8_t)5ULL)) goto mn126;
            gm_b16_8 t128 = gm_l3;
            uint8_t t129;
            void * t131 = gm_l0;
            uint8_t t132 = ((GM_LD(uint8_t, (((uint8_t *)t131) + 88) + 0) == ((uint8_t)0ULL)));
            if (!t132) goto ie130;
            uint8_t t134 = ((uint8_t)170ULL);
            t129 = t134;
            goto ix130;
ie130:;
            uint8_t t136 = ((uint8_t)0ULL);
            t129 = t136;
ix130:;
            gm_f_fill_abmf5g5(t128, t129);
            goto me4;
mn126:;
ma10:;
            uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t138 != ((uint8_t)6ULL)) goto mn137;
            uint32_t t139 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l9 = t139;
            void * t140 = gm_l0;
            void * t141 = gm_l2;
            gm_tl874t2atnlh3 t142 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t141) + 112));
            void * t143 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            uint32_t t144 = gm_l9;
            gm_b16_8 t145 = gm_l3;
            gm_tcfa6l7reoosn t146;
            gm_f_read_local_n8sc5ua((void *)&t146, (void *)NULL, cx, NULL, t140, t142, t143, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t146))[0] != 0)) {
                gm_tcfa6l7reoosn t147;
                GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                ((uint8_t *)&t147)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t147 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t146) + 8)));
                F.ret = t147;
                goto gm_fret;
            }
            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 8));
            goto me4;
mn137:;
ma11:;
            uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t150 != ((uint8_t)7ULL)) goto mn149;
            uint32_t t151 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l10 = t151;
            uint32_t t152 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 4));
            gm_l11 = t152;
            {
                void * t154 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t155 = gm_l10;
                void * t156 = gm_f_frame_up_ghkkuqg(t154, t155);
                void * t157;
                if ((GM_LD(void *, ((uint8_t *)&t156)) != 0)) {
                    void * t158 = GM_LD(void *, (((uint8_t *)&t156) + 0));
                    t157 = t158;
                } else {
                    void * t159 = gm_l0;
                    void * t160 = gm_l2;
                    gm_tl874t2atnlh3 t161 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t160) + 112));
                    gm_b16_8 t162;
                    GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sco971hnpq7nk));
                    GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(48));
                    gm_t76123gfet73g t163;
                    gm_f_trap_qh84l9d((void *)&t163, (void *)NULL, cx, NULL, t159, t161, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t163);
                    goto gm_fdone;
                }
                gm_l12 = t157;
            }
            {
                void * t164 = gm_l0;
                void * t165 = gm_l2;
                gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 112));
                void * t167 = gm_l12;
                uint32_t t168 = gm_l11;
                gm_b16_8 t169 = gm_l3;
                gm_tcfa6l7reoosn t170;
                gm_f_read_local_n8sc5ua((void *)&t170, (void *)NULL, cx, NULL, t164, t166, t167, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t170))[0] != 0)) {
                    gm_tcfa6l7reoosn t171;
                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                    ((uint8_t *)&t171)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t171 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t170) + 8)));
                    F.ret = t171;
                    goto gm_fret;
                }
                gm_b0_1 t172 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 8));
            }
            goto me4;
mn149:;
ma12:;
            uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t176 != ((uint8_t)4ULL)) goto pa175;
            goto po174;
pa175:;
            uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t178 != ((uint8_t)8ULL)) goto pa177;
            goto po174;
pa177:;
            uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t180 != ((uint8_t)9ULL)) goto pa179;
            goto po174;
pa179:;
            goto mn173;
po174:;
            {
                void * t182 = gm_l0;
                gm_tbl8il92l97ut t183 = gm_l1;
                void * t184 = gm_l2;
                gm_tl1r7a2fn1ev6 t185;
                gm_f_place_47rfqe5((void *)&t185, (void *)NULL, cx, NULL, t182, t183, t184, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t185))[0] != 0)) {
                    gm_tcfa6l7reoosn t186;
                    GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                    ((uint8_t *)&t186)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t186 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t185) + 8)));
                    F.ret = t186;
                    goto gm_fret;
                }
                F.l13 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t185) + 8));
            }
            {
                void * t187 = gm_l0;
                void * t188 = gm_l2;
                gm_tl874t2atnlh3 t189 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t188) + 112));
                void * t190 = gm_l2;
                gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t190) + 96));
                gm_b16_8 t192 = gm_l3;
                gm_tcfa6l7reoosn t193;
                gm_f_read_place_fcc38bb((void *)&t193, (void *)NULL, cx, NULL, t187, t189, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l13)), t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t193))[0] != 0)) {
                    gm_tcfa6l7reoosn t194;
                    GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                    ((uint8_t *)&t194)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t194 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t193) + 8)));
                    F.ret = t194;
                    goto gm_fret;
                }
                gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 8));
            }
            goto me4;
mn173:;
ma13:;
            uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t197 != ((uint8_t)10ULL)) goto mn196;
            gm_t1j70din2upc4 t198 = GM_LD(gm_t1j70din2upc4, ((((uint8_t *)t3) + 0) + 8));
            gm_l14 = t198;
            {
                void * t201 = gm_l2;
                uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)t201) + 124));
                if (!t202) goto ie200;
                {
                    void * t204 = gm_l0;
                    gm_tbl8il92l97ut t205 = gm_l1;
                    void * t206 = gm_l2;
                    gm_tl1r7a2fn1ev6 t207;
                    gm_f_place_47rfqe5((void *)&t207, (void *)NULL, cx, NULL, t204, t205, t206, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t207))[0] != 0)) {
                        gm_tcfa6l7reoosn t208;
                        GM_ST(uint8_t, (uint8_t *)&t208 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t208 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t208 + 4, 0);
                        ((uint8_t *)&t208)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t208 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t207) + 8)));
                        F.ret = t208;
                        goto gm_fret;
                    }
                    F.l15 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t207) + 8));
                }
                {
                    void * t209 = gm_l0;
                    void * t210 = gm_l2;
                    gm_tl874t2atnlh3 t211 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t210) + 112));
                    void * t212 = gm_l2;
                    gm_t88rcucg1b7uk t213 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t212) + 96));
                    gm_b16_8 t214 = gm_l3;
                    gm_tcfa6l7reoosn t215;
                    gm_f_read_place_fcc38bb((void *)&t215, (void *)NULL, cx, NULL, t209, t211, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l15)), t213, t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t215))[0] != 0)) {
                        gm_tcfa6l7reoosn t216;
                        GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                        ((uint8_t *)&t216)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t216 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t215) + 8)));
                        F.ret = t216;
                        goto gm_fret;
                    }
                    gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 8));
                }
                {
                    gm_b0_1 t218;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t218);
                    goto gm_fdone;
                }
                goto ix200;
ie200:;
ix200:;
            }
            {
                void * t219 = gm_l0;
                gm_t2lqu8jsj511d t220 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t219));
                gm_l16 = t220;
            }
            {
                void * t221 = gm_l0;
                void * t222 = GM_LD(void *, (((uint8_t *)&gm_l14) + 0));
                uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)t222) + 104));
                gm_b16_8 t224 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t221, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t224;
            }
            {
                void * t225 = gm_l0;
                gm_tbl8il92l97ut t226 = gm_l1;
                void * t227 = GM_LD(void *, (((uint8_t *)&gm_l14) + 0));
                gm_b16_8 t228 = gm_l17;
                gm_tcfa6l7reoosn t229;
                gm_f_ev_25jfdar((void *)&t229, (void *)NULL, cx, NULL, t225, t226, t227, t228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t229))[0] != 0)) {
                    gm_tcfa6l7reoosn t230;
                    GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                    ((uint8_t *)&t230)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t230 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t229) + 8)));
                    F.ret = t230;
                    goto gm_fret;
                }
                gm_b0_1 t231 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 8));
            }
            {
                uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 16));
                uint64_t t233 = gm_f_us_cuv01fp(t232);
                gm_l18 = t233;
            }
            {
                gm_b16_8 t234 = gm_l3;
                uint64_t t235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                void * t236 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)));
                uint64_t t237 = gm_l18;
                uint64_t t238 = (uintptr_t)t237;
                uint64_t t239 = gm_l18;
                uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint64_t r241; bool o242 = __builtin_add_overflow(t239, t240, &r241);
                if (o242) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 165, 32);
                uint64_t t243 = r241;
                uint64_t t244 = (uintptr_t)t243;
                if (t238 > t244 || t244 > (uintptr_t)t235) gm_trap_slice(t238, t244, t235, gm_src0jbci4ff2esa, 165, 27);
                gm_b16_8 t245;
                GM_ST(void *, (uint8_t *)&t245, (void *)(((uint8_t *)t236 + t238 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(t244 - t238));
                gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                gm_f_copy_from_f7j6p05(t234, t246);
            }
            {
                void * t247 = gm_l0;
                gm_t2lqu8jsj511d t248 = gm_l16;
                gm_f_release_jn5kqdi(t247, GM_CONV(gm_csc7lc6jlkqkuq, t248));
            }
            goto me4;
mn196:;
ma14:;
            uint8_t t250 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t250 != ((uint8_t)11ULL)) goto mn249;
            gm_tdvcgvrf81sbd t251 = GM_LD(gm_tdvcgvrf81sbd, ((((uint8_t *)t3) + 0) + 8));
            gm_l19 = t251;
            {
                void * t254 = gm_l2;
                uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)t254) + 124));
                if (!t255) goto ie253;
                {
                    void * t257 = gm_l0;
                    gm_tbl8il92l97ut t258 = gm_l1;
                    void * t259 = gm_l2;
                    gm_tl1r7a2fn1ev6 t260;
                    gm_f_place_47rfqe5((void *)&t260, (void *)NULL, cx, NULL, t257, t258, t259, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t260))[0] != 0)) {
                        gm_tcfa6l7reoosn t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        ((uint8_t *)&t261)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t261 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t260) + 8)));
                        F.ret = t261;
                        goto gm_fret;
                    }
                    F.l20 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t260) + 8));
                }
                {
                    void * t262 = gm_l0;
                    void * t263 = gm_l2;
                    gm_tl874t2atnlh3 t264 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t263) + 112));
                    void * t265 = gm_l2;
                    gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t265) + 96));
                    gm_b16_8 t267 = gm_l3;
                    gm_tcfa6l7reoosn t268;
                    gm_f_read_place_fcc38bb((void *)&t268, (void *)NULL, cx, NULL, t262, t264, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l20)), t266, t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t268))[0] != 0)) {
                        gm_tcfa6l7reoosn t269;
                        GM_ST(uint8_t, (uint8_t *)&t269 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t269 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t269 + 4, 0);
                        ((uint8_t *)&t269)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t269 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t268) + 8)));
                        F.ret = t269;
                        goto gm_fret;
                    }
                    gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t268) + 8));
                }
                {
                    gm_b0_1 t271;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t271);
                    goto gm_fdone;
                }
                goto ix253;
ie253:;
ix253:;
            }
            {
                void * t272 = gm_l0;
                gm_t2lqu8jsj511d t273 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t272));
                gm_l21 = t273;
            }
            {
                void * t274 = gm_l0;
                void * t275 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)t275) + 104));
                gm_b16_8 t277 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t274, t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t277;
            }
            {
                void * t278 = gm_l0;
                gm_tbl8il92l97ut t279 = gm_l1;
                void * t280 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                gm_b16_8 t281 = gm_l22;
                gm_tcfa6l7reoosn t282;
                gm_f_ev_25jfdar((void *)&t282, (void *)NULL, cx, NULL, t278, t279, t280, t281);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t282))[0] != 0)) {
                    gm_tcfa6l7reoosn t283;
                    GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                    ((uint8_t *)&t283)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t283 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t282) + 8)));
                    F.ret = t283;
                    goto gm_fret;
                }
                gm_b0_1 t284 = GM_LD(gm_b0_1, (((uint8_t *)&t282) + 8));
            }
            {
                void * t285 = gm_l0;
                void * t286 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
                gm_t88rcucg1b7uk t287 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t286) + 96));
                gm_b16_8 t288 = gm_l22;
                uint32_t t289 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
                gm_b16_8 t290 = gm_l3;
                gm_b0_1 t291 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t285, t287, t288, t289, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t292 = gm_l0;
                gm_t2lqu8jsj511d t293 = gm_l21;
                gm_f_release_jn5kqdi(t292, GM_CONV(gm_csc7lc6jlkqkuq, t293));
            }
            goto me4;
mn249:;
ma15:;
            uint8_t t295 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t295 != ((uint8_t)12ULL)) goto mn294;
            gm_tp0fa5qv7rqnn t296 = GM_LD(gm_tp0fa5qv7rqnn, ((((uint8_t *)t3) + 0) + 8));
            F.l23 = t296;
            {
                uint8_t t299 = ((GM_LD(uint8_t, (((uint8_t *)&F.l23) + 16) + 0) == ((uint8_t)0ULL)));
                if (!t299) goto ie298;
                void * t300 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)t300) + 124));
                uint8_t t302 = (uint8_t)!t301;
                if (!t302) goto ie298;
                {
                    void * t304 = gm_l0;
                    gm_t2lqu8jsj511d t305 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t304));
                    gm_l24 = t305;
                }
                {
                    void * t306 = gm_l0;
                    void * t307 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                    uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)t307) + 104));
                    gm_b16_8 t309 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t306, t308);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l25 = t309;
                }
                {
                    void * t310 = gm_l0;
                    gm_tbl8il92l97ut t311 = gm_l1;
                    void * t312 = GM_LD(void *, (((uint8_t *)&F.l23) + 0));
                    gm_b16_8 t313 = gm_l25;
                    gm_tcfa6l7reoosn t314;
                    gm_f_ev_25jfdar((void *)&t314, (void *)NULL, cx, NULL, t310, t311, t312, t313);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t314))[0] != 0)) {
                        gm_tcfa6l7reoosn t315;
                        GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
                        ((uint8_t *)&t315)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t315 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t314) + 8)));
                        F.ret = t315;
                        goto gm_fret;
                    }
                    gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t314) + 8));
                }
                {
                    void * t317 = gm_l0;
                    gm_tbl8il92l97ut t318 = gm_l1;
                    void * t319 = GM_LD(void *, (((uint8_t *)&F.l23) + 8));
                    gm_t833u93mr4iob t320;
                    gm_f_ev_index_bie377m((void *)&t320, (void *)NULL, cx, NULL, t317, t318, t319);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t320))[0] != 0)) {
                        gm_tcfa6l7reoosn t321;
                        GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
                        ((uint8_t *)&t321)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t321 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t320) + 8)));
                        F.ret = t321;
                        goto gm_fret;
                    }
                    uint64_t t322 = GM_LD(uint64_t, (((uint8_t *)&t320) + 8));
                    gm_l26 = t322;
                }
                {
                    uint64_t t324 = gm_l26;
                    uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 32));
                    uint8_t t326 = (t324 >= t325);
                    if (!t326) goto ie323;
                    {
                        void * t328 = gm_l0;
                        void * t329 = gm_l2;
                        gm_tl874t2atnlh3 t330 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t329) + 112));
                        uint64_t t331 = gm_l26;
                        uint64_t t332 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 32));
                        gm_t2lqu8jsj511d t333;
                        GM_ST(uint64_t, ((uint8_t *)&t333 + 0), t331);
                        GM_ST(uint64_t, ((uint8_t *)&t333 + 8), t332);
                        gm_tabb710d9mq2e t334;
                        gm_f_format_2hd15t9((void *)&t334, (void *)NULL, cx, NULL, t333);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 8, gm_src0jbci4ff2esa, 186, 56);
                        gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
                        gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                        gm_t76123gfet73g t337;
                        gm_f_trap_qh84l9d((void *)&t337, (void *)NULL, cx, NULL, t328, t330, t336);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t337);
                        goto gm_fdone;
                    }
                    goto ix323;
ie323:;
ix323:;
                }
                {
                    uint64_t t338 = gm_l26;
                    uint64_t t339 = gm_f_us_cuv01fp(t338);
                    uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t r341; bool o342 = __builtin_mul_overflow(t339, t340, &r341);
                    if (o342) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 187, 24);
                    uint64_t t343 = r341;
                    gm_l27 = t343;
                }
                {
                    gm_b16_8 t344 = gm_l3;
                    uint64_t t345 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    void * t346 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)));
                    uint64_t t347 = gm_l27;
                    uint64_t t348 = (uintptr_t)t347;
                    uint64_t t349 = gm_l27;
                    uint64_t t350 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint64_t r351; bool o352 = __builtin_add_overflow(t349, t350, &r351);
                    if (o352) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 188, 38);
                    uint64_t t353 = r351;
                    uint64_t t354 = (uintptr_t)t353;
                    if (t348 > t354 || t354 > (uintptr_t)t345) gm_trap_slice(t348, t354, t345, gm_src0jbci4ff2esa, 188, 31);
                    gm_b16_8 t355;
                    GM_ST(void *, (uint8_t *)&t355, (void *)(((uint8_t *)t346 + t348 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(t354 - t348));
                    gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
                    gm_f_copy_from_f7j6p05(t344, t356);
                }
                {
                    void * t357 = gm_l0;
                    gm_t2lqu8jsj511d t358 = gm_l24;
                    gm_f_release_jn5kqdi(t357, GM_CONV(gm_csc7lc6jlkqkuq, t358));
                }
                {
                    gm_b0_1 t359;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t359);
                    goto gm_fdone;
                }
                goto ix298;
ie298:;
ix298:;
            }
            {
                void * t360 = gm_l0;
                gm_tbl8il92l97ut t361 = gm_l1;
                void * t362 = gm_l2;
                gm_tl1r7a2fn1ev6 t363;
                gm_f_place_47rfqe5((void *)&t363, (void *)NULL, cx, NULL, t360, t361, t362, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t363))[0] != 0)) {
                    gm_tcfa6l7reoosn t364;
                    GM_ST(uint8_t, (uint8_t *)&t364 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t364 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t364 + 4, 0);
                    ((uint8_t *)&t364)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t364 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t363) + 8)));
                    F.ret = t364;
                    goto gm_fret;
                }
                F.l28 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t363) + 8));
            }
            {
                void * t365 = gm_l0;
                void * t366 = gm_l2;
                gm_tl874t2atnlh3 t367 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t366) + 112));
                void * t368 = gm_l2;
                gm_t88rcucg1b7uk t369 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t368) + 96));
                gm_b16_8 t370 = gm_l3;
                gm_tcfa6l7reoosn t371;
                gm_f_read_place_fcc38bb((void *)&t371, (void *)NULL, cx, NULL, t365, t367, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l28)), t369, t370);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t371))[0] != 0)) {
                    gm_tcfa6l7reoosn t372;
                    GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                    ((uint8_t *)&t372)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t372 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t371) + 8)));
                    F.ret = t372;
                    goto gm_fret;
                }
                gm_b0_1 t373 = GM_LD(gm_b0_1, (((uint8_t *)&t371) + 8));
            }
            goto me4;
mn294:;
ma16:;
            uint8_t t375 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t375 != ((uint8_t)13ULL)) goto mn374;
            gm_tt49maei14cl6 t376 = GM_LD(gm_tt49maei14cl6, ((((uint8_t *)t3) + 0) + 8));
            F.l29 = t376;
            void * t377 = gm_l0;
            gm_tbl8il92l97ut t378 = gm_l1;
            void * t379 = gm_l2;
            void * t380 = GM_LD(void *, (((uint8_t *)&F.l29) + 0));
            void * t381 = GM_LD(void *, (((uint8_t *)&F.l29) + 8));
            void * t382 = GM_LD(void *, (((uint8_t *)&F.l29) + 16));
            uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 24));
            gm_t9qkh0rsooc8d t384 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l29) + 25));
            uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 32));
            uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 40));
            gm_b16_8 t387 = gm_l3;
            gm_tcfa6l7reoosn t388;
            gm_f_ev_slice_i5uin87((void *)&t388, (void *)NULL, cx, NULL, t377, t378, t379, t380, t381, t382, t383, t384, t385, t386, t387);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t388))[0] != 0)) {
                gm_tcfa6l7reoosn t389;
                GM_ST(uint8_t, (uint8_t *)&t389 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t389 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
                ((uint8_t *)&t389)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t389 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t388) + 8)));
                F.ret = t389;
                goto gm_fret;
            }
            gm_b0_1 t390 = GM_LD(gm_b0_1, (((uint8_t *)&t388) + 8));
            goto me4;
mn374:;
ma17:;
            uint8_t t392 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t392 != ((uint8_t)14ULL)) goto mn391;
            void * t393 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l30 = t393;
            {
                void * t395 = gm_l0;
                gm_t2lqu8jsj511d t396 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t395));
                gm_l31 = t396;
            }
            {
                void * t397 = gm_l0;
                void * t398 = gm_l30;
                uint64_t t399 = GM_LD(uint64_t, (((uint8_t *)t398) + 104));
                gm_b16_8 t400 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t397, t399);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t400;
            }
            {
                void * t401 = gm_l0;
                gm_tbl8il92l97ut t402 = gm_l1;
                void * t403 = gm_l30;
                gm_b16_8 t404 = gm_l32;
                gm_tcfa6l7reoosn t405;
                gm_f_ev_25jfdar((void *)&t405, (void *)NULL, cx, NULL, t401, t402, t403, t404);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t405))[0] != 0)) {
                    gm_tcfa6l7reoosn t406;
                    GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
                    ((uint8_t *)&t406)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t406 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t405) + 8)));
                    F.ret = t406;
                    goto gm_fret;
                }
                gm_b0_1 t407 = GM_LD(gm_b0_1, (((uint8_t *)&t405) + 8));
            }
            {
                void * t408 = gm_l0;
                gm_b16_8 t409 = gm_l3;
                void * t410 = gm_l0;
                gm_b16_8 t411 = gm_l32;
                gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                gm_t2lqu8jsj511d t413 = gm_f_slice_parts_1se5hu4(t410, t412);
                uint64_t t414 = GM_LD(uint64_t, (((uint8_t *)&t413) + 8));
                gm_f_put_u64_irp77r2(t408, t409, t414);
            }
            {
                void * t415 = gm_l0;
                gm_t2lqu8jsj511d t416 = gm_l31;
                gm_f_release_jn5kqdi(t415, GM_CONV(gm_csc7lc6jlkqkuq, t416));
            }
            goto me4;
mn391:;
ma18:;
            uint8_t t418 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t418 != ((uint8_t)15ULL)) goto mn417;
            void * t419 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l33 = t419;
            {
                void * t421 = gm_l0;
                gm_t2lqu8jsj511d t422 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t421));
                gm_l34 = t422;
            }
            {
                void * t423 = gm_l0;
                void * t424 = gm_l33;
                uint64_t t425 = GM_LD(uint64_t, (((uint8_t *)t424) + 104));
                gm_b16_8 t426 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t423, t425);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t426;
            }
            {
                void * t427 = gm_l0;
                gm_tbl8il92l97ut t428 = gm_l1;
                void * t429 = gm_l33;
                gm_b16_8 t430 = gm_l35;
                gm_tcfa6l7reoosn t431;
                gm_f_ev_25jfdar((void *)&t431, (void *)NULL, cx, NULL, t427, t428, t429, t430);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t431))[0] != 0)) {
                    gm_tcfa6l7reoosn t432;
                    GM_ST(uint8_t, (uint8_t *)&t432 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t432 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t432 + 4, 0);
                    ((uint8_t *)&t432)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t432 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t431) + 8)));
                    F.ret = t432;
                    goto gm_fret;
                }
                gm_b0_1 t433 = GM_LD(gm_b0_1, (((uint8_t *)&t431) + 8));
            }
            {
                void * t434 = gm_l0;
                gm_b16_8 t435 = gm_l3;
                void * t436 = gm_l0;
                gm_b16_8 t437 = gm_l35;
                gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&t437));
                gm_t2lqu8jsj511d t439 = gm_f_slice_parts_1se5hu4(t436, t438);
                uint64_t t440 = GM_LD(uint64_t, (((uint8_t *)&t439) + 0));
                gm_f_put_ptr_188p3mm(t434, t435, t440);
            }
            {
                void * t441 = gm_l0;
                gm_t2lqu8jsj511d t442 = gm_l34;
                gm_f_release_jn5kqdi(t441, GM_CONV(gm_csc7lc6jlkqkuq, t442));
            }
            goto me4;
mn417:;
ma19:;
            uint8_t t444 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t444 != ((uint8_t)16ULL)) goto mn443;
            void * t445 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l36 = t445;
            {
                void * t447 = gm_l0;
                gm_tbl8il92l97ut t448 = gm_l1;
                void * t449 = gm_l36;
                gm_tl1r7a2fn1ev6 t450;
                gm_f_place_47rfqe5((void *)&t450, (void *)NULL, cx, NULL, t447, t448, t449, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t450))[0] != 0)) {
                    gm_tcfa6l7reoosn t451;
                    GM_ST(uint8_t, (uint8_t *)&t451 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t451 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t451 + 4, 0);
                    ((uint8_t *)&t451)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t451 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t450) + 8)));
                    F.ret = t451;
                    goto gm_fret;
                }
                uint8_t t457 = GM_LD(uint8_t, ((((uint8_t *)&t450) + 8) + 0));
                switch (t457) { case ((uint8_t)0ULL): goto ma454; case ((uint8_t)1ULL): goto ma453; case ((uint8_t)2ULL): goto ma453; case ((uint8_t)3ULL): goto ma455; default: goto ma456; }
ma453:;
                uint8_t t461 = GM_LD(uint8_t, ((((uint8_t *)&t450) + 8) + 0));
                if (t461 != ((uint8_t)1ULL)) goto pa460;
                uint64_t t462 = GM_LD(uint64_t, ((((uint8_t *)&t450) + 8) + 8));
                gm_l37 = t462;
                goto po459;
pa460:;
                uint8_t t464 = GM_LD(uint8_t, ((((uint8_t *)&t450) + 8) + 0));
                if (t464 != ((uint8_t)2ULL)) goto pa463;
                uint64_t t465 = GM_LD(uint64_t, ((((uint8_t *)&t450) + 8) + 8));
                gm_l37 = t465;
                goto po459;
pa463:;
                goto mn458;
po459:;
                void * t466 = gm_l0;
                gm_b16_8 t467 = gm_l3;
                uint64_t t468 = gm_l37;
                gm_f_put_ptr_188p3mm(t466, t467, t468);
                goto me452;
mn458:;
ma454:;
                uint8_t t470 = GM_LD(uint8_t, ((((uint8_t *)&t450) + 8) + 0));
                if (t470 != ((uint8_t)0ULL)) goto mn469;
                void * t471 = gm_l0;
                void * t472 = gm_l2;
                gm_tl874t2atnlh3 t473 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t472) + 112));
                gm_b16_8 t474;
                GM_ST(void *, (uint8_t *)&t474, (void *)(gm_sds80esh09gni));
                GM_ST(uintptr_t, (uint8_t *)&t474 + 8, (uintptr_t)(50));
                gm_t76123gfet73g t475;
                gm_f_trap_qh84l9d((void *)&t475, (void *)NULL, cx, NULL, t471, t473, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t475);
                goto gm_fdone;
                goto me452;
mn469:;
ma455:;
                uint8_t t477 = GM_LD(uint8_t, ((((uint8_t *)&t450) + 8) + 0));
                if (t477 != ((uint8_t)3ULL)) goto mn476;
                void * t478 = gm_l0;
                void * t479 = gm_l2;
                gm_tl874t2atnlh3 t480 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t479) + 112));
                gm_b16_8 t481;
                GM_ST(void *, (uint8_t *)&t481, (void *)(gm_su2imns6t7mh5));
                GM_ST(uintptr_t, (uint8_t *)&t481 + 8, (uintptr_t)(40));
                gm_t76123gfet73g t482;
                gm_f_trap_qh84l9d((void *)&t482, (void *)NULL, cx, NULL, t478, t480, t481);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t482);
                goto gm_fdone;
                goto me452;
mn476:;
ma456:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 211, 13);
me452:;
            }
            goto me4;
mn443:;
ma20:;
            uint8_t t484 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t484 != ((uint8_t)17ULL)) goto mn483;
            void * t485 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l39 = t485;
            uint64_t t486 = GM_LD(uint64_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l40 = t486;
            {
                void * t488 = gm_l0;
                gm_t2lqu8jsj511d t489 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t488));
                gm_l41 = t489;
            }
            {
                void * t490 = gm_l0;
                void * t491 = gm_l39;
                uint64_t t492 = GM_LD(uint64_t, (((uint8_t *)t491) + 104));
                gm_b16_8 t493 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t490, t492);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t493;
            }
            {
                void * t494 = gm_l0;
                gm_tbl8il92l97ut t495 = gm_l1;
                void * t496 = gm_l39;
                gm_b16_8 t497 = gm_l42;
                gm_tcfa6l7reoosn t498;
                gm_f_ev_25jfdar((void *)&t498, (void *)NULL, cx, NULL, t494, t495, t496, t497);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t498))[0] != 0)) {
                    gm_tcfa6l7reoosn t499;
                    GM_ST(uint8_t, (uint8_t *)&t499 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t499 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t499 + 4, 0);
                    ((uint8_t *)&t499)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t499 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t498) + 8)));
                    F.ret = t499;
                    goto gm_fret;
                }
                gm_b0_1 t500 = GM_LD(gm_b0_1, (((uint8_t *)&t498) + 8));
            }
            {
                void * t501 = gm_l0;
                void * t502 = gm_l2;
                gm_tl874t2atnlh3 t503 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t502) + 112));
                gm_b16_8 t504 = gm_l42;
                gm_b16_8 t505 = GM_LD(gm_b16_8, ((uint8_t *)&t504));
                uint64_t t506 = gm_l40;
                gm_t833u93mr4iob t507;
                gm_f_alloc_temp_9cmh124((void *)&t507, (void *)NULL, cx, NULL, t501, t503, t505, t506);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t507))[0] != 0)) {
                    gm_tcfa6l7reoosn t508;
                    GM_ST(uint8_t, (uint8_t *)&t508 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t508 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t508 + 4, 0);
                    ((uint8_t *)&t508)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t508 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t507) + 8)));
                    F.ret = t508;
                    goto gm_fret;
                }
                uint64_t t509 = GM_LD(uint64_t, (((uint8_t *)&t507) + 8));
                gm_l43 = t509;
            }
            {
                void * t510 = gm_l0;
                gm_t2lqu8jsj511d t511 = gm_l41;
                gm_f_release_jn5kqdi(t510, GM_CONV(gm_csc7lc6jlkqkuq, t511));
            }
            {
                void * t512 = gm_l0;
                gm_b16_8 t513 = gm_l3;
                uint64_t t514 = gm_l43;
                gm_f_put_ptr_188p3mm(t512, t513, t514);
            }
            goto me4;
mn483:;
ma21:;
            uint8_t t516 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t516 != ((uint8_t)18ULL)) goto mn515;
            uint32_t t517 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l44 = t517;
            uint8_t t518 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 4));
            gm_l45 = t518;
            {
                void * t520 = gm_l0;
                gm_b16_8 t521 = gm_l3;
                void * t522 = gm_l0;
                uint32_t t523 = gm_l44;
                uint64_t t524 = gm_f_fn_addr_odbbf6a(t522, t523);
                gm_f_put_word_tfn443i(t520, t521, ((uint64_t)0ULL), t524);
            }
            {
                uint8_t t526 = gm_l45;
                if (!t526) goto ie525;
                {
                    void * t528 = gm_l0;
                    gm_b16_8 t529 = gm_l3;
                    gm_f_put_word_tfn443i(t528, t529, ((uint64_t)1ULL), ((uint64_t)0ULL));
                }
                {
                    void * t530 = gm_l0;
                    gm_b16_8 t531 = gm_l3;
                    uint32_t t532 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t533 = (uint64_t)t532;
                    gm_f_put_word_tfn443i(t530, t531, ((uint64_t)2ULL), t533);
                }
                goto ix525;
ie525:;
ix525:;
            }
            goto me4;
mn515:;
ma22:;
            uint8_t t535 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t535 != ((uint8_t)19ULL)) goto mn534;
            uint32_t t536 = GM_LD(uint32_t, ((((uint8_t *)t3) + 0) + 8));
            gm_l46 = t536;
            {
                void * t538 = gm_l0;
                gm_b16_8 t539 = gm_l3;
                void * t540 = gm_l0;
                uint32_t t541 = gm_l46;
                uint64_t t542 = gm_f_fn_addr_odbbf6a(t540, t541);
                gm_f_put_word_tfn443i(t538, t539, ((uint64_t)0ULL), t542);
            }
            {
                void * t543 = gm_l0;
                gm_b16_8 t544 = gm_l3;
                void * t545 = gm_l0;
                void * t546 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t547 = gm_f_frame_handle_k12gn88((void *)NULL, cx, NULL, t545, t546);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t548 = (uint64_t)t547;
                gm_f_put_word_tfn443i(t543, t544, ((uint64_t)1ULL), t548);
            }
            {
                void * t549 = gm_l0;
                gm_b16_8 t550 = gm_l3;
                uint32_t t551 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t552 = (uint64_t)t551;
                gm_f_put_word_tfn443i(t549, t550, ((uint64_t)2ULL), t552);
            }
            goto me4;
mn534:;
ma23:;
            uint8_t t554 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t554 != ((uint8_t)20ULL)) goto mn553;
            {
                void * t556 = gm_l0;
                gm_b16_8 t557 = gm_l3;
                void * t558 = gm_l0;
                void * t559 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint32_t t560 = gm_f_frame_handle_k12gn88((void *)NULL, cx, NULL, t558, t559);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t561 = (uint64_t)t560;
                gm_f_put_word_tfn443i(t556, t557, ((uint64_t)0ULL), t561);
            }
            {
                void * t562 = gm_l0;
                gm_b16_8 t563 = gm_l3;
                uint32_t t564 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t565 = (uint64_t)t564;
                gm_f_put_word_tfn443i(t562, t563, ((uint64_t)1ULL), t565);
            }
            goto me4;
mn553:;
ma24:;
            uint8_t t567 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t567 != ((uint8_t)30ULL)) goto mn566;
            gm_t9qkh0rsooc8d t568 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l47 = t568;
            void * t569 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l48 = t569;
            void * t570 = gm_l0;
            gm_tbl8il92l97ut t571 = gm_l1;
            gm_t9qkh0rsooc8d t572 = gm_l47;
            void * t573 = gm_l48;
            void * t574 = gm_l2;
            gm_b16_8 t575 = gm_l3;
            gm_tcfa6l7reoosn t576;
            gm_f_unary_sorkbs7((void *)&t576, (void *)NULL, cx, NULL, t570, t571, t572, t573, t574, t575);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t576))[0] != 0)) {
                gm_tcfa6l7reoosn t577;
                GM_ST(uint8_t, (uint8_t *)&t577 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t577 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t577 + 4, 0);
                ((uint8_t *)&t577)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t577 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t576) + 8)));
                F.ret = t577;
                goto gm_fret;
            }
            gm_b0_1 t578 = GM_LD(gm_b0_1, (((uint8_t *)&t576) + 8));
            goto me4;
mn566:;
ma25:;
            uint8_t t580 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t580 != ((uint8_t)31ULL)) goto mn579;
            gm_ts6oqppjh0ba3 t581 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l49 = t581;
            void * t582 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l50 = t582;
            void * t583 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l51 = t583;
            void * t584 = gm_l0;
            gm_tbl8il92l97ut t585 = gm_l1;
            gm_ts6oqppjh0ba3 t586 = gm_l49;
            void * t587 = gm_l50;
            void * t588 = gm_l51;
            void * t589 = gm_l2;
            gm_b16_8 t590 = gm_l3;
            gm_tcfa6l7reoosn t591;
            gm_f_binary_6rg6s7q((void *)&t591, (void *)NULL, cx, NULL, t584, t585, t586, t587, t588, t589, t590);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t591))[0] != 0)) {
                gm_tcfa6l7reoosn t592;
                GM_ST(uint8_t, (uint8_t *)&t592 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t592 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t592 + 4, 0);
                ((uint8_t *)&t592)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t592 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t591) + 8)));
                F.ret = t592;
                goto gm_fret;
            }
            gm_b0_1 t593 = GM_LD(gm_b0_1, (((uint8_t *)&t591) + 8));
            goto me4;
mn579:;
ma26:;
            uint8_t t595 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t595 != ((uint8_t)33ULL)) goto mn594;
            gm_tmn7n0n9raou5 t596 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l52 = t596;
            void * t597 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l53 = t597;
            void * t598 = gm_l0;
            gm_tbl8il92l97ut t599 = gm_l1;
            gm_tmn7n0n9raou5 t600 = gm_l52;
            void * t601 = gm_l53;
            void * t602 = gm_l2;
            gm_b16_8 t603 = gm_l3;
            gm_tcfa6l7reoosn t604;
            gm_f_coerce_im8vs3u((void *)&t604, (void *)NULL, cx, NULL, t598, t599, t600, t601, t602, t603);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t604))[0] != 0)) {
                gm_tcfa6l7reoosn t605;
                GM_ST(uint8_t, (uint8_t *)&t605 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t605 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t605 + 4, 0);
                ((uint8_t *)&t605)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t605 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t604) + 8)));
                F.ret = t605;
                goto gm_fret;
            }
            gm_b0_1 t606 = GM_LD(gm_b0_1, (((uint8_t *)&t604) + 8));
            goto me4;
mn594:;
ma27:;
            uint8_t t608 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t608 != ((uint8_t)34ULL)) goto mn607;
            gm_tvdg94bk7aa22 t609 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l54 = t609;
            gm_b16_8 t610 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l55 = t610;
            void * t611 = gm_l0;
            gm_tbl8il92l97ut t612 = gm_l1;
            gm_tvdg94bk7aa22 t613 = gm_l54;
            gm_b16_8 t614 = gm_l55;
            void * t615 = gm_l2;
            gm_b16_8 t616 = gm_l3;
            gm_tcfa6l7reoosn t617;
            gm_f_ev_builtin_86a9q3q((void *)&t617, (void *)NULL, cx, NULL, t611, t612, t613, t614, t615, t616);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t617))[0] != 0)) {
                gm_tcfa6l7reoosn t618;
                GM_ST(uint8_t, (uint8_t *)&t618 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t618 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t618 + 4, 0);
                ((uint8_t *)&t618)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t618 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t617) + 8)));
                F.ret = t618;
                goto gm_fret;
            }
            gm_b0_1 t619 = GM_LD(gm_b0_1, (((uint8_t *)&t617) + 8));
            goto me4;
mn607:;
ma28:;
            uint8_t t621 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t621 != ((uint8_t)35ULL)) goto mn620;
            uint32_t t622 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l56 = t622;
            gm_b16_8 t623 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l57 = t623;
            void * t624 = gm_l0;
            gm_tbl8il92l97ut t625 = gm_l1;
            uint32_t t626 = gm_l56;
            gm_b16_8 t627 = gm_l57;
            void * t628 = gm_l2;
            gm_b16_8 t629 = gm_l3;
            gm_tcfa6l7reoosn t630;
            gm_f_call_node_a1b30k9((void *)&t630, (void *)NULL, cx, NULL, t624, t625, t626, t627, t628, t629);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t630))[0] != 0)) {
                gm_tcfa6l7reoosn t631;
                GM_ST(uint8_t, (uint8_t *)&t631 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t631 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t631 + 4, 0);
                ((uint8_t *)&t631)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t631 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t630) + 8)));
                F.ret = t631;
                goto gm_fret;
            }
            gm_b0_1 t632 = GM_LD(gm_b0_1, (((uint8_t *)&t630) + 8));
            goto me4;
mn620:;
ma29:;
            uint8_t t634 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t634 != ((uint8_t)36ULL)) goto mn633;
            gm_tf4r0415i49d1 t635 = GM_LD(gm_tf4r0415i49d1, ((((uint8_t *)t3) + 0) + 8));
            F.l58 = t635;
            void * t636 = gm_l0;
            gm_tbl8il92l97ut t637 = gm_l1;
            uint32_t t638 = GM_LD(uint32_t, (((uint8_t *)&F.l58) + 24));
            gm_b16_8 t639 = GM_LD(gm_b16_8, (((uint8_t *)&F.l58) + 32));
            void * t640 = gm_l2;
            gm_b16_8 t641 = gm_l3;
            gm_tcfa6l7reoosn t642;
            gm_f_ev_perform_ovt4j0p((void *)&t642, (void *)NULL, cx, NULL, t636, t637, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l58) + 0)), t638, t639, t640, t641);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t642))[0] != 0)) {
                gm_tcfa6l7reoosn t643;
                GM_ST(uint8_t, (uint8_t *)&t643 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t643 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t643 + 4, 0);
                ((uint8_t *)&t643)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t643 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t642) + 8)));
                F.ret = t643;
                goto gm_fret;
            }
            gm_b0_1 t644 = GM_LD(gm_b0_1, (((uint8_t *)&t642) + 8));
            goto me4;
mn633:;
ma30:;
            uint8_t t646 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t646 != ((uint8_t)37ULL)) goto mn645;
            void * t647 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l59 = t647;
            gm_b16_8 t648 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l60 = t648;
            void * t649 = gm_l0;
            gm_tbl8il92l97ut t650 = gm_l1;
            void * t651 = gm_l59;
            gm_b16_8 t652 = gm_l60;
            void * t653 = gm_l2;
            gm_b16_8 t654 = gm_l3;
            gm_tcfa6l7reoosn t655;
            gm_f_ev_call_value_f4tfqs3((void *)&t655, (void *)NULL, cx, NULL, t649, t650, t651, t652, t653, t654);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t655))[0] != 0)) {
                gm_tcfa6l7reoosn t656;
                GM_ST(uint8_t, (uint8_t *)&t656 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t656 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t656 + 4, 0);
                ((uint8_t *)&t656)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t656 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t655) + 8)));
                F.ret = t656;
                goto gm_fret;
            }
            gm_b0_1 t657 = GM_LD(gm_b0_1, (((uint8_t *)&t655) + 8));
            goto me4;
mn645:;
ma31:;
            uint8_t t659 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t659 != ((uint8_t)38ULL)) goto mn658;
            gm_b16_8 t660 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l61 = t660;
            gm_b16_8 t661 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l62 = t661;
            {
                {
                    gm_l64 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t664 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                    gm_l63 = t664;
                }
                {
                    for (;;) {
                        uint64_t t666 = gm_l64;
                        uint64_t t667 = gm_l63;
                        uint8_t t668 = (t666 < t667);
                        if (!t668) goto lb665;
                        {
                            uint64_t t671 = gm_l64;
                            uint64_t t674 = GM_LD(uint64_t, ((uint8_t *)&t671));
                            gm_l65 = t674;
                            {
                                uint64_t t676 = gm_l65;
                                uint64_t t677 = (uintptr_t)t676;
                                uint64_t t678 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                                if (t677 >= (uintptr_t)(t678)) gm_trap_index(t677, t678, gm_src0jbci4ff2esa, 250, 25);
                                uint64_t t679 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t677 * 8));
                                uint64_t t680 = gm_f_us_cuv01fp(t679);
                                gm_l66 = t680;
                            }
                            {
                                void * t681 = gm_l0;
                                gm_tbl8il92l97ut t682 = gm_l1;
                                uint64_t t683 = gm_l65;
                                uint64_t t684 = (uintptr_t)t683;
                                uint64_t t685 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                if (t684 >= (uintptr_t)(t685)) gm_trap_index(t684, t685, gm_src0jbci4ff2esa, 251, 29);
                                void * t686 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t684 * 8));
                                gm_b16_8 t687 = gm_l3;
                                uint64_t t688 = GM_LD(uintptr_t, ((uint8_t *)&t687) + 8);
                                void * t689 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t687)));
                                uint64_t t690 = gm_l66;
                                uint64_t t691 = (uintptr_t)t690;
                                uint64_t t692 = gm_l66;
                                uint64_t t693 = gm_l65;
                                uint64_t t694 = (uintptr_t)t693;
                                uint64_t t695 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                if (t694 >= (uintptr_t)(t695)) gm_trap_index(t694, t695, gm_src0jbci4ff2esa, 251, 53);
                                void * t696 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t694 * 8));
                                uint64_t t697 = GM_LD(uint64_t, (((uint8_t *)t696) + 104));
                                uint64_t t698 = gm_f_us_cuv01fp(t697);
                                uint64_t r699; bool o700 = __builtin_add_overflow(t692, t698, &r699);
                                if (o700) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 251, 46);
                                uint64_t t701 = r699;
                                uint64_t t702 = (uintptr_t)t701;
                                if (t691 > t702 || t702 > (uintptr_t)t688) gm_trap_slice(t691, t702, t688, gm_src0jbci4ff2esa, 251, 39);
                                gm_b16_8 t703;
                                GM_ST(void *, (uint8_t *)&t703, (void *)(((uint8_t *)t689 + t691 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t703 + 8, (uintptr_t)(t702 - t691));
                                gm_tcfa6l7reoosn t704;
                                gm_f_ev_25jfdar((void *)&t704, (void *)NULL, cx, NULL, t681, t682, t686, t703);
                                if (gm_unw.kind) {
                                    goto gm_ll669;
                                }
                                if (((((uint8_t *)&t704))[0] != 0)) {
                                    gm_tcfa6l7reoosn t705;
                                    GM_ST(uint8_t, (uint8_t *)&t705 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t705 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t705 + 4, 0);
                                    ((uint8_t *)&t705)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t705 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t704) + 8)));
                                    F.ret = t705;
                                    goto gm_fret;
                                }
                                gm_b0_1 t706 = GM_LD(gm_b0_1, (((uint8_t *)&t704) + 8));
                            }
                            goto me672;
mn673:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 249, 13);
me672:;
                        }
lc665:;
                        {
                            uint64_t t708 = gm_l64;
                            uint64_t r709; bool o710 = __builtin_add_overflow(t708, ((uint64_t)1ULL), &r709);
                            uint64_t t711 = r709;
                            gm_l64 = t711;
                        }
                    }
gm_ll669:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb665; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc665; }
                    }
                    goto gm_lad;
lb665:;
                }
            }
            goto me4;
mn658:;
ma32:;
            uint8_t t713 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t713 != ((uint8_t)39ULL)) goto mn712;
            void * t714 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l67 = t714;
            void * t716 = gm_l67;
            if (!(GM_LD(void *, ((uint8_t *)&t716)) != 0)) goto ie715;
            void * t717 = GM_LD(void *, ((uint8_t *)&t716));
            gm_l68 = t717;
            {
                void * t719 = gm_l0;
                gm_tbl8il92l97ut t720 = gm_l1;
                void * t721 = gm_l68;
                gm_b16_8 t722 = gm_l3;
                uint64_t t723 = GM_LD(uintptr_t, ((uint8_t *)&t722) + 8);
                void * t724 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t722)));
                uint64_t t725 = (uintptr_t)((uint64_t)0ULL);
                void * t726 = gm_l68;
                uint64_t t727 = GM_LD(uint64_t, (((uint8_t *)t726) + 104));
                uint64_t t728 = gm_f_us_cuv01fp(t727);
                uint64_t t729 = (uintptr_t)t728;
                if (t725 > t729 || t729 > (uintptr_t)t723) gm_trap_slice(t725, t729, t723, gm_src0jbci4ff2esa, 254, 56);
                gm_b16_8 t730;
                GM_ST(void *, (uint8_t *)&t730, (void *)(((uint8_t *)t724 + t725 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t730 + 8, (uintptr_t)(t729 - t725));
                gm_tcfa6l7reoosn t731;
                gm_f_ev_25jfdar((void *)&t731, (void *)NULL, cx, NULL, t719, t720, t721, t730);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t731))[0] != 0)) {
                    gm_tcfa6l7reoosn t732;
                    GM_ST(uint8_t, (uint8_t *)&t732 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t732 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t732 + 4, 0);
                    ((uint8_t *)&t732)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t732 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t731) + 8)));
                    F.ret = t732;
                    goto gm_fret;
                }
                gm_b0_1 t733 = GM_LD(gm_b0_1, (((uint8_t *)&t731) + 8));
            }
            goto ix715;
ie715:;
ix715:;
            goto me4;
mn712:;
ma33:;
            uint8_t t735 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t735 != ((uint8_t)40ULL)) goto mn734;
            gm_b16_8 t736 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l69 = t736;
            {
                void * t738 = gm_l0;
                void * t739 = gm_l2;
                gm_t88rcucg1b7uk t740 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t739) + 96));
                gm_tkn42u7na3t81 t741 = gm_f_bits_struct_ty_22ue20s(t738, GM_CONV(gm_csguankko4lmhd, t740));
                gm_t88rcucg1b7uk t742;
                if (((((uint8_t *)&t741))[4] != 0)) {
                    gm_t88rcucg1b7uk t743 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t741) + 0));
                    t742 = t743;
                } else {
                    void * t744 = gm_l0;
                    void * t745 = gm_l2;
                    gm_tl874t2atnlh3 t746 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t745) + 112));
                    gm_b16_8 t747;
                    GM_ST(void *, (uint8_t *)&t747, (void *)(gm_sen05m4ui88rt));
                    GM_ST(uintptr_t, (uint8_t *)&t747 + 8, (uintptr_t)(57));
                    gm_t76123gfet73g t748;
                    gm_f_trap_qh84l9d((void *)&t748, (void *)NULL, cx, NULL, t744, t746, t747);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t748);
                    goto gm_fdone;
                }
                gm_l70 = t742;
            }
            {
                void * t749 = gm_l0;
                gm_t2lqu8jsj511d t750 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t749));
                gm_l71 = t750;
            }
            {
                gm_l72 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                {
                    gm_l74 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t752 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                    gm_l73 = t752;
                }
                {
                    for (;;) {
                        uint64_t t754 = gm_l74;
                        uint64_t t755 = gm_l73;
                        uint8_t t756 = (t754 < t755);
                        if (!t756) goto lb753;
                        {
                            uint64_t t759 = gm_l74;
                            uint64_t t762 = GM_LD(uint64_t, ((uint8_t *)&t759));
                            gm_l75 = t762;
                            {
                                void * t764 = gm_l0;
                                uint64_t t765 = gm_l75;
                                uint64_t t766 = (uintptr_t)t765;
                                uint64_t t767 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                if (t766 >= (uintptr_t)(t767)) gm_trap_index(t766, t767, gm_src0jbci4ff2esa, 260, 28);
                                void * t768 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t766 * 8));
                                uint64_t t769 = GM_LD(uint64_t, (((uint8_t *)t768) + 104));
                                gm_b16_8 t770 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t764, t769);
                                if (gm_unw.kind) {
                                    goto gm_ll757;
                                }
                                gm_l76 = t770;
                            }
                            {
                                void * t771 = gm_l0;
                                gm_tbl8il92l97ut t772 = gm_l1;
                                uint64_t t773 = gm_l75;
                                uint64_t t774 = (uintptr_t)t773;
                                uint64_t t775 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                                if (t774 >= (uintptr_t)(t775)) gm_trap_index(t774, t775, gm_src0jbci4ff2esa, 261, 29);
                                void * t776 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t774 * 8));
                                gm_b16_8 t777 = gm_l76;
                                gm_tcfa6l7reoosn t778;
                                gm_f_ev_25jfdar((void *)&t778, (void *)NULL, cx, NULL, t771, t772, t776, t777);
                                if (gm_unw.kind) {
                                    goto gm_ll757;
                                }
                                if (((((uint8_t *)&t778))[0] != 0)) {
                                    gm_tcfa6l7reoosn t779;
                                    GM_ST(uint8_t, (uint8_t *)&t779 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t779 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t779 + 4, 0);
                                    ((uint8_t *)&t779)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t779 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t778) + 8)));
                                    F.ret = t779;
                                    goto gm_fret;
                                }
                                gm_b0_1 t780 = GM_LD(gm_b0_1, (((uint8_t *)&t778) + 8));
                            }
                            {
                                void * t781 = gm_l0;
                                void * t782 = gm_l2;
                                gm_t88rcucg1b7uk t783 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t782) + 96));
                                uint64_t t784 = gm_l75;
                                uint32_t t785 = (uint32_t)t784;
                                gm_tplpbqf3duvn3 t786 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t781, t783, t785);
                                if (gm_unw.kind) {
                                    goto gm_ll757;
                                }
                                gm_l77 = t786;
                            }
                            {
                                unsigned __int128 t787 = gm_l72;
                                void * t788 = gm_l0;
                                gm_t88rcucg1b7uk t789 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l77) + 4));
                                gm_b16_8 t790 = gm_l76;
                                gm_b16_8 t791 = GM_LD(gm_b16_8, ((uint8_t *)&t790));
                                unsigned __int128 t792 = gm_f_bits_raw_t6dqs65((void *)NULL, cx, NULL, t788, t789, t791);
                                if (gm_unw.kind) {
                                    goto gm_ll757;
                                }
                                uint32_t t793 = GM_LD(uint32_t, (((uint8_t *)&gm_l77) + 0));
                                unsigned __int128 t794 = (unsigned __int128)t793;
                                unsigned __int128 n795 = (unsigned __int128)t794;
                                if (n795 >= 128) gm_trap_shift((uint64_t)n795, "u128", gm_src0jbci4ff2esa, 263, 24);
                                unsigned __int128 r796 = (unsigned __int128)((unsigned __int128)t792 << (int)n795);
                                bool o797 = (unsigned __int128)(r796 >> (int)n795) != t792;
                                if (o797) gm_trap("integer overflow in `<<`", gm_src0jbci4ff2esa, 263, 24);
                                unsigned __int128 t798 = r796;
                                unsigned __int128 t799 = (unsigned __int128)(t787 | t798);
                                gm_l72 = t799;
                            }
                            goto me760;
mn761:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 259, 13);
me760:;
                        }
lc753:;
                        {
                            uint64_t t801 = gm_l74;
                            uint64_t r802; bool o803 = __builtin_add_overflow(t801, ((uint64_t)1ULL), &r802);
                            uint64_t t804 = r802;
                            gm_l74 = t804;
                        }
                    }
gm_ll757:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb753; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc753; }
                    }
                    goto gm_lad;
lb753:;
                }
            }
            {
                void * t805 = gm_l0;
                gm_b16_8 t806 = gm_l3;
                uint64_t t807 = GM_LD(uintptr_t, ((uint8_t *)&t806) + 8);
                void * t808 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t806)));
                uint64_t t809 = (uintptr_t)((uint64_t)0ULL);
                void * t810 = gm_l0;
                gm_t88rcucg1b7uk t811 = gm_l70;
                uint64_t t812 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t810, t811);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t813 = gm_f_us_cuv01fp(t812);
                uint64_t t814 = (uintptr_t)t813;
                if (t809 > t814 || t814 > (uintptr_t)t807) gm_trap_slice(t809, t814, t807, gm_src0jbci4ff2esa, 265, 24);
                gm_b16_8 t815;
                GM_ST(void *, (uint8_t *)&t815, (void *)(((uint8_t *)t808 + t809 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t815 + 8, (uintptr_t)(t814 - t809));
                unsigned __int128 t816 = gm_l72;
                gm_f_put_uint_cddin99(t805, t815, t816);
            }
            {
                void * t817 = gm_l0;
                gm_t2lqu8jsj511d t818 = gm_l71;
                gm_f_release_jn5kqdi(t817, GM_CONV(gm_csc7lc6jlkqkuq, t818));
            }
            goto me4;
mn734:;
ma34:;
            uint8_t t820 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t820 != ((uint8_t)41ULL)) goto mn819;
            gm_b16_8 t821 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 8));
            gm_l78 = t821;
            {
                uint64_t t823;
                uint64_t t825 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                uint8_t t826 = (t825 > ((uint64_t)0ULL));
                if (!t826) goto ie824;
                uint64_t t828 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t829 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                if (t828 >= (uintptr_t)(t829)) gm_trap_index(t828, t829, gm_src0jbci4ff2esa, 269, 40);
                void * t830 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l78)) + t828 * 8));
                uint64_t t831 = GM_LD(uint64_t, (((uint8_t *)t830) + 104));
                uint64_t t832 = gm_f_us_cuv01fp(t831);
                uint64_t t833 = t832;
                t823 = t833;
                goto ix824;
ie824:;
                uint64_t t835 = ((uint64_t)0ULL);
                t823 = t835;
ix824:;
                gm_l79 = t823;
            }
            {
                {
                    gm_l81 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t837 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                    gm_l80 = t837;
                }
                {
                    for (;;) {
                        uint64_t t839 = gm_l81;
                        uint64_t t840 = gm_l80;
                        uint8_t t841 = (t839 < t840);
                        if (!t841) goto lb838;
                        {
                            uint64_t t844 = gm_l81;
                            uint64_t t847 = GM_LD(uint64_t, ((uint8_t *)&t844));
                            gm_l82 = t847;
                            {
                                void * t849 = gm_l0;
                                gm_tbl8il92l97ut t850 = gm_l1;
                                uint64_t t851 = gm_l82;
                                uint64_t t852 = (uintptr_t)t851;
                                uint64_t t853 = GM_LD(uintptr_t, ((uint8_t *)&gm_l78) + 8);
                                if (t852 >= (uintptr_t)(t853)) gm_trap_index(t852, t853, gm_src0jbci4ff2esa, 270, 49);
                                void * t854 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l78)) + t852 * 8));
                                uint64_t t855 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t856 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t857 = gm_l82;
                                uint64_t t858 = gm_l79;
                                uint64_t r859; bool o860 = __builtin_mul_overflow(t857, t858, &r859);
                                if (o860) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 270, 63);
                                uint64_t t861 = r859;
                                uint64_t t862 = (uintptr_t)t861;
                                uint64_t t863 = gm_l82;
                                uint64_t r864; bool o865 = __builtin_add_overflow(t863, ((uint64_t)1ULL), &r864);
                                if (o865) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 270, 71);
                                uint64_t t866 = r864;
                                uint64_t t867 = gm_l79;
                                uint64_t r868; bool o869 = __builtin_mul_overflow(t866, t867, &r868);
                                if (o869) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 270, 70);
                                uint64_t t870 = r868;
                                uint64_t t871 = (uintptr_t)t870;
                                if (t862 > t871 || t871 > (uintptr_t)t855) gm_trap_slice(t862, t871, t855, gm_src0jbci4ff2esa, 270, 59);
                                gm_b16_8 t872;
                                GM_ST(void *, (uint8_t *)&t872, (void *)(((uint8_t *)t856 + t862 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t872 + 8, (uintptr_t)(t871 - t862));
                                gm_tcfa6l7reoosn t873;
                                gm_f_ev_25jfdar((void *)&t873, (void *)NULL, cx, NULL, t849, t850, t854, t872);
                                if (gm_unw.kind) {
                                    goto gm_ll842;
                                }
                                if (((((uint8_t *)&t873))[0] != 0)) {
                                    gm_tcfa6l7reoosn t874;
                                    GM_ST(uint8_t, (uint8_t *)&t874 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t874 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t874 + 4, 0);
                                    ((uint8_t *)&t874)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t874 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t873) + 8)));
                                    F.ret = t874;
                                    goto gm_fret;
                                }
                                gm_b0_1 t875 = GM_LD(gm_b0_1, (((uint8_t *)&t873) + 8));
                            }
                            goto me845;
mn846:;
                            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 270, 13);
me845:;
                        }
lc838:;
                        {
                            uint64_t t877 = gm_l81;
                            uint64_t r878; bool o879 = __builtin_add_overflow(t877, ((uint64_t)1ULL), &r878);
                            uint64_t t880 = r878;
                            gm_l81 = t880;
                        }
                    }
gm_ll842:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb838; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc838; }
                    }
                    goto gm_lad;
lb838:;
                }
            }
            goto me4;
mn819:;
ma35:;
            uint8_t t882 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t882 != ((uint8_t)42ULL)) goto mn881;
            void * t883 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l83 = t883;
            {
                void * t885 = gm_l83;
                uint64_t t886 = GM_LD(uint64_t, (((uint8_t *)t885) + 104));
                uint64_t t887 = gm_f_us_cuv01fp(t886);
                gm_l84 = t887;
            }
            {
                uint64_t t889 = gm_l84;
                uint8_t t890 = ((t889 == ((uint64_t)0ULL)));
                if (!t890) goto ie888;
                {
                    void * t892 = gm_l0;
                    gm_tbl8il92l97ut t893 = gm_l1;
                    void * t894 = gm_l83;
                    gm_b16_8 t895 = gm_l3;
                    gm_tcfa6l7reoosn t896;
                    gm_f_ev_25jfdar((void *)&t896, (void *)NULL, cx, NULL, t892, t893, t894, t895);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t896))[0] != 0)) {
                        gm_tcfa6l7reoosn t897;
                        GM_ST(uint8_t, (uint8_t *)&t897 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t897 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t897 + 4, 0);
                        ((uint8_t *)&t897)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t897 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t896) + 8)));
                        F.ret = t897;
                        goto gm_fret;
                    }
                    gm_b0_1 t898 = GM_LD(gm_b0_1, (((uint8_t *)&t896) + 8));
                }
                {
                    gm_b0_1 t899;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t899);
                    goto gm_fdone;
                }
                goto ix888;
ie888:;
ix888:;
            }
            {
                void * t900 = gm_l0;
                gm_tbl8il92l97ut t901 = gm_l1;
                void * t902 = gm_l83;
                uint64_t t903 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                void * t904 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                uint64_t t905 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t906 = gm_l84;
                uint64_t t907 = (uintptr_t)t906;
                if (t905 > t907 || t907 > (uintptr_t)t903) gm_trap_slice(t905, t907, t903, gm_src0jbci4ff2esa, 278, 28);
                gm_b16_8 t908;
                GM_ST(void *, (uint8_t *)&t908, (void *)(((uint8_t *)t904 + t905 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t908 + 8, (uintptr_t)(t907 - t905));
                gm_tcfa6l7reoosn t909;
                gm_f_ev_25jfdar((void *)&t909, (void *)NULL, cx, NULL, t900, t901, t902, t908);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t909))[0] != 0)) {
                    gm_tcfa6l7reoosn t910;
                    GM_ST(uint8_t, (uint8_t *)&t910 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t910 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t910 + 4, 0);
                    ((uint8_t *)&t910)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t910 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t909) + 8)));
                    F.ret = t910;
                    goto gm_fret;
                }
                gm_b0_1 t911 = GM_LD(gm_b0_1, (((uint8_t *)&t909) + 8));
            }
            {
                uint64_t t912 = gm_l84;
                gm_l85 = t912;
            }
            {
                for (;;) {
                    uint64_t t914 = gm_l85;
                    uint64_t t915 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t916 = (t914 < t915);
                    if (!t916) goto lb913;
                    {
                        uint64_t t919 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        void * t920 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                        uint64_t t921 = gm_l85;
                        uint64_t t922 = (uintptr_t)t921;
                        uint64_t t923 = gm_l85;
                        uint64_t t924 = gm_l84;
                        uint64_t r925; bool o926 = __builtin_add_overflow(t923, t924, &r925);
                        if (o926) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 281, 24);
                        uint64_t t927 = r925;
                        uint64_t t928 = (uintptr_t)t927;
                        if (t922 > t928 || t928 > (uintptr_t)t919) gm_trap_slice(t922, t928, t919, gm_src0jbci4ff2esa, 281, 17);
                        gm_b16_8 t929;
                        GM_ST(void *, (uint8_t *)&t929, (void *)(((uint8_t *)t920 + t922 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t929 + 8, (uintptr_t)(t928 - t922));
                        uint64_t t930 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        void * t931 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                        uint64_t t932 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t933 = gm_l84;
                        uint64_t t934 = (uintptr_t)t933;
                        if (t932 > t934 || t934 > (uintptr_t)t930) gm_trap_slice(t932, t934, t930, gm_src0jbci4ff2esa, 281, 41);
                        gm_b16_8 t935;
                        GM_ST(void *, (uint8_t *)&t935, (void *)(((uint8_t *)t931 + t932 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t935 + 8, (uintptr_t)(t934 - t932));
                        gm_b16_8 t936 = GM_LD(gm_b16_8, ((uint8_t *)&t935));
                        gm_f_copy_from_f7j6p05(t929, t936);
                    }
                    {
                        uint64_t t937 = gm_l85;
                        uint64_t t938 = gm_l84;
                        uint64_t r939; bool o940 = __builtin_add_overflow(t937, t938, &r939);
                        if (o940) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 282, 17);
                        uint64_t t941 = r939;
                        gm_l85 = t941;
                    }
lc913:;
                }
lb913:;
            }
            goto me4;
mn881:;
ma36:;
            uint8_t t943 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t943 != ((uint8_t)43ULL)) goto mn942;
            uint32_t t944 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l86 = t944;
            void * t945 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l87 = t945;
            {
                void * t948 = gm_l87;
                if (!(GM_LD(void *, ((uint8_t *)&t948)) != 0)) goto ie947;
                void * t949 = GM_LD(void *, ((uint8_t *)&t948));
                gm_l88 = t949;
                {
                    void * t951 = gm_l0;
                    gm_t2lqu8jsj511d t952 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t951));
                    gm_l89 = t952;
                }
                {
                    void * t953 = gm_l0;
                    void * t954 = gm_l88;
                    uint64_t t955 = GM_LD(uint64_t, (((uint8_t *)t954) + 104));
                    gm_b16_8 t956 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t953, t955);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l90 = t956;
                }
                {
                    void * t957 = gm_l0;
                    gm_tbl8il92l97ut t958 = gm_l1;
                    void * t959 = gm_l88;
                    gm_b16_8 t960 = gm_l90;
                    gm_tcfa6l7reoosn t961;
                    gm_f_ev_25jfdar((void *)&t961, (void *)NULL, cx, NULL, t957, t958, t959, t960);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t961))[0] != 0)) {
                        gm_tcfa6l7reoosn t962;
                        GM_ST(uint8_t, (uint8_t *)&t962 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t962 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t962 + 4, 0);
                        ((uint8_t *)&t962)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t962 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t961) + 8)));
                        F.ret = t962;
                        goto gm_fret;
                    }
                    gm_b0_1 t963 = GM_LD(gm_b0_1, (((uint8_t *)&t961) + 8));
                }
                {
                    void * t964 = gm_l0;
                    void * t965 = gm_l2;
                    gm_t88rcucg1b7uk t966 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t965) + 96));
                    gm_b16_8 t967 = gm_l3;
                    uint32_t t968 = gm_l86;
                    gm_b16_8 t969 = gm_l90;
                    gm_b16_8 t970 = GM_LD(gm_b16_8, ((uint8_t *)&t969));
                    gm_b0_1 t971 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t964, t966, t967, t968, t970);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t972 = gm_l0;
                    gm_t2lqu8jsj511d t973 = gm_l89;
                    gm_f_release_jn5kqdi(t972, GM_CONV(gm_csc7lc6jlkqkuq, t973));
                }
                goto ix947;
ie947:;
                {
                    void * t975 = gm_l0;
                    void * t976 = gm_l2;
                    gm_t88rcucg1b7uk t977 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t976) + 96));
                    gm_b16_8 t978 = gm_l3;
                    uint32_t t979 = gm_l86;
                    gm_b16_8 t980 = gm_f_empty_slice_ijtp9lo();
                    gm_b16_8 t981 = GM_LD(gm_b16_8, ((uint8_t *)&t980));
                    gm_b0_1 t982 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t975, t977, t978, t979, t981);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix947:;
            }
            goto me4;
mn942:;
ma37:;
            uint8_t t984 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t984 != ((uint8_t)44ULL)) goto mn983;
            void * t985 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l91 = t985;
            {
                void * t987 = gm_l0;
                gm_t2lqu8jsj511d t988 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t987));
                gm_l92 = t988;
            }
            {
                void * t989 = gm_l0;
                void * t990 = gm_l91;
                uint64_t t991 = GM_LD(uint64_t, (((uint8_t *)t990) + 104));
                gm_b16_8 t992 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t989, t991);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l93 = t992;
            }
            {
                void * t993 = gm_l0;
                gm_tbl8il92l97ut t994 = gm_l1;
                void * t995 = gm_l91;
                gm_b16_8 t996 = gm_l93;
                gm_tcfa6l7reoosn t997;
                gm_f_ev_25jfdar((void *)&t997, (void *)NULL, cx, NULL, t993, t994, t995, t996);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t997))[0] != 0)) {
                    gm_tcfa6l7reoosn t998;
                    GM_ST(uint8_t, (uint8_t *)&t998 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t998 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t998 + 4, 0);
                    ((uint8_t *)&t998)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t998 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t997) + 8)));
                    F.ret = t998;
                    goto gm_fret;
                }
                gm_b0_1 t999 = GM_LD(gm_b0_1, (((uint8_t *)&t997) + 8));
            }
            {
                void * t1000 = gm_l0;
                void * t1001 = gm_l2;
                gm_t88rcucg1b7uk t1002 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1001) + 96));
                gm_b16_8 t1003 = gm_l3;
                gm_b16_8 t1004 = gm_l93;
                gm_b16_8 t1005 = GM_LD(gm_b16_8, ((uint8_t *)&t1004));
                gm_b0_1 t1006 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t1000, t1002, t1003, ((uint8_t)1), t1005);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t1007 = gm_l0;
                gm_t2lqu8jsj511d t1008 = gm_l92;
                gm_f_release_jn5kqdi(t1007, GM_CONV(gm_csc7lc6jlkqkuq, t1008));
            }
            goto me4;
mn983:;
ma38:;
            uint8_t t1010 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1010 != ((uint8_t)45ULL)) goto mn1009;
            void * t1011 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l94 = t1011;
            uint8_t t1012 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l95 = t1012;
            void * t1013 = gm_l0;
            gm_tbl8il92l97ut t1014 = gm_l1;
            void * t1015 = gm_l94;
            uint8_t t1016 = gm_l95;
            void * t1017 = gm_l2;
            gm_b16_8 t1018 = gm_l3;
            gm_tcfa6l7reoosn t1019;
            gm_f_unwrap_f7c53ka((void *)&t1019, (void *)NULL, cx, NULL, t1013, t1014, t1015, t1016, t1017, t1018);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1019))[0] != 0)) {
                gm_tcfa6l7reoosn t1020;
                GM_ST(uint8_t, (uint8_t *)&t1020 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1020 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1020 + 4, 0);
                ((uint8_t *)&t1020)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1020 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1019) + 8)));
                F.ret = t1020;
                goto gm_fret;
            }
            gm_b0_1 t1021 = GM_LD(gm_b0_1, (((uint8_t *)&t1019) + 8));
            goto me4;
mn1009:;
ma39:;
            uint8_t t1023 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1023 != ((uint8_t)46ULL)) goto mn1022;
            gm_t3dcoc7077t1q t1024 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t3) + 0) + 8));
            gm_l96 = t1024;
            void * t1025 = gm_l0;
            gm_tbl8il92l97ut t1026 = gm_l1;
            void * t1027 = GM_LD(void *, (((uint8_t *)&gm_l96) + 0));
            gm_t9805qtonbsag t1028 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l96) + 8));
            void * t1029 = GM_LD(void *, (((uint8_t *)&gm_l96) + 16));
            void * t1030 = gm_l2;
            gm_b16_8 t1031 = gm_l3;
            gm_tcfa6l7reoosn t1032;
            gm_f_else__7epkk2p((void *)&t1032, (void *)NULL, cx, NULL, t1025, t1026, t1027, t1028, t1029, t1030, t1031);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1032))[0] != 0)) {
                gm_tcfa6l7reoosn t1033;
                GM_ST(uint8_t, (uint8_t *)&t1033 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1033 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1033 + 4, 0);
                ((uint8_t *)&t1033)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1033 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1032) + 8)));
                F.ret = t1033;
                goto gm_fret;
            }
            gm_b0_1 t1034 = GM_LD(gm_b0_1, (((uint8_t *)&t1032) + 8));
            goto me4;
mn1022:;
ma40:;
            uint8_t t1036 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1036 != ((uint8_t)47ULL)) goto mn1035;
            gm_t083kd7b1kkag t1037 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t3) + 0) + 8));
            gm_l97 = t1037;
            void * t1038 = gm_l0;
            gm_tbl8il92l97ut t1039 = gm_l1;
            void * t1040 = GM_LD(void *, (((uint8_t *)&gm_l97) + 0));
            gm_t9805qtonbsag t1041 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l97) + 8));
            uint32_t t1042 = GM_LD(uint32_t, (((uint8_t *)&gm_l97) + 16));
            void * t1043 = gm_l2;
            gm_b16_8 t1044 = gm_l3;
            gm_tcfa6l7reoosn t1045;
            gm_f_try__q6sv04f((void *)&t1045, (void *)NULL, cx, NULL, t1038, t1039, t1040, t1041, t1042, t1043, t1044);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1045))[0] != 0)) {
                gm_tcfa6l7reoosn t1046;
                GM_ST(uint8_t, (uint8_t *)&t1046 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1046 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1046 + 4, 0);
                ((uint8_t *)&t1046)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1046 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1045) + 8)));
                F.ret = t1046;
                goto gm_fret;
            }
            gm_b0_1 t1047 = GM_LD(gm_b0_1, (((uint8_t *)&t1045) + 8));
            goto me4;
mn1035:;
ma41:;
            uint8_t t1049 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1049 != ((uint8_t)48ULL)) goto mn1048;
            void * t1050 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l98 = t1050;
            void * t1051 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l99 = t1051;
            uint8_t t1052 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l100 = t1052;
            {
                void * t1054 = gm_l0;
                gm_t2lqu8jsj511d t1055 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1054));
                gm_l101 = t1055;
            }
            {
                void * t1056 = gm_l0;
                void * t1057 = gm_l98;
                uint64_t t1058 = GM_LD(uint64_t, (((uint8_t *)t1057) + 104));
                gm_b16_8 t1059 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1056, t1058);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l102 = t1059;
            }
            {
                void * t1060 = gm_l0;
                gm_tbl8il92l97ut t1061 = gm_l1;
                void * t1062 = gm_l98;
                gm_b16_8 t1063 = gm_l102;
                gm_tcfa6l7reoosn t1064;
                gm_f_ev_25jfdar((void *)&t1064, (void *)NULL, cx, NULL, t1060, t1061, t1062, t1063);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1064))[0] != 0)) {
                    gm_tcfa6l7reoosn t1065;
                    GM_ST(uint8_t, (uint8_t *)&t1065 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1065 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1065 + 4, 0);
                    ((uint8_t *)&t1065)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1065 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1064) + 8)));
                    F.ret = t1065;
                    goto gm_fret;
                }
                gm_b0_1 t1066 = GM_LD(gm_b0_1, (((uint8_t *)&t1064) + 8));
            }
            {
                void * t1067 = gm_l0;
                void * t1068 = gm_l99;
                uint64_t t1069 = GM_LD(uint64_t, (((uint8_t *)t1068) + 104));
                gm_b16_8 t1070 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1067, t1069);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l103 = t1070;
            }
            {
                void * t1071 = gm_l0;
                gm_tbl8il92l97ut t1072 = gm_l1;
                void * t1073 = gm_l99;
                gm_b16_8 t1074 = gm_l103;
                gm_tcfa6l7reoosn t1075;
                gm_f_ev_25jfdar((void *)&t1075, (void *)NULL, cx, NULL, t1071, t1072, t1073, t1074);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1075))[0] != 0)) {
                    gm_tcfa6l7reoosn t1076;
                    GM_ST(uint8_t, (uint8_t *)&t1076 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1076 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1076 + 4, 0);
                    ((uint8_t *)&t1076)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1076 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1075) + 8)));
                    F.ret = t1076;
                    goto gm_fret;
                }
                gm_b0_1 t1077 = GM_LD(gm_b0_1, (((uint8_t *)&t1075) + 8));
            }
            {
                void * t1078 = gm_l0;
                void * t1079 = gm_l2;
                gm_tl874t2atnlh3 t1080 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1079) + 112));
                void * t1081 = gm_l98;
                gm_t88rcucg1b7uk t1082 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1081) + 96));
                gm_b16_8 t1083 = gm_l102;
                gm_b16_8 t1084 = gm_l103;
                gm_t1ec8br9ddkav t1085;
                gm_f_equal_e5vf82h((void *)&t1085, (void *)NULL, cx, NULL, t1078, t1080, t1082, t1083, t1084);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1085))[0] != 0)) {
                    gm_tcfa6l7reoosn t1086;
                    GM_ST(uint8_t, (uint8_t *)&t1086 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1086 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1086 + 4, 0);
                    ((uint8_t *)&t1086)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1086 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1085) + 8)));
                    F.ret = t1086;
                    goto gm_fret;
                }
                uint8_t t1087 = GM_LD(uint8_t, (((uint8_t *)&t1085) + 8));
                gm_l104 = t1087;
            }
            {
                uint64_t t1088 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1089 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t1088 >= (uintptr_t)(t1089)) gm_trap_index(t1088, t1089, gm_src0jbci4ff2esa, 313, 13);
                uint8_t *t1090 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t1088 * 1);
                uint8_t t1091;
                uint8_t t1093 = gm_l104;
                uint8_t t1094 = gm_l100;
                uint8_t t1095 = (!(t1093 == t1094));
                if (!t1095) goto ie1092;
                uint8_t t1097 = ((uint8_t)1ULL);
                t1091 = t1097;
                goto ix1092;
ie1092:;
                uint8_t t1099 = ((uint8_t)0ULL);
                t1091 = t1099;
ix1092:;
                GM_ST(uint8_t, t1090, t1091);
            }
            {
                void * t1100 = gm_l0;
                gm_t2lqu8jsj511d t1101 = gm_l101;
                gm_f_release_jn5kqdi(t1100, GM_CONV(gm_csc7lc6jlkqkuq, t1101));
            }
            goto me4;
mn1048:;
ma42:;
            uint8_t t1103 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1103 != ((uint8_t)49ULL)) goto mn1102;
            void * t1104 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l105 = t1104;
            void * t1105 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l106 = t1105;
            uint8_t t1106 = GM_LD(uint8_t, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l107 = t1106;
            {
                void * t1108 = gm_l0;
                gm_t2lqu8jsj511d t1109 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1108));
                gm_l108 = t1109;
            }
            {
                void * t1110 = gm_l0;
                void * t1111 = gm_l105;
                uint64_t t1112 = GM_LD(uint64_t, (((uint8_t *)t1111) + 104));
                gm_b16_8 t1113 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1110, t1112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l109 = t1113;
            }
            {
                void * t1114 = gm_l0;
                gm_tbl8il92l97ut t1115 = gm_l1;
                void * t1116 = gm_l105;
                gm_b16_8 t1117 = gm_l109;
                gm_tcfa6l7reoosn t1118;
                gm_f_ev_25jfdar((void *)&t1118, (void *)NULL, cx, NULL, t1114, t1115, t1116, t1117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1118))[0] != 0)) {
                    gm_tcfa6l7reoosn t1119;
                    GM_ST(uint8_t, (uint8_t *)&t1119 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1119 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1119 + 4, 0);
                    ((uint8_t *)&t1119)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1119 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1118) + 8)));
                    F.ret = t1119;
                    goto gm_fret;
                }
                gm_b0_1 t1120 = GM_LD(gm_b0_1, (((uint8_t *)&t1118) + 8));
            }
            {
                void * t1121 = gm_l0;
                void * t1122 = gm_l106;
                uint64_t t1123 = GM_LD(uint64_t, (((uint8_t *)t1122) + 104));
                gm_b16_8 t1124 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1121, t1123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l110 = t1124;
            }
            {
                void * t1125 = gm_l0;
                gm_tbl8il92l97ut t1126 = gm_l1;
                void * t1127 = gm_l106;
                gm_b16_8 t1128 = gm_l110;
                gm_tcfa6l7reoosn t1129;
                gm_f_ev_25jfdar((void *)&t1129, (void *)NULL, cx, NULL, t1125, t1126, t1127, t1128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1129))[0] != 0)) {
                    gm_tcfa6l7reoosn t1130;
                    GM_ST(uint8_t, (uint8_t *)&t1130 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1130 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1130 + 4, 0);
                    ((uint8_t *)&t1130)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1130 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1129) + 8)));
                    F.ret = t1130;
                    goto gm_fret;
                }
                gm_b0_1 t1131 = GM_LD(gm_b0_1, (((uint8_t *)&t1129) + 8));
            }
            {
                void * t1132 = gm_l0;
                void * t1133 = gm_l2;
                gm_tl874t2atnlh3 t1134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1133) + 112));
                void * t1135 = gm_l105;
                gm_t88rcucg1b7uk t1136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1135) + 96));
                gm_b16_8 t1137 = gm_l109;
                void * t1138 = gm_l106;
                gm_t88rcucg1b7uk t1139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1138) + 96));
                gm_b16_8 t1140 = gm_l110;
                gm_t1ec8br9ddkav t1141;
                gm_f_contains_nugmipn((void *)&t1141, (void *)NULL, cx, NULL, t1132, t1134, t1136, t1137, t1139, t1140);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1141))[0] != 0)) {
                    gm_tcfa6l7reoosn t1142;
                    GM_ST(uint8_t, (uint8_t *)&t1142 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1142 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1142 + 4, 0);
                    ((uint8_t *)&t1142)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1142 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1141) + 8)));
                    F.ret = t1142;
                    goto gm_fret;
                }
                uint8_t t1143 = GM_LD(uint8_t, (((uint8_t *)&t1141) + 8));
                gm_l111 = t1143;
            }
            {
                uint64_t t1144 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t1144 >= (uintptr_t)(t1145)) gm_trap_index(t1144, t1145, gm_src0jbci4ff2esa, 323, 13);
                uint8_t *t1146 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t1144 * 1);
                uint8_t t1147;
                uint8_t t1149 = gm_l111;
                uint8_t t1150 = gm_l107;
                uint8_t t1151 = (!(t1149 == t1150));
                if (!t1151) goto ie1148;
                uint8_t t1153 = ((uint8_t)1ULL);
                t1147 = t1153;
                goto ix1148;
ie1148:;
                uint8_t t1155 = ((uint8_t)0ULL);
                t1147 = t1155;
ix1148:;
                GM_ST(uint8_t, t1146, t1147);
            }
            {
                void * t1156 = gm_l0;
                gm_t2lqu8jsj511d t1157 = gm_l108;
                gm_f_release_jn5kqdi(t1156, GM_CONV(gm_csc7lc6jlkqkuq, t1157));
            }
            goto me4;
mn1102:;
ma43:;
            uint8_t t1159 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1159 != ((uint8_t)50ULL)) goto mn1158;
            void * t1160 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l112 = t1160;
            gm_b16_8 t1161 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l113 = t1161;
            void * t1162 = gm_l0;
            gm_tbl8il92l97ut t1163 = gm_l1;
            void * t1164 = gm_l112;
            gm_b16_8 t1165 = gm_l113;
            void * t1166 = gm_l2;
            gm_b16_8 t1167 = gm_l3;
            gm_tcfa6l7reoosn t1168;
            gm_f_match__tg8e3g3((void *)&t1168, (void *)NULL, cx, NULL, t1162, t1163, t1164, t1165, t1166, t1167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1168))[0] != 0)) {
                gm_tcfa6l7reoosn t1169;
                GM_ST(uint8_t, (uint8_t *)&t1169 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1169 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1169 + 4, 0);
                ((uint8_t *)&t1169)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1169 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1168) + 8)));
                F.ret = t1169;
                goto gm_fret;
            }
            gm_b0_1 t1170 = GM_LD(gm_b0_1, (((uint8_t *)&t1168) + 8));
            goto me4;
mn1158:;
ma44:;
            uint8_t t1172 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1172 != ((uint8_t)51ULL)) goto mn1171;
            void * t1173 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l114 = t1173;
            void * t1174 = gm_l0;
            gm_tbl8il92l97ut t1175 = gm_l1;
            void * t1176 = gm_l114;
            gm_b16_8 t1177 = gm_l3;
            gm_tcfa6l7reoosn t1178;
            gm_f_block_5b89ev3((void *)&t1178, (void *)NULL, cx, NULL, t1174, t1175, t1176, t1177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1178))[0] != 0)) {
                gm_tcfa6l7reoosn t1179;
                GM_ST(uint8_t, (uint8_t *)&t1179 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1179 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1179 + 4, 0);
                ((uint8_t *)&t1179)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1179 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1178) + 8)));
                F.ret = t1179;
                goto gm_fret;
            }
            gm_b0_1 t1180 = GM_LD(gm_b0_1, (((uint8_t *)&t1178) + 8));
            goto me4;
mn1171:;
ma45:;
            uint8_t t1182 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1182 != ((uint8_t)52ULL)) goto mn1181;
            gm_trl2khmuphncd t1183 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t3) + 0) + 8));
            gm_l115 = t1183;
            {
                void * t1186 = gm_l0;
                gm_tbl8il92l97ut t1187 = gm_l1;
                gm_b16_8 t1188 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l115) + 0));
                gm_t1ec8br9ddkav t1189;
                gm_f_conds_30ust77((void *)&t1189, (void *)NULL, cx, NULL, t1186, t1187, t1188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1189))[0] != 0)) {
                    gm_tcfa6l7reoosn t1190;
                    GM_ST(uint8_t, (uint8_t *)&t1190 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1190 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1190 + 4, 0);
                    ((uint8_t *)&t1190)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1190 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1189) + 8)));
                    F.ret = t1190;
                    goto gm_fret;
                }
                uint8_t t1191 = GM_LD(uint8_t, (((uint8_t *)&t1189) + 8));
                if (!t1191) goto ie1185;
                {
                    void * t1193 = gm_l0;
                    gm_tbl8il92l97ut t1194 = gm_l1;
                    void * t1195 = GM_LD(void *, (((uint8_t *)&gm_l115) + 16));
                    gm_b16_8 t1196 = gm_l3;
                    gm_tcfa6l7reoosn t1197;
                    gm_f_ev_25jfdar((void *)&t1197, (void *)NULL, cx, NULL, t1193, t1194, t1195, t1196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t1197))[0] != 0)) {
                        gm_tcfa6l7reoosn t1198;
                        GM_ST(uint8_t, (uint8_t *)&t1198 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1198 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1198 + 4, 0);
                        ((uint8_t *)&t1198)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1198 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1197) + 8)));
                        F.ret = t1198;
                        goto gm_fret;
                    }
                    gm_b0_1 t1199 = GM_LD(gm_b0_1, (((uint8_t *)&t1197) + 8));
                }
                goto ix1185;
ie1185:;
                void * t1201 = GM_LD(void *, (((uint8_t *)&gm_l115) + 24));
                if (!(GM_LD(void *, ((uint8_t *)&t1201)) != 0)) goto ie1200;
                void * t1202 = GM_LD(void *, ((uint8_t *)&t1201));
                gm_l116 = t1202;
                {
                    void * t1204 = gm_l0;
                    gm_tbl8il92l97ut t1205 = gm_l1;
                    void * t1206 = gm_l116;
                    gm_b16_8 t1207 = gm_l3;
                    gm_tcfa6l7reoosn t1208;
                    gm_f_ev_25jfdar((void *)&t1208, (void *)NULL, cx, NULL, t1204, t1205, t1206, t1207);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t1208))[0] != 0)) {
                        gm_tcfa6l7reoosn t1209;
                        GM_ST(uint8_t, (uint8_t *)&t1209 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1209 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1209 + 4, 0);
                        ((uint8_t *)&t1209)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1209 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1208) + 8)));
                        F.ret = t1209;
                        goto gm_fret;
                    }
                    gm_b0_1 t1210 = GM_LD(gm_b0_1, (((uint8_t *)&t1208) + 8));
                }
                goto ix1200;
ie1200:;
ix1200:;
ix1185:;
            }
            goto me4;
mn1181:;
ma46:;
            uint8_t t1212 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1212 != ((uint8_t)53ULL)) goto mn1211;
            gm_t710lsrq1m8gk t1213 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 0) + 8));
            F.l117 = t1213;
            void * t1214 = gm_l0;
            gm_tbl8il92l97ut t1215 = gm_l1;
            uint32_t t1216 = GM_LD(uint32_t, (((uint8_t *)&F.l117) + 0));
            gm_b16_8 t1217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l117) + 8));
            void * t1218 = GM_LD(void *, (((uint8_t *)&F.l117) + 24));
            void * t1219 = GM_LD(void *, (((uint8_t *)&F.l117) + 32));
            gm_b16_8 t1220 = gm_l3;
            gm_tcfa6l7reoosn t1221;
            gm_f_loop__3bo7tki((void *)&t1221, (void *)NULL, cx, NULL, t1214, t1215, t1216, t1217, t1218, t1219, t1220);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1221))[0] != 0)) {
                gm_tcfa6l7reoosn t1222;
                GM_ST(uint8_t, (uint8_t *)&t1222 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1222 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1222 + 4, 0);
                ((uint8_t *)&t1222)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1222 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1221) + 8)));
                F.ret = t1222;
                goto gm_fret;
            }
            gm_b0_1 t1223 = GM_LD(gm_b0_1, (((uint8_t *)&t1221) + 8));
            goto me4;
mn1211:;
ma47:;
            uint8_t t1225 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1225 != ((uint8_t)54ULL)) goto mn1224;
            gm_te9hrk0u35rev t1226 = GM_LD(gm_te9hrk0u35rev, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l118 = t1226;
            void * t1227 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 16));
            gm_l119 = t1227;
            {
                void * t1229 = gm_l0;
                gm_tbl8il92l97ut t1230 = gm_l1;
                gm_te9hrk0u35rev t1231 = gm_l118;
                void * t1232 = gm_l2;
                gm_tl874t2atnlh3 t1233 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1232) + 112));
                gm_t9b5r56q9jk9c t1234;
                gm_f_resolve_target_bdf5i1e((void *)&t1234, (void *)NULL, cx, NULL, t1229, t1230, t1231, t1233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1234))[0] != 0)) {
                    gm_tcfa6l7reoosn t1235;
                    GM_ST(uint8_t, (uint8_t *)&t1235 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1235 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1235 + 4, 0);
                    ((uint8_t *)&t1235)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1235 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1234) + 8)));
                    F.ret = t1235;
                    goto gm_fret;
                }
                gm_tbl8il92l97ut t1236 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t1234) + 8));
                gm_l120 = t1236;
            }
            {
                void * t1238 = gm_l119;
                if (!(GM_LD(void *, ((uint8_t *)&t1238)) != 0)) goto ie1237;
                void * t1239 = GM_LD(void *, ((uint8_t *)&t1238));
                gm_l121 = t1239;
                {
                    void * t1242 = gm_l121;
                    uint64_t t1243 = GM_LD(uint64_t, (((uint8_t *)t1242) + 104));
                    uint8_t t1244 = ((t1243 == ((uint64_t)0ULL)));
                    if (!t1244) goto ie1241;
                    {
                        void * t1246 = gm_l0;
                        gm_tbl8il92l97ut t1247 = gm_l1;
                        void * t1248 = gm_l121;
                        gm_tcfa6l7reoosn t1249;
                        gm_f_ev_discard_8c4a8jg((void *)&t1249, (void *)NULL, cx, NULL, t1246, t1247, t1248);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t1249))[0] != 0)) {
                            gm_tcfa6l7reoosn t1250;
                            GM_ST(uint8_t, (uint8_t *)&t1250 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1250 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1250 + 4, 0);
                            ((uint8_t *)&t1250)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1250 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1249) + 8)));
                            F.ret = t1250;
                            goto gm_fret;
                        }
                        gm_b0_1 t1251 = GM_LD(gm_b0_1, (((uint8_t *)&t1249) + 8));
                    }
                    goto ix1241;
ie1241:;
                    {
                        void * t1253 = GM_LD(void *, (((uint8_t *)&gm_l120) + 0));
                        uint32_t t1254 = GM_LD(uint32_t, (((uint8_t *)&gm_l120) + 8));
                        gm_th2lqbo77pvsc t1255;
                        gm_f_label_dst_b5cat1e((void *)&t1255, t1253, t1254);
                        gm_b16_8 t1256;
                        if (((((uint8_t *)&t1255))[16] != 0)) {
                            gm_b16_8 t1257 = GM_LD(gm_b16_8, (((uint8_t *)&t1255) + 0));
                            t1256 = t1257;
                        } else {
                            void * t1258 = gm_l0;
                            void * t1259 = gm_l2;
                            gm_tl874t2atnlh3 t1260 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1259) + 112));
                            gm_b16_8 t1261;
                            GM_ST(void *, (uint8_t *)&t1261, (void *)(gm_s1kvbdtols1cc));
                            GM_ST(uintptr_t, (uint8_t *)&t1261 + 8, (uintptr_t)(52));
                            gm_t76123gfet73g t1262;
                            gm_f_trap_qh84l9d((void *)&t1262, (void *)NULL, cx, NULL, t1258, t1260, t1261);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1262);
                            goto gm_fdone;
                        }
                        gm_l122 = t1256;
                    }
                    {
                        void * t1263 = gm_l0;
                        gm_tbl8il92l97ut t1264 = gm_l1;
                        void * t1265 = gm_l121;
                        gm_b16_8 t1266 = gm_l122;
                        gm_tcfa6l7reoosn t1267;
                        gm_f_ev_25jfdar((void *)&t1267, (void *)NULL, cx, NULL, t1263, t1264, t1265, t1266);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t1267))[0] != 0)) {
                            gm_tcfa6l7reoosn t1268;
                            GM_ST(uint8_t, (uint8_t *)&t1268 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t1268 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t1268 + 4, 0);
                            ((uint8_t *)&t1268)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1268 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1267) + 8)));
                            F.ret = t1268;
                            goto gm_fret;
                        }
                        gm_b0_1 t1269 = GM_LD(gm_b0_1, (((uint8_t *)&t1267) + 8));
                    }
ix1241:;
                }
                goto ix1237;
ie1237:;
ix1237:;
            }
            {
                void * t1270 = GM_LD(void *, (((uint8_t *)&gm_l120) + 0));
                uint64_t t1271 = GM_LD(uint64_t, (((uint8_t *)t1270) + 0));
                uint32_t t1272 = GM_LD(uint32_t, (((uint8_t *)&gm_l120) + 8));
                gm_tn68el8d14vku t1273;
                GM_ST(uint32_t, (uint8_t *)&t1273 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t1273 + 0), t1271);
                GM_ST(uint32_t, ((uint8_t *)&t1273 + 8), t1272);
                gm_t76123gfet73g t1274;
                GM_ST(uint8_t, (uint8_t *)&t1274 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1274 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1274 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1274 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t1274 + 8), t1273);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1274);
                goto gm_fdone;
            }
            goto me4;
mn1224:;
ma48:;
            uint8_t t1276 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1276 != ((uint8_t)55ULL)) goto mn1275;
            gm_te9hrk0u35rev t1277 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t3) + 0) + 8));
            gm_l123 = t1277;
            {
                uint8_t t1280 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l123) + 0) == ((uint8_t)1ULL)));
                if (!t1280) goto ie1279;
                {
                    void * t1282 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                    uint64_t t1283 = GM_LD(uint64_t, (((uint8_t *)t1282) + 0));
                    gm_tf5srh4mp4b3i t1284;
                    GM_ST(uint8_t, (uint8_t *)&t1284 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1284 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1284 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t1284 + 0), t1283);
                    GM_ST(uint8_t, ((uint8_t *)&t1284 + 8), ((uint8_t)0));
                    gm_t76123gfet73g t1285;
                    GM_ST(uint8_t, (uint8_t *)&t1285 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1285 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1285 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1285 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t1285 + 8), t1284);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1285);
                    goto gm_fdone;
                }
                goto ix1279;
ie1279:;
ix1279:;
            }
            {
                void * t1286 = gm_l0;
                gm_tbl8il92l97ut t1287 = gm_l1;
                gm_te9hrk0u35rev t1288 = gm_l123;
                void * t1289 = gm_l2;
                gm_tl874t2atnlh3 t1290 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1289) + 112));
                gm_t9b5r56q9jk9c t1291;
                gm_f_resolve_target_bdf5i1e((void *)&t1291, (void *)NULL, cx, NULL, t1286, t1287, t1288, t1290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1291))[0] != 0)) {
                    gm_tcfa6l7reoosn t1292;
                    GM_ST(uint8_t, (uint8_t *)&t1292 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1292 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1292 + 4, 0);
                    ((uint8_t *)&t1292)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1292 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1291) + 8)));
                    F.ret = t1292;
                    goto gm_fret;
                }
                gm_tbl8il92l97ut t1293 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)&t1291) + 8));
                gm_l124 = t1293;
            }
            {
                void * t1294 = GM_LD(void *, (((uint8_t *)&gm_l124) + 0));
                uint64_t t1295 = GM_LD(uint64_t, (((uint8_t *)t1294) + 0));
                uint32_t t1296 = GM_LD(uint32_t, (((uint8_t *)&gm_l124) + 8));
                gm_tn68el8d14vku t1297;
                GM_ST(uint32_t, (uint8_t *)&t1297 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t1297 + 0), t1295);
                GM_ST(uint32_t, ((uint8_t *)&t1297 + 8), t1296);
                gm_t76123gfet73g t1298;
                GM_ST(uint8_t, (uint8_t *)&t1298 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1298 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1298 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1298 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tn68el8d14vku, ((uint8_t *)&t1298 + 8), t1297);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1298);
                goto gm_fdone;
            }
            goto me4;
mn1275:;
ma49:;
            uint8_t t1300 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1300 != ((uint8_t)56ULL)) goto mn1299;
            uint32_t t1301 = GM_LD(uint32_t, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l125 = t1301;
            void * t1302 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l126 = t1302;
            void * t1303 = gm_l0;
            gm_tbl8il92l97ut t1304 = gm_l1;
            uint32_t t1305 = gm_l125;
            void * t1306 = gm_l126;
            void * t1307 = gm_l2;
            gm_tcfa6l7reoosn t1308;
            gm_f_ev_return_8ggbvjj((void *)&t1308, (void *)NULL, cx, NULL, t1303, t1304, t1305, t1306, t1307);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1308))[0] != 0)) {
                gm_tcfa6l7reoosn t1309;
                GM_ST(uint8_t, (uint8_t *)&t1309 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1309 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1309 + 4, 0);
                ((uint8_t *)&t1309)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1309 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1308) + 8)));
                F.ret = t1309;
                goto gm_fret;
            }
            gm_b0_1 t1310 = GM_LD(gm_b0_1, (((uint8_t *)&t1308) + 8));
            goto me4;
mn1299:;
ma50:;
            uint8_t t1312 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1312 != ((uint8_t)57ULL)) goto mn1311;
            void * t1313 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l127 = t1313;
            {
                void * t1315 = gm_l0;
                gm_t2lqu8jsj511d t1316 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t1315));
                gm_l128 = t1316;
            }
            {
                void * t1317 = gm_l0;
                void * t1318 = gm_l127;
                uint64_t t1319 = GM_LD(uint64_t, (((uint8_t *)t1318) + 104));
                gm_b16_8 t1320 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t1317, t1319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l129 = t1320;
            }
            {
                void * t1321 = gm_l0;
                gm_tbl8il92l97ut t1322 = gm_l1;
                void * t1323 = gm_l127;
                gm_b16_8 t1324 = gm_l129;
                gm_tcfa6l7reoosn t1325;
                gm_f_ev_25jfdar((void *)&t1325, (void *)NULL, cx, NULL, t1321, t1322, t1323, t1324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1325))[0] != 0)) {
                    gm_tcfa6l7reoosn t1326;
                    GM_ST(uint8_t, (uint8_t *)&t1326 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1326 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1326 + 4, 0);
                    ((uint8_t *)&t1326)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1326 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1325) + 8)));
                    F.ret = t1326;
                    goto gm_fret;
                }
                gm_b0_1 t1327 = GM_LD(gm_b0_1, (((uint8_t *)&t1325) + 8));
            }
            {
                void * t1328 = gm_l0;
                void * t1329 = gm_l2;
                gm_tl874t2atnlh3 t1330 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1329) + 112));
                gm_b16_8 t1331 = gm_l129;
                gm_b16_8 t1332 = GM_LD(gm_b16_8, ((uint8_t *)&t1331));
                gm_td0ept12e7q8b t1333;
                gm_f_read_string_ttuk4gr((void *)&t1333, (void *)NULL, cx, NULL, t1328, t1330, t1332);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1333))[0] != 0)) {
                    gm_tcfa6l7reoosn t1334;
                    GM_ST(uint8_t, (uint8_t *)&t1334 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1334 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1334 + 4, 0);
                    ((uint8_t *)&t1334)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1334 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1333) + 8)));
                    F.ret = t1334;
                    goto gm_fret;
                }
                gm_b16_8 t1335 = GM_LD(gm_b16_8, (((uint8_t *)&t1333) + 8));
                gm_l130 = t1335;
            }
            {
                void * t1336 = gm_l0;
                gm_t2lqu8jsj511d t1337 = gm_l128;
                gm_f_release_jn5kqdi(t1336, GM_CONV(gm_csc7lc6jlkqkuq, t1337));
            }
            {
                void * t1338 = gm_l0;
                void * t1339 = gm_l2;
                gm_tl874t2atnlh3 t1340 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1339) + 112));
                gm_b16_8 t1341 = gm_l130;
                gm_t76123gfet73g t1342;
                gm_f_trap_qh84l9d((void *)&t1342, (void *)NULL, cx, NULL, t1338, t1340, t1341);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1342);
                goto gm_fdone;
            }
            goto me4;
mn1311:;
ma51:;
            uint8_t t1344 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1344 != ((uint8_t)58ULL)) goto mn1343;
            void * t1345 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 0));
            gm_l131 = t1345;
            void * t1346 = GM_LD(void *, (((((uint8_t *)t3) + 0) + 8) + 8));
            gm_l132 = t1346;
            void * t1347 = gm_l0;
            gm_tbl8il92l97ut t1348 = gm_l1;
            void * t1349 = gm_l131;
            void * t1350 = gm_l132;
            void * t1351 = gm_l2;
            gm_tcfa6l7reoosn t1352;
            gm_f_resume__mi7hk2f((void *)&t1352, (void *)NULL, cx, NULL, t1347, t1348, t1349, t1350, t1351);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1352))[0] != 0)) {
                gm_tcfa6l7reoosn t1353;
                GM_ST(uint8_t, (uint8_t *)&t1353 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1353 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1353 + 4, 0);
                ((uint8_t *)&t1353)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1353 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1352) + 8)));
                F.ret = t1353;
                goto gm_fret;
            }
            gm_b0_1 t1354 = GM_LD(gm_b0_1, (((uint8_t *)&t1352) + 8));
            goto me4;
mn1343:;
ma52:;
            uint8_t t1356 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1356 != ((uint8_t)59ULL)) goto mn1355;
            void * t1357 = GM_LD(void *, ((((uint8_t *)t3) + 0) + 8));
            gm_l133 = t1357;
            void * t1358 = gm_l0;
            gm_tbl8il92l97ut t1359 = gm_l1;
            void * t1360 = gm_l133;
            void * t1361 = gm_l2;
            gm_tcfa6l7reoosn t1362;
            gm_f_cancel__tb288ki((void *)&t1362, (void *)NULL, cx, NULL, t1358, t1359, t1360, t1361);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1362))[0] != 0)) {
                gm_tcfa6l7reoosn t1363;
                GM_ST(uint8_t, (uint8_t *)&t1363 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1363 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1363 + 4, 0);
                ((uint8_t *)&t1363)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1363 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1362) + 8)));
                F.ret = t1363;
                goto gm_fret;
            }
            gm_b0_1 t1364 = GM_LD(gm_b0_1, (((uint8_t *)&t1362) + 8));
            goto me4;
mn1355:;
ma53:;
            uint8_t t1366 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1366 != ((uint8_t)60ULL)) goto mn1365;
            gm_tntn2tf15puib t1367 = GM_LD(gm_tntn2tf15puib, ((((uint8_t *)t3) + 0) + 8));
            F.l134 = t1367;
            void * t1368 = gm_l0;
            gm_tbl8il92l97ut t1369 = gm_l1;
            gm_b16_8 t1370 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 40));
            gm_b16_8 t1371 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 56));
            gm_b16_8 t1372 = GM_LD(gm_b16_8, (((uint8_t *)&F.l134) + 72));
            void * t1373 = gm_l2;
            gm_tcfa6l7reoosn t1374;
            gm_f_write__eua2bmh((void *)&t1374, (void *)NULL, cx, NULL, t1368, t1369, &(*(gm_th1g69d7vpipg *)(((uint8_t *)&F.l134) + 0)), t1370, t1371, t1372, t1373);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1374))[0] != 0)) {
                gm_tcfa6l7reoosn t1375;
                GM_ST(uint8_t, (uint8_t *)&t1375 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1375 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1375 + 4, 0);
                ((uint8_t *)&t1375)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1375 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1374) + 8)));
                F.ret = t1375;
                goto gm_fret;
            }
            gm_b0_1 t1376 = GM_LD(gm_b0_1, (((uint8_t *)&t1374) + 8));
            goto me4;
mn1365:;
ma54:;
            uint8_t t1378 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1378 != ((uint8_t)61ULL)) goto mn1377;
            gm_t710lsrq1m8gk t1379 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 0) + 8));
            F.l135 = t1379;
            void * t1380 = gm_l0;
            gm_tbl8il92l97ut t1381 = gm_l1;
            void * t1382 = gm_l2;
            gm_b16_8 t1383 = gm_l3;
            gm_tcfa6l7reoosn t1384;
            gm_f_with__u1jp2ib((void *)&t1384, (void *)NULL, cx, NULL, t1380, t1381, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l135)), t1382, t1383);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1384))[0] != 0)) {
                gm_tcfa6l7reoosn t1385;
                GM_ST(uint8_t, (uint8_t *)&t1385 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1385 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1385 + 4, 0);
                ((uint8_t *)&t1385)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1385 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1384) + 8)));
                F.ret = t1385;
                goto gm_fret;
            }
            gm_b0_1 t1386 = GM_LD(gm_b0_1, (((uint8_t *)&t1384) + 8));
            goto me4;
mn1377:;
ma55:;
            uint8_t t1388 = GM_LD(uint8_t, ((((uint8_t *)t3) + 0) + 0));
            if (t1388 != ((uint8_t)62ULL)) goto mn1387;
            void * t1389 = gm_l0;
            void * t1390 = gm_l2;
            gm_tl874t2atnlh3 t1391 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t1390) + 112));
            gm_b16_8 t1392;
            GM_ST(void *, (uint8_t *)&t1392, (void *)(gm_spic5gjomd7lu));
            GM_ST(uintptr_t, (uint8_t *)&t1392 + 8, (uintptr_t)(61));
            gm_t76123gfet73g t1393;
            gm_f_trap_qh84l9d((void *)&t1393, (void *)NULL, cx, NULL, t1389, t1391, t1392);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1393);
            goto gm_fdone;
            goto me4;
mn1387:;
ma56:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 134, 5);
me4:;
        }
    }
    gm_b0_1 t1394;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1394);
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
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ew_c5kil07 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    uint64_t gm_l3; /* v */
    uint32_t gm_l4; /* l */
    uint64_t gm_l5; /* off */
    void * gm_l6; /* fr */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* o */
    gm_tkm39klvnn4cr gm_l10; /* x */
    uint64_t gm_l11; /* p */
    uint64_t gm_l12; /* q */
    gm_ta6jn77i9rmqf gm_l13; /* x */
    uint64_t gm_l14; /* p */
    uint64_t gm_l15; /* q */
    void * gm_l16; /* x */
    uint8_t gm_l17; /* is_and */
    void * gm_l18; /* l */
    void * gm_l19; /* r */
    uint8_t gm_l20; /* v */
    gm_t9qkh0rsooc8d gm_l21; /* op */
    gm_ttovhfee8mf28 gm_l22; /* t */
    void * gm_l23; /* x */
    uint64_t gm_l24; /* v */
    gm_tvdg94bk7aa22 gm_l25; /* op */
    gm_ttovhfee8mf28 gm_l26; /* from */
    gm_ttovhfee8mf28 gm_l27; /* to */
    void * gm_l28; /* x */
    uint64_t gm_l29; /* v */
    uint64_t gm_l31; /* v */
    uint64_t gm_l32; /* k */
    gm_tvdg94bk7aa22 gm_l33; /* op */
    gm_ttovhfee8mf28 gm_l34; /* t */
    void * gm_l35; /* x */
    uint64_t gm_l36; /* v */
    gm_tuggr5sml9o52 gm_l37; /* x */
    uint64_t gm_l38; /* p */
    uint64_t gm_l39; /* q */
    uint32_t gm_l40; /* f */
    gm_b16_8 gm_l41; /* args */
    gm_t2doi72kmmlil gm_l42; /* b */
    gm_b16_8 gm_l43; /* d */
    gm_trl2khmuphncd gm_l44; /* x */
    void * gm_l45; /* el */
    void * gm_l46; /* b */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    void * gm_l50; /* t */
    gm_b16_8 gm_l51; /* h */
    uint64_t gm_l52; /* addr */
    uint64_t gm_l53; /* addr */
    GM_STACK_CHECK(&F, 4863u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma20; case ((uint8_t)12ULL): goto ma19; case ((uint8_t)13ULL): goto ma20; case ((uint8_t)14ULL): goto ma20; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma20; case ((uint8_t)31ULL): goto ma20; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma20; case ((uint8_t)34ULL): goto ma20; case ((uint8_t)35ULL): goto ma16; case ((uint8_t)36ULL): goto ma20; case ((uint8_t)37ULL): goto ma20; case ((uint8_t)38ULL): goto ma20; case ((uint8_t)39ULL): goto ma20; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma18; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma20; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma20; case ((uint8_t)59ULL): goto ma20; case ((uint8_t)60ULL): goto ma20; case ((uint8_t)61ULL): goto ma20; case ((uint8_t)62ULL): goto ma20; default: goto ma20; }
ma4:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t2) + 0) + 8));
        gm_l3 = t25;
        uint64_t t26 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t26);
        goto gm_fdone;
        goto me3;
mn23:;
ma5:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t28 != ((uint8_t)6ULL)) goto mn27;
        uint32_t t29 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l4 = t29;
        uint64_t t30 = GM_LD(uint64_t, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l5 = t30;
        {
            void * t32 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_l6 = t32;
        }
        {
            uint32_t t33 = gm_l4;
            uint64_t t34 = (uint64_t)t33;
            uint64_t t35 = gm_f_us_cuv01fp(t34);
            gm_l7 = t35;
        }
        {
            uint64_t t37 = gm_l7;
            uint64_t t38 = (uintptr_t)t37;
            void * t39 = gm_l6;
            uint64_t t40 = GM_LD(uintptr_t, (((uint8_t *)t39) + 64) + 8);
            if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_src0jbci4ff2esa, 42, 16);
            uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t39) + 64)) + t38 * 8));
            uint8_t t42 = (!(t41 == ((uint64_t)0ULL)));
            if (!t42) goto ie36;
            {
                void * t44 = gm_l0;
                void * t45 = gm_l2;
                gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 112));
                uint64_t t47 = gm_l7;
                uint64_t t48 = (uintptr_t)t47;
                void * t49 = gm_l6;
                uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)t49) + 64) + 8);
                if (t48 >= (uintptr_t)(t50)) gm_trap_index(t48, t50, gm_src0jbci4ff2esa, 42, 64);
                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t49) + 64)) + t48 * 8));
                uint64_t t52 = gm_l7;
                uint64_t t53 = (uintptr_t)t52;
                void * t54 = gm_l6;
                void * t55 = GM_LD(void *, (((uint8_t *)t54) + 16));
                uint64_t t56 = GM_LD(uintptr_t, (((uint8_t *)t55) + 104) + 8);
                if (t53 >= (uintptr_t)(t56)) gm_trap_index(t53, t56, gm_src0jbci4ff2esa, 42, 75);
                uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t55) + 104)) + t53 * 8));
                void * t58 = gm_l2;
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 104));
                gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t44, t46, t51, t57, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix36;
ie36:;
ix36:;
        }
        {
            uint64_t t60 = gm_l5;
            uint64_t t61 = gm_f_us_cuv01fp(t60);
            gm_l8 = t61;
        }
        {
            void * t62 = gm_l0;
            void * t63 = gm_l6;
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 48));
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&t64) + 8);
            void * t66 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t64)));
            uint64_t t67 = gm_l8;
            uint64_t t68 = (uintptr_t)t67;
            uint64_t t69 = gm_l8;
            void * t70 = gm_l2;
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 104));
            uint64_t t72 = gm_f_us_cuv01fp(t71);
            uint64_t r73; bool o74 = __builtin_add_overflow(t69, t72, &r73);
            if (o74) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 44, 42);
            uint64_t t75 = r73;
            uint64_t t76 = (uintptr_t)t75;
            if (t68 > t76 || t76 > (uintptr_t)t65) gm_trap_slice(t68, t76, t65, gm_src0jbci4ff2esa, 44, 30);
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(((uint8_t *)t66 + t68 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(t76 - t68));
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            uint64_t t79 = gm_f_rd_word_8flcq6o(t62, t78);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t79);
            goto gm_fdone;
        }
        goto me3;
mn27:;
ma6:;
        uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t81 != ((uint8_t)21ULL)) goto mn80;
        gm_t0pc2c7dafsjt t82 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t2) + 0) + 8));
        F.l9 = t82;
        void * t83 = gm_l0;
        gm_tbl8il92l97ut t84 = gm_l1;
        gm_ts6oqppjh0ba3 t85 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&F.l9) + 0));
        gm_ttovhfee8mf28 t86 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l9) + 4));
        gm_ttovhfee8mf28 t87 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l9) + 12));
        void * t88 = GM_LD(void *, (((uint8_t *)&F.l9) + 24));
        void * t89 = GM_LD(void *, (((uint8_t *)&F.l9) + 32));
        void * t90 = gm_l2;
        gm_f_int_op_5g0lv3e((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t83, t84, t85, t86, t87, t88, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn80:;
ma7:;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t92 != ((uint8_t)23ULL)) goto mn91;
        gm_tkm39klvnn4cr t93 = GM_LD(gm_tkm39klvnn4cr, ((((uint8_t *)t2) + 0) + 8));
        gm_l10 = t93;
        {
            void * t95 = gm_l0;
            gm_tbl8il92l97ut t96 = gm_l1;
            void * t97 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
            gm_t833u93mr4iob t98;
            gm_f_ew_c5kil07((void *)&t98, (void *)NULL, cx, NULL, t95, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t98))[0] != 0)) {
                gm_t833u93mr4iob t99;
                GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
                ((uint8_t *)&t99)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t99 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t98) + 8)));
                F.ret = t99;
                goto gm_fret;
            }
            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&t98) + 8));
            gm_l11 = t100;
        }
        {
            void * t101 = gm_l0;
            gm_tbl8il92l97ut t102 = gm_l1;
            void * t103 = GM_LD(void *, (((uint8_t *)&gm_l10) + 24));
            gm_t833u93mr4iob t104;
            gm_f_ew_c5kil07((void *)&t104, (void *)NULL, cx, NULL, t101, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t104))[0] != 0)) {
                gm_t833u93mr4iob t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                ((uint8_t *)&t105)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t105 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t104) + 8)));
                F.ret = t105;
                goto gm_fret;
            }
            uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&t104) + 8));
            gm_l12 = t106;
        }
        {
            gm_t833u93mr4iob t107;
            gm_ts6oqppjh0ba3 t109 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l10) + 0));
            gm_tmn7n0n9raou5 t110 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l10) + 4));
            uint64_t t111 = gm_l11;
            uint64_t t112 = gm_l12;
            uint8_t t113 = gm_f_word_less_l13biee(t109, t110, t111, t112);
            if (!t113) goto ie108;
            uint64_t t115 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t116;
            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t116 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t116 + 24, 0);
            ((uint8_t *)&t116)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t116 + 8), t115);
            t107 = t116;
            goto ix108;
ie108:;
            uint64_t t118 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 24, 0);
            ((uint8_t *)&t119)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t119 + 8), t118);
            t107 = t119;
ix108:;
            uint8_t t120 = ((((uint8_t *)&t107))[0] != 0);
            F.ret = t107;
            goto gm_fret;
        }
        goto me3;
mn91:;
ma8:;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t122 != ((uint8_t)24ULL)) goto mn121;
        gm_ta6jn77i9rmqf t123 = GM_LD(gm_ta6jn77i9rmqf, ((((uint8_t *)t2) + 0) + 8));
        gm_l13 = t123;
        {
            void * t125 = gm_l0;
            gm_tbl8il92l97ut t126 = gm_l1;
            void * t127 = GM_LD(void *, (((uint8_t *)&gm_l13) + 8));
            gm_t833u93mr4iob t128;
            gm_f_ew_c5kil07((void *)&t128, (void *)NULL, cx, NULL, t125, t126, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t128))[0] != 0)) {
                gm_t833u93mr4iob t129;
                GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                ((uint8_t *)&t129)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t129 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t128) + 8)));
                F.ret = t129;
                goto gm_fret;
            }
            uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&t128) + 8));
            gm_l14 = t130;
        }
        {
            void * t131 = gm_l0;
            gm_tbl8il92l97ut t132 = gm_l1;
            void * t133 = GM_LD(void *, (((uint8_t *)&gm_l13) + 16));
            gm_t833u93mr4iob t134;
            gm_f_ew_c5kil07((void *)&t134, (void *)NULL, cx, NULL, t131, t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t134))[0] != 0)) {
                gm_t833u93mr4iob t135;
                GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                ((uint8_t *)&t135)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t135 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t134) + 8)));
                F.ret = t135;
                goto gm_fret;
            }
            uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)&t134) + 8));
            gm_l15 = t136;
        }
        {
            gm_t833u93mr4iob t137;
            gm_tmn7n0n9raou5 t139 = GM_LD(gm_tmn7n0n9raou5, (((uint8_t *)&gm_l13) + 0));
            uint64_t t140 = gm_l14;
            uint64_t t141 = gm_l15;
            uint8_t t142 = gm_f_word_eq_8jc0uot(t139, t140, t141);
            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
            uint8_t t144 = (!(t142 == t143));
            if (!t144) goto ie138;
            uint64_t t146 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t147 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t147 + 24, 0);
            ((uint8_t *)&t147)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t147 + 8), t146);
            t137 = t147;
            goto ix138;
ie138:;
            uint64_t t149 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t150;
            GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t150 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t150 + 24, 0);
            ((uint8_t *)&t150)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t150 + 8), t149);
            t137 = t150;
ix138:;
            uint8_t t151 = ((((uint8_t *)&t137))[0] != 0);
            F.ret = t137;
            goto gm_fret;
        }
        goto me3;
mn121:;
ma9:;
        uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t153 != ((uint8_t)25ULL)) goto mn152;
        void * t154 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l16 = t154;
        gm_t833u93mr4iob t155;
        void * t157 = gm_l0;
        gm_tbl8il92l97ut t158 = gm_l1;
        void * t159 = gm_l16;
        gm_t833u93mr4iob t160;
        gm_f_ew_c5kil07((void *)&t160, (void *)NULL, cx, NULL, t157, t158, t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t160))[0] != 0)) {
            gm_t833u93mr4iob t161;
            GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
            ((uint8_t *)&t161)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t161 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t160) + 8)));
            F.ret = t161;
            goto gm_fret;
        }
        uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&t160) + 8));
        uint8_t t163 = ((t162 == ((uint64_t)0ULL)));
        if (!t163) goto ie156;
        uint64_t t165 = ((uint64_t)1ULL);
        gm_t833u93mr4iob t166;
        GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t166 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t166 + 24, 0);
        ((uint8_t *)&t166)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t166 + 8), t165);
        t155 = t166;
        goto ix156;
ie156:;
        uint64_t t168 = ((uint64_t)0ULL);
        gm_t833u93mr4iob t169;
        GM_ST(uint8_t, (uint8_t *)&t169 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t169 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t169 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t169 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t169 + 24, 0);
        ((uint8_t *)&t169)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t169 + 8), t168);
        t155 = t169;
ix156:;
        uint8_t t170 = ((((uint8_t *)&t155))[0] != 0);
        F.ret = t155;
        goto gm_fret;
        goto me3;
mn152:;
ma10:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t172 != ((uint8_t)32ULL)) goto mn171;
        uint8_t t173 = GM_LD(uint8_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l17 = t173;
        void * t174 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l18 = t174;
        void * t175 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 16));
        gm_l19 = t175;
        {
            void * t177 = gm_l0;
            gm_tbl8il92l97ut t178 = gm_l1;
            void * t179 = gm_l18;
            gm_t833u93mr4iob t180;
            gm_f_ew_c5kil07((void *)&t180, (void *)NULL, cx, NULL, t177, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t180))[0] != 0)) {
                gm_t833u93mr4iob t181;
                GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
                ((uint8_t *)&t181)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t181 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t180) + 8)));
                F.ret = t181;
                goto gm_fret;
            }
            uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&t180) + 8));
            uint8_t t183 = (!(t182 == ((uint64_t)0ULL)));
            gm_l20 = t183;
        }
        {
            uint8_t t185 = gm_l20;
            uint8_t t186 = gm_l17;
            uint8_t t187 = (!(t185 == t186));
            if (!t187) goto ie184;
            {
                gm_t833u93mr4iob t189;
                uint8_t t191 = gm_l20;
                if (!t191) goto ie190;
                uint64_t t193 = ((uint64_t)1ULL);
                gm_t833u93mr4iob t194;
                GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t194 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t194 + 24, 0);
                ((uint8_t *)&t194)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t194 + 8), t193);
                t189 = t194;
                goto ix190;
ie190:;
                uint64_t t196 = ((uint64_t)0ULL);
                gm_t833u93mr4iob t197;
                GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t197 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t197 + 24, 0);
                ((uint8_t *)&t197)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t197 + 8), t196);
                t189 = t197;
ix190:;
                uint8_t t198 = ((((uint8_t *)&t189))[0] != 0);
                F.ret = t189;
                goto gm_fret;
            }
            goto ix184;
ie184:;
ix184:;
        }
        {
            gm_t833u93mr4iob t199;
            void * t201 = gm_l0;
            gm_tbl8il92l97ut t202 = gm_l1;
            void * t203 = gm_l19;
            gm_t833u93mr4iob t204;
            gm_f_ew_c5kil07((void *)&t204, (void *)NULL, cx, NULL, t201, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t204))[0] != 0)) {
                gm_t833u93mr4iob t205;
                GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                ((uint8_t *)&t205)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t205 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t204) + 8)));
                F.ret = t205;
                goto gm_fret;
            }
            uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&t204) + 8));
            uint8_t t207 = (!(t206 == ((uint64_t)0ULL)));
            if (!t207) goto ie200;
            uint64_t t209 = ((uint64_t)1ULL);
            gm_t833u93mr4iob t210;
            GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t210 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t210 + 24, 0);
            ((uint8_t *)&t210)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t210 + 8), t209);
            t199 = t210;
            goto ix200;
ie200:;
            uint64_t t212 = ((uint64_t)0ULL);
            gm_t833u93mr4iob t213;
            GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t213 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t213 + 24, 0);
            ((uint8_t *)&t213)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t213 + 8), t212);
            t199 = t213;
ix200:;
            uint8_t t214 = ((((uint8_t *)&t199))[0] != 0);
            F.ret = t199;
            goto gm_fret;
        }
        goto me3;
mn171:;
ma11:;
        uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t216 != ((uint8_t)26ULL)) goto mn215;
        gm_t9qkh0rsooc8d t217 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l21 = t217;
        gm_ttovhfee8mf28 t218 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 4));
        gm_l22 = t218;
        void * t219 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 16));
        gm_l23 = t219;
        {
            void * t221 = gm_l0;
            gm_tbl8il92l97ut t222 = gm_l1;
            void * t223 = gm_l23;
            gm_t833u93mr4iob t224;
            gm_f_ew_c5kil07((void *)&t224, (void *)NULL, cx, NULL, t221, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t224))[0] != 0)) {
                gm_t833u93mr4iob t225;
                GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                ((uint8_t *)&t225)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t225 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t224) + 8)));
                F.ret = t225;
                goto gm_fret;
            }
            uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&t224) + 8));
            gm_l24 = t226;
        }
        {
            void * t227 = gm_l0;
            gm_t9qkh0rsooc8d t228 = gm_l21;
            gm_ttovhfee8mf28 t229 = gm_l22;
            uint64_t t230 = gm_l24;
            void * t231 = gm_l2;
            gm_f_int_un_sm4tv7d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t227, t228, t229, t230, t231);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn215:;
ma12:;
        uint8_t t233 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t233 != ((uint8_t)27ULL)) goto mn232;
        gm_tvdg94bk7aa22 t234 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l25 = t234;
        gm_ttovhfee8mf28 t235 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 12));
        gm_l26 = t235;
        gm_ttovhfee8mf28 t236 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 20));
        gm_l27 = t236;
        void * t237 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 32));
        gm_l28 = t237;
        {
            void * t239 = gm_l0;
            gm_tbl8il92l97ut t240 = gm_l1;
            void * t241 = gm_l28;
            gm_t833u93mr4iob t242;
            gm_f_ew_c5kil07((void *)&t242, (void *)NULL, cx, NULL, t239, t240, t241);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t242))[0] != 0)) {
                gm_t833u93mr4iob t243;
                GM_ST(uint8_t, (uint8_t *)&t243 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t243 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t243 + 4, 0);
                ((uint8_t *)&t243)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t243 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t242) + 8)));
                F.ret = t243;
                goto gm_fret;
            }
            uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&t242) + 8));
            gm_l29 = t244;
        }
        {
            gm_tvdg94bk7aa22 t245 = gm_l25;
            gm_ttovhfee8mf28 t246 = gm_l26;
            gm_ttovhfee8mf28 t247 = gm_l27;
            uint64_t t248 = gm_l29;
            uint64_t t249 = gm_f_int_conv_rqdvurn(t245, GM_CONV(gm_csfd6et11lk21p, t246), GM_CONV(gm_csfd6et11lk21p, t247), t248);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t249);
            goto gm_fdone;
        }
        goto me3;
mn232:;
ma13:;
        uint8_t t251 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t251 != ((uint8_t)28ULL)) goto mn250;
        gm_tscalq56heofe t252 = GM_LD(gm_tscalq56heofe, ((((uint8_t *)t2) + 0) + 8));
        F.l30 = t252;
        {
            void * t254 = gm_l0;
            gm_tbl8il92l97ut t255 = gm_l1;
            void * t256 = GM_LD(void *, (((uint8_t *)&F.l30) + 24));
            gm_t833u93mr4iob t257;
            gm_f_ew_c5kil07((void *)&t257, (void *)NULL, cx, NULL, t254, t255, t256);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t257))[0] != 0)) {
                gm_t833u93mr4iob t258;
                GM_ST(uint8_t, (uint8_t *)&t258 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t258 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t258 + 4, 0);
                ((uint8_t *)&t258)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t258 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t257) + 8)));
                F.ret = t258;
                goto gm_fret;
            }
            uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&t257) + 8));
            gm_l31 = t259;
        }
        {
            void * t260 = gm_l0;
            gm_tbl8il92l97ut t261 = gm_l1;
            void * t262 = GM_LD(void *, (((uint8_t *)&F.l30) + 32));
            gm_t833u93mr4iob t263;
            gm_f_ew_c5kil07((void *)&t263, (void *)NULL, cx, NULL, t260, t261, t262);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t263))[0] != 0)) {
                gm_t833u93mr4iob t264;
                GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                ((uint8_t *)&t264)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t264 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t263) + 8)));
                F.ret = t264;
                goto gm_fret;
            }
            uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&t263) + 8));
            gm_l32 = t265;
        }
        {
            gm_tvdg94bk7aa22 t266 = GM_LD(gm_tvdg94bk7aa22, (((uint8_t *)&F.l30) + 0));
            gm_ttovhfee8mf28 t267 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&F.l30) + 12));
            uint64_t t268 = gm_l31;
            uint64_t t269 = gm_l32;
            uint64_t t270 = gm_f_bit_word_7ae821g(t266, GM_CONV(gm_csfd6et11lk21p, t267), t268, t269);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t270);
            goto gm_fdone;
        }
        goto me3;
mn250:;
ma14:;
        uint8_t t272 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t272 != ((uint8_t)29ULL)) goto mn271;
        gm_tvdg94bk7aa22 t273 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l33 = t273;
        gm_ttovhfee8mf28 t274 = GM_LD(gm_ttovhfee8mf28, (((((uint8_t *)t2) + 0) + 8) + 12));
        gm_l34 = t274;
        void * t275 = GM_LD(void *, (((((uint8_t *)t2) + 0) + 8) + 24));
        gm_l35 = t275;
        {
            void * t277 = gm_l0;
            gm_tbl8il92l97ut t278 = gm_l1;
            void * t279 = gm_l35;
            gm_t833u93mr4iob t280;
            gm_f_ew_c5kil07((void *)&t280, (void *)NULL, cx, NULL, t277, t278, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t280))[0] != 0)) {
                gm_t833u93mr4iob t281;
                GM_ST(uint8_t, (uint8_t *)&t281 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t281 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t281 + 4, 0);
                ((uint8_t *)&t281)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t281 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t280) + 8)));
                F.ret = t281;
                goto gm_fret;
            }
            uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&t280) + 8));
            gm_l36 = t282;
        }
        {
            gm_tvdg94bk7aa22 t283 = gm_l33;
            gm_ttovhfee8mf28 t284 = gm_l34;
            uint64_t t285 = gm_l36;
            uint64_t t286 = gm_f_bit_word_7ae821g(t283, GM_CONV(gm_csfd6et11lk21p, t284), t285, ((uint64_t)0ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t286);
            goto gm_fdone;
        }
        goto me3;
mn271:;
ma15:;
        uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t288 != ((uint8_t)22ULL)) goto mn287;
        gm_tuggr5sml9o52 t289 = GM_LD(gm_tuggr5sml9o52, ((((uint8_t *)t2) + 0) + 8));
        gm_l37 = t289;
        {
            void * t291 = gm_l0;
            gm_tbl8il92l97ut t292 = gm_l1;
            void * t293 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
            gm_t833u93mr4iob t294;
            gm_f_ew_c5kil07((void *)&t294, (void *)NULL, cx, NULL, t291, t292, t293);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t294))[0] != 0)) {
                gm_t833u93mr4iob t295;
                GM_ST(uint8_t, (uint8_t *)&t295 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t295 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t295 + 4, 0);
                ((uint8_t *)&t295)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t295 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t294) + 8)));
                F.ret = t295;
                goto gm_fret;
            }
            uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&t294) + 8));
            gm_l38 = t296;
        }
        {
            void * t297 = gm_l0;
            gm_tbl8il92l97ut t298 = gm_l1;
            void * t299 = GM_LD(void *, (((uint8_t *)&gm_l37) + 16));
            gm_t833u93mr4iob t300;
            gm_f_ew_c5kil07((void *)&t300, (void *)NULL, cx, NULL, t297, t298, t299);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t300))[0] != 0)) {
                gm_t833u93mr4iob t301;
                GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                ((uint8_t *)&t301)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t301 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t300) + 8)));
                F.ret = t301;
                goto gm_fret;
            }
            uint64_t t302 = GM_LD(uint64_t, (((uint8_t *)&t300) + 8));
            gm_l39 = t302;
        }
        {
            gm_ts6oqppjh0ba3 t303 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l37) + 0));
            uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 2));
            uint64_t t305 = gm_l38;
            uint64_t t306 = gm_l39;
            uint64_t t307 = gm_f_float_op_osevuqr(t303, t304, t305, t306);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t307);
            goto gm_fdone;
        }
        goto me3;
mn287:;
ma16:;
        uint8_t t309 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t309 != ((uint8_t)35ULL)) goto mn308;
        uint32_t t310 = GM_LD(uint32_t, (((((uint8_t *)t2) + 0) + 8) + 0));
        gm_l40 = t310;
        gm_b16_8 t311 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 0) + 8) + 8));
        gm_l41 = t311;
        {
            gm_t2doi72kmmlil t313;
            GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 1), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 2), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 3), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 4), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 5), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 6), ((uint8_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t313 + 7), ((uint8_t)0ULL));
            gm_l42 = t313;
        }
        {
            void * t314 = ((uint8_t *)&gm_l42);
            uint64_t t315 = (uintptr_t)((uint64_t)0ULL);
            void * t316 = gm_l2;
            uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)t316) + 104));
            uint64_t t318 = gm_f_us_cuv01fp(t317);
            uint64_t t319 = (uintptr_t)t318;
            if (t315 > t319 || t319 > (uintptr_t)8) gm_trap_slice(t315, t319, 8, gm_src0jbci4ff2esa, 87, 18);
            gm_b16_8 t320;
            GM_ST(void *, (uint8_t *)&t320, (void *)(((uint8_t *)t314 + t315 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(t319 - t315));
            gm_l43 = t320;
        }
        {
            void * t321 = gm_l0;
            gm_tbl8il92l97ut t322 = gm_l1;
            uint32_t t323 = gm_l40;
            gm_b16_8 t324 = gm_l41;
            void * t325 = gm_l2;
            gm_b16_8 t326 = gm_l43;
            gm_tcfa6l7reoosn t327;
            gm_f_call_node_a1b30k9((void *)&t327, (void *)NULL, cx, NULL, t321, t322, t323, t324, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t327))[0] != 0)) {
                gm_t833u93mr4iob t328;
                GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                ((uint8_t *)&t328)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t328 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t327) + 8)));
                F.ret = t328;
                goto gm_fret;
            }
            gm_b0_1 t329 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 8));
        }
        {
            void * t330 = gm_l0;
            gm_b16_8 t331 = gm_l43;
            gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&t331));
            uint64_t t333 = gm_f_rd_word_8flcq6o(t330, t332);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t333);
            goto gm_fdone;
        }
        goto me3;
mn308:;
ma17:;
        uint8_t t335 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t335 != ((uint8_t)52ULL)) goto mn334;
        gm_trl2khmuphncd t336 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 0) + 8));
        gm_l44 = t336;
        {
            void * t339 = gm_l0;
            gm_tbl8il92l97ut t340 = gm_l1;
            gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 0));
            gm_t1ec8br9ddkav t342;
            gm_f_conds_30ust77((void *)&t342, (void *)NULL, cx, NULL, t339, t340, t341);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t342))[0] != 0)) {
                gm_t833u93mr4iob t343;
                GM_ST(uint8_t, (uint8_t *)&t343 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t343 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t343 + 4, 0);
                ((uint8_t *)&t343)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t343 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t342) + 8)));
                F.ret = t343;
                goto gm_fret;
            }
            uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&t342) + 8));
            if (!t344) goto ie338;
            {
                void * t346 = gm_l0;
                gm_tbl8il92l97ut t347 = gm_l1;
                void * t348 = GM_LD(void *, (((uint8_t *)&gm_l44) + 16));
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t346, t347, t348);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix338;
ie338:;
ix338:;
        }
        {
            void * t350 = GM_LD(void *, (((uint8_t *)&gm_l44) + 24));
            if (!(GM_LD(void *, ((uint8_t *)&t350)) != 0)) goto ie349;
            void * t351 = GM_LD(void *, ((uint8_t *)&t350));
            gm_l45 = t351;
            {
                void * t353 = gm_l0;
                gm_tbl8il92l97ut t354 = gm_l1;
                void * t355 = gm_l45;
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t353, t354, t355);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix349;
ie349:;
ix349:;
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, ((uint64_t)0ULL));
            goto gm_fdone;
        }
        goto me3;
mn334:;
ma18:;
        uint8_t t357 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t357 != ((uint8_t)51ULL)) goto mn356;
        void * t358 = GM_LD(void *, ((((uint8_t *)t2) + 0) + 8));
        gm_l46 = t358;
        {
            void * t361 = gm_l46;
            uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)t361) + 24));
            if (!t362) goto ie360;
            {
                void * t364 = gm_l0;
                gm_tbl8il92l97ut t365 = gm_l1;
                void * t366 = gm_l2;
                gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t364, t365, t366);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix360;
ie360:;
ix360:;
        }
        {
            {
                void * t368 = gm_l46;
                gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)t368) + 0));
                gm_l47 = t369;
            }
            {
                gm_l48 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t371 = gm_l48;
                    uint64_t t372 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    uint8_t t373 = (t371 < t372);
                    if (!t373) goto lb370;
                    {
                        uint64_t t376 = gm_l48;
                        uint64_t t377 = (uintptr_t)t376;
                        uint64_t t378 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                        gm_t2k9dbsv5ff58 t381 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t377 * 40));
                        F.l49 = t381;
                        {
                            void * t383 = gm_l0;
                            gm_tbl8il92l97ut t384 = gm_l1;
                            gm_tcfa6l7reoosn t385;
                            gm_f_exec_stmt_4jh6tlr((void *)&t385, (void *)NULL, cx, NULL, t383, t384, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l49)));
                            if (gm_unw.kind) {
                                goto gm_ll374;
                            }
                            if (((((uint8_t *)&t385))[0] != 0)) {
                                gm_t833u93mr4iob t386;
                                GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t386 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t386 + 4, 0);
                                ((uint8_t *)&t386)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t386 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t385) + 8)));
                                F.ret = t386;
                                goto gm_fret;
                            }
                            gm_b0_1 t387 = GM_LD(gm_b0_1, (((uint8_t *)&t385) + 8));
                        }
                        goto me379;
mn380:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 98, 13);
me379:;
                    }
lc370:;
                    {
                        uint64_t t389 = gm_l48;
                        uint64_t r390; bool o391 = __builtin_add_overflow(t389, ((uint64_t)1ULL), &r390);
                        uint64_t t392 = r390;
                        gm_l48 = t392;
                    }
                }
gm_ll374:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb370; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc370; }
                }
                goto gm_lad;
lb370:;
            }
        }
        {
            void * t394 = gm_l46;
            void * t395 = GM_LD(void *, (((uint8_t *)t394) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t395)) != 0)) goto ie393;
            void * t396 = GM_LD(void *, ((uint8_t *)&t395));
            gm_l50 = t396;
            {
                void * t398 = gm_l0;
                gm_tbl8il92l97ut t399 = gm_l1;
                void * t400 = gm_l50;
                gm_f_ew_c5kil07((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t398, t399, t400);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix393;
ie393:;
ix393:;
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, ((uint64_t)0ULL));
            goto gm_fdone;
        }
        goto me3;
mn356:;
ma19:;
        uint8_t t404 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t404 != ((uint8_t)10ULL)) goto pa403;
        goto po402;
pa403:;
        uint8_t t406 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t406 != ((uint8_t)12ULL)) goto pa405;
        goto po402;
pa405:;
        uint8_t t408 = GM_LD(uint8_t, ((((uint8_t *)t2) + 0) + 0));
        if (t408 != ((uint8_t)9ULL)) goto pa407;
        goto po402;
pa407:;
        goto mn401;
po402:;
        {
            void * t411 = gm_l2;
            uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)t411) + 124));
            uint8_t t413 = (uint8_t)!t412;
            if (!t413) goto ie410;
            {
                void * t415 = gm_l0;
                gm_tbl8il92l97ut t416 = gm_l1;
                void * t417 = gm_l2;
                gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t415, t416, t417);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix410;
ie410:;
ix410:;
        }
        {
            void * t418 = gm_l0;
            gm_tbl8il92l97ut t419 = gm_l1;
            void * t420 = gm_l2;
            gm_tl1r7a2fn1ev6 t421;
            gm_f_place_47rfqe5((void *)&t421, (void *)NULL, cx, NULL, t418, t419, t420, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t421))[0] != 0)) {
                gm_t833u93mr4iob t422;
                GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                ((uint8_t *)&t422)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t422 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t421) + 8)));
                F.ret = t422;
                goto gm_fret;
            }
            uint8_t t429 = GM_LD(uint8_t, ((((uint8_t *)&t421) + 8) + 0));
            switch (t429) { case ((uint8_t)0ULL): goto ma424; case ((uint8_t)1ULL): goto ma425; case ((uint8_t)2ULL): goto ma426; case ((uint8_t)3ULL): goto ma427; default: goto ma428; }
ma424:;
            uint8_t t431 = GM_LD(uint8_t, ((((uint8_t *)&t421) + 8) + 0));
            if (t431 != ((uint8_t)0ULL)) goto mn430;
            gm_b16_8 t432 = GM_LD(gm_b16_8, ((((uint8_t *)&t421) + 8) + 8));
            gm_l51 = t432;
            void * t433 = gm_l0;
            gm_b16_8 t434 = gm_l51;
            uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&t434) + 8);
            void * t436 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t434)));
            uint64_t t437 = (uintptr_t)((uint64_t)0ULL);
            void * t438 = gm_l2;
            uint64_t t439 = GM_LD(uint64_t, (((uint8_t *)t438) + 104));
            uint64_t t440 = gm_f_us_cuv01fp(t439);
            uint64_t t441 = (uintptr_t)t440;
            if (t437 > t441 || t441 > (uintptr_t)t435) gm_trap_slice(t437, t441, t435, gm_src0jbci4ff2esa, 105, 46);
            gm_b16_8 t442;
            GM_ST(void *, (uint8_t *)&t442, (void *)(((uint8_t *)t436 + t437 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t442 + 8, (uintptr_t)(t441 - t437));
            gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&t442));
            uint64_t t444 = gm_f_rd_word_8flcq6o(t433, t443);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t444);
            goto gm_fdone;
            goto me423;
mn430:;
ma425:;
            uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)&t421) + 8) + 0));
            if (t446 != ((uint8_t)1ULL)) goto mn445;
            uint64_t t447 = GM_LD(uint64_t, ((((uint8_t *)&t421) + 8) + 8));
            gm_l52 = t447;
            void * t448 = gm_l0;
            void * t449 = gm_l2;
            gm_tl874t2atnlh3 t450 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t449) + 112));
            uint64_t t451 = gm_l52;
            void * t452 = gm_l0;
            void * t453 = gm_l2;
            gm_t88rcucg1b7uk t454 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t453) + 96));
            uint64_t t455 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t452, t454);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t456 = gm_l2;
            uint64_t t457 = GM_LD(uint64_t, (((uint8_t *)t456) + 104));
            gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t448, t450, t451, t455, t457);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me423;
mn445:;
ma426:;
            uint8_t t459 = GM_LD(uint8_t, ((((uint8_t *)&t421) + 8) + 0));
            if (t459 != ((uint8_t)2ULL)) goto mn458;
            uint64_t t460 = GM_LD(uint64_t, ((((uint8_t *)&t421) + 8) + 8));
            gm_l53 = t460;
            void * t461 = gm_l0;
            void * t462 = gm_l2;
            gm_tl874t2atnlh3 t463 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t462) + 112));
            uint64_t t464 = gm_l53;
            void * t465 = gm_l2;
            uint64_t t466 = GM_LD(uint64_t, (((uint8_t *)t465) + 104));
            gm_f_load_word_ud3iuha((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t461, t463, t464, ((uint64_t)1ULL), t466);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me423;
mn458:;
ma427:;
            uint8_t t468 = GM_LD(uint8_t, ((((uint8_t *)&t421) + 8) + 0));
            if (t468 != ((uint8_t)3ULL)) goto mn467;
            void * t469 = gm_l0;
            gm_tbl8il92l97ut t470 = gm_l1;
            void * t471 = gm_l2;
            gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t469, t470, t471);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me423;
mn467:;
ma428:;
            gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 104, 13);
me423:;
        }
        goto me3;
mn401:;
ma20:;
        void * t473 = gm_l0;
        gm_tbl8il92l97ut t474 = gm_l1;
        void * t475 = gm_l2;
        gm_f_ew_slow_0jkgi8d((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t473, t474, t475);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn472:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 37, 5);
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
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_ew_slow_0jkgi8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2) {
    struct gm_fr_ew_slow_0jkgi8d F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* n */
    gm_t2doi72kmmlil gm_l3; /* b */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_t2doi72kmmlil t2;
        GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 1), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 2), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 3), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 4), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 5), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 6), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t2 + 7), ((uint8_t)0ULL));
        gm_l3 = t2;
    }
    {
        void * t3 = ((uint8_t *)&gm_l3);
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 104));
        uint64_t t7 = gm_f_us_cuv01fp(t6);
        uint64_t t8 = (uintptr_t)t7;
        if (t4 > t8 || t8 > (uintptr_t)8) gm_trap_slice(t4, t8, 8, gm_src0jbci4ff2esa, 119, 10);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t3 + t4 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t4));
        gm_l4 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tbl8il92l97ut t11 = gm_l1;
        void * t12 = gm_l2;
        gm_b16_8 t13 = gm_l4;
        gm_tcfa6l7reoosn t14;
        gm_f_ev_25jfdar((void *)&t14, (void *)NULL, cx, NULL, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t14))[0] != 0)) {
            gm_t833u93mr4iob t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t15 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t14) + 8)));
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
    }
    {
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l4;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        uint64_t t20 = gm_f_rd_word_8flcq6o(t17, t19);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t20);
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
__attribute__((aligned(4))) void gm_f_get_builtin_s6t151o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_get_builtin_s6t151o F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t88rcucg1b7uk gm_l6; /* inner */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    gm_t88rcucg1b7uk gm_l8; /* pointee */
    uint64_t gm_l9; /* esz */
    void * gm_l10; /* base */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    uint64_t gm_l12; /* ptr */
    uint64_t gm_l13; /* len */
    gm_b16_8 gm_l14; /* owned */
    uint8_t gm_l15; /* host */
    gm_b16_8 gm_l16; /* b */
    gm_t2lqu8jsj511d gm_l17; /* sp */
    uint64_t gm_l18; /* k */
    uint64_t gm_l19; /* addr */
    uint64_t gm_l20; /* addr */
    gm_b16_8 gm_l21; /* h */
    gm_t88rcucg1b7uk gm_l22; /* pointee */
    uint64_t gm_l23; /* k */
    uint64_t gm_l24; /* i */
    gm_t2doi72kmmlil gm_l25; /* pb */
    uint64_t gm_l26; /* off */
    gm_b16_8 gm_l27; /* v */
    GM_STACK_CHECK(&F, 2528u);
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
        {
            void * t4 = gm_l0;
            void * t5 = gm_l4;
            gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 96));
            gm_t53at80pfdus3 t7;
            gm_f_kind_htv6093((void *)&t7, t4, GM_CONV(gm_csguankko4lmhd, t6));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
            if (t8 != ((uint8_t)13ULL)) goto ie3;
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
            gm_l6 = t9;
            {
                gm_t88rcucg1b7uk t11 = gm_l6;
                gm_l7 = t11;
            }
            {
                uint8_t t13 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                if (!t13) goto ie12;
                void * t14 = gm_l0;
                gm_t88rcucg1b7uk t15 = gm_l6;
                gm_t53at80pfdus3 t16;
                gm_f_kind_htv6093((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
                if (t17 != ((uint8_t)8ULL)) goto ie12;
                gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t16) + 8) + 4));
                gm_l8 = t18;
                {
                    gm_t88rcucg1b7uk t20 = gm_l8;
                    gm_l7 = t20;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                void * t21 = gm_l0;
                gm_t88rcucg1b7uk t22 = gm_l7;
                uint64_t t23 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t23;
            }
            {
                uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src0jbci4ff2esa, 620, 13);
                void * t26 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 8));
                gm_l10 = t26;
            }
            {
                void * t27 = gm_l0;
                gm_t2lqu8jsj511d t28 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t27));
                gm_l11 = t28;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                gm_b16_8 t29 = gm_f_empty_slice_ijtp9lo();
                gm_l14 = t29;
            }
            {
                gm_l15 = ((uint8_t)0);
            }
            {
                void * t30 = gm_l0;
                void * t31 = gm_l10;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 96));
                gm_t53at80pfdus3 t33;
                gm_f_kind_htv6093((void *)&t33, t30, GM_CONV(gm_csguankko4lmhd, t32));
                uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                switch (t40) { case ((uint8_t)0ULL): goto ma38; case ((uint8_t)1ULL): goto ma38; case ((uint8_t)2ULL): goto ma38; case ((uint8_t)3ULL): goto ma38; case ((uint8_t)4ULL): goto ma38; case ((uint8_t)5ULL): goto ma38; case ((uint8_t)6ULL): goto ma38; case ((uint8_t)7ULL): goto ma38; case ((uint8_t)8ULL): goto ma37; case ((uint8_t)9ULL): goto ma38; case ((uint8_t)10ULL): goto ma35; case ((uint8_t)11ULL): goto ma36; case ((uint8_t)12ULL): goto ma38; case ((uint8_t)13ULL): goto ma38; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma38; case ((uint8_t)16ULL): goto ma38; case ((uint8_t)17ULL): goto ma38; case ((uint8_t)18ULL): goto ma38; case ((uint8_t)19ULL): goto ma38; case ((uint8_t)20ULL): goto ma38; case ((uint8_t)21ULL): goto ma38; case ((uint8_t)22ULL): goto ma38; case ((uint8_t)23ULL): goto ma38; case ((uint8_t)24ULL): goto ma38; case ((uint8_t)25ULL): goto ma38; case ((uint8_t)26ULL): goto ma38; default: goto ma38; }
ma35:;
                uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                if (t42 != ((uint8_t)10ULL)) goto mn41;
                {
                    void * t44 = gm_l0;
                    void * t45 = gm_l10;
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 104));
                    gm_b16_8 t47 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t44, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t47;
                }
                {
                    void * t48 = gm_l0;
                    gm_tbl8il92l97ut t49 = gm_l1;
                    void * t50 = gm_l10;
                    gm_b16_8 t51 = gm_l16;
                    gm_tcfa6l7reoosn t52;
                    gm_f_ev_25jfdar((void *)&t52, (void *)NULL, cx, NULL, t48, t49, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t52))[0] != 0)) {
                        gm_tcfa6l7reoosn t53;
                        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                        ((uint8_t *)&t53)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t53 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t52) + 8)));
                        F.ret = t53;
                        goto gm_fret;
                    }
                    gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 8));
                }
                {
                    void * t55 = gm_l0;
                    gm_b16_8 t56 = gm_l16;
                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                    gm_t2lqu8jsj511d t58 = gm_f_slice_parts_1se5hu4(t55, t57);
                    gm_l17 = t58;
                }
                {
                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 0));
                    gm_l12 = t59;
                }
                {
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    gm_l13 = t60;
                }
                goto me34;
mn41:;
ma36:;
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                if (t62 != ((uint8_t)11ULL)) goto mn61;
                uint64_t t63 = GM_LD(uint64_t, ((((uint8_t *)&t33) + 8) + 0));
                gm_l18 = t63;
                {
                    uint64_t t65 = gm_l18;
                    gm_l13 = t65;
                }
                {
                    uint8_t t67 = 0;
                    uint8_t t68 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                    if (!t68) {
                        void * t69 = gm_l10;
                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 124));
                        t67 = t70;
                    } else t67 = 1;
                    if (!t67) goto ie66;
                    {
                        void * t72 = gm_l0;
                        gm_tbl8il92l97ut t73 = gm_l1;
                        void * t74 = gm_l10;
                        uint8_t t75 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                        gm_tl1r7a2fn1ev6 t76;
                        gm_f_place_47rfqe5((void *)&t76, (void *)NULL, cx, NULL, t72, t73, t74, t75);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t76))[0] != 0)) {
                            gm_tcfa6l7reoosn t77;
                            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                            ((uint8_t *)&t77)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t77 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t76) + 8)));
                            F.ret = t77;
                            goto gm_fret;
                        }
                        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
                        switch (t83) { case ((uint8_t)0ULL): goto ma80; case ((uint8_t)1ULL): goto ma79; case ((uint8_t)2ULL): goto ma79; case ((uint8_t)3ULL): goto ma81; default: goto ma82; }
ma79:;
                        uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
                        if (t87 != ((uint8_t)1ULL)) goto pa86;
                        uint64_t t88 = GM_LD(uint64_t, ((((uint8_t *)&t76) + 8) + 8));
                        gm_l19 = t88;
                        goto po85;
pa86:;
                        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
                        if (t90 != ((uint8_t)2ULL)) goto pa89;
                        uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)&t76) + 8) + 8));
                        gm_l19 = t91;
                        goto po85;
pa89:;
                        goto mn84;
po85:;
                        {
                            uint64_t t93 = gm_l19;
                            gm_l12 = t93;
                        }
                        goto me78;
mn84:;
ma80:;
                        uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
                        if (t95 != ((uint8_t)0ULL)) goto mn94;
                        gm_b16_8 t96 = GM_LD(gm_b16_8, ((((uint8_t *)&t76) + 8) + 8));
                        gm_l21 = t96;
                        {
                            gm_b16_8 t98 = gm_l21;
                            gm_l14 = t98;
                        }
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        goto me78;
mn94:;
ma81:;
                        uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)&t76) + 8) + 0));
                        if (t100 != ((uint8_t)3ULL)) goto mn99;
                        void * t101 = gm_l0;
                        void * t102 = gm_l4;
                        gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t102) + 112));
                        gm_b16_8 t104;
                        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sl4daef1gs04o));
                        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(36));
                        gm_t76123gfet73g t105;
                        gm_f_trap_qh84l9d((void *)&t105, (void *)NULL, cx, NULL, t101, t103, t104);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t105);
                        goto gm_fdone;
                        goto me78;
mn99:;
ma82:;
                        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 637, 17);
me78:;
                    }
                    goto ix66;
ie66:;
                    {
                        void * t107 = gm_l0;
                        void * t108 = gm_l10;
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 104));
                        gm_b16_8 t110 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t107, t109);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l14 = t110;
                    }
                    {
                        void * t111 = gm_l0;
                        gm_tbl8il92l97ut t112 = gm_l1;
                        void * t113 = gm_l10;
                        gm_b16_8 t114 = gm_l14;
                        gm_tcfa6l7reoosn t115;
                        gm_f_ev_25jfdar((void *)&t115, (void *)NULL, cx, NULL, t111, t112, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t115))[0] != 0)) {
                            gm_tcfa6l7reoosn t116;
                            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                            ((uint8_t *)&t116)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t116 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t115) + 8)));
                            F.ret = t116;
                            goto gm_fret;
                        }
                        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 8));
                    }
                    {
                        gm_l15 = ((uint8_t)1);
                    }
ix66:;
                }
                goto me34;
mn61:;
ma37:;
                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                if (t119 != ((uint8_t)8ULL)) goto mn118;
                gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t33) + 8) + 4));
                gm_l22 = t120;
                {
                    void * t123 = gm_l0;
                    gm_t88rcucg1b7uk t124 = gm_l22;
                    gm_t53at80pfdus3 t125;
                    gm_f_kind_htv6093((void *)&t125, t123, GM_CONV(gm_csguankko4lmhd, t124));
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&t125) + 0));
                    if (t126 != ((uint8_t)11ULL)) goto ie122;
                    uint64_t t127 = GM_LD(uint64_t, ((((uint8_t *)&t125) + 8) + 0));
                    gm_l23 = t127;
                    {
                        void * t129 = gm_l0;
                        gm_tbl8il92l97ut t130 = gm_l1;
                        void * t131 = gm_l10;
                        gm_t833u93mr4iob t132;
                        gm_f_ew_c5kil07((void *)&t132, (void *)NULL, cx, NULL, t129, t130, t131);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t132))[0] != 0)) {
                            gm_tcfa6l7reoosn t133;
                            GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                            ((uint8_t *)&t133)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t133 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t132) + 8)));
                            F.ret = t133;
                            goto gm_fret;
                        }
                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&t132) + 8));
                        gm_l12 = t134;
                    }
                    {
                        uint64_t t135 = gm_l23;
                        gm_l13 = t135;
                    }
                    goto ix122;
ie122:;
                    {
                        void * t137 = gm_l0;
                        void * t138 = gm_l4;
                        gm_tl874t2atnlh3 t139 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t138) + 112));
                        gm_b16_8 t140;
                        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sob69bvkm3aek));
                        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(32));
                        gm_t76123gfet73g t141;
                        gm_f_trap_qh84l9d((void *)&t141, (void *)NULL, cx, NULL, t137, t139, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t141);
                        goto gm_fdone;
                    }
ix122:;
                }
                goto me34;
mn118:;
ma38:;
                void * t143 = gm_l0;
                void * t144 = gm_l4;
                gm_tl874t2atnlh3 t145 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t144) + 112));
                gm_b16_8 t146;
                GM_ST(void *, (uint8_t *)&t146, (void *)(gm_sqnuqut1dl3fi));
                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(47));
                gm_t76123gfet73g t147;
                gm_f_trap_qh84l9d((void *)&t147, (void *)NULL, cx, NULL, t143, t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t147);
                goto gm_fdone;
                goto me34;
mn142:;
ma39:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 626, 5);
me34:;
            }
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t153) { case ((uint8_t)0ULL): goto ma151; case ((uint8_t)1ULL): goto ma151; case ((uint8_t)2ULL): goto ma151; case ((uint8_t)3ULL): goto ma151; case ((uint8_t)4ULL): goto ma151; case ((uint8_t)5ULL): goto ma151; case ((uint8_t)6ULL): goto ma151; case ((uint8_t)7ULL): goto ma151; case ((uint8_t)8ULL): goto ma151; case ((uint8_t)9ULL): goto ma151; case ((uint8_t)10ULL): goto ma151; case ((uint8_t)11ULL): goto ma151; case ((uint8_t)12ULL): goto ma151; case ((uint8_t)13ULL): goto ma151; case ((uint8_t)14ULL): goto ma151; case ((uint8_t)15ULL): goto ma151; case ((uint8_t)16ULL): goto ma151; case ((uint8_t)17ULL): goto ma151; case ((uint8_t)18ULL): goto ma151; case ((uint8_t)19ULL): goto ma151; case ((uint8_t)20ULL): goto ma151; case ((uint8_t)21ULL): goto ma151; case ((uint8_t)22ULL): goto ma151; case ((uint8_t)23ULL): goto ma151; case ((uint8_t)24ULL): goto ma151; case ((uint8_t)25ULL): goto ma151; case ((uint8_t)26ULL): goto ma151; case ((uint8_t)27ULL): goto ma151; case ((uint8_t)28ULL): goto ma151; case ((uint8_t)29ULL): goto ma151; case ((uint8_t)30ULL): goto ma151; case ((uint8_t)31ULL): goto ma151; case ((uint8_t)32ULL): goto ma151; case ((uint8_t)33ULL): goto ma151; case ((uint8_t)34ULL): goto ma151; case ((uint8_t)35ULL): goto ma151; case ((uint8_t)36ULL): goto ma151; case ((uint8_t)37ULL): goto ma151; case ((uint8_t)38ULL): goto ma149; case ((uint8_t)39ULL): goto ma150; case ((uint8_t)40ULL): goto ma151; case ((uint8_t)41ULL): goto ma151; case ((uint8_t)42ULL): goto ma151; case ((uint8_t)43ULL): goto ma151; case ((uint8_t)44ULL): goto ma151; case ((uint8_t)45ULL): goto ma151; case ((uint8_t)46ULL): goto ma151; case ((uint8_t)47ULL): goto ma151; case ((uint8_t)48ULL): goto ma151; case ((uint8_t)49ULL): goto ma151; case ((uint8_t)50ULL): goto ma151; case ((uint8_t)51ULL): goto ma151; case ((uint8_t)52ULL): goto ma151; case ((uint8_t)53ULL): goto ma151; case ((uint8_t)54ULL): goto ma151; case ((uint8_t)55ULL): goto ma151; case ((uint8_t)56ULL): goto ma151; case ((uint8_t)57ULL): goto ma151; case ((uint8_t)58ULL): goto ma151; case ((uint8_t)59ULL): goto ma151; case ((uint8_t)60ULL): goto ma151; case ((uint8_t)61ULL): goto ma151; case ((uint8_t)62ULL): goto ma151; case ((uint8_t)63ULL): goto ma151; case ((uint8_t)64ULL): goto ma151; case ((uint8_t)65ULL): goto ma151; case ((uint8_t)66ULL): goto ma151; case ((uint8_t)67ULL): goto ma151; case ((uint8_t)68ULL): goto ma151; case ((uint8_t)69ULL): goto ma151; case ((uint8_t)70ULL): goto ma151; case ((uint8_t)71ULL): goto ma151; case ((uint8_t)72ULL): goto ma151; case ((uint8_t)73ULL): goto ma151; case ((uint8_t)74ULL): goto ma151; case ((uint8_t)75ULL): goto ma151; case ((uint8_t)76ULL): goto ma151; case ((uint8_t)77ULL): goto ma151; case ((uint8_t)78ULL): goto ma151; case ((uint8_t)79ULL): goto ma151; case ((uint8_t)80ULL): goto ma151; case ((uint8_t)81ULL): goto ma151; default: goto ma151; }
ma149:;
                uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t155 != ((uint8_t)38ULL)) goto mn154;
                goto me148;
mn154:;
ma150:;
                uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t158 != ((uint8_t)39ULL)) goto mn157;
                {
                    uint64_t t160 = gm_l13;
                    uint64_t r161; bool o162 = __builtin_sub_overflow(t160, ((uint64_t)1ULL), &r161);
                    uint64_t t163 = r161;
                    gm_l24 = t163;
                }
                goto me148;
mn157:;
ma151:;
                {
                    void * t166 = gm_l0;
                    gm_tbl8il92l97ut t167 = gm_l1;
                    uint64_t t168 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t168 >= (uintptr_t)(t169)) gm_trap_index(t168, t169, gm_src0jbci4ff2esa, 662, 38);
                    void * t170 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t168 * 8));
                    gm_t833u93mr4iob t171;
                    gm_f_ev_index_bie377m((void *)&t171, (void *)NULL, cx, NULL, t166, t167, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t171))[0] != 0)) {
                        gm_tcfa6l7reoosn t172;
                        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
                        ((uint8_t *)&t172)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t172 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t171) + 8)));
                        F.ret = t172;
                        goto gm_fret;
                    }
                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&t171) + 8));
                    gm_l24 = t173;
                }
                goto me148;
mn164:;
ma152:;
                gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 659, 5);
me148:;
            }
            {
                uint64_t t175 = gm_l24;
                uint64_t t176 = gm_l13;
                uint8_t t177 = (t175 >= t176);
                if (!t177) goto ie174;
                {
                    gm_b16_8 t179 = gm_l5;
                    gm_f_fill_abmf5g5(t179, ((uint8_t)0ULL));
                }
                {
                    void * t180 = gm_l0;
                    gm_t2lqu8jsj511d t181 = gm_l11;
                    gm_f_release_jn5kqdi(t180, GM_CONV(gm_csc7lc6jlkqkuq, t181));
                }
                {
                    gm_b0_1 t182;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t182);
                    goto gm_fdone;
                }
                goto ix174;
ie174:;
ix174:;
            }
            {
                uint8_t t184 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)37ULL)));
                if (!t184) goto ie183;
                {
                    gm_t2doi72kmmlil t186;
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 1), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 2), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 3), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 4), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 5), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 6), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t186 + 7), ((uint8_t)0ULL));
                    gm_l25 = t186;
                }
                {
                    void * t187 = gm_l0;
                    void * t188 = ((uint8_t *)&gm_l25);
                    if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_src0jbci4ff2esa, 671, 19);
                    gm_b16_8 t189;
                    GM_ST(void *, (uint8_t *)&t189, (void *)(((uint8_t *)t188 + (uintptr_t)0 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                    uint64_t t190 = gm_l12;
                    uint64_t t191 = gm_l24;
                    uint64_t t192 = gm_l9;
                    uint64_t r193; bool o194 = __builtin_mul_overflow(t191, t192, &r193);
                    if (o194) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 671, 34);
                    uint64_t t195 = r193;
                    uint64_t r196; bool o197 = __builtin_add_overflow(t190, t195, &r196);
                    uint64_t t198 = r196;
                    gm_f_put_ptr_188p3mm(t187, t189, t198);
                }
                {
                    void * t199 = gm_l0;
                    void * t200 = gm_l4;
                    gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t200) + 96));
                    gm_b16_8 t202 = gm_l5;
                    void * t203 = ((uint8_t *)&gm_l25);
                    uint64_t t204 = (uintptr_t)((uint64_t)0ULL);
                    void * t205 = gm_l0;
                    uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)t205) + 176));
                    uint64_t t207 = gm_f_us_cuv01fp(t206);
                    uint64_t t208 = (uintptr_t)t207;
                    if (t204 > t208 || t208 > (uintptr_t)8) gm_trap_slice(t204, t208, 8, gm_src0jbci4ff2esa, 672, 32);
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(((uint8_t *)t203 + t204 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(t208 - t204));
                    gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
                    gm_b0_1 t211 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t199, t201, t202, t210);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix183;
ie183:;
                uint8_t t213 = gm_l15;
                if (!t213) goto ie212;
                {
                    uint64_t t215 = gm_l24;
                    uint64_t t216 = gm_l9;
                    uint64_t r217; bool o218 = __builtin_mul_overflow(t215, t216, &r217);
                    if (o218) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 674, 19);
                    uint64_t t219 = r217;
                    uint64_t t220 = gm_f_us_cuv01fp(t219);
                    gm_l26 = t220;
                }
                {
                    void * t221 = gm_l0;
                    void * t222 = gm_l4;
                    gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t222) + 96));
                    gm_b16_8 t224 = gm_l5;
                    gm_b16_8 t225 = gm_l14;
                    uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&t225) + 8);
                    void * t227 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t225)));
                    uint64_t t228 = gm_l26;
                    uint64_t t229 = (uintptr_t)t228;
                    uint64_t t230 = gm_l26;
                    uint64_t t231 = gm_l9;
                    uint64_t t232 = gm_f_us_cuv01fp(t231);
                    uint64_t r233; bool o234 = __builtin_add_overflow(t230, t232, &r233);
                    if (o234) gm_trap("integer overflow in `+`", gm_src0jbci4ff2esa, 675, 43);
                    uint64_t t235 = r233;
                    uint64_t t236 = (uintptr_t)t235;
                    if (t229 > t236 || t236 > (uintptr_t)t226) gm_trap_slice(t229, t236, t226, gm_src0jbci4ff2esa, 675, 32);
                    gm_b16_8 t237;
                    GM_ST(void *, (uint8_t *)&t237, (void *)(((uint8_t *)t227 + t229 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(t236 - t229));
                    gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                    gm_b0_1 t239 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t221, t223, t224, t238);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix212;
ie212:;
                {
                    void * t241 = gm_l0;
                    uint64_t t242 = gm_l9;
                    gm_b16_8 t243 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t241, t242);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t243;
                }
                {
                    void * t244 = gm_l0;
                    void * t245 = gm_l4;
                    gm_tl874t2atnlh3 t246 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t245) + 112));
                    uint64_t t247 = gm_l12;
                    uint64_t t248 = gm_l24;
                    uint64_t t249 = gm_l9;
                    uint64_t r250; bool o251 = __builtin_mul_overflow(t248, t249, &r250);
                    if (o251) gm_trap("integer overflow in `*`", gm_src0jbci4ff2esa, 678, 38);
                    uint64_t t252 = r250;
                    uint64_t r253; bool o254 = __builtin_add_overflow(t247, t252, &r253);
                    uint64_t t255 = r253;
                    void * t256 = gm_l0;
                    gm_t88rcucg1b7uk t257 = gm_l7;
                    uint64_t t258 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t256, t257);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t259 = gm_l27;
                    gm_tcfa6l7reoosn t260;
                    gm_f_load_at_6u81jcc((void *)&t260, (void *)NULL, cx, NULL, t244, t246, t255, t258, t259);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t260))[0] != 0)) {
                        gm_tcfa6l7reoosn t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        ((uint8_t *)&t261)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t261 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t260) + 8)));
                        F.ret = t261;
                        goto gm_fret;
                    }
                    gm_b0_1 t262 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 8));
                }
                {
                    void * t263 = gm_l0;
                    void * t264 = gm_l4;
                    gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t264) + 96));
                    gm_b16_8 t266 = gm_l5;
                    gm_b16_8 t267 = gm_l27;
                    gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                    gm_b0_1 t269 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t263, t265, t266, t268);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix212:;
ix183:;
            }
            {
                void * t270 = gm_l0;
                gm_t2lqu8jsj511d t271 = gm_l11;
                gm_f_release_jn5kqdi(t270, GM_CONV(gm_csc7lc6jlkqkuq, t271));
            }
            goto ix3;
ie3:;
            {
                void * t273 = gm_l0;
                void * t274 = gm_l4;
                gm_tl874t2atnlh3 t275 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t274) + 112));
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(gm_ssgce158bfb9m));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(54));
                gm_t76123gfet73g t277;
                gm_f_trap_qh84l9d((void *)&t277, (void *)NULL, cx, NULL, t273, t275, t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t277);
                goto gm_fdone;
            }
ix3:;
        }
    }
    gm_b0_1 t278;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t278);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_place_expr_qlfi6ot(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_place_expr_qlfi6ot F;
    void * gm_l0; /* m */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma4; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)15ULL)) goto pa12;
    goto po11;
pa12:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)16ULL)) goto pa14;
    goto po11;
pa14:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)17ULL)) goto pa16;
    goto po11;
pa16:;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t19 != ((uint8_t)18ULL)) goto pa18;
    goto po11;
pa18:;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t21 != ((uint8_t)14ULL)) goto pa20;
    goto po11;
pa20:;
    goto mn10;
po11:;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)19ULL)) goto mn22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l2 = t24;
    void * t25 = gm_l0;
    void * t26 = gm_l2;
    uint8_t t27 = gm_f_is_place_expr_qlfi6ot(t25, t26);
    t2 = t27;
    goto me3;
mn22:;
ma6:;
    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t29 != ((uint8_t)20ULL)) goto mn28;
    void * t30 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l3 = t30;
    uint8_t t31;
    void * t33 = gm_l0;
    void * t34 = gm_l3;
    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
    gm_t53at80pfdus3 t36;
    gm_f_kind_htv6093((void *)&t36, t33, GM_CONV(gm_csguankko4lmhd, t35));
    uint8_t t37;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
    if (t40 != ((uint8_t)11ULL)) goto mn39;
    t37 = ((uint8_t)1);
    goto me38;
mn39:;
    t37 = ((uint8_t)0);
    goto me38;
mn41:;
    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 406, 30);
me38:;
    if (!t37) goto ie32;
    void * t43 = gm_l0;
    void * t44 = gm_l3;
    uint8_t t45 = gm_f_is_place_expr_qlfi6ot(t43, t44);
    uint8_t t46 = t45;
    t31 = t46;
    goto ix32;
ie32:;
    uint8_t t48 = ((uint8_t)1);
    t31 = t48;
ix32:;
    t2 = t31;
    goto me3;
mn28:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn49:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 403, 67);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_read_string_ttuk4gr F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* b */
    gm_t2lqu8jsj511d gm_l3; /* sp */
    gm_b16_8 gm_l5; /* data */
    gm_b16_8 gm_l6; /* out */
    GM_STACK_CHECK(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_t2lqu8jsj511d t4 = gm_f_slice_parts_1se5hu4(t2, t3);
        gm_l3 = t4;
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        if (!t7) goto ie5;
        {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 184);
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        gm_t934k1j7u70ct t14;
        gm_f_read_u6r4vns((void *)&t14, t11, t12, t13, ((uint64_t)1ULL));
        gm_b16_8 t15;
        if (!((((uint8_t *)&t14))[0] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
            t15 = t16;
        } else {
            F.l4 = (*(gm_tpekhfs32op0f *)(((uint8_t *)&t14) + 8));
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l1;
            gm_t76123gfet73g t19;
            gm_f_mem_trap_ecogko0((void *)&t19, (void *)NULL, cx, NULL, t17, t18, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t19);
            goto gm_fdone;
        }
        gm_l5 = t15;
    }
    {
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        gm_tabb710d9mq2e t21;
        gm_f_make_2ip78bj((void *)&t21, (void *)NULL, cx, NULL, t20, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knna6g3th5t)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_src0jbci4ff2esa, 689, 12);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_l6 = t22;
    }
    {
        gm_b16_8 t23 = gm_l6;
        gm_b16_8 t24 = gm_l5;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_f_copy_from_f7j6p05(t23, t25);
    }
    {
        gm_b16_8 t26 = gm_l6;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8, t27);
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
__attribute__((aligned(4))) void gm_f_try__q6sv04f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_try__q6sv04f F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* inner */
    gm_t9805qtonbsag gm_l3; /* lift */
    uint32_t gm_l4; /* levels */
    void * gm_l5; /* n */
    gm_b16_8 gm_l6; /* dst */
    gm_t2lqu8jsj511d gm_l7; /* mk */
    gm_b16_8 gm_l8; /* b */
    void * gm_l9; /* target */
    gm_b16_8 gm_l10; /* ev */
    gm_t88rcucg1b7uk gm_l11; /* ret_ty */
    gm_t88rcucg1b7uk gm_l12; /* rerr */
    uint32_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* lifted */
    GM_STACK_CHECK(&F, 1394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        gm_t2lqu8jsj511d t3 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t2));
        gm_l7 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 104));
        gm_b16_8 t7 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_tbl8il92l97ut t9 = gm_l1;
        void * t10 = gm_l2;
        gm_b16_8 t11 = gm_l8;
        gm_tcfa6l7reoosn t12;
        gm_f_ev_25jfdar((void *)&t12, (void *)NULL, cx, NULL, t8, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_tcfa6l7reoosn t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t13 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t12) + 8)));
            F.ret = t13;
            goto gm_fret;
        }
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 8));
    }
    {
        void * t15 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        uint32_t t16 = gm_l4;
        void * t17 = gm_f_frame_up_ghkkuqg(t15, t16);
        void * t18;
        if ((GM_LD(void *, ((uint8_t *)&t17)) != 0)) {
            void * t19 = GM_LD(void *, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            void * t20 = gm_l0;
            void * t21 = gm_l5;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 112));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sl9b2am1i9pmg));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(51));
            gm_t76123gfet73g t24;
            gm_f_trap_qh84l9d((void *)&t24, (void *)NULL, cx, NULL, t20, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t24);
            goto gm_fdone;
        }
        gm_l9 = t18;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l2;
        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 96));
        gm_t53at80pfdus3 t28;
        gm_f_kind_htv6093((void *)&t28, t25, GM_CONV(gm_csguankko4lmhd, t27));
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma32; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma32; case ((uint8_t)3ULL): goto ma32; case ((uint8_t)4ULL): goto ma32; case ((uint8_t)5ULL): goto ma32; case ((uint8_t)6ULL): goto ma32; case ((uint8_t)7ULL): goto ma32; case ((uint8_t)8ULL): goto ma32; case ((uint8_t)9ULL): goto ma32; case ((uint8_t)10ULL): goto ma32; case ((uint8_t)11ULL): goto ma32; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma32; case ((uint8_t)16ULL): goto ma32; case ((uint8_t)17ULL): goto ma32; case ((uint8_t)18ULL): goto ma32; case ((uint8_t)19ULL): goto ma32; case ((uint8_t)20ULL): goto ma32; case ((uint8_t)21ULL): goto ma32; case ((uint8_t)22ULL): goto ma32; case ((uint8_t)23ULL): goto ma32; case ((uint8_t)24ULL): goto ma32; case ((uint8_t)25ULL): goto ma32; case ((uint8_t)26ULL): goto ma32; default: goto ma32; }
ma30:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t36 != ((uint8_t)13ULL)) goto mn35;
        {
            void * t39 = gm_l0;
            void * t40 = gm_l2;
            gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + 96));
            gm_b16_8 t42 = gm_l8;
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
            uint8_t t44 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t39, t41, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t44) goto ie38;
            {
                gm_b16_8 t46 = gm_l6;
                void * t47 = gm_l0;
                void * t48 = gm_l2;
                gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t48) + 96));
                gm_b16_8 t50 = gm_l8;
                gm_b16_8 t51 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t47, t49, t50);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                gm_f_copy_from_f7j6p05(t46, t52);
            }
            {
                void * t53 = gm_l0;
                gm_t2lqu8jsj511d t54 = gm_l7;
                gm_f_release_jn5kqdi(t53, GM_CONV(gm_csc7lc6jlkqkuq, t54));
            }
            {
                gm_b0_1 t55;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t55);
                goto gm_fdone;
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            void * t56 = gm_l9;
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 24));
            gm_f_fill_abmf5g5(t57, ((uint8_t)0ULL));
        }
        {
            void * t58 = gm_l9;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 0));
            gm_tf5srh4mp4b3i t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t60 + 0), t59);
            GM_ST(uint8_t, ((uint8_t *)&t60 + 8), ((uint8_t)0));
            gm_t76123gfet73g t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t61 + 8), t60);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t61);
            goto gm_fdone;
        }
        goto me29;
mn35:;
ma31:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
        if (t63 != ((uint8_t)14ULL)) goto mn62;
        {
            void * t66 = gm_l0;
            void * t67 = gm_l2;
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t67) + 96));
            gm_b16_8 t69 = gm_l8;
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            uint8_t t71 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t66, t68, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = (uint8_t)!t71;
            if (!t72) goto ie65;
            {
                gm_b16_8 t74 = gm_l6;
                void * t75 = gm_l0;
                void * t76 = gm_l2;
                gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t76) + 96));
                gm_b16_8 t78 = gm_l8;
                gm_b16_8 t79 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t75, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                gm_f_copy_from_f7j6p05(t74, t80);
            }
            {
                void * t81 = gm_l0;
                gm_t2lqu8jsj511d t82 = gm_l7;
                gm_f_release_jn5kqdi(t81, GM_CONV(gm_csc7lc6jlkqkuq, t82));
            }
            {
                gm_b0_1 t83;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t83);
                goto gm_fdone;
            }
            goto ix65;
ie65:;
ix65:;
        }
        {
            void * t84 = gm_l0;
            void * t85 = gm_l2;
            gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + 96));
            gm_b16_8 t87 = gm_l8;
            gm_b16_8 t88 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t84, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t88;
        }
        {
            void * t89 = gm_l0;
            void * t90 = gm_l9;
            uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)t90) + 12));
            void * t92 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t89, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 64));
            gm_l11 = t93;
        }
        {
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l11;
            gm_tgglga8ro3isc t96 = gm_f_res_types_pg7rvck(t94, GM_CONV(gm_csguankko4lmhd, t95));
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t96) + 0));
            gm_l12 = t97;
        }
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie98;
            uint32_t t99 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            gm_l13 = t99;
            {
                void * t101 = gm_l0;
                void * t102 = gm_l0;
                gm_t88rcucg1b7uk t103 = gm_l12;
                uint64_t t104 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t102, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t105 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t101, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t105;
            }
            {
                void * t106 = gm_l0;
                gm_t88rcucg1b7uk t107 = gm_l12;
                gm_b16_8 t108 = gm_l14;
                uint32_t t109 = gm_l13;
                gm_b16_8 t110 = gm_l10;
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_b0_1 t112 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t106, t107, t108, t109, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t113 = gm_l0;
                gm_t88rcucg1b7uk t114 = gm_l11;
                void * t115 = gm_l9;
                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)t115) + 24));
                gm_b16_8 t117 = gm_l14;
                gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                gm_b0_1 t119 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t113, t114, t116, ((uint8_t)1), t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix98;
ie98:;
            {
                void * t121 = gm_l0;
                gm_t88rcucg1b7uk t122 = gm_l11;
                void * t123 = gm_l9;
                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 24));
                gm_b16_8 t125 = gm_l10;
                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                gm_b0_1 t127 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t121, t122, t124, ((uint8_t)1), t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix98:;
        }
        {
            void * t128 = gm_l9;
            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 0));
            gm_tf5srh4mp4b3i t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t130 + 0), t129);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 8), ((uint8_t)1));
            gm_t76123gfet73g t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t131 + 8), t130);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t131);
            goto gm_fdone;
        }
        goto me29;
mn62:;
ma32:;
        void * t133 = gm_l0;
        void * t134 = gm_l5;
        gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 112));
        gm_b16_8 t136;
        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s0lfbgij46fm3));
        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(73));
        gm_t76123gfet73g t137;
        gm_f_trap_qh84l9d((void *)&t137, (void *)NULL, cx, NULL, t133, t135, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t137);
        goto gm_fdone;
        goto me29;
mn132:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_src0jbci4ff2esa, 489, 5);
me29:;
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
__attribute__((aligned(4))) void gm_f_unwrap_f7c53ka(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_unwrap_f7c53ka F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* x */
    uint8_t gm_l3; /* is_result */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_t2lqu8jsj511d gm_l6; /* mk */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 995u);
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
        {
            void * t3 = gm_l0;
            gm_t2lqu8jsj511d t4 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t3));
            gm_l6 = t4;
        }
        {
            void * t5 = gm_l0;
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 104));
            gm_b16_8 t8 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t5, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t8;
        }
        {
            void * t9 = gm_l0;
            gm_tbl8il92l97ut t10 = gm_l1;
            void * t11 = gm_l2;
            gm_b16_8 t12 = gm_l7;
            gm_tcfa6l7reoosn t13;
            gm_f_ev_25jfdar((void *)&t13, (void *)NULL, cx, NULL, t9, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_tcfa6l7reoosn t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t13) + 8)));
                F.ret = t14;
                goto gm_fret;
            }
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
        }
        {
            uint8_t t17 = gm_l3;
            if (!t17) goto ie16;
            {
                void * t20 = gm_l0;
                void * t21 = gm_l2;
                gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 96));
                gm_b16_8 t23 = gm_l7;
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                uint8_t t25 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t20, t22, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t25) goto ie19;
                {
                    void * t28 = gm_l0;
                    uint8_t t29 = gm_f_fast_23em7sv(t28);
                    if (!t29) goto ie27;
                    {
                        gm_b16_8 t31 = gm_l5;
                        gm_f_fill_abmf5g5(t31, ((uint8_t)0ULL));
                    }
                    {
                        void * t32 = gm_l0;
                        gm_t2lqu8jsj511d t33 = gm_l6;
                        gm_f_release_jn5kqdi(t32, GM_CONV(gm_csc7lc6jlkqkuq, t33));
                    }
                    {
                        gm_b0_1 t34;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t34);
                        goto gm_fdone;
                    }
                    goto ix27;
ie27:;
ix27:;
                }
                {
                    gm_f_list_uvfu1k1((void *)&F.l8, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t35 = gm_l0;
                    void * t36 = (void *)((uint8_t *)&F.l8);
                    void * t37 = gm_l0;
                    void * t38 = gm_l2;
                    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t38) + 96));
                    gm_tgglga8ro3isc t40 = gm_f_res_types_pg7rvck(t37, GM_CONV(gm_csguankko4lmhd, t39));
                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t40) + 0));
                    void * t42 = gm_l0;
                    void * t43 = gm_l2;
                    gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t43) + 96));
                    gm_b16_8 t45 = gm_l7;
                    gm_b16_8 t46 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t42, t44, t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl572d7579hns t47;
                    gm_f_format_value_vmle7qr((void *)&t47, (void *)NULL, cx, NULL, t35, t36, t41, t46, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b0_1 t48;
                    if (!((((uint8_t *)&t47))[0] != 0)) {
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
                        t48 = t49;
                    } else {
                        t48 = GM_ZERO(gm_b0_1);
                    }
                }
                {
                    void * t51 = gm_l0;
                    void * t52 = gm_l4;
                    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 112));
                    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                    gm_tnpifv20irnph t55;
                    GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                    gm_tabb710d9mq2e t56;
                    gm_f_format_r6pm0oj((void *)&t56, (void *)NULL, cx, NULL, t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_src0jbci4ff2esa, 427, 36);
                    gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                    gm_t76123gfet73g t59;
                    gm_f_trap_qh84l9d((void *)&t59, (void *)NULL, cx, NULL, t51, t53, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t59);
                    goto gm_fdone;
                }
                goto ix19;
ie19:;
ix19:;
            }
            {
                gm_b16_8 t60 = gm_l5;
                void * t61 = gm_l0;
                void * t62 = gm_l2;
                gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t62) + 96));
                gm_b16_8 t64 = gm_l7;
                gm_b16_8 t65 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t61, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_f_copy_from_f7j6p05(t60, t66);
            }
            goto ix16;
ie16:;
            {
                void * t69 = gm_l0;
                void * t70 = gm_l2;
                gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t70) + 96));
                gm_b16_8 t72 = gm_l7;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                uint8_t t74 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t69, t71, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t75 = (uint8_t)!t74;
                if (!t75) goto ie68;
                {
                    void * t78 = gm_l0;
                    uint8_t t79 = gm_f_fast_23em7sv(t78);
                    if (!t79) goto ie77;
                    {
                        gm_b16_8 t81 = gm_l5;
                        gm_f_fill_abmf5g5(t81, ((uint8_t)0ULL));
                    }
                    {
                        void * t82 = gm_l0;
                        gm_t2lqu8jsj511d t83 = gm_l6;
                        gm_f_release_jn5kqdi(t82, GM_CONV(gm_csc7lc6jlkqkuq, t83));
                    }
                    {
                        gm_b0_1 t84;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t84);
                        goto gm_fdone;
                    }
                    goto ix77;
ie77:;
ix77:;
                }
                {
                    void * t85 = gm_l0;
                    void * t86 = gm_l4;
                    gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t86) + 112));
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sd5r0412dj6fb));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(14));
                    gm_t76123gfet73g t89;
                    gm_f_trap_qh84l9d((void *)&t89, (void *)NULL, cx, NULL, t85, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t89);
                    goto gm_fdone;
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                gm_b16_8 t90 = gm_l5;
                void * t91 = gm_l0;
                void * t92 = gm_l2;
                gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 96));
                gm_b16_8 t94 = gm_l7;
                gm_b16_8 t95 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t91, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_f_copy_from_f7j6p05(t90, t96);
            }
ix16:;
        }
        {
            void * t97 = gm_l0;
            gm_t2lqu8jsj511d t98 = gm_l6;
            gm_f_release_jn5kqdi(t97, GM_CONV(gm_csc7lc6jlkqkuq, t98));
        }
    }
    gm_b0_1 t99;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t99);
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
