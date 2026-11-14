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
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; } gm_t8d5d2ep1fo6t;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; gm_t9qkh0rsooc8d f10; gm_t9qkh0rsooc8d f11; gm_t9qkh0rsooc8d f12; gm_t9qkh0rsooc8d f13; gm_t9qkh0rsooc8d f14; gm_t9qkh0rsooc8d f15; gm_t9qkh0rsooc8d f16; gm_t9qkh0rsooc8d f17; } gm_ta8d7579em356;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_tbgd14esf0jvi a[2]; } gm_t0vh9i8m50f41;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8d5d2ep1fo6t) == 4 && _Alignof(gm_t8d5d2ep1fo6t) == 1 && offsetof(gm_t8d5d2ep1fo6t, f0) == 0 && offsetof(gm_t8d5d2ep1fo6t, f1) == 1 && offsetof(gm_t8d5d2ep1fo6t, f2) == 2 && offsetof(gm_t8d5d2ep1fo6t, f3) == 3, "layout of `(Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_ta8d7579em356) == 18 && _Alignof(gm_ta8d7579em356) == 1 && offsetof(gm_ta8d7579em356, f0) == 0 && offsetof(gm_ta8d7579em356, f1) == 1 && offsetof(gm_ta8d7579em356, f2) == 2 && offsetof(gm_ta8d7579em356, f3) == 3 && offsetof(gm_ta8d7579em356, f4) == 4 && offsetof(gm_ta8d7579em356, f5) == 5 && offsetof(gm_ta8d7579em356, f6) == 6 && offsetof(gm_ta8d7579em356, f7) == 7 && offsetof(gm_ta8d7579em356, f8) == 8 && offsetof(gm_ta8d7579em356, f9) == 9 && offsetof(gm_ta8d7579em356, f10) == 10 && offsetof(gm_ta8d7579em356, f11) == 11 && offsetof(gm_ta8d7579em356, f12) == 12 && offsetof(gm_ta8d7579em356, f13) == 13 && offsetof(gm_ta8d7579em356, f14) == 14 && offsetof(gm_ta8d7579em356, f15) == 15 && offsetof(gm_ta8d7579em356, f16) == 16 && offsetof(gm_ta8d7579em356, f17) == 17, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; } gm_tt7vn9ehj0acr;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t0vh9i8m50f41) == 64 && _Alignof(gm_t0vh9i8m50f41) == 8 && offsetof(gm_t0vh9i8m50f41, a) == 0, "layout of `[2]Edit`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; } gm_t23tohc57ebhi;
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; } gm_t41cj6gvba3r8;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; } gm_t72hb7duiv9b4;
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; gm_t3fcbuehh3bs2 f4; } gm_tak1k6bk5qcen;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tt7vn9ehj0acr) == 3 && _Alignof(gm_tt7vn9ehj0acr) == 1 && offsetof(gm_tt7vn9ehj0acr, f0) == 0 && offsetof(gm_tt7vn9ehj0acr, f1) == 2, "layout of `?BinOp`");
_Static_assert(sizeof(gm_t23tohc57ebhi) == 64 && _Alignof(gm_t23tohc57ebhi) == 8 && offsetof(gm_t23tohc57ebhi, f0) == 0 && offsetof(gm_t23tohc57ebhi, f1) == 32, "layout of `(TokenKind, TokenKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t41cj6gvba3r8) == 96 && _Alignof(gm_t41cj6gvba3r8) == 8 && offsetof(gm_t41cj6gvba3r8, f0) == 0 && offsetof(gm_t41cj6gvba3r8, f1) == 32 && offsetof(gm_t41cj6gvba3r8, f2) == 64, "layout of `(TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_t72hb7duiv9b4) == 128 && _Alignof(gm_t72hb7duiv9b4) == 8 && offsetof(gm_t72hb7duiv9b4, f0) == 0 && offsetof(gm_t72hb7duiv9b4, f1) == 32 && offsetof(gm_t72hb7duiv9b4, f2) == 64 && offsetof(gm_t72hb7duiv9b4, f3) == 96, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tak1k6bk5qcen) == 160 && _Alignof(gm_tak1k6bk5qcen) == 8 && offsetof(gm_tak1k6bk5qcen, f0) == 0 && offsetof(gm_tak1k6bk5qcen, f1) == 32 && offsetof(gm_tak1k6bk5qcen, f2) == 64 && offsetof(gm_tak1k6bk5qcen, f3) == 96 && offsetof(gm_tak1k6bk5qcen, f4) == 128, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_tao63pk3r18v5 v1; } f1; } gm_t1vei8bd9e9m7;
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
_Static_assert(sizeof(gm_t1vei8bd9e9m7) == 96 && _Alignof(gm_t1vei8bd9e9m7) == 8 && offsetof(gm_t1vei8bd9e9m7, f0) == 0 && offsetof(gm_t1vei8bd9e9m7, f1) == 8, "layout of `Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t1vei8bd9e9m7 v1; } f1; } gm_tb1kjfin6jnc2;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tb1kjfin6jnc2) == 104 && _Alignof(gm_tb1kjfin6jnc2) == 8 && offsetof(gm_tb1kjfin6jnc2, f0) == 0 && offsetof(gm_tb1kjfin6jnc2, f1) == 8, "layout of `Abort!Subscript`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7te2tdpebbt4 v1; } f1; } gm_t3q68r4ne5jv8;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
_Static_assert(sizeof(gm_t3q68r4ne5jv8) == 152 && _Alignof(gm_t3q68r4ne5jv8) == 8 && offsetof(gm_t3q68r4ne5jv8, f0) == 0 && offsetof(gm_t3q68r4ne5jv8, f1) == 8, "layout of `Abort!Arg`");
struct gm_fr_add_from_7os0rvi {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_add_op_99op354 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_ambiguous_try_else_0v14t0i {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_t0vh9i8m50f41 l5; /* <temporary array> */
    gm_t0vh9i8m50f41 l6; /* <temporary array> */
    gm_b0_1 ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_reported_kw_name_it872te {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_binary_41fr5r5 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_binary_follows_jeb42bi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bit_from_pu2bq19 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tm4pc0emhrv1g l5; /* t */
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tc3oht4ksth9d l12; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_bit_op_p81j9u3 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_can_begin_expr_rmia9bs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_can_begin_type_a3rjrm7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cmp_op_9gj7im4 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_deny_in_head_06d9o22 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_eat_label_7eokqmm {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_8ckfo47 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ends_expr_after_name_5t7p9lg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_error_expr_dhdi86k {
    gm_frame h;
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1s091rv {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_is_arith_l6b4tpf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_q2f0b93 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_relational_n3goics {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fvvkq82 {
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
struct gm_fr_mul_from_mlskgvm {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_mul_op_ab6loj8 {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
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
struct gm_fr_op_text_3o86dat {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_parens_0pje0fd {
    gm_frame h;
    gm_t0vh9i8m50f41 ret;
};
struct gm_fr_parse_add_dh9nvbt {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_arg_t6fq0nq {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tc3oht4ksth9d l5; /* e */
    gm_t3q68r4ne5jv8 ret;
};
struct gm_fr_parse_bang_irmkots {
    gm_frame h;
    void * l0; /* p */
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t0mfir12leg2g l3; /* rhs */
    gm_tc3oht4ksth9d l4; /* r */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_bit_h83vs72 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_block_expr_0j6b78q {
    gm_frame h;
    gm_t79rbdv4rafe5 l1; /* block */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_break_hehputf {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_call_args_32e205h {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_capture_body_fgf89k2 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_cmp_q5clpig {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* first */
    gm_tc3oht4ksth9d l3; /* lhs */
    gm_tc3oht4ksth9d l6; /* rhs */
    gm_tc3oht4ksth9d l7; /* e */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_continue_ertnf67 {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* label */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_dot_suffix_1fk204i {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_else_smoqgg4 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_t1s75i08qtptd l2; /* capture */
    gm_tc3oht4ksth9d l3; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_expr_bkb2n6e {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fallback_g1vpkra {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_head_expr_6sofdhn {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_import_s1hv231 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_intrinsic_4ou54h3 {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* t */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_logic_ajejref {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_mul_35e1t49 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_not_a8b3gjf {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_paren_sq1guoi {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* elems */
    gm_tc3oht4ksth9d l8; /* value */
    gm_t1s75i08qtptd l9; /* name */
    gm_tc3oht4ksth9d l10; /* value */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ao4kstr {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_postfix_ops_2p1csn3 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_tc3oht4ksth9d l3; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_prefix_671fi6d {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* operand */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_primary_1vvr4ji {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_range_o9o2al7 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_resume_u919e4d {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* operand */
    gm_tc3oht4ksth9d l7; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_return_fjkmc1q {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_shift_jium2c6 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_1hujuhl {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* base */
    gm_t1vei8bd9e9m7 l3; /* s */
    gm_tc3oht4ksth9d l5; /* index */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_subscript_inner_dhgpovb {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* first */
    gm_tb1kjfin6jnc2 ret;
};
struct gm_fr_prefix_op_japctlh {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l8mrumu {
    gm_frame h;
    gm_tv1o1j44ii4db l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_from_5v6ko9m {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l5; /* rhs */
    gm_tc3oht4ksth9d l6; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_47d6kh3 {
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
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_shift_from_3cnaq25 {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* first */
    gm_tc3oht4ksth9d l2; /* lhs */
    gm_tc3oht4ksth9d l4; /* rhs */
    gm_tc3oht4ksth9d l5; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_shift_op_sro3o9l {
    gm_frame h;
    gm_tt7vn9ehj0acr ret;
};
struct gm_fr_shrink_to_lo_iok07dh {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
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
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b);
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_reported_kw_name_it872te(void * a0);
__attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_bump_reported_kw_name_gs5eq1i(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_error_expr_dhdi86k(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_format_3nkc0o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5k9iv40(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5nk8afu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gqrkfjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o5o20fv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_pb6f6q3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_keyword_as_name_ipm23p9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyword_text_149oqn4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_for_q8vk3da(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_foreign_l4091na(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_handler_lit_1nkojsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_if_shktfou(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_match_1lcgsrh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_with_hlmj8pt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_report_diag_big1nsf(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0);
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_speculate_djd8kc6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1cqejvllajb6[3];
extern const uint8_t gm_s1llcn0p4qesk[20];
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s1q0n6t6d7g6n[34];
extern const uint8_t gm_s27pt4vcvtscl[24];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3tpa8q2q7llg[17];
extern const uint8_t gm_s42atdm90l4do[14];
extern const uint8_t gm_s4niue72anpss[37];
extern const uint8_t gm_s4puodol85qqo[55];
extern const uint8_t gm_s541gi95epov5[25];
extern const uint8_t gm_s5lrfo6g7933d[50];
extern const uint8_t gm_s5us2hgd173vu[27];
extern const uint8_t gm_s5vb1b1mnli2b[72];
extern const uint8_t gm_s8ki76moa277n[23];
extern const uint8_t gm_s8sl52ot187i2[50];
extern const uint8_t gm_s8vhc5kn5i8tv[38];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_sa3k87epai61e[25];
extern const uint8_t gm_sam3pd3469drv[55];
extern const uint8_t gm_saojtqmfeak72[8];
extern const uint8_t gm_sb455a3lakuom[60];
extern const uint8_t gm_sbckhsd8r0nr0[20];
extern const uint8_t gm_sh0is7j09prt2[23];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shsfl3e9phpsi[23];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sjrimf03ml0ct[13];
extern const uint8_t gm_skefd3dps40es[40];
extern const uint8_t gm_skh04d6p3n402[79];
extern const uint8_t gm_sl7a8f8pe8le9[21];
extern const uint8_t gm_slp04vsc9cddi[49];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_snemlnnsbdb13[46];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_sqm6u7rqmi1il[19];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0dtioet3p4f6[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_ssd99k9slj99i[3];
extern const uint8_t gm_ssf8mqtfpipac[45];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_sujvknhnh5am8[19];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svhvj3h6g939d[66];
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
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
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
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqcdhc1ipuua(const uint8_t *a, const uint8_t *b) {
    if (a[2] != b[2]) return false;
    return !a[2] || gm_eqbqb4gb1d92(a, b);
}
inline bool gm_eqebiqthsuot(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eq929lennfd3(a, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8ckfo47(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8ckfo47 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 136)));
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
inline __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_1s091rv F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
            gm_tm4pc0emhrv1g t4;
            gm_f_bump_q2i40re((void *)&t4, t3);
        }
    }
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_parse_bang_irmkots *)((&F.h)->up))->l0));
        gm_f_parse_type_2m93dtq((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_q2f0b93(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_q2f0b93 F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t1 == t2));
    F.ret = t3;
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
inline __attribute__((aligned(4))) void gm_f_list_fvvkq82(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fvvkq82 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8ckfo47();
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
inline __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_capture_body_fgf89k2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)65ULL));
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t7);
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
        gm_f_parse_expr_bkb2n6e((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
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
inline __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_mul_35e1t49 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t6 = gm_l0;
        gm_f_mul_from_mlskgvm((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t6, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_postfix_ao4kstr F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* allow_bang */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_primary_1vvr4ji((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t6 = gm_l0;
        uint8_t t7 = gm_l1;
        gm_f_parse_postfix_ops_2p1csn3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t6, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t7);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l8mrumu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l8mrumu F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b16_8 t11 = gm_f_empty_slice_8ckfo47();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_47d6kh3(t10, ((uint64_t)1ULL), t12);
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
        gm_tv1o1j44ii4db t21 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)a1));
        GM_ST(gm_tv1o1j44ii4db, ((uint8_t *)t20 + t18 * 136), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 136)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_47d6kh3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_47d6kh3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_shrink_to_lo_iok07dh(gm_cseahvg8v7p0vs a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shrink_to_lo_iok07dh F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 0));
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_tl874t2atnlh3 t4;
    GM_ST(uint32_t, ((uint8_t *)&t4 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 4), t2);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_add_from_7os0rvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_from_7os0rvi F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_mul_from_mlskgvm((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        for (;;) {
            void * t7 = gm_l0;
            gm_t3fcbuehh3bs2 t8;
            gm_f_kind_k239kpg((void *)&t8, t7);
            gm_tt7vn9ehj0acr t9 = gm_f_add_op_99op354(&t8);
            if (!((((uint8_t *)&t9))[2] != 0)) goto lb6;
            gm_ts6oqppjh0ba3 t10 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t9));
            gm_l3 = t10;
            {
                {
                    void * t14 = gm_l0;
                    gm_tm4pc0emhrv1g t15;
                    gm_f_bump_q2i40re((void *)&t15, t14);
                }
            }
            {
                void * t16 = gm_l0;
                gm_to1cssm8dfiv5 t17;
                gm_f_parse_mul_35e1t49((void *)&t17, (void *)NULL, cx, NULL, t16);
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
                if (((((uint8_t *)&t17))[0] != 0)) {
                    gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
                    gm_to1cssm8dfiv5 t19;
                    memset(&t19, 0, sizeof t19);
                    ((uint8_t *)&t19)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), t18);
                    F.ret = t19;
                    goto gm_fret;
                }
                F.l4 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t17) + 8));
            }
            {
                void * t20 = gm_l0;
                gm_ts6oqppjh0ba3 t21 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t20, t21, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll11;
                }
            }
lc6:;
        }
gm_ll11:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
    }
    {
        const uint8_t *t22 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t22, 88);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_op_99op354 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; default: goto ma11; }
ma3:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)8ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn14;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t18;
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 1), t17);
    gm_tt7vn9ehj0acr t19;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[2] = 1;
    t1 = t19;
    goto me2;
mn14:;
ma4:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t21 != ((uint8_t)8ULL)) goto mn20;
    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t22 != ((uint8_t)10ULL)) goto mn20;
    gm_t9qkh0rsooc8d t23;
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 1), t23);
    gm_tt7vn9ehj0acr t25;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[2] = 1;
    t1 = t25;
    goto me2;
mn20:;
ma5:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)8ULL)) goto mn26;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t28 != ((uint8_t)14ULL)) goto mn26;
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30 + 1), t29);
    gm_tt7vn9ehj0acr t31;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t31), t30);
    ((uint8_t *)&t31)[2] = 1;
    t1 = t31;
    goto me2;
mn26:;
ma6:;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t33 != ((uint8_t)8ULL)) goto mn32;
    uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t34 != ((uint8_t)18ULL)) goto mn32;
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t36 + 1), t35);
    gm_tt7vn9ehj0acr t37;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[2] = 1;
    t1 = t37;
    goto me2;
mn32:;
ma7:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t39 != ((uint8_t)8ULL)) goto mn38;
    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t40 != ((uint8_t)1ULL)) goto mn38;
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t42 + 1), t41);
    gm_tt7vn9ehj0acr t43;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43), t42);
    ((uint8_t *)&t43)[2] = 1;
    t1 = t43;
    goto me2;
mn38:;
ma8:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)8ULL)) goto mn44;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t46 != ((uint8_t)11ULL)) goto mn44;
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t48;
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 1), t47);
    gm_tt7vn9ehj0acr t49;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t49), t48);
    ((uint8_t *)&t49)[2] = 1;
    t1 = t49;
    goto me2;
mn44:;
ma9:;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t51 != ((uint8_t)8ULL)) goto mn50;
    uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t52 != ((uint8_t)15ULL)) goto mn50;
    gm_t9qkh0rsooc8d t53;
    GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t54;
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 1), t53);
    gm_tt7vn9ehj0acr t55;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t55), t54);
    ((uint8_t *)&t55)[2] = 1;
    t1 = t55;
    goto me2;
mn50:;
ma10:;
    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t57 != ((uint8_t)8ULL)) goto mn56;
    uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t58 != ((uint8_t)19ULL)) goto mn56;
    gm_t9qkh0rsooc8d t59;
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t60;
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t60 + 1), t59);
    gm_tt7vn9ehj0acr t61;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t61), t60);
    ((uint8_t *)&t61)[2] = 1;
    t1 = t61;
    goto me2;
mn56:;
ma11:;
    gm_tt7vn9ehj0acr t63;
    memset(&t63, 0, sizeof t63);
    t1 = t63;
    goto me2;
mn62:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 61, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_add_op_99op354_c(gm_t3fcbuehh3bs2 a0) { return gm_f_add_op_99op354(&a0); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ambiguous_try_else_0v14t0i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_ambiguous_try_else_0v14t0i F;
    void * gm_l0; /* p */
    void * gm_l1; /* lhs */
    void * gm_l2; /* rhs */
    void * gm_l3; /* operand */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)12ULL)) goto ie2;
        uint8_t t5 = GM_LD(uint8_t, ((((((uint8_t *)t3) + 8) + 8) + 0) + 0));
        if (t5 != ((uint8_t)4ULL)) goto ie2;
        void * t6 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        {
            void * t8 = gm_l1;
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s5vb1b1mnli2b));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(71));
            gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t11 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_shsfl3e9phpsi));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(22));
            {
                void * t14 = gm_l0;
                uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 0));
                void * t16 = gm_l3;
                uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t16) + 72) + 4));
                void * t18 = gm_l2;
                uint32_t t19 = GM_LD(uint32_t, ((((uint8_t *)t18) + 72) + 8));
                gm_f_parens_0pje0fd((void *)&(*(gm_t0vh9i8m50f41 *)((uint8_t *)&F.l5)), t15, t17, t19);
            }
            void * t20 = ((uint8_t *)&F.l5);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0dtioet3p4f6, 154, 47);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t20 + (uintptr_t)0 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t22 = t21;
            gm_b0_1 t23 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t11, t12, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            void * t24 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s8ki76moa277n));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(22));
            {
                void * t27 = gm_l0;
                uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)t27) + 0));
                void * t29 = gm_l1;
                uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)t29) + 72) + 4));
                void * t31 = gm_l1;
                uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t31) + 72) + 8));
                gm_f_parens_0pje0fd((void *)&(*(gm_t0vh9i8m50f41 *)((uint8_t *)&F.l6)), t28, t30, t32);
            }
            void * t33 = ((uint8_t *)&F.l6);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0dtioet3p4f6, 155, 47);
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t33 + (uintptr_t)0 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t35 = t34;
            gm_b0_1 t36 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t24, t25, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t37 = gm_l0;
            gm_f_report_diag_big1nsf(t37, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t39;
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_binary_41fr5r5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3) {
    struct gm_fr_binary_41fr5r5 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    memcpy(&F.l3, a3, sizeof F.l3);
    {
        void * t2 = gm_l0;
        gm_trl9d7m1k5lbd t3;
        memset((uint8_t *)&t3 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t4 = gm_l1;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t3 + 0, t4);
        void * t5 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t3 + 8, t5);
        void * t6 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t3 + 16, t6);
        gm_t2gtbh39ptekd t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)13ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t7 + 8), t3);
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
        gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t8), GM_CONV(gm_cseahvg8v7p0vs, t9)));
        gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)gm_out), t2, &t7, GM_CONV(gm_cseahvg8v7p0vs, t10));
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
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binary_follows_jeb42bi F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_t9qkh0rsooc8d t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)16ULL));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
    gm_t0am46iohlpol t14;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 0), t12);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 1), t13);
    uint8_t t15 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t14) + 0, ((uint8_t *)&t11))) t15 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t14) + 1, ((uint8_t *)&t11))) t15 = 1;
    uint8_t t16 = (uint8_t)(t15);
    t1 = t16;
    goto me2;
mn8:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)8ULL)) goto mn17;
    gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t19;
    uint8_t t20;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    switch (t28) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma25; case ((uint8_t)4ULL): goto ma25; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma23; case ((uint8_t)7ULL): goto ma22; case ((uint8_t)8ULL): goto ma22; case ((uint8_t)9ULL): goto ma22; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma25; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma25; case ((uint8_t)17ULL): goto ma23; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma25; case ((uint8_t)21ULL): goto ma23; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma26; case ((uint8_t)28ULL): goto ma26; case ((uint8_t)29ULL): goto ma26; case ((uint8_t)30ULL): goto ma26; case ((uint8_t)31ULL): goto ma26; case ((uint8_t)32ULL): goto ma26; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma26; case ((uint8_t)35ULL): goto ma26; case ((uint8_t)36ULL): goto ma26; case ((uint8_t)37ULL): goto ma26; case ((uint8_t)38ULL): goto ma26; case ((uint8_t)39ULL): goto ma26; case ((uint8_t)40ULL): goto ma26; case ((uint8_t)41ULL): goto ma22; case ((uint8_t)42ULL): goto ma22; case ((uint8_t)43ULL): goto ma22; case ((uint8_t)44ULL): goto ma22; case ((uint8_t)45ULL): goto ma26; case ((uint8_t)46ULL): goto ma26; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma26; case ((uint8_t)49ULL): goto ma26; case ((uint8_t)50ULL): goto ma22; case ((uint8_t)51ULL): goto ma22; case ((uint8_t)52ULL): goto ma26; case ((uint8_t)53ULL): goto ma26; case ((uint8_t)54ULL): goto ma26; case ((uint8_t)55ULL): goto ma26; case ((uint8_t)56ULL): goto ma26; case ((uint8_t)57ULL): goto ma26; case ((uint8_t)58ULL): goto ma26; case ((uint8_t)59ULL): goto ma26; case ((uint8_t)60ULL): goto ma26; case ((uint8_t)61ULL): goto ma26; case ((uint8_t)62ULL): goto ma26; case ((uint8_t)63ULL): goto ma26; case ((uint8_t)64ULL): goto ma26; case ((uint8_t)65ULL): goto ma26; case ((uint8_t)66ULL): goto ma26; case ((uint8_t)67ULL): goto ma22; case ((uint8_t)68ULL): goto ma22; default: goto ma26; }
ma22:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t32 != ((uint8_t)67ULL)) goto pa31;
    goto po30;
pa31:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t34 != ((uint8_t)68ULL)) goto pa33;
    goto po30;
pa33:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t36 != ((uint8_t)41ULL)) goto pa35;
    goto po30;
pa35:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t38 != ((uint8_t)42ULL)) goto pa37;
    goto po30;
pa37:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t40 != ((uint8_t)43ULL)) goto pa39;
    goto po30;
pa39:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t42 != ((uint8_t)44ULL)) goto pa41;
    goto po30;
pa41:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t44 != ((uint8_t)50ULL)) goto pa43;
    goto po30;
pa43:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t46 != ((uint8_t)51ULL)) goto pa45;
    goto po30;
pa45:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t48 != ((uint8_t)7ULL)) goto pa47;
    goto po30;
pa47:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t50 != ((uint8_t)8ULL)) goto pa49;
    goto po30;
pa49:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t52 != ((uint8_t)9ULL)) goto pa51;
    goto po30;
pa51:;
    goto mn29;
po30:;
    t20 = ((uint8_t)1);
    goto me21;
mn29:;
ma23:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t56 != ((uint8_t)5ULL)) goto pa55;
    goto po54;
pa55:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t58 != ((uint8_t)13ULL)) goto pa57;
    goto po54;
pa57:;
    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t60 != ((uint8_t)17ULL)) goto pa59;
    goto po54;
pa59:;
    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t62 != ((uint8_t)21ULL)) goto pa61;
    goto po54;
pa61:;
    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t64 != ((uint8_t)6ULL)) goto pa63;
    goto po54;
pa63:;
    goto mn53;
po54:;
    t20 = ((uint8_t)1);
    goto me21;
mn53:;
ma24:;
    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t68 != ((uint8_t)0ULL)) goto pa67;
    goto po66;
pa67:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t70 != ((uint8_t)10ULL)) goto pa69;
    goto po66;
pa69:;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t72 != ((uint8_t)14ULL)) goto pa71;
    goto po66;
pa71:;
    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t74 != ((uint8_t)18ULL)) goto pa73;
    goto po66;
pa73:;
    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t76 != ((uint8_t)1ULL)) goto pa75;
    goto po66;
pa75:;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t78 != ((uint8_t)11ULL)) goto pa77;
    goto po66;
pa77:;
    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t80 != ((uint8_t)15ULL)) goto pa79;
    goto po66;
pa79:;
    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t82 != ((uint8_t)19ULL)) goto pa81;
    goto po66;
pa81:;
    goto mn65;
po66:;
    t20 = ((uint8_t)1);
    goto me21;
mn65:;
ma25:;
    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t86 != ((uint8_t)2ULL)) goto pa85;
    goto po84;
pa85:;
    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t88 != ((uint8_t)12ULL)) goto pa87;
    goto po84;
pa87:;
    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t90 != ((uint8_t)16ULL)) goto pa89;
    goto po84;
pa89:;
    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t92 != ((uint8_t)20ULL)) goto pa91;
    goto po84;
pa91:;
    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t94 != ((uint8_t)3ULL)) goto pa93;
    goto po84;
pa93:;
    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
    if (t96 != ((uint8_t)4ULL)) goto pa95;
    goto po84;
pa95:;
    goto mn83;
po84:;
    t20 = ((uint8_t)1);
    goto me21;
mn83:;
ma26:;
    t20 = ((uint8_t)0);
    goto me21;
mn97:;
ma27:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 88, 18);
me21:;
    t1 = t20;
    goto me2;
mn17:;
ma5:;
    t1 = ((uint8_t)0);
    goto me2;
mn98:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 86, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_binary_follows_jeb42bi_c(gm_t3fcbuehh3bs2 a0) { return gm_f_binary_follows_jeb42bi(&a0); }
__attribute__((aligned(4))) void gm_f_bit_from_pu2bq19(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_bit_from_pu2bq19 F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l3; /* chain */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* word */
    gm_b16_8 gm_l9; /* before */
    gm_b16_8 gm_l10; /* after */
    GM_STACK_CHECK(&F, 1546u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_shift_from_3cnaq25((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        gm_tt7vn9ehj0acr t6;
        memset(&t6, 0, sizeof t6);
        gm_l3 = t6;
    }
    {
        for (;;) {
            void * t8 = gm_l0;
            gm_t3fcbuehh3bs2 t9;
            gm_f_kind_k239kpg((void *)&t9, t8);
            gm_tt7vn9ehj0acr t10 = gm_f_bit_op_p81j9u3(&t9);
            if (!((((uint8_t *)&t10))[2] != 0)) goto lb7;
            gm_ts6oqppjh0ba3 t11 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t10));
            gm_l4 = t11;
            {
                uint8_t t15;
                if (((((uint8_t *)&gm_l3))[2] != 0)) goto mn17;
                t15 = ((uint8_t)1);
                goto me16;
mn17:;
                t15 = ((uint8_t)0);
                goto me16;
mn18:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 302, 12);
me16:;
                if (!t15) goto ie14;
                {
                    gm_ts6oqppjh0ba3 t20 = gm_l4;
                    gm_tt7vn9ehj0acr t21;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t21), t20);
                    ((uint8_t *)&t21)[2] = 1;
                    gm_l3 = t21;
                }
                goto ix14;
ie14:;
                gm_tt7vn9ehj0acr t23 = gm_l3;
                gm_ts6oqppjh0ba3 t24 = gm_l4;
                gm_tt7vn9ehj0acr t25;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25), t24);
                ((uint8_t *)&t25)[2] = 1;
                uint8_t t26 = (!gm_eqcdhc1ipuua(((uint8_t *)&t23), ((uint8_t *)&t25)));
                if (!t26) goto ie22;
                {
                    void * t28 = gm_l0;
                    void * t29 = gm_l0;
                    gm_tl874t2atnlh3 t30 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t29));
                    gm_b16_8 t31;
                    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_svhvj3h6g939d));
                    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(65));
                    gm_b0_1 t32 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t28, t30, t31);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                goto ix22;
ie22:;
ix22:;
ix14:;
            }
            {
                void * t33 = gm_l0;
                gm_f_bump_q2i40re((void *)&F.l5, t33);
            }
            {
                gm_ts6oqppjh0ba3 t35 = gm_l4;
                gm_ts6oqppjh0ba3 t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)7ULL));
                gm_ts6oqppjh0ba3 t37;
                GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)8ULL));
                gm_t9ldr1q5hn9fq t38;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t38 + 0), t36);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t38 + 2), t37);
                uint8_t t39 = 0;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t38) + 0, ((uint8_t *)&t35))) t39 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t38) + 2, ((uint8_t *)&t35))) t39 = 1;
                uint8_t t40 = (uint8_t)(t39);
                if (!t40) goto ie34;
                void * t41 = gm_l0;
                gm_t9qkh0rsooc8d t42;
                uint8_t t44 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                if (!t44) goto ie43;
                gm_t9qkh0rsooc8d t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)7ULL));
                gm_t9qkh0rsooc8d t47 = t46;
                t42 = t47;
                goto ix43;
ie43:;
                gm_t9qkh0rsooc8d t49;
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)8ULL));
                gm_t9qkh0rsooc8d t50 = t49;
                t42 = t50;
ix43:;
                uint8_t t51 = gm_f_at_54906rm(t41, t42);
                if (!t51) goto ie34;
                void * t52 = gm_l0;
                uint32_t t53 = gm_f_lo_fp82gt4(t52);
                uint32_t t54 = GM_LD(uint32_t, ((((uint8_t *)&F.l5) + 32) + 8));
                uint8_t t55 = ((t53 == t54));
                if (!t55) goto ie34;
                {
                    gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 32));
                    void * t58 = gm_l0;
                    gm_tl874t2atnlh3 t59 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t58));
                    gm_tl874t2atnlh3 t60 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t57), GM_CONV(gm_cseahvg8v7p0vs, t59)));
                    gm_l6 = t60;
                }
                {
                    gm_b16_8 t61;
                    uint8_t t63 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                    if (!t63) goto ie62;
                    gm_b16_8 t65;
                    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s1cqejvllajb6));
                    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(2));
                    gm_b16_8 t66 = t65;
                    t61 = t66;
                    goto ix62;
ie62:;
                    gm_b16_8 t68;
                    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_ssd99k9slj99i));
                    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(2));
                    gm_b16_8 t69 = t68;
                    t61 = t69;
ix62:;
                    gm_l7 = t61;
                }
                {
                    gm_b16_8 t70;
                    uint8_t t72 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l4) + 0) == ((uint8_t)7ULL)));
                    if (!t72) goto ie71;
                    gm_b16_8 t74;
                    GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s920dpvvv6cej));
                    GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(3));
                    gm_b16_8 t75 = t74;
                    t70 = t75;
                    goto ix71;
ie71:;
                    gm_b16_8 t77;
                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_spi47lg4tr80j));
                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(2));
                    gm_b16_8 t78 = t77;
                    t70 = t78;
ix71:;
                    gm_l8 = t70;
                }
                {
                    gm_b16_8 t79;
                    uint32_t t81 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 72) + 8));
                    uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                    uint8_t t83 = ((t81 == t82));
                    if (!t83) goto ie80;
                    gm_b16_8 t85;
                    GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
                    gm_b16_8 t86 = t85;
                    t79 = t86;
                    goto ix80;
ie80:;
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
                    gm_b16_8 t89 = t88;
                    t79 = t89;
ix80:;
                    gm_l9 = t79;
                }
                {
                    {
                        void * t91 = gm_l0;
                        gm_tm4pc0emhrv1g t92;
                        gm_f_bump_q2i40re((void *)&t92, t91);
                    }
                }
                {
                    gm_b16_8 t93;
                    void * t95 = gm_l0;
                    uint32_t t96 = gm_f_lo_fp82gt4(t95);
                    uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t98 = ((t96 == t97));
                    if (!t98) goto ie94;
                    gm_b16_8 t100;
                    GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(1));
                    gm_b16_8 t101 = t100;
                    t93 = t101;
                    goto ix94;
ie94:;
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(0));
                    gm_b16_8 t104 = t103;
                    t93 = t104;
ix94:;
                    gm_l10 = t93;
                }
                {
                    gm_tl874t2atnlh3 t105 = gm_l6;
                    gm_t6j6smj7qoctu t106;
                    gm_b16_8 t107 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
                    gm_b16_8 t108 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t108);
                    gm_tabb710d9mq2e t109;
                    gm_f_format_5nk8afu((void *)&t109, (void *)NULL, cx, NULL, &t106);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                    if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_src0dtioet3p4f6, 317, 33);
                    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                    gm_f_error_s6hn871((void *)&F.l11, (void *)NULL, cx, NULL, t105, t111);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    void * t112 = (void *)((uint8_t *)&F.l11);
                    gm_t6j6smj7qoctu t113;
                    gm_b16_8 t114 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t114);
                    gm_b16_8 t115 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t115);
                    gm_tabb710d9mq2e t116;
                    gm_f_format_gqrkfjb((void *)&t116, (void *)NULL, cx, NULL, &t113);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                    if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_src0dtioet3p4f6, 318, 23);
                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                    gm_tl874t2atnlh3 t119 = gm_l6;
                    gm_tfeab279c16rr t120;
                    gm_b16_8 t121 = gm_l9;
                    GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
                    gm_b16_8 t122 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t120 + 16, t122);
                    gm_b16_8 t123 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t120 + 32, t123);
                    gm_tabb710d9mq2e t124;
                    gm_f_format_dsta2ql((void *)&t124, (void *)NULL, cx, NULL, &t120);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                    if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_src0dtioet3p4f6, 318, 75);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                    gm_b0_1 t127 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t112, t118, t119, t126);
                    if (gm_unw.kind) {
                        goto gm_ll12;
                    }
                }
                {
                    void * t128 = gm_l0;
                    gm_b0_1 t129 = gm_f_fail_diag_u0q4kp3(t128, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                    memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
                    ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t129);
                    goto gm_fdone;
                }
                goto ix34;
ie34:;
ix34:;
            }
            {
                void * t130 = gm_l0;
                gm_to1cssm8dfiv5 t131;
                gm_f_parse_shift_jium2c6((void *)&t131, (void *)NULL, cx, NULL, t130);
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
                if (((((uint8_t *)&t131))[0] != 0)) {
                    gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 8));
                    gm_to1cssm8dfiv5 t133;
                    memset(&t133, 0, sizeof t133);
                    ((uint8_t *)&t133)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t133 + 8), t132);
                    F.ret = t133;
                    goto gm_fret;
                }
                F.l12 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t131) + 8));
            }
            {
                void * t134 = gm_l0;
                gm_ts6oqppjh0ba3 t135 = gm_l4;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t134, t135, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_ll12;
                }
            }
lc7:;
        }
gm_ll12:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
        }
        goto gm_lad;
lb7:;
    }
    {
        const uint8_t *t136 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t136, 88);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_op_p81j9u3 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)8ULL)) goto mn9;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t11 != ((uint8_t)7ULL)) goto mn9;
    gm_ts6oqppjh0ba3 t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)7ULL));
    gm_tt7vn9ehj0acr t13;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[2] = 1;
    t1 = t13;
    goto me2;
mn9:;
ma4:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)8ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)8ULL)) goto mn14;
    gm_ts6oqppjh0ba3 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
    gm_tt7vn9ehj0acr t18;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[2] = 1;
    t1 = t18;
    goto me2;
mn14:;
ma5:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)8ULL)) goto mn19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)9ULL)) goto mn19;
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)9ULL));
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn19:;
ma6:;
    gm_tt7vn9ehj0acr t25;
    memset(&t25, 0, sizeof t25);
    t1 = t25;
    goto me2;
mn24:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 45, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_bit_op_p81j9u3_c(gm_t3fcbuehh3bs2 a0) { return gm_f_bit_op_p81j9u3(&a0); }
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_expr_rmia9bs F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma3; case ((uint8_t)6ULL): goto ma3; case ((uint8_t)7ULL): goto ma3; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)0ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto pa13;
    goto po10;
pa13:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto pa15;
    goto po10;
pa15:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)4ULL)) goto pa17;
    goto po10;
pa17:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)5ULL)) goto pa19;
    goto po10;
pa19:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t22 != ((uint8_t)6ULL)) goto pa21;
    goto po10;
pa21:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t24 != ((uint8_t)7ULL)) goto pa23;
    goto po10;
pa23:;
    goto mn9;
po10:;
    t1 = ((uint8_t)1);
    goto me2;
mn9:;
ma4:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t26 != ((uint8_t)2ULL)) goto mn25;
    gm_t9qkh0rsooc8d t27 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t27;
    gm_t9qkh0rsooc8d t28 = gm_l1;
    gm_ta8d7579em356 t29;
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)25ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 0, t30);
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)10ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 1, t31);
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)19ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 2, t32);
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)11ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 3, t33);
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)24ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 4, t34);
    gm_t9qkh0rsooc8d t35;
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)27ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 5, t35);
    gm_t9qkh0rsooc8d t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)6ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 6, t36);
    gm_t9qkh0rsooc8d t37;
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)14ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 7, t37);
    gm_t9qkh0rsooc8d t38;
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 8, t38);
    gm_t9qkh0rsooc8d t39;
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)17ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 9, t39);
    gm_t9qkh0rsooc8d t40;
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)31ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 10, t40);
    gm_t9qkh0rsooc8d t41;
    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 11, t41);
    gm_t9qkh0rsooc8d t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)23ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 12, t42);
    gm_t9qkh0rsooc8d t43;
    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 13, t43);
    gm_t9qkh0rsooc8d t44;
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 14, t44);
    gm_t9qkh0rsooc8d t45;
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)22ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 15, t45);
    gm_t9qkh0rsooc8d t46;
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)26ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 16, t46);
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
    GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t29 + 17, t47);
    uint8_t t48 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 0, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 1, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 2, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 3, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 4, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 5, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 6, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 7, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 8, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 9, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 10, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 11, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 12, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 13, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 14, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 15, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 16, ((uint8_t *)&t28))) t48 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t29) + 17, ((uint8_t *)&t28))) t48 = 1;
    uint8_t t49 = (uint8_t)(t48);
    t1 = t49;
    goto me2;
mn25:;
ma5:;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t51 != ((uint8_t)8ULL)) goto mn50;
    gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t52;
    gm_t9qkh0rsooc8d t53 = gm_l2;
    gm_t9qkh0rsooc8d t54;
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)61ULL));
    gm_t9qkh0rsooc8d t55;
    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)63ULL));
    gm_t9qkh0rsooc8d t56;
    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)58ULL));
    gm_t9qkh0rsooc8d t57;
    GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t58;
    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)9ULL));
    gm_t9qkh0rsooc8d t59;
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)7ULL));
    gm_t9qkh0rsooc8d t60;
    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)46ULL));
    gm_t9qkh0rsooc8d t61;
    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t62;
    GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t63;
    GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)53ULL));
    gm_tfh78knfg7urq t64;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 0), t54);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 1), t55);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 2), t56);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 3), t57);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 4), t58);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 5), t59);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 6), t60);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 7), t61);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 8), t62);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 9), t63);
    uint8_t t65 = 0;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 0, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 1, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 2, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 3, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 4, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 5, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 6, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 7, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 8, ((uint8_t *)&t53))) t65 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t64) + 9, ((uint8_t *)&t53))) t65 = 1;
    uint8_t t66 = (uint8_t)(t65);
    t1 = t66;
    goto me2;
mn50:;
ma6:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t70 != ((uint8_t)9ULL)) goto pa69;
    goto po68;
pa69:;
    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t72 != ((uint8_t)10ULL)) goto pa71;
    goto po68;
pa71:;
    goto mn67;
po68:;
    t1 = ((uint8_t)0);
    goto me2;
mn67:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 5, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_expr_rmia9bs_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_expr_rmia9bs(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_can_begin_type_a3rjrm7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* kw */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma3; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; default: goto ma6; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)0ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)7ULL)) goto pa13;
    goto po10;
pa13:;
    goto mn9;
po10:;
    t1 = ((uint8_t)1);
    goto me2;
mn9:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t17;
    gm_t9qkh0rsooc8d t18 = gm_l1;
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)24ULL));
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)27ULL));
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)6ULL));
    gm_t8d5d2ep1fo6t t23;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 0), t19);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 1), t20);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 2), t21);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 3), t22);
    uint8_t t24 = 0;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t23) + 0, ((uint8_t *)&t18))) t24 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t23) + 1, ((uint8_t *)&t18))) t24 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t23) + 2, ((uint8_t *)&t18))) t24 = 1;
    if (gm_eq2ceic3f8rp(((uint8_t *)&t23) + 3, ((uint8_t *)&t18))) t24 = 1;
    uint8_t t25 = (uint8_t)(t24);
    t1 = t25;
    goto me2;
mn15:;
ma5:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)8ULL)) goto mn26;
    gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t28;
    gm_t9qkh0rsooc8d t29 = gm_l2;
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)45ULL));
    gm_t9qkh0rsooc8d t31;
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)47ULL));
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)63ULL));
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)61ULL));
    gm_t8d5d2ep1fo6t t34;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 0), t30);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 1), t31);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 2), t32);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 3), t33);
    uint8_t t35 = 0;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 0, ((uint8_t *)&t29))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 1, ((uint8_t *)&t29))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 2, ((uint8_t *)&t29))) t35 = 1;
    if (gm_eqv3tj1pu43g(((uint8_t *)&t34) + 3, ((uint8_t *)&t29))) t35 = 1;
    uint8_t t36 = (uint8_t)(t35);
    t1 = t36;
    goto me2;
mn26:;
ma6:;
    t1 = ((uint8_t)0);
    goto me2;
mn37:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 23, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_can_begin_type_a3rjrm7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_can_begin_type_a3rjrm7(&a0); }
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_op_9gj7im4 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; default: goto ma11; }
ma3:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t16 != ((uint8_t)16ULL)) goto mn14;
    gm_ts6oqppjh0ba3 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)12ULL));
    gm_tt7vn9ehj0acr t18;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[2] = 1;
    t1 = t18;
    goto me2;
mn14:;
ma4:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)2ULL)) goto mn19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)15ULL)) goto mn19;
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)14ULL));
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn19:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)8ULL)) goto mn24;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t26 != ((uint8_t)67ULL)) goto mn24;
    gm_ts6oqppjh0ba3 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)13ULL));
    gm_tt7vn9ehj0acr t28;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[2] = 1;
    t1 = t28;
    goto me2;
mn24:;
ma6:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t30 != ((uint8_t)8ULL)) goto mn29;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t31 != ((uint8_t)68ULL)) goto mn29;
    gm_ts6oqppjh0ba3 t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)15ULL));
    gm_tt7vn9ehj0acr t33;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t33), t32);
    ((uint8_t *)&t33)[2] = 1;
    t1 = t33;
    goto me2;
mn29:;
ma7:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t35 != ((uint8_t)8ULL)) goto mn34;
    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t36 != ((uint8_t)41ULL)) goto mn34;
    gm_ts6oqppjh0ba3 t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)16ULL));
    gm_tt7vn9ehj0acr t38;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t38), t37);
    ((uint8_t *)&t38)[2] = 1;
    t1 = t38;
    goto me2;
mn34:;
ma8:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t40 != ((uint8_t)8ULL)) goto mn39;
    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t41 != ((uint8_t)42ULL)) goto mn39;
    gm_ts6oqppjh0ba3 t42;
    GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)17ULL));
    gm_tt7vn9ehj0acr t43;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t43), t42);
    ((uint8_t *)&t43)[2] = 1;
    t1 = t43;
    goto me2;
mn39:;
ma9:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)8ULL)) goto mn44;
    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t46 != ((uint8_t)43ULL)) goto mn44;
    gm_ts6oqppjh0ba3 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)18ULL));
    gm_tt7vn9ehj0acr t48;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t48), t47);
    ((uint8_t *)&t48)[2] = 1;
    t1 = t48;
    goto me2;
mn44:;
ma10:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t50 != ((uint8_t)8ULL)) goto mn49;
    uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t51 != ((uint8_t)44ULL)) goto mn49;
    gm_ts6oqppjh0ba3 t52;
    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)19ULL));
    gm_tt7vn9ehj0acr t53;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t53), t52);
    ((uint8_t *)&t53)[2] = 1;
    t1 = t53;
    goto me2;
mn49:;
ma11:;
    gm_tt7vn9ehj0acr t55;
    memset(&t55, 0, sizeof t55);
    t1 = t55;
    goto me2;
mn54:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 33, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_cmp_op_9gj7im4_c(gm_t3fcbuehh3bs2 a0) { return gm_f_cmp_op_9gj7im4(&a0); }
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_deny_in_head_06d9o22(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_deny_in_head_06d9o22 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 128) + 0));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t7));
            gm_b16_8 t9 = gm_l1;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_3nkc0o4((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src0dtioet3p4f6, 589, 52);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_b0_1 t14 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t6, t8, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t15;
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t15 + 1), t14);
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b0_1 t17;
        gm_t9qkh0rsooc8d t18;
        ((uint8_t *)&t18)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t18 + 1), t17);
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
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
inline __attribute__((aligned(4))) void gm_f_eat_label_7eokqmm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_label_7eokqmm F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* sym */
    GM_STACK_CHECK_SHALLOW(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t3fcbuehh3bs2 t4;
        gm_f_kind_k239kpg((void *)&t4, t3);
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t5 != ((uint8_t)0ULL)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l1 = t6;
        {
            gm_b16_8 t9 = gm_l1;
            void * t10 = gm_l0;
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 88) + 0));
            uint8_t t12 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t11) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t11))) + i * 16, ((uint8_t *)&t9))) { t12 = 1; break; }
            uint8_t t13 = (uint8_t)(!t12);
            if (!t13) goto ie8;
            {
                gm_t1s75i08qtptd t15;
                memset(&t15, 0, sizeof t15);
                uint8_t t16 = 0;
                F.ret = t15;
                goto gm_fret;
            }
            goto ix8;
ie8:;
ix8:;
        }
        {
            gm_t08olq97rscn3 t17;
            memset((uint8_t *)&t17 + 28, 0, 4);
            gm_b16_8 t18 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t17 + 0, t18);
            void * t19 = gm_l0;
            gm_tm4pc0emhrv1g t20;
            gm_f_bump_q2i40re((void *)&t20, t19);
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t20) + 32));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t17 + 16, t21);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out))[32] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t1s75i08qtptd *)gm_out) + 0, t17);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_t1s75i08qtptd t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t1s75i08qtptd gm_f_eat_label_7eokqmm_c(void * a0) { gm_t1s75i08qtptd gm_r; gm_f_eat_label_7eokqmm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ends_expr_after_name_5t7p9lg F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t6 != ((uint8_t)0ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t8 != ((uint8_t)3ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)4ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)5ULL)) goto pa11;
    goto po4;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t15 != ((uint8_t)19ULL)) goto pa13;
    goto po4;
pa13:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto pa16;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t18 != ((uint8_t)25ULL)) goto pa16;
    goto po4;
pa16:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t20 != ((uint8_t)2ULL)) goto pa19;
    uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t21 != ((uint8_t)10ULL)) goto pa19;
    goto po4;
pa19:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn22:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 787, 60);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_ends_expr_after_name_5t7p9lg_c(gm_t3fcbuehh3bs2 a0) { return gm_f_ends_expr_after_name_5t7p9lg(&a0); }
extern __attribute__((aligned(4))) void gm_f_fn_1s091rv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_is_arith_l6b4tpf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_arith_l6b4tpf F;
    void * gm_l0; /* e */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t5 != ((uint8_t)13ULL)) goto mn4;
    gm_ts6oqppjh0ba3 t6 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l1 = t6;
    uint8_t t7 = 0;
    uint8_t t8 = 0;
    uint8_t t9 = 0;
    uint8_t t10;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    t10 = ((uint8_t)1);
    goto me11;
mn12:;
    t10 = ((uint8_t)0);
    goto me11;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 26);
me11:;
    if (!t10) {
        uint8_t t15;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t18 != ((uint8_t)1ULL)) goto mn17;
        t15 = ((uint8_t)1);
        goto me16;
mn17:;
        t15 = ((uint8_t)0);
        goto me16;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 43);
me16:;
        t9 = t15;
    } else t9 = 1;
    if (!t9) {
        uint8_t t20;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t23 != ((uint8_t)2ULL)) goto mn22;
        t20 = ((uint8_t)1);
        goto me21;
mn22:;
        t20 = ((uint8_t)0);
        goto me21;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 100, 60);
me21:;
        t8 = t20;
    } else t8 = 1;
    if (!t8) {
        gm_ts6oqppjh0ba3 t25 = gm_l1;
        gm_ts6oqppjh0ba3 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)4ULL));
        gm_ts6oqppjh0ba3 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        gm_t9ldr1q5hn9fq t28;
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 0), t26);
        GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 2), t27);
        uint8_t t29 = 0;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t28) + 0, ((uint8_t *)&t25))) t29 = 1;
        if (gm_eqbqb4gb1d92(((uint8_t *)&t28) + 2, ((uint8_t *)&t25))) t29 = 1;
        uint8_t t30 = (uint8_t)(t29);
        t7 = t30;
    } else t7 = 1;
    t2 = t7;
    goto me3;
mn4:;
    t2 = ((uint8_t)0);
    goto me3;
mn31:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 99, 47);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_relational_n3goics F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t3fcbuehh3bs2 t1 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)a0));
    gm_t72hb7duiv9b4 t2;
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)41ULL));
    gm_t3fcbuehh3bs2 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t4 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t4 + 8), t3);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 0, t4);
    gm_t9qkh0rsooc8d t5;
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)42ULL));
    gm_t3fcbuehh3bs2 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t6 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 8), t5);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 32, t6);
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)43ULL));
    gm_t3fcbuehh3bs2 t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t8 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8 + 8), t7);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 64, t8);
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)44ULL));
    gm_t3fcbuehh3bs2 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 8), t9);
    GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t2 + 96, t10);
    uint8_t t11 = 0;
    if (gm_eqt7ftn1841u(((uint8_t *)&t2) + 0, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t2) + 32, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t2) + 64, ((uint8_t *)&t1))) t11 = 1;
    if (gm_eqt7ftn1841u(((uint8_t *)&t2) + 96, ((uint8_t *)&t1))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_is_relational_n3goics_c(gm_t3fcbuehh3bs2 a0) { return gm_f_is_relational_n3goics(&a0); }
inline __attribute__((aligned(4))) void gm_f_mul_from_mlskgvm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_mul_from_mlskgvm F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tc3oht4ksth9d t2 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
        F.l2 = t2;
    }
    {
        for (;;) {
            void * t4 = gm_l0;
            gm_t3fcbuehh3bs2 t5;
            gm_f_kind_k239kpg((void *)&t5, t4);
            gm_tt7vn9ehj0acr t6 = gm_f_mul_op_ab6loj8(&t5);
            if (!((((uint8_t *)&t6))[2] != 0)) goto lb3;
            gm_ts6oqppjh0ba3 t7 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&t6));
            gm_l3 = t7;
            {
                {
                    void * t11 = gm_l0;
                    gm_tm4pc0emhrv1g t12;
                    gm_f_bump_q2i40re((void *)&t12, t11);
                }
            }
            {
                void * t13 = gm_l0;
                gm_to1cssm8dfiv5 t14;
                gm_f_parse_prefix_671fi6d((void *)&t14, (void *)NULL, cx, NULL, t13);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (((((uint8_t *)&t14))[0] != 0)) {
                    gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 8));
                    gm_to1cssm8dfiv5 t16;
                    memset(&t16, 0, sizeof t16);
                    ((uint8_t *)&t16)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
                    F.ret = t16;
                    goto gm_fret;
                }
                F.l4 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t14) + 8));
            }
            {
                void * t17 = gm_l0;
                gm_ts6oqppjh0ba3 t18 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), (void *)NULL, cx, NULL, t17, t18, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc3:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
    }
    {
        const uint8_t *t19 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t19, 88);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mul_op_ab6loj8 F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 91u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma9; default: goto ma9; }
ma3:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t13 != ((uint8_t)8ULL)) goto mn12;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn12;
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16 + 1), t15);
    gm_tt7vn9ehj0acr t17;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t17), t16);
    ((uint8_t *)&t17)[2] = 1;
    t1 = t17;
    goto me2;
mn12:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t19 != ((uint8_t)8ULL)) goto mn18;
    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t20 != ((uint8_t)12ULL)) goto mn18;
    gm_t9qkh0rsooc8d t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 1), t21);
    gm_tt7vn9ehj0acr t23;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[2] = 1;
    t1 = t23;
    goto me2;
mn18:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)8ULL)) goto mn24;
    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t26 != ((uint8_t)16ULL)) goto mn24;
    gm_t9qkh0rsooc8d t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t28;
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 1), t27);
    gm_tt7vn9ehj0acr t29;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t29), t28);
    ((uint8_t *)&t29)[2] = 1;
    t1 = t29;
    goto me2;
mn24:;
ma6:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t31 != ((uint8_t)8ULL)) goto mn30;
    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t32 != ((uint8_t)20ULL)) goto mn30;
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 1), t33);
    gm_tt7vn9ehj0acr t35;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[2] = 1;
    t1 = t35;
    goto me2;
mn30:;
ma7:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t37 != ((uint8_t)8ULL)) goto mn36;
    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t38 != ((uint8_t)3ULL)) goto mn36;
    gm_ts6oqppjh0ba3 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)4ULL));
    gm_tt7vn9ehj0acr t40;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t40), t39);
    ((uint8_t *)&t40)[2] = 1;
    t1 = t40;
    goto me2;
mn36:;
ma8:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t42 != ((uint8_t)8ULL)) goto mn41;
    uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t43 != ((uint8_t)4ULL)) goto mn41;
    gm_ts6oqppjh0ba3 t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)5ULL));
    gm_tt7vn9ehj0acr t45;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t45), t44);
    ((uint8_t *)&t45)[2] = 1;
    t1 = t45;
    goto me2;
mn41:;
ma9:;
    gm_tt7vn9ehj0acr t47;
    memset(&t47, 0, sizeof t47);
    t1 = t47;
    goto me2;
mn46:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 73, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_mul_op_ab6loj8_c(gm_t3fcbuehh3bs2 a0) { return gm_f_mul_op_ab6loj8(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_text_3o86dat F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    gm_t9qkh0rsooc8d gm_l2; /* kw */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)8ULL)) goto mn8;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l1 = t10;
    gm_t9qkh0rsooc8d t11 = gm_l1;
    gm_b16_8 t12 = gm_f_punct_text_ov4r40u(t11);
    t1 = t12;
    goto me2;
mn8:;
ma4:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    gm_t9qkh0rsooc8d t15 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
    gm_l2 = t15;
    gm_t9qkh0rsooc8d t16 = gm_l2;
    gm_b16_8 t17 = gm_f_keyword_text_149oqn4(t16);
    t1 = t17;
    goto me2;
mn13:;
ma5:;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(0));
    t1 = t19;
    goto me2;
mn18:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 252, 49);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_op_text_3o86dat_c(gm_t3fcbuehh3bs2 a0) { return gm_f_op_text_3o86dat(&a0); }
inline __attribute__((aligned(4))) void gm_f_parens_0pje0fd(void *gm_out, uint32_t a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parens_0pje0fd F;
    uint32_t gm_l0; /* file */
    uint32_t gm_l1; /* lo */
    uint32_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 12, 0, 4);
        uint32_t t2 = gm_l0;
        uint32_t t3 = gm_l1;
        uint32_t t4 = gm_l1;
        gm_tl874t2atnlh3 t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t2);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 4), t3);
        GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t4);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 0, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_smooaouqmm8dj));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 0)) + 16, t6);
        memset((uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 12, 0, 4);
        uint32_t t7 = gm_l0;
        uint32_t t8 = gm_l2;
        uint32_t t9 = gm_l2;
        gm_tl874t2atnlh3 t10;
        GM_ST(uint32_t, ((uint8_t *)&t10 + 0), t7);
        GM_ST(uint32_t, ((uint8_t *)&t10 + 4), t8);
        GM_ST(uint32_t, ((uint8_t *)&t10 + 8), t9);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 0, t10);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s1asl1e1tvb0k));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbgd14esf0jvi *)((uint8_t *)&(*(gm_t0vh9i8m50f41 *)gm_out) + 32)) + 16, t11);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t0vh9i8m50f41 gm_f_parens_0pje0fd_c(uint32_t a0, uint32_t a1, uint32_t a2) { gm_t0vh9i8m50f41 gm_r; gm_f_parens_0pje0fd(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_parse_add_dh9nvbt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_add_dh9nvbt F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_t3fcbuehh3bs2 t8;
        gm_f_kind_k239kpg((void *)&t8, t7);
        uint8_t t9 = gm_f_binary_follows_jeb42bi(&t8);
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie6;
        {
            const uint8_t *t12 = ((uint8_t *)&F.l1);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t12, 88);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t13 = gm_l0;
        gm_f_add_from_7os0rvi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t13, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_parse_arg_t6fq0nq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_arg_t6fq0nq F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    GM_STACK_CHECK(&F, 2450u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t5 = gm_l0;
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)50ULL));
        uint8_t t7 = gm_f_at_54906rm(t5, t6);
        if (!t7) goto ie4;
        {
            {
                void * t10 = gm_l0;
                gm_tm4pc0emhrv1g t11;
                gm_f_bump_q2i40re((void *)&t11, t10);
            }
        }
        {
            void * t12 = gm_l0;
            gm_to1cssm8dfiv5 t13;
            gm_f_parse_expr_bkb2n6e((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
                gm_t3q68r4ne5jv8 t15;
                memset(&t15, 0, sizeof t15);
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
                F.ret = t15;
                goto gm_fret;
            }
            F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t13) + 8));
        }
        {
            gm_t7te2tdpebbt4 t16;
            memset((uint8_t *)&t16 + 140, 0, 4);
            gm_tc3oht4ksth9d t17 = F.l2;
            gm_t22mtv3sd9prf t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t18 + 8), t17);
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t16 + 0, t18);
            void * t19 = gm_l0;
            uint32_t t20 = gm_l1;
            gm_tl874t2atnlh3 t21 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t19, t20));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t16 + 128, t21);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out))[0] = 0;
            GM_ST(gm_t7te2tdpebbt4, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 8, t16);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t23 = gm_l0;
        uint8_t t24 = gm_f_nth_is_name_oqhoobd(t23, ((uint64_t)0ULL));
        if (!t24) goto ie22;
        void * t25 = gm_l0;
        gm_t9qkh0rsooc8d t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)56ULL));
        uint8_t t27 = gm_f_nth_is_3fgqmcb(t25, ((uint64_t)1ULL), t26);
        if (!t27) goto ie22;
        {
            void * t30 = gm_l0;
            gm_t3fcbuehh3bs2 t31;
            gm_f_kind_k239kpg((void *)&t31, t30);
            uint8_t t32 = ((GM_LD(uint8_t, ((uint8_t *)&t31) + 0) == ((uint8_t)1ULL)));
            if (!t32) goto ie29;
            {
                void * t34 = gm_l0;
                void * t35 = gm_l0;
                gm_tl874t2atnlh3 t36 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t35));
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s4niue72anpss));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(36));
                gm_b0_1 t38 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t34, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            void * t39 = gm_l0;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s3tpa8q2q7llg));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(16));
            gm_t4jn8fl3lmk5k t41;
            gm_f_parse_name_figjvbe((void *)&t41, (void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t41))[0] != 0)) {
                gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 8));
                gm_t3q68r4ne5jv8 t43;
                memset(&t43, 0, sizeof t43);
                ((uint8_t *)&t43)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t43 + 8), t42);
                F.ret = t43;
                goto gm_fret;
            }
            gm_l3 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t41) + 8));
        }
        {
            {
                void * t45 = gm_l0;
                gm_tm4pc0emhrv1g t46;
                gm_f_bump_q2i40re((void *)&t46, t45);
            }
        }
        {
            void * t47 = gm_l0;
            gm_to1cssm8dfiv5 t48;
            gm_f_parse_expr_bkb2n6e((void *)&t48, (void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t48))[0] != 0)) {
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 8));
                gm_t3q68r4ne5jv8 t50;
                memset(&t50, 0, sizeof t50);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t50 + 8), t49);
                F.ret = t50;
                goto gm_fret;
            }
            F.l4 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t48) + 8));
        }
        {
            gm_t7te2tdpebbt4 t51;
            memset((uint8_t *)&t51 + 140, 0, 4);
            gm_t56mj5hnm2uc6 t52;
            memcpy((uint8_t *)&t52 + 0, ((uint8_t *)&gm_l3), 32);
            memcpy((uint8_t *)&t52 + 32, ((uint8_t *)&F.l4), 88);
            gm_t22mtv3sd9prf t53;
            GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&t53 + 8), t52);
            GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t51 + 0, t53);
            void * t54 = gm_l0;
            uint32_t t55 = gm_l1;
            gm_tl874t2atnlh3 t56 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t54, t55));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t51 + 128, t56);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out))[0] = 0;
            GM_ST(gm_t7te2tdpebbt4, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 8, t51);
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t57 = gm_l0;
        gm_to1cssm8dfiv5 t58;
        gm_f_parse_expr_bkb2n6e((void *)&t58, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t58))[0] != 0)) {
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 8));
            gm_t3q68r4ne5jv8 t60;
            memset(&t60, 0, sizeof t60);
            ((uint8_t *)&t60)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
            F.ret = t60;
            goto gm_fret;
        }
        F.l5 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t58) + 8));
    }
    {
        gm_t7te2tdpebbt4 t61;
        memset((uint8_t *)&t61 + 140, 0, 4);
        gm_tc3oht4ksth9d t62 = F.l5;
        gm_t22mtv3sd9prf t63;
        memset(&t63, 0, sizeof t63);
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t63 + 8), t62);
        GM_ST(gm_t22mtv3sd9prf, (uint8_t *)&t61 + 0, t63);
        void * t64 = gm_l0;
        uint32_t t65 = gm_l1;
        gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t64, t65));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t61 + 128, t66);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out))[0] = 0;
        GM_ST(gm_t7te2tdpebbt4, (uint8_t *)&(*(gm_t3q68r4ne5jv8 *)gm_out) + 8, t61);
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
inline __attribute__((aligned(4))) void gm_f_parse_bang_irmkots(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_bang_irmkots F;
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 633u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        void * t4 = F.l0;
        gm_t3fcbuehh3bs2 t5;
        gm_f_nth_c0r2nvs((void *)&t5, t4, ((uint64_t)1ULL));
        uint8_t t6 = gm_f_can_begin_type_a3rjrm7(&t5);
        if (!t6) goto ie3;
        {
            void * t8 = F.l0;
            gm_closure t9 = ((gm_closure){(void *)gm_f_fn_1s091rv, (void *)&F.h, cx});
            gm_f_speculate_djd8kc6((void *)&F.l3, (void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&F.l3))[88] != 0)) goto ie10;
            gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3));
            F.l4 = t11;
            {
                void * t13 = F.l0;
                void * t14 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t15 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3mf58gv9kf3n t16;
                GM_ST(void *, ((uint8_t *)&t16 + 0), t14);
                GM_ST(void *, ((uint8_t *)&t16 + 8), t15);
                gm_t2gtbh39ptekd t17;
                memset(&t17, 0, sizeof t17);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)17ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t17 + 8), t16);
                void * t18 = F.l0;
                uint32_t t19 = gm_l2;
                gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
                gm_tc3oht4ksth9d t21;
                gm_f_mk_expr_53qj7nd((void *)&t21, t13, &t17, GM_CONV(gm_cseahvg8v7p0vs, t20));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
                GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t21);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        {
            void * t23 = F.l0;
            gm_tm4pc0emhrv1g t24;
            gm_f_bump_q2i40re((void *)&t24, t23);
        }
    }
    {
        void * t25 = F.l0;
        void * t26 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2gtbh39ptekd t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t27 + 8), t26);
        void * t28 = F.l0;
        uint32_t t29 = gm_l2;
        gm_tl874t2atnlh3 t30 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t28, t29));
        gm_tc3oht4ksth9d t31;
        gm_f_mk_expr_53qj7nd((void *)&t31, t25, &t27, GM_CONV(gm_cseahvg8v7p0vs, t30));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t31);
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
inline __attribute__((aligned(4))) void gm_f_parse_bit_h83vs72(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_bit_h83vs72 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_t3fcbuehh3bs2 t8;
        gm_f_kind_k239kpg((void *)&t8, t7);
        uint8_t t9 = gm_f_binary_follows_jeb42bi(&t8);
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie6;
        {
            const uint8_t *t12 = ((uint8_t *)&F.l1);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t12, 88);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t13 = gm_l0;
        gm_f_bit_from_pu2bq19((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t13, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_block_expr_0j6b78q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_block_expr_0j6b78q F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 493u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saojtqmfeak72));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_t9qkh0rsooc8d t4 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
            gm_to1cssm8dfiv5 t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        void * t8 = gm_l0;
        gm_toc4mrkn2e7o9 t9;
        gm_f_parse_block_tpg3f1i((void *)&t9, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 8));
            gm_to1cssm8dfiv5 t11;
            memset(&t11, 0, sizeof t11);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        F.l1 = (*(gm_t79rbdv4rafe5 *)(((uint8_t *)&t9) + 8));
    }
    {
        void * t12 = gm_l0;
        gm_t79rbdv4rafe5 t13 = F.l1;
        gm_t2gtbh39ptekd t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t14 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)44ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t14 + 8), t13);
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
        gm_tc3oht4ksth9d t16;
        gm_f_mk_expr_53qj7nd((void *)&t16, t12, &t14, GM_CONV(gm_cseahvg8v7p0vs, t15));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t16);
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
inline __attribute__((aligned(4))) void gm_f_parse_break_hehputf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_break_hehputf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    void * gm_l4; /* value */
    GM_STACK_CHECK(&F, 816u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
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
        gm_f_eat_label_7eokqmm((void *)&F.l2, t7);
    }
    {
        uint8_t t9;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 773, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t15 = gm_l0;
            gm_t3fcbuehh3bs2 t16;
            gm_f_kind_k239kpg((void *)&t16, t15);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            if (t17 != ((uint8_t)0ULL)) goto ie14;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_l3 = t18;
            {
                void * t21 = gm_l0;
                gm_t3fcbuehh3bs2 t22;
                gm_f_nth_c0r2nvs((void *)&t22, t21, ((uint64_t)1ULL));
                uint8_t t23 = gm_f_ends_expr_after_name_5t7p9lg(&t22);
                if (!t23) goto ie20;
                {
                    void * t25 = gm_l0;
                    void * t26 = gm_l0;
                    gm_tl874t2atnlh3 t27 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t26));
                    gm_b16_8 t28 = gm_l3;
                    gm_tnpifv20irnph t29;
                    GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
                    gm_tabb710d9mq2e t30;
                    gm_f_format_pb6f6q3((void *)&t30, (void *)NULL, cx, NULL, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src0dtioet3p4f6, 776, 42);
                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                    gm_b0_1 t33 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t25, t27, t32);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
                    ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t33);
                    goto gm_fdone;
                }
                goto ix20;
ie20:;
ix20:;
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
        gm_l4 = ((void *)0);
    }
    {
        void * t35 = gm_l0;
        gm_t3fcbuehh3bs2 t36;
        gm_f_kind_k239kpg((void *)&t36, t35);
        uint8_t t37 = gm_f_can_begin_expr_rmia9bs(&t36);
        if (!t37) goto ie34;
        {
            void * t39 = gm_l0;
            gm_to1cssm8dfiv5 t40;
            gm_f_parse_expr_bkb2n6e((void *)&t40, (void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) {
                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 8));
                gm_to1cssm8dfiv5 t42;
                memset(&t42, 0, sizeof t42);
                ((uint8_t *)&t42)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t42 + 8), t41);
                F.ret = t42;
                goto gm_fret;
            }
            void * t43 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&t40) + 8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t43;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t44 = gm_l0;
        gm_tchr52g0ema53 t45;
        memcpy((uint8_t *)&t45 + 0, ((uint8_t *)&F.l2), 40);
        void * t46 = gm_l4;
        GM_ST(void *, (uint8_t *)&t45 + 40, t46);
        gm_t2gtbh39ptekd t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)46ULL));
        GM_ST(gm_tchr52g0ema53, ((uint8_t *)&t47 + 8), t45);
        void * t48 = gm_l0;
        uint32_t t49 = gm_l1;
        gm_tl874t2atnlh3 t50 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t48, t49));
        gm_tc3oht4ksth9d t51;
        gm_f_mk_expr_53qj7nd((void *)&t51, t44, &t47, GM_CONV(gm_cseahvg8v7p0vs, t50));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t51);
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
inline __attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_call_args_32e205h F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    uint8_t gm_l3; /* trailing */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_t9qkh0rsooc8d t4;
            GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)61ULL));
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t6 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t3, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t6))[0] != 0)) {
                gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 4));
                gm_thk954m1ol1ig t8;
                GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t8 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t8 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
                ((uint8_t *)&t8)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
                F.ret = t8;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t6) + 4));
        }
    }
    {
        void * t10 = gm_l0;
        gm_toh0ootodhjue t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t11 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t11 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t12 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t10, GM_CONV(gm_cs3cuou245tr5f, t11)));
        gm_l1 = t12;
    }
    {
    }
    {
        gm_f_list_6oos54v((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        for (;;) {
            void * t17 = gm_l0;
            gm_t9qkh0rsooc8d t18;
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)62ULL));
            uint8_t t19 = gm_f_at_54906rm(t17, t18);
            uint8_t t20 = (uint8_t)!t19;
            if (!t20) goto lb16;
            {
                void * t23 = (void *)((uint8_t *)&F.l2);
                void * t24 = gm_l0;
                gm_t3q68r4ne5jv8 t25;
                gm_f_parse_arg_t6fq0nq((void *)&t25, (void *)NULL, cx, NULL, t24);
                if (gm_unw.kind) {
                    goto gm_ll21;
                }
                if (((((uint8_t *)&t25))[0] != 0)) {
                    gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 8));
                    gm_thk954m1ol1ig t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t27 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t27 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
                    ((uint8_t *)&t27)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), t26);
                    F.ret = t27;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll21;
                }
                gm_tftb1odc739t5 t28 = gm_f_push_jis5lhu(t23, &(*(gm_t7te2tdpebbt4 *)(((uint8_t *)&t25) + 8)));
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src0dtioet3p4f6, 558, 9);
                gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
            }
            {
                gm_l3 = ((uint8_t)0);
            }
            {
                void * t31 = gm_l0;
                gm_t9qkh0rsooc8d t32;
                GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)57ULL));
                uint8_t t33 = gm_f_eat_qmock9g(t31, t32);
                uint8_t t34 = (uint8_t)!t33;
                if (!t34) goto ie30;
                {
                    goto lb16;
                }
                goto ix30;
ie30:;
ix30:;
            }
            {
                gm_l3 = ((uint8_t)1);
            }
lc16:;
        }
gm_ll21:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
        }
        goto gm_ld1_1;
lb16:;
    }
    {
        {
            void * t37 = gm_l0;
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)62ULL));
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s5us2hgd173vu));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(26));
            gm_t0u0j6voh3loe t40 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t37, t38, t39);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            if (((((uint8_t *)&t40))[0] != 0)) {
                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 4));
                gm_thk954m1ol1ig t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t42 + 24, 0);
                ((uint8_t *)&t42)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t42 + 8), t41);
                F.ret = t42;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t40) + 4));
        }
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        memset((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.ret + 8)) + 17, 0, 7);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.ret + 8)) + 0, t44);
        uint8_t t45 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&F.ret + 8)) + 16, t45);
        uint8_t t46 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t46;
        goto gm_ld1_1;
    }
    {
        {
            void * t48 = gm_l0;
            gm_toh0ootodhjue t49 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t48) + 128), t49);
        }
    }
    goto gm_ls50;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t52 = gm_l0;
            gm_toh0ootodhjue t53 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t52) + 128), t53);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls50:;
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
extern __attribute__((aligned(4))) void gm_f_parse_capture_body_fgf89k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cmp_q5clpig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cmp_q5clpig F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* start_pos */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    uint8_t gm_l5; /* first_rel */
    gm_tl874t2atnlh3 gm_l8; /* at */
    uint8_t gm_l10; /* negated */
    gm_th2lqbo77pvsc gm_l11; /* fix */
    gm_t08olq97rscn3 gm_l12; /* mid */
    int32_t gm_l13; /* count */
    gm_b16_8 gm_l14; /* text */
    GM_STACK_CHECK(&F, 1778u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 72));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_to1cssm8dfiv5 t5;
        gm_f_parse_prefix_671fi6d((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t5) + 8));
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        uint8_t t11 = gm_f_binary_follows_jeb42bi(&t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie8;
        {
            const uint8_t *t14 = ((uint8_t *)&F.l2);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t14, 88);
            goto gm_fdone;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t15 = gm_l0;
        gm_to1cssm8dfiv5 t16;
        gm_f_range_from_5v6ko9m((void *)&t16, (void *)NULL, cx, NULL, t15, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t16) + 8));
    }
    {
        void * t19 = gm_l0;
        gm_t3fcbuehh3bs2 t20;
        gm_f_kind_k239kpg((void *)&t20, t19);
        gm_tt7vn9ehj0acr t21 = gm_f_cmp_op_9gj7im4(&t20);
        gm_ts6oqppjh0ba3 t22;
        if (((((uint8_t *)&t21))[2] != 0)) {
            gm_ts6oqppjh0ba3 t23 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&t21) + 0));
            t22 = t23;
        } else {
            const uint8_t *t24 = ((uint8_t *)&F.l3);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t24, 88);
            goto gm_fdone;
        }
        gm_l4 = t22;
    }
    {
        void * t25 = gm_l0;
        gm_t3fcbuehh3bs2 t26;
        gm_f_kind_k239kpg((void *)&t26, t25);
        uint8_t t27 = gm_f_is_relational_n3goics(&t26);
        gm_l5 = t27;
    }
    {
        {
            void * t29 = gm_l0;
            gm_tm4pc0emhrv1g t30;
            gm_f_bump_q2i40re((void *)&t30, t29);
        }
    }
    {
        void * t31 = gm_l0;
        gm_to1cssm8dfiv5 t32;
        gm_f_parse_range_o9o2al7((void *)&t32, (void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t32))[0] != 0)) {
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 8));
            gm_to1cssm8dfiv5 t34;
            memset(&t34, 0, sizeof t34);
            ((uint8_t *)&t34)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t34 + 8), t33);
            F.ret = t34;
            goto gm_fret;
        }
        F.l6 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t32) + 8));
    }
    {
        void * t35 = gm_l0;
        gm_ts6oqppjh0ba3 t36 = gm_l4;
        gm_f_binary_41fr5r5((void *)&F.l7, (void *)NULL, cx, NULL, t35, t36, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t38 = gm_l0;
        gm_t3fcbuehh3bs2 t39;
        gm_f_kind_k239kpg((void *)&t39, t38);
        gm_tt7vn9ehj0acr t40 = gm_f_cmp_op_9gj7im4(&t39);
        uint8_t t41;
        if (((((uint8_t *)&t40))[2] != 0)) goto mn43;
        t41 = ((uint8_t)1);
        goto me42;
mn43:;
        t41 = ((uint8_t)0);
        goto me42;
mn44:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 225, 8);
me42:;
        if (!t41) goto ie37;
        {
            const uint8_t *t46 = ((uint8_t *)&F.l7);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t46, 88);
            goto gm_fdone;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        void * t47 = gm_l0;
        gm_tl874t2atnlh3 t48 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t47));
        gm_l8 = t48;
    }
    {
        gm_tl874t2atnlh3 t49 = gm_l8;
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s5lrfo6g7933d));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(49));
        gm_f_error_s6hn871((void *)&F.l9, (void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t51 = 0;
        uint64_t t52 = gm_l1;
        uint8_t t53 = (t52 > ((uint64_t)0ULL));
        if (t53) {
            uint64_t t54 = gm_l1;
            uint64_t r55; bool o56 = __builtin_sub_overflow(t54, ((uint64_t)1ULL), &r55);
            if (o56) gm_trap("integer overflow in `-`", gm_src0dtioet3p4f6, 231, 41);
            uint64_t t57 = r55;
            uint64_t t58 = (uintptr_t)t57;
            void * t59 = gm_l0;
            uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)t59) + 8) + 8);
            if (t58 >= (uintptr_t)(t60)) gm_trap_index(t58, t60, gm_src0dtioet3p4f6, 231, 34);
            gm_t3fcbuehh3bs2 t61 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, (((uint8_t *)t59) + 8)) + t58 * 48) + 0));
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)18ULL));
            uint8_t t63 = (((GM_LD(uint8_t, ((uint8_t *)&t61) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t61) + 8), ((uint8_t *)&t62))));
            t51 = t63;
        } else t51 = 0;
        gm_l10 = t51;
    }
    {
        gm_th2lqbo77pvsc t64;
        memset(&t64, 0, sizeof t64);
        gm_l11 = t64;
    }
    {
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
        if (t66 != ((uint8_t)6ULL)) goto ie65;
        gm_t08olq97rscn3 t67 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l6) + 8) + 8));
        gm_l12 = t67;
        {
            uint8_t t70 = gm_l5;
            if (!t70) goto ie69;
            void * t71 = gm_l0;
            gm_t3fcbuehh3bs2 t72;
            gm_f_kind_k239kpg((void *)&t72, t71);
            uint8_t t73 = gm_f_is_relational_n3goics(&t72);
            if (!t73) goto ie69;
            uint8_t t74 = gm_l10;
            uint8_t t75 = (uint8_t)!t74;
            if (!t75) goto ie69;
            {
                gm_t6j6smj7qoctu t77;
                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t77 + 0, t78);
                void * t79 = gm_l0;
                gm_t3fcbuehh3bs2 t80;
                gm_f_kind_k239kpg((void *)&t80, t79);
                gm_b16_8 t81 = gm_f_op_text_3o86dat(&t80);
                GM_ST(gm_b16_8, (uint8_t *)&t77 + 16, t81);
                gm_tabb710d9mq2e t82;
                gm_f_format_5k9iv40((void *)&t82, (void *)NULL, cx, NULL, &t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_src0dtioet3p4f6, 235, 19);
                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l11)) + 0, t84);
            }
            goto ix69;
ie69:;
ix69:;
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        gm_l13 = ((int32_t)0LL);
    }
    {
        for (;;) {
            void * t86 = gm_l0;
            gm_t3fcbuehh3bs2 t87;
            gm_f_kind_k239kpg((void *)&t87, t86);
            gm_tt7vn9ehj0acr t88 = gm_f_cmp_op_9gj7im4(&t87);
            uint8_t t89;
            if (((((uint8_t *)&t88))[2] != 0)) goto mn91;
            t89 = ((uint8_t)0);
            goto me90;
mn91:;
            t89 = ((uint8_t)1);
            goto me90;
mn92:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 239, 11);
me90:;
            if (!t89) goto lb85;
            {
                {
                    void * t96 = gm_l0;
                    gm_tm4pc0emhrv1g t97;
                    gm_f_bump_q2i40re((void *)&t97, t96);
                }
            }
            {
                {
                    void * t99 = gm_l0;
                    gm_to1cssm8dfiv5 t100;
                    gm_f_parse_range_o9o2al7((void *)&t100, (void *)NULL, cx, NULL, t99);
                    if (gm_unw.kind) {
                        goto gm_ll93;
                    }
                    if (((((uint8_t *)&t100))[0] != 0)) {
                        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 8));
                        gm_to1cssm8dfiv5 t102;
                        memset(&t102, 0, sizeof t102);
                        ((uint8_t *)&t102)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t102 + 8), t101);
                        F.ret = t102;
                        goto gm_fret;
                    }
                }
            }
            {
                int32_t t103 = gm_l13;
                int32_t r104; bool o105 = __builtin_add_overflow(t103, ((int32_t)1LL), &r104);
                if (o105) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 242, 9);
                int32_t t106 = r104;
                gm_l13 = t106;
            }
lc85:;
        }
gm_ll93:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb85; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc85; }
        }
        goto gm_lad;
lb85:;
    }
    {
        if (!((((uint8_t *)&gm_l11))[16] != 0)) goto ie107;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l14 = t108;
        {
            int32_t t111 = gm_l13;
            uint8_t t112 = ((t111 == ((int32_t)1LL)));
            if (!t112) goto ie110;
            {
                void * t114 = (void *)((uint8_t *)&F.l9);
                gm_b16_8 t115;
                GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s27pt4vcvtscl));
                GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(23));
                gm_tl874t2atnlh3 t116 = gm_l8;
                gm_b16_8 t117 = gm_l14;
                gm_b0_1 t118 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t114, t115, t116, t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix110;
ie110:;
ix110:;
        }
        goto ix107;
ie107:;
ix107:;
    }
    {
        void * t119 = gm_l0;
        gm_f_report_diag_big1nsf(t119, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
    }
    {
        const uint8_t *t120 = ((uint8_t *)&F.l7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t120, 88);
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
inline __attribute__((aligned(4))) void gm_f_parse_continue_ertnf67(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_continue_ertnf67 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* sym */
    GM_STACK_CHECK(&F, 502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
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
        gm_f_eat_label_7eokqmm((void *)&F.l2, t7);
    }
    {
        uint8_t t9;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 796, 8);
me10:;
        if (!t9) goto ie8;
        {
            void * t15 = gm_l0;
            gm_t3fcbuehh3bs2 t16;
            gm_f_kind_k239kpg((void *)&t16, t15);
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            if (t17 != ((uint8_t)0ULL)) goto ie14;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_l3 = t18;
            {
                void * t20 = gm_l0;
                void * t21 = gm_l0;
                gm_tl874t2atnlh3 t22 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t21));
                gm_b16_8 t23 = gm_l3;
                gm_tnpifv20irnph t24;
                GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
                gm_tabb710d9mq2e t25;
                gm_f_format_pb6f6q3((void *)&t25, (void *)NULL, cx, NULL, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src0dtioet3p4f6, 798, 38);
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                gm_b0_1 t28 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t20, t22, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
                ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t28);
                goto gm_fdone;
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
        void * t29 = gm_l0;
        gm_t1s75i08qtptd t30 = F.l2;
        gm_t2gtbh39ptekd t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)47ULL));
        GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t31 + 8), t30);
        void * t32 = gm_l0;
        uint32_t t33 = gm_l1;
        gm_tl874t2atnlh3 t34 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t32, t33));
        gm_tc3oht4ksth9d t35;
        gm_f_mk_expr_53qj7nd((void *)&t35, t29, &t31, GM_CONV(gm_cseahvg8v7p0vs, t34));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t35);
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
__attribute__((aligned(4))) void gm_f_parse_dot_suffix_1fk204i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_dot_suffix_1fk204i F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* n */
    gm_t9qkh0rsooc8d gm_l5; /* k */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l7; /* l */
    uint32_t gm_l8; /* index */
    uint64_t gm_l9; /* v */
    GM_STACK_CHECK(&F, 1209u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        {
            void * t4 = gm_l0;
            gm_tm4pc0emhrv1g t5;
            gm_f_bump_q2i40re((void *)&t5, t4);
        }
    }
    {
        void * t6 = gm_l0;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t6));
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; default: goto ma14; }
ma11:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l4 = t19;
        {
            {
                void * t22 = gm_l0;
                gm_tm4pc0emhrv1g t23;
                gm_f_bump_q2i40re((void *)&t23, t22);
            }
        }
        {
            void * t24 = gm_l0;
            gm_tioie2ju3q4ul t25;
            void * t26 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t25 + 0, t26);
            memset((uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 8)) + 28, 0, 4);
            gm_b16_8 t27 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 8)) + 0, t27);
            gm_tl874t2atnlh3 t28 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)((uint8_t *)&t25 + 8)) + 16, t28);
            gm_t2gtbh39ptekd t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t29 + 8), t25);
            void * t30 = gm_l0;
            uint32_t t31 = gm_l2;
            gm_tl874t2atnlh3 t32 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t30, t31));
            gm_tc3oht4ksth9d t33;
            gm_f_mk_expr_53qj7nd((void *)&t33, t24, &t29, GM_CONV(gm_cseahvg8v7p0vs, t32));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t33);
            goto gm_fdone;
        }
        goto me10;
mn17:;
ma12:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t35 != ((uint8_t)2ULL)) goto mn34;
        gm_t9qkh0rsooc8d t36 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t9) + 8));
        gm_l5 = t36;
        {
            void * t38 = gm_l0;
            gm_t9qkh0rsooc8d t39 = gm_l5;
            gm_f_keyword_as_name_ipm23p9((void *)&gm_l6, (void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = gm_l0;
            gm_tioie2ju3q4ul t41;
            void * t42 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t41 + 0, t42);
            memcpy((uint8_t *)&t41 + 8, ((uint8_t *)&gm_l6), 32);
            gm_t2gtbh39ptekd t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t43 + 8), t41);
            void * t44 = gm_l0;
            uint32_t t45 = gm_l2;
            gm_tl874t2atnlh3 t46 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t44, t45));
            gm_tc3oht4ksth9d t47;
            gm_f_mk_expr_53qj7nd((void *)&t47, t40, &t43, GM_CONV(gm_cseahvg8v7p0vs, t46));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t47);
            goto gm_fdone;
        }
        goto me10;
mn34:;
ma13:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t49 != ((uint8_t)3ULL)) goto mn48;
        void * t50 = GM_LD(void *, (((uint8_t *)&t9) + 8));
        gm_l7 = t50;
        {
            {
                void * t53 = gm_l0;
                gm_tm4pc0emhrv1g t54;
                gm_f_bump_q2i40re((void *)&t54, t53);
            }
        }
        {
            gm_l8 = ((uint32_t)0ULL);
        }
        {
            void * t56 = gm_l7;
            gm_tf5srh4mp4b3i t57 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)t56) + 0)));
            if (!((((uint8_t *)&t57))[8] != 0)) goto ie55;
            uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&t57));
            gm_l9 = t58;
            uint64_t t59 = gm_l9;
            uint8_t t60 = (t59 <= ((uint64_t)4294967295ULL));
            if (!t60) goto ie55;
            void * t61 = gm_l7;
            uint8_t t62;
            if ((((((uint8_t *)t61) + 48))[8] != 0)) goto mn64;
            t62 = ((uint8_t)1);
            goto me63;
mn64:;
            t62 = ((uint8_t)0);
            goto me63;
mn65:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 470, 63);
me63:;
            if (!t62) goto ie55;
            {
                uint64_t t67 = gm_l9;
                uint32_t t68 = (uint32_t)t67;
                gm_l8 = t68;
            }
            goto ix55;
ie55:;
            {
                void * t70 = gm_l0;
                gm_tl874t2atnlh3 t71 = gm_l3;
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sbckhsd8r0nr0));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(19));
                gm_b0_1 t73 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t70, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix55:;
        }
        {
            void * t74 = gm_l0;
            gm_tupk62cr6lbdu t75;
            void * t76 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t75 + 0, t76);
            uint32_t t77 = gm_l8;
            GM_ST(uint32_t, (uint8_t *)&t75 + 8, t77);
            gm_tl874t2atnlh3 t78 = gm_l3;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t75 + 12, t78);
            gm_t2gtbh39ptekd t79;
            memset(&t79, 0, sizeof t79);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)23ULL));
            GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t79 + 8), t75);
            void * t80 = gm_l0;
            uint32_t t81 = gm_l2;
            gm_tl874t2atnlh3 t82 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t80, t81));
            gm_tc3oht4ksth9d t83;
            gm_f_mk_expr_53qj7nd((void *)&t83, t74, &t79, GM_CONV(gm_cseahvg8v7p0vs, t82));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t83);
            goto gm_fdone;
        }
        goto me10;
mn48:;
ma14:;
        void * t85 = gm_l0;
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s8vhc5kn5i8tv));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(37));
        gm_b0_1 t87 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t87);
        goto gm_fdone;
        goto me10;
mn84:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 458, 5);
me10:;
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
inline __attribute__((aligned(4))) void gm_f_parse_else_smoqgg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_else_smoqgg4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1051u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_logic_ajejref((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)7ULL));
        uint8_t t9 = gm_f_at_kw_p0r3sr9(t7, t8);
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie6;
        {
            const uint8_t *t12 = ((uint8_t *)&F.l1);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t12, 88);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        {
            void * t14 = gm_l0;
            gm_tm4pc0emhrv1g t15;
            gm_f_bump_q2i40re((void *)&t15, t14);
        }
    }
    {
        gm_t1s75i08qtptd t16;
        memset(&t16, 0, sizeof t16);
        F.l2 = t16;
    }
    {
        void * t18 = gm_l0;
        uint8_t t19 = gm_f_nth_is_name_oqhoobd(t18, ((uint64_t)0ULL));
        if (!t19) goto ie17;
        void * t20 = gm_l0;
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)59ULL));
        uint8_t t22 = gm_f_nth_is_3fgqmcb(t20, ((uint64_t)1ULL), t21);
        if (!t22) goto ie17;
        {
            void * t24 = gm_l0;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s1pvh8pfrs3ir));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(6));
            gm_t4jn8fl3lmk5k t26;
            gm_f_parse_name_figjvbe((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t26))[0] != 0)) {
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
                gm_to1cssm8dfiv5 t28;
                memset(&t28, 0, sizeof t28);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
                F.ret = t28;
                goto gm_fret;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)) + 36, 0);
            ((uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)))[32] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l2)) + 0, (*(gm_t08olq97rscn3 *)(((uint8_t *)&t26) + 8)));
        }
        {
            {
                void * t30 = gm_l0;
                gm_tm4pc0emhrv1g t31;
                gm_f_bump_q2i40re((void *)&t31, t30);
            }
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t32 = gm_l0;
        uint8_t t33;
        if (((((uint8_t *)&F.l2))[32] != 0)) goto mn35;
        t33 = ((uint8_t)0);
        goto me34;
mn35:;
        t33 = ((uint8_t)1);
        goto me34;
mn36:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 135, 33);
me34:;
        gm_to1cssm8dfiv5 t37;
        gm_f_parse_fallback_g1vpkra((void *)&t37, (void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) {
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
            gm_to1cssm8dfiv5 t39;
            memset(&t39, 0, sizeof t39);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), t38);
            F.ret = t39;
            goto gm_fret;
        }
        F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t37) + 8));
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)((uint8_t *)&F.l1);
        void * t42 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t43 = gm_f_ambiguous_try_else_0v14t0i((void *)NULL, cx, NULL, t40, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t44 = gm_l0;
        gm_tioi3jh9v22g5 t45;
        void * t46 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t45 + 0, t46);
        memcpy((uint8_t *)&t45 + 8, ((uint8_t *)&F.l2), 40);
        void * t47 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t45 + 48, t47);
        gm_t2gtbh39ptekd t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)14ULL));
        GM_ST(gm_tioi3jh9v22g5, ((uint8_t *)&t48 + 8), t45);
        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 72));
        gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
        gm_tl874t2atnlh3 t51 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t49), GM_CONV(gm_cseahvg8v7p0vs, t50)));
        gm_tc3oht4ksth9d t52;
        gm_f_mk_expr_53qj7nd((void *)&t52, t44, &t48, GM_CONV(gm_cseahvg8v7p0vs, t51));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t52);
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
inline __attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_expr_bkb2n6e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
    }
    {
    }
    {
        void * t8 = gm_l0;
        gm_f_parse_else_smoqgg4((void *)&F.ret, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t9 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t9;
        goto gm_ld1_1;
    }
    {
        void * t10 = gm_l0;
        gm_f_leave_rb20u32(t10);
    }
    goto gm_ls11;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t12 = gm_l0;
        gm_f_leave_rb20u32(t12);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls11:;
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
inline __attribute__((aligned(4))) void gm_f_parse_fallback_g1vpkra(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fallback_g1vpkra F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* captured */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_f_parse_capture_body_fgf89k2((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5);
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
        void * t7 = gm_l0;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)65ULL));
        uint8_t t9 = gm_f_at_54906rm(t7, t8);
        if (!t9) goto ie6;
        void * t10 = gm_l0;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 128) + 0));
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie6;
        {
            void * t14 = gm_l0;
            gm_f_parse_block_expr_0j6b78q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
    }
    {
        void * t21 = gm_l0;
        gm_f_parse_else_smoqgg4((void *)&F.ret, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t22 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t22;
        goto gm_ld1_1;
    }
    {
        void * t23 = gm_l0;
        gm_f_leave_rb20u32(t23);
    }
    goto gm_ls24;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t25 = gm_l0;
        gm_f_leave_rb20u32(t25);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls24:;
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
inline __attribute__((aligned(4))) void gm_f_parse_head_expr_6sofdhn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_head_expr_6sofdhn F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
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
            gm_toh0ootodhjue t12 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t11) + 128), t12);
        }
    }
    goto gm_ls13;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t15 = gm_l0;
            gm_toh0ootodhjue t16 = gm_l1;
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
inline __attribute__((aligned(4))) void gm_f_parse_import_s1hv231(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_import_s1hv231 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tibupt4dmcqai gm_l2; /* l */
    GM_STACK_CHECK(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)5ULL)) goto ie7;
        gm_l2 = (*(gm_tibupt4dmcqai *)(((uint8_t *)&t9) + 8));
        {
            {
                void * t13 = gm_l0;
                gm_tm4pc0emhrv1g t14;
                gm_f_bump_q2i40re((void *)&t14, t13);
            }
        }
        {
            void * t15 = gm_l0;
            gm_tibupt4dmcqai t16 = gm_l2;
            gm_t2gtbh39ptekd t17;
            memset(&t17, 0, sizeof t17);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)38ULL));
            GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t17 + 8), t16);
            void * t18 = gm_l0;
            uint32_t t19 = gm_l1;
            gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
            gm_tc3oht4ksth9d t21;
            gm_f_mk_expr_53qj7nd((void *)&t21, t15, &t17, GM_CONV(gm_cseahvg8v7p0vs, t20));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t21);
            goto gm_fdone;
        }
        goto ix7;
ie7:;
        {
            void * t23 = gm_l0;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s8sl52ot187i2));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(49));
            gm_b0_1 t25 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t25);
            goto gm_fdone;
        }
ix7:;
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
inline __attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_intrinsic_4ou54h3 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* sym */
    gm_t08olq97rscn3 gm_l3; /* name */
    gm_th2lqbo77pvsc gm_l4; /* args */
    GM_STACK_CHECK(&F, 583u);
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
        if (t4 != ((uint8_t)6ULL)) goto ie3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 0) + 8));
        gm_l2 = t5;
        {
            memset((uint8_t *)&gm_l3 + 28, 0, 4);
            gm_b16_8 t7 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&gm_l3 + 0, t7);
            gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 32));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&gm_l3 + 16, t8);
        }
        {
            gm_th2lqbo77pvsc t9;
            memset(&t9, 0, sizeof t9);
            gm_l4 = t9;
        }
        {
            void * t11 = gm_l0;
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)61ULL));
            uint8_t t13 = gm_f_at_54906rm(t11, t12);
            if (!t13) goto ie10;
            {
                void * t15 = gm_l0;
                gm_thk954m1ol1ig t16;
                gm_f_parse_call_args_32e205h((void *)&t16, (void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t16))[0] != 0)) {
                    gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
                    gm_to1cssm8dfiv5 t18;
                    memset(&t18, 0, sizeof t18);
                    ((uint8_t *)&t18)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
                    F.ret = t18;
                    goto gm_fret;
                }
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((((uint8_t *)&t16) + 8) + 0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 0, t19);
            }
            goto ix10;
ie10:;
ix10:;
        }
        {
            void * t20 = gm_l0;
            gm_tet9guhev5r4u t21;
            memcpy((uint8_t *)&t21 + 0, ((uint8_t *)&gm_l3), 32);
            memcpy((uint8_t *)&t21 + 32, ((uint8_t *)&gm_l4), 24);
            gm_t2gtbh39ptekd t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)49ULL));
            GM_ST(gm_tet9guhev5r4u, ((uint8_t *)&t22 + 8), t21);
            void * t23 = gm_l0;
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 32) + 4));
            gm_tl874t2atnlh3 t25 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t23, t24));
            gm_tc3oht4ksth9d t26;
            gm_f_mk_expr_53qj7nd((void *)&t26, t20, &t22, GM_CONV(gm_cseahvg8v7p0vs, t25));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t26);
            goto gm_fdone;
        }
        goto ix3;
ie3:;
        {
            void * t28 = gm_l0;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sjrimf03ml0ct));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(12));
            gm_b0_1 t30 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t30);
            goto gm_fdone;
        }
ix3:;
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
inline __attribute__((aligned(4))) void gm_f_parse_logic_ajejref(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_logic_ajejref F;
    void * gm_l0; /* p */
    gm_tt7vn9ehj0acr gm_l2; /* chain */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_not_a8b3gjf((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        gm_tt7vn9ehj0acr t6;
        memset(&t6, 0, sizeof t6);
        gm_l2 = t6;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb7;
            {
                gm_ts6oqppjh0ba3 t10;
                GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)20ULL));
                gm_l3 = t10;
            }
            {
                void * t12 = gm_l0;
                gm_t9qkh0rsooc8d t13;
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)20ULL));
                uint8_t t14 = gm_f_at_kw_p0r3sr9(t12, t13);
                if (!t14) goto ie11;
                {
                    gm_ts6oqppjh0ba3 t16;
                    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)21ULL));
                    gm_l3 = t16;
                }
                goto ix11;
ie11:;
                uint8_t t18 = 0;
                void * t19 = gm_l0;
                gm_t9qkh0rsooc8d t20;
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)0ULL));
                uint8_t t21 = gm_f_at_kw_p0r3sr9(t19, t20);
                uint8_t t22 = (uint8_t)!t21;
                if (!t22) {
                    void * t23 = gm_l0;
                    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 128) + 1));
                    t18 = t24;
                } else t18 = 1;
                if (!t18) goto ie17;
                {
                    goto lb7;
                }
                goto ix17;
ie17:;
ix17:;
ix11:;
            }
            {
                uint8_t t27;
                if (((((uint8_t *)&gm_l2))[2] != 0)) goto mn29;
                t27 = ((uint8_t)1);
                goto me28;
mn29:;
                t27 = ((uint8_t)0);
                goto me28;
mn30:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 185, 12);
me28:;
                if (!t27) goto ie26;
                {
                    gm_ts6oqppjh0ba3 t32 = gm_l3;
                    gm_tt7vn9ehj0acr t33;
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t33), t32);
                    ((uint8_t *)&t33)[2] = 1;
                    gm_l2 = t33;
                }
                goto ix26;
ie26:;
                gm_tt7vn9ehj0acr t35 = gm_l2;
                gm_ts6oqppjh0ba3 t36 = gm_l3;
                gm_tt7vn9ehj0acr t37;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t37), t36);
                ((uint8_t *)&t37)[2] = 1;
                uint8_t t38 = (!gm_eqcdhc1ipuua(((uint8_t *)&t35), ((uint8_t *)&t37)));
                if (!t38) goto ie34;
                {
                    void * t40 = gm_l0;
                    void * t41 = gm_l0;
                    gm_tl874t2atnlh3 t42 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t41));
                    gm_b16_8 t43;
                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_snemlnnsbdb13));
                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(45));
                    gm_b0_1 t44 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t40, t42, t43);
                    if (gm_unw.kind) {
                        goto gm_ll8;
                    }
                }
                goto ix34;
ie34:;
ix34:;
ix26:;
            }
            {
                {
                    void * t46 = gm_l0;
                    gm_tm4pc0emhrv1g t47;
                    gm_f_bump_q2i40re((void *)&t47, t46);
                }
            }
            {
                void * t48 = gm_l0;
                gm_to1cssm8dfiv5 t49;
                gm_f_parse_not_a8b3gjf((void *)&t49, (void *)NULL, cx, NULL, t48);
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
                if (((((uint8_t *)&t49))[0] != 0)) {
                    gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 8));
                    gm_to1cssm8dfiv5 t51;
                    memset(&t51, 0, sizeof t51);
                    ((uint8_t *)&t51)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t51 + 8), t50);
                    F.ret = t51;
                    goto gm_fret;
                }
                F.l4 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t49) + 8));
            }
            {
                void * t52 = gm_l0;
                gm_ts6oqppjh0ba3 t53 = gm_l3;
                gm_f_binary_41fr5r5((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)), (void *)NULL, cx, NULL, t52, t53, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_ll8;
                }
            }
lc7:;
        }
gm_ll8:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
        }
        goto gm_lad;
lb7:;
    }
    {
        const uint8_t *t54 = ((uint8_t *)&F.l1);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t54, 88);
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
extern __attribute__((aligned(4))) void gm_f_parse_mul_35e1t49(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_parse_not_a8b3gjf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_not_a8b3gjf F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)18ULL));
        uint8_t t5 = gm_f_at_kw_p0r3sr9(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            gm_f_parse_cmp_q5clpig((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t8);
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
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        {
            void * t12 = gm_l0;
            gm_tm4pc0emhrv1g t13;
            gm_f_bump_q2i40re((void *)&t13, t12);
        }
    }
    {
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            gm_to1cssm8dfiv5 t17;
            memset(&t17, 0, sizeof t17);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), t16);
            F.ret = t17;
            goto gm_fret;
        }
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
    }
    {
        void * t20 = gm_l0;
        gm_to1cssm8dfiv5 t21;
        gm_f_parse_not_a8b3gjf((void *)&t21, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t21))[0] != 0)) {
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 8));
            gm_to1cssm8dfiv5 t23;
            memset(&t23, 0, sizeof t23);
            ((uint8_t *)&t23)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
            F.ret = t23;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t21) + 8));
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        void * t24 = gm_l0;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)5ULL));
        void * t26 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t8ghgl2jj0qha t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 0), t25);
        GM_ST(void *, ((uint8_t *)&t27 + 8), t26);
        gm_t2gtbh39ptekd t28;
        memset(&t28, 0, sizeof t28);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t28 + 8), t27);
        void * t29 = gm_l0;
        uint32_t t30 = gm_l1;
        gm_tl874t2atnlh3 t31 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t29, t30));
        gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.ret + 8)), t24, &t28, GM_CONV(gm_cseahvg8v7p0vs, t31));
        uint8_t t32 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t32;
        goto gm_ld1_1;
    }
    {
        void * t33 = gm_l0;
        gm_f_leave_rb20u32(t33);
    }
    goto gm_ls34;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t35 = gm_l0;
        gm_f_leave_rb20u32(t35);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls34:;
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
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_paren_sq1guoi F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    gm_toh0ootodhjue gm_l3; /* old */
    uint8_t gm_l5; /* comma */
    gm_tqs5p4n2tq19v gm_l6; /* named */
    uint32_t gm_l7; /* elem_start */
    gm_tl874t2atnlh3 gm_l11; /* whole */
    GM_STACK_CHECK(&F, 2743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l2 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_t9qkh0rsooc8d t6;
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)61ULL));
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t8 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t8))[0] != 0)) {
                gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 4));
                gm_to1cssm8dfiv5 t10;
                memset(&t10, 0, sizeof t10);
                ((uint8_t *)&t10)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), t9);
                F.ret = t10;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 4));
        }
    }
    {
        void * t12 = gm_l0;
        gm_toh0ootodhjue t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t13 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t14 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t12, GM_CONV(gm_cs3cuou245tr5f, t13)));
        gm_l3 = t14;
    }
    {
    }
    {
        gm_f_list_fvvkq82((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        gm_tqs5p4n2tq19v t18;
        memset(&t18, 0, sizeof t18);
        gm_l6 = t18;
    }
    {
        for (;;) {
            void * t20 = gm_l0;
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)62ULL));
            uint8_t t22 = gm_f_at_54906rm(t20, t21);
            uint8_t t23 = (uint8_t)!t22;
            if (!t23) goto lb19;
            {
                void * t26 = gm_l0;
                uint32_t t27 = gm_f_lo_fp82gt4(t26);
                gm_l7 = t27;
            }
            {
                void * t29 = gm_l0;
                gm_t9qkh0rsooc8d t30;
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)50ULL));
                uint8_t t31 = gm_f_at_54906rm(t29, t30);
                if (!t31) goto ie28;
                {
                    {
                        void * t34 = gm_l0;
                        gm_tm4pc0emhrv1g t35;
                        gm_f_bump_q2i40re((void *)&t35, t34);
                    }
                }
                {
                    uint8_t t37 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                    if (!t37) goto ie36;
                    {
                        void * t39 = gm_l0;
                        void * t40 = gm_l0;
                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 0));
                        uint32_t t42 = gm_l7;
                        void * t43 = gm_l0;
                        gm_tl874t2atnlh3 t44 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t43));
                        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&t44) + 8));
                        gm_tl874t2atnlh3 t46;
                        GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t41);
                        GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t42);
                        GM_ST(uint32_t, ((uint8_t *)&t46 + 8), t45);
                        gm_b16_8 t47;
                        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_shbaullahutpe));
                        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(36));
                        gm_b0_1 t48 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t39, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_ll24;
                        }
                    }
                    goto ix36;
ie36:;
ix36:;
                }
                {
                    gm_to1cssm8dfiv5 t49;
                    uint8_t t51 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                    if (!t51) goto ie50;
                    void * t53 = gm_l0;
                    gm_to1cssm8dfiv5 t54;
                    gm_f_parse_expr_bkb2n6e((void *)&t54, (void *)NULL, cx, NULL, t53);
                    if (gm_unw.kind) {
                        goto gm_ll24;
                    }
                    gm_to1cssm8dfiv5 t55 = t54;
                    t49 = t55;
                    goto ix50;
ie50:;
                    void * t57 = gm_l0;
                    gm_to1cssm8dfiv5 t58;
                    gm_f_parse_type_2m93dtq((void *)&t58, (void *)NULL, cx, NULL, t57);
                    if (gm_unw.kind) {
                        goto gm_ll24;
                    }
                    gm_to1cssm8dfiv5 t59 = t58;
                    t49 = t59;
ix50:;
                    if (((((uint8_t *)&t49))[0] != 0)) {
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 8));
                        gm_to1cssm8dfiv5 t61;
                        memset(&t61, 0, sizeof t61);
                        ((uint8_t *)&t61)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t61 + 8), t60);
                        F.ret = t61;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                        goto gm_ll24;
                    }
                    F.l8 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t49) + 8));
                }
                {
                    void * t62 = (void *)((uint8_t *)&F.l4);
                    gm_tv1o1j44ii4db t63;
                    memset((uint8_t *)&t63 + 129, 0, 7);
                    gm_t1s75i08qtptd t64;
                    memset(&t64, 0, sizeof t64);
                    GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t63 + 0, t64);
                    memcpy((uint8_t *)&t63 + 40, ((uint8_t *)&F.l8), 88);
                    GM_ST(uint8_t, (uint8_t *)&t63 + 128, ((uint8_t)1));
                    gm_tftb1odc739t5 t65 = gm_f_push_l8mrumu(t62, &t63);
                    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src0dtioet3p4f6, 704, 13);
                    gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                }
                {
                    void * t67 = gm_l0;
                    gm_t9qkh0rsooc8d t68;
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)57ULL));
                    uint8_t t69 = gm_f_eat_qmock9g(t67, t68);
                    gm_l5 = t69;
                }
                {
                    uint8_t t71 = gm_l5;
                    uint8_t t72 = (uint8_t)!t71;
                    if (!t72) goto ie70;
                    {
                        goto lb19;
                    }
                    goto ix70;
ie70:;
ix70:;
                }
                {
                    goto lc19;
                }
                goto ix28;
ie28:;
ix28:;
            }
            {
                gm_t1s75i08qtptd t74;
                memset(&t74, 0, sizeof t74);
                F.l9 = t74;
            }
            {
                void * t76 = gm_l0;
                uint8_t t77 = gm_f_nth_is_name_oqhoobd(t76, ((uint64_t)0ULL));
                if (!t77) goto ie75;
                void * t78 = gm_l0;
                gm_t9qkh0rsooc8d t79;
                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)56ULL));
                uint8_t t80 = gm_f_nth_is_3fgqmcb(t78, ((uint64_t)1ULL), t79);
                if (!t80) goto ie75;
                {
                    void * t82 = gm_l0;
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s1pvh8pfrs3ir));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(6));
                    gm_t4jn8fl3lmk5k t84;
                    gm_f_parse_name_figjvbe((void *)&t84, (void *)NULL, cx, NULL, t82, t83);
                    if (gm_unw.kind) {
                        goto gm_ll24;
                    }
                    if (((((uint8_t *)&t84))[0] != 0)) {
                        gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 8));
                        gm_to1cssm8dfiv5 t86;
                        memset(&t86, 0, sizeof t86);
                        ((uint8_t *)&t86)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t86 + 8), t85);
                        F.ret = t86;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                        goto gm_ll24;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l9)) + 33, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l9)) + 34, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l9)) + 36, 0);
                    ((uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l9)))[32] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t1s75i08qtptd *)((uint8_t *)&F.l9)) + 0, (*(gm_t08olq97rscn3 *)(((uint8_t *)&t84) + 8)));
                }
                {
                    {
                        void * t88 = gm_l0;
                        gm_tm4pc0emhrv1g t89;
                        gm_f_bump_q2i40re((void *)&t89, t88);
                    }
                }
                goto ix75;
ie75:;
ix75:;
            }
            {
                uint8_t t91;
                if (((((uint8_t *)&gm_l6))[1] != 0)) goto mn93;
                t91 = ((uint8_t)1);
                goto me92;
mn93:;
                t91 = ((uint8_t)0);
                goto me92;
mn94:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 714, 12);
me92:;
                if (!t91) goto ie90;
                {
                    uint8_t t96;
                    if (((((uint8_t *)&F.l9))[32] != 0)) goto mn98;
                    t96 = ((uint8_t)0);
                    goto me97;
mn98:;
                    t96 = ((uint8_t)1);
                    goto me97;
mn99:;
                    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 715, 21);
me97:;
                    gm_tqs5p4n2tq19v t100;
                    GM_ST(uint8_t, ((uint8_t *)&t100), t96);
                    ((uint8_t *)&t100)[1] = 1;
                    gm_l6 = t100;
                }
                goto ix90;
ie90:;
                gm_tqs5p4n2tq19v t102 = gm_l6;
                uint8_t t103;
                if (((((uint8_t *)&F.l9))[32] != 0)) goto mn105;
                t103 = ((uint8_t)0);
                goto me104;
mn105:;
                t103 = ((uint8_t)1);
                goto me104;
mn106:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 716, 33);
me104:;
                gm_tqs5p4n2tq19v t107;
                GM_ST(uint8_t, ((uint8_t *)&t107), t103);
                ((uint8_t *)&t107)[1] = 1;
                uint8_t t108 = (!gm_eqebiqthsuot(((uint8_t *)&t102), ((uint8_t *)&t107)));
                if (!t108) goto ie101;
                {
                    void * t110 = gm_l0;
                    void * t111 = gm_l0;
                    uint32_t t112 = GM_LD(uint32_t, (((uint8_t *)t111) + 0));
                    uint32_t t113 = gm_l7;
                    void * t114 = gm_l0;
                    gm_tl874t2atnlh3 t115 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t114));
                    uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&t115) + 8));
                    gm_tl874t2atnlh3 t117;
                    GM_ST(uint32_t, ((uint8_t *)&t117 + 0), t112);
                    GM_ST(uint32_t, ((uint8_t *)&t117 + 4), t113);
                    GM_ST(uint32_t, ((uint8_t *)&t117 + 8), t116);
                    gm_b16_8 t118;
                    GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sam3pd3469drv));
                    GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(54));
                    gm_b0_1 t119 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t110, t117, t118);
                    if (gm_unw.kind) {
                        goto gm_ll24;
                    }
                }
                goto ix101;
ie101:;
ix101:;
ix90:;
            }
            {
                gm_to1cssm8dfiv5 t120;
                uint8_t t122 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
                if (!t122) goto ie121;
                void * t124 = gm_l0;
                gm_to1cssm8dfiv5 t125;
                gm_f_parse_expr_bkb2n6e((void *)&t125, (void *)NULL, cx, NULL, t124);
                if (gm_unw.kind) {
                    goto gm_ll24;
                }
                gm_to1cssm8dfiv5 t126 = t125;
                t120 = t126;
                goto ix121;
ie121:;
                void * t128 = gm_l0;
                gm_to1cssm8dfiv5 t129;
                gm_f_parse_type_2m93dtq((void *)&t129, (void *)NULL, cx, NULL, t128);
                if (gm_unw.kind) {
                    goto gm_ll24;
                }
                gm_to1cssm8dfiv5 t130 = t129;
                t120 = t130;
ix121:;
                if (((((uint8_t *)&t120))[0] != 0)) {
                    gm_b0_1 t131 = GM_LD(gm_b0_1, (((uint8_t *)&t120) + 8));
                    gm_to1cssm8dfiv5 t132;
                    memset(&t132, 0, sizeof t132);
                    ((uint8_t *)&t132)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t132 + 8), t131);
                    F.ret = t132;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll24;
                }
                F.l10 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t120) + 8));
            }
            {
                void * t133 = (void *)((uint8_t *)&F.l4);
                gm_tv1o1j44ii4db t134;
                memset((uint8_t *)&t134 + 129, 0, 7);
                memcpy((uint8_t *)&t134 + 0, ((uint8_t *)&F.l9), 40);
                memcpy((uint8_t *)&t134 + 40, ((uint8_t *)&F.l10), 88);
                GM_ST(uint8_t, (uint8_t *)&t134 + 128, ((uint8_t)0));
                gm_tftb1odc739t5 t135 = gm_f_push_l8mrumu(t133, &t134);
                if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_src0dtioet3p4f6, 720, 9);
                gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
            }
            {
                void * t137 = gm_l0;
                gm_t9qkh0rsooc8d t138;
                GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)57ULL));
                uint8_t t139 = gm_f_eat_qmock9g(t137, t138);
                gm_l5 = t139;
            }
            {
                uint8_t t141 = gm_l5;
                uint8_t t142 = (uint8_t)!t141;
                if (!t142) goto ie140;
                {
                    goto lb19;
                }
                goto ix140;
ie140:;
ix140:;
            }
lc19:;
        }
gm_ll24:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
        }
        goto gm_ld1_1;
lb19:;
    }
    {
        {
            void * t145 = gm_l0;
            gm_t9qkh0rsooc8d t146;
            GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)62ULL));
            gm_b16_8 t147;
            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sa3k87epai61e));
            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(24));
            gm_t0u0j6voh3loe t148 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t145, t146, t147);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            if (((((uint8_t *)&t148))[0] != 0)) {
                gm_b0_1 t149 = GM_LD(gm_b0_1, (((uint8_t *)&t148) + 4));
                gm_to1cssm8dfiv5 t150;
                memset(&t150, 0, sizeof t150);
                ((uint8_t *)&t150)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t150 + 8), t149);
                F.ret = t150;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t148) + 4));
        }
    }
    {
        void * t152 = gm_l0;
        uint32_t t153 = gm_l2;
        gm_tl874t2atnlh3 t154 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t152, t153));
        gm_l11 = t154;
    }
    {
        uint64_t t156 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t157 = ((t156 == ((uint64_t)1ULL)));
        if (!t157) goto ie155;
        uint8_t t158 = gm_l5;
        uint8_t t159 = (uint8_t)!t158;
        if (!t159) goto ie155;
        uint64_t t160 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t161 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_src0dtioet3p4f6, 726, 47);
        uint8_t t162;
        if ((((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t160 * 136) + 0))[32] != 0)) goto mn164;
        t162 = ((uint8_t)1);
        goto me163;
mn164:;
        t162 = ((uint8_t)0);
        goto me163;
mn165:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 726, 47);
me163:;
        if (!t162) goto ie155;
        uint64_t t166 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t167 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t166 >= (uintptr_t)(t167)) gm_trap_index(t166, t167, gm_src0dtioet3p4f6, 726, 83);
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t166 * 136) + 128));
        uint8_t t169 = (uint8_t)!t168;
        if (!t169) goto ie155;
        {
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            void * t171 = gm_l0;
            uint64_t t172 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t173 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
            if (t172 >= (uintptr_t)(t173)) gm_trap_index(t172, t173, gm_src0dtioet3p4f6, 727, 37);
            void * t174 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t172 * 136) + 40)));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t2gtbh39ptekd t175;
            memset(&t175, 0, sizeof t175);
            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)9ULL));
            GM_ST(void *, ((uint8_t *)&t175 + 8), t174);
            gm_tl874t2atnlh3 t176 = gm_l11;
            gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.ret + 8)), t171, &t175, GM_CONV(gm_cseahvg8v7p0vs, t176));
            uint8_t t177 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t177;
            goto gm_ld1_1;
        }
        goto ix155;
ie155:;
ix155:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        void * t178 = gm_l0;
        gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_t2gtbh39ptekd t180;
        memset(&t180, 0, sizeof t180);
        GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 8), t179);
        gm_tl874t2atnlh3 t181 = gm_l11;
        gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.ret + 8)), t178, &t180, GM_CONV(gm_cseahvg8v7p0vs, t181));
        uint8_t t182 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t182;
        goto gm_ld1_1;
    }
    {
        {
            void * t184 = gm_l0;
            gm_toh0ootodhjue t185 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t184) + 128), t185);
        }
    }
    goto gm_ls186;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t188 = gm_l0;
            gm_toh0ootodhjue t189 = gm_l3;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t188) + 128), t189);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls186:;
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
extern __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_parse_postfix_ops_2p1csn3 F;
    void * gm_l0; /* p */
    uint8_t gm_l2; /* allow_bang */
    uint32_t gm_l4; /* links */
    uint32_t gm_l5; /* start */
    gm_t3fcbuehh3bs2 gm_l6; /* k */
    gm_th2lqbo77pvsc gm_l7; /* args */
    GM_STACK_CHECK(&F, 1941u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        gm_tc3oht4ksth9d t2 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)a1));
        F.l3 = t2;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 72) + 4));
                gm_l5 = t6;
            }
            {
                void * t7 = gm_l0;
                gm_f_kind_k239kpg((void *)&gm_l6, t7);
            }
            {
                uint8_t t9 = 0;
                gm_t3fcbuehh3bs2 t10 = gm_l6;
                gm_tak1k6bk5qcen t11;
                gm_t9qkh0rsooc8d t12;
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)45ULL));
                gm_t3fcbuehh3bs2 t13;
                GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 0, t13);
                gm_t9qkh0rsooc8d t14;
                GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)47ULL));
                gm_t3fcbuehh3bs2 t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 8), t14);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 32, t15);
                gm_t9qkh0rsooc8d t16;
                GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)61ULL));
                gm_t3fcbuehh3bs2 t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t17 + 8), t16);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 64, t17);
                gm_t9qkh0rsooc8d t18;
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)63ULL));
                gm_t3fcbuehh3bs2 t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 8), t18);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 96, t19);
                gm_t9qkh0rsooc8d t20;
                GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)58ULL));
                gm_t3fcbuehh3bs2 t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t21 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 8), t20);
                GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t11 + 128, t21);
                uint8_t t22 = 0;
                if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 0, ((uint8_t *)&t10))) t22 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 32, ((uint8_t *)&t10))) t22 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 64, ((uint8_t *)&t10))) t22 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 96, ((uint8_t *)&t10))) t22 = 1;
                if (gm_eqt7ftn1841u(((uint8_t *)&t11) + 128, ((uint8_t *)&t10))) t22 = 1;
                uint8_t t23 = (uint8_t)(t22);
                if (!t23) {
                    uint8_t t24 = 0;
                    uint8_t t25 = gm_l2;
                    if (t25) {
                        gm_t3fcbuehh3bs2 t26 = gm_l6;
                        gm_t9qkh0rsooc8d t27;
                        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)46ULL));
                        uint8_t t28 = (((GM_LD(uint8_t, ((uint8_t *)&t26) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t26) + 8), ((uint8_t *)&t27))));
                        t24 = t28;
                    } else t24 = 0;
                    t9 = t24;
                } else t9 = 1;
                if (!t9) goto ie8;
                {
                    uint32_t t30 = gm_l4;
                    uint32_t r31; bool o32 = __builtin_add_overflow(t30, ((uint32_t)1ULL), &r31);
                    if (o32) gm_trap("integer overflow in `+`", gm_src0dtioet3p4f6, 426, 13);
                    uint32_t t33 = r31;
                    gm_l4 = t33;
                }
                {
                    uint32_t t35 = gm_l4;
                    uint32_t t36 = GM_LD(uint32_t, gm_cpvard348ed);
                    uint8_t t37 = (t35 > t36);
                    if (!t37) goto ie34;
                    {
                        void * t39 = gm_l0;
                        void * t40 = gm_l0;
                        gm_tl874t2atnlh3 t41 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t40));
                        uint32_t t42 = GM_LD(uint32_t, gm_cpvard348ed);
                        gm_t88rcucg1b7uk t43;
                        GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t42);
                        gm_tabb710d9mq2e t44;
                        gm_f_format_o5o20fv((void *)&t44, (void *)NULL, cx, NULL, t43);
                        if (gm_unw.kind) {
                            goto gm_ll4;
                        }
                        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_src0dtioet3p4f6, 428, 42);
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                        gm_b0_1 t47 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t39, t41, t46);
                        if (gm_unw.kind) {
                            goto gm_ll4;
                        }
                        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
                        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t47);
                        goto gm_fdone;
                    }
                    goto ix34;
ie34:;
ix34:;
                }
                goto ix8;
ie8:;
ix8:;
            }
            {
                gm_t3fcbuehh3bs2 t48 = gm_l6;
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                switch (t58) { case ((uint8_t)0ULL): goto ma56; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma56; case ((uint8_t)4ULL): goto ma56; case ((uint8_t)5ULL): goto ma56; case ((uint8_t)6ULL): goto ma56; case ((uint8_t)7ULL): goto ma56; case ((uint8_t)8ULL): goto ma50; case ((uint8_t)9ULL): goto ma56; case ((uint8_t)10ULL): goto ma56; default: goto ma56; }
ma50:;
                uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t60 != ((uint8_t)8ULL)) goto mn59;
                uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t61 != ((uint8_t)45ULL)) goto mn59;
                {
                    {
                        void * t64 = gm_l0;
                        gm_tm4pc0emhrv1g t65;
                        gm_f_bump_q2i40re((void *)&t65, t64);
                    }
                }
                {
                    void * t66 = gm_l0;
                    void * t67 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_t2gtbh39ptekd t68;
                    memset(&t68, 0, sizeof t68);
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)15ULL));
                    GM_ST(void *, ((uint8_t *)&t68 + 8), t67);
                    void * t69 = gm_l0;
                    uint32_t t70 = gm_l5;
                    gm_tl874t2atnlh3 t71 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t69, t70));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t66, &t68, GM_CONV(gm_cseahvg8v7p0vs, t71));
                }
                goto me49;
mn59:;
ma51:;
                uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t73 != ((uint8_t)8ULL)) goto mn72;
                uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t74 != ((uint8_t)46ULL)) goto mn72;
                uint8_t t75 = gm_l2;
                if (!t75) goto mn72;
                {
                    void * t77 = gm_l0;
                    gm_to1cssm8dfiv5 t78;
                    gm_f_parse_bang_irmkots((void *)&t78, (void *)NULL, cx, NULL, t77, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t78))[0] != 0)) {
                        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 8));
                        gm_to1cssm8dfiv5 t80;
                        memset(&t80, 0, sizeof t80);
                        ((uint8_t *)&t80)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t80 + 8), t79);
                        F.ret = t80;
                        goto gm_fret;
                    }
                    F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t78) + 8));
                }
                goto me49;
mn72:;
ma52:;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t82 != ((uint8_t)8ULL)) goto mn81;
                uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t83 != ((uint8_t)47ULL)) goto mn81;
                {
                    {
                        void * t86 = gm_l0;
                        gm_tm4pc0emhrv1g t87;
                        gm_f_bump_q2i40re((void *)&t87, t86);
                    }
                }
                {
                    void * t88 = gm_l0;
                    void * t89 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_t2gtbh39ptekd t90;
                    memset(&t90, 0, sizeof t90);
                    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&t90 + 8), t89);
                    void * t91 = gm_l0;
                    uint32_t t92 = gm_l5;
                    gm_tl874t2atnlh3 t93 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t91, t92));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t88, &t90, GM_CONV(gm_cseahvg8v7p0vs, t93));
                }
                goto me49;
mn81:;
ma53:;
                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t95 != ((uint8_t)8ULL)) goto mn94;
                uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t96 != ((uint8_t)61ULL)) goto mn94;
                {
                    void * t98 = gm_l0;
                    gm_thk954m1ol1ig t99;
                    gm_f_parse_call_args_32e205h((void *)&t99, (void *)NULL, cx, NULL, t98);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t99))[0] != 0)) {
                        gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 8));
                        gm_to1cssm8dfiv5 t101;
                        memset(&t101, 0, sizeof t101);
                        ((uint8_t *)&t101)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t101 + 8), t100);
                        F.ret = t101;
                        goto gm_fret;
                    }
                    gm_l7 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t99) + 8));
                }
                {
                    void * t102 = gm_l0;
                    gm_tlljg61a2ctgq t103;
                    memset((uint8_t *)&t103 + 25, 0, 7);
                    void * t104 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    GM_ST(void *, (uint8_t *)&t103 + 0, t104);
                    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t103 + 8, t105);
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
                    GM_ST(uint8_t, (uint8_t *)&t103 + 24, t106);
                    gm_t2gtbh39ptekd t107;
                    GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t107 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t107 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t107 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t107 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t107 + 8), t103);
                    void * t108 = gm_l0;
                    uint32_t t109 = gm_l5;
                    gm_tl874t2atnlh3 t110 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t108, t109));
                    gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)), t102, &t107, GM_CONV(gm_cseahvg8v7p0vs, t110));
                }
                goto me49;
mn94:;
ma54:;
                uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t112 != ((uint8_t)8ULL)) goto mn111;
                uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t113 != ((uint8_t)63ULL)) goto mn111;
                {
                    void * t115 = gm_l0;
                    gm_to1cssm8dfiv5 t116;
                    gm_f_parse_subscript_1hujuhl((void *)&t116, (void *)NULL, cx, NULL, t115, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t116))[0] != 0)) {
                        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 8));
                        gm_to1cssm8dfiv5 t118;
                        memset(&t118, 0, sizeof t118);
                        ((uint8_t *)&t118)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t118 + 8), t117);
                        F.ret = t118;
                        goto gm_fret;
                    }
                    F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t116) + 8));
                }
                goto me49;
mn111:;
ma55:;
                uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&t48) + 0));
                if (t120 != ((uint8_t)8ULL)) goto mn119;
                uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)&t48) + 8) + 0));
                if (t121 != ((uint8_t)58ULL)) goto mn119;
                {
                    void * t123 = gm_l0;
                    gm_to1cssm8dfiv5 t124;
                    gm_f_parse_dot_suffix_1fk204i((void *)&t124, (void *)NULL, cx, NULL, t123, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t124))[0] != 0)) {
                        gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 8));
                        gm_to1cssm8dfiv5 t126;
                        memset(&t126, 0, sizeof t126);
                        ((uint8_t *)&t126)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t126 + 8), t125);
                        F.ret = t126;
                        goto gm_fret;
                    }
                    F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t124) + 8));
                }
                goto me49;
mn119:;
ma56:;
                goto lb3;
                goto me49;
mn127:;
ma57:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 431, 9);
me49:;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
    }
    {
        const uint8_t *t128 = ((uint8_t *)&F.l3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t128, 88);
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
inline __attribute__((aligned(4))) void gm_f_parse_prefix_671fi6d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_prefix_671fi6d F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    uint32_t gm_l2; /* start */
    GM_STACK_CHECK(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t3fcbuehh3bs2 t3;
        gm_f_kind_k239kpg((void *)&t3, t2);
        gm_taari1s0m51a7 t4 = gm_f_prefix_op_japctlh(&t3);
        gm_t9qkh0rsooc8d t5;
        if (((((uint8_t *)&t4))[1] != 0)) {
            gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            gm_f_parse_postfix_ao4kstr((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t7, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l1 = t5;
    }
    {
        void * t8 = gm_l0;
        uint32_t t9 = gm_f_lo_fp82gt4(t8);
        gm_l2 = t9;
    }
    {
        {
            void * t11 = gm_l0;
            gm_tm4pc0emhrv1g t12;
            gm_f_bump_q2i40re((void *)&t12, t11);
        }
    }
    {
        void * t13 = gm_l0;
        gm_t9qkh0rsooc8d t14 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t14))[0] != 0)) {
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
            gm_to1cssm8dfiv5 t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
            F.ret = t16;
            goto gm_fret;
        }
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
    }
    {
        void * t19 = gm_l0;
        gm_to1cssm8dfiv5 t20;
        gm_f_parse_prefix_671fi6d((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t20))[0] != 0)) {
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 8));
            gm_to1cssm8dfiv5 t22;
            memset(&t22, 0, sizeof t22);
            ((uint8_t *)&t22)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t22 + 8), t21);
            F.ret = t22;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        F.l3 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t20) + 8));
    }
    {
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        void * t23 = gm_l0;
        gm_t9qkh0rsooc8d t24 = gm_l1;
        void * t25 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_t8ghgl2jj0qha t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26 + 0), t24);
        GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
        gm_t2gtbh39ptekd t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t27 + 8), t26);
        void * t28 = gm_l0;
        uint32_t t29 = gm_l2;
        gm_tl874t2atnlh3 t30 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t28, t29));
        gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.ret + 8)), t23, &t27, GM_CONV(gm_cseahvg8v7p0vs, t30));
        uint8_t t31 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t31;
        goto gm_ld1_1;
    }
    {
        void * t32 = gm_l0;
        gm_f_leave_rb20u32(t32);
    }
    goto gm_ls33;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t34 = gm_l0;
        gm_f_leave_rb20u32(t34);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls33:;
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
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_primary_1vvr4ji F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_t3fcbuehh3bs2 gm_l2; /* k */
    gm_t08olq97rscn3 gm_l3; /* label */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* l */
    void * gm_l6; /* l */
    gm_tibupt4dmcqai gm_l7; /* l */
    gm_b16_8 gm_l8; /* name */
    gm_t08olq97rscn3 gm_l9; /* name */
    GM_STACK_CHECK(&F, 4918u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_f_kind_k239kpg((void *)&gm_l2, t4);
    }
    {
        uint8_t t6;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 597, 8);
me7:;
        if (!t6) goto ie5;
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)56ULL));
        uint8_t t13 = gm_f_nth_is_3fgqmcb(t11, ((uint64_t)1ULL), t12);
        if (!t13) goto ie5;
        void * t14 = gm_l0;
        gm_t3fcbuehh3bs2 t15;
        gm_f_nth_c0r2nvs((void *)&t15, t14, ((uint64_t)2ULL));
        gm_t23tohc57ebhi t16;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)31ULL));
        gm_t3fcbuehh3bs2 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 8), t17);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 0, t18);
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)12ULL));
        gm_t3fcbuehh3bs2 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t20 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20 + 8), t19);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t16 + 32, t20);
        uint8_t t21 = 0;
        if (gm_eqt7ftn1841u(((uint8_t *)&t16) + 0, ((uint8_t *)&t15))) t21 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t16) + 32, ((uint8_t *)&t15))) t21 = 1;
        uint8_t t22 = (uint8_t)(t21);
        if (!t22) goto ie5;
        {
            void * t24 = gm_l0;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_so7em1q8clnib));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(7));
            gm_t4jn8fl3lmk5k t26;
            gm_f_parse_ident_n2c6b75((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t26))[0] != 0)) {
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
                gm_to1cssm8dfiv5 t28;
                memset(&t28, 0, sizeof t28);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), t27);
                F.ret = t28;
                goto gm_fret;
            }
            gm_l3 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t26) + 8));
        }
        {
            {
                void * t30 = gm_l0;
                gm_tm4pc0emhrv1g t31;
                gm_f_bump_q2i40re((void *)&t31, t30);
            }
        }
        {
            void * t33 = gm_l0;
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)31ULL));
            uint8_t t35 = gm_f_at_kw_p0r3sr9(t33, t34);
            if (!t35) goto ie32;
            {
                void * t37 = gm_l0;
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s1llcn0p4qesk));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(19));
                gm_t9qkh0rsooc8d t39 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t39))[0] != 0)) {
                    gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                    gm_to1cssm8dfiv5 t41;
                    memset(&t41, 0, sizeof t41);
                    ((uint8_t *)&t41)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t41 + 8), t40);
                    F.ret = t41;
                    goto gm_fret;
                }
                gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
            }
            {
                void * t43 = gm_l0;
                gm_t08olq97rscn3 t44 = gm_l3;
                gm_t1s75i08qtptd t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 36, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t45), t44);
                ((uint8_t *)&t45)[32] = 1;
                gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t43, &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t46 = gm_l0;
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sqm6u7rqmi1il));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t48 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t48))[0] != 0)) {
                gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                gm_to1cssm8dfiv5 t50;
                memset(&t50, 0, sizeof t50);
                ((uint8_t *)&t50)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t50 + 8), t49);
                F.ret = t50;
                goto gm_fret;
            }
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        {
            void * t52 = gm_l0;
            gm_t08olq97rscn3 t53 = gm_l3;
            gm_t1s75i08qtptd t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t54), t53);
            ((uint8_t *)&t54)[32] = 1;
            gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t52, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint8_t t56;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t59 != ((uint8_t)2ULL)) goto mn58;
        t56 = ((uint8_t)1);
        goto me57;
mn58:;
        t56 = ((uint8_t)0);
        goto me57;
mn60:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 607, 8);
me57:;
        if (!t56) goto ie55;
        gm_t3fcbuehh3bs2 t61 = gm_l2;
        gm_t41cj6gvba3r8 t62;
        gm_t9qkh0rsooc8d t63;
        GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)25ULL));
        gm_t3fcbuehh3bs2 t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t64 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t64 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t64 + 8), t63);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t62 + 0, t64);
        gm_t9qkh0rsooc8d t65;
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)10ULL));
        gm_t3fcbuehh3bs2 t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t66 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 8), t65);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t62 + 32, t66);
        gm_t9qkh0rsooc8d t67;
        GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)19ULL));
        gm_t3fcbuehh3bs2 t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t68 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t68 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t68 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 8), t67);
        GM_ST(gm_t3fcbuehh3bs2, (uint8_t *)&t62 + 64, t68);
        uint8_t t69 = 0;
        if (gm_eqt7ftn1841u(((uint8_t *)&t62) + 0, ((uint8_t *)&t61))) t69 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t62) + 32, ((uint8_t *)&t61))) t69 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t62) + 64, ((uint8_t *)&t61))) t69 = 1;
        uint8_t t70 = (uint8_t)(!t69);
        if (!t70) goto ie55;
        void * t71 = gm_l0;
        uint8_t t72 = gm_f_at_reported_kw_name_it872te(t71);
        if (!t72) goto ie55;
        {
            void * t74 = gm_l0;
            gm_f_bump_reported_kw_name_gs5eq1i((void *)&gm_l4, t74);
        }
        {
            void * t75 = gm_l0;
            gm_t08olq97rscn3 t76 = gm_l4;
            gm_t2gtbh39ptekd t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t77 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)6ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t77 + 8), t76);
            gm_tl874t2atnlh3 t78 = gm_l1;
            gm_tc3oht4ksth9d t79;
            gm_f_mk_expr_53qj7nd((void *)&t79, t75, &t77, GM_CONV(gm_cseahvg8v7p0vs, t78));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t79);
            goto gm_fdone;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t111) { case ((uint8_t)0ULL): goto ma88; case ((uint8_t)1ULL): goto ma86; case ((uint8_t)2ULL): goto ma84; case ((uint8_t)3ULL): goto ma81; case ((uint8_t)4ULL): goto ma82; case ((uint8_t)5ULL): goto ma83; case ((uint8_t)6ULL): goto ma105; case ((uint8_t)7ULL): goto ma106; case ((uint8_t)8ULL): goto ma87; case ((uint8_t)9ULL): goto ma109; case ((uint8_t)10ULL): goto ma109; default: goto ma109; }
ma81:;
        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t113 != ((uint8_t)3ULL)) goto mn112;
        void * t114 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        gm_l5 = t114;
        {
            {
                void * t117 = gm_l0;
                gm_tm4pc0emhrv1g t118;
                gm_f_bump_q2i40re((void *)&t118, t117);
            }
        }
        {
            void * t119 = gm_l0;
            void * t120 = gm_l5;
            gm_t2gtbh39ptekd t121;
            memset(&t121, 0, sizeof t121);
            GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)0ULL));
            GM_ST(void *, ((uint8_t *)&t121 + 8), t120);
            gm_tl874t2atnlh3 t122 = gm_l1;
            gm_tc3oht4ksth9d t123;
            gm_f_mk_expr_53qj7nd((void *)&t123, t119, &t121, GM_CONV(gm_cseahvg8v7p0vs, t122));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t123);
            goto gm_fdone;
        }
        goto me80;
mn112:;
ma82:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t125 != ((uint8_t)4ULL)) goto mn124;
        void * t126 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        gm_l6 = t126;
        {
            {
                void * t129 = gm_l0;
                gm_tm4pc0emhrv1g t130;
                gm_f_bump_q2i40re((void *)&t130, t129);
            }
        }
        {
            void * t131 = gm_l0;
            void * t132 = gm_l6;
            gm_t2gtbh39ptekd t133;
            memset(&t133, 0, sizeof t133);
            GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t133 + 8), t132);
            gm_tl874t2atnlh3 t134 = gm_l1;
            gm_tc3oht4ksth9d t135;
            gm_f_mk_expr_53qj7nd((void *)&t135, t131, &t133, GM_CONV(gm_cseahvg8v7p0vs, t134));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t135);
            goto gm_fdone;
        }
        goto me80;
mn124:;
ma83:;
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t137 != ((uint8_t)5ULL)) goto mn136;
        gm_tibupt4dmcqai t138 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&gm_l2) + 8));
        gm_l7 = t138;
        {
            {
                void * t141 = gm_l0;
                gm_tm4pc0emhrv1g t142;
                gm_f_bump_q2i40re((void *)&t142, t141);
            }
        }
        {
            void * t143 = gm_l0;
            gm_tibupt4dmcqai t144 = gm_l7;
            gm_t2gtbh39ptekd t145;
            memset(&t145, 0, sizeof t145);
            GM_ST(uint8_t, ((uint8_t *)&t145 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t145 + 8), t144);
            gm_tl874t2atnlh3 t146 = gm_l1;
            gm_tc3oht4ksth9d t147;
            gm_f_mk_expr_53qj7nd((void *)&t147, t143, &t145, GM_CONV(gm_cseahvg8v7p0vs, t146));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t147);
            goto gm_fdone;
        }
        goto me80;
mn136:;
ma84:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t151 != ((uint8_t)2ULL)) goto pa150;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t152 != ((uint8_t)25ULL)) goto pa150;
        goto po149;
pa150:;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t154 != ((uint8_t)2ULL)) goto pa153;
        uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t155 != ((uint8_t)10ULL)) goto pa153;
        goto po149;
pa153:;
        goto mn148;
po149:;
        {
            {
                void * t158 = gm_l0;
                gm_tm4pc0emhrv1g t159;
                gm_f_bump_q2i40re((void *)&t159, t158);
            }
        }
        {
            void * t160 = gm_l0;
            gm_t3fcbuehh3bs2 t161 = gm_l2;
            gm_t9qkh0rsooc8d t162;
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)25ULL));
            uint8_t t163 = (((GM_LD(uint8_t, ((uint8_t *)&t161) + 0) == ((uint8_t)2ULL)) && gm_eq2ceic3f8rp((((uint8_t *)&t161) + 8), ((uint8_t *)&t162))));
            gm_t2gtbh39ptekd t164;
            memset(&t164, 0, sizeof t164);
            GM_ST(uint8_t, ((uint8_t *)&t164 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, ((uint8_t *)&t164 + 8), t163);
            gm_tl874t2atnlh3 t165 = gm_l1;
            gm_tc3oht4ksth9d t166;
            gm_f_mk_expr_53qj7nd((void *)&t166, t160, &t164, GM_CONV(gm_cseahvg8v7p0vs, t165));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t166);
            goto gm_fdone;
        }
        goto me80;
mn148:;
ma85:;
        uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t168 != ((uint8_t)2ULL)) goto mn167;
        uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t169 != ((uint8_t)19ULL)) goto mn167;
        {
            {
                void * t172 = gm_l0;
                gm_tm4pc0emhrv1g t173;
                gm_f_bump_q2i40re((void *)&t173, t172);
            }
        }
        {
            void * t174 = gm_l0;
            gm_t2gtbh39ptekd t175;
            memset(&t175, 0, sizeof t175);
            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)4ULL));
            gm_tl874t2atnlh3 t176 = gm_l1;
            gm_tc3oht4ksth9d t177;
            gm_f_mk_expr_53qj7nd((void *)&t177, t174, &t175, GM_CONV(gm_cseahvg8v7p0vs, t176));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t177);
            goto gm_fdone;
        }
        goto me80;
mn167:;
ma86:;
        uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t179 != ((uint8_t)1ULL)) goto mn178;
        {
            {
                void * t182 = gm_l0;
                gm_tm4pc0emhrv1g t183;
                gm_f_bump_q2i40re((void *)&t183, t182);
            }
        }
        {
            void * t184 = gm_l0;
            gm_t2gtbh39ptekd t185;
            memset(&t185, 0, sizeof t185);
            GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)7ULL));
            gm_tl874t2atnlh3 t186 = gm_l1;
            gm_tc3oht4ksth9d t187;
            gm_f_mk_expr_53qj7nd((void *)&t187, t184, &t185, GM_CONV(gm_cseahvg8v7p0vs, t186));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t187);
            goto gm_fdone;
        }
        goto me80;
mn178:;
ma87:;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t189 != ((uint8_t)8ULL)) goto mn188;
        uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t190 != ((uint8_t)53ULL)) goto mn188;
        {
            {
                void * t193 = gm_l0;
                gm_tm4pc0emhrv1g t194;
                gm_f_bump_q2i40re((void *)&t194, t193);
            }
        }
        {
            void * t195 = gm_l0;
            gm_t2gtbh39ptekd t196;
            memset(&t196, 0, sizeof t196);
            GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)5ULL));
            gm_tl874t2atnlh3 t197 = gm_l1;
            gm_tc3oht4ksth9d t198;
            gm_f_mk_expr_53qj7nd((void *)&t198, t195, &t196, GM_CONV(gm_cseahvg8v7p0vs, t197));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t198);
            goto gm_fdone;
        }
        goto me80;
mn188:;
ma88:;
        uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t200 != ((uint8_t)0ULL)) goto mn199;
        gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_l8 = t201;
        {
            uint8_t t204 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_st2dnokm1h0jd, 4) == 0));
            if (!t204) goto ie203;
            void * t205 = gm_l0;
            gm_t9qkh0rsooc8d t206;
            GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)61ULL));
            uint8_t t207 = gm_f_nth_is_3fgqmcb(t205, ((uint64_t)1ULL), t206);
            if (!t207) goto ie203;
            {
                void * t209 = gm_l0;
                gm_f_parse_cont_type_ljmco4q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix203;
ie203:;
ix203:;
        }
        {
            uint8_t t211 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sic5vsema0r36, 7) == 0));
            if (!t211) goto ie210;
            void * t212 = gm_l0;
            gm_t3fcbuehh3bs2 t213;
            gm_f_nth_c0r2nvs((void *)&t213, t212, ((uint64_t)1ULL));
            uint8_t t214;
            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&t213) + 0));
            if (t217 != ((uint8_t)0ULL)) goto mn216;
            t214 = ((uint8_t)1);
            goto me215;
mn216:;
            t214 = ((uint8_t)0);
            goto me215;
mn218:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 643, 38);
me215:;
            if (!t214) goto ie210;
            {
                void * t220 = gm_l0;
                gm_f_parse_handler_lit_1nkojsu((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix210;
ie210:;
ix210:;
        }
        {
            uint8_t t222 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sv0fbf330lmcd, 7) == 0));
            if (!t222) goto ie221;
            void * t223 = gm_l0;
            gm_t3fcbuehh3bs2 t224;
            gm_f_nth_c0r2nvs((void *)&t224, t223, ((uint64_t)1ULL));
            uint8_t t225;
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&t224) + 0));
            if (t228 != ((uint8_t)5ULL)) goto mn227;
            t225 = ((uint8_t)1);
            goto me226;
mn227:;
            t225 = ((uint8_t)0);
            goto me226;
mn229:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 644, 38);
me226:;
            if (!t225) goto ie221;
            {
                void * t231 = gm_l0;
                gm_f_parse_foreign_l4091na((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t231);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix221;
ie221:;
ix221:;
        }
        {
            {
                void * t233 = gm_l0;
                gm_tm4pc0emhrv1g t234;
                gm_f_bump_q2i40re((void *)&t234, t233);
            }
        }
        {
            void * t235 = gm_l0;
            gm_t08olq97rscn3 t236;
            memset((uint8_t *)&t236 + 28, 0, 4);
            gm_b16_8 t237 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t236 + 0, t237);
            gm_tl874t2atnlh3 t238 = gm_l1;
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t236 + 16, t238);
            gm_t2gtbh39ptekd t239;
            GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t239 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t239 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t239 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)6ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t239 + 8), t236);
            gm_tl874t2atnlh3 t240 = gm_l1;
            gm_tc3oht4ksth9d t241;
            gm_f_mk_expr_53qj7nd((void *)&t241, t235, &t239, GM_CONV(gm_cseahvg8v7p0vs, t240));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t241);
            goto gm_fdone;
        }
        goto me80;
mn199:;
ma89:;
        uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t243 != ((uint8_t)8ULL)) goto mn242;
        uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t244 != ((uint8_t)61ULL)) goto mn242;
        void * t245 = gm_l0;
        gm_t9qkh0rsooc8d t246;
        GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)0ULL));
        gm_f_parse_paren_sq1guoi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t245, t246);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn242:;
ma90:;
        uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t248 != ((uint8_t)8ULL)) goto mn247;
        uint8_t t249 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t249 != ((uint8_t)58ULL)) goto mn247;
        {
            {
                void * t252 = gm_l0;
                gm_tm4pc0emhrv1g t253;
                gm_f_bump_q2i40re((void *)&t253, t252);
            }
        }
        {
            void * t254 = gm_l0;
            gm_b16_8 t255;
            GM_ST(void *, (uint8_t *)&t255, (void *)(gm_s541gi95epov5));
            GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(24));
            gm_t4jn8fl3lmk5k t256;
            gm_f_parse_ident_n2c6b75((void *)&t256, (void *)NULL, cx, NULL, t254, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t256))[0] != 0)) {
                gm_b0_1 t257 = GM_LD(gm_b0_1, (((uint8_t *)&t256) + 8));
                gm_to1cssm8dfiv5 t258;
                memset(&t258, 0, sizeof t258);
                ((uint8_t *)&t258)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t258 + 8), t257);
                F.ret = t258;
                goto gm_fret;
            }
            gm_l9 = (*(gm_t08olq97rscn3 *)(((uint8_t *)&t256) + 8));
        }
        {
            void * t259 = gm_l0;
            gm_t08olq97rscn3 t260 = gm_l9;
            gm_t2gtbh39ptekd t261;
            GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t261 + 8), t260);
            void * t262 = gm_l0;
            uint32_t t263 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_tl874t2atnlh3 t264 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t262, t263));
            gm_tc3oht4ksth9d t265;
            gm_f_mk_expr_53qj7nd((void *)&t265, t259, &t261, GM_CONV(gm_cseahvg8v7p0vs, t264));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t265);
            goto gm_fdone;
        }
        goto me80;
mn247:;
ma91:;
        uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t269 != ((uint8_t)8ULL)) goto pa268;
        uint8_t t270 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t270 != ((uint8_t)45ULL)) goto pa268;
        goto po267;
pa268:;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t272 != ((uint8_t)8ULL)) goto pa271;
        uint8_t t273 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t273 != ((uint8_t)47ULL)) goto pa271;
        goto po267;
pa271:;
        uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t275 != ((uint8_t)8ULL)) goto pa274;
        uint8_t t276 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t276 != ((uint8_t)63ULL)) goto pa274;
        goto po267;
pa274:;
        goto mn266;
po267:;
        void * t277 = gm_l0;
        gm_t9qkh0rsooc8d t278;
        GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)0ULL));
        gm_f_parse_type_prefix_kfrfcdd((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t277, t278);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn266:;
ma92:;
        uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t280 != ((uint8_t)2ULL)) goto mn279;
        uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t281 != ((uint8_t)11ULL)) goto mn279;
        void * t282 = gm_l0;
        gm_f_parse_fn_expr_c9357a4((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t282);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn279:;
ma93:;
        uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t284 != ((uint8_t)2ULL)) goto mn283;
        uint8_t t285 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t285 != ((uint8_t)24ULL)) goto mn283;
        void * t286 = gm_l0;
        gm_f_parse_struct_lit_86pp62c((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t286);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn283:;
ma94:;
        uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t288 != ((uint8_t)2ULL)) goto mn287;
        uint8_t t289 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t289 != ((uint8_t)27ULL)) goto mn287;
        void * t290 = gm_l0;
        gm_f_parse_union_lit_39ap2o5((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t290);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn287:;
ma95:;
        uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t292 != ((uint8_t)2ULL)) goto mn291;
        uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t293 != ((uint8_t)6ULL)) goto mn291;
        void * t294 = gm_l0;
        gm_f_parse_effect_lit_cqei081((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t294);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn291:;
ma96:;
        uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t296 != ((uint8_t)2ULL)) goto mn295;
        uint8_t t297 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t297 != ((uint8_t)14ULL)) goto mn295;
        void * t298 = gm_l0;
        gm_f_parse_import_s1hv231((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t298);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn295:;
ma97:;
        uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t300 != ((uint8_t)2ULL)) goto mn299;
        uint8_t t301 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t301 != ((uint8_t)13ULL)) goto mn299;
        {
            void * t303 = gm_l0;
            gm_b16_8 t304;
            GM_ST(void *, (uint8_t *)&t304, (void *)(gm_sujvknhnh5am8));
            GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t305 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t303, t304);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t305))[0] != 0)) {
                gm_b0_1 t306 = GM_LD(gm_b0_1, (((uint8_t *)&t305) + 1));
                gm_to1cssm8dfiv5 t307;
                memset(&t307, 0, sizeof t307);
                ((uint8_t *)&t307)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t307 + 8), t306);
                F.ret = t307;
                goto gm_fret;
            }
            gm_b0_1 t308 = GM_LD(gm_b0_1, (((uint8_t *)&t305) + 1));
        }
        {
            void * t309 = gm_l0;
            gm_f_parse_if_shktfou((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me80;
mn299:;
ma98:;
        uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t311 != ((uint8_t)2ULL)) goto mn310;
        uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t312 != ((uint8_t)17ULL)) goto mn310;
        {
            void * t314 = gm_l0;
            gm_b16_8 t315;
            GM_ST(void *, (uint8_t *)&t315, (void *)(gm_sl7a8f8pe8le9));
            GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(20));
            gm_t9qkh0rsooc8d t316 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t314, t315);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t316))[0] != 0)) {
                gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                gm_to1cssm8dfiv5 t318;
                memset(&t318, 0, sizeof t318);
                ((uint8_t *)&t318)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t318 + 8), t317);
                F.ret = t318;
                goto gm_fret;
            }
            gm_b0_1 t319 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
        }
        {
            void * t320 = gm_l0;
            gm_f_parse_match_1lcgsrh((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me80;
mn310:;
ma99:;
        uint8_t t322 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t322 != ((uint8_t)2ULL)) goto mn321;
        uint8_t t323 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t323 != ((uint8_t)31ULL)) goto mn321;
        {
            void * t325 = gm_l0;
            gm_b16_8 t326;
            GM_ST(void *, (uint8_t *)&t326, (void *)(gm_s1llcn0p4qesk));
            GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(19));
            gm_t9qkh0rsooc8d t327 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t327))[0] != 0)) {
                gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
                gm_to1cssm8dfiv5 t329;
                memset(&t329, 0, sizeof t329);
                ((uint8_t *)&t329)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t329 + 8), t328);
                F.ret = t329;
                goto gm_fret;
            }
            gm_b0_1 t330 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
        }
        {
            void * t331 = gm_l0;
            gm_t1s75i08qtptd t332;
            memset(&t332, 0, sizeof t332);
            gm_f_parse_with_hlmj8pt((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t331, &t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me80;
mn321:;
ma100:;
        uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t334 != ((uint8_t)2ULL)) goto mn333;
        uint8_t t335 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t335 != ((uint8_t)12ULL)) goto mn333;
        {
            void * t337 = gm_l0;
            gm_b16_8 t338;
            GM_ST(void *, (uint8_t *)&t338, (void *)(gm_sqm6u7rqmi1il));
            GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(18));
            gm_t9qkh0rsooc8d t339 = gm_f_deny_in_head_06d9o22((void *)NULL, cx, NULL, t337, t338);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t339))[0] != 0)) {
                gm_b0_1 t340 = GM_LD(gm_b0_1, (((uint8_t *)&t339) + 1));
                gm_to1cssm8dfiv5 t341;
                memset(&t341, 0, sizeof t341);
                ((uint8_t *)&t341)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t341 + 8), t340);
                F.ret = t341;
                goto gm_fret;
            }
            gm_b0_1 t342 = GM_LD(gm_b0_1, (((uint8_t *)&t339) + 1));
        }
        {
            void * t343 = gm_l0;
            gm_t1s75i08qtptd t344;
            memset(&t344, 0, sizeof t344);
            gm_f_parse_for_q8vk3da((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t343, &t344);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me80;
mn333:;
ma101:;
        uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t346 != ((uint8_t)2ULL)) goto mn345;
        uint8_t t347 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t347 != ((uint8_t)23ULL)) goto mn345;
        void * t348 = gm_l0;
        gm_f_parse_return_fjkmc1q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t348);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn345:;
ma102:;
        uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t350 != ((uint8_t)2ULL)) goto mn349;
        uint8_t t351 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t351 != ((uint8_t)1ULL)) goto mn349;
        void * t352 = gm_l0;
        gm_f_parse_break_hehputf((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t352);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn349:;
ma103:;
        uint8_t t354 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t354 != ((uint8_t)2ULL)) goto mn353;
        uint8_t t355 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t355 != ((uint8_t)3ULL)) goto mn353;
        void * t356 = gm_l0;
        gm_f_parse_continue_ertnf67((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t356);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn353:;
ma104:;
        uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t358 != ((uint8_t)2ULL)) goto mn357;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t359 != ((uint8_t)22ULL)) goto mn357;
        void * t360 = gm_l0;
        gm_f_parse_resume_u919e4d((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t360);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn357:;
ma105:;
        uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t362 != ((uint8_t)6ULL)) goto mn361;
        void * t363 = gm_l0;
        gm_f_parse_intrinsic_4ou54h3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t363);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn361:;
ma106:;
        uint8_t t365 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t365 != ((uint8_t)7ULL)) goto mn364;
        void * t366 = gm_l0;
        gm_t9qkh0rsooc8d t367;
        GM_ST(uint8_t, ((uint8_t *)&t367 + 0), ((uint8_t)0ULL));
        gm_f_parse_attributed_00u9rjp((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t366, t367);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me80;
mn364:;
ma107:;
        uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t369 != ((uint8_t)2ULL)) goto mn368;
        uint8_t t370 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t370 != ((uint8_t)18ULL)) goto mn368;
        void * t371 = gm_l0;
        gm_tl874t2atnlh3 t372 = gm_l1;
        gm_b16_8 t373;
        GM_ST(void *, (uint8_t *)&t373, (void *)(gm_sb455a3lakuom));
        GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(59));
        gm_b0_1 t374 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t371, t372, t373);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t374);
        goto gm_fdone;
        goto me80;
mn368:;
ma108:;
        uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t376 != ((uint8_t)2ULL)) goto mn375;
        uint8_t t377 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
        if (t377 != ((uint8_t)26ULL)) goto mn375;
        void * t378 = gm_l0;
        gm_tl874t2atnlh3 t379 = gm_l1;
        gm_b16_8 t380;
        GM_ST(void *, (uint8_t *)&t380, (void *)(gm_slp04vsc9cddi));
        GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)(48));
        gm_b0_1 t381 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t378, t379, t380);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t381);
        goto gm_fdone;
        goto me80;
mn375:;
ma109:;
        void * t383 = gm_l0;
        gm_b16_8 t384;
        GM_ST(void *, (uint8_t *)&t384, (void *)(gm_s42atdm90l4do));
        GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(13));
        gm_b0_1 t385 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t383, t384);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t385);
        goto gm_fdone;
        goto me80;
mn382:;
ma110:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 611, 5);
me80:;
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
inline __attribute__((aligned(4))) void gm_f_parse_range_o9o2al7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_range_o9o2al7 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_t3fcbuehh3bs2 t8;
        gm_f_kind_k239kpg((void *)&t8, t7);
        uint8_t t9 = gm_f_binary_follows_jeb42bi(&t8);
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie6;
        {
            const uint8_t *t12 = ((uint8_t *)&F.l1);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t12, 88);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t13 = gm_l0;
        gm_f_range_from_5v6ko9m((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t13, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_resume_u919e4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_resume_u919e4d F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_tl874t2atnlh3 gm_l3; /* whole */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    void * gm_l6; /* value */
    GM_STACK_CHECK(&F, 772u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
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
        gm_to1cssm8dfiv5 t8;
        gm_f_parse_postfix_ao4kstr((void *)&t8, (void *)NULL, cx, NULL, t7, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 8));
            gm_to1cssm8dfiv5 t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), t9);
            F.ret = t10;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t8) + 8));
    }
    {
        void * t11 = gm_l0;
        uint32_t t12 = gm_l1;
        gm_tl874t2atnlh3 t13 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t11, t12));
        gm_l3 = t13;
    }
    {
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 8) + 0));
        if (t15 != ((uint8_t)19ULL)) goto ie14;
        void * t16 = GM_LD(void *, (((((uint8_t *)&F.l2) + 8) + 8) + 0));
        gm_l4 = t16;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l2) + 8) + 8) + 8));
        gm_l5 = t17;
        {
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t21 = (t20 <= ((uint64_t)1ULL));
            if (!t21) goto ie19;
            uint8_t t22 = 0;
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
            if (!t24) {
                uint64_t t25 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src0dtioet3p4f6, 811, 48);
                uint8_t t27;
                uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t25 * 144) + 0) + 0));
                if (t30 != ((uint8_t)0ULL)) goto mn29;
                t27 = ((uint8_t)1);
                goto me28;
mn29:;
                t27 = ((uint8_t)0);
                goto me28;
mn31:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 811, 48);
me28:;
                t22 = t27;
            } else t22 = 1;
            if (!t22) goto ie19;
            {
                gm_l6 = ((void *)0);
            }
            {
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t35 = ((t34 == ((uint64_t)1ULL)));
                if (!t35) goto ie33;
                {
                    uint64_t t38 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_src0dtioet3p4f6, 814, 38);
                    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t38 * 144) + 0) + 0));
                    if (t40 != ((uint8_t)0ULL)) goto ie37;
                    gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t38 * 144) + 0) + 8));
                    F.l7 = t41;
                    {
                        void * t43 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l6 = t43;
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                void * t44 = gm_l0;
                void * t45 = gm_l4;
                void * t46 = gm_l6;
                gm_t3mf58gv9kf3n t47;
                GM_ST(void *, ((uint8_t *)&t47 + 0), t45);
                GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
                gm_t2gtbh39ptekd t48;
                memset(&t48, 0, sizeof t48);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)48ULL));
                GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t48 + 8), t47);
                gm_tl874t2atnlh3 t49 = gm_l3;
                gm_tc3oht4ksth9d t50;
                gm_f_mk_expr_53qj7nd((void *)&t50, t44, &t48, GM_CONV(gm_cseahvg8v7p0vs, t49));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
                GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t50);
                goto gm_fdone;
            }
            goto ix19;
ie19:;
ix19:;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t51 = gm_l0;
        gm_tl874t2atnlh3 t52 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_skh04d6p3n402));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(78));
        gm_b0_1 t54 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t51, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&(*(gm_to1cssm8dfiv5 *)gm_out), 0, sizeof (*(gm_to1cssm8dfiv5 *)gm_out));
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 1;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t54);
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
inline __attribute__((aligned(4))) void gm_f_parse_return_fjkmc1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_return_fjkmc1q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    void * gm_l2; /* value */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        {
            void * t5 = gm_l0;
            gm_tm4pc0emhrv1g t6;
            gm_f_bump_q2i40re((void *)&t6, t5);
        }
    }
    {
        gm_l2 = ((void *)0);
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        uint8_t t10 = gm_f_can_begin_expr_rmia9bs(&t9);
        if (!t10) goto ie7;
        {
            void * t12 = gm_l0;
            gm_to1cssm8dfiv5 t13;
            gm_f_parse_expr_bkb2n6e((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
                gm_to1cssm8dfiv5 t15;
                memset(&t15, 0, sizeof t15);
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t15 + 8), t14);
                F.ret = t15;
                goto gm_fret;
            }
            void * t16 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&t13) + 8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l2 = t16;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t17 = gm_l0;
        void * t18 = gm_l2;
        gm_t2gtbh39ptekd t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)45ULL));
        GM_ST(void *, ((uint8_t *)&t19 + 8), t18);
        void * t20 = gm_l0;
        uint32_t t21 = gm_l1;
        gm_tl874t2atnlh3 t22 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t20, t21));
        gm_tc3oht4ksth9d t23;
        gm_f_mk_expr_53qj7nd((void *)&t23, t17, &t19, GM_CONV(gm_cseahvg8v7p0vs, t22));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t23);
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
inline __attribute__((aligned(4))) void gm_f_parse_shift_jium2c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_shift_jium2c6 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_parse_prefix_671fi6d((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l1 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t7 = gm_l0;
        gm_t3fcbuehh3bs2 t8;
        gm_f_kind_k239kpg((void *)&t8, t7);
        uint8_t t9 = gm_f_binary_follows_jeb42bi(&t8);
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie6;
        {
            const uint8_t *t12 = ((uint8_t *)&F.l1);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t12, 88);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t13 = gm_l0;
        gm_f_shift_from_3cnaq25((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t13, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_parse_subscript_1hujuhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_parse_subscript_1hujuhl F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    gm_tao63pk3r18v5 gm_l6; /* x */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    GM_STACK_CHECK(&F, 1099u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 72) + 4));
        gm_l2 = t2;
    }
    {
        {
            void * t4 = gm_l0;
            gm_tm4pc0emhrv1g t5;
            gm_f_bump_q2i40re((void *)&t5, t4);
        }
    }
    {
        void * t6 = gm_l0;
        gm_tb1kjfin6jnc2 t7;
        gm_f_parse_subscript_inner_dhgpovb((void *)&t7, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 8));
            gm_to1cssm8dfiv5 t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        F.l3 = (*(gm_t1vei8bd9e9m7 *)(((uint8_t *)&t7) + 8));
    }
    {
        {
            void * t11 = gm_l0;
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)64ULL));
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sh0is7j09prt2));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(22));
            gm_t0u0j6voh3loe t14 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t14))[0] != 0)) {
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 4));
                gm_to1cssm8dfiv5 t16;
                memset(&t16, 0, sizeof t16);
                ((uint8_t *)&t16)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), t15);
                F.ret = t16;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t14) + 4));
        }
    }
    {
        void * t18 = gm_l0;
        uint32_t t19 = gm_l2;
        gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t18, t19));
        gm_l4 = t20;
    }
    {
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t23 != ((uint8_t)0ULL)) goto mn22;
        gm_tc3oht4ksth9d t24 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&F.l3) + 8));
        F.l5 = t24;
        void * t25 = gm_l0;
        void * t26 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t27 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t28;
        GM_ST(void *, ((uint8_t *)&t28 + 0), t26);
        GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
        gm_t2gtbh39ptekd t29;
        memset(&t29, 0, sizeof t29);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)20ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t29 + 8), t28);
        gm_tl874t2atnlh3 t30 = gm_l4;
        gm_tc3oht4ksth9d t31;
        gm_f_mk_expr_53qj7nd((void *)&t31, t25, &t29, GM_CONV(gm_cseahvg8v7p0vs, t30));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t31);
        goto gm_fdone;
        goto me21;
mn22:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t33 != ((uint8_t)1ULL)) goto mn32;
        gm_tao63pk3r18v5 t34 = GM_LD(gm_tao63pk3r18v5, (((uint8_t *)&F.l3) + 8));
        gm_l6 = t34;
        {
            gm_tao63pk3r18v5 t36 = gm_l6;
            gm_l7 = t36;
        }
        {
            void * t37 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (((uint8_t *)&gm_l7) + 0), t37);
        }
        {
            void * t38 = gm_l0;
            gm_tao63pk3r18v5 t39 = gm_l7;
            gm_t2gtbh39ptekd t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t40 + 8), t39);
            gm_tl874t2atnlh3 t41 = gm_l4;
            gm_tc3oht4ksth9d t42;
            gm_f_mk_expr_53qj7nd((void *)&t42, t38, &t40, GM_CONV(gm_cseahvg8v7p0vs, t41));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t42);
            goto gm_fdone;
        }
        goto me21;
mn32:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 502, 5);
me21:;
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
__attribute__((aligned(4))) void gm_f_parse_subscript_inner_dhgpovb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_subscript_inner_dhgpovb F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    void * gm_l2; /* placeholder */
    uint8_t gm_l3; /* inclusive */
    gm_tl874t2atnlh3 gm_l4; /* op_span */
    void * gm_l5; /* hi */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    uint8_t gm_l10; /* inclusive */
    uint8_t gm_l11; /* open */
    void * gm_l12; /* hi */
    GM_STACK_CHECK(&F, 1438u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)1));
        gm_toh0ootodhjue t4 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t2, GM_CONV(gm_cs3cuou245tr5f, t3)));
        gm_l1 = t4;
    }
    {
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t9));
        gm_tc3oht4ksth9d t11;
        gm_f_error_expr_dhdi86k((void *)&t11, t8, GM_CONV(gm_cseahvg8v7p0vs, t10));
        void * t12 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_l2 = t12;
    }
    {
        uint8_t t14 = 0;
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)50ULL));
        uint8_t t17 = gm_f_at_54906rm(t15, t16);
        if (!t17) {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)51ULL));
            uint8_t t20 = gm_f_at_54906rm(t18, t19);
            t14 = t20;
        } else t14 = 1;
        if (!t14) goto ie13;
        {
            void * t22 = gm_l0;
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)51ULL));
            uint8_t t24 = gm_f_at_54906rm(t22, t23);
            gm_l3 = t24;
        }
        {
            void * t25 = gm_l0;
            gm_tm4pc0emhrv1g t26;
            gm_f_bump_q2i40re((void *)&t26, t25);
            gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t26) + 32));
            gm_l4 = t27;
        }
        {
            gm_l5 = ((void *)0);
        }
        {
            void * t29 = gm_l0;
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)64ULL));
            uint8_t t31 = gm_f_at_54906rm(t29, t30);
            uint8_t t32 = (uint8_t)!t31;
            if (!t32) goto ie28;
            {
                void * t34 = gm_l0;
                gm_to1cssm8dfiv5 t35;
                gm_f_parse_expr_bkb2n6e((void *)&t35, (void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                if (((((uint8_t *)&t35))[0] != 0)) {
                    gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 8));
                    gm_tb1kjfin6jnc2 t37;
                    memset(&t37, 0, sizeof t37);
                    ((uint8_t *)&t37)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), t36);
                    F.ret = t37;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld1_1;
                }
                void * t38 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&t35) + 8)));
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_l5 = t38;
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            uint8_t t40 = gm_l3;
            if (!t40) goto ie39;
            void * t41 = gm_l5;
            uint8_t t42;
            if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) goto mn44;
            t42 = ((uint8_t)1);
            goto me43;
mn44:;
            t42 = ((uint8_t)0);
            goto me43;
mn45:;
            gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 527, 26);
me43:;
            if (!t42) goto ie39;
            {
                void * t47 = gm_l0;
                gm_tl874t2atnlh3 t48 = gm_l4;
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_skefd3dps40es));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(39));
                gm_b0_1 t50 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t47, t48, t49);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            gm_tao63pk3r18v5 t51;
            memset((uint8_t *)&t51 + 25, 0, 7);
            void * t52 = gm_l2;
            GM_ST(void *, (uint8_t *)&t51 + 0, t52);
            GM_ST(void *, (uint8_t *)&t51 + 8, ((void *)0));
            void * t53 = gm_l5;
            GM_ST(void *, (uint8_t *)&t51 + 16, t53);
            uint8_t t54 = gm_l3;
            GM_ST(uint8_t, (uint8_t *)&t51 + 24, t54);
            gm_t1vei8bd9e9m7 t55;
            memset(&t55, 0, sizeof t55);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t55 + 8), t51);
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(gm_t1vei8bd9e9m7, (uint8_t *)&F.ret + 8, t55);
            uint8_t t56 = ((((uint8_t *)&F.ret))[0] != 0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t56;
            goto gm_ld1_1;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t57 = gm_l0;
        gm_to1cssm8dfiv5 t58;
        gm_f_parse_expr_bkb2n6e((void *)&t58, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        if (((((uint8_t *)&t58))[0] != 0)) {
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 8));
            gm_tb1kjfin6jnc2 t60;
            memset(&t60, 0, sizeof t60);
            ((uint8_t *)&t60)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t60 + 8), t59);
            F.ret = t60;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
            goto gm_ld1_1;
        }
        F.l6 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t58) + 8));
    }
    {
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 8) + 0));
        if (t62 != ((uint8_t)13ULL)) goto ie61;
        gm_ts6oqppjh0ba3 t63 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)&F.l6) + 8) + 8) + 0));
        gm_l7 = t63;
        void * t64 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 8));
        gm_l8 = t64;
        void * t65 = GM_LD(void *, (((((uint8_t *)&F.l6) + 8) + 8) + 16));
        gm_l9 = t65;
        {
            gm_ts6oqppjh0ba3 t68 = gm_l7;
            gm_ts6oqppjh0ba3 t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t70;
            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)11ULL));
            gm_t9ldr1q5hn9fq t71;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t71 + 0), t69);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t71 + 2), t70);
            uint8_t t72 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t71) + 0, ((uint8_t *)&t68))) t72 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t71) + 2, ((uint8_t *)&t68))) t72 = 1;
            uint8_t t73 = (uint8_t)(t72);
            if (!t73) goto ie67;
            {
                uint8_t t75 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l7) + 0) == ((uint8_t)11ULL)));
                gm_l10 = t75;
            }
            {
                uint8_t t76 = 0;
                void * t77 = gm_l9;
                uint8_t t78 = ((GM_LD(uint8_t, (((uint8_t *)t77) + 8) + 0) == ((uint8_t)50ULL)));
                if (t78) {
                    void * t79 = gm_l9;
                    gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t79) + 72));
                    uint8_t t81 = gm_f_is_empty_q2f0b93(GM_CONV(gm_cseahvg8v7p0vs, t80));
                    t76 = t81;
                } else t76 = 0;
                gm_l11 = t76;
            }
            {
                uint8_t t83 = gm_l11;
                if (!t83) goto ie82;
                uint8_t t84 = gm_l10;
                if (!t84) goto ie82;
                {
                    void * t86 = gm_l0;
                    gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_skefd3dps40es));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(39));
                    gm_b0_1 t89 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t86, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_ld1_1;
                    }
                }
                goto ix82;
ie82:;
ix82:;
            }
            {
                void * t90;
                uint8_t t92 = gm_l11;
                if (!t92) goto ie91;
                void * t94 = ((void *)0);
                t90 = t94;
                goto ix91;
ie91:;
                void * t96 = gm_l9;
                void * t97 = t96;
                t90 = t97;
ix91:;
                gm_l12 = t90;
            }
            {
                gm_tao63pk3r18v5 t98;
                memset((uint8_t *)&t98 + 25, 0, 7);
                void * t99 = gm_l2;
                GM_ST(void *, (uint8_t *)&t98 + 0, t99);
                void * t100 = gm_l8;
                GM_ST(void *, (uint8_t *)&t98 + 8, t100);
                void * t101 = gm_l12;
                GM_ST(void *, (uint8_t *)&t98 + 16, t101);
                uint8_t t102 = gm_l10;
                GM_ST(uint8_t, (uint8_t *)&t98 + 24, t102);
                gm_t1vei8bd9e9m7 t103;
                memset(&t103, 0, sizeof t103);
                GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t103 + 8), t98);
                GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                ((uint8_t *)&F.ret)[0] = 0;
                GM_ST(gm_t1vei8bd9e9m7, (uint8_t *)&F.ret + 8, t103);
                uint8_t t104 = ((((uint8_t *)&F.ret))[0] != 0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t104;
                goto gm_ld1_1;
            }
            goto ix67;
ie67:;
ix67:;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_tc3oht4ksth9d t105 = F.l6;
        gm_t1vei8bd9e9m7 t106;
        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t106 + 8), t105);
        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
        ((uint8_t *)&F.ret)[0] = 0;
        GM_ST(gm_t1vei8bd9e9m7, (uint8_t *)&F.ret + 8, t106);
        uint8_t t107 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t107;
        goto gm_ld1_1;
    }
    {
        {
            void * t109 = gm_l0;
            gm_toh0ootodhjue t110 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t109) + 128), t110);
        }
    }
    goto gm_ls111;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t113 = gm_l0;
            gm_toh0ootodhjue t114 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t113) + 128), t114);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls111:;
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_prefix_op_japctlh F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_taari1s0m51a7 t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; default: goto ma8; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)8ULL)) goto mn11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t13 != ((uint8_t)1ULL)) goto mn11;
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t15;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
    t1 = t15;
    goto me2;
mn11:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)8ULL)) goto mn16;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t18 != ((uint8_t)9ULL)) goto mn16;
    gm_t9qkh0rsooc8d t19;
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t20;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
    ((uint8_t *)&t20)[1] = 1;
    t1 = t20;
    goto me2;
mn16:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t22 != ((uint8_t)8ULL)) goto mn21;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t23 != ((uint8_t)7ULL)) goto mn21;
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t25;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[1] = 1;
    t1 = t25;
    goto me2;
mn21:;
ma6:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)8ULL)) goto mn26;
    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t28 != ((uint8_t)46ULL)) goto mn26;
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t30;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t30), t29);
    ((uint8_t *)&t30)[1] = 1;
    t1 = t30;
    goto me2;
mn26:;
ma7:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t33 != ((uint8_t)26ULL)) goto mn31;
    gm_t9qkh0rsooc8d t34;
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)4ULL));
    gm_taari1s0m51a7 t35;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35), t34);
    ((uint8_t *)&t35)[1] = 1;
    t1 = t35;
    goto me2;
mn31:;
ma8:;
    gm_taari1s0m51a7 t37;
    memset(&t37, 0, sizeof t37);
    t1 = t37;
    goto me2;
mn36:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 391, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_prefix_op_japctlh_c(gm_t3fcbuehh3bs2 a0) { return gm_f_prefix_op_japctlh(&a0); }
__attribute__((aligned(4))) void gm_f_range_from_5v6ko9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_range_from_5v6ko9m F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    uint8_t gm_l4; /* open */
    GM_STACK_CHECK(&F, 1273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_bit_from_pu2bq19((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        gm_ts6oqppjh0ba3 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)10ULL));
        gm_l3 = t6;
    }
    {
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)51ULL));
        uint8_t t10 = gm_f_at_54906rm(t8, t9);
        if (!t10) goto ie7;
        {
            gm_ts6oqppjh0ba3 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)11ULL));
            gm_l3 = t12;
        }
        goto ix7;
ie7:;
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)50ULL));
        uint8_t t16 = gm_f_at_54906rm(t14, t15);
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie13;
        {
            const uint8_t *t19 = ((uint8_t *)&F.l2);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t19, 88);
            goto gm_fdone;
        }
        goto ix13;
ie13:;
ix13:;
ix7:;
    }
    {
        {
            void * t21 = gm_l0;
            gm_tm4pc0emhrv1g t22;
            gm_f_bump_q2i40re((void *)&t22, t21);
        }
    }
    {
        uint8_t t23 = 0;
        void * t24 = gm_l0;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 128) + 2));
        if (t25) {
            void * t26 = gm_l0;
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)64ULL));
            uint8_t t28 = gm_f_at_54906rm(t26, t27);
            t23 = t28;
        } else t23 = 0;
        gm_l4 = t23;
    }
    {
        gm_tc3oht4ksth9d t29;
        uint8_t t31 = gm_l4;
        if (!t31) goto ie30;
        void * t33 = gm_l0;
        void * t34 = gm_l0;
        gm_tl874t2atnlh3 t35 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t34));
        gm_tl874t2atnlh3 t36 = GM_CONV(gm_tl874t2atnlh3, gm_f_shrink_to_lo_iok07dh(GM_CONV(gm_cseahvg8v7p0vs, t35)));
        gm_tc3oht4ksth9d t37;
        gm_f_error_expr_dhdi86k((void *)&t37, t33, GM_CONV(gm_cseahvg8v7p0vs, t36));
        gm_tc3oht4ksth9d t38 = t37;
        t29 = t38;
        goto ix30;
ie30:;
        void * t40 = gm_l0;
        gm_to1cssm8dfiv5 t41;
        gm_f_parse_bit_h83vs72((void *)&t41, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t41))[0] != 0)) {
            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 8));
            gm_to1cssm8dfiv5 t43;
            memset(&t43, 0, sizeof t43);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t43 + 8), t42);
            F.ret = t43;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t44 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t41) + 8));
        t29 = t44;
ix30:;
        F.l5 = t29;
    }
    {
        void * t45 = gm_l0;
        gm_ts6oqppjh0ba3 t46 = gm_l3;
        gm_f_binary_41fr5r5((void *)&F.l6, (void *)NULL, cx, NULL, t45, t46, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t48 = 0;
        void * t49 = gm_l0;
        gm_t9qkh0rsooc8d t50;
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)50ULL));
        uint8_t t51 = gm_f_at_54906rm(t49, t50);
        if (!t51) {
            void * t52 = gm_l0;
            gm_t9qkh0rsooc8d t53;
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)51ULL));
            uint8_t t54 = gm_f_at_54906rm(t52, t53);
            t48 = t54;
        } else t48 = 1;
        if (!t48) goto ie47;
        {
            void * t56 = gm_l0;
            void * t57 = gm_l0;
            gm_tl874t2atnlh3 t58 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t57));
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s1q0n6t6d7g6n));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(33));
            gm_b0_1 t60 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t56, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            for (;;) {
                uint8_t t62 = 0;
                void * t63 = gm_l0;
                gm_t9qkh0rsooc8d t64;
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)50ULL));
                uint8_t t65 = gm_f_at_54906rm(t63, t64);
                if (!t65) {
                    void * t66 = gm_l0;
                    gm_t9qkh0rsooc8d t67;
                    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)51ULL));
                    uint8_t t68 = gm_f_at_54906rm(t66, t67);
                    t62 = t68;
                } else t62 = 1;
                if (!t62) goto lb61;
                {
                    {
                        void * t72 = gm_l0;
                        gm_tm4pc0emhrv1g t73;
                        gm_f_bump_q2i40re((void *)&t73, t72);
                    }
                }
                {
                    {
                        void * t75 = gm_l0;
                        gm_to1cssm8dfiv5 t76;
                        gm_f_parse_bit_h83vs72((void *)&t76, (void *)NULL, cx, NULL, t75);
                        if (gm_unw.kind) {
                            goto gm_ll69;
                        }
                        if (((((uint8_t *)&t76))[0] != 0)) {
                            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 8));
                            gm_to1cssm8dfiv5 t78;
                            memset(&t78, 0, sizeof t78);
                            ((uint8_t *)&t78)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t78 + 8), t77);
                            F.ret = t78;
                            goto gm_fret;
                        }
                    }
                }
lc61:;
            }
gm_ll69:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb61; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc61; }
            }
            goto gm_lad;
lb61:;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        const uint8_t *t79 = ((uint8_t *)&F.l6);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t79, 88);
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
__attribute__((aligned(4))) void gm_f_shift_from_3cnaq25(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_shift_from_3cnaq25 F;
    void * gm_l0; /* p */
    gm_ts6oqppjh0ba3 gm_l3; /* op */
    GM_STACK_CHECK(&F, 1017u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_to1cssm8dfiv5 t3;
        gm_f_add_from_7os0rvi((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tc3oht4ksth9d *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) {
            gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 8));
            gm_to1cssm8dfiv5 t5;
            memset(&t5, 0, sizeof t5);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), t4);
            F.ret = t5;
            goto gm_fret;
        }
        F.l2 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t3) + 8));
    }
    {
        void * t6 = gm_l0;
        gm_t3fcbuehh3bs2 t7;
        gm_f_kind_k239kpg((void *)&t7, t6);
        gm_tt7vn9ehj0acr t8 = gm_f_shift_op_sro3o9l(&t7);
        gm_ts6oqppjh0ba3 t9;
        if (((((uint8_t *)&t8))[2] != 0)) {
            gm_ts6oqppjh0ba3 t10 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            const uint8_t *t11 = ((uint8_t *)&F.l2);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t11, 88);
            goto gm_fdone;
        }
        gm_l3 = t9;
    }
    {
        {
            void * t13 = gm_l0;
            gm_tm4pc0emhrv1g t14;
            gm_f_bump_q2i40re((void *)&t14, t13);
        }
    }
    {
        void * t15 = gm_l0;
        gm_to1cssm8dfiv5 t16;
        gm_f_parse_add_dh9nvbt((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            gm_to1cssm8dfiv5 t18;
            memset(&t18, 0, sizeof t18);
            ((uint8_t *)&t18)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t18 + 8), t17);
            F.ret = t18;
            goto gm_fret;
        }
        F.l4 = (*(gm_tc3oht4ksth9d *)(((uint8_t *)&t16) + 8));
    }
    {
        void * t20 = (void *)((uint8_t *)&F.l2);
        uint8_t t21 = gm_f_is_arith_l6b4tpf(t20);
        if (!t21) goto ie19;
        {
            void * t23 = gm_l0;
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s4puodol85qqo));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(54));
            gm_b0_1 t26 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix19;
ie19:;
        void * t28 = (void *)((uint8_t *)&F.l4);
        uint8_t t29 = gm_f_is_arith_l6b4tpf(t28);
        if (!t29) goto ie27;
        {
            void * t31 = gm_l0;
            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s4puodol85qqo));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(54));
            gm_b0_1 t34 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix27;
ie27:;
ix27:;
ix19:;
    }
    {
        void * t35 = gm_l0;
        gm_ts6oqppjh0ba3 t36 = gm_l3;
        gm_f_binary_41fr5r5((void *)&F.l5, (void *)NULL, cx, NULL, t35, t36, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t38 = gm_l0;
        gm_t3fcbuehh3bs2 t39;
        gm_f_kind_k239kpg((void *)&t39, t38);
        gm_tt7vn9ehj0acr t40 = gm_f_shift_op_sro3o9l(&t39);
        uint8_t t41;
        if (((((uint8_t *)&t40))[2] != 0)) goto mn43;
        t41 = ((uint8_t)0);
        goto me42;
mn43:;
        t41 = ((uint8_t)1);
        goto me42;
mn44:;
        gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 346, 8);
me42:;
        if (!t41) goto ie37;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l0;
            gm_tl874t2atnlh3 t48 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t47));
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_ssf8mqtfpipac));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(44));
            gm_b0_1 t50 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t46, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            for (;;) {
                void * t52 = gm_l0;
                gm_t3fcbuehh3bs2 t53;
                gm_f_kind_k239kpg((void *)&t53, t52);
                gm_tt7vn9ehj0acr t54 = gm_f_shift_op_sro3o9l(&t53);
                uint8_t t55;
                if (((((uint8_t *)&t54))[2] != 0)) goto mn57;
                t55 = ((uint8_t)0);
                goto me56;
mn57:;
                t55 = ((uint8_t)1);
                goto me56;
mn58:;
                gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 348, 15);
me56:;
                if (!t55) goto lb51;
                {
                    {
                        void * t62 = gm_l0;
                        gm_tm4pc0emhrv1g t63;
                        gm_f_bump_q2i40re((void *)&t63, t62);
                    }
                }
                {
                    {
                        void * t65 = gm_l0;
                        gm_to1cssm8dfiv5 t66;
                        gm_f_parse_add_dh9nvbt((void *)&t66, (void *)NULL, cx, NULL, t65);
                        if (gm_unw.kind) {
                            goto gm_ll59;
                        }
                        if (((((uint8_t *)&t66))[0] != 0)) {
                            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 8));
                            gm_to1cssm8dfiv5 t68;
                            memset(&t68, 0, sizeof t68);
                            ((uint8_t *)&t68)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t68 + 8), t67);
                            F.ret = t68;
                            goto gm_fret;
                        }
                    }
                }
lc51:;
            }
gm_ll59:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb51; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc51; }
            }
            goto gm_lad;
lb51:;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        const uint8_t *t69 = ((uint8_t *)&F.l5);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_to1cssm8dfiv5 *)gm_out) + 8, t69, 88);
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
inline __attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_shift_op_sro3o9l F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tt7vn9ehj0acr t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma3; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; default: goto ma8; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)8ULL)) goto mn11;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t13 != ((uint8_t)5ULL)) goto mn11;
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
    gm_ts6oqppjh0ba3 t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
    gm_tt7vn9ehj0acr t16;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t16), t15);
    ((uint8_t *)&t16)[2] = 1;
    t1 = t16;
    goto me2;
mn11:;
ma4:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t18 != ((uint8_t)8ULL)) goto mn17;
    uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t19 != ((uint8_t)13ULL)) goto mn17;
    gm_t9qkh0rsooc8d t20;
    GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)1ULL));
    gm_ts6oqppjh0ba3 t21;
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21 + 1), t20);
    gm_tt7vn9ehj0acr t22;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t22), t21);
    ((uint8_t *)&t22)[2] = 1;
    t1 = t22;
    goto me2;
mn17:;
ma5:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t24 != ((uint8_t)8ULL)) goto mn23;
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t25 != ((uint8_t)17ULL)) goto mn23;
    gm_t9qkh0rsooc8d t26;
    GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
    gm_ts6oqppjh0ba3 t27;
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t27 + 1), t26);
    gm_tt7vn9ehj0acr t28;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[2] = 1;
    t1 = t28;
    goto me2;
mn23:;
ma6:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t30 != ((uint8_t)8ULL)) goto mn29;
    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t31 != ((uint8_t)21ULL)) goto mn29;
    gm_t9qkh0rsooc8d t32;
    GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)3ULL));
    gm_ts6oqppjh0ba3 t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)3ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 1), t32);
    gm_tt7vn9ehj0acr t34;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t34), t33);
    ((uint8_t *)&t34)[2] = 1;
    t1 = t34;
    goto me2;
mn29:;
ma7:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t36 != ((uint8_t)8ULL)) goto mn35;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)a0) + 8) + 0));
    if (t37 != ((uint8_t)6ULL)) goto mn35;
    gm_ts6oqppjh0ba3 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)6ULL));
    gm_tt7vn9ehj0acr t39;
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t39), t38);
    ((uint8_t *)&t39)[2] = 1;
    t1 = t39;
    goto me2;
mn35:;
ma8:;
    gm_tt7vn9ehj0acr t41;
    memset(&t41, 0, sizeof t41);
    t1 = t41;
    goto me2;
mn40:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src0dtioet3p4f6, 52, 54);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tt7vn9ehj0acr gm_f_shift_op_sro3o9l_c(gm_t3fcbuehh3bs2 a0) { return gm_f_shift_op_sro3o9l(&a0); }
