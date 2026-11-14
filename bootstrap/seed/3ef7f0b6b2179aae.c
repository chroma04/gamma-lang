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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_cell_addr_bl2jmen {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_addr_heqn3dl {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_bits_1of8c6t {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_t6evimt84lndq l14; /* x */
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_global_addr_9an4270 {
    gm_frame h;
    gm_tq7qjpkmqmc5s l4; /* gl */
    gm_t833u93mr4iob ret;
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
struct gm_fr_place_addr_m751en7 {
    gm_frame h;
    gm_tpautr4br7h3l l17; /* sd */
    gm_t833u93mr4iob ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_value_bytes_l7s126n {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_tckdobunaulee l18; /* vd */
    gm_t6evimt84lndq l28; /* x */
    gm_t6evimt84lndq l34; /* x */
    gm_tpautr4br7h3l l36; /* sd */
    gm_t6evimt84lndq l43; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) void gm_f_get_srnes22(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_global_index_nhjk80t(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f99phma(void * a0, gm_tn68el8d14vku a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_t4gfk5t(void * a0, uint64_t a1, gm_tlcm4lvu3g1a8 a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_k5m9lemc4jn[40] __attribute__((aligned(8)));
extern uint8_t gm_kba69021uoh[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
extern uint8_t gm_koedfqu36hd[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sct0vr0h329vl[43];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_src8sgunk80orlb[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_0fd5f52 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t5 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 176));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    if (t6 > t10 || t10 > (uintptr_t)t4) gm_trap_slice(t6, t10, t4, gm_srcnqtpfma6qe4a, 106, 67);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t5 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t6));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t1, t11, t12);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 184u);
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
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t9);
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
__attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5) {
    struct gm_fr_cell_addr_bl2jmen F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* cell */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint64_t gm_l4; /* align */
    uint8_t gm_l5; /* mutable */
    uint64_t gm_l6; /* key */
    gm_tlcm4lvu3g1a8 gm_l7; /* a */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* b */
    GM_STACK_CHECK(&F, 1106u);
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
        void * t2 = gm_l2;
        uint64_t t3 = (uintptr_t)t2;
        gm_l6 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 896);
        uint64_t t7 = gm_l6;
        gm_tb7u19loo3j87 t8;
        gm_f_get_srnes22((void *)&t8, t6, t7);
        gm_tb7u19loo3j87 t9 = t8;
        if (!((((uint8_t *)&t9))[16] != 0)) goto ie4;
        gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t9));
        gm_l7 = t10;
        {
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
            gm_t833u93mr4iob t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
            ((uint8_t *)&t13)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t13 + 8), t12);
            gm_t833u93mr4iob t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l3;
        uint64_t t18 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_tl874t2atnlh3 t20 = gm_l1;
        uint64_t t21 = gm_l8;
        uint64_t t22 = gm_l4;
        gm_t9qkh0rsooc8d t23;
        uint8_t t25 = gm_l5;
        if (!t25) goto ie24;
        gm_t9qkh0rsooc8d t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        gm_t9qkh0rsooc8d t28 = t27;
        t23 = t28;
        goto ix24;
ie24:;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)4ULL));
        gm_t9qkh0rsooc8d t31 = t30;
        t23 = t31;
ix24:;
        uint8_t t32 = gm_l5;
        gm_t833u93mr4iob t33;
        gm_f_alloc_mem_0vu6kcs((void *)&t33, (void *)NULL, cx, NULL, t19, t20, t21, t22, t23, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) {
            gm_t76123gfet73g t35 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t34) + 8));
            gm_t833u93mr4iob t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t36 + 8), t35);
            F.ret = t36;
            goto gm_fret;
        }
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t34) + 8));
        gm_l9 = t37;
    }
    {
        void * t38 = gm_l0;
        void * t39 = (void *)(((uint8_t *)t38) + 896);
        uint64_t t40 = gm_l6;
        uint64_t t41 = gm_l9;
        gm_t88rcucg1b7uk t42 = gm_l3;
        gm_tlcm4lvu3g1a8 t43;
        GM_ST(uint32_t, (uint8_t *)&t43 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t41);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 8), t42);
        gm_tftb1odc739t5 t44 = gm_f_put_t4gfk5t(t39, t40, t43);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_src8sgunk80orlb, 20, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    {
        uint64_t t47 = gm_l8;
        uint64_t t48 = gm_f_us_cuv01fp(t47);
        gm_tabb710d9mq2e t49;
        gm_f_make_fill_jpvm0kc((void *)&t49, (void *)NULL, cx, NULL, t48, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5m9lemc4jn)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t50 = t49;
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_src8sgunk80orlb, 21, 10);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
        gm_l10 = t51;
    }
    {
    }
    {
        void * t56 = gm_l0;
        gm_tl874t2atnlh3 t57 = gm_l1;
        void * t58 = gm_l2;
        gm_t88rcucg1b7uk t59 = gm_l3;
        gm_b16_8 t60 = gm_l10;
        gm_tcfa6l7reoosn t61;
        gm_f_value_bytes_l7s126n((void *)&t61, (void *)NULL, cx, NULL, t56, t57, &(*(gm_t6evimt84lndq *)(((uint8_t *)t58) + 0)), t59, t60, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_tcfa6l7reoosn t62 = t61;
        if (((((uint8_t *)&t62))[0] != 0)) {
            gm_t76123gfet73g t63 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t62) + 8));
            gm_t833u93mr4iob t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t64 + 8), t63);
            F.ret = t64;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 8));
    }
    {
        {
            void * t67 = gm_l0;
            void * t68 = (void *)(((uint8_t *)t67) + 184);
            uint64_t t69 = gm_l9;
            gm_b16_8 t70 = gm_l10;
            gm_b16_8 t71 = t70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            gm_tgfdnufetdnfc t73;
            gm_f_init_9fuf99v((void *)&t73, t68, t69, t72);
        }
    }
    {
        uint64_t t74 = gm_l9;
        gm_t833u93mr4iob t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
        ((uint8_t *)&t75)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t75 + 8), t74);
        gm_t833u93mr4iob t76 = t75;
        uint8_t t77 = ((((uint8_t *)&t76))[0] != 0);
        F.ret = t75;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t77;
        goto gm_ld1_1;
    }
    {
        gm_b16_8 t78 = gm_l10;
        gm_b16_8 t79 = t78;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
        gm_b0_1 t81 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t80, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
    }
    goto gm_ls82;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t83 = gm_l10;
        gm_b16_8 t84 = t83;
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_b0_1 t86 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t85, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls82:;
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
inline __attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_addr_heqn3dl F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* cell */
    GM_STACK_CHECK(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t4), GM_CONV(gm_csguankko4lmhd, t5));
        gm_t9805qtonbsag t7 = t6;
        uint32_t t8;
        if (((((uint8_t *)&t7))[4] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            void * t10 = gm_l0;
            gm_tl874t2atnlh3 t11 = gm_l3;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0ggo8ek7kfrt));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(33));
            gm_t76123gfet73g t13;
            gm_f_unsupported_70vvk1e((void *)&t13, (void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t14 + 8), t13);
            gm_t833u93mr4iob t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 1968) + 0));
        gm_b16_8 t20 = t19;
        uint32_t t21 = gm_l4;
        uint64_t t22 = (uint64_t)t21;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        gm_b16_8 t25 = t20;
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_src8sgunk80orlb, 8, 13);
        void * t27 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t25)) + t24 * 88) + 80));
        void * t28 = t27;
        void * t29;
        if ((GM_LD(void *, ((uint8_t *)&t28)) != 0)) {
            void * t30 = GM_LD(void *, (((uint8_t *)&t28) + 0));
            t29 = t30;
        } else {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = gm_l3;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s3js9g3l46oiv));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(39));
            gm_t76123gfet73g t34;
            gm_f_unsupported_70vvk1e((void *)&t34, (void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t35 + 8), t34);
            gm_t833u93mr4iob t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        gm_l5 = t29;
    }
    {
        void * t38 = gm_l0;
        gm_tl874t2atnlh3 t39 = gm_l3;
        void * t40 = gm_l5;
        gm_t88rcucg1b7uk t41 = gm_l2;
        void * t42 = gm_l0;
        gm_t88rcucg1b7uk t43 = gm_l2;
        uint64_t t44 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_cell_addr_bl2jmen((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t38, t39, t40, t41, t44, ((uint8_t)0));
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
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_bits_1of8c6t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint8_t gm_l4; /* b */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* w */
    uint32_t gm_l7; /* index */
    gm_tcmkompbl09il gm_l8; /* u */
    void * gm_l9; /* a */
    unsigned __int128 gm_l10; /* raw */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    gm_tplpbqf3duvn3 gm_l15; /* bf */
    GM_STACK_CHECK(&F, 2111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t11 != ((uint8_t)2ULL)) goto mn10;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
        gm_l4 = t12;
        gm_tdlro53ljvkl0 t13;
        uint8_t t15 = gm_l4;
        if (!t15) goto ie14;
        unsigned __int128 t17 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        gm_tdlro53ljvkl0 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 40, 0);
        ((uint8_t *)&t18)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t18 + 16), t17);
        t13 = t18;
        goto ix14;
ie14:;
        unsigned __int128 t20 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        gm_tdlro53ljvkl0 t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t21 + 40, 0);
        ((uint8_t *)&t21)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t21 + 16), t20);
        t13 = t21;
ix14:;
        gm_tdlro53ljvkl0 t22 = t13;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t13;
        goto gm_fret;
        goto me2;
mn10:;
ma4:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t25 != ((uint8_t)0ULL)) goto mn24;
        unsigned __int128 t26 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
        gm_l5 = t26;
        {
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l3;
            uint32_t t30 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t30;
        }
        {
            gm_tdlro53ljvkl0 t31;
            uint32_t t33 = gm_l6;
            uint8_t t34 = (t33 >= ((uint32_t)128ULL));
            if (!t34) goto ie32;
            unsigned __int128 t36 = gm_l5;
            unsigned __int128 t37 = t36;
            gm_tdlro53ljvkl0 t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 40, 0);
            ((uint8_t *)&t38)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t38 + 16), t37);
            t31 = t38;
            goto ix32;
ie32:;
            unsigned __int128 t40 = gm_l5;
            uint32_t t41 = gm_l6;
            unsigned __int128 t42 = (unsigned __int128)t41;
            unsigned __int128 n43 = (unsigned __int128)t42;
            if (n43 >= 128) gm_trap_shift((uint64_t)n43, "u128", gm_src8sgunk80orlb, 175, 57);
            unsigned __int128 r44 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n43);
            bool o45 = (unsigned __int128)(r44 >> (int)n43) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o45) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 175, 57);
            unsigned __int128 t46 = r44;
            unsigned __int128 r47; bool o48 = __builtin_sub_overflow(t46, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_src8sgunk80orlb, 175, 56);
            unsigned __int128 t49 = r47;
            unsigned __int128 t50 = (unsigned __int128)(t40 & t49);
            unsigned __int128 t51 = t50;
            gm_tdlro53ljvkl0 t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t52 + 40, 0);
            ((uint8_t *)&t52)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t52 + 16), t51);
            t31 = t52;
ix32:;
            gm_tdlro53ljvkl0 t53 = t31;
            uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto me2;
mn24:;
ma5:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t56 != ((uint8_t)10ULL)) goto mn55;
        uint32_t t57 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
        gm_l7 = t57;
        {
            void * t59 = gm_l0;
            gm_t88rcucg1b7uk t60 = gm_l3;
            gm_t57euh612ligr t61;
            gm_f_union_def_h8mvf4t((void *)&t61, t59, GM_CONV(gm_csguankko4lmhd, t60));
            gm_t57euh612ligr t62 = t61;
            gm_tcmkompbl09il t63;
            if (((((uint8_t *)&t62))[24] != 0)) {
                gm_tcmkompbl09il t64 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t62) + 0));
                t63 = t64;
            } else {
                void * t65 = gm_l0;
                gm_tl874t2atnlh3 t66 = gm_l1;
                gm_b16_8 t67;
                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(24));
                gm_t76123gfet73g t68;
                gm_f_unsupported_70vvk1e((void *)&t68, (void *)NULL, cx, NULL, t65, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tdlro53ljvkl0 t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
                ((uint8_t *)&t69)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t69 + 16), t68);
                gm_tdlro53ljvkl0 t70 = t69;
                uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
                F.ret = t69;
                goto gm_fret;
            }
            gm_l8 = t63;
        }
        {
            void * t72 = gm_l0;
            gm_tl874t2atnlh3 t73 = gm_l1;
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t75 = t74;
            uint32_t t76 = gm_l7;
            uint64_t t77 = (uint64_t)t76;
            uint64_t t78 = gm_f_us_cuv01fp(t77);
            uint64_t t79 = (uintptr_t)t78;
            gm_b16_8 t80 = t75;
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&t80) + 8);
            if (t79 >= (uintptr_t)(t81)) gm_trap_index(t79, t81, gm_src8sgunk80orlb, 179, 59);
            gm_tbci5ospk5nnn t82 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t80)) + t79 * 72) + 24));
            unsigned __int128 t83 = gm_f_bits128_jigkui4(&t82);
            gm_t6evimt84lndq t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t84 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t84 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t84 + 16), t83);
            gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
            gm_f_const_bits_1of8c6t((void *)&(*(gm_tdlro53ljvkl0 *)gm_out), (void *)NULL, cx, NULL, t72, t73, &t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me2;
mn55:;
ma6:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t87 != ((uint8_t)3ULL)) goto mn86;
        void * t88 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
        gm_l9 = t88;
        {
            gm_l10 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                void * t91 = gm_l9;
                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t91) + 0));
                gm_b16_8 t93 = t92;
                gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                gm_l11 = t94;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t96 = gm_l12;
                    uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t98 = (t96 < t97);
                    if (!t98) goto lb95;
                    {
                        gm_thf8as07kj3uj t101;
                        memset((uint8_t *)&t101 + 8, 0, 8);
                        uint64_t t102 = gm_l12;
                        GM_ST(uint64_t, (uint8_t *)&t101 + 0, t102);
                        uint64_t t103 = gm_l12;
                        uint64_t t104 = (uintptr_t)t103;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        memcpy((uint8_t *)&t101 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t104 * 48), 48);
                        gm_thf8as07kj3uj t106 = t101;
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&t106) + 0));
                        gm_l13 = t109;
                        gm_t6evimt84lndq t110 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t106) + 16));
                        F.l14 = t110;
                        {
                            void * t112 = gm_l0;
                            gm_t88rcucg1b7uk t113 = gm_l3;
                            uint64_t t114 = gm_l13;
                            uint32_t t115 = (uint32_t)t114;
                            gm_tplpbqf3duvn3 t116 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t112, t113, t115);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            gm_l15 = t116;
                        }
                        {
                            unsigned __int128 t117 = gm_l10;
                            void * t118 = gm_l0;
                            gm_tl874t2atnlh3 t119 = gm_l1;
                            gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 4));
                            gm_tdlro53ljvkl0 t121;
                            gm_f_const_bits_1of8c6t((void *)&t121, (void *)NULL, cx, NULL, t118, t119, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l14)), t120);
                            if (gm_unw.kind) {
                                goto gm_ll99;
                            }
                            gm_tdlro53ljvkl0 t122 = t121;
                            if (((((uint8_t *)&t122))[0] != 0)) {
                                gm_t76123gfet73g t123 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t122) + 16));
                                gm_tdlro53ljvkl0 t124;
                                GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t124 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t124 + 40, 0);
                                ((uint8_t *)&t124)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t124 + 16), t123);
                                F.ret = t124;
                                goto gm_fret;
                            }
                            unsigned __int128 t125 = GM_LD(unsigned __int128, (((uint8_t *)&t122) + 16));
                            uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 0));
                            unsigned __int128 t127 = (unsigned __int128)t126;
                            unsigned __int128 n128 = (unsigned __int128)t127;
                            if (n128 >= 128) gm_trap_shift((uint64_t)n128, "u128", gm_src8sgunk80orlb, 185, 24);
                            unsigned __int128 r129 = (unsigned __int128)((unsigned __int128)t125 << (int)n128);
                            bool o130 = (unsigned __int128)(r129 >> (int)n128) != t125;
                            if (o130) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 185, 24);
                            unsigned __int128 t131 = r129;
                            unsigned __int128 t132 = (unsigned __int128)(t117 | t131);
                            gm_l10 = t132;
                        }
                        goto me107;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 183, 13);
me107:;
                    }
lc95:;
                    {
                        uint64_t t134 = gm_l12;
                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                        uint64_t t137 = r135;
                        gm_l12 = t137;
                    }
                }
gm_ll99:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
                }
                goto gm_lad;
lb95:;
            }
        }
        {
            unsigned __int128 t138 = gm_l10;
            gm_tdlro53ljvkl0 t139;
            GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t139 + 40, 0);
            ((uint8_t *)&t139)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t139 + 16), t138);
            gm_tdlro53ljvkl0 t140 = t139;
            uint8_t t141 = ((((uint8_t *)&t140))[0] != 0);
            F.ret = t139;
            goto gm_fret;
        }
        goto me2;
mn86:;
ma7:;
        void * t143 = gm_l0;
        gm_tl874t2atnlh3 t144 = gm_l1;
        gm_b16_8 t145;
        GM_ST(void *, (uint8_t *)&t145, (void *)(gm_sfali8nl85hc9));
        GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(24));
        gm_t76123gfet73g t146;
        gm_f_unsupported_70vvk1e((void *)&t146, (void *)NULL, cx, NULL, t143, t144, t145);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdlro53ljvkl0 t147;
        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t147 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t147 + 40, 0);
        ((uint8_t *)&t147)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t147 + 16), t146);
        gm_tdlro53ljvkl0 t148 = t147;
        uint8_t t149 = ((((uint8_t *)&t148))[0] != 0);
        F.ret = t147;
        goto gm_fret;
        goto me2;
mn142:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 171, 5);
me2:;
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
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_global_addr_9an4270 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* decl */
    uint64_t gm_l3; /* i */
    gm_tn68el8d14vku gm_l5; /* key */
    uint64_t gm_l6; /* a */
    uint64_t gm_l7; /* al */
    uint64_t gm_l8; /* addr */
    void * gm_l9; /* cell */
    void * gm_l10; /* cell */
    gm_b16_8 gm_l11; /* b */
    GM_STACK_CHECK(&F, 2358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint64_t t4 = gm_l2;
        gm_tf5srh4mp4b3i t5 = gm_f_global_index_nhjk80t(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        uint64_t t7;
        if (((((uint8_t *)&t6))[8] != 0)) {
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            void * t9 = gm_l0;
            gm_tl874t2atnlh3 t10 = gm_l1;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sct0vr0h329vl));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(42));
            gm_t76123gfet73g t12;
            gm_f_trap_qh84l9d((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t13 + 8), t12);
            gm_t833u93mr4iob t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t7;
    }
    {
        uint64_t t16 = gm_l3;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 8));
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 1448) + 0) + 8);
        if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_src8sgunk80orlb, 31, 11);
        gm_tq7qjpkmqmc5s t21 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 1448) + 0)) + t17 * 104));
        F.l4 = t21;
    }
    {
        uint64_t t22 = gm_l3;
        uint32_t t23;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t25) goto ie24;
        void * t27 = gm_l0;
        uint32_t t28 = GM_LD(uint32_t, ((((uint8_t *)t27) + 1656) + 40));
        uint32_t t29 = t28;
        t23 = t29;
        goto ix24;
ie24:;
        uint32_t t31 = ((uint32_t)0ULL);
        t23 = t31;
ix24:;
        gm_tn68el8d14vku t32;
        GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t32 + 0), t22);
        GM_ST(uint32_t, ((uint8_t *)&t32 + 8), t23);
        gm_l5 = t32;
    }
    {
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t34) goto ie33;
        {
            void * t37 = gm_l0;
            void * t38 = (void *)(((uint8_t *)t37) + 992);
            gm_tn68el8d14vku t39 = gm_l5;
            gm_tf5srh4mp4b3i t40 = gm_f_get_ftjmcbc(t38, t39);
            gm_tf5srh4mp4b3i t41 = t40;
            if (!((((uint8_t *)&t41))[8] != 0)) goto ie36;
            uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&t41));
            gm_l6 = t42;
            {
                uint64_t t44 = gm_l6;
                gm_t833u93mr4iob t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
                ((uint8_t *)&t45)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t45 + 8), t44);
                gm_t833u93mr4iob t46 = t45;
                uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
                F.ret = t45;
                goto gm_fret;
            }
            goto ix36;
ie36:;
ix36:;
        }
        goto ix33;
ie33:;
        uint64_t t49 = gm_l3;
        uint64_t t50 = (uintptr_t)t49;
        void * t51 = gm_l0;
        uint64_t t52 = GM_LD(uintptr_t, ((((uint8_t *)t51) + 952) + 0) + 8);
        if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_src8sgunk80orlb, 36, 15);
        uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t51) + 952) + 0)) + t50 * 8));
        uint8_t t54 = (!(t53 == ((uint64_t)0ULL)));
        if (!t54) goto ie48;
        {
            uint64_t t56 = gm_l3;
            uint64_t t57 = (uintptr_t)t56;
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 952) + 0) + 8);
            if (t57 >= (uintptr_t)(t59)) gm_trap_index(t57, t59, gm_src8sgunk80orlb, 37, 16);
            uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t58) + 952) + 0)) + t57 * 8));
            gm_t833u93mr4iob t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t61 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t61 + 24, 0);
            ((uint8_t *)&t61)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t61 + 8), t60);
            gm_t833u93mr4iob t62 = t61;
            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
            F.ret = t61;
            goto gm_fret;
        }
        goto ix48;
ie48:;
ix48:;
ix33:;
    }
    {
        uint8_t t65;
        if ((((((uint8_t *)&F.l4) + 56))[16] != 0)) goto mn67;
        t65 = ((uint8_t)0);
        goto me66;
mn67:;
        t65 = ((uint8_t)1);
        goto me66;
mn68:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 39, 8);
me66:;
        if (!t65) goto ie64;
        {
            void * t70 = gm_l0;
            gm_tl874t2atnlh3 t71 = gm_l1;
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
            gm_tnpifv20irnph t73;
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
            gm_tabb710d9mq2e t74;
            gm_f_format_3tqcmdm((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t75 = t74;
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src8sgunk80orlb, 39, 63);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
            gm_b16_8 t77 = t76;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_t76123gfet73g t79;
            gm_f_unsupported_70vvk1e((void *)&t79, (void *)NULL, cx, NULL, t70, t71, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t80 + 8), t79);
            gm_t833u93mr4iob t81 = t80;
            uint8_t t82 = ((((uint8_t *)&t81))[0] != 0);
            F.ret = t80;
            goto gm_fret;
        }
        goto ix64;
ie64:;
ix64:;
    }
    {
        void * t83 = gm_l0;
        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
        uint64_t t85 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t86 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l4) + 80));
        gm_tf5srh4mp4b3i t87 = t86;
        uint64_t t88;
        if (((((uint8_t *)&t87))[8] != 0)) {
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&t87) + 0));
            t88 = t89;
        } else {
            t88 = ((uint64_t)1ULL);
        }
        uint64_t t90 = (t85 > t88 ? t85 : t88);
        gm_l7 = t90;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        void * t92 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
        void * t93 = t92;
        if (!(GM_LD(void *, ((uint8_t *)&t93)) != 0)) goto ie91;
        void * t94 = GM_LD(void *, ((uint8_t *)&t93));
        gm_l9 = t94;
        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        uint8_t t96 = (uint8_t)!t95;
        if (!t96) goto ie91;
        {
            void * t98 = gm_l0;
            gm_tl874t2atnlh3 t99 = gm_l1;
            void * t100 = gm_l9;
            gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t102 = gm_l7;
            gm_t833u93mr4iob t103;
            gm_f_cell_addr_bl2jmen((void *)&t103, (void *)NULL, cx, NULL, t98, t99, t100, t101, t102, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t104 = t103;
            if (((((uint8_t *)&t104))[0] != 0)) {
                gm_t76123gfet73g t105 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t104) + 8));
                gm_t833u93mr4iob t106;
                GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                ((uint8_t *)&t106)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t106 + 8), t105);
                F.ret = t106;
                goto gm_fret;
            }
            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&t104) + 8));
            gm_l8 = t107;
        }
        goto ix91;
ie91:;
        void * t109 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
        void * t110 = t109;
        if (!(GM_LD(void *, ((uint8_t *)&t110)) != 0)) goto ie108;
        void * t111 = GM_LD(void *, ((uint8_t *)&t110));
        gm_l10 = t111;
        {
            void * t113 = gm_l0;
            gm_tl874t2atnlh3 t114 = gm_l1;
            void * t115 = gm_l0;
            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t117 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t118 = gm_l7;
            gm_t9qkh0rsooc8d t119;
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)5ULL));
            gm_t833u93mr4iob t120;
            gm_f_alloc_mem_0vu6kcs((void *)&t120, (void *)NULL, cx, NULL, t113, t114, t117, t118, t119, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t121 = t120;
            if (((((uint8_t *)&t121))[0] != 0)) {
                gm_t76123gfet73g t122 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t121) + 8));
                gm_t833u93mr4iob t123;
                GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                ((uint8_t *)&t123)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t123 + 8), t122);
                F.ret = t123;
                goto gm_fret;
            }
            uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&t121) + 8));
            gm_l8 = t124;
        }
        {
            void * t125 = gm_l0;
            gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t127 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t125, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t128 = gm_f_us_cuv01fp(t127);
            gm_tabb710d9mq2e t129;
            gm_f_make_fill_jpvm0kc((void *)&t129, (void *)NULL, cx, NULL, t128, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koedfqu36hd)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t130 = t129;
            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src8sgunk80orlb, 47, 14);
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
            gm_l11 = t131;
        }
        {
        }
        {
            void * t136 = gm_l0;
            gm_tl874t2atnlh3 t137 = gm_l1;
            void * t138 = gm_l10;
            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            gm_b16_8 t140 = gm_l11;
            gm_tcfa6l7reoosn t141;
            gm_f_value_bytes_l7s126n((void *)&t141, (void *)NULL, cx, NULL, t136, t137, &(*(gm_t6evimt84lndq *)(((uint8_t *)t138) + 0)), t139, t140, ((uint64_t)0ULL));
            if (gm_unw.kind) {
                goto gm_ld112_1;
            }
            gm_tcfa6l7reoosn t142 = t141;
            if (((((uint8_t *)&t142))[0] != 0)) {
                gm_t76123gfet73g t143 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t142) + 8));
                gm_t833u93mr4iob t144;
                GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                ((uint8_t *)&t144)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t144 + 8), t143);
                F.ret = t144;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld112_1;
            }
            gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 8));
        }
        {
            {
                void * t147 = gm_l0;
                void * t148 = (void *)(((uint8_t *)t147) + 184);
                uint64_t t149 = gm_l8;
                gm_b16_8 t150 = gm_l11;
                gm_b16_8 t151 = t150;
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                gm_tgfdnufetdnfc t153;
                gm_f_init_9fuf99v((void *)&t153, t148, t149, t152);
            }
        }
        {
            gm_b16_8 t154 = gm_l11;
            gm_b16_8 t155 = t154;
            gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
            gm_b0_1 t157 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t156, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
        }
        goto gm_ls158;
gm_ld112_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            gm_b16_8 t159 = gm_l11;
            gm_b16_8 t160 = t159;
            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
            gm_b0_1 t162 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t161, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls158:;
        goto ix108;
ie108:;
        {
            void * t164 = gm_l0;
            gm_tl874t2atnlh3 t165 = gm_l1;
            void * t166 = gm_l0;
            gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t168 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t169 = gm_l7;
            gm_t9qkh0rsooc8d t170;
            GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)5ULL));
            gm_t833u93mr4iob t171;
            gm_f_alloc_mem_0vu6kcs((void *)&t171, (void *)NULL, cx, NULL, t164, t165, t168, t169, t170, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t172 = t171;
            if (((((uint8_t *)&t172))[0] != 0)) {
                gm_t76123gfet73g t173 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t172) + 8));
                gm_t833u93mr4iob t174;
                GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                ((uint8_t *)&t174)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t174 + 8), t173);
                F.ret = t174;
                goto gm_fret;
            }
            uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&t172) + 8));
            gm_l8 = t175;
        }
ix108:;
ix91:;
    }
    {
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
        if (!t177) goto ie176;
        {
            void * t179 = gm_l0;
            void * t180 = (void *)(((uint8_t *)t179) + 992);
            gm_tn68el8d14vku t181 = gm_l5;
            uint64_t t182 = gm_l8;
            gm_tftb1odc739t5 t183 = gm_f_put_f99phma(t180, t181, t182);
            gm_tftb1odc739t5 t184 = t183;
            if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_src8sgunk80orlb, 54, 26);
            gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
        }
        goto ix176;
ie176:;
        {
            uint64_t t187 = gm_l3;
            uint64_t t188 = (uintptr_t)t187;
            void * t189 = gm_l0;
            uint64_t t190 = GM_LD(uintptr_t, ((((uint8_t *)t189) + 952) + 0) + 8);
            if (t188 >= (uintptr_t)(t190)) gm_trap_index(t188, t190, gm_src8sgunk80orlb, 54, 68);
            uint64_t t191 = gm_l8;
            GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t189) + 952) + 0)) + t188 * 8), t191);
        }
ix176:;
    }
    {
        uint64_t t192 = gm_l8;
        gm_t833u93mr4iob t193;
        GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t193 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t193 + 24, 0);
        ((uint8_t *)&t193)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t193 + 8), t192);
        gm_t833u93mr4iob t194 = t193;
        uint8_t t195 = ((((uint8_t *)&t194))[0] != 0);
        F.ret = t193;
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
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_place_addr_m751en7 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkaapcii86udk gm_l2; /* pl */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    gm_t88rcucg1b7uk gm_l4; /* root */
    gm_tlcm4lvu3g1a8 gm_l5; /* known */
    void * gm_l6; /* a */
    uint64_t gm_l7; /* base */
    uint64_t gm_l8; /* addend */
    gm_t88rcucg1b7uk gm_l9; /* cur */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint64_t gm_l13; /* len */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    gm_b16_8 gm_l15; /* elems */
    uint32_t gm_l16; /* id */
    GM_STACK_CHECK(&F, 1812u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 896);
        void * t6 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint64_t t7 = (uintptr_t)t6;
        gm_tb7u19loo3j87 t8;
        gm_f_get_srnes22((void *)&t8, t5, t7);
        gm_tb7u19loo3j87 t9 = t8;
        if (!((((uint8_t *)&t9))[16] != 0)) goto ie3;
        gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t9));
        gm_l5 = t10;
        {
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t12;
        }
        goto ix3;
ie3:;
        uint64_t t14 = GM_LD(uintptr_t, (((uint8_t *)a2) + 8) + 8);
        uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
        if (!t15) goto ie13;
        void * t16 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 0) + 0));
        if (t17 != ((uint8_t)3ULL)) goto ie13;
        void * t18 = GM_LD(void *, ((((uint8_t *)t16) + 0) + 16));
        gm_l6 = t18;
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            void * t23 = gm_l6;
            uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 0) + 8);
            uint64_t t25 = (uint64_t)t24;
            gm_t88rcucg1b7uk t26 = gm_l3;
            gm_t88rcucg1b7uk t27 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t22, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t27;
        }
        goto ix13;
ie13:;
        uint64_t t29 = GM_LD(uintptr_t, (((uint8_t *)a2) + 8) + 8);
        uint8_t t30 = (t29 > ((uint64_t)0ULL));
        if (!t30) goto ie28;
        {
            void * t32 = gm_l0;
            gm_tl874t2atnlh3 t33 = gm_l1;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sqrl3kgomorsv));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(38));
            gm_t76123gfet73g t35;
            gm_f_unsupported_70vvk1e((void *)&t35, (void *)NULL, cx, NULL, t32, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t833u93mr4iob t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t36 + 8), t35);
            gm_t833u93mr4iob t37 = t36;
            uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
            F.ret = t36;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
ix13:;
ix3:;
    }
    {
        void * t39 = gm_l0;
        gm_tl874t2atnlh3 t40 = gm_l1;
        void * t41 = GM_LD(void *, (((uint8_t *)a2) + 0));
        gm_t88rcucg1b7uk t42 = gm_l4;
        void * t43 = gm_l0;
        gm_t88rcucg1b7uk t44 = gm_l4;
        uint64_t t45 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t46 = GM_LD(void *, (((uint8_t *)a2) + 0));
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t46) + 48));
        uint8_t t48 = (uint8_t)!t47;
        gm_t833u93mr4iob t49;
        gm_f_cell_addr_bl2jmen((void *)&t49, (void *)NULL, cx, NULL, t39, t40, t41, t42, t45, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t50 = t49;
        if (((((uint8_t *)&t50))[0] != 0)) {
            gm_t76123gfet73g t51 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t50) + 8));
            gm_t833u93mr4iob t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
            ((uint8_t *)&t52)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t52 + 8), t51);
            F.ret = t52;
            goto gm_fret;
        }
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&t50) + 8));
        gm_l7 = t53;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        gm_t88rcucg1b7uk t54 = gm_l4;
        gm_l9 = t54;
    }
    {
        {
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l10 = t56;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t58 = gm_l11;
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t60 = (t58 < t59);
                if (!t60) goto lb57;
                {
                    uint64_t t63 = gm_l11;
                    uint64_t t64 = (uintptr_t)t63;
                    uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t64 * 8));
                    uint64_t t67 = t66;
                    uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&t67));
                    gm_l12 = t70;
                    {
                        void * t72 = gm_l0;
                        gm_t88rcucg1b7uk t73 = gm_l9;
                        gm_t53at80pfdus3 t74;
                        gm_f_kind_htv6093((void *)&t74, t72, GM_CONV(gm_csguankko4lmhd, t73));
                        gm_t53at80pfdus3 t75 = t74;
                        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
                        switch (t82) { case ((uint8_t)0ULL): goto ma80; case ((uint8_t)1ULL): goto ma80; case ((uint8_t)2ULL): goto ma80; case ((uint8_t)3ULL): goto ma80; case ((uint8_t)4ULL): goto ma80; case ((uint8_t)5ULL): goto ma80; case ((uint8_t)6ULL): goto ma80; case ((uint8_t)7ULL): goto ma80; case ((uint8_t)8ULL): goto ma80; case ((uint8_t)9ULL): goto ma80; case ((uint8_t)10ULL): goto ma80; case ((uint8_t)11ULL): goto ma77; case ((uint8_t)12ULL): goto ma78; case ((uint8_t)13ULL): goto ma80; case ((uint8_t)14ULL): goto ma80; case ((uint8_t)15ULL): goto ma80; case ((uint8_t)16ULL): goto ma80; case ((uint8_t)17ULL): goto ma79; case ((uint8_t)18ULL): goto ma80; case ((uint8_t)19ULL): goto ma80; case ((uint8_t)20ULL): goto ma80; case ((uint8_t)21ULL): goto ma80; case ((uint8_t)22ULL): goto ma80; case ((uint8_t)23ULL): goto ma80; case ((uint8_t)24ULL): goto ma80; case ((uint8_t)25ULL): goto ma80; case ((uint8_t)26ULL): goto ma80; default: goto ma80; }
ma77:;
                        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
                        if (t84 != ((uint8_t)11ULL)) goto mn83;
                        uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)&t75) + 8) + 0));
                        gm_l13 = t85;
                        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t75) + 8) + 8));
                        gm_l14 = t86;
                        {
                            uint64_t t88 = gm_l8;
                            uint64_t t89 = gm_l12;
                            void * t90 = gm_l0;
                            gm_t88rcucg1b7uk t91 = gm_l14;
                            uint64_t t92 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t90, t91);
                            if (gm_unw.kind) {
                                goto gm_ll61;
                            }
                            uint64_t r93; bool o94 = __builtin_mul_overflow(t89, t92, &r93);
                            if (o94) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 151, 27);
                            uint64_t t95 = r93;
                            uint64_t r96; bool o97 = __builtin_add_overflow(t88, t95, &r96);
                            if (o97) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 151, 17);
                            uint64_t t98 = r96;
                            gm_l8 = t98;
                        }
                        {
                            uint64_t t100 = gm_l12;
                            uint64_t t101 = gm_l13;
                            uint8_t t102 = (t100 < t101);
                            if (!t102) goto ie99;
                            {
                                gm_t88rcucg1b7uk t104 = gm_l14;
                                gm_l9 = t104;
                            }
                            goto ix99;
ie99:;
ix99:;
                        }
                        goto me76;
mn83:;
ma78:;
                        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
                        if (t106 != ((uint8_t)12ULL)) goto mn105;
                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((((uint8_t *)&t75) + 8) + 0));
                        gm_l15 = t107;
                        {
                            uint64_t t109 = gm_l8;
                            void * t110 = gm_l0;
                            gm_t88rcucg1b7uk t111 = gm_l9;
                            gm_b16_8 t112 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t110, t111);
                            if (gm_unw.kind) {
                                goto gm_ll61;
                            }
                            gm_b16_8 t113 = t112;
                            uint64_t t114 = gm_l12;
                            uint64_t t115 = gm_f_us_cuv01fp(t114);
                            uint64_t t116 = (uintptr_t)t115;
                            gm_b16_8 t117 = t113;
                            uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&t117) + 8);
                            if (t116 >= (uintptr_t)(t118)) gm_trap_index(t116, t118, gm_src8sgunk80orlb, 155, 27);
                            uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t117)) + t116 * 8));
                            uint64_t r120; bool o121 = __builtin_add_overflow(t109, t119, &r120);
                            if (o121) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 155, 17);
                            uint64_t t122 = r120;
                            gm_l8 = t122;
                        }
                        {
                            gm_b16_8 t123 = gm_l15;
                            gm_b16_8 t124 = t123;
                            uint64_t t125 = gm_l12;
                            uint64_t t126 = gm_f_us_cuv01fp(t125);
                            uint64_t t127 = (uintptr_t)t126;
                            gm_b16_8 t128 = t124;
                            uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&t128) + 8);
                            if (t127 >= (uintptr_t)(t129)) gm_trap_index(t127, t129, gm_src8sgunk80orlb, 156, 23);
                            gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t128)) + t127 * 4));
                            gm_l9 = t130;
                        }
                        goto me76;
mn105:;
ma79:;
                        uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
                        if (t132 != ((uint8_t)17ULL)) goto mn131;
                        uint32_t t133 = GM_LD(uint32_t, ((((uint8_t *)&t75) + 8) + 0));
                        gm_l16 = t133;
                        {
                            void * t136 = gm_l0;
                            void * t137 = GM_LD(void *, (((uint8_t *)t136) + 8));
                            gm_b16_8 t138 = GM_LD(gm_b16_8, ((((uint8_t *)t137) + 1088) + 0));
                            gm_b16_8 t139 = t138;
                            uint32_t t140 = gm_l16;
                            uint64_t t141 = (uint64_t)t140;
                            uint64_t t142 = gm_f_us_cuv01fp(t141);
                            uint64_t t143 = (uintptr_t)t142;
                            gm_b16_8 t144 = t139;
                            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&t144) + 8);
                            if (t143 >= (uintptr_t)(t145)) gm_trap_index(t143, t145, gm_src8sgunk80orlb, 159, 32);
                            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t144)) + t143 * 184) + 24) + 0));
                            if (t146 != ((uint8_t)0ULL)) goto ie135;
                            gm_tpautr4br7h3l t147 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t144)) + t143 * 184) + 24) + 8));
                            F.l17 = t147;
                            {
                                uint64_t t149 = gm_l8;
                                void * t150 = gm_l0;
                                gm_t88rcucg1b7uk t151 = gm_l9;
                                gm_b16_8 t152 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t150, t151);
                                if (gm_unw.kind) {
                                    goto gm_ll61;
                                }
                                gm_b16_8 t153 = t152;
                                uint64_t t154 = gm_l12;
                                uint64_t t155 = gm_f_us_cuv01fp(t154);
                                uint64_t t156 = (uintptr_t)t155;
                                gm_b16_8 t157 = t153;
                                uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&t157) + 8);
                                if (t156 >= (uintptr_t)(t158)) gm_trap_index(t156, t158, gm_src8sgunk80orlb, 160, 27);
                                uint64_t t159 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t157)) + t156 * 8));
                                uint64_t r160; bool o161 = __builtin_add_overflow(t149, t159, &r160);
                                if (o161) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 160, 17);
                                uint64_t t162 = r160;
                                gm_l8 = t162;
                            }
                            {
                                gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                                gm_b16_8 t164 = t163;
                                uint64_t t165 = gm_l12;
                                uint64_t t166 = gm_f_us_cuv01fp(t165);
                                uint64_t t167 = (uintptr_t)t166;
                                gm_b16_8 t168 = t164;
                                uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&t168) + 8);
                                if (t167 >= (uintptr_t)(t169)) gm_trap_index(t167, t169, gm_src8sgunk80orlb, 161, 23);
                                gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t168)) + t167 * 40) + 16));
                                gm_l9 = t170;
                            }
                            goto ix135;
ie135:;
                            {
                                void * t172 = gm_l0;
                                gm_tl874t2atnlh3 t173 = gm_l1;
                                gm_b16_8 t174;
                                GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(38));
                                gm_t76123gfet73g t175;
                                gm_f_unsupported_70vvk1e((void *)&t175, (void *)NULL, cx, NULL, t172, t173, t174);
                                if (gm_unw.kind) {
                                    goto gm_ll61;
                                }
                                gm_t833u93mr4iob t176;
                                GM_ST(uint8_t, (uint8_t *)&t176 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t176 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t176 + 4, 0);
                                ((uint8_t *)&t176)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t176 + 8), t175);
                                gm_t833u93mr4iob t177 = t176;
                                uint8_t t178 = ((((uint8_t *)&t177))[0] != 0);
                                F.ret = t176;
                                goto gm_fret;
                            }
ix135:;
                        }
                        goto me76;
mn131:;
ma80:;
                        void * t180 = gm_l0;
                        gm_tl874t2atnlh3 t181 = gm_l1;
                        gm_b16_8 t182;
                        GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sqrl3kgomorsv));
                        GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(38));
                        gm_t76123gfet73g t183;
                        gm_f_unsupported_70vvk1e((void *)&t183, (void *)NULL, cx, NULL, t180, t181, t182);
                        if (gm_unw.kind) {
                            goto gm_ll61;
                        }
                        gm_t833u93mr4iob t184;
                        GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                        ((uint8_t *)&t184)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t184 + 8), t183);
                        gm_t833u93mr4iob t185 = t184;
                        uint8_t t186 = ((((uint8_t *)&t185))[0] != 0);
                        F.ret = t184;
                        goto gm_fret;
                        goto me76;
mn179:;
ma81:;
                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 149, 9);
me76:;
                    }
                    goto me68;
mn69:;
                    gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 148, 5);
me68:;
                }
lc57:;
                {
                    uint64_t t188 = gm_l11;
                    uint64_t r189; bool o190 = __builtin_add_overflow(t188, ((uint64_t)1ULL), &r189);
                    uint64_t t191 = r189;
                    gm_l11 = t191;
                }
            }
gm_ll61:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb57; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc57; }
            }
            goto gm_lad;
lb57:;
        }
    }
    {
        uint64_t t192 = gm_l7;
        uint64_t t193 = gm_l8;
        uint64_t r194; bool o195 = __builtin_add_overflow(t192, t193, &r194);
        if (o195) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 166, 12);
        uint64_t t196 = r194;
        gm_t833u93mr4iob t197;
        GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t197 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t197 + 24, 0);
        ((uint8_t *)&t197)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t197 + 8), t196);
        gm_t833u93mr4iob t198 = t197;
        uint8_t t199 = ((((uint8_t *)&t198))[0] != 0);
        F.ret = t197;
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
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5) {
    struct gm_fr_value_bytes_l7s126n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* off */
    uint64_t gm_l6; /* o */
    unsigned __int128 gm_l7; /* bits */
    double gm_l8; /* x */
    uint8_t gm_l9; /* b */
    void * gm_l10; /* x */
    gm_t88rcucg1b7uk gm_l11; /* inner */
    void * gm_l12; /* x */
    void * gm_l13; /* x */
    gm_b16_8 gm_l14; /* offs */
    uint32_t gm_l15; /* index */
    void * gm_l16; /* payload */
    gm_tcmkompbl09il gm_l17; /* u */
    uint64_t gm_l19; /* n */
    void * gm_l20; /* x */
    gm_t88rcucg1b7uk gm_l21; /* pt */
    void * gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    uint64_t gm_l24; /* esz */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    gm_b16_8 gm_l29; /* elems */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* k */
    uint32_t gm_l35; /* id */
    gm_t88rcucg1b7uk gm_l37; /* u */
    unsigned __int128 gm_l38; /* raw */
    gm_b16_8 gm_l39; /* offs */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_tkaapcii86udk gm_l44; /* pl */
    gm_t88rcucg1b7uk gm_l45; /* p */
    gm_t88rcucg1b7uk gm_l46; /* p */
    gm_t88rcucg1b7uk gm_l47; /* inner */
    gm_t88rcucg1b7uk gm_l48; /* p */
    gm_t88rcucg1b7uk gm_l49; /* p */
    gm_t88rcucg1b7uk gm_l50; /* pointee */
    gm_tkaapcii86udk gm_l51; /* start */
    uint64_t gm_l52; /* len */
    gm_t88rcucg1b7uk gm_l53; /* elem */
    uint64_t gm_l54; /* p */
    uint64_t gm_l55; /* addr */
    uint32_t gm_l56; /* f */
    GM_STACK_CHECK(&F, 7190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            uint64_t t3 = gm_l5;
            uint64_t t4 = gm_f_us_cuv01fp(t3);
            gm_l6 = t4;
        }
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            switch (t20) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma19; }
ma6:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t22 != ((uint8_t)0ULL)) goto mn21;
            unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
            gm_l7 = t23;
            void * t24 = gm_l0;
            gm_b16_8 t25 = gm_l4;
            gm_b16_8 t26 = t25;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&t26) + 8);
            void * t28 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t26)));
            uint64_t t29 = gm_l6;
            uint64_t t30 = (uintptr_t)t29;
            uint64_t t31 = gm_l6;
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l3;
            uint64_t t34 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t35 = gm_f_us_cuv01fp(t34);
            uint64_t r36; bool o37 = __builtin_add_overflow(t31, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 62, 41);
            uint64_t t38 = r36;
            uint64_t t39 = (uintptr_t)t38;
            if (t30 > t39 || t39 > (uintptr_t)t27) gm_trap_slice(t30, t39, t27, gm_src8sgunk80orlb, 62, 34);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t28 + t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39 - t30));
            unsigned __int128 t41 = gm_l7;
            gm_f_put_uint_cddin99(t24, t40, t41);
            goto me5;
mn21:;
ma7:;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t43 != ((uint8_t)1ULL)) goto mn42;
            double t44 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
            gm_l8 = t44;
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l3;
            double t47 = gm_l8;
            gm_b16_8 t48 = gm_l4;
            gm_b16_8 t49 = t48;
            uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&t49) + 8);
            void * t51 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t49)));
            uint64_t t52 = gm_l6;
            uint64_t t53 = (uintptr_t)t52;
            uint64_t t54 = gm_l6;
            void * t55 = gm_l0;
            gm_t88rcucg1b7uk t56 = gm_l3;
            uint64_t t57 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t58 = gm_f_us_cuv01fp(t57);
            uint64_t r59; bool o60 = __builtin_add_overflow(t54, t58, &r59);
            if (o60) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 63, 47);
            uint64_t t61 = r59;
            uint64_t t62 = (uintptr_t)t61;
            if (t53 > t62 || t62 > (uintptr_t)t50) gm_trap_slice(t53, t62, t50, gm_src8sgunk80orlb, 63, 40);
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t51 + t53 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(t62 - t53));
            gm_f_put_float_fiad72d(t45, GM_CONV(gm_csguankko4lmhd, t46), t47, t63);
            goto me5;
mn42:;
ma8:;
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t65 != ((uint8_t)2ULL)) goto mn64;
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
            gm_l9 = t66;
            {
                uint64_t t68 = gm_l6;
                uint64_t t69 = (uintptr_t)t68;
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_src8sgunk80orlb, 64, 23);
                uint8_t *t71 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t69 * 1);
                uint8_t t72;
                uint8_t t74 = gm_l9;
                if (!t74) goto ie73;
                uint8_t t76 = ((uint8_t)1ULL);
                t72 = t76;
                goto ix73;
ie73:;
                uint8_t t78 = ((uint8_t)0ULL);
                t72 = t78;
ix73:;
                GM_ST(uint8_t, t71, t72);
            }
            goto me5;
mn64:;
ma9:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t80 != ((uint8_t)6ULL)) goto mn79;
            goto me5;
mn79:;
ma10:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t83 != ((uint8_t)7ULL)) goto mn82;
            void * t84 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l10 = t84;
            {
                void * t86 = gm_l0;
                gm_t88rcucg1b7uk t87 = gm_l3;
                gm_t88rcucg1b7uk t88 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t86, GM_CONV(gm_csguankko4lmhd, t87)));
                gm_l11 = t88;
            }
            {
                void * t89 = gm_l0;
                gm_tl874t2atnlh3 t90 = gm_l1;
                void * t91 = gm_l10;
                gm_t88rcucg1b7uk t92 = gm_l11;
                gm_b16_8 t93 = gm_l4;
                uint64_t t94 = gm_l5;
                gm_tcfa6l7reoosn t95;
                gm_f_value_bytes_l7s126n((void *)&t95, (void *)NULL, cx, NULL, t89, t90, &(*(gm_t6evimt84lndq *)((uint8_t *)t91)), t92, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t96 = t95;
                if (((((uint8_t *)&t96))[0] != 0)) {
                    gm_t76123gfet73g t97 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t96) + 8));
                    gm_tcfa6l7reoosn t98;
                    GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                    ((uint8_t *)&t98)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t98 + 8), t97);
                    F.ret = t98;
                    goto gm_fret;
                }
                gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 8));
            }
            {
                void * t101 = gm_l0;
                void * t102 = GM_LD(void *, (((uint8_t *)t101) + 8));
                gm_t88rcucg1b7uk t103 = gm_l11;
                uint8_t t104 = gm_f_nullable_pointer_7qfmn6o(t102, GM_CONV(gm_csguankko4lmhd, t103));
                uint8_t t105 = (uint8_t)!t104;
                if (!t105) goto ie100;
                {
                    gm_b16_8 t107 = gm_l4;
                    gm_b16_8 t108 = t107;
                    uint64_t t109 = gm_l5;
                    uint64_t t110 = (uintptr_t)((uint64_t)1ULL);
                    void * t111 = gm_l0;
                    gm_t88rcucg1b7uk t112 = gm_l3;
                    gm_b16_8 t113 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t111, t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t114 = t113;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&t114) + 8);
                    if (t110 >= (uintptr_t)(t115)) gm_trap_index(t110, t115, gm_src8sgunk80orlb, 69, 69);
                    uint64_t t116 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t114)) + t110 * 8));
                    uint64_t r117; bool o118 = __builtin_add_overflow(t109, t116, &r117);
                    if (o118) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 69, 63);
                    uint64_t t119 = r117;
                    uint64_t t120 = gm_f_us_cuv01fp(t119);
                    uint64_t t121 = (uintptr_t)t120;
                    gm_b16_8 t122 = t108;
                    uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&t122) + 8);
                    if (t121 >= (uintptr_t)(t123)) gm_trap_index(t121, t123, gm_src8sgunk80orlb, 69, 56);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t122)) + t121 * 1), ((uint8_t)1ULL));
                }
                goto ix100;
ie100:;
ix100:;
            }
            goto me5;
mn82:;
ma11:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t125 != ((uint8_t)8ULL)) goto mn124;
            void * t126 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l12 = t126;
            void * t127 = gm_l0;
            gm_tl874t2atnlh3 t128 = gm_l1;
            void * t129 = gm_l12;
            gm_t6evimt84lndq t130 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t129));
            void * t131 = gm_l0;
            gm_t88rcucg1b7uk t132 = gm_l3;
            gm_tgglga8ro3isc t133 = gm_f_res_types_pg7rvck(t131, GM_CONV(gm_csguankko4lmhd, t132));
            gm_tgglga8ro3isc t134 = t133;
            gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t134) + 4));
            gm_b16_8 t136 = gm_l4;
            uint64_t t137 = gm_l5;
            uint64_t t138 = (uintptr_t)((uint64_t)1ULL);
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = gm_l3;
            gm_b16_8 t141 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t139, t140);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t142 = t141;
            uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&t142) + 8);
            if (t138 >= (uintptr_t)(t143)) gm_trap_index(t138, t143, gm_src8sgunk80orlb, 71, 77);
            uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t142)) + t138 * 8));
            uint64_t r145; bool o146 = __builtin_add_overflow(t137, t144, &r145);
            if (o146) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 71, 71);
            uint64_t t147 = r145;
            gm_tcfa6l7reoosn t148;
            gm_f_value_bytes_l7s126n((void *)&t148, (void *)NULL, cx, NULL, t127, t128, &t130, t135, t136, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t149 = t148;
            if (((((uint8_t *)&t149))[0] != 0)) {
                gm_t76123gfet73g t150 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t149) + 8));
                gm_tcfa6l7reoosn t151;
                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                ((uint8_t *)&t151)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), t150);
                F.ret = t151;
                goto gm_fret;
            }
            gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 8));
            goto me5;
mn124:;
ma12:;
            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t154 != ((uint8_t)9ULL)) goto mn153;
            void * t155 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l13 = t155;
            {
                void * t157 = gm_l0;
                gm_t88rcucg1b7uk t158 = gm_l3;
                gm_b16_8 t159 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t157, t158);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t159;
            }
            {
                gm_b16_8 t160 = gm_l4;
                gm_b16_8 t161 = t160;
                uint64_t t162 = gm_l5;
                uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_src8sgunk80orlb, 74, 26);
                uint64_t t165 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t163 * 8));
                uint64_t r166; bool o167 = __builtin_add_overflow(t162, t165, &r166);
                if (o167) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 74, 20);
                uint64_t t168 = r166;
                uint64_t t169 = gm_f_us_cuv01fp(t168);
                uint64_t t170 = (uintptr_t)t169;
                gm_b16_8 t171 = t161;
                uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&t171) + 8);
                if (t170 >= (uintptr_t)(t172)) gm_trap_index(t170, t172, gm_src8sgunk80orlb, 74, 13);
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t171)) + t170 * 1), ((uint8_t)1ULL));
            }
            {
                void * t173 = gm_l0;
                gm_tl874t2atnlh3 t174 = gm_l1;
                void * t175 = gm_l13;
                gm_t6evimt84lndq t176 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t175));
                void * t177 = gm_l0;
                gm_t88rcucg1b7uk t178 = gm_l3;
                gm_tgglga8ro3isc t179 = gm_f_res_types_pg7rvck(t177, GM_CONV(gm_csguankko4lmhd, t178));
                gm_tgglga8ro3isc t180 = t179;
                gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t180) + 0));
                gm_b16_8 t182 = gm_l4;
                uint64_t t183 = gm_l5;
                uint64_t t184 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                if (t184 >= (uintptr_t)(t185)) gm_trap_index(t184, t185, gm_src8sgunk80orlb, 75, 72);
                uint64_t t186 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t184 * 8));
                uint64_t r187; bool o188 = __builtin_add_overflow(t183, t186, &r187);
                if (o188) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 75, 66);
                uint64_t t189 = r187;
                gm_tcfa6l7reoosn t190;
                gm_f_value_bytes_l7s126n((void *)&t190, (void *)NULL, cx, NULL, t173, t174, &t176, t181, t182, t189);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t191 = t190;
                if (((((uint8_t *)&t191))[0] != 0)) {
                    gm_t76123gfet73g t192 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t191) + 8));
                    gm_tcfa6l7reoosn t193;
                    GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                    ((uint8_t *)&t193)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t193 + 8), t192);
                    F.ret = t193;
                    goto gm_fret;
                }
                gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t191) + 8));
            }
            goto me5;
mn153:;
ma13:;
            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t196 != ((uint8_t)10ULL)) goto mn195;
            uint32_t t197 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l15 = t197;
            void * t198 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
            gm_l16 = t198;
            {
                void * t200 = gm_l0;
                gm_t88rcucg1b7uk t201 = gm_l3;
                gm_t57euh612ligr t202;
                gm_f_union_def_h8mvf4t((void *)&t202, t200, GM_CONV(gm_csguankko4lmhd, t201));
                gm_t57euh612ligr t203 = t202;
                gm_tcmkompbl09il t204;
                if (((((uint8_t *)&t203))[24] != 0)) {
                    gm_tcmkompbl09il t205 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t203) + 0));
                    t204 = t205;
                } else {
                    void * t206 = gm_l0;
                    gm_tl874t2atnlh3 t207 = gm_l1;
                    gm_b16_8 t208;
                    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sa6mes8ijqmmk));
                    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(29));
                    gm_t76123gfet73g t209;
                    gm_f_unsupported_70vvk1e((void *)&t209, (void *)NULL, cx, NULL, t206, t207, t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                    ((uint8_t *)&t210)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t210 + 8), t209);
                    gm_tcfa6l7reoosn t211 = t210;
                    uint8_t t212 = ((((uint8_t *)&t211))[0] != 0);
                    F.ret = t210;
                    goto gm_fret;
                }
                gm_l17 = t204;
            }
            {
                gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                gm_b16_8 t214 = t213;
                uint32_t t215 = gm_l15;
                uint64_t t216 = (uint64_t)t215;
                uint64_t t217 = gm_f_us_cuv01fp(t216);
                uint64_t t218 = (uintptr_t)t217;
                gm_b16_8 t219 = t214;
                uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&t219) + 8);
                if (t218 >= (uintptr_t)(t220)) gm_trap_index(t218, t220, gm_src8sgunk80orlb, 79, 19);
                gm_tckdobunaulee t221 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t219)) + t218 * 72));
                F.l18 = t221;
            }
            {
                void * t222 = gm_l0;
                gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
                uint64_t t224 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t222, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t225 = gm_f_us_cuv01fp(t224);
                gm_l19 = t225;
            }
            {
                void * t226 = gm_l0;
                uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t228 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t229 = gm_l6;
                uint64_t t230 = (uintptr_t)t229;
                uint64_t t231 = gm_l6;
                uint64_t t232 = gm_l19;
                uint64_t r233; bool o234 = __builtin_add_overflow(t231, t232, &r233);
                if (o234) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 81, 31);
                uint64_t t235 = r233;
                uint64_t t236 = (uintptr_t)t235;
                if (t230 > t236 || t236 > (uintptr_t)t227) gm_trap_slice(t230, t236, t227, gm_src8sgunk80orlb, 81, 24);
                gm_b16_8 t237;
                GM_ST(void *, (uint8_t *)&t237, (void *)(((uint8_t *)t228 + t230 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(t236 - t230));
                unsigned __int128 t238 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l18) + 24)));
                gm_f_put_uint_cddin99(t226, t237, t238);
            }
            {
                void * t240 = gm_l16;
                void * t241 = t240;
                if (!(GM_LD(void *, ((uint8_t *)&t241)) != 0)) goto ie239;
                void * t242 = GM_LD(void *, ((uint8_t *)&t241));
                gm_l20 = t242;
                {
                    gm_tkn42u7na3t81 t244 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 16));
                    gm_tkn42u7na3t81 t245 = t244;
                    gm_t88rcucg1b7uk t246;
                    if (((((uint8_t *)&t245))[4] != 0)) {
                        gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t245) + 0));
                        t246 = t247;
                    } else {
                        gm_b0_1 t248;
                        gm_tcfa6l7reoosn t249;
                        GM_ST(uint8_t, (uint8_t *)&t249 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t249 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t249 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t249 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t249 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t249 + 24, 0);
                        ((uint8_t *)&t249)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t249 + 8), t248);
                        gm_tcfa6l7reoosn t250 = t249;
                        uint8_t t251 = ((((uint8_t *)&t250))[0] != 0);
                        F.ret = t249;
                        goto gm_fret;
                    }
                    gm_l21 = t246;
                }
                {
                    void * t252 = gm_l0;
                    gm_tl874t2atnlh3 t253 = gm_l1;
                    void * t254 = gm_l20;
                    gm_t6evimt84lndq t255 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t254));
                    gm_t88rcucg1b7uk t256 = gm_l21;
                    gm_b16_8 t257 = gm_l4;
                    uint64_t t258 = gm_l5;
                    void * t259 = gm_l0;
                    gm_t88rcucg1b7uk t260 = gm_l3;
                    uint64_t t261 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t259, t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t r262; bool o263 = __builtin_add_overflow(t258, t261, &r262);
                    if (o263) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 84, 54);
                    uint64_t t264 = r262;
                    gm_tcfa6l7reoosn t265;
                    gm_f_value_bytes_l7s126n((void *)&t265, (void *)NULL, cx, NULL, t252, t253, &t255, t256, t257, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t266 = t265;
                    if (((((uint8_t *)&t266))[0] != 0)) {
                        gm_t76123gfet73g t267 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t266) + 8));
                        gm_tcfa6l7reoosn t268;
                        GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                        ((uint8_t *)&t268)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t268 + 8), t267);
                        F.ret = t268;
                        goto gm_fret;
                    }
                    gm_b0_1 t269 = GM_LD(gm_b0_1, (((uint8_t *)&t266) + 8));
                }
                goto ix239;
ie239:;
ix239:;
            }
            goto me5;
mn195:;
ma14:;
            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t271 != ((uint8_t)3ULL)) goto mn270;
            void * t272 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l22 = t272;
            void * t273 = gm_l0;
            gm_t88rcucg1b7uk t274 = gm_l3;
            gm_t53at80pfdus3 t275;
            gm_f_kind_htv6093((void *)&t275, t273, GM_CONV(gm_csguankko4lmhd, t274));
            gm_t53at80pfdus3 t276 = t275;
            uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&t276) + 0));
            switch (t283) { case ((uint8_t)0ULL): goto ma281; case ((uint8_t)1ULL): goto ma281; case ((uint8_t)2ULL): goto ma281; case ((uint8_t)3ULL): goto ma281; case ((uint8_t)4ULL): goto ma281; case ((uint8_t)5ULL): goto ma281; case ((uint8_t)6ULL): goto ma281; case ((uint8_t)7ULL): goto ma281; case ((uint8_t)8ULL): goto ma281; case ((uint8_t)9ULL): goto ma281; case ((uint8_t)10ULL): goto ma281; case ((uint8_t)11ULL): goto ma278; case ((uint8_t)12ULL): goto ma279; case ((uint8_t)13ULL): goto ma281; case ((uint8_t)14ULL): goto ma281; case ((uint8_t)15ULL): goto ma281; case ((uint8_t)16ULL): goto ma281; case ((uint8_t)17ULL): goto ma280; case ((uint8_t)18ULL): goto ma281; case ((uint8_t)19ULL): goto ma281; case ((uint8_t)20ULL): goto ma281; case ((uint8_t)21ULL): goto ma281; case ((uint8_t)22ULL): goto ma281; case ((uint8_t)23ULL): goto ma281; case ((uint8_t)24ULL): goto ma281; case ((uint8_t)25ULL): goto ma281; case ((uint8_t)26ULL): goto ma281; default: goto ma281; }
ma278:;
            uint8_t t285 = GM_LD(uint8_t, (((uint8_t *)&t276) + 0));
            if (t285 != ((uint8_t)11ULL)) goto mn284;
            gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t276) + 8) + 8));
            gm_l23 = t286;
            {
                void * t288 = gm_l0;
                gm_t88rcucg1b7uk t289 = gm_l23;
                uint64_t t290 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t288, t289);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t290;
            }
            {
                {
                    void * t292 = gm_l22;
                    gm_b16_8 t293 = GM_LD(gm_b16_8, (((uint8_t *)t292) + 0));
                    gm_b16_8 t294 = t293;
                    gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                    gm_l25 = t295;
                }
                {
                    gm_l26 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t297 = gm_l26;
                        uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint8_t t299 = (t297 < t298);
                        if (!t299) goto lb296;
                        {
                            gm_thf8as07kj3uj t302;
                            memset((uint8_t *)&t302 + 8, 0, 8);
                            uint64_t t303 = gm_l26;
                            GM_ST(uint64_t, (uint8_t *)&t302 + 0, t303);
                            uint64_t t304 = gm_l26;
                            uint64_t t305 = (uintptr_t)t304;
                            uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                            memcpy((uint8_t *)&t302 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t305 * 48), 48);
                            gm_thf8as07kj3uj t307 = t302;
                            uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&t307) + 0));
                            gm_l27 = t310;
                            gm_t6evimt84lndq t311 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t307) + 16));
                            F.l28 = t311;
                            {
                                void * t313 = gm_l0;
                                gm_tl874t2atnlh3 t314 = gm_l1;
                                gm_t6evimt84lndq t315 = F.l28;
                                gm_t88rcucg1b7uk t316 = gm_l23;
                                gm_b16_8 t317 = gm_l4;
                                uint64_t t318 = gm_l5;
                                uint64_t t319 = gm_l27;
                                uint64_t t320 = (uint64_t)t319;
                                uint64_t t321 = gm_l24;
                                uint64_t r322; bool o323 = __builtin_mul_overflow(t320, t321, &r322);
                                if (o323) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 90, 94);
                                uint64_t t324 = r322;
                                uint64_t r325; bool o326 = __builtin_add_overflow(t318, t324, &r325);
                                if (o326) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 90, 88);
                                uint64_t t327 = r325;
                                gm_tcfa6l7reoosn t328;
                                gm_f_value_bytes_l7s126n((void *)&t328, (void *)NULL, cx, NULL, t313, t314, &t315, t316, t317, t327);
                                if (gm_unw.kind) {
                                    goto gm_ll300;
                                }
                                gm_tcfa6l7reoosn t329 = t328;
                                if (((((uint8_t *)&t329))[0] != 0)) {
                                    gm_t76123gfet73g t330 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t329) + 8));
                                    gm_tcfa6l7reoosn t331;
                                    GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                                    ((uint8_t *)&t331)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t331 + 8), t330);
                                    F.ret = t331;
                                    goto gm_fret;
                                }
                                gm_b0_1 t332 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 8));
                            }
                            goto me308;
mn309:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 90, 17);
me308:;
                        }
lc296:;
                        {
                            uint64_t t334 = gm_l26;
                            uint64_t r335; bool o336 = __builtin_add_overflow(t334, ((uint64_t)1ULL), &r335);
                            uint64_t t337 = r335;
                            gm_l26 = t337;
                        }
                    }
gm_ll300:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb296; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc296; }
                    }
                    goto gm_lad;
lb296:;
                }
            }
            goto me277;
mn284:;
ma279:;
            uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&t276) + 0));
            if (t339 != ((uint8_t)12ULL)) goto mn338;
            gm_b16_8 t340 = GM_LD(gm_b16_8, ((((uint8_t *)&t276) + 8) + 0));
            gm_l29 = t340;
            {
                void * t342 = gm_l0;
                gm_t88rcucg1b7uk t343 = gm_l3;
                gm_b16_8 t344 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t342, t343);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l30 = t344;
            }
            {
                {
                    void * t346 = gm_l22;
                    gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)t346) + 0));
                    gm_b16_8 t348 = t347;
                    gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                    gm_l31 = t349;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t351 = gm_l32;
                        uint64_t t352 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t353 = (t351 < t352);
                        if (!t353) goto lb350;
                        {
                            gm_thf8as07kj3uj t356;
                            memset((uint8_t *)&t356 + 8, 0, 8);
                            uint64_t t357 = gm_l32;
                            GM_ST(uint64_t, (uint8_t *)&t356 + 0, t357);
                            uint64_t t358 = gm_l32;
                            uint64_t t359 = (uintptr_t)t358;
                            uint64_t t360 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            memcpy((uint8_t *)&t356 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t359 * 48), 48);
                            gm_thf8as07kj3uj t361 = t356;
                            uint64_t t364 = GM_LD(uint64_t, (((uint8_t *)&t361) + 0));
                            gm_l33 = t364;
                            gm_t6evimt84lndq t365 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t361) + 16));
                            F.l34 = t365;
                            {
                                void * t367 = gm_l0;
                                gm_tl874t2atnlh3 t368 = gm_l1;
                                uint64_t t369 = gm_l33;
                                uint64_t t370 = (uintptr_t)t369;
                                uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                if (t370 >= (uintptr_t)(t371)) gm_trap_index(t370, t371, gm_src8sgunk80orlb, 94, 77);
                                gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t370 * 4));
                                gm_b16_8 t373 = gm_l4;
                                uint64_t t374 = gm_l5;
                                uint64_t t375 = gm_l33;
                                uint64_t t376 = (uintptr_t)t375;
                                uint64_t t377 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                                if (t376 >= (uintptr_t)(t377)) gm_trap_index(t376, t377, gm_src8sgunk80orlb, 94, 98);
                                uint64_t t378 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t376 * 8));
                                uint64_t r379; bool o380 = __builtin_add_overflow(t374, t378, &r379);
                                if (o380) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 94, 92);
                                uint64_t t381 = r379;
                                gm_tcfa6l7reoosn t382;
                                gm_f_value_bytes_l7s126n((void *)&t382, (void *)NULL, cx, NULL, t367, t368, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l34)), t372, t373, t381);
                                if (gm_unw.kind) {
                                    goto gm_ll354;
                                }
                                gm_tcfa6l7reoosn t383 = t382;
                                if (((((uint8_t *)&t383))[0] != 0)) {
                                    gm_t76123gfet73g t384 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t383) + 8));
                                    gm_tcfa6l7reoosn t385;
                                    GM_ST(uint8_t, (uint8_t *)&t385 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t385 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t385 + 4, 0);
                                    ((uint8_t *)&t385)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t385 + 8), t384);
                                    F.ret = t385;
                                    goto gm_fret;
                                }
                                gm_b0_1 t386 = GM_LD(gm_b0_1, (((uint8_t *)&t383) + 8));
                            }
                            goto me362;
mn363:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 94, 17);
me362:;
                        }
lc350:;
                        {
                            uint64_t t388 = gm_l32;
                            uint64_t r389; bool o390 = __builtin_add_overflow(t388, ((uint64_t)1ULL), &r389);
                            uint64_t t391 = r389;
                            gm_l32 = t391;
                        }
                    }
gm_ll354:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb350; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc350; }
                    }
                    goto gm_lad;
lb350:;
                }
            }
            goto me277;
mn338:;
ma280:;
            uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&t276) + 0));
            if (t393 != ((uint8_t)17ULL)) goto mn392;
            uint32_t t394 = GM_LD(uint32_t, ((((uint8_t *)&t276) + 8) + 0));
            gm_l35 = t394;
            void * t395 = gm_l0;
            void * t396 = GM_LD(void *, (((uint8_t *)t395) + 8));
            gm_b16_8 t397 = GM_LD(gm_b16_8, ((((uint8_t *)t396) + 1088) + 0));
            gm_b16_8 t398 = t397;
            uint32_t t399 = gm_l35;
            uint64_t t400 = (uint64_t)t399;
            uint64_t t401 = gm_f_us_cuv01fp(t400);
            uint64_t t402 = (uintptr_t)t401;
            gm_b16_8 t403 = t398;
            uint64_t t404 = GM_LD(uintptr_t, ((uint8_t *)&t403) + 8);
            if (t402 >= (uintptr_t)(t404)) gm_trap_index(t402, t404, gm_src8sgunk80orlb, 96, 34);
            uint8_t t407 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t403)) + t402 * 184) + 24) + 0));
            if (t407 != ((uint8_t)0ULL)) goto mn406;
            gm_tpautr4br7h3l t408 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&t403)) + t402 * 184) + 24) + 8));
            F.l36 = t408;
            {
                if (!(((((uint8_t *)&F.l36) + 40))[4] != 0)) goto ie410;
                gm_t88rcucg1b7uk t411 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l36) + 40));
                gm_l37 = t411;
                {
                    void * t413 = gm_l0;
                    gm_tl874t2atnlh3 t414 = gm_l1;
                    gm_t88rcucg1b7uk t415 = gm_l3;
                    gm_tdlro53ljvkl0 t416;
                    gm_f_const_bits_1of8c6t((void *)&t416, (void *)NULL, cx, NULL, t413, t414, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l2)), t415);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tdlro53ljvkl0 t417 = t416;
                    if (((((uint8_t *)&t417))[0] != 0)) {
                        gm_t76123gfet73g t418 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t417) + 16));
                        gm_tcfa6l7reoosn t419;
                        GM_ST(uint8_t, (uint8_t *)&t419 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t419 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t419 + 4, 0);
                        ((uint8_t *)&t419)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t419 + 8), t418);
                        F.ret = t419;
                        goto gm_fret;
                    }
                    unsigned __int128 t420 = GM_LD(unsigned __int128, (((uint8_t *)&t417) + 16));
                    gm_l38 = t420;
                }
                {
                    void * t421 = gm_l0;
                    gm_b16_8 t422 = gm_l4;
                    gm_b16_8 t423 = t422;
                    uint64_t t424 = GM_LD(uintptr_t, ((uint8_t *)&t423) + 8);
                    void * t425 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t423)));
                    uint64_t t426 = gm_l6;
                    uint64_t t427 = (uintptr_t)t426;
                    uint64_t t428 = gm_l6;
                    void * t429 = gm_l0;
                    gm_t88rcucg1b7uk t430 = gm_l37;
                    uint64_t t431 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t429, t430);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t432 = gm_f_us_cuv01fp(t431);
                    uint64_t r433; bool o434 = __builtin_add_overflow(t428, t432, &r433);
                    if (o434) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 100, 43);
                    uint64_t t435 = r433;
                    uint64_t t436 = (uintptr_t)t435;
                    if (t427 > t436 || t436 > (uintptr_t)t424) gm_trap_slice(t427, t436, t424, gm_src8sgunk80orlb, 100, 36);
                    gm_b16_8 t437;
                    GM_ST(void *, (uint8_t *)&t437, (void *)(((uint8_t *)t425 + t427 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(t436 - t427));
                    unsigned __int128 t438 = gm_l38;
                    gm_f_put_uint_cddin99(t421, t437, t438);
                }
                {
                    gm_b0_1 t439;
                    gm_tcfa6l7reoosn t440;
                    GM_ST(uint8_t, (uint8_t *)&t440 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t440 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t440 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t440 + 24, 0);
                    ((uint8_t *)&t440)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t440 + 8), t439);
                    gm_tcfa6l7reoosn t441 = t440;
                    uint8_t t442 = ((((uint8_t *)&t441))[0] != 0);
                    F.ret = t440;
                    goto gm_fret;
                }
                goto ix410;
ie410:;
ix410:;
            }
            {
                void * t443 = gm_l0;
                gm_t88rcucg1b7uk t444 = gm_l3;
                gm_b16_8 t445 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t443, t444);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l39 = t445;
            }
            {
                {
                    void * t447 = gm_l22;
                    gm_b16_8 t448 = GM_LD(gm_b16_8, (((uint8_t *)t447) + 0));
                    gm_b16_8 t449 = t448;
                    gm_b16_8 t450 = GM_LD(gm_b16_8, ((uint8_t *)&t449));
                    gm_l40 = t450;
                }
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t452 = gm_l41;
                        uint64_t t453 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        uint8_t t454 = (t452 < t453);
                        if (!t454) goto lb451;
                        {
                            gm_thf8as07kj3uj t457;
                            memset((uint8_t *)&t457 + 8, 0, 8);
                            uint64_t t458 = gm_l41;
                            GM_ST(uint64_t, (uint8_t *)&t457 + 0, t458);
                            uint64_t t459 = gm_l41;
                            uint64_t t460 = (uintptr_t)t459;
                            uint64_t t461 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                            memcpy((uint8_t *)&t457 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t460 * 48), 48);
                            gm_thf8as07kj3uj t462 = t457;
                            uint64_t t465 = GM_LD(uint64_t, (((uint8_t *)&t462) + 0));
                            gm_l42 = t465;
                            gm_t6evimt84lndq t466 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t462) + 16));
                            F.l43 = t466;
                            {
                                void * t468 = gm_l0;
                                gm_tl874t2atnlh3 t469 = gm_l1;
                                uint64_t t470 = gm_l42;
                                uint64_t t471 = (uintptr_t)t470;
                                uint64_t t472 = GM_LD(uintptr_t, (((uint8_t *)&F.l36) + 0) + 8);
                                if (t471 >= (uintptr_t)(t472)) gm_trap_index(t471, t472, gm_src8sgunk80orlb, 104, 81);
                                gm_t88rcucg1b7uk t473 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l36) + 0)) + t471 * 40) + 16));
                                gm_b16_8 t474 = gm_l4;
                                uint64_t t475 = gm_l5;
                                uint64_t t476 = gm_l42;
                                uint64_t t477 = (uintptr_t)t476;
                                uint64_t t478 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                                if (t477 >= (uintptr_t)(t478)) gm_trap_index(t477, t478, gm_src8sgunk80orlb, 104, 109);
                                uint64_t t479 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t477 * 8));
                                uint64_t r480; bool o481 = __builtin_add_overflow(t475, t479, &r480);
                                if (o481) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 104, 103);
                                uint64_t t482 = r480;
                                gm_tcfa6l7reoosn t483;
                                gm_f_value_bytes_l7s126n((void *)&t483, (void *)NULL, cx, NULL, t468, t469, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l43)), t473, t474, t482);
                                if (gm_unw.kind) {
                                    goto gm_ll455;
                                }
                                gm_tcfa6l7reoosn t484 = t483;
                                if (((((uint8_t *)&t484))[0] != 0)) {
                                    gm_t76123gfet73g t485 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t484) + 8));
                                    gm_tcfa6l7reoosn t486;
                                    GM_ST(uint8_t, (uint8_t *)&t486 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t486 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t486 + 4, 0);
                                    ((uint8_t *)&t486)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t486 + 8), t485);
                                    F.ret = t486;
                                    goto gm_fret;
                                }
                                gm_b0_1 t487 = GM_LD(gm_b0_1, (((uint8_t *)&t484) + 8));
                            }
                            goto me463;
mn464:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 104, 21);
me463:;
                        }
lc451:;
                        {
                            uint64_t t489 = gm_l41;
                            uint64_t r490; bool o491 = __builtin_add_overflow(t489, ((uint64_t)1ULL), &r490);
                            uint64_t t492 = r490;
                            gm_l41 = t492;
                        }
                    }
gm_ll455:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb451; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc451; }
                    }
                    goto gm_lad;
lb451:;
                }
            }
            goto me405;
mn406:;
            void * t494 = gm_l0;
            gm_tl874t2atnlh3 t495 = gm_l1;
            gm_b16_8 t496;
            GM_ST(void *, (uint8_t *)&t496, (void *)(gm_s67fd8s6hf84e));
            GM_ST(uintptr_t, (uint8_t *)&t496 + 8, (uintptr_t)(23));
            gm_t76123gfet73g t497;
            gm_f_unsupported_70vvk1e((void *)&t497, (void *)NULL, cx, NULL, t494, t495, t496);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t498;
            GM_ST(uint8_t, (uint8_t *)&t498 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t498 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t498 + 4, 0);
            ((uint8_t *)&t498)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t498 + 8), t497);
            gm_tcfa6l7reoosn t499 = t498;
            uint8_t t500 = ((((uint8_t *)&t499))[0] != 0);
            F.ret = t498;
            goto gm_fret;
            goto me405;
mn493:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 96, 28);
me405:;
            goto me277;
mn392:;
ma281:;
            void * t502 = gm_l0;
            gm_tl874t2atnlh3 t503 = gm_l1;
            gm_b16_8 t504;
            GM_ST(void *, (uint8_t *)&t504, (void *)(gm_s67fd8s6hf84e));
            GM_ST(uintptr_t, (uint8_t *)&t504 + 8, (uintptr_t)(23));
            gm_t76123gfet73g t505;
            gm_f_unsupported_70vvk1e((void *)&t505, (void *)NULL, cx, NULL, t502, t503, t504);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t506;
            GM_ST(uint8_t, (uint8_t *)&t506 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t506 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t506 + 4, 0);
            ((uint8_t *)&t506)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t506 + 8), t505);
            gm_tcfa6l7reoosn t507 = t506;
            uint8_t t508 = ((((uint8_t *)&t507))[0] != 0);
            F.ret = t506;
            goto gm_fret;
            goto me277;
mn501:;
ma282:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 87, 20);
me277:;
            goto me5;
mn270:;
ma15:;
            uint8_t t510 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t510 != ((uint8_t)4ULL)) goto mn509;
            gm_tkaapcii86udk t511 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l2) + 16));
            gm_l44 = t511;
            {
                void * t513 = gm_l0;
                gm_t88rcucg1b7uk t514 = gm_l3;
                gm_t53at80pfdus3 t515;
                gm_f_kind_htv6093((void *)&t515, t513, GM_CONV(gm_csguankko4lmhd, t514));
                gm_t53at80pfdus3 t516 = t515;
                gm_t88rcucg1b7uk t517;
                uint8_t t523 = GM_LD(uint8_t, (((uint8_t *)&t516) + 0));
                switch (t523) { case ((uint8_t)0ULL): goto ma521; case ((uint8_t)1ULL): goto ma521; case ((uint8_t)2ULL): goto ma521; case ((uint8_t)3ULL): goto ma521; case ((uint8_t)4ULL): goto ma521; case ((uint8_t)5ULL): goto ma521; case ((uint8_t)6ULL): goto ma521; case ((uint8_t)7ULL): goto ma521; case ((uint8_t)8ULL): goto ma519; case ((uint8_t)9ULL): goto ma519; case ((uint8_t)10ULL): goto ma521; case ((uint8_t)11ULL): goto ma521; case ((uint8_t)12ULL): goto ma521; case ((uint8_t)13ULL): goto ma520; case ((uint8_t)14ULL): goto ma521; case ((uint8_t)15ULL): goto ma521; case ((uint8_t)16ULL): goto ma521; case ((uint8_t)17ULL): goto ma521; case ((uint8_t)18ULL): goto ma521; case ((uint8_t)19ULL): goto ma521; case ((uint8_t)20ULL): goto ma521; case ((uint8_t)21ULL): goto ma521; case ((uint8_t)22ULL): goto ma521; case ((uint8_t)23ULL): goto ma521; case ((uint8_t)24ULL): goto ma521; case ((uint8_t)25ULL): goto ma521; case ((uint8_t)26ULL): goto ma521; default: goto ma521; }
ma519:;
                uint8_t t527 = GM_LD(uint8_t, (((uint8_t *)&t516) + 0));
                if (t527 != ((uint8_t)8ULL)) goto pa526;
                gm_t88rcucg1b7uk t528 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t516) + 8) + 4));
                gm_l45 = t528;
                goto po525;
pa526:;
                uint8_t t530 = GM_LD(uint8_t, (((uint8_t *)&t516) + 0));
                if (t530 != ((uint8_t)9ULL)) goto pa529;
                gm_t88rcucg1b7uk t531 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t516) + 8) + 4));
                gm_l45 = t531;
                goto po525;
pa529:;
                goto mn524;
po525:;
                gm_t88rcucg1b7uk t532 = gm_l45;
                t517 = t532;
                goto me518;
mn524:;
ma520:;
                uint8_t t534 = GM_LD(uint8_t, (((uint8_t *)&t516) + 0));
                if (t534 != ((uint8_t)13ULL)) goto mn533;
                gm_t88rcucg1b7uk t535 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t516) + 8));
                gm_l47 = t535;
                void * t536 = gm_l0;
                gm_t88rcucg1b7uk t537 = gm_l47;
                gm_t53at80pfdus3 t538;
                gm_f_kind_htv6093((void *)&t538, t536, GM_CONV(gm_csguankko4lmhd, t537));
                gm_t53at80pfdus3 t539 = t538;
                gm_t88rcucg1b7uk t540;
                uint8_t t545 = GM_LD(uint8_t, (((uint8_t *)&t539) + 0));
                if (t545 != ((uint8_t)8ULL)) goto pa544;
                gm_t88rcucg1b7uk t546 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t539) + 8) + 4));
                gm_l48 = t546;
                goto po543;
pa544:;
                uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&t539) + 0));
                if (t548 != ((uint8_t)9ULL)) goto pa547;
                gm_t88rcucg1b7uk t549 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t539) + 8) + 4));
                gm_l48 = t549;
                goto po543;
pa547:;
                goto mn542;
po543:;
                gm_t88rcucg1b7uk t550 = gm_l48;
                t540 = t550;
                goto me541;
mn542:;
                void * t552 = gm_l0;
                gm_tl874t2atnlh3 t553 = gm_l1;
                gm_b16_8 t554;
                GM_ST(void *, (uint8_t *)&t554, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t554 + 8, (uintptr_t)(35));
                gm_t76123gfet73g t555;
                gm_f_unsupported_70vvk1e((void *)&t555, (void *)NULL, cx, NULL, t552, t553, t554);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t556;
                GM_ST(uint8_t, (uint8_t *)&t556 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t556 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t556 + 4, 0);
                ((uint8_t *)&t556)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t556 + 8), t555);
                gm_tcfa6l7reoosn t557 = t556;
                uint8_t t558 = ((((uint8_t *)&t557))[0] != 0);
                F.ret = t556;
                goto gm_fret;
                goto me541;
mn551:;
                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 113, 37);
me541:;
                t517 = t540;
                goto me518;
mn533:;
ma521:;
                void * t560 = gm_l0;
                gm_tl874t2atnlh3 t561 = gm_l1;
                gm_b16_8 t562;
                GM_ST(void *, (uint8_t *)&t562, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t562 + 8, (uintptr_t)(35));
                gm_t76123gfet73g t563;
                gm_f_unsupported_70vvk1e((void *)&t563, (void *)NULL, cx, NULL, t560, t561, t562);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t564;
                GM_ST(uint8_t, (uint8_t *)&t564 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t564 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t564 + 4, 0);
                ((uint8_t *)&t564)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t564 + 8), t563);
                gm_tcfa6l7reoosn t565 = t564;
                uint8_t t566 = ((((uint8_t *)&t565))[0] != 0);
                F.ret = t564;
                goto gm_fret;
                goto me518;
mn559:;
ma522:;
                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 111, 27);
me518:;
                gm_l50 = t517;
            }
            {
                void * t567 = gm_l0;
                uint64_t t568 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                void * t569 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                uint64_t t570 = gm_l6;
                uint64_t t571 = (uintptr_t)t570;
                if (t571 > (uintptr_t)t568 || (uintptr_t)t568 > (uintptr_t)t568) gm_trap_slice(t571, (uintptr_t)t568, t568, gm_src8sgunk80orlb, 119, 23);
                gm_b16_8 t572;
                GM_ST(void *, (uint8_t *)&t572, (void *)(((uint8_t *)t569 + t571 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t572 + 8, (uintptr_t)((uintptr_t)t568 - t571));
                void * t573 = gm_l0;
                gm_tl874t2atnlh3 t574 = gm_l1;
                gm_t88rcucg1b7uk t575 = gm_l50;
                gm_t833u93mr4iob t576;
                gm_f_place_addr_m751en7((void *)&t576, (void *)NULL, cx, NULL, t573, t574, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l44)), t575);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t577 = t576;
                if (((((uint8_t *)&t577))[0] != 0)) {
                    gm_t76123gfet73g t578 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t577) + 8));
                    gm_tcfa6l7reoosn t579;
                    GM_ST(uint8_t, (uint8_t *)&t579 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t579 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t579 + 4, 0);
                    ((uint8_t *)&t579)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t579 + 8), t578);
                    F.ret = t579;
                    goto gm_fret;
                }
                uint64_t t580 = GM_LD(uint64_t, (((uint8_t *)&t577) + 8));
                gm_f_put_ptr_188p3mm(t567, t572, t580);
            }
            goto me5;
mn509:;
ma16:;
            uint8_t t582 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t582 != ((uint8_t)5ULL)) goto mn581;
            gm_tkaapcii86udk t583 = GM_LD(gm_tkaapcii86udk, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l51 = t583;
            uint64_t t584 = GM_LD(uint64_t, ((((uint8_t *)&F.l2) + 16) + 24));
            gm_l52 = t584;
            {
                void * t587 = gm_l0;
                gm_t88rcucg1b7uk t588 = gm_l3;
                gm_t53at80pfdus3 t589;
                gm_f_kind_htv6093((void *)&t589, t587, GM_CONV(gm_csguankko4lmhd, t588));
                gm_t53at80pfdus3 t590 = t589;
                uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&t590) + 0));
                if (t591 != ((uint8_t)10ULL)) goto ie586;
                gm_t88rcucg1b7uk t592 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t590) + 8) + 4));
                gm_l53 = t592;
                {
                    void * t594 = gm_l0;
                    uint64_t t595 = GM_LD(uint64_t, (((uint8_t *)t594) + 176));
                    uint64_t t596 = gm_f_us_cuv01fp(t595);
                    gm_l54 = t596;
                }
                {
                    gm_l55 = ((uint64_t)0ULL);
                }
                {
                    uint8_t t598 = 0;
                    uint64_t t599 = gm_l52;
                    uint8_t t600 = (t599 > ((uint64_t)0ULL));
                    if (!t600) {
                        uint64_t t601 = GM_LD(uintptr_t, (((uint8_t *)&gm_l51) + 8) + 8);
                        uint8_t t602 = (t601 > ((uint64_t)0ULL));
                        t598 = t602;
                    } else t598 = 1;
                    if (!t598) goto ie597;
                    {
                        void * t604 = gm_l0;
                        gm_tl874t2atnlh3 t605 = gm_l1;
                        gm_t88rcucg1b7uk t606 = gm_l53;
                        gm_t833u93mr4iob t607;
                        gm_f_place_addr_m751en7((void *)&t607, (void *)NULL, cx, NULL, t604, t605, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l51)), t606);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t833u93mr4iob t608 = t607;
                        if (((((uint8_t *)&t608))[0] != 0)) {
                            gm_t76123gfet73g t609 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t608) + 8));
                            gm_tcfa6l7reoosn t610;
                            GM_ST(uint8_t, (uint8_t *)&t610 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t610 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t610 + 4, 0);
                            ((uint8_t *)&t610)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t610 + 8), t609);
                            F.ret = t610;
                            goto gm_fret;
                        }
                        uint64_t t611 = GM_LD(uint64_t, (((uint8_t *)&t608) + 8));
                        gm_l55 = t611;
                    }
                    goto ix597;
ie597:;
ix597:;
                }
                {
                    void * t612 = gm_l0;
                    uint64_t t613 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    void * t614 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
                    uint64_t t615 = gm_l6;
                    uint64_t t616 = (uintptr_t)t615;
                    uint64_t t617 = gm_l6;
                    uint64_t t618 = gm_l54;
                    uint64_t r619; bool o620 = __builtin_mul_overflow(((uint64_t)2ULL), t618, &r619);
                    if (o620) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 126, 36);
                    uint64_t t621 = r619;
                    uint64_t r622; bool o623 = __builtin_add_overflow(t617, t621, &r622);
                    if (o623) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 126, 32);
                    uint64_t t624 = r622;
                    uint64_t t625 = (uintptr_t)t624;
                    if (t616 > t625 || t625 > (uintptr_t)t613) gm_trap_slice(t616, t625, t613, gm_src8sgunk80orlb, 126, 25);
                    gm_b16_8 t626;
                    GM_ST(void *, (uint8_t *)&t626, (void *)(((uint8_t *)t614 + t616 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t626 + 8, (uintptr_t)(t625 - t616));
                    uint64_t t627 = gm_l55;
                    uint64_t t628 = gm_l52;
                    gm_f_put_slice_ko14loi(t612, t626, t627, t628);
                }
                goto ix586;
ie586:;
                {
                    void * t630 = gm_l0;
                    gm_tl874t2atnlh3 t631 = gm_l1;
                    gm_b16_8 t632;
                    GM_ST(void *, (uint8_t *)&t632, (void *)(gm_svlcdvcv2bp94));
                    GM_ST(uintptr_t, (uint8_t *)&t632 + 8, (uintptr_t)(23));
                    gm_t76123gfet73g t633;
                    gm_f_unsupported_70vvk1e((void *)&t633, (void *)NULL, cx, NULL, t630, t631, t632);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tcfa6l7reoosn t634;
                    GM_ST(uint8_t, (uint8_t *)&t634 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t634 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t634 + 4, 0);
                    ((uint8_t *)&t634)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t634 + 8), t633);
                    gm_tcfa6l7reoosn t635 = t634;
                    uint8_t t636 = ((((uint8_t *)&t635))[0] != 0);
                    F.ret = t634;
                    goto gm_fret;
                }
ix586:;
            }
            goto me5;
mn581:;
ma17:;
            uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t638 != ((uint8_t)11ULL)) goto mn637;
            uint32_t t639 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
            gm_l56 = t639;
            void * t640 = gm_l0;
            uint64_t t641 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t642 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t643 = gm_l6;
            uint64_t t644 = (uintptr_t)t643;
            if (t644 > (uintptr_t)t641 || (uintptr_t)t641 > (uintptr_t)t641) gm_trap_slice(t644, (uintptr_t)t641, t641, gm_src8sgunk80orlb, 128, 29);
            gm_b16_8 t645;
            GM_ST(void *, (uint8_t *)&t645, (void *)(((uint8_t *)t642 + t644 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t645 + 8, (uintptr_t)((uintptr_t)t641 - t644));
            void * t646 = gm_l0;
            uint32_t t647 = gm_l56;
            uint64_t t648 = gm_f_fn_addr_odbbf6a(t646, t647);
            gm_f_put_ptr_188p3mm(t640, t645, t648);
            goto me5;
mn637:;
ma18:;
            uint8_t t652 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t652 != ((uint8_t)12ULL)) goto pa651;
            goto po650;
pa651:;
            uint8_t t654 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t654 != ((uint8_t)13ULL)) goto pa653;
            goto po650;
pa653:;
            goto mn649;
po650:;
            void * t655 = gm_l0;
            gm_tl874t2atnlh3 t656 = gm_l1;
            gm_b16_8 t657;
            GM_ST(void *, (uint8_t *)&t657, (void *)(gm_sbbavs945cjhp));
            GM_ST(uintptr_t, (uint8_t *)&t657 + 8, (uintptr_t)(31));
            gm_t76123gfet73g t658;
            gm_f_unsupported_70vvk1e((void *)&t658, (void *)NULL, cx, NULL, t655, t656, t657);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcfa6l7reoosn t659;
            GM_ST(uint8_t, (uint8_t *)&t659 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t659 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t659 + 4, 0);
            ((uint8_t *)&t659)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t659 + 8), t658);
            gm_tcfa6l7reoosn t660 = t659;
            uint8_t t661 = ((((uint8_t *)&t660))[0] != 0);
            F.ret = t659;
            goto gm_fret;
            goto me5;
mn649:;
ma19:;
            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 61, 5);
me5:;
        }
    }
    gm_b0_1 t662;
    gm_tcfa6l7reoosn t663;
    GM_ST(uint8_t, (uint8_t *)&t663 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t663 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t663 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t663 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t663 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t663 + 24, 0);
    ((uint8_t *)&t663)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t663 + 8), t662);
    gm_tcfa6l7reoosn t664 = t663;
    F.ret = t664;
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
