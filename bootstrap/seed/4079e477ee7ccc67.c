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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint32_t f2; } gm_tba3spc7ecv13;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
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
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tba3spc7ecv13) == 12 && _Alignof(gm_tba3spc7ecv13) == 4 && offsetof(gm_tba3spc7ecv13, f0) == 0 && offsetof(gm_tba3spc7ecv13, f1) == 4 && offsetof(gm_tba3spc7ecv13, f2) == 8, "layout of `Slot(u32, u32)`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_tba3spc7ecv13 f0; uint8_t f1; uint8_t pe[3]; } gm_tf38rotgtqq79;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
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
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf38rotgtqq79) == 16 && _Alignof(gm_tf38rotgtqq79) == 4 && offsetof(gm_tf38rotgtqq79, f0) == 0 && offsetof(gm_tf38rotgtqq79, f1) == 12, "layout of `?(signed: bool, min: u32, max: u32)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { gm_tbci5ospk5nnn a[2]; } gm_thu7n15ilds8r;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { gm_tngtuiuskn938 a[2]; } gm_tdskijji6mt1d;
_Static_assert(sizeof(gm_thu7n15ilds8r) == 96 && _Alignof(gm_thu7n15ilds8r) == 8 && offsetof(gm_thu7n15ilds8r, a) == 0, "layout of `[2]Int`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tdskijji6mt1d) == 192 && _Alignof(gm_tdskijji6mt1d) == 8 && offsetof(gm_tdskijji6mt1d, a) == 0, "layout of `[2]Rat`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_obligation_ss66lhk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* o */
    void * l2; /* tys */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_t9bmh80edou9c l14; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l15; /* <temporary array> */
    gm_trqfvmjtt4sij l19; /* parts */
    gm_t91vm1g0k2rpm l26; /* n */
    gm_b0_1 ret;
};
struct gm_fr_clear_a02qt6l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_p214f4b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_conversion_ok_qftumna {
    gm_frame h;
    gm_tckdobunaulee l8; /* v */
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
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
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_eq_ceufbo4 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fin_cond_32pmhkp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fin_expr_rk98p2r {
    gm_frame h;
    void * l0; /* cx */
    gm_t710lsrq1m8gk l8; /* x */
    gm_tbci5ospk5nnn l11; /* v */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_trqfvmjtt4sij l19; /* fields */
    gm_tdskijji6mt1d l20; /* <tmp> */
    gm_tngtuiuskn938 l23; /* r */
    gm_b0_1 ret;
};
struct gm_fr_fin_float_9k2be5v {
    gm_frame h;
    gm_tngtuiuskn938 l2; /* r */
    gm_b0_1 ret;
};
struct gm_fr_fin_pat_92m9fnd {
    gm_frame h;
    void * l0; /* cx */
    gm_tbci5ospk5nnn l2; /* lo */
    gm_tbci5ospk5nnn l3; /* hi */
    gm_thu7n15ilds8r l4; /* <tmp> */
    gm_tbci5ospk5nnn l7; /* v */
    gm_b0_1 ret;
};
struct gm_fr_finalize_k1g9q48 {
    gm_frame h;
    gm_tpm220j5qhjke l14; /* o */
    gm_b0_1 ret;
};
struct gm_fr_fn_2v5b782 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_513k31a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ud6f4hb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_int_range_jdeefnh {
    gm_frame h;
    gm_tf38rotgtqq79 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
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
struct gm_fr_literal_core_ii46ie6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_lossless_a54o69s {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_numeric_uv79l0u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_settle_h9ikvs5 {
    gm_frame h;
    gm_t88rcucg1b7uk l6; /* src */
    uint8_t l7; /* mentioned */
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_show_rat_1s624db {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_untyped_float_m6lbr0g {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_teti411eujvf4 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_fallbacks_gcbql0t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_obligation_ss66lhk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_clear_a02qt6l(void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_p214f4b(void * a0);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_conversion_ok_qftumna(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_cond_32pmhkp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_float_9k2be5v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_pat_92m9fnd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_fixed_by_afiovc1(void * a0, void * a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_095q43u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2d2knap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_61q4vm1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_83ij19e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c5b6tc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_du48r1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ebi1ob2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i8teo07(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_isu6baa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ja2c5em(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lhid49k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_o9tvoie(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qm09p7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sl0efd0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sq1gio6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) uint8_t gm_f_has_ord_1io9knm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf38rotgtqq79 gm_f_int_range_jdeefnh(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_literal_core_ii46ie6(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_u1gvj7k(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_numeric_uv79l0u(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_param_has_2r09oc7(void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_vars_vpp9ov6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_kf1lpoo3h6o[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s03s7smc9pnp4[8];
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7s25m3nlrif2[6];
extern const uint8_t gm_s9tjtf1373ti5[5];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sf44a6nc03ff5[10];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgujtgfflv5dk[8];
extern const uint8_t gm_sh57gpir0vvh2[24];
extern const uint8_t gm_siflgunfq3r7b[11];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_snoj0t5lf2lht[4];
extern const uint8_t gm_snqm9b8h9b70a[120];
extern const uint8_t gm_sq89uknu8mulu[8];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr8du5b2u9c7v[33];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcseljft758gtd[];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svknltl22rlbs[9];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq9m6mvtmgde(a, b);
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_a02qt6l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_a02qt6l F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_clear_p214f4b(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_p214f4b F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 20)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        int32_t t11 = gm_f_mag_cmp_s39veq6(t7, t10);
        uint8_t t12 = ((t11 == ((int32_t)0LL)));
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ceufbo4 F;
    GM_STACK_CHECK_SHALLOW(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (t2) {
        uint8_t t3 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        t1 = t3;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_count_omtt6qd F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 48));
    F.ret = t2;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_2v5b782 F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 21u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t88rcucg1b7uk t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_settle_h9ikvs5 *)((&F.h)->up))->l6));
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_settle_h9ikvs5 *)((&F.h)->up))->l7), ((uint8_t)1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_513k31a F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fin_pat_92m9fnd *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ud6f4hb F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fin_expr_rk98p2r *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_b0_1 t4 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
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
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
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
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_numeric_uv79l0u(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_numeric_uv79l0u F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_obligation_ss66lhk *)((&F.h)->up))->l2));
    gm_t88rcucg1b7uk t3 = gm_l0;
    uint8_t t4 = gm_f_is_integer_8d4f9ek(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_obligation_ss66lhk *)((&F.h)->up))->l2));
        gm_t88rcucg1b7uk t6 = gm_l0;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t12 != ((uint8_t)3ULL)) goto mn11;
        t9 = ((uint8_t)1);
        goto me10;
mn11:;
        t9 = ((uint8_t)0);
        goto me10;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 67, 66);
me10:;
        t1 = t9;
    } else t1 = 1;
    F.ret = t1;
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
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rat_1s624db F;
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_untyped_float_m6lbr0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_untyped_float_m6lbr0g F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_u1gvj7k((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kf1lpoo3h6o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcod98slk3vnra, 561, 80);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
    gm_teti411eujvf4 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_obligation_ss66lhk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_obligation_ss66lhk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_t88rcucg1b7uk gm_l7; /* from0 */
    gm_t88rcucg1b7uk gm_l8; /* to */
    gm_tl874t2atnlh3 gm_l9; /* span */
    gm_t88rcucg1b7uk gm_l10; /* from */
    gm_closure gm_l11; /* numeric */
    uint8_t gm_l12; /* ff */
    uint8_t gm_l13; /* tf */
    gm_tpmdr2hmdhsqr gm_l16; /* <temporary array> */
    gm_b16_8 gm_l17; /* methods */
    gm_b16_8 gm_l18; /* hint */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* m */
    gm_t88rcucg1b7uk gm_l23; /* ty */
    gm_tl874t2atnlh3 gm_l24; /* span */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_t88rcucg1b7uk gm_l27; /* t */
    uint8_t gm_l28; /* is_float */
    uint8_t gm_l29; /* ok */
    uint32_t gm_l30; /* p */
    gm_b16_8 gm_l31; /* what */
    GM_STACK_CHECK(&F, 3899u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        F.l2 = t4;
    }
    {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t13 != ((uint8_t)0ULL)) goto mn12;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 0));
        gm_l3 = t14;
        gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 4));
        gm_l4 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 96);
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            gm_t88rcucg1b7uk t21 = gm_l3;
            gm_t88rcucg1b7uk t22 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t22;
        }
        {
            void * t24 = F.l2;
            gm_t88rcucg1b7uk t25 = gm_l5;
            uint8_t t26 = gm_f_is_unit_n83ttnb(t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) goto ie23;
            void * t28 = F.l2;
            gm_t88rcucg1b7uk t29 = gm_l5;
            gm_t53at80pfdus3 t30;
            gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
            gm_t53at80pfdus3 t31 = t30;
            uint8_t t32 = (!(GM_LD(uint8_t, ((uint8_t *)&t31) + 0) == ((uint8_t)5ULL)));
            if (!t32) goto ie23;
            void * t33 = F.l2;
            gm_t88rcucg1b7uk t34 = gm_l5;
            uint8_t t35 = gm_f_is_error_8k27gd1(t33, GM_CONV(gm_csguankko4lmhd, t34));
            uint8_t t36 = (uint8_t)!t35;
            if (!t36) goto ie23;
            {
                gm_tl874t2atnlh3 t38 = gm_l4;
                void * t39 = gm_l0;
                gm_t88rcucg1b7uk t40 = gm_l5;
                gm_b16_8 t41 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t42;
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                gm_tabb710d9mq2e t43;
                gm_f_format_2d2knap((void *)&t43, (void *)NULL, cx, NULL, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t44 = t43;
                if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcseljft758gtd, 59, 39);
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                gm_b16_8 t46 = t45;
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                gm_f_error_s6hn871((void *)&F.l6, (void *)NULL, cx, NULL, t38, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t48 = (void *)((uint8_t *)&F.l6);
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sr8du5b2u9c7v));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(32));
                uint32_t t50 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
                gm_tl874t2atnlh3 t53;
                GM_ST(uint32_t, ((uint8_t *)&t53 + 0), t50);
                GM_ST(uint32_t, ((uint8_t *)&t53 + 4), t51);
                GM_ST(uint32_t, ((uint8_t *)&t53 + 8), t52);
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s9tjtf1373ti5));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(4));
                gm_b0_1 t55 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t48, t49, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t56 = gm_l0;
                void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                gm_b0_1 t58 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t57, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix23;
ie23:;
ix23:;
        }
        goto me5;
mn12:;
ma7:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t60 != ((uint8_t)1ULL)) goto mn59;
        gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 0));
        gm_l7 = t61;
        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 4));
        gm_l8 = t62;
        gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 8));
        gm_l9 = t63;
        {
            void * t65 = gm_l0;
            void * t66 = (void *)(((uint8_t *)t65) + 96);
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
            gm_t88rcucg1b7uk t69 = gm_l7;
            gm_t88rcucg1b7uk t70 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t66, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t70;
        }
        {
            uint8_t t72 = 0;
            void * t73 = F.l2;
            gm_t88rcucg1b7uk t74 = gm_l10;
            uint8_t t75 = gm_f_is_error_8k27gd1(t73, GM_CONV(gm_csguankko4lmhd, t74));
            if (!t75) {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                gm_t88rcucg1b7uk t78 = gm_l10;
                gm_t88rcucg1b7uk t79 = gm_l8;
                uint8_t t80 = gm_f_conversion_ok_qftumna(t77, GM_CONV(gm_csguankko4lmhd, t78), GM_CONV(gm_csguankko4lmhd, t79));
                t72 = t80;
            } else t72 = 1;
            if (!t72) goto ie71;
            {
                gm_b0_1 t82;
                uint8_t t83 = 0;
                F.ret = t82;
                goto gm_fret;
            }
            goto ix71;
ie71:;
ix71:;
        }
        {
            gm_closure t84 = ((gm_closure){(void *)gm_f_numeric_uv79l0u, (void *)&F.h, cx});
            gm_l11 = t84;
        }
        {
            void * t85 = F.l2;
            gm_t88rcucg1b7uk t86 = gm_l10;
            gm_t53at80pfdus3 t87;
            gm_f_kind_jcnt4hm((void *)&t87, t85, GM_CONV(gm_csguankko4lmhd, t86));
            gm_t53at80pfdus3 t88 = t87;
            uint8_t t89;
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
            if (t92 != ((uint8_t)3ULL)) goto mn91;
            t89 = ((uint8_t)1);
            goto me90;
mn91:;
            t89 = ((uint8_t)0);
            goto me90;
mn93:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 68, 19);
me90:;
            gm_l12 = t89;
        }
        {
            void * t94 = F.l2;
            gm_t88rcucg1b7uk t95 = gm_l8;
            gm_t53at80pfdus3 t96;
            gm_f_kind_jcnt4hm((void *)&t96, t94, GM_CONV(gm_csguankko4lmhd, t95));
            gm_t53at80pfdus3 t97 = t96;
            uint8_t t98;
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
            if (t101 != ((uint8_t)3ULL)) goto mn100;
            t98 = ((uint8_t)1);
            goto me99;
mn100:;
            t98 = ((uint8_t)0);
            goto me99;
mn102:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 69, 19);
me99:;
            gm_l13 = t98;
        }
        {
        }
        {
        }
        {
        }
        {
            gm_b16_8 t103;
            uint8_t t105 = 0;
            gm_closure t106 = gm_l11;
            gm_t88rcucg1b7uk t107 = gm_l10;
            gm_closure t108 = t106;
            gm_node *ccx109 = t108.cx;
            void * t110 = t108.code;
            uint8_t t111;
            if (GM_PTR_TAGGED(t110)) {
                t111 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t110))(GM_CONV(gm_csguankko4lmhd, t107));
            } else {
                t111 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t110)(t108.env, ccx109, NULL, t107);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = (uint8_t)!t111;
            if (!t112) {
                gm_closure t113 = gm_l11;
                gm_t88rcucg1b7uk t114 = gm_l8;
                gm_closure t115 = t113;
                gm_node *ccx116 = t115.cx;
                void * t117 = t115.code;
                uint8_t t118;
                if (GM_PTR_TAGGED(t117)) {
                    t118 = ((uint8_t (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t117))(GM_CONV(gm_csguankko4lmhd, t114));
                } else {
                    t118 = ((uint8_t (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t117)(t115.env, ccx116, NULL, t114);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t119 = (uint8_t)!t118;
                t105 = t119;
            } else t105 = 1;
            if (!t105) goto ie104;
            gm_b16_8 t121 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t122 = t121;
            gm_b16_8 t123 = t122;
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
            t103 = t124;
            goto ix104;
ie104:;
            gm_b16_8 t125;
            uint8_t t127 = gm_l12;
            uint8_t t128 = (uint8_t)!t127;
            if (!t128) goto ie126;
            uint8_t t129 = gm_l13;
            uint8_t t130 = (uint8_t)!t129;
            if (!t130) goto ie126;
            {
                gm_b16_8 t133;
                GM_ST(void *, (uint8_t *)&t133, (void *)(gm_s7f68g16j7a3o));
                GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(5));
                gm_b16_8 t134;
                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s02419ud122hi));
                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(7));
                gm_b16_8 t135;
                GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sbanjlvk3n14i));
                GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l14)) + 0, t133);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l14)) + 16, t134);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l14)) + 32, t135);
            }
            void * t136 = ((uint8_t *)&F.l14);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcseljft758gtd, 72, 41);
            gm_b16_8 t137;
            GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t136 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_b16_8 t138 = t137;
            t125 = t138;
            goto ix126;
ie126:;
            gm_b16_8 t139;
            uint8_t t141 = gm_l12;
            if (!t141) goto ie140;
            uint8_t t142 = gm_l13;
            uint8_t t143 = (uint8_t)!t142;
            if (!t143) goto ie140;
            {
                gm_b16_8 t146;
                GM_ST(void *, (uint8_t *)&t146, (void *)(gm_s7f68g16j7a3o));
                GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(5));
                gm_b16_8 t147;
                GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sjb60mo5hejk2));
                GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(5));
                gm_b16_8 t148;
                GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s02419ud122hi));
                GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(7));
                gm_b16_8 t149;
                GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sbanjlvk3n14i));
                GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l15)) + 0, t146);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l15)) + 16, t147);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l15)) + 32, t148);
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tdmi4fk9nhqa7 *)((uint8_t *)&F.l15)) + 48, t149);
            }
            void * t150 = ((uint8_t *)&F.l15);
            if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcseljft758gtd, 74, 37);
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(((uint8_t *)t150 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
            gm_b16_8 t152 = t151;
            t139 = t152;
            goto ix140;
ie140:;
            {
                gm_b16_8 t155;
                GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sfrjcsmisriv1));
                GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(2));
                gm_tpmdr2hmdhsqr t156;
                GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t155);
                gm_tpmdr2hmdhsqr t157 = t156;
                gm_l16 = t157;
            }
            void * t158 = ((uint8_t *)&gm_l16);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcseljft758gtd, 76, 20);
            gm_b16_8 t159;
            GM_ST(void *, (uint8_t *)&t159, (void *)(((uint8_t *)t158 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t160 = t159;
            t139 = t160;
ix140:;
            t125 = t139;
ix126:;
            t103 = t125;
ix104:;
            gm_l17 = t103;
        }
        {
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_svknltl22rlbs));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(8));
            gm_l18 = t161;
        }
        {
            void * t163 = gm_l0;
            void * t164 = GM_LD(void *, (((uint8_t *)t163) + 0));
            gm_t88rcucg1b7uk t165 = gm_l8;
            gm_tkn42u7na3t81 t166 = gm_f_bits_struct_ty_l1kjtap(t164, GM_CONV(gm_csguankko4lmhd, t165));
            gm_t88rcucg1b7uk t167 = gm_l10;
            gm_tkn42u7na3t81 t168;
            GM_ST(uint8_t, (uint8_t *)&t168 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t168 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t168), t167);
            ((uint8_t *)&t168)[4] = 1;
            gm_tkn42u7na3t81 t169 = t166;
            gm_tkn42u7na3t81 t170 = t168;
            uint8_t t171 = (gm_eqs65r2l0loh(((uint8_t *)&t169), ((uint8_t *)&t170)));
            if (!t171) goto ie162;
            {
                gm_tfeab279c16rr t173;
                void * t174 = gm_l0;
                gm_t88rcucg1b7uk t175 = gm_l10;
                gm_b16_8 t176 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 0, t176);
                void * t177 = gm_l0;
                gm_t88rcucg1b7uk t178 = gm_l8;
                gm_b16_8 t179 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t177, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 16, t179);
                void * t180 = gm_l0;
                gm_t88rcucg1b7uk t181 = gm_l8;
                gm_b16_8 t182 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t180, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t173 + 32, t182);
                gm_tabb710d9mq2e t183;
                gm_f_format_ebi1ob2((void *)&t183, (void *)NULL, cx, NULL, &t173);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t184 = t183;
                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_srcseljft758gtd, 81, 24);
                gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 8));
                gm_b16_8 t186 = t185;
                gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                gm_l18 = t187;
            }
            goto ix162;
ie162:;
            uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t190 = (t189 > ((uint64_t)0ULL));
            if (!t190) goto ie188;
            {
                gm_f_list_epvducg((void *)&F.l19, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t193 = gm_l17;
                    gm_l20 = t193;
                }
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t195 = gm_l21;
                        uint64_t t196 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint8_t t197 = (t195 < t196);
                        if (!t197) goto lb194;
                        {
                            uint64_t t200 = gm_l21;
                            uint64_t t201 = (uintptr_t)t200;
                            uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                            gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t201 * 16));
                            gm_l22 = t205;
                            {
                                void * t207 = (void *)((uint8_t *)&F.l19);
                                gm_t6j6smj7qoctu t208;
                                gm_b16_8 t209 = gm_l22;
                                GM_ST(gm_b16_8, (uint8_t *)&t208 + 0, t209);
                                void * t210 = gm_l0;
                                gm_t88rcucg1b7uk t211 = gm_l8;
                                gm_b16_8 t212 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t210, t211);
                                if (gm_unw.kind) {
                                    goto gm_ll198;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t208 + 16, t212);
                                gm_tabb710d9mq2e t213;
                                gm_f_format_61q4vm1((void *)&t213, (void *)NULL, cx, NULL, &t208);
                                if (gm_unw.kind) {
                                    goto gm_ll198;
                                }
                                gm_tabb710d9mq2e t214 = t213;
                                if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_srcseljft758gtd, 84, 47);
                                gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
                                gm_b16_8 t216 = t215;
                                gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                                gm_tftb1odc739t5 t218 = gm_f_push_3ef746b(t207, t217);
                                gm_tftb1odc739t5 t219 = t218;
                                if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 1, gm_srcseljft758gtd, 84, 36);
                                gm_b0_1 t220 = GM_LD(gm_b0_1, (((uint8_t *)&t219) + 1));
                            }
                            goto me203;
mn204:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 84, 17);
me203:;
                        }
lc194:;
                        {
                            uint64_t t222 = gm_l21;
                            uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                            uint64_t t225 = r223;
                            gm_l21 = t225;
                        }
                    }
gm_ll198:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb194; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc194; }
                    }
                    goto gm_lad;
lb194:;
                }
            }
            {
                gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                gm_b16_8 t227 = t226;
                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                gm_b16_8 t229;
                GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t230;
                gm_f_join_sfknljb((void *)&t230, (void *)NULL, cx, NULL, t228, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t231 = t230;
                if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 8, gm_srcseljft758gtd, 85, 54);
                gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&t231) + 8));
                gm_tnpifv20irnph t233;
                GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t232);
                gm_tabb710d9mq2e t234;
                gm_f_format_ja2c5em((void *)&t234, (void *)NULL, cx, NULL, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t235 = t234;
                if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 8, gm_srcseljft758gtd, 85, 24);
                gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&t235) + 8));
                gm_b16_8 t237 = t236;
                gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                gm_l18 = t238;
            }
            goto ix188;
ie188:;
ix188:;
ix162:;
        }
        {
            void * t239 = gm_l0;
            gm_tl874t2atnlh3 t240 = gm_l9;
            gm_tfeab279c16rr t241;
            void * t242 = gm_l0;
            gm_t88rcucg1b7uk t243 = gm_l8;
            gm_b16_8 t244 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t242, t243);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 0, t244);
            void * t245 = gm_l0;
            gm_t88rcucg1b7uk t246 = gm_l10;
            gm_b16_8 t247 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t245, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 16, t247);
            gm_b16_8 t248 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 32, t248);
            gm_tabb710d9mq2e t249;
            gm_f_format_c5b6tc3((void *)&t249, (void *)NULL, cx, NULL, &t241);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t250 = t249;
            if (((((uint8_t *)&t250))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t250) + 8, gm_srcseljft758gtd, 87, 28);
            gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&t250) + 8));
            gm_b16_8 t252 = t251;
            gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&t252));
            gm_b0_1 t254 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t239, t240, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me5;
mn59:;
ma8:;
        uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t256 != ((uint8_t)2ULL)) goto mn255;
        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)a1) + 8) + 0));
        gm_l23 = t257;
        gm_tl874t2atnlh3 t258 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)a1) + 8) + 4));
        gm_l24 = t258;
        {
            void * t260 = gm_l0;
            void * t261 = (void *)(((uint8_t *)t260) + 96);
            void * t262 = gm_l0;
            void * t263 = GM_LD(void *, (((uint8_t *)t262) + 0));
            gm_t88rcucg1b7uk t264 = gm_l23;
            gm_t88rcucg1b7uk t265 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t261, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t265;
        }
        {
            void * t267 = F.l2;
            gm_t88rcucg1b7uk t268 = gm_l25;
            uint8_t t269 = gm_f_is_error_8k27gd1(t267, GM_CONV(gm_csguankko4lmhd, t268));
            uint8_t t270 = (uint8_t)!t269;
            if (!t270) goto ie266;
            void * t271 = gm_l0;
            void * t272 = GM_LD(void *, (((uint8_t *)t271) + 0));
            gm_t88rcucg1b7uk t273 = gm_l25;
            uint8_t t274 = gm_f_has_ord_1io9knm(t272, GM_CONV(gm_csguankko4lmhd, t273));
            uint8_t t275 = (uint8_t)!t274;
            if (!t275) goto ie266;
            {
                void * t277 = gm_l0;
                gm_tl874t2atnlh3 t278 = gm_l24;
                void * t279 = gm_l0;
                gm_t88rcucg1b7uk t280 = gm_l25;
                gm_b16_8 t281 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t279, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t282;
                GM_ST(gm_b16_8, ((uint8_t *)&t282 + 0), t281);
                gm_tabb710d9mq2e t283;
                gm_f_format_095q43u((void *)&t283, (void *)NULL, cx, NULL, t282);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t284 = t283;
                if (((((uint8_t *)&t284))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t284) + 8, gm_srcseljft758gtd, 92, 32);
                gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&t284) + 8));
                gm_b16_8 t286 = t285;
                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                gm_b0_1 t288 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t277, t278, t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix266;
ie266:;
ix266:;
        }
        goto me5;
mn255:;
ma9:;
        uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t290 != ((uint8_t)3ULL)) goto mn289;
        gm_t91vm1g0k2rpm t291 = GM_LD(gm_t91vm1g0k2rpm, (((uint8_t *)a1) + 8));
        F.l26 = t291;
        {
            void * t293 = gm_l0;
            void * t294 = (void *)(((uint8_t *)t293) + 96);
            void * t295 = gm_l0;
            void * t296 = GM_LD(void *, (((uint8_t *)t295) + 0));
            gm_t88rcucg1b7uk t297 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 0));
            gm_t88rcucg1b7uk t298 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t294, t296, t297);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t298;
        }
        {
            void * t300 = F.l2;
            gm_t88rcucg1b7uk t301 = gm_l27;
            gm_t53at80pfdus3 t302;
            gm_f_kind_jcnt4hm((void *)&t302, t300, GM_CONV(gm_csguankko4lmhd, t301));
            gm_t53at80pfdus3 t303 = t302;
            uint8_t t304;
            uint8_t t310 = GM_LD(uint8_t, (((uint8_t *)&t303) + 0));
            switch (t310) { case ((uint8_t)0ULL): goto ma308; case ((uint8_t)1ULL): goto ma308; case ((uint8_t)2ULL): goto ma308; case ((uint8_t)3ULL): goto ma308; case ((uint8_t)4ULL): goto ma308; case ((uint8_t)5ULL): goto ma308; case ((uint8_t)6ULL): goto ma308; case ((uint8_t)7ULL): goto ma308; case ((uint8_t)8ULL): goto ma308; case ((uint8_t)9ULL): goto ma308; case ((uint8_t)10ULL): goto ma308; case ((uint8_t)11ULL): goto ma306; case ((uint8_t)12ULL): goto ma308; case ((uint8_t)13ULL): goto ma308; case ((uint8_t)14ULL): goto ma308; case ((uint8_t)15ULL): goto ma308; case ((uint8_t)16ULL): goto ma308; case ((uint8_t)17ULL): goto ma308; case ((uint8_t)18ULL): goto ma308; case ((uint8_t)19ULL): goto ma308; case ((uint8_t)20ULL): goto ma307; case ((uint8_t)21ULL): goto ma308; case ((uint8_t)22ULL): goto ma308; case ((uint8_t)23ULL): goto ma308; case ((uint8_t)24ULL): goto ma308; case ((uint8_t)25ULL): goto ma308; case ((uint8_t)26ULL): goto ma308; default: goto ma308; }
ma306:;
            uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&t303) + 0));
            if (t312 != ((uint8_t)11ULL)) goto mn311;
            t304 = ((uint8_t)1);
            goto me305;
mn311:;
ma307:;
            uint8_t t314 = GM_LD(uint8_t, (((uint8_t *)&t303) + 0));
            if (t314 != ((uint8_t)20ULL)) goto mn313;
            t304 = ((uint8_t)1);
            goto me305;
mn313:;
ma308:;
            t304 = ((uint8_t)0);
            goto me305;
mn315:;
ma309:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 97, 16);
me305:;
            if (!t304) goto ie299;
            {
                void * t317 = gm_l0;
                gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 4));
                gm_b16_8 t319;
                GM_ST(void *, (uint8_t *)&t319, (void *)(gm_snqm9b8h9b70a));
                GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(119));
                gm_b0_1 t320 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t317, t318, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t321;
                uint8_t t322 = 0;
                F.ret = t321;
                goto gm_fret;
            }
            goto ix299;
ie299:;
ix299:;
        }
        {
            void * t324 = F.l2;
            gm_t88rcucg1b7uk t325 = gm_l27;
            uint8_t t326 = gm_f_is_error_8k27gd1(t324, GM_CONV(gm_csguankko4lmhd, t325));
            if (!t326) goto ie323;
            {
                gm_b0_1 t328;
                uint8_t t329 = 0;
                F.ret = t328;
                goto gm_fret;
            }
            goto ix323;
ie323:;
ix323:;
        }
        {
            void * t330 = F.l2;
            gm_t88rcucg1b7uk t331 = gm_l27;
            gm_t53at80pfdus3 t332;
            gm_f_kind_jcnt4hm((void *)&t332, t330, GM_CONV(gm_csguankko4lmhd, t331));
            gm_t53at80pfdus3 t333 = t332;
            uint8_t t334;
            uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&t333) + 0));
            if (t337 != ((uint8_t)3ULL)) goto mn336;
            t334 = ((uint8_t)1);
            goto me335;
mn336:;
            t334 = ((uint8_t)0);
            goto me335;
mn338:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 102, 25);
me335:;
            gm_l28 = t334;
        }
        {
            gm_l29 = ((uint8_t)0);
        }
        {
            void * t340 = F.l2;
            gm_t88rcucg1b7uk t341 = gm_l27;
            gm_t53at80pfdus3 t342;
            gm_f_kind_jcnt4hm((void *)&t342, t340, GM_CONV(gm_csguankko4lmhd, t341));
            gm_t53at80pfdus3 t343 = t342;
            uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&t343) + 0));
            if (t344 != ((uint8_t)19ULL)) goto ie339;
            uint32_t t345 = GM_LD(uint32_t, (((uint8_t *)&t343) + 8));
            gm_l30 = t345;
            {
                uint8_t t347 = 0;
                uint8_t t348;
                uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
                if (!t350) goto ie349;
                void * t352 = gm_l0;
                void * t353 = GM_LD(void *, (((uint8_t *)t352) + 0));
                uint32_t t354 = gm_l30;
                gm_t9qkh0rsooc8d t355;
                GM_ST(uint8_t, ((uint8_t *)&t355 + 0), ((uint8_t)3ULL));
                uint8_t t356 = gm_f_param_has_2r09oc7(t353, t354, t355);
                uint8_t t357 = t356;
                t348 = t357;
                goto ix349;
ie349:;
                void * t359 = gm_l0;
                void * t360 = GM_LD(void *, (((uint8_t *)t359) + 0));
                uint32_t t361 = gm_l30;
                gm_t9qkh0rsooc8d t362;
                GM_ST(uint8_t, ((uint8_t *)&t362 + 0), ((uint8_t)2ULL));
                uint8_t t363 = gm_f_param_has_2r09oc7(t360, t361, t362);
                uint8_t t364 = t363;
                t348 = t364;
ix349:;
                if (t348) {
                    uint8_t t365 = 0;
                    uint8_t t366 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                    uint8_t t367 = (uint8_t)!t366;
                    if (!t367) {
                        void * t368 = gm_l0;
                        void * t369 = GM_LD(void *, (((uint8_t *)t368) + 0));
                        uint32_t t370 = gm_l30;
                        gm_t9qkh0rsooc8d t371;
                        GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)4ULL));
                        uint8_t t372 = gm_f_param_has_2r09oc7(t369, t370, t371);
                        t365 = t372;
                    } else t365 = 1;
                    t347 = t365;
                } else t347 = 0;
                gm_l29 = t347;
            }
            goto ix339;
ie339:;
            uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
            if (!t374) goto ie373;
            {
                uint8_t t376 = 0;
                void * t377 = F.l2;
                gm_t88rcucg1b7uk t378 = gm_l27;
                uint8_t t379 = gm_f_is_integer_8d4f9ek(t377, GM_CONV(gm_csguankko4lmhd, t378));
                if (t379) {
                    uint8_t t380 = 0;
                    uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                    uint8_t t382 = (uint8_t)!t381;
                    if (!t382) {
                        void * t383 = F.l2;
                        gm_t88rcucg1b7uk t384 = gm_l27;
                        uint8_t t385 = gm_f_is_signed_integer_s66djm7(t383, GM_CONV(gm_csguankko4lmhd, t384));
                        t380 = t385;
                    } else t380 = 1;
                    t376 = t380;
                } else t376 = 0;
                gm_l29 = t376;
            }
            goto ix373;
ie373:;
            uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
            if (!t387) goto ie386;
            {
                uint8_t t389 = 0;
                void * t390 = F.l2;
                gm_t88rcucg1b7uk t391 = gm_l27;
                uint8_t t392 = gm_f_is_signed_integer_s66djm7(t390, GM_CONV(gm_csguankko4lmhd, t391));
                if (!t392) {
                    uint8_t t393 = gm_l28;
                    t389 = t393;
                } else t389 = 1;
                gm_l29 = t389;
            }
            goto ix386;
ie386:;
            {
                uint8_t t395 = 0;
                void * t396 = F.l2;
                gm_t88rcucg1b7uk t397 = gm_l27;
                uint8_t t398 = gm_f_is_integer_8d4f9ek(t396, GM_CONV(gm_csguankko4lmhd, t397));
                if (!t398) {
                    uint8_t t399 = gm_l28;
                    t395 = t399;
                } else t395 = 1;
                gm_l29 = t395;
            }
ix386:;
ix373:;
ix339:;
        }
        {
            uint8_t t401 = gm_l29;
            uint8_t t402 = (uint8_t)!t401;
            if (!t402) goto ie400;
            {
                gm_b16_8 t404;
                uint8_t t406 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 32));
                if (!t406) goto ie405;
                gm_b16_8 t408;
                GM_ST(void *, (uint8_t *)&t408, (void *)(gm_sgujtgfflv5dk));
                GM_ST(uintptr_t, (uint8_t *)&t408 + 8, (uintptr_t)(7));
                gm_b16_8 t409 = t408;
                t404 = t409;
                goto ix405;
ie405:;
                gm_b16_8 t410;
                uint8_t t412 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 33));
                if (!t412) goto ie411;
                gm_b16_8 t414;
                GM_ST(void *, (uint8_t *)&t414, (void *)(gm_sh57gpir0vvh2));
                GM_ST(uintptr_t, (uint8_t *)&t414 + 8, (uintptr_t)(23));
                gm_b16_8 t415 = t414;
                t410 = t415;
                goto ix411;
ie411:;
                gm_b16_8 t417;
                GM_ST(void *, (uint8_t *)&t417, (void *)(gm_s03s7smc9pnp4));
                GM_ST(uintptr_t, (uint8_t *)&t417 + 8, (uintptr_t)(7));
                gm_b16_8 t418 = t417;
                t410 = t418;
ix411:;
                t404 = t410;
ix405:;
                gm_l31 = t404;
            }
            {
                void * t419 = gm_l0;
                gm_tl874t2atnlh3 t420 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 4));
                gm_tfeab279c16rr t421;
                gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t421 + 0, t422);
                gm_b16_8 t423 = gm_l31;
                GM_ST(gm_b16_8, (uint8_t *)&t421 + 16, t423);
                void * t424 = gm_l0;
                gm_t88rcucg1b7uk t425 = gm_l27;
                gm_b16_8 t426 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t424, t425);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t421 + 32, t426);
                gm_tabb710d9mq2e t427;
                gm_f_format_isu6baa((void *)&t427, (void *)NULL, cx, NULL, &t421);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t428 = t427;
                if (((((uint8_t *)&t428))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t428) + 8, gm_srcseljft758gtd, 115, 34);
                gm_b16_8 t429 = GM_LD(gm_b16_8, (((uint8_t *)&t428) + 8));
                gm_b16_8 t430 = t429;
                gm_b16_8 t431 = GM_LD(gm_b16_8, ((uint8_t *)&t430));
                gm_b0_1 t432 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t419, t420, t431);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix400;
ie400:;
ix400:;
        }
        goto me5;
mn289:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 55, 5);
me5:;
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
__attribute__((aligned(4))) uint8_t gm_f_conversion_ok_qftumna(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conversion_ok_qftumna F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* from */
    gm_t88rcucg1b7uk gm_l2; /* to */
    gm_t88rcucg1b7uk gm_l3; /* b */
    gm_t88rcucg1b7uk gm_l4; /* b */
    gm_tep2drsl9408b gm_l5; /* u */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 505u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        uint8_t t6 = gm_f_lossless_a54o69s(t3, GM_CONV(gm_csguankko4lmhd, t4), GM_CONV(gm_csguankko4lmhd, t5));
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_tkn42u7na3t81 t12 = gm_f_bits_struct_ty_l1kjtap(t10, GM_CONV(gm_csguankko4lmhd, t11));
        gm_tkn42u7na3t81 t13 = t12;
        if (!((((uint8_t *)&t13))[4] != 0)) goto ie9;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t13));
        gm_l3 = t14;
        gm_t88rcucg1b7uk t15 = gm_l3;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = t15;
        gm_t88rcucg1b7uk t18 = t16;
        uint8_t t19 = (gm_eq9m6mvtmgde(((uint8_t *)&t17), ((uint8_t *)&t18)));
        if (!t19) goto ie9;
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        uint8_t t22 = gm_f_is_plain_n60iihm(t20, GM_CONV(gm_csguankko4lmhd, t21));
        if (!t22) goto ie9;
        {
            uint8_t t24 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t26 = gm_l0;
        gm_t88rcucg1b7uk t27 = gm_l2;
        gm_tkn42u7na3t81 t28 = gm_f_bits_struct_ty_l1kjtap(t26, GM_CONV(gm_csguankko4lmhd, t27));
        gm_tkn42u7na3t81 t29 = t28;
        if (!((((uint8_t *)&t29))[4] != 0)) goto ie25;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t29));
        gm_l4 = t30;
        gm_t88rcucg1b7uk t31 = gm_l4;
        gm_t88rcucg1b7uk t32 = gm_l1;
        gm_t88rcucg1b7uk t33 = t31;
        gm_t88rcucg1b7uk t34 = t32;
        uint8_t t35 = (gm_eq9m6mvtmgde(((uint8_t *)&t33), ((uint8_t *)&t34)));
        if (!t35) goto ie25;
        void * t36 = gm_l0;
        gm_t88rcucg1b7uk t37 = gm_l2;
        uint8_t t38 = gm_f_is_plain_n60iihm(t36, GM_CONV(gm_csguankko4lmhd, t37));
        if (!t38) goto ie25;
        {
            uint8_t t40 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix25;
ie25:;
ix25:;
    }
    {
        void * t42 = gm_l0;
        gm_t88rcucg1b7uk t43 = gm_l1;
        gm_tdlpv0q69lov1 t44;
        gm_f_union_variants_a7khaqp((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
        gm_tdlpv0q69lov1 t45 = t44;
        if (!((((uint8_t *)&t45))[24] != 0)) goto ie41;
        gm_tep2drsl9408b t46 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&t45));
        gm_l5 = t46;
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 8);
        gm_t88rcucg1b7uk t49 = gm_l2;
        uint8_t t50 = gm_f_is_integer_8d4f9ek(t48, GM_CONV(gm_csguankko4lmhd, t49));
        if (!t50) goto ie41;
        {
            {
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
                gm_l6 = t53;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t55 = gm_l7;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t57 = (t55 < t56);
                    if (!t57) goto lb54;
                    {
                        uint64_t t60 = gm_l7;
                        uint64_t t61 = (uintptr_t)t60;
                        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tckdobunaulee t65 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t61 * 72));
                        F.l8 = t65;
                        {
                            uint8_t t68;
                            if ((((((uint8_t *)&F.l8) + 16))[4] != 0)) goto mn70;
                            t68 = ((uint8_t)0);
                            goto me69;
mn70:;
                            t68 = ((uint8_t)1);
                            goto me69;
mn71:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 294, 16);
me69:;
                            if (!t68) goto ie67;
                            {
                                uint8_t t73 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix67;
ie67:;
ix67:;
                        }
                        goto me63;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 293, 9);
me63:;
                    }
lc54:;
                    {
                        uint64_t t75 = gm_l7;
                        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                        uint64_t t78 = r76;
                        gm_l7 = t78;
                    }
                }
lb54:;
            }
        }
        {
            void * t79 = gm_l0;
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 16));
            gm_t88rcucg1b7uk t81 = gm_l2;
            uint8_t t82 = gm_f_lossless_a54o69s(t79, GM_CONV(gm_csguankko4lmhd, t80), GM_CONV(gm_csguankko4lmhd, t81));
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint8_t t84 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fin_cond_32pmhkp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fin_cond_32pmhkp F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* p */
    GM_STACK_CHECK(&F, 202u);
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
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 24));
                    gm_l4 = t15;
                    {
                        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t18 != ((uint8_t)1ULL)) goto ie17;
                        void * t19 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l5 = t19;
                        {
                            void * t21 = gm_l0;
                            void * t22 = gm_l5;
                            gm_b0_1 t23 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t21, t22);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 199, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l3;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l3 = t28;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_expr_rk98p2r(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fin_expr_rk98p2r F;
    void * gm_l1; /* e */
    uint64_t gm_l2; /* errors */
    gm_b16_8 gm_l3; /* arms */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l6; /* a */
    gm_trl2khmuphncd gm_l7; /* x */
    gm_toceffb697p0u gm_l9; /* fixed_by */
    void * gm_l10; /* tys */
    gm_tl874t2atnlh3 gm_l13; /* span */
    gm_b16_8 gm_l14; /* t */
    void * gm_l15; /* r */
    void * gm_l16; /* c */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_tl874t2atnlh3 gm_l18; /* span */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l24; /* x */
    gm_tmn7n0n9raou5 gm_l25; /* kind */
    void * gm_l26; /* inner */
    gm_t88rcucg1b7uk gm_l27; /* target */
    gm_b16_8 gm_l28; /* steps */
    GM_STACK_CHECK(&F, 4245u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = F.l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 8));
        uint64_t t6 = gm_f_error_count_omtt6qd(t5);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l1;
        gm_closure t8 = ((gm_closure){(void *)gm_f_fn_ud6f4hb, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t9 = gm_l1;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma14; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma14; case ((uint8_t)32ULL): goto ma14; case ((uint8_t)33ULL): goto ma14; case ((uint8_t)34ULL): goto ma14; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma14; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma14; case ((uint8_t)39ULL): goto ma14; case ((uint8_t)40ULL): goto ma14; case ((uint8_t)41ULL): goto ma14; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma14; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma14; case ((uint8_t)50ULL): goto ma14; case ((uint8_t)51ULL): goto ma14; case ((uint8_t)52ULL): goto ma14; case ((uint8_t)53ULL): goto ma14; case ((uint8_t)54ULL): goto ma14; case ((uint8_t)55ULL): goto ma14; case ((uint8_t)56ULL): goto ma14; case ((uint8_t)57ULL): goto ma14; case ((uint8_t)58ULL): goto ma14; default: goto ma14; }
ma11:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t18 != ((uint8_t)44ULL)) goto mn17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)t9) + 8) + 8) + 8));
        gm_l3 = t19;
        {
            {
                gm_b16_8 t22 = gm_l3;
                gm_l4 = t22;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t24 = gm_l5;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t26 = (t24 < t25);
                    if (!t26) goto lb23;
                    {
                        uint64_t t29 = gm_l5;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tbbqefuhv2r8h t34 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t30 * 24));
                        gm_l6 = t34;
                        {
                            void * t36 = F.l0;
                            void * t37 = GM_LD(void *, (((uint8_t *)&gm_l6) + 0));
                            gm_b0_1 t38 = gm_f_fin_pat_92m9fnd((void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_ll27;
                            }
                        }
                        goto me32;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 127, 13);
me32:;
                    }
lc23:;
                    {
                        uint64_t t40 = gm_l5;
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        uint64_t t43 = r41;
                        gm_l5 = t43;
                    }
                }
gm_ll27:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
                }
                goto gm_lad;
lb23:;
            }
        }
        {
            void * t45 = F.l0;
            uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 480) + 0) + 8);
            uint8_t t47 = (t46 > ((uint64_t)0ULL));
            if (!t47) goto ie44;
            void * t48 = F.l0;
            void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 8));
            uint64_t t52 = gm_f_error_count_omtt6qd(t51);
            uint64_t t53 = gm_l2;
            uint8_t t54 = ((t52 == t53));
            if (!t54) goto ie44;
            {
                void * t56 = F.l0;
                void * t57 = gm_l1;
                gm_b0_1 t58 = gm_f_check_deferred_match_742e2u5((void *)NULL, cx, NULL, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix44;
ie44:;
ix44:;
        }
        goto me10;
mn17:;
ma12:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t60 != ((uint8_t)47ULL)) goto mn59;
        gm_trl2khmuphncd t61 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t9) + 8) + 8));
        gm_l7 = t61;
        void * t62 = F.l0;
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
        gm_b16_8 t64 = t63;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_b0_1 t66 = gm_f_fin_cond_32pmhkp((void *)NULL, cx, NULL, t62, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me10;
mn59:;
ma13:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t68 != ((uint8_t)48ULL)) goto mn67;
        gm_t710lsrq1m8gk t69 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t9) + 8) + 8));
        F.l8 = t69;
        void * t70 = F.l0;
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
        gm_b16_8 t72 = t71;
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_b0_1 t74 = gm_f_fin_cond_32pmhkp((void *)NULL, cx, NULL, t70, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me10;
mn67:;
ma14:;
        goto me10;
mn75:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 125, 5);
me10:;
    }
    {
        gm_toceffb697p0u t77;
        void * t79 = gm_l1;
        uint8_t t80;
        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t79) + 8) + 0));
        if (t83 != ((uint8_t)0ULL)) goto mn82;
        t80 = ((uint8_t)1);
        goto me81;
mn82:;
        t80 = ((uint8_t)0);
        goto me81;
mn84:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 135, 33);
me81:;
        if (!t80) goto ie78;
        void * t86 = F.l0;
        void * t87 = (void *)(((uint8_t *)t86) + 96);
        void * t88 = F.l0;
        void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
        void * t90 = (void *)(((uint8_t *)t89) + 8);
        void * t91 = gm_l1;
        gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t91) + 0));
        gm_toceffb697p0u t93 = gm_f_fixed_by_afiovc1(t87, t90, GM_CONV(gm_csguankko4lmhd, t92));
        gm_toceffb697p0u t94 = t93;
        t77 = t94;
        goto ix78;
ie78:;
        gm_toceffb697p0u t96;
        memset(&t96, 0, sizeof t96);
        gm_toceffb697p0u t97 = t96;
        t77 = t97;
ix78:;
        gm_l9 = t77;
    }
    {
        void * t98 = gm_l1;
        uint8_t *t99 = (((uint8_t *)t98) + 0);
        void * t100 = F.l0;
        void * t101 = (void *)(((uint8_t *)t100) + 96);
        void * t102 = F.l0;
        void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
        void * t104 = gm_l1;
        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 0));
        gm_t88rcucg1b7uk t106 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t101, t103, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, t99, t106);
    }
    {
        void * t107 = F.l0;
        void * t108 = GM_LD(void *, (((uint8_t *)t107) + 0));
        void * t109 = (void *)(((uint8_t *)t108) + 8);
        gm_l10 = t109;
    }
    {
        void * t110 = gm_l1;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
        switch (t118) { case ((uint8_t)0ULL): goto ma112; case ((uint8_t)1ULL): goto ma116; case ((uint8_t)2ULL): goto ma113; case ((uint8_t)3ULL): goto ma114; case ((uint8_t)4ULL): goto ma116; case ((uint8_t)5ULL): goto ma116; case ((uint8_t)6ULL): goto ma116; case ((uint8_t)7ULL): goto ma116; case ((uint8_t)8ULL): goto ma116; case ((uint8_t)9ULL): goto ma116; case ((uint8_t)10ULL): goto ma116; case ((uint8_t)11ULL): goto ma116; case ((uint8_t)12ULL): goto ma116; case ((uint8_t)13ULL): goto ma116; case ((uint8_t)14ULL): goto ma116; case ((uint8_t)15ULL): goto ma116; case ((uint8_t)16ULL): goto ma116; case ((uint8_t)17ULL): goto ma116; case ((uint8_t)18ULL): goto ma116; case ((uint8_t)19ULL): goto ma116; case ((uint8_t)20ULL): goto ma116; case ((uint8_t)21ULL): goto ma116; case ((uint8_t)22ULL): goto ma116; case ((uint8_t)23ULL): goto ma116; case ((uint8_t)24ULL): goto ma116; case ((uint8_t)25ULL): goto ma116; case ((uint8_t)26ULL): goto ma116; case ((uint8_t)27ULL): goto ma116; case ((uint8_t)28ULL): goto ma116; case ((uint8_t)29ULL): goto ma116; case ((uint8_t)30ULL): goto ma116; case ((uint8_t)31ULL): goto ma116; case ((uint8_t)32ULL): goto ma115; case ((uint8_t)33ULL): goto ma116; case ((uint8_t)34ULL): goto ma116; case ((uint8_t)35ULL): goto ma116; case ((uint8_t)36ULL): goto ma116; case ((uint8_t)37ULL): goto ma116; case ((uint8_t)38ULL): goto ma116; case ((uint8_t)39ULL): goto ma116; case ((uint8_t)40ULL): goto ma116; case ((uint8_t)41ULL): goto ma116; case ((uint8_t)42ULL): goto ma116; case ((uint8_t)43ULL): goto ma116; case ((uint8_t)44ULL): goto ma116; case ((uint8_t)45ULL): goto ma116; case ((uint8_t)46ULL): goto ma116; case ((uint8_t)47ULL): goto ma116; case ((uint8_t)48ULL): goto ma116; case ((uint8_t)49ULL): goto ma116; case ((uint8_t)50ULL): goto ma116; case ((uint8_t)51ULL): goto ma116; case ((uint8_t)52ULL): goto ma116; case ((uint8_t)53ULL): goto ma116; case ((uint8_t)54ULL): goto ma116; case ((uint8_t)55ULL): goto ma116; case ((uint8_t)56ULL): goto ma116; case ((uint8_t)57ULL): goto ma116; case ((uint8_t)58ULL): goto ma116; default: goto ma116; }
ma112:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
        if (t120 != ((uint8_t)0ULL)) goto mn119;
        gm_tbci5ospk5nnn t121 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t110) + 8) + 8));
        F.l11 = t121;
        {
            void * t124 = gm_l10;
            void * t125 = gm_l1;
            gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t125) + 0));
            gm_t53at80pfdus3 t127;
            gm_f_kind_jcnt4hm((void *)&t127, t124, GM_CONV(gm_csguankko4lmhd, t126));
            gm_t53at80pfdus3 t128 = t127;
            uint8_t t129;
            uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&t128) + 0));
            if (t132 != ((uint8_t)19ULL)) goto mn131;
            t129 = ((uint8_t)1);
            goto me130;
mn131:;
            t129 = ((uint8_t)0);
            goto me130;
mn133:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 140, 16);
me130:;
            if (!t129) goto ie123;
            {
                gm_b0_1 t135;
                uint8_t t136 = 0;
                F.ret = t135;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        {
            void * t138 = gm_l10;
            void * t139 = gm_l1;
            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t139) + 0));
            gm_t53at80pfdus3 t141;
            gm_f_kind_jcnt4hm((void *)&t141, t138, GM_CONV(gm_csguankko4lmhd, t140));
            gm_t53at80pfdus3 t142 = t141;
            uint8_t t143;
            uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
            if (t146 != ((uint8_t)3ULL)) goto mn145;
            t143 = ((uint8_t)1);
            goto me144;
mn145:;
            t143 = ((uint8_t)0);
            goto me144;
mn147:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 141, 16);
me144:;
            if (!t143) goto ie137;
            {
                void * t149 = F.l0;
                void * t150 = gm_l1;
                gm_tngtuiuskn938 t151;
                gm_f_from_int_ofjuoqh((void *)&t151, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t152 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t149, t150, &t151, ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix137;
ie137:;
            void * t154 = gm_l10;
            void * t155 = gm_l1;
            gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t155) + 0));
            uint8_t t157 = gm_f_is_error_8k27gd1(t154, GM_CONV(gm_csguankko4lmhd, t156));
            uint8_t t158 = (uint8_t)!t157;
            if (!t158) goto ie153;
            void * t159 = F.l0;
            void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
            void * t161 = gm_l1;
            gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 0));
            uint8_t t163 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t160, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t164 = (uint8_t)!t163;
            if (!t164) goto ie153;
            {
                void * t166 = gm_l1;
                gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t166) + 72));
                gm_t6j6smj7qoctu t168;
                gm_b16_8 t169 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t168 + 0, t169);
                void * t170 = F.l0;
                void * t171 = gm_l1;
                gm_t88rcucg1b7uk t172 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t171) + 0));
                gm_b16_8 t173 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t170, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t168 + 16, t173);
                gm_tabb710d9mq2e t174;
                gm_f_format_lhid49k((void *)&t174, (void *)NULL, cx, NULL, &t168);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t175 = t174;
                if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 8, gm_srcseljft758gtd, 144, 41);
                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t175) + 8));
                gm_b16_8 t177 = t176;
                gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                gm_f_error_s6hn871((void *)&F.l12, (void *)NULL, cx, NULL, t167, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                if (!((((uint8_t *)&gm_l9))[12] != 0)) goto ie179;
                gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l9));
                gm_l13 = t180;
                {
                    void * t182 = F.l0;
                    void * t183 = gm_l1;
                    gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t183) + 0));
                    gm_b16_8 t185 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t182, t184);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t185;
                }
                {
                    void * t186 = (void *)((uint8_t *)&F.l12);
                    gm_tl874t2atnlh3 t187 = gm_l13;
                    gm_tfeab279c16rr t188;
                    gm_b16_8 t189 = gm_l14;
                    GM_ST(gm_b16_8, (uint8_t *)&t188 + 0, t189);
                    gm_b16_8 t190 = gm_l14;
                    GM_ST(gm_b16_8, (uint8_t *)&t188 + 16, t190);
                    gm_b16_8 t191 = gm_l14;
                    GM_ST(gm_b16_8, (uint8_t *)&t188 + 32, t191);
                    gm_tabb710d9mq2e t192;
                    gm_f_format_i8teo07((void *)&t192, (void *)NULL, cx, NULL, &t188);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t193 = t192;
                    if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_srcseljft758gtd, 148, 39);
                    gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
                    gm_b16_8 t195 = t194;
                    gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
                    gm_f_add_label_50fj7vv(t186, GM_CONV(gm_cseahvg8v7p0vs, t187), t196);
                }
                goto ix179;
ie179:;
ix179:;
            }
            {
                void * t197 = F.l0;
                void * t198 = GM_LD(void *, (((uint8_t *)t197) + 0));
                gm_b0_1 t199 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t198, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix153;
ie153:;
ix153:;
ix137:;
        }
        goto me111;
mn119:;
ma113:;
        uint8_t t201 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
        if (t201 != ((uint8_t)2ULL)) goto mn200;
        void * t202 = GM_LD(void *, ((((uint8_t *)t110) + 8) + 8));
        gm_l15 = t202;
        {
            void * t205 = gm_l10;
            void * t206 = gm_l1;
            gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t206) + 0));
            gm_t53at80pfdus3 t208;
            gm_f_kind_jcnt4hm((void *)&t208, t205, GM_CONV(gm_csguankko4lmhd, t207));
            gm_t53at80pfdus3 t209 = t208;
            uint8_t t210;
            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&t209) + 0));
            if (t213 != ((uint8_t)19ULL)) goto mn212;
            t210 = ((uint8_t)1);
            goto me211;
mn212:;
            t210 = ((uint8_t)0);
            goto me211;
mn214:;
            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 154, 16);
me211:;
            if (!t210) goto ie204;
            {
                gm_b0_1 t216;
                uint8_t t217 = 0;
                F.ret = t216;
                goto gm_fret;
            }
            goto ix204;
ie204:;
ix204:;
        }
        {
            void * t219 = gm_l10;
            void * t220 = gm_l1;
            gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t220) + 0));
            uint8_t t222 = gm_f_is_integer_8d4f9ek(t219, GM_CONV(gm_csguankko4lmhd, t221));
            if (!t222) goto ie218;
            {
                void * t224 = F.l0;
                void * t225 = gm_l1;
                gm_tl874t2atnlh3 t226 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t225) + 72));
                void * t227 = F.l0;
                void * t228 = gm_l1;
                gm_t88rcucg1b7uk t229 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t228) + 0));
                gm_b16_8 t230 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t227, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t231;
                GM_ST(gm_b16_8, ((uint8_t *)&t231 + 0), t230);
                gm_tabb710d9mq2e t232;
                gm_f_format_i1v5tqv((void *)&t232, (void *)NULL, cx, NULL, t231);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t233 = t232;
                if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 8, gm_srcseljft758gtd, 156, 34);
                gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t233) + 8));
                gm_b16_8 t235 = t234;
                gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                gm_b0_1 t237 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t224, t226, t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t238 = gm_l1;
                uint8_t *t239 = (((uint8_t *)t238) + 8);
                gm_tbci5ospk5nnn t240;
                gm_f_zero_s25l990((void *)&t240, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_teti411eujvf4 t241;
                GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t241 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t241 + 8), t240);
                GM_ST(gm_teti411eujvf4, t239, t241);
            }
            goto ix218;
ie218:;
            {
                void * t243 = F.l0;
                void * t244 = gm_l1;
                void * t245 = gm_l15;
                gm_b0_1 t246 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t243, t244, &(*(gm_tngtuiuskn938 *)((uint8_t *)t245)), ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix218:;
        }
        goto me111;
mn200:;
ma114:;
        uint8_t t248 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
        if (t248 != ((uint8_t)3ULL)) goto mn247;
        void * t249 = GM_LD(void *, ((((uint8_t *)t110) + 8) + 8));
        gm_l16 = t249;
        {
            void * t251 = F.l0;
            void * t252 = GM_LD(void *, (((uint8_t *)t251) + 0));
            void * t253 = gm_l1;
            gm_t88rcucg1b7uk t254 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t253) + 0));
            gm_tkn42u7na3t81 t255 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t252, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t256 = t255;
            gm_t88rcucg1b7uk t257;
            if (((((uint8_t *)&t256))[4] != 0)) {
                gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t256) + 0));
                t257 = t258;
            } else {
                {
                    void * t260 = F.l0;
                    void * t261 = gm_l1;
                    gm_tl874t2atnlh3 t262 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t261) + 72));
                    void * t263 = F.l0;
                    void * t264 = gm_l1;
                    gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t264) + 0));
                    gm_b16_8 t266 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t263, t265);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t267;
                    GM_ST(gm_b16_8, ((uint8_t *)&t267 + 0), t266);
                    gm_tabb710d9mq2e t268;
                    gm_f_format_sq1gio6((void *)&t268, (void *)NULL, cx, NULL, t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t269 = t268;
                    if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 8, gm_srcseljft758gtd, 165, 34);
                    gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                    gm_b16_8 t271 = t270;
                    gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                    gm_b0_1 t273 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t260, t262, t272);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t274 = gm_l1;
                    uint8_t *t275 = ((uint8_t *)t274);
                    void * t276 = F.l0;
                    void * t277 = GM_LD(void *, (((uint8_t *)t276) + 0));
                    void * t278 = gm_l1;
                    gm_tl874t2atnlh3 t279 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t278) + 72));
                    void * t280 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t277, t279);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t69m56meqefs8 t281 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t280));
                    GM_ST(gm_t69m56meqefs8, t275, t281);
                }
                gm_b0_1 t282;
                uint8_t t283 = 0;
                F.ret = t282;
                goto gm_fret;
            }
            gm_l17 = t257;
        }
        {
            void * t284 = gm_l1;
            gm_tl874t2atnlh3 t285 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t284) + 72));
            gm_l18 = t285;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l19, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                {
                    void * t288 = gm_l16;
                    memcpy((uint8_t *)&F.l20 + 0, (((uint8_t *)t288) + 0), 96);
                    void * t289 = gm_l16;
                    memcpy((uint8_t *)&F.l20 + 96, (((uint8_t *)t289) + 96), 96);
                }
                void * t290 = ((uint8_t *)&F.l20);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcseljft758gtd, 171, 13);
                gm_b16_8 t291;
                GM_ST(void *, (uint8_t *)&t291, (void *)(((uint8_t *)t290 + (uintptr_t)0 * 96)));
                GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t292 = t291;
                gm_l21 = t292;
            }
            {
                gm_l22 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t294 = gm_l22;
                    uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    uint8_t t296 = (t294 < t295);
                    if (!t296) goto lb293;
                    {
                        uint64_t t299 = gm_l22;
                        uint64_t t300 = (uintptr_t)t299;
                        uint64_t t301 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        gm_tngtuiuskn938 t304 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t300 * 96));
                        F.l23 = t304;
                        {
                            gm_t88rcucg1b7uk t306 = gm_l17;
                            gm_teti411eujvf4 t307;
                            gm_f_untyped_float_m6lbr0g((void *)&t307, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l23)));
                            if (gm_unw.kind) {
                                goto gm_ll297;
                            }
                            gm_tl874t2atnlh3 t308 = gm_l18;
                            void * t309 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t306, &t307, t308);
                            if (gm_unw.kind) {
                                goto gm_ll297;
                            }
                            gm_l24 = t309;
                        }
                        {
                            void * t311 = gm_l10;
                            gm_t88rcucg1b7uk t312 = gm_l17;
                            gm_t53at80pfdus3 t313;
                            gm_f_kind_jcnt4hm((void *)&t313, t311, GM_CONV(gm_csguankko4lmhd, t312));
                            gm_t53at80pfdus3 t314 = t313;
                            uint8_t t315;
                            uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&t314) + 0));
                            if (t318 != ((uint8_t)19ULL)) goto mn317;
                            t315 = ((uint8_t)0);
                            goto me316;
mn317:;
                            t315 = ((uint8_t)1);
                            goto me316;
mn319:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 173, 20);
me316:;
                            if (!t315) goto ie310;
                            {
                                void * t321 = F.l0;
                                void * t322 = gm_l24;
                                gm_b0_1 t323 = gm_f_fin_float_9k2be5v((void *)NULL, cx, NULL, t321, t322, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l23)), ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_ll297;
                                }
                            }
                            goto ix310;
ie310:;
ix310:;
                        }
                        {
                            void * t324 = (void *)((uint8_t *)&F.l19);
                            void * t325 = gm_l24;
                            gm_tftb1odc739t5 t326 = gm_f_push_d943vgp_2(t324, &(*(gm_t69m56meqefs8 *)((uint8_t *)t325)));
                            gm_tftb1odc739t5 t327 = t326;
                            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 1, gm_srcseljft758gtd, 174, 17);
                            gm_b0_1 t328 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 1));
                        }
                        goto me302;
mn303:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 171, 13);
me302:;
                    }
lc293:;
                    {
                        uint64_t t330 = gm_l22;
                        uint64_t r331; bool o332 = __builtin_add_overflow(t330, ((uint64_t)1ULL), &r331);
                        uint64_t t333 = r331;
                        gm_l22 = t333;
                    }
                }
gm_ll297:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb293; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc293; }
                }
                goto gm_lad;
lb293:;
            }
        }
        {
            void * t334 = gm_l1;
            gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
            gm_teti411eujvf4 t336;
            memset(&t336, 0, sizeof t336);
            GM_ST(uint8_t, ((uint8_t *)&t336 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t336 + 8), t335);
            GM_ST(gm_teti411eujvf4, (((uint8_t *)t334) + 8), t336);
        }
        goto me111;
mn247:;
ma115:;
        uint8_t t338 = GM_LD(uint8_t, ((((uint8_t *)t110) + 8) + 0));
        if (t338 != ((uint8_t)32ULL)) goto mn337;
        gm_tmn7n0n9raou5 t339 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t110) + 8) + 8) + 0));
        gm_l25 = t339;
        void * t340 = GM_LD(void *, (((((uint8_t *)t110) + 8) + 8) + 8));
        gm_l26 = t340;
        uint8_t t342 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l25) + 0) == ((uint8_t)12ULL)));
        if (!t342) goto ie341;
        {
            void * t344 = gm_l1;
            gm_t88rcucg1b7uk t345 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t344) + 0));
            gm_l27 = t345;
        }
        {
            void * t347 = gm_l26;
            gm_t88rcucg1b7uk t348 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t347) + 0));
            gm_t88rcucg1b7uk t349 = gm_l27;
            gm_t88rcucg1b7uk t350 = t348;
            gm_t88rcucg1b7uk t351 = t349;
            uint8_t t352 = (gm_eq9m6mvtmgde(((uint8_t *)&t350), ((uint8_t *)&t351)));
            if (!t352) goto ie346;
            {
                void * t354 = gm_l1;
                void * t355 = gm_l26;
                gm_t69m56meqefs8 t356 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t355));
                GM_ST(gm_t69m56meqefs8, ((uint8_t *)t354), t356);
            }
            {
                gm_b0_1 t357;
                uint8_t t358 = 0;
                F.ret = t357;
                goto gm_fret;
            }
            goto ix346;
ie346:;
ix346:;
        }
        {
            void * t360 = F.l0;
            void * t361 = gm_l26;
            gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t361) + 0));
            gm_t88rcucg1b7uk t363 = gm_l27;
            gm_th2lqbo77pvsc t364;
            gm_f_steps_for_768gb3b((void *)&t364, (void *)NULL, cx, NULL, t360, t362, t363, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t365 = t364;
            if (!((((uint8_t *)&t365))[16] != 0)) goto ie359;
            gm_b16_8 t366 = GM_LD(gm_b16_8, ((uint8_t *)&t365));
            gm_l28 = t366;
            {
                void * t368 = gm_l1;
                uint8_t *t369 = ((uint8_t *)t368);
                void * t370 = gm_l26;
                gm_b16_8 t371 = gm_l28;
                gm_b16_8 t372 = t371;
                gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                void * t374 = gm_f_apply_steps_fd8cr97((void *)NULL, cx, NULL, t370, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t69m56meqefs8 t375 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t374));
                GM_ST(gm_t69m56meqefs8, t369, t375);
            }
            {
                gm_b0_1 t376;
                uint8_t t377 = 0;
                F.ret = t376;
                goto gm_fret;
            }
            goto ix359;
ie359:;
ix359:;
        }
        {
            void * t379 = gm_l26;
            gm_tl874t2atnlh3 t380 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t379) + 72));
            void * t381 = F.l0;
            gm_b16_8 t382 = GM_LD(gm_b16_8, ((((uint8_t *)t381) + 360) + 0));
            gm_b16_8 t383 = t382;
            uint8_t t384 = 0;
            gm_tl874t2atnlh3 t385 = t380;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t383) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t383))) + i * 12, ((uint8_t *)&t385))) { t384 = 1; break; }
            uint8_t t386 = (uint8_t)(!t384);
            if (!t386) goto ie378;
            {
                void * t388 = F.l0;
                void * t389 = gm_l26;
                gm_tl874t2atnlh3 t390 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t389) + 72));
                gm_t6j6smj7qoctu t391;
                void * t392 = F.l0;
                gm_t88rcucg1b7uk t393 = gm_l27;
                gm_b16_8 t394 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t392, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t391 + 0, t394);
                void * t395 = F.l0;
                void * t396 = gm_l26;
                gm_t88rcucg1b7uk t397 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t396) + 0));
                gm_b16_8 t398 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t395, t397);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t391 + 16, t398);
                gm_tabb710d9mq2e t399;
                gm_f_format_ns6dben((void *)&t399, (void *)NULL, cx, NULL, &t391);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t400 = t399;
                if (((((uint8_t *)&t400))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t400) + 8, gm_srcseljft758gtd, 189, 38);
                gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&t400) + 8));
                gm_b16_8 t402 = t401;
                gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
                gm_b0_1 t404 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t388, t390, t403);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix378;
ie378:;
ix378:;
        }
        {
            void * t405 = gm_l1;
            uint8_t *t406 = ((uint8_t *)t405);
            void * t407 = F.l0;
            void * t408 = GM_LD(void *, (((uint8_t *)t407) + 0));
            void * t409 = gm_l26;
            gm_tl874t2atnlh3 t410 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t409) + 72));
            void * t411 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t408, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t69m56meqefs8 t412 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t411));
            GM_ST(gm_t69m56meqefs8, t406, t412);
        }
        goto ix341;
ie341:;
ix341:;
        goto me111;
mn337:;
ma116:;
        goto me111;
mn413:;
ma117:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 138, 5);
me111:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_float_9k2be5v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3) {
    struct gm_fr_fin_float_9k2be5v F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l3; /* from_int */
    uint16_t gm_l4; /* bits */
    double gm_l5; /* x */
    GM_STACK_CHECK(&F, 930u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        void * t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
        gm_t53at80pfdus3 t8;
        gm_f_kind_jcnt4hm((void *)&t8, t5, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t53at80pfdus3 t9 = t8;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t10 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t11 = GM_LD(uint16_t, (((uint8_t *)&t9) + 8));
        gm_l4 = t11;
        {
            uint16_t t13 = gm_l4;
            double t14 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)), t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t14;
        }
        {
            double t16 = gm_l5;
            double t17 = gm_l5;
            double t18 = (t16 - t17);
            uint8_t t19 = (!(t18 == ((double)0e0)));
            if (!t19) goto ie15;
            {
                void * t21 = gm_l0;
                void * t22 = gm_l1;
                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
                void * t24 = gm_l0;
                void * t25 = gm_l1;
                gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + 0));
                gm_b16_8 t27 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t24, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t28;
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
                gm_tabb710d9mq2e t29;
                gm_f_format_sl0efd0((void *)&t29, (void *)NULL, cx, NULL, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t30 = t29;
                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcseljft758gtd, 226, 26);
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                gm_b16_8 t32 = t31;
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                gm_b0_1 t34 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t21, t23, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t35 = gm_l1;
                gm_teti411eujvf4 t36;
                memset(&t36, 0, sizeof t36);
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t36 + 8), ((double)0e0));
                GM_ST(gm_teti411eujvf4, (((uint8_t *)t35) + 8), t36);
            }
            {
                gm_b0_1 t37;
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
            goto ix15;
ie15:;
ix15:;
        }
        {
            uint8_t t40 = gm_l3;
            if (!t40) goto ie39;
            double t41 = gm_l5;
            gm_tngtuiuskn938 t42;
            gm_f_from_f64_1parp9j((void *)&t42, (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = gm_f_eq_ceufbo4(&t42, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
            uint8_t t44 = (uint8_t)!t43;
            if (!t44) goto ie39;
            {
                void * t46 = gm_l0;
                void * t47 = gm_l1;
                gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + 72));
                gm_t6j6smj7qoctu t49;
                gm_b16_8 t50 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
                void * t51 = gm_l0;
                void * t52 = gm_l1;
                gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 0));
                gm_b16_8 t54 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t51, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t54);
                gm_tabb710d9mq2e t55;
                gm_f_format_du48r1i((void *)&t55, (void *)NULL, cx, NULL, &t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcseljft758gtd, 231, 26);
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                gm_b16_8 t58 = t57;
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                gm_b0_1 t60 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t46, t48, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix39;
ie39:;
ix39:;
        }
        {
            void * t61 = gm_l1;
            double t62 = gm_l5;
            gm_teti411eujvf4 t63;
            memset(&t63, 0, sizeof t63);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t63 + 8), t62);
            GM_ST(gm_teti411eujvf4, (((uint8_t *)t61) + 8), t63);
        }
        goto ix2;
ie2:;
        {
            {
                void * t66 = gm_l1;
                gm_teti411eujvf4 t67;
                memset(&t67, 0, sizeof t67);
                GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t67 + 8), ((double)0e0));
                GM_ST(gm_teti411eujvf4, (((uint8_t *)t66) + 8), t67);
            }
            {
                gm_b0_1 t68;
                uint8_t t69 = 0;
                F.ret = t68;
                goto gm_fret;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fin_pat_92m9fnd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fin_pat_92m9fnd F;
    void * gm_l1; /* p */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t *t3 = (((uint8_t *)t2) + 0);
        void * t4 = F.l0;
        void * t5 = (void *)(((uint8_t *)t4) + 96);
        void * t6 = F.l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = gm_l1;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + 0));
        gm_t88rcucg1b7uk t10 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t5, t7, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_t88rcucg1b7uk, t3, t10);
    }
    {
        void * t11 = gm_l1;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
        if (t14 != ((uint8_t)3ULL)) goto mn13;
        gm_tbci5ospk5nnn t15 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t11) + 8) + 8) + 0));
        F.l2 = t15;
        gm_tbci5ospk5nnn t16 = GM_LD(gm_tbci5ospk5nnn, (((((uint8_t *)t11) + 8) + 8) + 48));
        F.l3 = t16;
        {
            {
                memcpy((uint8_t *)&F.l4 + 0, ((uint8_t *)&F.l2), 48);
                memcpy((uint8_t *)&F.l4 + 48, ((uint8_t *)&F.l3), 48);
            }
            void * t19 = ((uint8_t *)&F.l4);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcseljft758gtd, 209, 30);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t19 + (uintptr_t)0 * 48)));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t21 = t20;
            gm_l5 = t21;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l6;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tbci5ospk5nnn t33 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t29 * 48));
                    F.l7 = t33;
                    {
                        void * t36 = F.l0;
                        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
                        void * t38 = (void *)(((uint8_t *)t37) + 8);
                        void * t39 = gm_l1;
                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
                        uint8_t t41 = gm_f_is_integer_8d4f9ek(t38, GM_CONV(gm_csguankko4lmhd, t40));
                        if (!t41) goto ie35;
                        void * t42 = F.l0;
                        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                        void * t44 = gm_l1;
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + 0));
                        uint8_t t46 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t43, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), t45);
                        if (gm_unw.kind) {
                            goto gm_ll26;
                        }
                        uint8_t t47 = (uint8_t)!t46;
                        if (!t47) goto ie35;
                        {
                            void * t49 = F.l0;
                            void * t50 = gm_l1;
                            gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t50) + 120));
                            gm_t6j6smj7qoctu t52;
                            gm_b16_8 t53 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
                            void * t54 = F.l0;
                            void * t55 = gm_l1;
                            gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t55) + 0));
                            gm_b16_8 t57 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t54, t56);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t57);
                            gm_tabb710d9mq2e t58;
                            gm_f_format_qm09p7m((void *)&t58, (void *)NULL, cx, NULL, &t52);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            gm_tabb710d9mq2e t59 = t58;
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_srcseljft758gtd, 211, 34);
                            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                            gm_b16_8 t61 = t60;
                            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                            gm_b0_1 t63 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t49, t51, t62);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    goto me31;
mn32:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 209, 30);
me31:;
                }
lc22:;
                {
                    uint64_t t65 = gm_l6;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l6 = t68;
                }
            }
gm_ll26:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
            }
            goto gm_lad;
lb22:;
        }
        goto me12;
mn13:;
        void * t70 = gm_l1;
        gm_closure t71 = ((gm_closure){(void *)gm_f_fn_513k31a, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t70, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me12;
mn69:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 207, 5);
me12:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_finalize_k1g9q48(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_finalize_k1g9q48 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_thsh8t5u55gps gm_l5; /* p */
    gm_t88rcucg1b7uk gm_l6; /* src */
    gm_t88rcucg1b7uk gm_l7; /* core */
    gm_b16_8 gm_l8; /* kind */
    gm_b16_8 gm_l9; /* hint */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* what */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1656u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l2 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 280) + 0));
            gm_l3 = t7;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l4;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l4;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_thsh8t5u55gps t19 = GM_LD(gm_thsh8t5u55gps, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 20));
                    gm_l5 = t19;
                    {
                        void * t21 = gm_l0;
                        void * t22 = (void *)(((uint8_t *)t21) + 96);
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 0));
                        gm_t88rcucg1b7uk t26 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t22, t24, t25);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_l6 = t26;
                    }
                    {
                        void * t28 = gm_l2;
                        gm_t88rcucg1b7uk t29 = gm_l6;
                        gm_t53at80pfdus3 t30;
                        gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
                        gm_t53at80pfdus3 t31 = t30;
                        uint8_t t32;
                        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                        if (t35 != ((uint8_t)23ULL)) goto mn34;
                        t32 = ((uint8_t)0);
                        goto me33;
mn34:;
                        t32 = ((uint8_t)1);
                        goto me33;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 11, 12);
me33:;
                        if (!t32) goto ie27;
                        {
                            goto lc8;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    {
                        void * t38 = gm_l2;
                        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                        gm_t88rcucg1b7uk t40 = GM_CONV(gm_t88rcucg1b7uk, gm_f_literal_core_ii46ie6(t38, GM_CONV(gm_csguankko4lmhd, t39)));
                        gm_l7 = t40;
                    }
                    {
                        void * t42 = gm_l0;
                        void * t43 = (void *)(((uint8_t *)t42) + 96);
                        void * t44 = gm_l2;
                        gm_t88rcucg1b7uk t45 = gm_l6;
                        gm_t88rcucg1b7uk t46 = gm_l7;
                        gm_t9qkh0rsooc8d t47 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t43, t44, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_t9qkh0rsooc8d t48 = t47;
                        uint8_t t49;
                        if (!((((uint8_t *)&t48))[0] != 0)) goto mn51;
                        t49 = ((uint8_t)1);
                        goto me50;
mn51:;
                        t49 = ((uint8_t)0);
                        goto me50;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 13, 12);
me50:;
                        if (!t49) goto ie41;
                        void * t53 = gm_l0;
                        void * t54 = (void *)(((uint8_t *)t53) + 96);
                        void * t55 = gm_l2;
                        gm_t88rcucg1b7uk t56 = gm_l6;
                        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                        gm_t9qkh0rsooc8d t58 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t54, t55, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        gm_t9qkh0rsooc8d t59 = t58;
                        uint8_t t60;
                        if (!((((uint8_t *)&t59))[0] != 0)) goto mn62;
                        t60 = ((uint8_t)1);
                        goto me61;
mn62:;
                        t60 = ((uint8_t)0);
                        goto me61;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 13, 53);
me61:;
                        if (!t60) goto ie41;
                        {
                            void * t65 = gm_l0;
                            void * t66 = (void *)(((uint8_t *)t65) + 96);
                            void * t67 = gm_l2;
                            gm_t88rcucg1b7uk t68 = gm_l6;
                            gm_taari1s0m51a7 t69 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t66, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            gm_taari1s0m51a7 t70 = t69;
                            gm_b16_8 t71;
                            if (!((((uint8_t *)&t70))[1] != 0)) goto mn73;
                            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
                            if (t74 != ((uint8_t)1ULL)) goto mn73;
                            gm_b16_8 t75;
                            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sq89uknu8mulu));
                            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(7));
                            t71 = t75;
                            goto me72;
mn73:;
                            if (!((((uint8_t *)&t70))[1] != 0)) goto mn76;
                            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t70) + 0));
                            if (t77 != ((uint8_t)2ULL)) goto mn76;
                            gm_b16_8 t78;
                            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sf44a6nc03ff5));
                            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(9));
                            t71 = t78;
                            goto me72;
mn76:;
                            gm_b16_8 t80;
                            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_siflgunfq3r7b));
                            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(10));
                            t71 = t80;
                            goto me72;
mn79:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 14, 21);
me72:;
                            gm_l8 = t71;
                        }
                        {
                            gm_b16_8 t81;
                            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(0));
                            gm_l9 = t81;
                        }
                        {
                            void * t83 = gm_l2;
                            gm_t88rcucg1b7uk t84 = gm_l7;
                            gm_t53at80pfdus3 t85;
                            gm_f_kind_jcnt4hm((void *)&t85, t83, GM_CONV(gm_csguankko4lmhd, t84));
                            gm_t53at80pfdus3 t86 = t85;
                            uint8_t t87;
                            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
                            if (t90 != ((uint8_t)19ULL)) goto mn89;
                            t87 = ((uint8_t)1);
                            goto me88;
mn89:;
                            t87 = ((uint8_t)0);
                            goto me88;
mn91:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 20, 16);
me88:;
                            if (!t87) goto ie82;
                            {
                                gm_b16_8 t93;
                                uint8_t t95 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sq89uknu8mulu, 7) == 0));
                                if (!t95) goto ie94;
                                gm_b16_8 t97;
                                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s7s25m3nlrif2));
                                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(5));
                                gm_b16_8 t98 = t97;
                                t93 = t98;
                                goto ix94;
ie94:;
                                gm_b16_8 t100;
                                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_snoj0t5lf2lht));
                                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(3));
                                gm_b16_8 t101 = t100;
                                t93 = t101;
ix94:;
                                gm_l10 = t93;
                            }
                            {
                                gm_tfeab279c16rr t102;
                                void * t103 = gm_l0;
                                gm_t88rcucg1b7uk t104 = gm_l7;
                                gm_b16_8 t105 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t103, t104);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t105);
                                gm_b16_8 t106 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t106);
                                void * t107 = gm_l0;
                                gm_t88rcucg1b7uk t108 = gm_l7;
                                gm_b16_8 t109 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t107, t108);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t102 + 32, t109);
                                gm_tabb710d9mq2e t110;
                                gm_f_format_o9tvoie((void *)&t110, (void *)NULL, cx, NULL, &t102);
                                if (gm_unw.kind) {
                                    goto gm_ll12;
                                }
                                gm_tabb710d9mq2e t111 = t110;
                                if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcseljft758gtd, 22, 24);
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                                gm_b16_8 t113 = t112;
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                                gm_l9 = t114;
                            }
                            goto ix82;
ie82:;
ix82:;
                        }
                        {
                            gm_b16_8 t115;
                            uint8_t t117 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l8)), gm_sf44a6nc03ff5, 9) == 0));
                            if (!t117) goto ie116;
                            gm_b16_8 t119;
                            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_svklio0qebpog));
                            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(8));
                            gm_b16_8 t120 = t119;
                            t115 = t120;
                            goto ix116;
ie116:;
                            gm_b16_8 t122;
                            GM_ST(void *, (uint8_t *)&t122, (void *)(gm_s4lo69slu4igt));
                            GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(7));
                            gm_b16_8 t123 = t122;
                            t115 = t123;
ix116:;
                            gm_l11 = t115;
                        }
                        {
                            void * t124 = gm_l0;
                            gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 8));
                            gm_t0uuhaljsf410 t126;
                            void * t127 = gm_l0;
                            gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                            gm_b16_8 t129 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t127, t128);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t129);
                            gm_b16_8 t130 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 16, t130);
                            gm_b16_8 t131 = gm_l11;
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 32, t131);
                            gm_b16_8 t132 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 48, t132);
                            gm_tabb710d9mq2e t133;
                            gm_f_format_83ij19e((void *)&t133, (void *)NULL, cx, NULL, &t126);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                            gm_tabb710d9mq2e t134 = t133;
                            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_srcseljft758gtd, 25, 30);
                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                            gm_b16_8 t136 = t135;
                            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                            gm_b0_1 t138 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t124, t125, t137);
                            if (gm_unw.kind) {
                                goto gm_ll12;
                            }
                        }
                        {
                            void * t139 = gm_l0;
                            void * t140 = (void *)(((uint8_t *)t139) + 360);
                            gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 8));
                            gm_tftb1odc739t5 t142 = gm_f_push_0mk7udh(t140, GM_CONV(gm_cseahvg8v7p0vs, t141));
                            gm_tftb1odc739t5 t143 = t142;
                            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcseljft758gtd, 26, 13);
                            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                        }
                        goto ix41;
ie41:;
ix41:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 9, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t146 = gm_l4;
                    uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                    uint64_t t149 = r147;
                    gm_l4 = t149;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
        }
    }
    {
        void * t150 = gm_l0;
        void * t151 = (void *)(((uint8_t *)t150) + 280);
        gm_f_clear_p214f4b(t151);
    }
    {
        void * t152 = gm_l0;
        void * t153 = (void *)(((uint8_t *)t152) + 96);
        void * t154 = gm_l2;
        gm_b0_1 t155 = gm_f_apply_fallbacks_gcbql0t((void *)NULL, cx, NULL, t153, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t157 = gm_l0;
            gm_b16_8 t158 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 320) + 0));
            gm_l12 = t158;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t160 = gm_l13;
                uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t162 = (t160 < t161);
                if (!t162) goto lb159;
                {
                    uint64_t t165 = gm_l13;
                    uint64_t t166 = (uintptr_t)t165;
                    uint64_t t167 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_tpm220j5qhjke t170 = GM_LD(gm_tpm220j5qhjke, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t166 * 48));
                    F.l14 = t170;
                    {
                        void * t172 = gm_l0;
                        gm_b0_1 t173 = gm_f_check_obligation_ss66lhk((void *)NULL, cx, NULL, t172, &(*(gm_tpm220j5qhjke *)((uint8_t *)&F.l14)));
                        if (gm_unw.kind) {
                            goto gm_ll163;
                        }
                    }
                    goto me168;
mn169:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 31, 5);
me168:;
                }
lc159:;
                {
                    uint64_t t175 = gm_l13;
                    uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                    uint64_t t178 = r176;
                    gm_l13 = t178;
                }
            }
gm_ll163:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb159; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc159; }
            }
            goto gm_lad;
lb159:;
        }
    }
    {
        void * t179 = gm_l0;
        void * t180 = (void *)(((uint8_t *)t179) + 320);
        gm_f_clear_a02qt6l(t180);
    }
    {
        void * t181 = gm_l0;
        void * t182 = gm_l1;
        gm_b0_1 t183 = gm_f_fin_expr_rk98p2r((void *)NULL, cx, NULL, t181, t182);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2v5b782(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_513k31a(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ud6f4hb(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tf38rotgtqq79 gm_f_int_range_jdeefnh(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_range_jdeefnh F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* s */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t15;
        uint16_t t16 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
        gm_l3 = t16;
        uint8_t t17 = gm_l2;
        uint16_t t18 = gm_l3;
        uint32_t t19 = (uint32_t)t18;
        uint16_t t20 = gm_l3;
        uint32_t t21 = (uint32_t)t20;
        gm_tba3spc7ecv13 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), t17);
        GM_ST(uint32_t, ((uint8_t *)&t22 + 4), t19);
        GM_ST(uint32_t, ((uint8_t *)&t22 + 8), t21);
        gm_tf38rotgtqq79 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 14, 0);
        GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t23), t22);
        ((uint8_t *)&t23)[12] = 1;
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me6;
mn13:;
ma8:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        gm_tba3spc7ecv13 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0));
        GM_ST(uint32_t, ((uint8_t *)&t27 + 4), ((uint32_t)32ULL));
        GM_ST(uint32_t, ((uint8_t *)&t27 + 8), ((uint32_t)64ULL));
        gm_tf38rotgtqq79 t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 14, 0);
        GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t28), t27);
        ((uint8_t *)&t28)[12] = 1;
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me6;
mn25:;
ma9:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t31 != ((uint8_t)2ULL)) goto mn30;
        gm_tba3spc7ecv13 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)1));
        GM_ST(uint32_t, ((uint8_t *)&t32 + 4), ((uint32_t)32ULL));
        GM_ST(uint32_t, ((uint8_t *)&t32 + 8), ((uint32_t)64ULL));
        gm_tf38rotgtqq79 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 14, 0);
        GM_ST(gm_tba3spc7ecv13, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[12] = 1;
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me6;
mn30:;
ma10:;
        gm_tf38rotgtqq79 t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me6;
mn35:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 278, 5);
me6:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_literal_core_ii46ie6(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_literal_core_ii46ie6 F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* target */
    gm_t88rcucg1b7uk gm_l2; /* core */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_t88rcucg1b7uk gm_l4; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                gm_t88rcucg1b7uk t7 = gm_l2;
                gm_t53at80pfdus3 t8;
                gm_f_kind_jcnt4hm((void *)&t8, t6, GM_CONV(gm_csguankko4lmhd, t7));
                gm_t53at80pfdus3 t9 = t8;
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
                switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma11:;
                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
                if (t17 != ((uint8_t)13ULL)) goto mn16;
                gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t9) + 8));
                gm_l3 = t18;
                {
                    gm_t88rcucg1b7uk t20 = gm_l3;
                    gm_l2 = t20;
                }
                goto me10;
mn16:;
ma12:;
                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
                if (t22 != ((uint8_t)14ULL)) goto mn21;
                gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t9) + 8) + 4));
                gm_l4 = t23;
                {
                    gm_t88rcucg1b7uk t25 = gm_l4;
                    gm_l2 = t25;
                }
                goto me10;
mn21:;
ma13:;
                gm_t88rcucg1b7uk t27 = gm_l2;
                uint8_t t28 = 0;
                F.ret = t27;
                goto gm_fret;
                goto me10;
mn26:;
ma14:;
                gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 246, 9);
me10:;
            }
lc3:;
        }
lb3:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lossless_a54o69s F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* from */
    gm_t88rcucg1b7uk gm_l2; /* to */
    void * gm_l3; /* tys */
    uint16_t gm_l4; /* b */
    gm_tba3spc7ecv13 gm_l5; /* r */
    uint32_t gm_l6; /* needed */
    uint16_t gm_l7; /* a */
    uint16_t gm_l8; /* b */
    gm_tba3spc7ecv13 gm_l9; /* f */
    gm_tba3spc7ecv13 gm_l10; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 963u);
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
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t88rcucg1b7uk t7 = t5;
        gm_t88rcucg1b7uk t8 = t6;
        uint8_t t9 = (gm_eq9m6mvtmgde(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie4;
        {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t13 = gm_l3;
        gm_t88rcucg1b7uk t14 = gm_l1;
        gm_t53at80pfdus3 t15;
        gm_f_kind_jcnt4hm((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        gm_t53at80pfdus3 t16 = t15;
        uint8_t t17 = ((GM_LD(uint8_t, ((uint8_t *)&t16) + 0) == ((uint8_t)4ULL)));
        if (!t17) goto ie12;
        {
            void * t19 = gm_l3;
            gm_t88rcucg1b7uk t20 = gm_l2;
            uint8_t t21 = gm_f_is_integer_8d4f9ek(t19, GM_CONV(gm_csguankko4lmhd, t20));
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l3;
        gm_t88rcucg1b7uk t25 = gm_l2;
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        gm_t53at80pfdus3 t27 = t26;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t28 != ((uint8_t)3ULL)) goto ie23;
        uint16_t t29 = GM_LD(uint16_t, (((uint8_t *)&t27) + 8));
        gm_l4 = t29;
        void * t30 = gm_l3;
        gm_t88rcucg1b7uk t31 = gm_l1;
        uint8_t t32 = gm_f_is_integer_8d4f9ek(t30, GM_CONV(gm_csguankko4lmhd, t31));
        if (!t32) goto ie23;
        {
            void * t34 = gm_l3;
            gm_t88rcucg1b7uk t35 = gm_l1;
            gm_tf38rotgtqq79 t36 = gm_f_int_range_jdeefnh(t34, GM_CONV(gm_csguankko4lmhd, t35));
            gm_tf38rotgtqq79 t37 = t36;
            gm_tba3spc7ecv13 t38;
            if (((((uint8_t *)&t37))[12] != 0)) {
                gm_tba3spc7ecv13 t39 = GM_LD(gm_tba3spc7ecv13, (((uint8_t *)&t37) + 0));
                t38 = t39;
            } else {
                uint8_t t40 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l5 = t38;
        }
        {
            uint32_t t41;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (!t43) goto ie42;
            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            uint32_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint32_t)1ULL), &r46);
            if (o47) gm_trap("integer overflow in `-`", gm_srcseljft758gtd, 262, 33);
            uint32_t t48 = r46;
            uint32_t t49 = t48;
            t41 = t49;
            goto ix42;
ie42:;
            uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            uint32_t t52 = t51;
            t41 = t52;
ix42:;
            gm_l6 = t41;
        }
        {
            uint32_t t53 = gm_l6;
            uint32_t t54;
            uint16_t t56 = gm_l4;
            uint8_t t57 = ((t56 == ((uint16_t)16ULL)));
            if (!t57) goto ie55;
            uint32_t t59 = ((uint32_t)11ULL);
            t54 = t59;
            goto ix55;
ie55:;
            uint32_t t60;
            uint16_t t62 = gm_l4;
            uint8_t t63 = ((t62 == ((uint16_t)32ULL)));
            if (!t63) goto ie61;
            uint32_t t65 = ((uint32_t)24ULL);
            t60 = t65;
            goto ix61;
ie61:;
            uint32_t t67 = ((uint32_t)53ULL);
            t60 = t67;
ix61:;
            t54 = t60;
ix55:;
            uint8_t t68 = (t53 <= t54);
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t71 = gm_l3;
        gm_t88rcucg1b7uk t72 = gm_l1;
        gm_t53at80pfdus3 t73;
        gm_f_kind_jcnt4hm((void *)&t73, t71, GM_CONV(gm_csguankko4lmhd, t72));
        gm_t53at80pfdus3 t74 = t73;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t74) + 0));
        if (t75 != ((uint8_t)3ULL)) goto ie70;
        uint16_t t76 = GM_LD(uint16_t, (((uint8_t *)&t74) + 8));
        gm_l7 = t76;
        void * t77 = gm_l3;
        gm_t88rcucg1b7uk t78 = gm_l2;
        gm_t53at80pfdus3 t79;
        gm_f_kind_jcnt4hm((void *)&t79, t77, GM_CONV(gm_csguankko4lmhd, t78));
        gm_t53at80pfdus3 t80 = t79;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t80) + 0));
        if (t81 != ((uint8_t)3ULL)) goto ie70;
        uint16_t t82 = GM_LD(uint16_t, (((uint8_t *)&t80) + 8));
        gm_l8 = t82;
        {
            uint16_t t84 = gm_l8;
            uint16_t t85 = gm_l7;
            uint8_t t86 = (t84 > t85);
            uint8_t t87 = 0;
            F.ret = t86;
            goto gm_fret;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        void * t89 = gm_l3;
        gm_t88rcucg1b7uk t90 = gm_l1;
        uint8_t t91 = gm_f_is_integer_8d4f9ek(t89, GM_CONV(gm_csguankko4lmhd, t90));
        if (!t91) goto ie88;
        void * t92 = gm_l3;
        gm_t88rcucg1b7uk t93 = gm_l2;
        uint8_t t94 = gm_f_is_integer_8d4f9ek(t92, GM_CONV(gm_csguankko4lmhd, t93));
        if (!t94) goto ie88;
        {
            void * t96 = gm_l3;
            gm_t88rcucg1b7uk t97 = gm_l1;
            gm_tf38rotgtqq79 t98 = gm_f_int_range_jdeefnh(t96, GM_CONV(gm_csguankko4lmhd, t97));
            gm_tf38rotgtqq79 t99 = t98;
            gm_tba3spc7ecv13 t100;
            if (((((uint8_t *)&t99))[12] != 0)) {
                gm_tba3spc7ecv13 t101 = GM_LD(gm_tba3spc7ecv13, (((uint8_t *)&t99) + 0));
                t100 = t101;
            } else {
                uint8_t t102 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l9 = t100;
        }
        {
            void * t103 = gm_l3;
            gm_t88rcucg1b7uk t104 = gm_l2;
            gm_tf38rotgtqq79 t105 = gm_f_int_range_jdeefnh(t103, GM_CONV(gm_csguankko4lmhd, t104));
            gm_tf38rotgtqq79 t106 = t105;
            gm_tba3spc7ecv13 t107;
            if (((((uint8_t *)&t106))[12] != 0)) {
                gm_tba3spc7ecv13 t108 = GM_LD(gm_tba3spc7ecv13, (((uint8_t *)&t106) + 0));
                t107 = t108;
            } else {
                uint8_t t109 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l10 = t107;
        }
        {
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t113 = ((t111 == t112));
            if (!t113) goto ie110;
            {
                uint32_t t115 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t117 = (t115 >= t116);
                uint8_t t118 = 0;
                F.ret = t117;
                goto gm_fret;
            }
            goto ix110;
ie110:;
ix110:;
        }
        {
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t121 = (uint8_t)!t120;
            if (!t121) goto ie119;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            if (!t122) goto ie119;
            {
                uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 4));
                uint32_t t125 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t126 = (t124 > t125);
                uint8_t t127 = 0;
                F.ret = t126;
                goto gm_fret;
            }
            goto ix119;
ie119:;
ix119:;
        }
        {
            uint8_t t128 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        uint8_t t129 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_numeric_uv79l0u(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_settle_h9ikvs5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_thsh8t5u55gps gm_l5; /* p */
    gm_t88rcucg1b7uk gm_l8; /* core */
    GM_STACK_CHECK(&F, 629u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 280) + 0));
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
                    gm_thsh8t5u55gps t16 = GM_LD(gm_thsh8t5u55gps, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 20));
                    gm_l5 = t16;
                    {
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)t18) + 96);
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 0));
                        gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t19, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        F.l6 = t23;
                    }
                    {
                        void * t25 = gm_l0;
                        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                        void * t27 = (void *)(((uint8_t *)t26) + 8);
                        gm_t88rcucg1b7uk t28 = F.l6;
                        gm_t53at80pfdus3 t29;
                        gm_f_kind_jcnt4hm((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
                        gm_t53at80pfdus3 t30 = t29;
                        uint8_t t31;
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
                        if (t34 != ((uint8_t)23ULL)) goto mn33;
                        t31 = ((uint8_t)0);
                        goto me32;
mn33:;
                        t31 = ((uint8_t)1);
                        goto me32;
mn35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 40, 12);
me32:;
                        if (!t31) goto ie24;
                        {
                            goto lc5;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        F.l7 = ((uint8_t)0);
                    }
                    {
                        void * t37 = gm_l0;
                        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
                        void * t39 = gm_l0;
                        void * t40 = (void *)(((uint8_t *)t39) + 96);
                        void * t41 = gm_l0;
                        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
                        gm_t88rcucg1b7uk t43 = gm_l1;
                        gm_t88rcucg1b7uk t44 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t40, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_closure t45 = ((gm_closure){(void *)gm_f_fn_2v5b782, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t38, GM_CONV(gm_csguankko4lmhd, t44), t45);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        uint8_t t47 = F.l7;
                        if (!t47) goto ie46;
                        {
                            void * t49 = gm_l0;
                            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                            void * t51 = (void *)(((uint8_t *)t50) + 8);
                            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                            gm_t88rcucg1b7uk t53 = GM_CONV(gm_t88rcucg1b7uk, gm_f_literal_core_ii46ie6(t51, GM_CONV(gm_csguankko4lmhd, t52)));
                            gm_l8 = t53;
                        }
                        {
                            void * t55 = gm_l0;
                            void * t56 = (void *)(((uint8_t *)t55) + 96);
                            void * t57 = gm_l0;
                            void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                            void * t59 = (void *)(((uint8_t *)t58) + 8);
                            gm_t88rcucg1b7uk t60 = F.l6;
                            gm_t88rcucg1b7uk t61 = gm_l8;
                            gm_t9qkh0rsooc8d t62 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t56, t59, t60, t61);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_t9qkh0rsooc8d t63 = t62;
                            uint8_t t64;
                            if (!((((uint8_t *)&t63))[0] != 0)) goto mn66;
                            t64 = ((uint8_t)1);
                            goto me65;
mn66:;
                            t64 = ((uint8_t)0);
                            goto me65;
mn67:;
                            gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 47, 16);
me65:;
                            if (!t64) goto ie54;
                            {
                                {
                                    void * t70 = gm_l0;
                                    void * t71 = (void *)(((uint8_t *)t70) + 96);
                                    void * t72 = gm_l0;
                                    void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                                    void * t74 = (void *)(((uint8_t *)t73) + 8);
                                    gm_t88rcucg1b7uk t75 = F.l6;
                                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 4));
                                    gm_t9qkh0rsooc8d t77 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t71, t74, t75, t76);
                                    if (gm_unw.kind) {
                                        goto gm_ll9;
                                    }
                                }
                            }
                            goto ix54;
ie54:;
ix54:;
                        }
                        goto ix46;
ie46:;
ix46:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcseljft758gtd, 38, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t79 = gm_l4;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l4 = t82;
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
        void * t83 = gm_l0;
        void * t84 = (void *)(((uint8_t *)t83) + 96);
        void * t85 = gm_l0;
        void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
        gm_t88rcucg1b7uk t87 = gm_l1;
        gm_tl874t2atnlh3 t88 = gm_l2;
        gm_t88rcucg1b7uk t89 = gm_f_settle_vars_vpp9ov6((void *)NULL, cx, NULL, t84, t86, t87, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = 0;
        F.ret = t89;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
