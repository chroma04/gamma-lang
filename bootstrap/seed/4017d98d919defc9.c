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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; int64_t f1; } gm_t4d55u65a2eb7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_th5c3ge61oapm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t4d55u65a2eb7) == 24 && _Alignof(gm_t4d55u65a2eb7) == 8 && offsetof(gm_t4d55u65a2eb7, f0) == 0 && offsetof(gm_t4d55u65a2eb7, f1) == 16, "layout of `([]u8, i64)`");
typedef struct { gm_th5c3ge61oapm f0; uint8_t f1; uint8_t pe[7]; } gm_t4ml5v7ugcjrt;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th5c3ge61oapm) == 32 && _Alignof(gm_th5c3ge61oapm) == 8 && offsetof(gm_th5c3ge61oapm, f0) == 0 && offsetof(gm_th5c3ge61oapm, f1) == 16 && offsetof(gm_th5c3ge61oapm, f2) == 24, "layout of `EnumInfo`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4ml5v7ugcjrt) == 40 && _Alignof(gm_t4ml5v7ugcjrt) == 8 && offsetof(gm_t4ml5v7ugcjrt, f0) == 0 && offsetof(gm_t4ml5v7ugcjrt, f1) == 32, "layout of `?EnumInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_ttu09lij36f8c f0; uint8_t f1; uint8_t pe[7]; } gm_tu3261i9p5758;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tu3261i9p5758) == 48 && _Alignof(gm_tu3261i9p5758) == 8 && offsetof(gm_tu3261i9p5758, f0) == 0 && offsetof(gm_tu3261i9p5758, f1) == 40, "layout of `??View`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_debug_view_gp38euq {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_define_enum_v780plg {
    gm_frame h;
    gm_tckdobunaulee l14; /* v */
    gm_trqfvmjtt4sij l15; /* items */
    gm_tckdobunaulee l20; /* v */
    gm_b16_8 ret;
};
struct gm_fr_define_view_sob497j {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_trqfvmjtt4sij l7; /* members */
    gm_t4c7ipjj9s3fp l8; /* taken */
    gm_trqfvmjtt4sij l11; /* labels */
    gm_trqfvmjtt4sij l30; /* arms */
    gm_tpautr4br7h3l l31; /* def */
    gm_trqfvmjtt4sij l32; /* names */
    gm_t0mglksrkfa5e l35; /* f */
    gm_t0mglksrkfa5e l40; /* f */
    gm_trqfvmjtt4sij l43; /* names */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l48; /* arms */
    gm_t0mglksrkfa5e l52; /* f */
    gm_trqfvmjtt4sij l56; /* names */
    gm_tckdobunaulee l59; /* vd */
    gm_trqfvmjtt4sij l61; /* arms */
    gm_tckdobunaulee l65; /* vd */
    gm_trqfvmjtt4sij l67; /* body */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dpjcq5f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_js3tkq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tv2p23d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_enum_info_61n2nkp {
    gm_frame h;
    gm_t4ml5v7ugcjrt ret;
};
struct gm_fr_get_fq78978 {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_5kr0r9p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_9mnq5mu {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_is_aggregate_buh1uic {
    gm_frame h;
    gm_tpautr4br7h3l l2; /* def */
    gm_tckdobunaulee l6; /* v */
    uint8_t ret;
};
struct gm_fr_is_signed_integer_ige9b74 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
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
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_at7dsst {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_packed_v79gj1r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pad_juftt9c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pointer_view_efkbf4q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_knqt0be {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_member_eubf7s6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_union_m2cqbk2 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* decls */
    gm_b0_1 ret;
};
struct gm_fr_reserve_40g0de6 {
    gm_frame h;
    gm_ttu09lij36f8c l4; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_scalar_f0e206b {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_static_assert_15uo2jf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_view_ams6b9s {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_b16_8 ret;
};
struct gm_fr_unique_view_members_3bvsjcm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_view_defs_u461okc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_kind_pth6nol {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_view_number_29hi30b {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qkqm3in {
    gm_frame h;
    gm_ttu09lij36f8c l2; /* cached */
    gm_ttu09lij36f8c l7; /* r */
    gm_ttu09lij36f8c l11; /* r */
    gm_ttu09lij36f8c l12; /* v */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_views_bc1b3r4 {
    gm_frame h;
    gm_tbpdf5ej15iof ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dpjcq5f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_js3tkq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tv2p23d(void);
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_p1k923m(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_08mmoe2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0bd7vot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_3qphnbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7c4i91d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_7nu71mn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8ibn6ei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_cn4968h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hn5gv7p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ivs3cj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jk4dii4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl4ha3s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lfjb9aq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_of6149p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_phh9f1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ubnq9mq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_v301q4v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_v0dhnfj(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint64_t gm_f_hash64_apkb7bo(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_5kr0r9p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_9mnq5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_at7dsst(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_m7lva8q(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_00pon0c(void * a0, uint64_t a1, gm_b0_1 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_25ngckq(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_remove_6blu1gh(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ier753o(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u1no4ia(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ue93873(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vfiknt5(void * a0, const void * a1);
extern uint8_t gm_cgsmfcq937e_3[16] __attribute__((aligned(8)));
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p17h7dbk8us[8];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4v09sn49l08n[7];
extern const uint8_t gm_s557bv2sfqcom[6];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_sakkgen9m77d6[16];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_si68bogf7eu32[8];
extern const uint8_t gm_sk2tgsi47prt7[14];
extern const uint8_t gm_sk8hn2ar9njah[15];
extern const uint8_t gm_smg3p90in08sb[10];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_soim4k70n6bc7[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcs9q7ghsrcgv5[];
extern const uint8_t gm_stntm4cqqrhf7[7];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_sumv38t63phct[14];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 816);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srckfibkpt681aa, 450, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dpjcq5f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dpjcq5f F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_js3tkq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_js3tkq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tv2p23d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tv2p23d F;
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_get_fq78978(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_fq78978 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_p1k923m(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_ttsm01t0d8srg t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 16) + 16));
        gm_ttsm01t0d8srg t14;
        GM_ST(gm_b0_1, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[0] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_p217beu F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 32) + 24));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_5kr0r9p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_5kr0r9p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_dpjcq5f();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_9mnq5mu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_9mnq5mu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_tv2p23d();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_ige9b74 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_signed_integer_s66djm7(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_at7dsst(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_at7dsst F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_js3tkq1();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tkn42u7na3t81 gm_f_pop_knqt0be(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_knqt0be F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* x */
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
            gm_tkn42u7na3t81 t8;
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
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
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
        gm_t88rcucg1b7uk t29 = gm_l2;
        gm_tkn42u7na3t81 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30), t29);
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
inline __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scalar_f0e206b F;
    gm_b16_8 gm_l0; /* cty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t6j6smj7qoctu t1;
    gm_b16_8 t2 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = GM_LD(gm_b16_8, gm_cgsmfcq937e_3);
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t3);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
    ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
    GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
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
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2) {
    struct gm_fr_static_assert_15uo2jf F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* cty */
    uint64_t gm_l2; /* size */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 872) + 104);
        gm_t3877pk7lv748 t4;
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        uint64_t t6 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_write_u1no4ia(t3, &t4);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcs9q7ghsrcgv5, 343, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_view_members_3bvsjcm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_view_members_3bvsjcm F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t1, t2, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_defs_u461okc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((((uint8_t *)t1) + 872) + 104) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_debug_view_gp38euq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* p */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 872) + 96));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_view_qkqm3in((void *)&(*(gm_ttu09lij36f8c *)gm_out), (void *)NULL, cx, NULL, t6, t7);
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
        void * t8 = gm_l0;
        GM_ST(uint8_t, ((((uint8_t *)t8) + 872) + 96), ((uint8_t)1));
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_f_view_qkqm3in((void *)&F.l2, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            void * t12 = gm_l0;
            void * t13 = (void *)((((uint8_t *)t12) + 872) + 56);
            gm_tkn42u7na3t81 t14 = gm_f_pop_knqt0be(t13);
            if (!((((uint8_t *)&t14))[4] != 0)) goto lb11;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t14));
            gm_l3 = t15;
            {
                void * t18 = gm_l0;
                gm_t88rcucg1b7uk t19 = gm_l3;
                gm_b0_1 t20 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t18, t19);
                if (gm_unw.kind) {
                    goto gm_ll16;
                }
            }
lc11:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
        }
        goto gm_lad;
lb11:;
    }
    {
        void * t21 = gm_l0;
        GM_ST(uint8_t, ((((uint8_t *)t21) + 872) + 96), ((uint8_t)0));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_enum_v780plg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_define_enum_v780plg F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* view */
    void * gm_l3; /* def */
    gm_t88rcucg1b7uk gm_l4; /* tag */
    gm_b16_8 gm_l5; /* variants */
    gm_th5c3ge61oapm gm_l6; /* info */
    gm_b16_8 gm_l7; /* name */
    uint64_t gm_l8; /* n */
    gm_b16_8 gm_l9; /* prefix */
    uint64_t gm_l10; /* k */
    uint8_t gm_l11; /* clash */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    int64_t gm_l16; /* lo */
    int64_t gm_l17; /* hi */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l21; /* e */
    int64_t gm_l22; /* value */
    uint64_t gm_l23; /* natural */
    gm_tc0nbpdhofi37 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* s */
    uint64_t gm_l28; /* bits */
    uint8_t gm_l29; /* fits */
    uint64_t gm_l30; /* bits */
    int64_t gm_l31; /* wide */
    gm_b16_8 gm_l32; /* e */
    GM_STACK_CHECK(&F, 2636u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_gej99vn(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l3 = t4;
    }
    {
        void * t6 = gm_l3;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 24) + 0));
        if (t7 != ((uint8_t)1ULL)) goto ie5;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t6) + 24) + 8) + 0));
        gm_l4 = t8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 24) + 8) + 8));
        gm_l5 = t9;
        {
            void * t11 = gm_l0;
            gm_t88rcucg1b7uk t12 = gm_l1;
            gm_t4ml5v7ugcjrt t13;
            gm_f_enum_info_61n2nkp((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th5c3ge61oapm t14;
            if (((((uint8_t *)&t13))[32] != 0)) {
                t14 = (*(gm_th5c3ge61oapm *)(((uint8_t *)&t13) + 0));
            } else {
                void * t15 = gm_l0;
                gm_t88rcucg1b7uk t16 = gm_l4;
                gm_b16_8 t17 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
            gm_l6 = t14;
        }
        {
            gm_b16_8 t19 = gm_l2;
            gm_l7 = t19;
        }
        {
            gm_b16_8 t21 = gm_l2;
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s557bv2sfqcom));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(5));
            uint8_t t23 = gm_f_starts_with_6vvt01e(t21, t22);
            uint8_t t24 = (uint8_t)!t23;
            if (!t24) goto ie20;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l0;
                gm_t88rcucg1b7uk t28 = gm_l1;
                gm_b16_8 t29 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t30;
                GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                gm_tabb710d9mq2e t31;
                gm_f_format_kl4ha3s((void *)&t31, (void *)NULL, cx, NULL, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcs9q7ghsrcgv5, 377, 29);
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                uint64_t t34 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t26, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t34;
            }
            {
                uint64_t t35 = gm_l8;
                gm_t4ce0jd3tdro4 t36;
                GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t35);
                gm_tabb710d9mq2e t37;
                gm_f_format_8ibn6ei((void *)&t37, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcs9q7ghsrcgv5, 378, 16);
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_l7 = t39;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
            gm_l9 = t40;
        }
        {
            gm_l10 = ((uint64_t)3ULL);
        }
        {
            for (;;) {
                if (!((uint8_t)1)) goto lb41;
                {
                    gm_l11 = ((uint8_t)0);
                }
                {
                    {
                        gm_b16_8 t45 = gm_l5;
                        gm_l12 = t45;
                    }
                    {
                        gm_l13 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t47 = gm_l13;
                            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            uint8_t t49 = (t47 < t48);
                            if (!t49) goto lb46;
                            {
                                uint64_t t52 = gm_l13;
                                uint64_t t53 = (uintptr_t)t52;
                                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                gm_tckdobunaulee t57 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t53 * 72));
                                F.l14 = t57;
                                {
                                    void * t60 = gm_l0;
                                    void * t61 = (void *)((((uint8_t *)t60) + 872) + 200);
                                    gm_t6j6smj7qoctu t62;
                                    gm_b16_8 t63 = gm_l9;
                                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
                                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t64);
                                    gm_tabb710d9mq2e t65;
                                    gm_f_format_of6149p((void *)&t65, (void *)NULL, cx, NULL, &t62);
                                    if (gm_unw.kind) {
                                        goto gm_ll50;
                                    }
                                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcs9q7ghsrcgv5, 387, 41);
                                    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                                    gm_tqs5p4n2tq19v t68 = gm_f_get_p217beu(t61, t67);
                                    uint8_t t69;
                                    if (((((uint8_t *)&t68))[1] != 0)) goto mn71;
                                    t69 = ((uint8_t)0);
                                    goto me70;
mn71:;
                                    t69 = ((uint8_t)1);
                                    goto me70;
mn72:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 387, 16);
me70:;
                                    if (!t69) goto ie59;
                                    {
                                        gm_l11 = ((uint8_t)1);
                                    }
                                    goto ix59;
ie59:;
ix59:;
                                }
                                goto me55;
mn56:;
                                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 386, 9);
me55:;
                            }
lc46:;
                            {
                                uint64_t t75 = gm_l13;
                                uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                                uint64_t t78 = r76;
                                gm_l13 = t78;
                            }
                        }
gm_ll50:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                        }
                        goto gm_ll42;
lb46:;
                    }
                }
                {
                    uint8_t t80 = gm_l11;
                    uint8_t t81 = (uint8_t)!t80;
                    if (!t81) goto ie79;
                    {
                        goto lb41;
                    }
                    goto ix79;
ie79:;
ix79:;
                }
                {
                    gm_t6j6smj7qoctu t83;
                    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t83 + 0, t84);
                    void * t85 = gm_l0;
                    gm_t88rcucg1b7uk t86 = gm_l1;
                    gm_b16_8 t87 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_ll42;
                    }
                    uint64_t t88 = gm_l10;
                    gm_b16_8 t89 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_ll42;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t83 + 16, t89);
                    gm_tabb710d9mq2e t90;
                    gm_f_format_of6149p((void *)&t90, (void *)NULL, cx, NULL, &t83);
                    if (gm_unw.kind) {
                        goto gm_ll42;
                    }
                    if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srcs9q7ghsrcgv5, 390, 18);
                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                    gm_l9 = t92;
                }
                {
                    uint64_t t93 = gm_l10;
                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                    if (o95) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 391, 9);
                    uint64_t t96 = r94;
                    gm_l10 = t96;
                }
lc41:;
            }
gm_ll42:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb41; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc41; }
            }
            goto gm_lad;
lb41:;
        }
        {
            gm_f_list_epvducg((void *)&F.l15, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l16 = ((int64_t)0LL);
        }
        {
            gm_l17 = ((int64_t)0LL);
        }
        {
            {
                gm_b16_8 t98 = gm_l5;
                gm_l18 = t98;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l19;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l19;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_tckdobunaulee t110 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t106 * 72));
                        F.l20 = t110;
                        {
                            gm_t6j6smj7qoctu t112;
                            gm_b16_8 t113 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t112 + 0, t113);
                            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t112 + 16, t114);
                            gm_tabb710d9mq2e t115;
                            gm_f_format_of6149p((void *)&t115, (void *)NULL, cx, NULL, &t112);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_srcs9q7ghsrcgv5, 397, 14);
                            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                            gm_l21 = t116;
                        }
                        {
                            gm_t913bb735pvt5 t117 = gm_f_to_i64_4cgpvak(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l20) + 24)));
                            int64_t t118;
                            if (((((uint8_t *)&t117))[8] != 0)) {
                                int64_t t119 = GM_LD(int64_t, (((uint8_t *)&t117) + 0));
                                t118 = t119;
                            } else {
                                t118 = ((int64_t)0LL);
                            }
                            gm_l22 = t118;
                        }
                        {
                            int64_t t120 = gm_l16;
                            int64_t t121 = gm_l22;
                            int64_t t122 = (t120 < t121 ? t120 : t121);
                            gm_l16 = t122;
                        }
                        {
                            int64_t t123 = gm_l17;
                            int64_t t124 = gm_l22;
                            int64_t t125 = (t123 > t124 ? t123 : t124);
                            gm_l17 = t125;
                        }
                        {
                            void * t126 = gm_l0;
                            void * t127 = (void *)((((uint8_t *)t126) + 872) + 200);
                            gm_b16_8 t128 = gm_l21;
                            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                            gm_tftb1odc739t5 t130 = gm_f_put_5c6qgui(t127, t129, ((uint8_t)1));
                            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 1, gm_srcs9q7ghsrcgv5, 401, 9);
                            gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t130) + 1));
                        }
                        {
                            void * t132 = gm_l0;
                            gm_b16_8 t133 = gm_l21;
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                            gm_b0_1 t135 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t132, t134);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                        }
                        {
                            void * t136 = (void *)((uint8_t *)&F.l15);
                            gm_t4d55u65a2eb7 t137;
                            gm_b16_8 t138 = gm_l21;
                            GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
                            int64_t t139 = gm_l22;
                            GM_ST(int64_t, (uint8_t *)&t137 + 16, t139);
                            gm_tabb710d9mq2e t140;
                            gm_f_format_3qphnbe((void *)&t140, (void *)NULL, cx, NULL, &t137);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_srcs9q7ghsrcgv5, 403, 20);
                            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
                            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                            gm_tftb1odc739t5 t143 = gm_f_push_3ef746b(t136, t142);
                            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcs9q7ghsrcgv5, 403, 9);
                            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                        }
                        goto me108;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 396, 5);
me108:;
                    }
lc99:;
                    {
                        uint64_t t146 = gm_l19;
                        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                        uint64_t t149 = r147;
                        gm_l19 = t149;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        {
            gm_l23 = ((uint64_t)4ULL);
        }
        {
            {
                {
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 0, ((uint64_t)1ULL));
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 8, ((uint64_t)2ULL));
                    GM_ST(uint64_t, (uint8_t *)&gm_l24 + 16, ((uint64_t)4ULL));
                }
                void * t152 = ((uint8_t *)&gm_l24);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcs9q7ghsrcgv5, 408, 5);
                gm_b16_8 t153;
                GM_ST(void *, (uint8_t *)&t153, (void *)(((uint8_t *)t152 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t154 = t153;
                gm_l25 = t154;
            }
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t156 = gm_l26;
                    uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    uint8_t t158 = (t156 < t157);
                    if (!t158) goto lb155;
                    {
                        uint64_t t161 = gm_l26;
                        uint64_t t162 = (uintptr_t)t161;
                        uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        uint64_t t164 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t162 * 8));
                        uint64_t t167 = GM_LD(uint64_t, ((uint8_t *)&t164));
                        gm_l27 = t167;
                        {
                            uint64_t t169 = gm_l27;
                            uint64_t r170; bool o171 = __builtin_mul_overflow(((uint64_t)8ULL), t169, &r170);
                            if (o171) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 409, 17);
                            uint64_t t172 = r170;
                            gm_l28 = t172;
                        }
                        {
                            uint8_t t173;
                            int64_t t175 = gm_l16;
                            uint8_t t176 = (t175 < ((int64_t)0LL));
                            if (!t176) goto ie174;
                            uint8_t t178 = 0;
                            int64_t t179 = gm_l16;
                            uint64_t t180 = gm_l28;
                            uint64_t r181; bool o182 = __builtin_sub_overflow(t180, ((uint64_t)1ULL), &r181);
                            if (o182) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 48);
                            uint64_t t183 = r181;
                            unsigned __int128 n184 = (unsigned __int128)t183;
                            if (n184 >= 64) gm_trap_shift((uint64_t)n184, "i64", gm_srcs9q7ghsrcgv5, 410, 37);
                            int64_t r185 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n184);
                            bool o186 = (int64_t)(r185 >> (int)n184) != ((int64_t)1LL);
                            if (o186) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 37);
                            int64_t t187 = r185;
                            if (t187 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 35);
                            int64_t t188 = (int64_t)((uint64_t)0 - (uint64_t)t187);
                            uint8_t t189 = (t179 >= t188);
                            if (t189) {
                                int64_t t190 = gm_l17;
                                uint64_t t191 = gm_l28;
                                uint64_t r192; bool o193 = __builtin_sub_overflow(t191, ((uint64_t)1ULL), &r192);
                                if (o193) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 410, 80);
                                uint64_t t194 = r192;
                                unsigned __int128 n195 = (unsigned __int128)t194;
                                if (n195 >= 64) gm_trap_shift((uint64_t)n195, "i64", gm_srcs9q7ghsrcgv5, 410, 69);
                                int64_t r196 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n195);
                                bool o197 = (int64_t)(r196 >> (int)n195) != ((int64_t)1LL);
                                if (o197) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 69);
                                int64_t t198 = r196;
                                uint8_t t199 = (t190 < t198);
                                t178 = t199;
                            } else t178 = 0;
                            uint8_t t200 = t178;
                            t173 = t200;
                            goto ix174;
ie174:;
                            int64_t t202 = gm_l17;
                            uint64_t t203 = gm_l28;
                            unsigned __int128 n204 = (unsigned __int128)t203;
                            if (n204 >= 64) gm_trap_shift((uint64_t)n204, "i64", gm_srcs9q7ghsrcgv5, 410, 106);
                            int64_t r205 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n204);
                            bool o206 = (int64_t)(r205 >> (int)n204) != ((int64_t)1LL);
                            if (o206) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 410, 106);
                            int64_t t207 = r205;
                            uint8_t t208 = (t202 < t207);
                            uint8_t t209 = t208;
                            t173 = t209;
ix174:;
                            gm_l29 = t173;
                        }
                        {
                            uint8_t t211 = gm_l29;
                            if (!t211) goto ie210;
                            {
                                uint64_t t213 = gm_l27;
                                gm_l23 = t213;
                            }
                            {
                                goto lb155;
                            }
                            goto ix210;
ie210:;
ix210:;
                        }
                        goto me165;
mn166:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 408, 5);
me165:;
                    }
lc155:;
                    {
                        uint64_t t215 = gm_l26;
                        uint64_t r216; bool o217 = __builtin_add_overflow(t215, ((uint64_t)1ULL), &r216);
                        uint64_t t218 = r216;
                        gm_l26 = t218;
                    }
                }
lb155:;
            }
        }
        {
            uint64_t t220 = gm_l23;
            uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
            uint8_t t222 = (t220 < t221);
            if (!t222) goto ie219;
            {
                uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 417, 22);
                uint64_t t225 = (uint64_t)(t224 / ((uint64_t)2ULL));
                uint64_t r226; bool o227 = __builtin_mul_overflow(((uint64_t)8ULL), t225, &r226);
                if (o227) gm_trap("integer overflow in `*`", gm_srcs9q7ghsrcgv5, 417, 17);
                uint64_t t228 = r226;
                gm_l30 = t228;
            }
            {
                int64_t t229;
                uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                if (!t231) goto ie230;
                uint64_t t233 = gm_l30;
                uint64_t r234; bool o235 = __builtin_sub_overflow(t233, ((uint64_t)1ULL), &r234);
                if (o235) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 51);
                uint64_t t236 = r234;
                unsigned __int128 n237 = (unsigned __int128)t236;
                if (n237 >= 64) gm_trap_shift((uint64_t)n237, "i64", gm_srcs9q7ghsrcgv5, 418, 40);
                int64_t r238 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n237);
                bool o239 = (int64_t)(r238 >> (int)n237) != ((int64_t)1LL);
                if (o239) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 40);
                int64_t t240 = r238;
                if (t240 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                int64_t t241 = (int64_t)((uint64_t)0 - (uint64_t)t240);
                int64_t r242; bool o243 = __builtin_sub_overflow(t241, ((int64_t)1LL), &r242);
                if (o243) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 418, 38);
                int64_t t244 = r242;
                int64_t t245 = t244;
                t229 = t245;
                goto ix230;
ie230:;
                uint64_t t247 = gm_l30;
                unsigned __int128 n248 = (unsigned __int128)t247;
                if (n248 >= 64) gm_trap_shift((uint64_t)n248, "i64", gm_srcs9q7ghsrcgv5, 418, 75);
                int64_t r249 = (int64_t)((uint64_t)((int64_t)1LL) << (int)n248);
                bool o250 = (int64_t)(r249 >> (int)n248) != ((int64_t)1LL);
                if (o250) gm_trap("integer overflow in `<<`", gm_srcs9q7ghsrcgv5, 418, 75);
                int64_t t251 = r249;
                int64_t t252 = t251;
                t229 = t252;
ix230:;
                gm_l31 = t229;
            }
            {
                gm_b16_8 t253 = gm_l9;
                gm_tnpifv20irnph t254;
                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 0), t253);
                gm_tabb710d9mq2e t255;
                gm_f_format_ubnq9mq((void *)&t255, (void *)NULL, cx, NULL, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 8, gm_srcs9q7ghsrcgv5, 419, 14);
                gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&t255) + 8));
                gm_l32 = t256;
            }
            {
                void * t257 = gm_l0;
                void * t258 = (void *)((((uint8_t *)t257) + 872) + 200);
                gm_b16_8 t259 = gm_l32;
                gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
                gm_tftb1odc739t5 t261 = gm_f_put_5c6qgui(t258, t260, ((uint8_t)1));
                if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 1, gm_srcs9q7ghsrcgv5, 420, 9);
                gm_b0_1 t262 = GM_LD(gm_b0_1, (((uint8_t *)&t261) + 1));
            }
            {
                void * t263 = gm_l0;
                gm_b16_8 t264 = gm_l32;
                gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                gm_b0_1 t266 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t263, t265);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t267 = (void *)((uint8_t *)&F.l15);
                gm_t4d55u65a2eb7 t268;
                gm_b16_8 t269 = gm_l32;
                GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t269);
                int64_t t270 = gm_l31;
                GM_ST(int64_t, (uint8_t *)&t268 + 16, t270);
                gm_tabb710d9mq2e t271;
                gm_f_format_3qphnbe((void *)&t271, (void *)NULL, cx, NULL, &t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t271))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t271) + 8, gm_srcs9q7ghsrcgv5, 422, 20);
                gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)&t271) + 8));
                gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
                gm_tftb1odc739t5 t274 = gm_f_push_3ef746b(t267, t273);
                if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 1, gm_srcs9q7ghsrcgv5, 422, 9);
                gm_b0_1 t275 = GM_LD(gm_b0_1, (((uint8_t *)&t274) + 1));
            }
            goto ix219;
ie219:;
ix219:;
        }
        {
            void * t276 = gm_l0;
            void * t277 = (void *)((((uint8_t *)t276) + 872) + 104);
            gm_t6j6smj7qoctu t278;
            gm_b16_8 t279 = gm_l7;
            gm_b16_8 t280 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t278 + 0, t280);
            gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
            gm_b16_8 t283;
            GM_ST(void *, (uint8_t *)&t283, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t284;
            gm_f_join_sfknljb((void *)&t284, (void *)NULL, cx, NULL, t282, t283);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srcs9q7ghsrcgv5, 424, 57);
            gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t278 + 16, t285);
            gm_tftb1odc739t5 t286 = gm_f_write_ier753o(t277, &t278);
            if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 1, gm_srcs9q7ghsrcgv5, 424, 5);
            gm_b0_1 t287 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 1));
        }
        {
            void * t288 = gm_l0;
            gm_b16_8 t289 = gm_l7;
            uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
            gm_b0_1 t291 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t288, t289, t290);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t292 = gm_l7;
            uint8_t t293 = 0;
            F.ret = t292;
            goto gm_fret;
        }
        goto ix5;
ie5:;
        {
            gm_trap("reached `@unreachable()`", gm_srcs9q7ghsrcgv5, 373, 44);
        }
ix5:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_define_view_sob497j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_define_view_sob497j F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_b16_8 gm_l4; /* cty */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* offs */
    gm_b16_8 gm_l9; /* elems */
    gm_b16_8 gm_l10; /* names */
    gm_b16_8 gm_l12; /* chosen */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* k */
    gm_th2lqbo77pvsc gm_l16; /* n */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    gm_t88rcucg1b7uk gm_l23; /* e */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    gm_b16_8 gm_l25; /* target */
    gm_b16_8 gm_l26; /* len */
    gm_t88rcucg1b7uk gm_l27; /* inner */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l36; /* chosen */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    gm_b16_8 gm_l41; /* name */
    gm_b16_8 gm_l42; /* fields */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l47; /* chosen */
    gm_b16_8 gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* k */
    gm_t88rcucg1b7uk gm_l53; /* tag */
    gm_b16_8 gm_l54; /* variants */
    gm_b16_8 gm_l55; /* tag_cty */
    gm_b16_8 gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    gm_b16_8 gm_l60; /* chosen */
    gm_b16_8 gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* k */
    gm_t88rcucg1b7uk gm_l66; /* p */
    uint64_t gm_l68; /* at */
    gm_b16_8 gm_l69; /* <tmp> */
    uint64_t gm_l70; /* <tmp> */
    gm_tbccvikg6b96e gm_l71; /* m */
    GM_STACK_CHECK(&F, 5101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((((uint8_t *)t2) + 872) + 0);
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t5;
        gm_f_get_v0dhnfj((void *)&t5, t3, t4);
        gm_ttu09lij36f8c t6;
        if (((((uint8_t *)&t5))[40] != 0)) {
            t6 = (*(gm_ttu09lij36f8c *)(((uint8_t *)&t5) + 0));
        } else {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        F.l2 = t6;
    }
    {
        gm_ttu09lij36f8c t9 = F.l2;
        gm_t6j6smj7qoctu t10;
        if (((((uint8_t *)&t9))[32] != 0)) {
            t10 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t9) + 0));
        } else {
            gm_b0_1 t11;
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_l4 = t13;
    }
    {
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l1;
        uint64_t t16 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_b16_8 t19 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t19;
    }
    {
        gm_f_list_at7dsst((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_hash_map_cuh4fs9((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        gm_t53at80pfdus3 t22;
        gm_f_kind_ghvav22((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        switch (t31) { case ((uint8_t)0ULL): goto ma29; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma29; case ((uint8_t)3ULL): goto ma29; case ((uint8_t)4ULL): goto ma29; case ((uint8_t)5ULL): goto ma29; case ((uint8_t)6ULL): goto ma29; case ((uint8_t)7ULL): goto ma29; case ((uint8_t)8ULL): goto ma29; case ((uint8_t)9ULL): goto ma29; case ((uint8_t)10ULL): goto ma25; case ((uint8_t)11ULL): goto ma29; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma26; case ((uint8_t)14ULL): goto ma27; case ((uint8_t)15ULL): goto ma29; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma28; case ((uint8_t)18ULL): goto ma29; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma29; case ((uint8_t)21ULL): goto ma29; case ((uint8_t)22ULL): goto ma29; case ((uint8_t)23ULL): goto ma29; case ((uint8_t)24ULL): goto ma29; case ((uint8_t)25ULL): goto ma29; case ((uint8_t)26ULL): goto ma29; default: goto ma29; }
ma24:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t33 != ((uint8_t)12ULL)) goto mn32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)&t22) + 8) + 0));
        gm_l9 = t34;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)&t22) + 8) + 16));
        gm_l10 = t35;
        {
            gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            uint8_t t39 = (t38 > ((uint64_t)0ULL));
            if (!t39) goto ie37;
            {
                gm_b16_8 t41 = gm_l10;
                void * t42 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t43 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t43;
            }
            {
                {
                    gm_b16_8 t45 = gm_l12;
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                    gm_l13 = t46;
                }
                {
                    gm_l14 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t48 = gm_l14;
                        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        uint8_t t50 = (t48 < t49);
                        if (!t50) goto lb47;
                        {
                            gm_t13urt52695sh t53;
                            uint64_t t54 = gm_l14;
                            GM_ST(uint64_t, (uint8_t *)&t53 + 0, t54);
                            uint64_t t55 = gm_l14;
                            uint64_t t56 = (uintptr_t)t55;
                            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                            memcpy((uint8_t *)&t53 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t56 * 24), 24);
                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&t53) + 0));
                            gm_l15 = t60;
                            gm_l16 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t53) + 8));
                            {
                                void * t62 = (void *)((uint8_t *)&F.l11);
                                gm_th2lqbo77pvsc t63 = gm_l16;
                                gm_b16_8 t64;
                                if (((((uint8_t *)&t63))[16] != 0)) {
                                    gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 0));
                                    t64 = t65;
                                } else {
                                    uint64_t t66 = gm_l15;
                                    gm_t4ce0jd3tdro4 t67;
                                    GM_ST(uint64_t, ((uint8_t *)&t67 + 0), t66);
                                    gm_tabb710d9mq2e t68;
                                    gm_f_format_7c4i91d((void *)&t68, (void *)NULL, cx, NULL, t67);
                                    if (gm_unw.kind) {
                                        goto gm_ll51;
                                    }
                                    if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcs9q7ghsrcgv5, 239, 68);
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                                    t64 = t70;
                                }
                                gm_tftb1odc739t5 t71 = gm_f_push_3ef746b(t62, t64);
                                if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcs9q7ghsrcgv5, 239, 49);
                                gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                            }
                            goto me58;
mn59:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 239, 17);
me58:;
                        }
lc47:;
                        {
                            uint64_t t74 = gm_l14;
                            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                            uint64_t t77 = r75;
                            gm_l14 = t77;
                        }
                    }
gm_ll51:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                    }
                    goto gm_lad;
lb47:;
                }
            }
            goto ix37;
ie37:;
            {
                {
                    gm_l18 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    gm_l17 = t80;
                }
                {
                    for (;;) {
                        uint64_t t82 = gm_l18;
                        uint64_t t83 = gm_l17;
                        uint8_t t84 = (t82 < t83);
                        if (!t84) goto lb81;
                        {
                            uint64_t t87 = gm_l18;
                            uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)&t87));
                            gm_l19 = t90;
                            {
                                void * t92 = (void *)((uint8_t *)&F.l11);
                                uint64_t t93 = gm_l19;
                                gm_t4ce0jd3tdro4 t94;
                                GM_ST(uint64_t, ((uint8_t *)&t94 + 0), t93);
                                gm_tabb710d9mq2e t95;
                                gm_f_format_7c4i91d((void *)&t95, (void *)NULL, cx, NULL, t94);
                                if (gm_unw.kind) {
                                    goto gm_ll85;
                                }
                                if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcs9q7ghsrcgv5, 241, 53);
                                gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
                                gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                                gm_tftb1odc739t5 t98 = gm_f_push_3ef746b(t92, t97);
                                if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srcs9q7ghsrcgv5, 241, 41);
                                gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                            }
                            goto me88;
mn89:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 241, 17);
me88:;
                        }
lc81:;
                        {
                            uint64_t t101 = gm_l18;
                            uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                            uint64_t t104 = r102;
                            gm_l18 = t104;
                        }
                    }
gm_ll85:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb81; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc81; }
                    }
                    goto gm_lad;
lb81:;
                }
            }
ix37:;
        }
        {
            {
                gm_b16_8 t106 = gm_l9;
                gm_l20 = t106;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t108 = gm_l21;
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t110 = (t108 < t109);
                    if (!t110) goto lb107;
                    {
                        uint64_t t113 = gm_l21;
                        uint64_t t114 = gm_l21;
                        uint64_t t115 = (uintptr_t)t114;
                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t115 * 4));
                        gm_tlcm4lvu3g1a8 t118;
                        GM_ST(uint32_t, (uint8_t *)&t118 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t118 + 0), t113);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t118 + 8), t117);
                        uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&t118) + 0));
                        gm_l22 = t121;
                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t118) + 8));
                        gm_l23 = t122;
                        {
                            void * t124 = gm_l0;
                            void * t125 = (void *)((uint8_t *)&F.l7);
                            uint64_t t126 = gm_l22;
                            uint64_t t127 = (uintptr_t)t126;
                            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcs9q7ghsrcgv5, 243, 69);
                            uint64_t t129 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t127 * 8));
                            gm_t88rcucg1b7uk t130 = gm_l23;
                            uint64_t t131 = gm_l22;
                            uint64_t t132 = (uintptr_t)t131;
                            uint64_t t133 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                            if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srcs9q7ghsrcgv5, 243, 81);
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t132 * 16));
                            gm_b0_1 t135 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t124, t125, t129, t130, t134);
                            if (gm_unw.kind) {
                                goto gm_ll111;
                            }
                        }
                        goto me119;
mn120:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 243, 13);
me119:;
                    }
lc107:;
                    {
                        uint64_t t137 = gm_l21;
                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                        uint64_t t140 = r138;
                        gm_l21 = t140;
                    }
                }
gm_ll111:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb107; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc107; }
                }
                goto gm_lad;
lb107:;
            }
        }
        goto me23;
mn32:;
ma25:;
        uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t142 != ((uint8_t)10ULL)) goto mn141;
        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t22) + 8) + 4));
        gm_l24 = t143;
        {
            void * t145 = gm_l0;
            gm_t88rcucg1b7uk t146 = gm_l24;
            gm_b16_8 t147 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t147;
        }
        {
            void * t148 = gm_l0;
            void * t149 = gm_l0;
            gm_t88rcucg1b7uk t150 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t151 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t148, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l26 = t151;
        }
        {
            void * t152 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t153;
            uint64_t t154 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t154 >= (uintptr_t)(t155)) gm_trap_index(t154, t155, gm_srcs9q7ghsrcgv5, 248, 45);
            uint64_t t156 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t154 * 8));
            GM_ST(uint64_t, (uint8_t *)&t153 + 0, t156);
            uint64_t t157 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t158 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t157 >= (uintptr_t)(t158)) gm_trap_index(t157, t158, gm_srcs9q7ghsrcgv5, 248, 60);
            uint64_t t159 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t157 * 8));
            GM_ST(uint64_t, (uint8_t *)&t153 + 8, t159);
            gm_b16_8 t160 = gm_l25;
            gm_tnpifv20irnph t161;
            GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t160);
            gm_tabb710d9mq2e t162;
            gm_f_format_cn4968h((void *)&t162, (void *)NULL, cx, NULL, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_srcs9q7ghsrcgv5, 248, 75);
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
            gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
            GM_ST(gm_b16_8, (uint8_t *)&t153 + 16, t164);
            gm_tftb1odc739t5 t165 = gm_f_push_m7lva8q(t152, &t153);
            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srcs9q7ghsrcgv5, 248, 13);
            gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
        }
        {
            void * t167 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t168;
            uint64_t t169 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t169 >= (uintptr_t)(t170)) gm_trap_index(t169, t170, gm_srcs9q7ghsrcgv5, 249, 45);
            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t169 * 8));
            GM_ST(uint64_t, (uint8_t *)&t168 + 0, t171);
            uint64_t t172 = gm_l5;
            uint64_t t173 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t173 >= (uintptr_t)(t174)) gm_trap_index(t173, t174, gm_srcs9q7ghsrcgv5, 249, 67);
            uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t173 * 8));
            uint64_t r176; bool o177 = __builtin_sub_overflow(t172, t175, &r176);
            if (o177) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 249, 60);
            uint64_t t178 = r176;
            GM_ST(uint64_t, (uint8_t *)&t168 + 8, t178);
            gm_b16_8 t179 = gm_l26;
            gm_tnpifv20irnph t180;
            GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
            gm_tabb710d9mq2e t181;
            gm_f_format_jk4dii4((void *)&t181, (void *)NULL, cx, NULL, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 8, gm_srcs9q7ghsrcgv5, 249, 82);
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&t181) + 8));
            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 16, t183);
            gm_tftb1odc739t5 t184 = gm_f_push_m7lva8q(t167, &t168);
            if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srcs9q7ghsrcgv5, 249, 13);
            gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
        }
        goto me23;
mn141:;
ma26:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t187 != ((uint8_t)13ULL)) goto mn186;
        gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t22) + 8));
        gm_l27 = t188;
        {
            void * t190 = gm_l0;
            void * t191 = (void *)((uint8_t *)&F.l7);
            uint64_t t192 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_srcs9q7ghsrcgv5, 252, 38);
            uint64_t t194 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t192 * 8));
            gm_t88rcucg1b7uk t195 = gm_l27;
            gm_b16_8 t196;
            GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s5qkai093c1q0));
            GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(5));
            gm_b0_1 t197 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t190, t191, t194, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t198 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t199;
            uint64_t t200 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t200 >= (uintptr_t)(t201)) gm_trap_index(t200, t201, gm_srcs9q7ghsrcgv5, 253, 45);
            uint64_t t202 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t200 * 8));
            GM_ST(uint64_t, (uint8_t *)&t199 + 0, t202);
            GM_ST(uint64_t, (uint8_t *)&t199 + 8, ((uint64_t)1ULL));
            gm_b16_8 t203;
            GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sk8hn2ar9njah));
            GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(14));
            GM_ST(gm_b16_8, (uint8_t *)&t199 + 16, t203);
            gm_tftb1odc739t5 t204 = gm_f_push_m7lva8q(t198, &t199);
            if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_srcs9q7ghsrcgv5, 253, 13);
            gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
        }
        goto me23;
mn186:;
ma27:;
        uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t207 != ((uint8_t)14ULL)) goto mn206;
        gm_t88rcucg1b7uk t208 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t22) + 8) + 0));
        gm_l28 = t208;
        gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t22) + 8) + 4));
        gm_l29 = t209;
        {
            void * t211 = (void *)((uint8_t *)&F.l7);
            gm_tbccvikg6b96e t212;
            uint64_t t213 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t214 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t213 >= (uintptr_t)(t214)) gm_trap_index(t213, t214, gm_srcs9q7ghsrcgv5, 256, 45);
            uint64_t t215 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t213 * 8));
            GM_ST(uint64_t, (uint8_t *)&t212 + 0, t215);
            GM_ST(uint64_t, (uint8_t *)&t212 + 8, ((uint64_t)1ULL));
            gm_b16_8 t216;
            GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sk2tgsi47prt7));
            GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(13));
            GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t216);
            gm_tftb1odc739t5 t217 = gm_f_push_m7lva8q(t211, &t212);
            if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srcs9q7ghsrcgv5, 256, 13);
            gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
        }
        {
            gm_f_list_58nr42v_2((void *)&F.l30, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t219 = (void *)((uint8_t *)&F.l30);
            gm_tcmkompbl09il t220;
            memset((uint8_t *)&t220 + 4, 0, 4);
            gm_t88rcucg1b7uk t221 = gm_l29;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t220 + 0, t221);
            gm_b16_8 t222;
            GM_ST(void *, (uint8_t *)&t222, (void *)(gm_soim4k70n6bc7));
            GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&t220 + 8, t222);
            gm_tftb1odc739t5 t223 = gm_f_push_bpovr2k_2(t219, &t220);
            if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 1, gm_srcs9q7ghsrcgv5, 258, 13);
            gm_b0_1 t224 = GM_LD(gm_b0_1, (((uint8_t *)&t223) + 1));
        }
        {
            void * t225 = (void *)((uint8_t *)&F.l30);
            gm_tcmkompbl09il t226;
            memset((uint8_t *)&t226 + 4, 0, 4);
            gm_t88rcucg1b7uk t227 = gm_l28;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t226 + 0, t227);
            gm_b16_8 t228;
            GM_ST(void *, (uint8_t *)&t228, (void *)(gm_shhr1hlq1ethc));
            GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&t226 + 8, t228);
            gm_tftb1odc739t5 t229 = gm_f_push_bpovr2k_2(t225, &t226);
            if (((((uint8_t *)&t229))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t229) + 1, gm_srcs9q7ghsrcgv5, 259, 13);
            gm_b0_1 t230 = GM_LD(gm_b0_1, (((uint8_t *)&t229) + 1));
        }
        {
            void * t231 = gm_l0;
            void * t232 = (void *)((uint8_t *)&F.l7);
            uint64_t t233 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t234 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t233 >= (uintptr_t)(t234)) gm_trap_index(t233, t234, gm_srcs9q7ghsrcgv5, 260, 37);
            uint64_t t235 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t233 * 8));
            gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
            gm_b0_1 t238 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t231, t232, t235, t237);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me23;
mn206:;
ma28:;
        uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t240 != ((uint8_t)17ULL)) goto mn239;
        {
            void * t242 = gm_l0;
            gm_t88rcucg1b7uk t243 = gm_l1;
            void * t244 = gm_f_adt_of_gej99vn(t242, GM_CONV(gm_csguankko4lmhd, t243));
            uint8_t t250 = GM_LD(uint8_t, ((((uint8_t *)t244) + 24) + 0));
            switch (t250) { case ((uint8_t)0ULL): goto ma246; case ((uint8_t)1ULL): goto ma248; case ((uint8_t)2ULL): goto ma247; default: goto ma249; }
ma246:;
            uint8_t t252 = GM_LD(uint8_t, ((((uint8_t *)t244) + 24) + 0));
            if (t252 != ((uint8_t)0ULL)) goto mn251;
            gm_tpautr4br7h3l t253 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t244) + 24) + 8));
            F.l31 = t253;
            {
                gm_f_list_epvducg((void *)&F.l32, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                    gm_l33 = t256;
                }
                {
                    gm_l34 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t258 = gm_l34;
                        uint64_t t259 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        uint8_t t260 = (t258 < t259);
                        if (!t260) goto lb257;
                        {
                            uint64_t t263 = gm_l34;
                            uint64_t t264 = (uintptr_t)t263;
                            uint64_t t265 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                            gm_t0mglksrkfa5e t268 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t264 * 40));
                            F.l35 = t268;
                            {
                                void * t270 = (void *)((uint8_t *)&F.l32);
                                gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&F.l35) + 0));
                                gm_tftb1odc739t5 t272 = gm_f_push_3ef746b(t270, t271);
                                if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 1, gm_srcs9q7ghsrcgv5, 266, 43);
                                gm_b0_1 t273 = GM_LD(gm_b0_1, (((uint8_t *)&t272) + 1));
                            }
                            goto me266;
mn267:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 266, 21);
me266:;
                        }
lc257:;
                        {
                            uint64_t t275 = gm_l34;
                            uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                            uint64_t t278 = r276;
                            gm_l34 = t278;
                        }
                    }
lb257:;
                }
            }
            {
                gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)&t279));
                void * t281 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t282 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t280, t281);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l36 = t282;
            }
            {
                {
                    gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                    gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                    gm_l37 = t285;
                }
                {
                    gm_l38 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t287 = gm_l38;
                        uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        uint8_t t289 = (t287 < t288);
                        if (!t289) goto lb286;
                        {
                            gm_t8ta482bl6be0 t292;
                            uint64_t t293 = gm_l38;
                            GM_ST(uint64_t, (uint8_t *)&t292 + 0, t293);
                            uint64_t t294 = gm_l38;
                            uint64_t t295 = (uintptr_t)t294;
                            uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            memcpy((uint8_t *)&t292 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t295 * 40), 40);
                            uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&t292) + 0));
                            gm_l39 = t299;
                            F.l40 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t292) + 8));
                            {
                                uint64_t t301 = gm_l39;
                                uint64_t t302 = (uintptr_t)t301;
                                uint64_t t303 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                if (t302 >= (uintptr_t)(t303)) gm_trap_index(t302, t303, gm_srcs9q7ghsrcgv5, 269, 33);
                                gm_th2lqbo77pvsc t304 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t302 * 24));
                                gm_b16_8 t305;
                                if (((((uint8_t *)&t304))[16] != 0)) {
                                    gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t304) + 0));
                                    t305 = t306;
                                } else {
                                    uint64_t t307 = gm_l39;
                                    gm_t4ce0jd3tdro4 t308;
                                    GM_ST(uint64_t, ((uint8_t *)&t308 + 0), t307);
                                    gm_tabb710d9mq2e t309;
                                    gm_f_format_tc7tn98((void *)&t309, (void *)NULL, cx, NULL, t308);
                                    if (gm_unw.kind) {
                                        goto gm_ll290;
                                    }
                                    if (((((uint8_t *)&t309))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t309) + 8, gm_srcs9q7ghsrcgv5, 269, 48);
                                    gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&t309) + 8));
                                    gm_b16_8 t311 = GM_LD(gm_b16_8, ((uint8_t *)&t310));
                                    t305 = t311;
                                }
                                gm_l41 = t305;
                            }
                            {
                                void * t312 = gm_l0;
                                void * t313 = (void *)((uint8_t *)&F.l7);
                                uint64_t t314 = gm_l39;
                                uint64_t t315 = (uintptr_t)t314;
                                uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                if (t315 >= (uintptr_t)(t316)) gm_trap_index(t315, t316, gm_srcs9q7ghsrcgv5, 270, 50);
                                uint64_t t317 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t315 * 8));
                                gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l40) + 16));
                                gm_b16_8 t319 = gm_l41;
                                gm_b0_1 t320 = gm_f_push_member_eubf7s6((void *)NULL, cx, NULL, t312, t313, t317, t318, t319);
                                if (gm_unw.kind) {
                                    goto gm_ll290;
                                }
                            }
                            goto me297;
mn298:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 268, 21);
me297:;
                        }
lc286:;
                        {
                            uint64_t t322 = gm_l38;
                            uint64_t r323; bool o324 = __builtin_add_overflow(t322, ((uint64_t)1ULL), &r323);
                            uint64_t t325 = r323;
                            gm_l38 = t325;
                        }
                    }
gm_ll290:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb286; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc286; }
                    }
                    goto gm_lad;
lb286:;
                }
            }
            goto me245;
mn251:;
ma247:;
            uint8_t t327 = GM_LD(uint8_t, ((((uint8_t *)t244) + 24) + 0));
            if (t327 != ((uint8_t)2ULL)) goto mn326;
            gm_b16_8 t328 = GM_LD(gm_b16_8, ((((uint8_t *)t244) + 24) + 8));
            gm_l42 = t328;
            {
                gm_f_list_epvducg((void *)&F.l43, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t331 = gm_l42;
                    gm_l44 = t331;
                }
                {
                    gm_l45 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t333 = gm_l45;
                        uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        uint8_t t335 = (t333 < t334);
                        if (!t335) goto lb332;
                        {
                            uint64_t t338 = gm_l45;
                            uint64_t t339 = (uintptr_t)t338;
                            uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                            gm_t0mglksrkfa5e t343 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t339 * 40));
                            F.l46 = t343;
                            {
                                void * t345 = (void *)((uint8_t *)&F.l43);
                                gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                                gm_tftb1odc739t5 t347 = gm_f_push_3ef746b(t345, t346);
                                if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 1, gm_srcs9q7ghsrcgv5, 275, 39);
                                gm_b0_1 t348 = GM_LD(gm_b0_1, (((uint8_t *)&t347) + 1));
                            }
                            goto me341;
mn342:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 275, 21);
me341:;
                        }
lc332:;
                        {
                            uint64_t t350 = gm_l45;
                            uint64_t r351; bool o352 = __builtin_add_overflow(t350, ((uint64_t)1ULL), &r351);
                            uint64_t t353 = r351;
                            gm_l45 = t353;
                        }
                    }
lb332:;
                }
            }
            {
                gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
                void * t356 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t357 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t355, t356);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l47 = t357;
            }
            {
                gm_f_list_58nr42v_2((void *)&F.l48, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t359 = gm_l42;
                    gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                    gm_l49 = t360;
                }
                {
                    gm_l50 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t362 = gm_l50;
                        uint64_t t363 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                        uint8_t t364 = (t362 < t363);
                        if (!t364) goto lb361;
                        {
                            gm_t8ta482bl6be0 t367;
                            uint64_t t368 = gm_l50;
                            GM_ST(uint64_t, (uint8_t *)&t367 + 0, t368);
                            uint64_t t369 = gm_l50;
                            uint64_t t370 = (uintptr_t)t369;
                            uint64_t t371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                            memcpy((uint8_t *)&t367 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t370 * 40), 40);
                            uint64_t t374 = GM_LD(uint64_t, (((uint8_t *)&t367) + 0));
                            gm_l51 = t374;
                            F.l52 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t367) + 8));
                            {
                                void * t376 = (void *)((uint8_t *)&F.l48);
                                gm_tcmkompbl09il t377;
                                memset((uint8_t *)&t377 + 4, 0, 4);
                                gm_t88rcucg1b7uk t378 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l52) + 16));
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t377 + 0, t378);
                                uint64_t t379 = gm_l51;
                                uint64_t t380 = (uintptr_t)t379;
                                uint64_t t381 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                                if (t380 >= (uintptr_t)(t381)) gm_trap_index(t380, t381, gm_srcs9q7ghsrcgv5, 279, 55);
                                gm_th2lqbo77pvsc t382 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t380 * 24));
                                gm_b16_8 t383;
                                if (((((uint8_t *)&t382))[16] != 0)) {
                                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&t382) + 0));
                                    t383 = t384;
                                } else {
                                    uint64_t t385 = gm_l51;
                                    gm_t4ce0jd3tdro4 t386;
                                    GM_ST(uint64_t, ((uint8_t *)&t386 + 0), t385);
                                    gm_tabb710d9mq2e t387;
                                    gm_f_format_tc7tn98((void *)&t387, (void *)NULL, cx, NULL, t386);
                                    if (gm_unw.kind) {
                                        goto gm_ll365;
                                    }
                                    if (((((uint8_t *)&t387))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t387) + 8, gm_srcs9q7ghsrcgv5, 279, 70);
                                    gm_b16_8 t388 = GM_LD(gm_b16_8, (((uint8_t *)&t387) + 8));
                                    gm_b16_8 t389 = GM_LD(gm_b16_8, ((uint8_t *)&t388));
                                    t383 = t389;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t377 + 8, t383);
                                gm_tftb1odc739t5 t390 = gm_f_push_bpovr2k_2(t376, &t377);
                                if (((((uint8_t *)&t390))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t390) + 1, gm_srcs9q7ghsrcgv5, 279, 25);
                                gm_b0_1 t391 = GM_LD(gm_b0_1, (((uint8_t *)&t390) + 1));
                            }
                            goto me372;
mn373:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 278, 21);
me372:;
                        }
lc361:;
                        {
                            uint64_t t393 = gm_l50;
                            uint64_t r394; bool o395 = __builtin_add_overflow(t393, ((uint64_t)1ULL), &r394);
                            uint64_t t396 = r394;
                            gm_l50 = t396;
                        }
                    }
gm_ll365:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb361; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc361; }
                    }
                    goto gm_lad;
lb361:;
                }
            }
            {
                void * t397 = gm_l0;
                void * t398 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t399 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 0));
                gm_b16_8 t400 = GM_LD(gm_b16_8, ((uint8_t *)&t399));
                gm_b0_1 t401 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t397, t398, ((uint64_t)0ULL), t400);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me245;
mn326:;
ma248:;
            uint8_t t403 = GM_LD(uint8_t, ((((uint8_t *)t244) + 24) + 0));
            if (t403 != ((uint8_t)1ULL)) goto mn402;
            gm_t88rcucg1b7uk t404 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t244) + 24) + 8) + 0));
            gm_l53 = t404;
            gm_b16_8 t405 = GM_LD(gm_b16_8, (((((uint8_t *)t244) + 24) + 8) + 8));
            gm_l54 = t405;
            {
                void * t407 = gm_l0;
                gm_t88rcucg1b7uk t408 = gm_l1;
                gm_b16_8 t409 = gm_l4;
                gm_b16_8 t410 = gm_f_define_enum_v780plg((void *)NULL, cx, NULL, t407, t408, t409);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l55 = t410;
            }
            {
                void * t412 = gm_l0;
                gm_t88rcucg1b7uk t413 = gm_l1;
                uint8_t t414 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t412, t413);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t415 = (uint8_t)!t414;
                if (!t415) goto ie411;
                {
                    gm_b0_1 t417;
                    uint8_t t418 = 0;
                    F.ret = t417;
                    goto gm_fret;
                }
                goto ix411;
ie411:;
ix411:;
            }
            {
                void * t419 = (void *)((uint8_t *)&F.l7);
                gm_tbccvikg6b96e t420;
                uint64_t t421 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t422 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t421 >= (uintptr_t)(t422)) gm_trap_index(t421, t422, gm_srcs9q7ghsrcgv5, 286, 53);
                uint64_t t423 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t421 * 8));
                GM_ST(uint64_t, (uint8_t *)&t420 + 0, t423);
                void * t424 = gm_l0;
                gm_t88rcucg1b7uk t425 = gm_l53;
                uint64_t t426 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t424, t425);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint64_t, (uint8_t *)&t420 + 8, t426);
                gm_b16_8 t427 = gm_l55;
                gm_tnpifv20irnph t428;
                GM_ST(gm_b16_8, ((uint8_t *)&t428 + 0), t427);
                gm_tabb710d9mq2e t429;
                gm_f_format_7nu71mn((void *)&t429, (void *)NULL, cx, NULL, t428);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t429))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t429) + 8, gm_srcs9q7ghsrcgv5, 286, 88);
                gm_b16_8 t430 = GM_LD(gm_b16_8, (((uint8_t *)&t429) + 8));
                gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&t430));
                GM_ST(gm_b16_8, (uint8_t *)&t420 + 16, t431);
                gm_tftb1odc739t5 t432 = gm_f_push_m7lva8q(t419, &t420);
                if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 1, gm_srcs9q7ghsrcgv5, 286, 21);
                gm_b0_1 t433 = GM_LD(gm_b0_1, (((uint8_t *)&t432) + 1));
            }
            {
                void * t434 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t435;
                GM_ST(void *, (uint8_t *)&t435, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t435 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t436 = gm_f_put_5c6qgui(t434, t435, ((uint8_t)1));
                if (((((uint8_t *)&t436))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t436) + 1, gm_srcs9q7ghsrcgv5, 287, 21);
                gm_b0_1 t437 = GM_LD(gm_b0_1, (((uint8_t *)&t436) + 1));
            }
            {
                gm_f_list_epvducg((void *)&F.l56, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t439 = gm_l54;
                    gm_l57 = t439;
                }
                {
                    gm_l58 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t441 = gm_l58;
                        uint64_t t442 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                        uint8_t t443 = (t441 < t442);
                        if (!t443) goto lb440;
                        {
                            uint64_t t446 = gm_l58;
                            uint64_t t447 = (uintptr_t)t446;
                            uint64_t t448 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                            gm_tckdobunaulee t451 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t447 * 72));
                            F.l59 = t451;
                            {
                                void * t453 = (void *)((uint8_t *)&F.l56);
                                gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&F.l59) + 0));
                                gm_tftb1odc739t5 t455 = gm_f_push_3ef746b(t453, t454);
                                if (((((uint8_t *)&t455))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t455) + 1, gm_srcs9q7ghsrcgv5, 289, 42);
                                gm_b0_1 t456 = GM_LD(gm_b0_1, (((uint8_t *)&t455) + 1));
                            }
                            goto me449;
mn450:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 289, 21);
me449:;
                        }
lc440:;
                        {
                            uint64_t t458 = gm_l58;
                            uint64_t r459; bool o460 = __builtin_add_overflow(t458, ((uint64_t)1ULL), &r459);
                            uint64_t t461 = r459;
                            gm_l58 = t461;
                        }
                    }
lb440:;
                }
            }
            {
                gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&F.l56) + 0));
                gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)&t462));
                void * t464 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t465 = gm_f_unique_view_members_3bvsjcm((void *)NULL, cx, NULL, t463, t464);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t465;
            }
            {
                gm_f_list_58nr42v_2((void *)&F.l61, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t467 = gm_l54;
                    gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
                    gm_l62 = t468;
                }
                {
                    gm_l63 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t470 = gm_l63;
                        uint64_t t471 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                        uint8_t t472 = (t470 < t471);
                        if (!t472) goto lb469;
                        {
                            gm_t3l8idb2m6c1r t475;
                            uint64_t t476 = gm_l63;
                            GM_ST(uint64_t, (uint8_t *)&t475 + 0, t476);
                            uint64_t t477 = gm_l63;
                            uint64_t t478 = (uintptr_t)t477;
                            uint64_t t479 = GM_LD(uintptr_t, ((uint8_t *)&gm_l62) + 8);
                            memcpy((uint8_t *)&t475 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l62)) + t478 * 72), 72);
                            uint64_t t482 = GM_LD(uint64_t, (((uint8_t *)&t475) + 0));
                            gm_l64 = t482;
                            F.l65 = (*(gm_tckdobunaulee *)(((uint8_t *)&t475) + 8));
                            {
                                gm_tkn42u7na3t81 t484 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l65) + 16));
                                gm_t88rcucg1b7uk t485;
                                if (((((uint8_t *)&t484))[4] != 0)) {
                                    gm_t88rcucg1b7uk t486 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t484) + 0));
                                    t485 = t486;
                                } else {
                                    goto lc469;
                                }
                                gm_l66 = t485;
                            }
                            {
                                void * t487 = (void *)((uint8_t *)&F.l61);
                                gm_tcmkompbl09il t488;
                                memset((uint8_t *)&t488 + 4, 0, 4);
                                gm_t88rcucg1b7uk t489 = gm_l66;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t488 + 0, t489);
                                uint64_t t490 = gm_l64;
                                uint64_t t491 = (uintptr_t)t490;
                                uint64_t t492 = GM_LD(uintptr_t, ((uint8_t *)&gm_l60) + 8);
                                if (t491 >= (uintptr_t)(t492)) gm_trap_index(t491, t492, gm_srcs9q7ghsrcgv5, 294, 52);
                                gm_th2lqbo77pvsc t493 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l60)) + t491 * 24));
                                gm_b16_8 t494;
                                if (((((uint8_t *)&t493))[16] != 0)) {
                                    gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&t493) + 0));
                                    t494 = t495;
                                } else {
                                    uint64_t t496 = gm_l64;
                                    gm_t4ce0jd3tdro4 t497;
                                    GM_ST(uint64_t, ((uint8_t *)&t497 + 0), t496);
                                    gm_tabb710d9mq2e t498;
                                    gm_f_format_0bd7vot((void *)&t498, (void *)NULL, cx, NULL, t497);
                                    if (gm_unw.kind) {
                                        goto gm_ll473;
                                    }
                                    if (((((uint8_t *)&t498))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t498) + 8, gm_srcs9q7ghsrcgv5, 294, 67);
                                    gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&t498) + 8));
                                    gm_b16_8 t500 = GM_LD(gm_b16_8, ((uint8_t *)&t499));
                                    t494 = t500;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t488 + 8, t494);
                                gm_tftb1odc739t5 t501 = gm_f_push_bpovr2k_2(t487, &t488);
                                if (((((uint8_t *)&t501))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t501) + 1, gm_srcs9q7ghsrcgv5, 294, 25);
                                gm_b0_1 t502 = GM_LD(gm_b0_1, (((uint8_t *)&t501) + 1));
                            }
                            goto me480;
mn481:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 292, 21);
me480:;
                        }
lc469:;
                        {
                            uint64_t t504 = gm_l63;
                            uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                            uint64_t t507 = r505;
                            gm_l63 = t507;
                        }
                    }
gm_ll473:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb469; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc469; }
                    }
                    goto gm_lad;
lb469:;
                }
            }
            {
                void * t508 = gm_l0;
                void * t509 = (void *)((uint8_t *)&F.l7);
                uint64_t t510 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t511 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t510 >= (uintptr_t)(t511)) gm_trap_index(t510, t511, gm_srcs9q7ghsrcgv5, 296, 45);
                uint64_t t512 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t510 * 8));
                gm_b16_8 t513 = GM_LD(gm_b16_8, (((uint8_t *)&F.l61) + 0));
                gm_b16_8 t514 = GM_LD(gm_b16_8, ((uint8_t *)&t513));
                gm_b0_1 t515 = gm_f_push_union_m2cqbk2((void *)NULL, cx, NULL, t508, t509, t512, t514);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me245;
mn402:;
ma249:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 263, 13);
me245:;
        }
        goto me23;
mn239:;
ma29:;
        gm_b0_1 t517;
        uint8_t t518 = 0;
        F.ret = t517;
        goto gm_fret;
        goto me23;
mn516:;
ma30:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 234, 5);
me23:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l67, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l68 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t520 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_l69 = t520;
        }
        {
            gm_l70 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t522 = gm_l70;
                uint64_t t523 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                uint8_t t524 = (t522 < t523);
                if (!t524) goto lb521;
                {
                    uint64_t t527 = gm_l70;
                    uint64_t t528 = (uintptr_t)t527;
                    uint64_t t529 = GM_LD(uintptr_t, ((uint8_t *)&gm_l69) + 8);
                    gm_tbccvikg6b96e t532 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l69)) + t528 * 32));
                    gm_l71 = t532;
                    {
                        void * t534 = (void *)((uint8_t *)&F.l67);
                        uint64_t t535 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 0));
                        uint64_t t536 = gm_l68;
                        uint64_t r537; bool o538 = __builtin_sub_overflow(t535, t536, &r537);
                        if (o538) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 305, 20);
                        uint64_t t539 = r537;
                        gm_b0_1 t540 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t534, t539);
                        if (gm_unw.kind) {
                            goto gm_ll525;
                        }
                    }
                    {
                        void * t541 = (void *)((uint8_t *)&F.l67);
                        gm_b16_8 t542 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 16));
                        gm_tnpifv20irnph t543;
                        GM_ST(gm_b16_8, ((uint8_t *)&t543 + 0), t542);
                        gm_tftb1odc739t5 t544 = gm_f_write_dqbiqk0(t541, t543);
                        if (((((uint8_t *)&t544))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t544) + 1, gm_srcs9q7ghsrcgv5, 306, 9);
                        gm_b0_1 t545 = GM_LD(gm_b0_1, (((uint8_t *)&t544) + 1));
                    }
                    {
                        uint64_t t546 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 0));
                        uint64_t t547 = GM_LD(uint64_t, (((uint8_t *)&gm_l71) + 8));
                        uint64_t r548; bool o549 = __builtin_add_overflow(t546, t547, &r548);
                        if (o549) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 307, 14);
                        uint64_t t550 = r548;
                        gm_l68 = t550;
                    }
                    goto me530;
mn531:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 304, 5);
me530:;
                }
lc521:;
                {
                    uint64_t t552 = gm_l70;
                    uint64_t r553; bool o554 = __builtin_add_overflow(t552, ((uint64_t)1ULL), &r553);
                    uint64_t t555 = r553;
                    gm_l70 = t555;
                }
            }
gm_ll525:;
            if (gm_unw.frame == &F.h && gm_unw.label == 9) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb521; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc521; }
            }
            goto gm_lad;
lb521:;
        }
    }
    {
        void * t556 = (void *)((uint8_t *)&F.l67);
        uint64_t t557 = gm_l5;
        uint64_t t558 = gm_l68;
        uint64_t r559; bool o560 = __builtin_sub_overflow(t557, t558, &r559);
        if (o560) gm_trap("integer overflow in `-`", gm_srcs9q7ghsrcgv5, 309, 16);
        uint64_t t561 = r559;
        gm_b0_1 t562 = gm_f_pad_juftt9c((void *)NULL, cx, NULL, t556, t561);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t563 = gm_l0;
        void * t564 = (void *)((((uint8_t *)t563) + 872) + 104);
        gm_t6j6smj7qoctu t565;
        gm_b16_8 t566 = gm_l4;
        gm_b16_8 t567 = gm_f_packed_v79gj1r((void *)NULL, cx, NULL, t566);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t565 + 0, t567);
        gm_b16_8 t568 = GM_LD(gm_b16_8, (((uint8_t *)&F.l67) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t565 + 16, t568);
        gm_tftb1odc739t5 t569 = gm_f_write_ue93873(t564, &t565);
        if (((((uint8_t *)&t569))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t569) + 1, gm_srcs9q7ghsrcgv5, 310, 5);
        gm_b0_1 t570 = GM_LD(gm_b0_1, (((uint8_t *)&t569) + 1));
    }
    {
        void * t571 = gm_l0;
        gm_b16_8 t572 = gm_l4;
        uint64_t t573 = gm_l5;
        gm_b0_1 t574 = gm_f_static_assert_15uo2jf((void *)NULL, cx, NULL, t571, t572, t573);
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
__attribute__((aligned(4))) void gm_f_enum_info_61n2nkp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_enum_info_61n2nkp F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* def */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    uint64_t gm_l4; /* size */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* base */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_ghvav22((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)17ULL)) goto ie2;
        {
            void * t8 = gm_l0;
            gm_t88rcucg1b7uk t9 = gm_l1;
            void * t10 = gm_f_adt_of_gej99vn(t8, GM_CONV(gm_csguankko4lmhd, t9));
            gm_l2 = t10;
        }
        {
            void * t12 = gm_l2;
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 24) + 0));
            if (t13 != ((uint8_t)1ULL)) goto ie11;
            gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t12) + 24) + 8) + 0));
            gm_l3 = t14;
            {
                void * t16 = gm_l0;
                gm_t88rcucg1b7uk t17 = gm_l3;
                uint64_t t18 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t18;
            }
            {
                uint64_t t20 = gm_l4;
                uint8_t t21 = (t20 > ((uint64_t)4ULL));
                if (!t21) goto ie19;
                {
                    gm_t4ml5v7ugcjrt t23;
                    memset(&t23, 0, sizeof t23);
                    uint8_t t24 = 0;
                    F.ret = t23;
                    goto gm_fret;
                }
                goto ix19;
ie19:;
ix19:;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t26 = gm_l5;
                    void * t27 = gm_l2;
                    uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)t27) + 0) + 8);
                    uint8_t t29 = (t26 < t28);
                    if (!t29) goto lb25;
                    uint8_t t30 = 0;
                    uint8_t t31 = 0;
                    uint8_t t32 = 0;
                    {
                        uint64_t t34 = gm_l5;
                        uint64_t t35 = (uintptr_t)t34;
                        void * t36 = gm_l2;
                        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 0) + 8);
                        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srcs9q7ghsrcgv5, 362, 33);
                        uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 0)) + t35 * 1));
                        gm_l6 = t38;
                    }
                    uint8_t t39 = 0;
                    uint8_t t40 = gm_l6;
                    uint8_t t41 = (((uint8_t)97ULL) <= t40);
                    if (t41) {
                        uint8_t t42 = gm_l6;
                        uint8_t t43 = (t42 <= ((uint8_t)122ULL));
                        t39 = t43;
                    } else t39 = 0;
                    uint8_t t44 = t39;
                    if (!t44) {
                        {
                            uint64_t t46 = gm_l5;
                            uint64_t t47 = (uintptr_t)t46;
                            void * t48 = gm_l2;
                            uint64_t t49 = GM_LD(uintptr_t, (((uint8_t *)t48) + 0) + 8);
                            if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcs9q7ghsrcgv5, 362, 61);
                            uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t48) + 0)) + t47 * 1));
                            gm_l7 = t50;
                        }
                        uint8_t t51 = 0;
                        uint8_t t52 = gm_l7;
                        uint8_t t53 = (((uint8_t)65ULL) <= t52);
                        if (t53) {
                            uint8_t t54 = gm_l7;
                            uint8_t t55 = (t54 <= ((uint8_t)90ULL));
                            t51 = t55;
                        } else t51 = 0;
                        uint8_t t56 = t51;
                        t32 = t56;
                    } else t32 = 1;
                    if (!t32) {
                        {
                            uint64_t t58 = gm_l5;
                            uint64_t t59 = (uintptr_t)t58;
                            void * t60 = gm_l2;
                            uint64_t t61 = GM_LD(uintptr_t, (((uint8_t *)t60) + 0) + 8);
                            if (t59 >= (uintptr_t)(t61)) gm_trap_index(t59, t61, gm_srcs9q7ghsrcgv5, 362, 89);
                            uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t60) + 0)) + t59 * 1));
                            gm_l8 = t62;
                        }
                        uint8_t t63 = 0;
                        uint8_t t64 = gm_l8;
                        uint8_t t65 = (((uint8_t)48ULL) <= t64);
                        if (t65) {
                            uint8_t t66 = gm_l8;
                            uint8_t t67 = (t66 <= ((uint8_t)57ULL));
                            t63 = t67;
                        } else t63 = 0;
                        uint8_t t68 = t63;
                        t31 = t68;
                    } else t31 = 1;
                    if (!t31) {
                        uint64_t t69 = gm_l5;
                        uint64_t t70 = (uintptr_t)t69;
                        void * t71 = gm_l2;
                        uint64_t t72 = GM_LD(uintptr_t, (((uint8_t *)t71) + 0) + 8);
                        if (t70 >= (uintptr_t)(t72)) gm_trap_index(t70, t72, gm_srcs9q7ghsrcgv5, 362, 117);
                        uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t71) + 0)) + t70 * 1));
                        uint8_t t74 = ((t73 == ((uint8_t)95ULL)));
                        t30 = t74;
                    } else t30 = 1;
                    if (!t30) goto lb25;
                    {
                        uint64_t t77 = gm_l5;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        if (o79) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 362, 139);
                        uint64_t t80 = r78;
                        gm_l5 = t80;
                    }
lc25:;
                }
lb25:;
            }
            {
                void * t81 = gm_l2;
                uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)t81) + 0) + 8);
                void * t83 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t81) + 0)));
                uint64_t t84 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t85 = gm_l5;
                uint64_t t86 = (uintptr_t)t85;
                if (t84 > t86 || t86 > (uintptr_t)t82) gm_trap_slice(t84, t86, t82, gm_srcs9q7ghsrcgv5, 363, 13);
                gm_b16_8 t87;
                GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t83 + t84 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(t86 - t84));
                gm_l9 = t87;
            }
            {
                gm_b16_8 t89 = gm_l9;
                gm_th2lqbo77pvsc t90;
                gm_f_member_name_mj2bvs3((void *)&t90, (void *)NULL, cx, NULL, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t91;
                if (((((uint8_t *)&t90))[16] != 0)) goto mn93;
                t91 = ((uint8_t)1);
                goto me92;
mn93:;
                t91 = ((uint8_t)0);
                goto me92;
mn94:;
                gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 364, 8);
me92:;
                if (!t91) goto ie88;
                {
                    gm_t4ml5v7ugcjrt t96;
                    memset(&t96, 0, sizeof t96);
                    uint8_t t97 = 0;
                    F.ret = t96;
                    goto gm_fret;
                }
                goto ix88;
ie88:;
ix88:;
            }
            {
                gm_th5c3ge61oapm t98;
                memset((uint8_t *)&t98 + 25, 0, 7);
                gm_b16_8 t99 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
                uint64_t t100 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t98 + 16, t100);
                void * t101 = gm_l0;
                gm_t88rcucg1b7uk t102 = gm_l3;
                uint8_t t103 = gm_f_is_signed_integer_ige9b74(t101, GM_CONV(gm_csguankko4lmhd, t102));
                GM_ST(uint8_t, (uint8_t *)&t98 + 24, t103);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t4ml5v7ugcjrt *)gm_out) + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t4ml5v7ugcjrt *)gm_out) + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t4ml5v7ugcjrt *)gm_out) + 36, 0);
                ((uint8_t *)&(*(gm_t4ml5v7ugcjrt *)gm_out))[32] = 1;
                GM_ST(gm_th5c3ge61oapm, (uint8_t *)&(*(gm_t4ml5v7ugcjrt *)gm_out) + 0, t98);
                goto gm_fdone;
            }
            goto ix11;
ie11:;
            {
                gm_t4ml5v7ugcjrt t105;
                memset(&t105, 0, sizeof t105);
                uint8_t t106 = 0;
                F.ret = t105;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
        {
            gm_t4ml5v7ugcjrt t108;
            memset(&t108, 0, sizeof t108);
            uint8_t t109 = 0;
            F.ret = t108;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) uint8_t gm_f_is_aggregate_buh1uic(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_aggregate_buh1uic F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* variants */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* p */
    GM_STACK_CHECK(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma9; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma9; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma9; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma9; case ((uint8_t)21ULL): goto ma9; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma9; case ((uint8_t)24ULL): goto ma9; case ((uint8_t)25ULL): goto ma9; case ((uint8_t)26ULL): goto ma9; default: goto ma9; }
ma6:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t15 != ((uint8_t)12ULL)) goto pa14;
        goto po13;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t17 != ((uint8_t)10ULL)) goto pa16;
        goto po13;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t19 != ((uint8_t)14ULL)) goto pa18;
        goto po13;
pa18:;
        goto mn12;
po13:;
        uint8_t t20 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me5;
mn12:;
ma7:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t22 != ((uint8_t)13ULL)) goto mn21;
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_t5v8n6qrn4rh4 t25;
        gm_f_repr_53quqki((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (!(GM_LD(uint8_t, ((uint8_t *)&t25) + 0) == ((uint8_t)3ULL)));
        uint8_t t27 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me5;
mn21:;
ma8:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t29 != ((uint8_t)17ULL)) goto mn28;
        {
            void * t31 = gm_l0;
            gm_t88rcucg1b7uk t32 = gm_l1;
            void * t33 = gm_f_adt_of_gej99vn(t31, GM_CONV(gm_csguankko4lmhd, t32));
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t33) + 24) + 0));
            switch (t39) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma36; default: goto ma38; }
ma35:;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t33) + 24) + 0));
            if (t41 != ((uint8_t)0ULL)) goto mn40;
            gm_tpautr4br7h3l t42 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t33) + 24) + 8));
            F.l2 = t42;
            uint8_t t43;
            if ((((((uint8_t *)&F.l2) + 40))[4] != 0)) goto mn45;
            t43 = ((uint8_t)1);
            goto me44;
mn45:;
            t43 = ((uint8_t)0);
            goto me44;
mn46:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 143, 40);
me44:;
            uint8_t t47 = 0;
            F.ret = t43;
            goto gm_fret;
            goto me34;
mn40:;
ma36:;
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t33) + 24) + 0));
            if (t49 != ((uint8_t)2ULL)) goto mn48;
            uint8_t t50 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
            goto me34;
mn48:;
ma37:;
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t33) + 24) + 0));
            if (t52 != ((uint8_t)1ULL)) goto mn51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((((uint8_t *)t33) + 24) + 8) + 8));
            gm_l3 = t53;
            {
                {
                    gm_b16_8 t56 = gm_l3;
                    gm_l4 = t56;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t58 = gm_l5;
                        uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t60 = (t58 < t59);
                        if (!t60) goto lb57;
                        {
                            uint64_t t63 = gm_l5;
                            uint64_t t64 = (uintptr_t)t63;
                            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            gm_tckdobunaulee t68 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t64 * 72));
                            F.l6 = t68;
                            {
                                if (!(((((uint8_t *)&F.l6) + 16))[4] != 0)) goto ie70;
                                gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                                gm_l7 = t71;
                                {
                                    void * t74 = gm_l0;
                                    gm_t88rcucg1b7uk t75 = gm_l7;
                                    uint64_t t76 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t74, t75);
                                    if (gm_unw.kind) {
                                        goto gm_ll61;
                                    }
                                    uint8_t t77 = (t76 > ((uint64_t)0ULL));
                                    if (!t77) goto ie73;
                                    {
                                        uint8_t t79 = 0;
                                        F.ret = ((uint8_t)1);
                                        goto gm_fret;
                                    }
                                    goto ix73;
ie73:;
ix73:;
                                }
                                goto ix70;
ie70:;
ix70:;
                            }
                            goto me66;
mn67:;
                            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 146, 21);
me66:;
                        }
lc57:;
                        {
                            uint64_t t81 = gm_l5;
                            uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                            uint64_t t84 = r82;
                            gm_l5 = t84;
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
                uint8_t t85 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto me34;
mn51:;
ma38:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 142, 13);
me34:;
        }
        goto me5;
mn28:;
ma9:;
        uint8_t t87 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me5;
mn86:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 137, 5);
me5:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_packed_v79gj1r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_packed_v79gj1r F;
    gm_b16_8 gm_l0; /* cty */
    gm_t6j6smj7qoctu gm_l1; /* sp */
    GM_STACK_CHECK(&F, 362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        gm_ttu09lij36f8c t5;
        gm_f_split_once_mqcu1ce((void *)&t5, t3, t4);
        if (!((((uint8_t *)&t5))[32] != 0)) goto ie2;
        gm_l1 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t5) + 0));
        {
            gm_t6j6smj7qoctu t7;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
            gm_tabb710d9mq2e t10;
            gm_f_format_v301q4v((void *)&t10, (void *)NULL, cx, NULL, &t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcs9q7ghsrcgv5, 431, 48);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t14 = gm_l0;
        gm_tnpifv20irnph t15;
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
        gm_tabb710d9mq2e t16;
        gm_f_format_ivs3cj0((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcs9q7ghsrcgv5, 432, 12);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_pad_juftt9c(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_pad_juftt9c F;
    void * gm_l0; /* body */
    uint64_t gm_l1; /* bytes */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l1;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 437, 17);
            uint64_t t4 = (uint64_t)(t3 / ((uint64_t)8ULL));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    {
                        void * t15 = gm_l0;
                        gm_b16_8 t16;
                        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sakkgen9m77d6));
                        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(15));
                        gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcs9q7ghsrcgv5, 437, 29);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 437, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l3 = t23;
                }
            }
lb5:;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t25 = gm_l1;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 438, 17);
            uint64_t t26 = (uint64_t)(t25 % ((uint64_t)8ULL));
            gm_l4 = t26;
        }
        {
            for (;;) {
                uint64_t t28 = gm_l5;
                uint64_t t29 = gm_l4;
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l5;
                    {
                        void * t37 = gm_l0;
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sumv38t63phct));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(13));
                        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcs9q7ghsrcgv5, 438, 29);
                        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 438, 5);
me34:;
                }
lc27:;
                {
                    uint64_t t42 = gm_l5;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l5 = t45;
                }
            }
lb27:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pointer_view_efkbf4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_pointer_view_efkbf4q F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    gm_t88rcucg1b7uk gm_l5; /* target */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t88rcucg1b7uk t5;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma9; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)8ULL)) goto mn13;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l2 = t15;
        gm_t88rcucg1b7uk t16 = gm_l2;
        t5 = t16;
        goto me6;
mn13:;
ma8:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t18 != ((uint8_t)9ULL)) goto mn17;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l3 = t19;
        gm_t88rcucg1b7uk t20 = gm_l3;
        t5 = t20;
        goto me6;
mn17:;
ma9:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t22 != ((uint8_t)13ULL)) goto mn21;
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
        gm_l4 = t23;
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l4;
        gm_b16_8 t26 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t27 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me6;
mn21:;
ma10:;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s44ek1gecbajs));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(6));
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
        goto me6;
mn28:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 194, 18);
me6:;
        gm_l5 = t5;
    }
    {
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l5;
        gm_b16_8 t33 = gm_f_target_view_ams6b9s((void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
        gm_tabb710d9mq2e t35;
        gm_f_format_lfjb9aq((void *)&t35, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcs9q7ghsrcgv5, 200, 12);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_member_eubf7s6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_push_member_eubf7s6 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* name */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 407u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_ttu09lij36f8c t4;
        gm_f_view_qkqm3in((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t5;
        if (((((uint8_t *)&t4))[32] != 0)) {
            t5 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t4) + 0));
        } else {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l5 = t5;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9 = gm_l4;
        gm_b0_1 t10 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l1;
        gm_tbccvikg6b96e t12;
        uint64_t t13 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l3;
        uint64_t t16 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t12 + 8, t16);
        gm_tfeab279c16rr t17;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 0, t18);
        gm_b16_8 t19 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 16, t19);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t17 + 32, t20);
        gm_tabb710d9mq2e t21;
        gm_f_format_hn5gv7p((void *)&t21, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcs9q7ghsrcgv5, 324, 69);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 16, t23);
        gm_tftb1odc739t5 t24 = gm_f_push_m7lva8q(t11, &t12);
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcs9q7ghsrcgv5, 324, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_union_m2cqbk2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_push_union_m2cqbk2 F;
    void * gm_l0; /* ty */
    void * gm_l1; /* members */
    uint64_t gm_l2; /* offset */
    gm_b16_8 gm_l3; /* arms */
    uint64_t gm_l5; /* size */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tcmkompbl09il gm_l8; /* arm */
    gm_t6j6smj7qoctu gm_l9; /* v */
    GM_STACK_CHECK(&F, 630u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
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
                    gm_tcmkompbl09il t15 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t11 * 24));
                    gm_l8 = t15;
                    {
                        void * t17 = gm_l0;
                        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
                        gm_ttu09lij36f8c t19;
                        gm_f_view_qkqm3in((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_t6j6smj7qoctu t20;
                        if (((((uint8_t *)&t19))[32] != 0)) {
                            t20 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t19) + 0));
                        } else {
                            goto lc4;
                        }
                        gm_l9 = t20;
                    }
                    {
                        void * t21 = gm_l0;
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b0_1 t23 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t24 = (void *)((uint8_t *)&F.l4);
                        gm_tfeab279c16rr t25;
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t28);
                        gm_tftb1odc739t5 t29 = gm_f_write_vfiknt5(t24, &t25);
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcs9q7ghsrcgv5, 334, 9);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    {
                        uint64_t t31 = gm_l5;
                        void * t32 = gm_l0;
                        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 0));
                        uint64_t t34 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint64_t t35 = (t31 > t34 ? t31 : t34);
                        gm_l5 = t35;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 331, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t37 = gm_l7;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l7 = t40;
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
        uint64_t t42 = gm_l5;
        uint8_t t43 = (t42 > ((uint64_t)0ULL));
        if (!t43) goto ie41;
        {
            void * t45 = gm_l1;
            gm_tbccvikg6b96e t46;
            uint64_t t47 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
            uint64_t t48 = gm_l5;
            GM_ST(uint64_t, (uint8_t *)&t46 + 8, t48);
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_tnpifv20irnph t50;
            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
            gm_tabb710d9mq2e t51;
            gm_f_format_phh9f1s((void *)&t51, (void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srcs9q7ghsrcgv5, 338, 67);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t53);
            gm_tftb1odc739t5 t54 = gm_f_push_m7lva8q(t45, &t46);
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcs9q7ghsrcgv5, 338, 9);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        goto ix41;
ie41:;
ix41:;
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
inline __attribute__((aligned(4))) void gm_f_reserve_40g0de6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_reserve_40g0de6 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* cty */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_b16_8 t5 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t6 = gm_f_view_number_29hi30b((void *)NULL, cx, NULL, t2, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t6;
    }
    {
        gm_b16_8 t7;
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        uint8_t t11 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t11) goto ie8;
        gm_tf1bjg81u0glv t13;
        uint64_t t14 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
        void * t15 = gm_l0;
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_b16_8 t17 = gm_f_view_kind_pth6nol((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 8, t17);
        gm_tabb710d9mq2e t18;
        gm_f_format_08mmoe2((void *)&t18, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcs9q7ghsrcgv5, 186, 43);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_b16_8 t20 = t19;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        t7 = t21;
        goto ix8;
ie8:;
        uint64_t t23 = gm_l2;
        gm_t4ce0jd3tdro4 t24;
        GM_ST(uint64_t, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_8ibn6ei((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcs9q7ghsrcgv5, 186, 104);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_b16_8 t27 = t26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        t7 = t28;
ix8:;
        gm_l3 = t7;
    }
    {
        gm_b16_8 t29 = gm_l3;
        gm_f_scalar_f0e206b((void *)&F.l4, t29);
    }
    {
        void * t30 = gm_l0;
        void * t31 = (void *)((((uint8_t *)t30) + 872) + 0);
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t33 = gm_f_put_25ngckq(t31, t32, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l4)));
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcs9q7ghsrcgv5, 188, 5);
        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
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
extern __attribute__((aligned(4))) void gm_f_scalar_f0e206b(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttu09lij36f8c gm_f_scalar_f0e206b_c(gm_b16_8 a0) { gm_ttu09lij36f8c gm_r; gm_f_scalar_f0e206b(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_static_assert_15uo2jf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_view_ams6b9s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_target_view_ams6b9s F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t6j6smj7qoctu gm_l3; /* v */
    gm_t6j6smj7qoctu gm_l4; /* v */
    gm_t6j6smj7qoctu gm_l5; /* v */
    GM_STACK_CHECK(&F, 588u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((((uint8_t *)t3) + 872) + 0);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t6;
        gm_f_get_v0dhnfj((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[40] != 0)) goto ie2;
        F.l2 = (*(gm_ttu09lij36f8c *)(((uint8_t *)&t6) + 0));
        {
            if (!((((uint8_t *)&F.l2))[32] != 0)) goto ie8;
            gm_t6j6smj7qoctu t9 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&F.l2));
            gm_l3 = t9;
            {
                uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 16) + 8);
                uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
                if (!t13) goto ie11;
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s557bv2sfqcom));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
                uint8_t t16 = gm_f_starts_with_6vvt01e(t14, t15);
                uint8_t t17 = (uint8_t)!t16;
                if (!t17) goto ie11;
                {
                    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t20 = 0;
                    F.ret = t19;
                    goto gm_fret;
                }
                goto ix11;
ie11:;
ix11:;
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t24 = gm_l0;
        gm_t88rcucg1b7uk t25 = gm_l1;
        uint64_t t26 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        if (!t27) goto ie23;
        {
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t32 = gm_l0;
        gm_t88rcucg1b7uk t33 = gm_l1;
        uint8_t t34 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t34) goto ie31;
        {
            void * t36 = gm_l0;
            gm_t88rcucg1b7uk t37 = gm_l1;
            gm_ttu09lij36f8c t38;
            gm_f_reserve_40g0de6((void *)&t38, (void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t38))[32] != 0)) gm_trap("unwrap of null", gm_srcs9q7ghsrcgv5, 214, 14);
            gm_l4 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t38) + 0));
        }
        {
            void * t39 = gm_l0;
            void * t40 = (void *)((((uint8_t *)t39) + 872) + 56);
            gm_t88rcucg1b7uk t41 = gm_l1;
            gm_tftb1odc739t5 t42 = gm_f_push_k3l9nic(t40, GM_CONV(gm_csguankko4lmhd, t41));
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcs9q7ghsrcgv5, 215, 9);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        {
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t47 = gm_l0;
        gm_t88rcucg1b7uk t48 = gm_l1;
        gm_ttu09lij36f8c t49;
        gm_f_view_qkqm3in((void *)&t49, (void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t49))[32] != 0)) goto ie46;
        gm_l5 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t49) + 0));
        {
            uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 16) + 8);
            uint8_t t53 = ((t52 == ((uint64_t)0ULL)));
            if (!t53) goto ie51;
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s557bv2sfqcom));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
            uint8_t t56 = gm_f_starts_with_6vvt01e(t54, t55);
            uint8_t t57 = (uint8_t)!t56;
            if (!t57) goto ie51;
            {
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            goto ix51;
ie51:;
ix51:;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_view_defs_u461okc(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_view_kind_pth6nol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_kind_pth6nol F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_ghvav22((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)10ULL)) goto mn13;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l2 = t15;
        {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l2;
            gm_t53at80pfdus3 t20;
            gm_f_kind_ghvav22((void *)&t20, t18, GM_CONV(gm_csguankko4lmhd, t19));
            uint8_t t21;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
            if (t24 != ((uint8_t)0ULL)) goto mn23;
            uint16_t t25 = GM_LD(uint16_t, ((((uint8_t *)&t20) + 8) + 2));
            if (!(t25 == ((uint16_t)8ULL))) goto mn23;
            t21 = ((uint8_t)1);
            goto me22;
mn23:;
            t21 = ((uint8_t)0);
            goto me22;
mn26:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 164, 16);
me22:;
            if (!t21) goto ie17;
            {
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s3p17h7dbk8us));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(7));
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_stntm4cqqrhf7));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(6));
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        goto me5;
mn13:;
ma7:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t33 != ((uint8_t)13ULL)) goto mn32;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_smg3p90in08sb));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(9));
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me5;
mn32:;
ma8:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t37 != ((uint8_t)14ULL)) goto mn36;
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_si68bogf7eu32));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me5;
mn36:;
ma9:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t41 != ((uint8_t)17ULL)) goto mn40;
        gm_b16_8 t42;
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = gm_l1;
        gm_t4ml5v7ugcjrt t46;
        gm_f_enum_info_61n2nkp((void *)&t46, (void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t47;
        if (((((uint8_t *)&t46))[32] != 0)) goto mn49;
        t47 = ((uint8_t)0);
        goto me48;
mn49:;
        t47 = ((uint8_t)1);
        goto me48;
mn50:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 169, 33);
me48:;
        if (!t47) goto ie43;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s4v09sn49l08n));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(6));
        gm_b16_8 t53 = t52;
        t42 = t53;
        goto ix43;
ie43:;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(0));
        gm_b16_8 t56 = t55;
        t42 = t56;
ix43:;
        uint8_t t57 = 0;
        F.ret = t42;
        goto gm_fret;
        goto me5;
mn40:;
ma10:;
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(0));
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me5;
mn58:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 162, 5);
me5:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_view_number_29hi30b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_view_number_29hi30b F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* key */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l1;
        uint64_t t3 = gm_f_hash64_apkb7bo(t2);
        if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 177, 10);
        uint64_t t4 = (uint64_t)(t3 % ((uint64_t)1000000000000ULL));
        gm_l2 = t4;
    }
    {
        for (;;) {
            void * t6 = gm_l0;
            void * t7 = (void *)((((uint8_t *)t6) + 872) + 144);
            uint64_t t8 = gm_l2;
            gm_ttsm01t0d8srg t9 = gm_f_get_fq78978(t7, t8);
            uint8_t t10;
            if (((((uint8_t *)&t9))[0] != 0)) goto mn12;
            t10 = ((uint8_t)0);
            goto me11;
mn12:;
            t10 = ((uint8_t)1);
            goto me11;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 178, 11);
me11:;
            if (!t10) goto lb5;
            {
                uint64_t t16 = gm_l2;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_srcs9q7ghsrcgv5, 178, 54);
                uint64_t t19 = r17;
                if (((uint64_t)1000000000000ULL) == 0) gm_trap("division by zero", gm_srcs9q7ghsrcgv5, 178, 53);
                uint64_t t20 = (uint64_t)(t19 % ((uint64_t)1000000000000ULL));
                gm_l2 = t20;
            }
lc5:;
        }
lb5:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)((((uint8_t *)t21) + 872) + 144);
        uint64_t t23 = gm_l2;
        gm_b0_1 t24;
        gm_tftb1odc739t5 t25 = gm_f_put_00pon0c(t22, t23, t24);
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcs9q7ghsrcgv5, 179, 5);
        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
    }
    {
        uint64_t t27 = gm_l2;
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_view_qkqm3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_view_qkqm3in F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    gm_t88rcucg1b7uk gm_l6; /* p */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t6j6smj7qoctu gm_l10; /* e */
    GM_STACK_CHECK(&F, 1813u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)((((uint8_t *)t3) + 872) + 0);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tu3261i9p5758 t6;
        gm_f_get_v0dhnfj((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[40] != 0)) goto ie2;
        F.l2 = (*(gm_ttu09lij36f8c *)(((uint8_t *)&t6) + 0));
        {
            {
                void * t9 = gm_l0;
                gm_b16_8 t10 = GM_LD(gm_b16_8, (((((uint8_t *)t9) + 872) + 56) + 0));
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
                gm_l3 = t11;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t15 = (t13 < t14);
                    if (!t15) goto lb12;
                    {
                        uint64_t t18 = gm_l4;
                        uint64_t t19 = gm_l4;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 4));
                        gm_tlcm4lvu3g1a8 t23;
                        GM_ST(uint32_t, (uint8_t *)&t23 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t23 + 0), t18);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23 + 8), t22);
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t23) + 0));
                        gm_l5 = t26;
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t23) + 8));
                        gm_l6 = t27;
                        {
                            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
                            uint8_t t32 = ((t30 == t31));
                            if (!t32) goto ie29;
                            {
                                {
                                    void * t35 = gm_l0;
                                    void * t36 = (void *)((((uint8_t *)t35) + 872) + 56);
                                    uint64_t t37 = gm_l5;
                                    gm_t88rcucg1b7uk t38 = GM_CONV(gm_t88rcucg1b7uk, gm_f_remove_6blu1gh(t36, t37));
                                }
                            }
                            {
                                void * t39 = gm_l0;
                                gm_t88rcucg1b7uk t40 = gm_l1;
                                gm_b0_1 t41 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_ll16;
                                }
                            }
                            {
                                goto lb12;
                            }
                            goto ix29;
ie29:;
ix29:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 89, 9);
me24:;
                    }
lc12:;
                    {
                        uint64_t t43 = gm_l4;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l4 = t46;
                    }
                }
gm_ll16:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
                }
                goto gm_lad;
lb12:;
            }
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l2), 40);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t48 = gm_l0;
        gm_t88rcucg1b7uk t49 = gm_l1;
        uint64_t t50 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
        if (!t51) goto ie47;
        {
            void * t53 = gm_l0;
            void * t54 = (void *)((((uint8_t *)t53) + 872) + 0);
            uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            gm_ttu09lij36f8c t56;
            memset(&t56, 0, sizeof t56);
            gm_tftb1odc739t5 t57 = gm_f_put_25ngckq(t54, t55, &t56);
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcs9q7ghsrcgv5, 99, 9);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        {
            gm_ttu09lij36f8c t59;
            memset(&t59, 0, sizeof t59);
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        void * t61 = gm_l0;
        gm_t88rcucg1b7uk t62 = gm_l1;
        gm_t5v8n6qrn4rh4 t63;
        gm_f_repr_53quqki((void *)&t63, (void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t64;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        switch (t71) { case ((uint8_t)0ULL): goto ma69; case ((uint8_t)1ULL): goto ma69; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma67; case ((uint8_t)4ULL): goto ma69; case ((uint8_t)5ULL): goto ma69; case ((uint8_t)6ULL): goto ma68; default: goto ma69; }
ma66:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t73 != ((uint8_t)2ULL)) goto mn72;
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_snokcvhtidt97));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(5));
        gm_ttu09lij36f8c t75;
        gm_f_scalar_f0e206b((void *)&t75, t74);
        t64 = t75;
        goto me65;
mn72:;
ma67:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t77 != ((uint8_t)3ULL)) goto mn76;
        void * t78 = gm_l0;
        gm_t88rcucg1b7uk t79 = gm_l1;
        gm_b16_8 t80 = gm_f_pointer_view_efkbf4q((void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t81;
        gm_f_scalar_f0e206b((void *)&t81, t80);
        t64 = t81;
        goto me65;
mn76:;
ma68:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t83 != ((uint8_t)6ULL)) goto mn82;
        gm_ttu09lij36f8c t85;
        void * t87 = gm_l0;
        gm_t88rcucg1b7uk t88 = gm_l1;
        uint8_t t89 = gm_f_is_aggregate_buh1uic((void *)NULL, cx, NULL, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t89) goto ie86;
        {
            void * t91 = gm_l0;
            gm_t88rcucg1b7uk t92 = gm_l1;
            gm_f_reserve_40g0de6((void *)&F.l7, (void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l1;
            gm_b0_1 t95 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ttu09lij36f8c t96 = F.l7;
        gm_ttu09lij36f8c t97 = t96;
        t85 = t97;
        goto ix86;
ie86:;
        void * t99 = gm_l0;
        gm_t88rcucg1b7uk t100 = gm_l1;
        gm_t53at80pfdus3 t101;
        gm_f_kind_ghvav22((void *)&t101, t99, GM_CONV(gm_csguankko4lmhd, t100));
        gm_ttu09lij36f8c t102;
        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
        switch (t108) { case ((uint8_t)0ULL): goto ma106; case ((uint8_t)1ULL): goto ma106; case ((uint8_t)2ULL): goto ma106; case ((uint8_t)3ULL): goto ma106; case ((uint8_t)4ULL): goto ma106; case ((uint8_t)5ULL): goto ma106; case ((uint8_t)6ULL): goto ma106; case ((uint8_t)7ULL): goto ma106; case ((uint8_t)8ULL): goto ma106; case ((uint8_t)9ULL): goto ma106; case ((uint8_t)10ULL): goto ma106; case ((uint8_t)11ULL): goto ma104; case ((uint8_t)12ULL): goto ma106; case ((uint8_t)13ULL): goto ma106; case ((uint8_t)14ULL): goto ma106; case ((uint8_t)15ULL): goto ma106; case ((uint8_t)16ULL): goto ma106; case ((uint8_t)17ULL): goto ma105; case ((uint8_t)18ULL): goto ma106; case ((uint8_t)19ULL): goto ma106; case ((uint8_t)20ULL): goto ma106; case ((uint8_t)21ULL): goto ma106; case ((uint8_t)22ULL): goto ma106; case ((uint8_t)23ULL): goto ma106; case ((uint8_t)24ULL): goto ma106; case ((uint8_t)25ULL): goto ma106; case ((uint8_t)26ULL): goto ma106; default: goto ma106; }
ma104:;
        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
        if (t110 != ((uint8_t)11ULL)) goto mn109;
        uint64_t t111 = GM_LD(uint64_t, ((((uint8_t *)&t101) + 8) + 0));
        gm_l8 = t111;
        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t101) + 8) + 8));
        gm_l9 = t112;
        gm_ttu09lij36f8c t114;
        void * t116 = gm_l0;
        gm_t88rcucg1b7uk t117 = gm_l9;
        gm_ttu09lij36f8c t118;
        gm_f_view_qkqm3in((void *)&t118, (void *)NULL, cx, NULL, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t118))[32] != 0)) goto ie115;
        gm_l10 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t118) + 0));
        gm_t6j6smj7qoctu t120;
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
        gm_tf1bjg81u0glv t122;
        uint64_t t123 = gm_l8;
        GM_ST(uint64_t, (uint8_t *)&t122 + 0, t123);
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t122 + 8, t124);
        gm_tabb710d9mq2e t125;
        gm_f_format_6a91mh8((void *)&t125, (void *)NULL, cx, NULL, &t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 8, gm_srcs9q7ghsrcgv5, 114, 52);
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t125) + 8));
        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
        GM_ST(gm_b16_8, (uint8_t *)&t120 + 16, t127);
        gm_t6j6smj7qoctu t128 = t120;
        gm_ttu09lij36f8c t129;
        GM_ST(uint8_t, (uint8_t *)&t129 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t129 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t129 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t129), t128);
        ((uint8_t *)&t129)[32] = 1;
        t114 = t129;
        goto ix115;
ie115:;
        void * t131 = gm_l0;
        gm_t88rcucg1b7uk t132 = gm_l1;
        gm_b16_8 t133 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t131, t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t134;
        gm_f_scalar_f0e206b((void *)&t134, t133);
        gm_ttu09lij36f8c t135 = t134;
        t114 = t135;
ix115:;
        gm_ttu09lij36f8c t136 = t114;
        t102 = t136;
        goto me103;
mn109:;
ma105:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t101) + 0));
        if (t138 != ((uint8_t)17ULL)) goto mn137;
        void * t139 = gm_l0;
        gm_t88rcucg1b7uk t140 = gm_l1;
        gm_t4ml5v7ugcjrt t141;
        gm_f_enum_info_61n2nkp((void *)&t141, (void *)NULL, cx, NULL, t139, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t142;
        if (((((uint8_t *)&t141))[32] != 0)) goto mn144;
        t142 = ((uint8_t)0);
        goto me143;
mn144:;
        t142 = ((uint8_t)1);
        goto me143;
mn145:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 119, 35);
me143:;
        if (!t142) goto mn137;
        {
            void * t147 = gm_l0;
            gm_t88rcucg1b7uk t148 = gm_l1;
            gm_f_reserve_40g0de6((void *)&F.l11, (void *)NULL, cx, NULL, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t149 = gm_l0;
            gm_t88rcucg1b7uk t150 = gm_l1;
            gm_b0_1 t151 = gm_f_define_view_sob497j((void *)NULL, cx, NULL, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_ttu09lij36f8c t152 = F.l11;
        gm_ttu09lij36f8c t153 = t152;
        t102 = t153;
        goto me103;
mn137:;
ma106:;
        void * t155 = gm_l0;
        gm_t88rcucg1b7uk t156 = gm_l1;
        gm_b16_8 t157 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t155, t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t158;
        gm_f_scalar_f0e206b((void *)&t158, t157);
        t102 = t158;
        goto me103;
mn154:;
ma107:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 111, 17);
me103:;
        gm_ttu09lij36f8c t159 = t102;
        t85 = t159;
ix86:;
        gm_ttu09lij36f8c t160 = t85;
        t64 = t160;
        goto me65;
mn82:;
ma69:;
        void * t162 = gm_l0;
        gm_t88rcucg1b7uk t163 = gm_l1;
        gm_b16_8 t164 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t162, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t165;
        gm_f_scalar_f0e206b((void *)&t165, t164);
        t64 = t165;
        goto me65;
mn161:;
ma70:;
        gm_trap("no match arm matched the scrutinee", gm_srcs9q7ghsrcgv5, 102, 16);
me65:;
        F.l12 = t64;
    }
    {
        void * t166 = gm_l0;
        void * t167 = (void *)((((uint8_t *)t166) + 872) + 0);
        uint32_t t168 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t169 = gm_f_put_25ngckq(t167, t168, &(*(gm_ttu09lij36f8c *)((uint8_t *)&F.l12)));
        if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srcs9q7ghsrcgv5, 131, 5);
        gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l12), 40);
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
inline __attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_views_bc1b3r4 F;
    GM_STACK_CHECK(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 97, 0, 7);
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_9mnq5mu((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_nof7vfj((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_uvfu1k1((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_5kr0r9p((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_cuh4fs9((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 56, t2);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 96, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 104, t3);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 144, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_tbpdf5ej15iof *)gm_out) + 200, t5);
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
