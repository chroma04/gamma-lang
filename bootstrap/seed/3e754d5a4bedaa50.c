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
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; int64_t v1; gm_b16_8 v2; } f1; } gm_t5e047ltlbe02;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t5e047ltlbe02 f0; uint8_t f1; uint8_t pe[7]; } gm_t1m5mugfio2m4;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e047ltlbe02) == 24 && _Alignof(gm_t5e047ltlbe02) == 8 && offsetof(gm_t5e047ltlbe02, f0) == 0 && offsetof(gm_t5e047ltlbe02, f1) == 8, "layout of `Const`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1m5mugfio2m4) == 32 && _Alignof(gm_t1m5mugfio2m4) == 8 && offsetof(gm_t1m5mugfio2m4, f0) == 0 && offsetof(gm_t1m5mugfio2m4, f1) == 24, "layout of `?Const`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
struct gm_fr_compare_hq04o5p {
    gm_frame h;
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_eval_5c9oefa {
    gm_frame h;
    gm_tv2uf4leil5hq l16; /* b */
    gm_tet9guhev5r4u l17; /* i */
    gm_tc3oht4ksth9d l19; /* arg */
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_field_abqqbar {
    gm_frame h;
    gm_t1m5mugfio2m4 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_type_size_h6snnvm {
    gm_frame h;
    gm_t913bb735pvt5 ret;
};
struct gm_fr_when_cond_9a46a5h {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqsqt22rph28(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt4fm78ifa7(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_compare_hq04o5p(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_compare_hq04o5p_c(gm_ts6oqppjh0ba3 a0, gm_t5e047ltlbe02 a1, gm_t5e047ltlbe02 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_eval_5c9oefa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_target_field_abqqbar(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_target_field_abqqbar_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_type_size_h6snnvm(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
extern const uint8_t gm_s030jgonh2s3s[105];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s3ngn7me40qa4[5];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6iroad3kr6qd[7];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_sa42h1oh66nds[9];
extern const uint8_t gm_saean4ms0g52k[8];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_sjmmigkb19htg[4];
extern const uint8_t gm_sla1l2pt1mfvt[3];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smbmfla2idsdo[68];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_soqlsukui24da[7];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src2pgaip05o19o[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcc2qutubarto1[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
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
inline bool gm_eqsqt22rph28(const uint8_t *a, const uint8_t *b) {
    return GM_LD(int64_t, a) == GM_LD(int64_t, b);
}
inline bool gm_eqt4fm78ifa7(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq929lennfd3(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eqsqt22rph28(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq0iqiubfm7e(a + 8, b + 8);
    return true;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1 = gm_l1;
    uint64_t t2 = (uintptr_t)t1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 200) + 0) + 8);
    if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srcc2qutubarto1, 547, 78);
    void * t5 = (void *)(((uint8_t *)GM_LD(void *, ((((uint8_t *)t3) + 200) + 0)) + t2 * 184) + 56);
    gm_b16_8 t6 = gm_l2;
    gm_tf5srh4mp4b3i t7 = gm_f_get_espvf96(t5, t6);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_compare_hq04o5p(void *gm_out, gm_ts6oqppjh0ba3 a0, const void * a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compare_hq04o5p F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t5e047ltlbe02 gm_l1; /* a */
    gm_t5e047ltlbe02 gm_l2; /* b */
    int64_t gm_l3; /* x */
    int64_t gm_l4; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 823u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma3; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; default: goto ma5; }
ma3:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t9 != ((uint8_t)12ULL)) goto mn8;
        gm_t5e047ltlbe02 t10 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a1));
        gm_t5e047ltlbe02 t11 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a2));
        gm_t5e047ltlbe02 t12 = t10;
        gm_t5e047ltlbe02 t13 = t11;
        uint8_t t14 = (gm_eqt4fm78ifa7(((uint8_t *)&t12), ((uint8_t *)&t13)));
        gm_t5e047ltlbe02 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
        gm_t1m5mugfio2m4 t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 28, 0);
        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t16), t15);
        ((uint8_t *)&t16)[24] = 1;
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
        goto me2;
mn8:;
ma4:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t19 != ((uint8_t)13ULL)) goto mn18;
        gm_t5e047ltlbe02 t20 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a1));
        gm_t5e047ltlbe02 t21 = GM_LD(gm_t5e047ltlbe02, ((uint8_t *)a2));
        gm_t5e047ltlbe02 t22 = t20;
        gm_t5e047ltlbe02 t23 = t21;
        uint8_t t24 = (!gm_eqt4fm78ifa7(((uint8_t *)&t22), ((uint8_t *)&t23)));
        gm_t5e047ltlbe02 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t25 + 8), t24);
        gm_t1m5mugfio2m4 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 28, 0);
        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[24] = 1;
        uint8_t t27 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me2;
mn18:;
ma5:;
        goto me2;
mn28:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 83, 5);
me2:;
    }
    {
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t31 != ((uint8_t)1ULL)) goto ie30;
        int64_t t32 = GM_LD(int64_t, (((uint8_t *)a1) + 8));
        gm_l3 = t32;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t35 != ((uint8_t)1ULL)) goto ie34;
            int64_t t36 = GM_LD(int64_t, (((uint8_t *)a2) + 8));
            gm_l4 = t36;
            {
                gm_t1m5mugfio2m4 t38;
                uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                switch (t46) { case ((uint8_t)0ULL): goto ma44; case ((uint8_t)1ULL): goto ma44; case ((uint8_t)2ULL): goto ma44; case ((uint8_t)3ULL): goto ma44; case ((uint8_t)4ULL): goto ma44; case ((uint8_t)5ULL): goto ma44; case ((uint8_t)6ULL): goto ma44; case ((uint8_t)7ULL): goto ma44; case ((uint8_t)8ULL): goto ma44; case ((uint8_t)9ULL): goto ma44; case ((uint8_t)10ULL): goto ma44; case ((uint8_t)11ULL): goto ma44; case ((uint8_t)12ULL): goto ma44; case ((uint8_t)13ULL): goto ma44; case ((uint8_t)14ULL): goto ma44; case ((uint8_t)15ULL): goto ma44; case ((uint8_t)16ULL): goto ma40; case ((uint8_t)17ULL): goto ma41; case ((uint8_t)18ULL): goto ma42; case ((uint8_t)19ULL): goto ma43; case ((uint8_t)20ULL): goto ma44; case ((uint8_t)21ULL): goto ma44; default: goto ma44; }
ma40:;
                uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t48 != ((uint8_t)16ULL)) goto mn47;
                int64_t t49 = gm_l3;
                int64_t t50 = gm_l4;
                uint8_t t51 = (t49 < t50);
                gm_t5e047ltlbe02 t52;
                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t52 + 8), t51);
                gm_t1m5mugfio2m4 t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t53 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t53), t52);
                ((uint8_t *)&t53)[24] = 1;
                t38 = t53;
                goto me39;
mn47:;
ma41:;
                uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t55 != ((uint8_t)17ULL)) goto mn54;
                int64_t t56 = gm_l3;
                int64_t t57 = gm_l4;
                uint8_t t58 = (t56 <= t57);
                gm_t5e047ltlbe02 t59;
                GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t59 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t59 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t59 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t59 + 8), t58);
                gm_t1m5mugfio2m4 t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t60 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t60 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t60), t59);
                ((uint8_t *)&t60)[24] = 1;
                t38 = t60;
                goto me39;
mn54:;
ma42:;
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t62 != ((uint8_t)18ULL)) goto mn61;
                int64_t t63 = gm_l3;
                int64_t t64 = gm_l4;
                uint8_t t65 = (t63 > t64);
                gm_t5e047ltlbe02 t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t66 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t66 + 8), t65);
                gm_t1m5mugfio2m4 t67;
                GM_ST(uint8_t, (uint8_t *)&t67 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t67 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t67 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t67), t66);
                ((uint8_t *)&t67)[24] = 1;
                t38 = t67;
                goto me39;
mn61:;
ma43:;
                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t69 != ((uint8_t)19ULL)) goto mn68;
                int64_t t70 = gm_l3;
                int64_t t71 = gm_l4;
                uint8_t t72 = (t70 >= t71);
                gm_t5e047ltlbe02 t73;
                GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t73 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t73 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t73 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t73 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t73 + 8), t72);
                gm_t1m5mugfio2m4 t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t74), t73);
                ((uint8_t *)&t74)[24] = 1;
                t38 = t74;
                goto me39;
mn68:;
ma44:;
                gm_t1m5mugfio2m4 t76;
                memset(&t76, 0, sizeof t76);
                t38 = t76;
                goto me39;
mn75:;
ma45:;
                gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 90, 12);
me39:;
                uint8_t t77 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            goto ix34;
ie34:;
            {
                gm_t1m5mugfio2m4 t79;
                memset(&t79, 0, sizeof t79);
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
ix34:;
        }
        goto ix30;
ie30:;
        {
            gm_t1m5mugfio2m4 t82;
            memset(&t82, 0, sizeof t82);
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
ix30:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_compare_hq04o5p_c(gm_ts6oqppjh0ba3 a0, gm_t5e047ltlbe02 a1, gm_t5e047ltlbe02 a2) { gm_t1m5mugfio2m4 gm_r; gm_f_compare_hq04o5p(&gm_r, a0, &a1, &a2); return gm_r; }
__attribute__((aligned(4))) void gm_f_eval_5c9oefa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint32_t a3) {
    struct gm_fr_eval_5c9oefa F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    void * gm_l5; /* lit */
    uint64_t gm_l6; /* v */
    void * gm_l7; /* x */
    void * gm_l8; /* x */
    uint8_t gm_l9; /* b */
    gm_t08olq97rscn3 gm_l10; /* id */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    gm_t08olq97rscn3 gm_l13; /* m */
    gm_t08olq97rscn3 gm_l14; /* id */
    uint64_t gm_l15; /* d */
    gm_b16_8 gm_l18; /* args */
    gm_t08olq97rscn3 gm_l20; /* t */
    int64_t gm_l21; /* bytes */
    gm_ts6oqppjh0ba3 gm_l22; /* op */
    void * gm_l23; /* lhs */
    void * gm_l24; /* rhs */
    gm_t5e047ltlbe02 gm_l25; /* a */
    uint8_t gm_l26; /* x */
    uint8_t gm_l27; /* y */
    gm_t5e047ltlbe02 gm_l28; /* b */
    GM_STACK_CHECK(&F, 2890u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t3 = gm_l3;
        uint8_t t4 = (t3 > ((uint32_t)64ULL));
        if (!t4) goto ie2;
        {
            gm_t1m5mugfio2m4 t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l2;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma19; case ((uint8_t)11ULL): goto ma19; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma18; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma19; case ((uint8_t)17ULL): goto ma19; case ((uint8_t)18ULL): goto ma19; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma19; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma19; case ((uint8_t)24ULL): goto ma19; case ((uint8_t)25ULL): goto ma19; case ((uint8_t)26ULL): goto ma19; case ((uint8_t)27ULL): goto ma19; case ((uint8_t)28ULL): goto ma19; case ((uint8_t)29ULL): goto ma19; case ((uint8_t)30ULL): goto ma19; case ((uint8_t)31ULL): goto ma19; case ((uint8_t)32ULL): goto ma19; case ((uint8_t)33ULL): goto ma19; case ((uint8_t)34ULL): goto ma19; case ((uint8_t)35ULL): goto ma19; case ((uint8_t)36ULL): goto ma19; case ((uint8_t)37ULL): goto ma19; case ((uint8_t)38ULL): goto ma19; case ((uint8_t)39ULL): goto ma19; case ((uint8_t)40ULL): goto ma19; case ((uint8_t)41ULL): goto ma19; case ((uint8_t)42ULL): goto ma19; case ((uint8_t)43ULL): goto ma19; case ((uint8_t)44ULL): goto ma19; case ((uint8_t)45ULL): goto ma19; case ((uint8_t)46ULL): goto ma19; case ((uint8_t)47ULL): goto ma19; case ((uint8_t)48ULL): goto ma19; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma19; default: goto ma19; }
ma10:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t23 != ((uint8_t)3ULL)) goto mn22;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 8));
        gm_l4 = t24;
        uint8_t t25 = gm_l4;
        gm_t5e047ltlbe02 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t26 + 8), t25);
        gm_t1m5mugfio2m4 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 28, 0);
        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t27), t26);
        ((uint8_t *)&t27)[24] = 1;
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
        goto me9;
mn22:;
ma11:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t30 != ((uint8_t)0ULL)) goto mn29;
        void * t31 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l5 = t31;
        {
            void * t33 = gm_l5;
            gm_tf5srh4mp4b3i t34 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)t33) + 0)));
            gm_tf5srh4mp4b3i t35 = t34;
            uint64_t t36;
            if (((((uint8_t *)&t35))[8] != 0)) {
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t35) + 0));
                t36 = t37;
            } else {
                gm_t1m5mugfio2m4 t38;
                memset(&t38, 0, sizeof t38);
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
            gm_l6 = t36;
        }
        {
            uint64_t t40 = gm_l6;
            gm_t9805qtonbsag t41;
            uint32_t t42 = (uint32_t)t40;
            gm_t9805qtonbsag t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t43), t42);
            ((uint8_t *)&t43)[4] = 1;
            gm_t9805qtonbsag t44;
            memset(&t44, 0, sizeof t44);
            t41 = ((unsigned __int128)(t40) <= (((unsigned __int128)0ULL << 64) | 4294967295ULL)) ? t43 : t44;
            gm_t9805qtonbsag t45 = t41;
            uint32_t t46;
            if (((((uint8_t *)&t45))[4] != 0)) {
                uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)&t45) + 0));
                t46 = t47;
            } else {
                gm_t1m5mugfio2m4 t48;
                memset(&t48, 0, sizeof t48);
                uint8_t t49 = 0;
                F.ret = t48;
                goto gm_fret;
            }
            int64_t t50 = (int64_t)t46;
            gm_t5e047ltlbe02 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
            GM_ST(int64_t, ((uint8_t *)&t51 + 8), t50);
            gm_t1m5mugfio2m4 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 28, 0);
            GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[24] = 1;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto me9;
mn29:;
ma12:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t55 != ((uint8_t)9ULL)) goto mn54;
        void * t56 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
        gm_l7 = t56;
        void * t57 = gm_l0;
        uint64_t t58 = gm_l1;
        void * t59 = gm_l7;
        uint32_t t60 = gm_l3;
        gm_f_eval_5c9oefa((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), (void *)NULL, cx, NULL, t57, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me9;
mn54:;
ma13:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t62 != ((uint8_t)12ULL)) goto mn61;
        uint8_t t63 = GM_LD(uint8_t, ((((((uint8_t *)t8) + 8) + 8) + 0) + 0));
        if (t63 != ((uint8_t)5ULL)) goto mn61;
        void * t64 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l8 = t64;
        {
            void * t67 = gm_l0;
            uint64_t t68 = gm_l1;
            void * t69 = gm_l8;
            uint32_t t70 = gm_l3;
            gm_t1m5mugfio2m4 t71;
            gm_f_eval_5c9oefa((void *)&t71, (void *)NULL, cx, NULL, t67, t68, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1m5mugfio2m4 t72 = t71;
            if (!((((uint8_t *)&t72))[24] != 0)) goto ie66;
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t72) + 0));
            if (t73 != ((uint8_t)0ULL)) goto ie66;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t72) + 8));
            gm_l9 = t74;
            {
                uint8_t t76 = gm_l9;
                uint8_t t77 = (uint8_t)!t76;
                gm_t5e047ltlbe02 t78;
                GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t78 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t78 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t78 + 8), t77);
                gm_t1m5mugfio2m4 t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 28, 0);
                GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t79), t78);
                ((uint8_t *)&t79)[24] = 1;
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto ix66;
ie66:;
            {
                gm_t1m5mugfio2m4 t82;
                memset(&t82, 0, sizeof t82);
                uint8_t t83 = 0;
                F.ret = t82;
                goto gm_fret;
            }
ix66:;
        }
        goto me9;
mn61:;
ma14:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t85 != ((uint8_t)8ULL)) goto mn84;
        gm_t08olq97rscn3 t86 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l10 = t86;
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
        gm_t5e047ltlbe02 t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 8), t87);
        gm_t1m5mugfio2m4 t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 28, 0);
        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t89), t88);
        ((uint8_t *)&t89)[24] = 1;
        uint8_t t90 = 0;
        F.ret = t89;
        goto gm_fret;
        goto me9;
mn84:;
ma15:;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t92 != ((uint8_t)22ULL)) goto mn91;
        void * t93 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l11 = t93;
        gm_t08olq97rscn3 t94 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l12 = t94;
        {
            void * t97 = gm_l11;
            uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)t97) + 8) + 0));
            if (t98 != ((uint8_t)6ULL)) goto ie96;
            gm_t08olq97rscn3 t99 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t97) + 8) + 8));
            gm_l13 = t99;
            {
                uint8_t t102 = 0;
                uint8_t t103 = (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l13) + 0) + 8) == 6) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l13) + 0)), gm_snoai0j6d9u36, 6) == 0));
                if (!t103) {
                    void * t104 = gm_l0;
                    uint64_t t105 = gm_l1;
                    gm_b16_8 t106;
                    GM_ST(void *, (uint8_t *)&t106, (void *)(gm_snoai0j6d9u36));
                    GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(6));
                    gm_tf5srh4mp4b3i t107 = gm_f_lookup_59juqoo(t104, t105, t106);
                    gm_tf5srh4mp4b3i t108 = t107;
                    uint8_t t109;
                    if (((((uint8_t *)&t108))[8] != 0)) goto mn111;
                    t109 = ((uint8_t)0);
                    goto me110;
mn111:;
                    t109 = ((uint8_t)1);
                    goto me110;
mn112:;
                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 47, 42);
me110:;
                    t102 = t109;
                } else t102 = 1;
                if (!t102) goto ie101;
                {
                    gm_t1m5mugfio2m4 t114;
                    memset(&t114, 0, sizeof t114);
                    uint8_t t115 = 0;
                    F.ret = t114;
                    goto gm_fret;
                }
                goto ix101;
ie101:;
ix101:;
            }
            {
                void * t116 = gm_l0;
                gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_f_target_field_abqqbar((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), t116, t117);
                goto gm_fdone;
            }
            goto ix96;
ie96:;
            {
                gm_t1m5mugfio2m4 t119;
                memset(&t119, 0, sizeof t119);
                uint8_t t120 = 0;
                F.ret = t119;
                goto gm_fret;
            }
ix96:;
        }
        goto me9;
mn91:;
ma16:;
        uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t122 != ((uint8_t)6ULL)) goto mn121;
        gm_t08olq97rscn3 t123 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t8) + 8) + 8));
        gm_l14 = t123;
        {
            void * t125 = gm_l0;
            uint64_t t126 = gm_l1;
            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
            gm_tf5srh4mp4b3i t128 = gm_f_lookup_59juqoo(t125, t126, t127);
            gm_tf5srh4mp4b3i t129 = t128;
            uint64_t t130;
            if (((((uint8_t *)&t129))[8] != 0)) {
                uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&t129) + 0));
                t130 = t131;
            } else {
                gm_t1m5mugfio2m4 t132;
                memset(&t132, 0, sizeof t132);
                uint8_t t133 = 0;
                F.ret = t132;
                goto gm_fret;
            }
            gm_l15 = t130;
        }
        {
            uint64_t t135 = gm_l15;
            uint64_t t136 = (uintptr_t)t135;
            void * t137 = gm_l0;
            uint64_t t138 = GM_LD(uintptr_t, ((((uint8_t *)t137) + 240) + 0) + 8);
            if (t136 >= (uintptr_t)(t138)) gm_trap_index(t136, t138, gm_src2pgaip05o19o, 53, 28);
            uint8_t t139 = GM_LD(uint8_t, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t137) + 240) + 0)) + t136 * 216) + 40) + 64) + 0));
            if (t139 != ((uint8_t)0ULL)) goto ie134;
            gm_tv2uf4leil5hq t140 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t137) + 240) + 0)) + t136 * 216) + 40) + 64) + 8));
            F.l16 = t140;
            {
                uint8_t t143 = 0;
                uint8_t t144 = ((GM_LD(uint8_t, (((uint8_t *)&F.l16) + 32) + 0) == ((uint8_t)1ULL)));
                if (!t144) {
                    void * t145 = GM_LD(void *, (((uint8_t *)&F.l16) + 24));
                    void * t146 = t145;
                    uint8_t t147;
                    if ((GM_LD(void *, ((uint8_t *)&t146)) != 0)) goto mn149;
                    t147 = ((uint8_t)0);
                    goto me148;
mn149:;
                    t147 = ((uint8_t)1);
                    goto me148;
mn150:;
                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 54, 34);
me148:;
                    t143 = t147;
                } else t143 = 1;
                if (!t143) goto ie142;
                {
                    gm_t1m5mugfio2m4 t152;
                    memset(&t152, 0, sizeof t152);
                    uint8_t t153 = 0;
                    F.ret = t152;
                    goto gm_fret;
                }
                goto ix142;
ie142:;
ix142:;
            }
            {
                void * t154 = gm_l0;
                uint64_t t155 = gm_l1;
                void * t156 = GM_LD(void *, (((uint8_t *)&F.l16) + 40));
                uint32_t t157 = gm_l3;
                uint32_t r158; bool o159 = __builtin_add_overflow(t157, ((uint32_t)1ULL), &r158);
                if (o159) gm_trap("integer overflow in `+`", gm_src2pgaip05o19o, 55, 43);
                uint32_t t160 = r158;
                gm_f_eval_5c9oefa((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), (void *)NULL, cx, NULL, t154, t155, t156, t160);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix134;
ie134:;
            {
                gm_t1m5mugfio2m4 t162;
                memset(&t162, 0, sizeof t162);
                uint8_t t163 = 0;
                F.ret = t162;
                goto gm_fret;
            }
ix134:;
        }
        goto me9;
mn121:;
ma17:;
        uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t165 != ((uint8_t)49ULL)) goto mn164;
        gm_tet9guhev5r4u t166 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t8) + 8) + 8));
        F.l17 = t166;
        {
            uint8_t t169 = (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l17) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l17) + 0) + 0)), gm_saean4ms0g52k, 7) == 0));
            if (!t169) goto ie168;
            {
                gm_t1m5mugfio2m4 t171;
                memset(&t171, 0, sizeof t171);
                uint8_t t172 = 0;
                F.ret = t171;
                goto gm_fret;
            }
            goto ix168;
ie168:;
ix168:;
        }
        {
            gm_th2lqbo77pvsc t173 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l17) + 32));
            gm_th2lqbo77pvsc t174 = t173;
            gm_b16_8 t175;
            if (((((uint8_t *)&t174))[16] != 0)) {
                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 0));
                t175 = t176;
            } else {
                gm_t1m5mugfio2m4 t177;
                memset(&t177, 0, sizeof t177);
                uint8_t t178 = 0;
                F.ret = t177;
                goto gm_fret;
            }
            gm_l18 = t175;
        }
        {
            uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
            uint8_t t181 = (!(t180 == ((uint64_t)1ULL)));
            if (!t181) goto ie179;
            {
                gm_t1m5mugfio2m4 t183;
                memset(&t183, 0, sizeof t183);
                uint8_t t184 = 0;
                F.ret = t183;
                goto gm_fret;
            }
            goto ix179;
ie179:;
ix179:;
        }
        {
            uint64_t t186 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
            if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_src2pgaip05o19o, 61, 33);
            uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t186 * 144) + 0) + 0));
            if (t188 != ((uint8_t)0ULL)) goto ie185;
            gm_tc3oht4ksth9d t189 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t186 * 144) + 0) + 8));
            F.l19 = t189;
            {
                uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)&F.l19) + 8) + 0));
                if (t192 != ((uint8_t)6ULL)) goto ie191;
                gm_t08olq97rscn3 t193 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l19) + 8) + 8));
                gm_l20 = t193;
                {
                    void * t195 = gm_l0;
                    gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                    gm_t913bb735pvt5 t197 = gm_f_type_size_h6snnvm(t195, t196);
                    gm_t913bb735pvt5 t198 = t197;
                    int64_t t199;
                    if (((((uint8_t *)&t198))[8] != 0)) {
                        int64_t t200 = GM_LD(int64_t, (((uint8_t *)&t198) + 0));
                        t199 = t200;
                    } else {
                        gm_t1m5mugfio2m4 t201;
                        memset(&t201, 0, sizeof t201);
                        uint8_t t202 = 0;
                        F.ret = t201;
                        goto gm_fret;
                    }
                    gm_l21 = t199;
                }
                {
                    int64_t t203 = gm_l21;
                    gm_t5e047ltlbe02 t204;
                    GM_ST(uint8_t, (uint8_t *)&t204 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t204 + 16, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)1ULL));
                    GM_ST(int64_t, ((uint8_t *)&t204 + 8), t203);
                    gm_t1m5mugfio2m4 t205;
                    GM_ST(uint8_t, (uint8_t *)&t205 + 25, 0);
                    GM_ST(uint16_t, (uint8_t *)&t205 + 26, 0);
                    GM_ST(uint32_t, (uint8_t *)&t205 + 28, 0);
                    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t205), t204);
                    ((uint8_t *)&t205)[24] = 1;
                    uint8_t t206 = 0;
                    F.ret = t205;
                    goto gm_fret;
                }
                goto ix191;
ie191:;
                {
                    gm_t1m5mugfio2m4 t208;
                    memset(&t208, 0, sizeof t208);
                    uint8_t t209 = 0;
                    F.ret = t208;
                    goto gm_fret;
                }
ix191:;
            }
            goto ix185;
ie185:;
            {
                gm_t1m5mugfio2m4 t211;
                memset(&t211, 0, sizeof t211);
                uint8_t t212 = 0;
                F.ret = t211;
                goto gm_fret;
            }
ix185:;
        }
        goto me9;
mn164:;
ma18:;
        uint8_t t214 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
        if (t214 != ((uint8_t)13ULL)) goto mn213;
        gm_ts6oqppjh0ba3 t215 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t8) + 8) + 8) + 0));
        gm_l22 = t215;
        void * t216 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 8));
        gm_l23 = t216;
        void * t217 = GM_LD(void *, (((((uint8_t *)t8) + 8) + 8) + 16));
        gm_l24 = t217;
        {
            void * t219 = gm_l0;
            uint64_t t220 = gm_l1;
            void * t221 = gm_l23;
            uint32_t t222 = gm_l3;
            gm_t1m5mugfio2m4 t223;
            gm_f_eval_5c9oefa((void *)&t223, (void *)NULL, cx, NULL, t219, t220, t221, t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1m5mugfio2m4 t224 = t223;
            gm_t5e047ltlbe02 t225;
            if (((((uint8_t *)&t224))[24] != 0)) {
                gm_t5e047ltlbe02 t226 = GM_LD(gm_t5e047ltlbe02, (((uint8_t *)&t224) + 0));
                t225 = t226;
            } else {
                gm_t1m5mugfio2m4 t227;
                memset(&t227, 0, sizeof t227);
                uint8_t t228 = 0;
                F.ret = t227;
                goto gm_fret;
            }
            gm_l25 = t225;
        }
        {
            gm_ts6oqppjh0ba3 t230 = gm_l22;
            gm_ts6oqppjh0ba3 t231;
            GM_ST(uint8_t, (uint8_t *)&t231 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)20ULL));
            gm_ts6oqppjh0ba3 t232;
            GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t232 + 0), ((uint8_t)21ULL));
            gm_t9ldr1q5hn9fq t233;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t233 + 0), t231);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t233 + 2), t232);
            gm_t9ldr1q5hn9fq t234 = t233;
            uint8_t t235 = 0;
            gm_ts6oqppjh0ba3 t236 = t230;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t234) + 0, ((uint8_t *)&t236))) t235 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t234) + 2, ((uint8_t *)&t236))) t235 = 1;
            uint8_t t237 = (uint8_t)(t235);
            if (!t237) goto ie229;
            {
                uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                if (t240 != ((uint8_t)0ULL)) goto ie239;
                uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
                gm_l26 = t241;
                {
                    void * t244 = gm_l0;
                    uint64_t t245 = gm_l1;
                    void * t246 = gm_l24;
                    uint32_t t247 = gm_l3;
                    gm_t1m5mugfio2m4 t248;
                    gm_f_eval_5c9oefa((void *)&t248, (void *)NULL, cx, NULL, t244, t245, t246, t247);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t1m5mugfio2m4 t249 = t248;
                    if (!((((uint8_t *)&t249))[24] != 0)) goto ie243;
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&t249) + 0));
                    if (t250 != ((uint8_t)0ULL)) goto ie243;
                    uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&t249) + 8));
                    gm_l27 = t251;
                    {
                        uint8_t t253;
                        uint8_t t255 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l22) + 0) == ((uint8_t)20ULL)));
                        if (!t255) goto ie254;
                        uint8_t t257 = 0;
                        uint8_t t258 = gm_l26;
                        if (t258) {
                            uint8_t t259 = gm_l27;
                            t257 = t259;
                        } else t257 = 0;
                        uint8_t t260 = t257;
                        t253 = t260;
                        goto ix254;
ie254:;
                        uint8_t t262 = 0;
                        uint8_t t263 = gm_l26;
                        if (!t263) {
                            uint8_t t264 = gm_l27;
                            t262 = t264;
                        } else t262 = 1;
                        uint8_t t265 = t262;
                        t253 = t265;
ix254:;
                        gm_t5e047ltlbe02 t266;
                        GM_ST(uint8_t, (uint8_t *)&t266 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t266 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t266 + 4, 0);
                        GM_ST(uint8_t, (uint8_t *)&t266 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t266 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t266 + 12, 0);
                        GM_ST(uint64_t, (uint8_t *)&t266 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)0ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t266 + 8), t253);
                        gm_t1m5mugfio2m4 t267;
                        GM_ST(uint8_t, (uint8_t *)&t267 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t267 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t267 + 28, 0);
                        GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t267), t266);
                        ((uint8_t *)&t267)[24] = 1;
                        uint8_t t268 = 0;
                        F.ret = t267;
                        goto gm_fret;
                    }
                    goto ix243;
ie243:;
                    {
                        gm_t1m5mugfio2m4 t270;
                        memset(&t270, 0, sizeof t270);
                        uint8_t t271 = 0;
                        F.ret = t270;
                        goto gm_fret;
                    }
ix243:;
                }
                goto ix239;
ie239:;
                {
                    gm_t1m5mugfio2m4 t273;
                    memset(&t273, 0, sizeof t273);
                    uint8_t t274 = 0;
                    F.ret = t273;
                    goto gm_fret;
                }
ix239:;
            }
            goto ix229;
ie229:;
ix229:;
        }
        {
            void * t275 = gm_l0;
            uint64_t t276 = gm_l1;
            void * t277 = gm_l24;
            uint32_t t278 = gm_l3;
            gm_t1m5mugfio2m4 t279;
            gm_f_eval_5c9oefa((void *)&t279, (void *)NULL, cx, NULL, t275, t276, t277, t278);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1m5mugfio2m4 t280 = t279;
            gm_t5e047ltlbe02 t281;
            if (((((uint8_t *)&t280))[24] != 0)) {
                gm_t5e047ltlbe02 t282 = GM_LD(gm_t5e047ltlbe02, (((uint8_t *)&t280) + 0));
                t281 = t282;
            } else {
                gm_t1m5mugfio2m4 t283;
                memset(&t283, 0, sizeof t283);
                uint8_t t284 = 0;
                F.ret = t283;
                goto gm_fret;
            }
            gm_l28 = t281;
        }
        {
            gm_ts6oqppjh0ba3 t285 = gm_l22;
            gm_f_compare_hq04o5p((void *)&(*(gm_t1m5mugfio2m4 *)gm_out), t285, &(*(gm_t5e047ltlbe02 *)((uint8_t *)&gm_l25)), &(*(gm_t5e047ltlbe02 *)((uint8_t *)&gm_l28)));
            goto gm_fdone;
        }
        goto me9;
mn213:;
ma19:;
        gm_t1m5mugfio2m4 t287;
        memset(&t287, 0, sizeof t287);
        uint8_t t288 = 0;
        F.ret = t287;
        goto gm_fret;
        goto me9;
mn286:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 33, 5);
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
inline __attribute__((aligned(4))) void gm_f_target_field_abqqbar(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_target_field_abqqbar F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t1m5mugfio2m4 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sla1l2pt1mfvt, 2) == 0)) goto mn3;
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 88) + 24));
    gm_t5e047ltlbe02 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)2ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
    gm_t1m5mugfio2m4 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 28, 0);
    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[24] = 1;
    t1 = t7;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s3ngn7me40qa4, 4) == 0)) goto mn8;
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + 88) + 40));
    gm_t5e047ltlbe02 t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
    gm_t1m5mugfio2m4 t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t12 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t12 + 28, 0);
    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t12), t11);
    ((uint8_t *)&t12)[24] = 1;
    t1 = t12;
    goto me2;
mn8:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_soqlsukui24da, 6) == 0)) goto mn13;
    gm_b16_8 t14;
    void * t16 = gm_l0;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 88) + 8));
    if (!t17) goto ie15;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6iroad3kr6qd));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(6));
    gm_b16_8 t20 = t19;
    t14 = t20;
    goto ix15;
ie15:;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sjmmigkb19htg));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
    gm_b16_8 t23 = t22;
    t14 = t23;
ix15:;
    gm_t5e047ltlbe02 t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t14);
    gm_t1m5mugfio2m4 t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 28, 0);
    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[24] = 1;
    t1 = t25;
    goto me2;
mn13:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sa42h1oh66nds, 8) == 0)) goto mn26;
    void * t27 = gm_l0;
    void * t28 = (void *)(((uint8_t *)t27) + 88);
    uint32_t t29 = gm_f_ptr_bits_glps5i7(t28);
    int64_t t30 = (int64_t)t29;
    gm_t5e047ltlbe02 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
    GM_ST(int64_t, ((uint8_t *)&t31 + 8), t30);
    gm_t1m5mugfio2m4 t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 28, 0);
    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[24] = 1;
    t1 = t32;
    goto me2;
mn26:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sgdafdv2n5c25, 4) == 0)) goto mn33;
    void * t34 = gm_l0;
    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t34) + 72));
    gm_t5e047ltlbe02 t36;
    GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
    gm_t1m5mugfio2m4 t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 25, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 28, 0);
    GM_ST(gm_t5e047ltlbe02, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[24] = 1;
    t1 = t37;
    goto me2;
mn33:;
    gm_t1m5mugfio2m4 t39;
    memset(&t39, 0, sizeof t39);
    t1 = t39;
    goto me2;
mn38:;
    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 100, 65);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1m5mugfio2m4 gm_f_target_field_abqqbar_c(void * a0, gm_b16_8 a1) { gm_t1m5mugfio2m4 gm_r; gm_f_target_field_abqqbar(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_type_size_h6snnvm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_type_size_h6snnvm F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    int64_t gm_l2; /* bits */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    int64_t gm_l6; /* size */
    GM_STACK_CHECK_SHALLOW(&F, 841u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l1;
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_t6j6smj7qoctu t7 = t4;
        uint8_t t8 = 0;
        gm_b16_8 t9 = t3;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 0, ((uint8_t *)&t9))) t8 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 16, ((uint8_t *)&t9))) t8 = 1;
        uint8_t t10 = (uint8_t)(t8);
        if (!t10) goto ie2;
        {
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 88) + 0));
            int64_t t14 = (int64_t)t13;
            gm_t913bb735pvt5 t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[8] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t18 = gm_l1;
        gm_tfeab279c16rr t19;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_shnig5tqtdedi));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sm9e81djuncnu));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sqqajgod2bb4o));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 32, t22);
        gm_tfeab279c16rr t23 = t19;
        uint8_t t24 = 0;
        gm_b16_8 t25 = t18;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 0, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 16, ((uint8_t *)&t25))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 32, ((uint8_t *)&t25))) t24 = 1;
        uint8_t t26 = (uint8_t)(t24);
        if (!t26) goto ie17;
        {
            gm_t913bb735pvt5 t28;
            uint8_t t30 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_shnig5tqtdedi, 3) == 0));
            if (!t30) goto ie29;
            int64_t t32 = ((int64_t)2LL);
            gm_t913bb735pvt5 t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t33), t32);
            ((uint8_t *)&t33)[8] = 1;
            t28 = t33;
            goto ix29;
ie29:;
            gm_t913bb735pvt5 t34;
            uint8_t t36 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_sm9e81djuncnu, 3) == 0));
            if (!t36) goto ie35;
            int64_t t38 = ((int64_t)4LL);
            gm_t913bb735pvt5 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t39), t38);
            ((uint8_t *)&t39)[8] = 1;
            t34 = t39;
            goto ix35;
ie35:;
            int64_t t41 = ((int64_t)8LL);
            gm_t913bb735pvt5 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t42 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[8] = 1;
            t34 = t42;
ix35:;
            t28 = t34;
ix29:;
            uint8_t t43 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t45 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l1)), gm_s929lennfd3g6, 4) == 0));
        if (!t45) goto ie44;
        {
            gm_t913bb735pvt5 t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 12, 0);
            GM_ST(int64_t, ((uint8_t *)&t47), ((int64_t)1LL));
            ((uint8_t *)&t47)[8] = 1;
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        uint8_t t50 = 0;
        uint8_t t51 = 0;
        gm_b16_8 t52 = gm_l1;
        uint8_t t53 = gm_f_is_reserved_type_name_tuq4qsj(t52);
        uint8_t t54 = (uint8_t)!t53;
        if (!t54) {
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t56 = (t55 < ((uint64_t)2ULL));
            t51 = t56;
        } else t51 = 1;
        if (!t51) {
            uint64_t t57 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_src2pgaip05o19o, 114, 59);
            uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t57 * 1));
            gm_tqs5p4n2tq19v t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)117ULL));
            GM_ST(uint8_t, ((uint8_t *)&t60 + 1), ((uint8_t)105ULL));
            gm_tqs5p4n2tq19v t61 = t60;
            uint8_t t62 = 0;
            uint8_t t63 = t59;
            if (gm_eqjmccqdb90f(((uint8_t *)&t61) + 0, ((uint8_t *)&t63))) t62 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t61) + 1, ((uint8_t *)&t63))) t62 = 1;
            uint8_t t64 = (uint8_t)(!t62);
            t50 = t64;
        } else t50 = 1;
        if (!t50) goto ie49;
        {
            gm_t913bb735pvt5 t66;
            memset(&t66, 0, sizeof t66);
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        gm_l2 = ((int64_t)0LL);
    }
    {
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t70 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t71 = (uintptr_t)((uint64_t)1ULL);
            if (t71 > (uintptr_t)t69 || (uintptr_t)t69 > (uintptr_t)t69) gm_trap_slice(t71, (uintptr_t)t69, t69, gm_src2pgaip05o19o, 116, 14);
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t70 + t71 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)((uintptr_t)t69 - t71));
            gm_l3 = t72;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t74 = gm_l4;
                uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t76 = (t74 < t75);
                if (!t76) goto lb73;
                {
                    uint64_t t79 = gm_l4;
                    uint64_t t80 = (uintptr_t)t79;
                    uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t80 * 1));
                    uint8_t t83 = t82;
                    uint8_t t86 = GM_LD(uint8_t, ((uint8_t *)&t83));
                    gm_l5 = t86;
                    {
                        int64_t t88 = gm_l2;
                        int64_t r89; bool o90 = __builtin_mul_overflow(t88, ((int64_t)10LL), &r89);
                        if (o90) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 116, 33);
                        int64_t t91 = r89;
                        uint8_t t92 = gm_l5;
                        uint8_t r93; bool o94 = __builtin_sub_overflow(t92, ((uint8_t)48ULL), &r93);
                        if (o94) gm_trap("integer overflow in `-`", gm_src2pgaip05o19o, 116, 49);
                        uint8_t t95 = r93;
                        int64_t t96 = (int64_t)t95;
                        int64_t r97; bool o98 = __builtin_add_overflow(t91, t96, &r97);
                        if (o98) gm_trap("integer overflow in `+`", gm_src2pgaip05o19o, 116, 33);
                        int64_t t99 = r97;
                        gm_l2 = t99;
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_src2pgaip05o19o, 116, 5);
me84:;
                }
lc73:;
                {
                    uint64_t t101 = gm_l4;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l4 = t104;
                }
            }
lb73:;
        }
    }
    {
        gm_l6 = ((int64_t)1LL);
    }
    {
        for (;;) {
            int64_t t106 = gm_l6;
            int64_t r107; bool o108 = __builtin_mul_overflow(t106, ((int64_t)8LL), &r107);
            if (o108) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 119, 11);
            int64_t t109 = r107;
            int64_t t110 = gm_l2;
            uint8_t t111 = (t109 < t110);
            if (!t111) goto lb105;
            {
                int64_t t114 = gm_l6;
                int64_t r115; bool o116 = __builtin_mul_overflow(t114, ((int64_t)2LL), &r115);
                if (o116) gm_trap("integer overflow in `*`", gm_src2pgaip05o19o, 119, 29);
                int64_t t117 = r115;
                gm_l6 = t117;
            }
lc105:;
        }
lb105:;
    }
    {
        int64_t t118 = gm_l6;
        gm_t913bb735pvt5 t119;
        GM_ST(uint8_t, (uint8_t *)&t119 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t119 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t119 + 12, 0);
        GM_ST(int64_t, ((uint8_t *)&t119), t118);
        ((uint8_t *)&t119)[8] = 1;
        uint8_t t120 = 0;
        F.ret = t119;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_when_cond_9a46a5h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* cond */
    gm_t5e047ltlbe02 gm_l3; /* v */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l2;
        gm_t1m5mugfio2m4 t5;
        gm_f_eval_5c9oefa((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4, ((uint32_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1m5mugfio2m4 t6 = t5;
        gm_t5e047ltlbe02 t7;
        if (((((uint8_t *)&t6))[24] != 0)) {
            gm_t5e047ltlbe02 t8 = GM_LD(gm_t5e047ltlbe02, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
                void * t12 = gm_l2;
                gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s030jgonh2s3s));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(104));
                gm_b0_1 t15 = gm_f_report_eardf07((void *)NULL, cx, NULL, t11, t13, t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tqs5p4n2tq19v t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t19 != ((uint8_t)0ULL)) goto ie18;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 8));
        gm_l4 = t20;
        {
            uint8_t t22 = gm_l4;
            gm_tqs5p4n2tq19v t23;
            GM_ST(uint8_t, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[1] = 1;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix18;
ie18:;
        {
            {
                void * t27 = gm_l0;
                void * t28 = GM_LD(void *, (((uint8_t *)t27) + 8));
                void * t29 = gm_l2;
                gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_smbmfla2idsdo));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(67));
                gm_b0_1 t32 = gm_f_report_eardf07((void *)NULL, cx, NULL, t28, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tqs5p4n2tq19v t33;
                memset(&t33, 0, sizeof t33);
                uint8_t t34 = 0;
                F.ret = t33;
                goto gm_fret;
            }
        }
ix18:;
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
