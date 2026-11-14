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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_teqsi7sd08g9f;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; } gm_to3r6jqo24gs1;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t08olq97rscn3 v0; unsigned __int128 v1; } f1; } gm_t3d480iurj7vi;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t4rau9abddl2u;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t9t4u07j77scv;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_teqsi7sd08g9f) == 48 && _Alignof(gm_teqsi7sd08g9f) == 8 && offsetof(gm_teqsi7sd08g9f, f0) == 0 && offsetof(gm_teqsi7sd08g9f, f1) == 16 && offsetof(gm_teqsi7sd08g9f, f2) == 32 && offsetof(gm_teqsi7sd08g9f, f3) == 40, "layout of `(string, string, u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_to3r6jqo24gs1) == 32 && _Alignof(gm_to3r6jqo24gs1) == 8 && offsetof(gm_to3r6jqo24gs1, f0) == 0 && offsetof(gm_to3r6jqo24gs1, f1) == 8 && offsetof(gm_to3r6jqo24gs1, f2) == 16, "layout of `StaticCell`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_tob1snuii5bme v1; } f1; } gm_tt9fudfifskca;
_Static_assert(sizeof(gm_t3d480iurj7vi) == 48 && _Alignof(gm_t3d480iurj7vi) == 16 && offsetof(gm_t3d480iurj7vi, f0) == 0 && offsetof(gm_t3d480iurj7vi, f1) == 16, "layout of `Unsup!u128`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4rau9abddl2u) == 104 && _Alignof(gm_t4rau9abddl2u) == 8 && offsetof(gm_t4rau9abddl2u, f0) == 0 && offsetof(gm_t4rau9abddl2u, f1) == 8 && offsetof(gm_t4rau9abddl2u, f2) == 48 && offsetof(gm_t4rau9abddl2u, f3) == 88, "layout of `SiteConst`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint64_t f0; gm_tq7qjpkmqmc5s f1; } gm_t964gsbl2irde;
_Static_assert(sizeof(gm_t9t4u07j77scv) == 40 && _Alignof(gm_t9t4u07j77scv) == 8 && offsetof(gm_t9t4u07j77scv, f0) == 0 && offsetof(gm_t9t4u07j77scv, f1) == 8, "layout of `Unsup!ConstsText`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tt9fudfifskca) == 40 && _Alignof(gm_tt9fudfifskca) == 8 && offsetof(gm_tt9fudfifskca, f0) == 0 && offsetof(gm_tt9fudfifskca, f1) == 8, "layout of `Unsup!Reloc`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t964gsbl2irde) == 112 && _Alignof(gm_t964gsbl2irde) == 8 && offsetof(gm_t964gsbl2irde, f0) == 0 && offsetof(gm_t964gsbl2irde, f1) == 8, "layout of `(i: usize, x: GlobalInfo)`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_adt_mpqv8qe {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_bits_of_ilgn83m {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l17; /* x */
    gm_t3d480iurj7vi ret;
};
struct gm_fr_cell_storage_1rtp280 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cell_type_0jgf3ue {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_const_storage_0d7nj5d {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_empty_slice_8rccll4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_al38tsc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_gen_consts_6ibghrc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_trqfvmjtt4sij l2; /* init */
    gm_trqfvmjtt4sij l8; /* bytes */
    gm_trqfvmjtt4sij l11; /* relocs */
    gm_tq7qjpkmqmc5s l19; /* gl */
    gm_trqfvmjtt4sij l25; /* tls_init */
    gm_trqfvmjtt4sij l28; /* bytes */
    gm_trqfvmjtt4sij l31; /* relocs */
    gm_trqfvmjtt4sij l39; /* bytes */
    gm_trqfvmjtt4sij l42; /* relocs */
    gm_t4rau9abddl2u l49; /* sc */
    gm_t9t4u07j77scv ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_list_opmir2c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_reloc_blfij5d {
    gm_frame h;
    gm_tpautr4br7h3l l16; /* sd */
    gm_tt9fudfifskca ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_dffnss0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vgtfcjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jc1rn8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ng75pjh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
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
struct gm_fr_store_int_mfkaqk5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_value_bytes_qm8egmj {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tckdobunaulee l27; /* vd */
    gm_t6evimt84lndq l38; /* x */
    gm_t6evimt84lndq l44; /* x */
    gm_tpautr4br7h3l l46; /* sd */
    gm_t6evimt84lndq l53; /* x */
    gm_tqgtd5gtgj65s ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_format_0o96lei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_5ktivom(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_global_tag_gs7s3p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_key_m4hm4f5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dffnss0(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgtfcjf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_uldb5pe(void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jc1rn8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ng75pjh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_60kg76i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dhp21l9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lket48d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qbs7gku(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_snsbmln(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t09mk5p(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_th5g0ba(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9o0juu(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vlrefbu(void * a0, const void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s2uimijqjekbd[67];
extern const uint8_t gm_s31vj4bjbj0gj[36];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sb8jvk2vbau9p[25];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sbe61ep7tock5[5];
extern const uint8_t gm_sbe7f0bkvca75[43];
extern const uint8_t gm_scc54gv4a7k04[6];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sg1uf2uigo01q[27];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_skff6pclobvnu[40];
extern const uint8_t gm_slpqve1dqssq0[8];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcuro7l7pd15c2[];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mpqv8qe F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 135, 63);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t3 * 184);
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_u86m8ir F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8rccll4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_al38tsc F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
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
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 82, 91);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b16_8 t9 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_opmir2c F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8rccll4();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dffnss0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_dffnss0 F;
    void * gm_l0; /* l */
    gm_to3r6jqo24gs1 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_al38tsc();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jc1rn8(t10, ((uint64_t)1ULL), t12);
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
        gm_to3r6jqo24gs1 t21 = GM_LD(gm_to3r6jqo24gs1, ((uint8_t *)a1));
        GM_ST(gm_to3r6jqo24gs1, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgtfcjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vgtfcjf F;
    void * gm_l0; /* l */
    gm_tob1snuii5bme gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_8rccll4();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ng75pjh(t10, ((uint64_t)1ULL), t12);
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
        gm_tob1snuii5bme t21 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)a1));
        GM_ST(gm_tob1snuii5bme, ((uint8_t *)t20 + t18 * 32), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jc1rn8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jc1rn8 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ng75pjh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ng75pjh F;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_bits_of_ilgn83m F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* b */
    unsigned __int128 gm_l4; /* bits */
    uint32_t gm_l5; /* w */
    uint32_t gm_l6; /* index */
    uint32_t gm_l7; /* id */
    gm_t88rcucg1b7uk gm_l8; /* tag_ty */
    gm_b16_8 gm_l9; /* variants */
    __int128 gm_l10; /* tag */
    void * gm_l11; /* a */
    gm_tcmkompbl09il gm_l12; /* bl */
    unsigned __int128 gm_l13; /* raw */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    gm_tov4mtrfp5e7v gm_l18; /* f */
    GM_STACK_CHECK(&F, 1738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t11 != ((uint8_t)2ULL)) goto mn10;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l3 = t12;
        gm_t3d480iurj7vi t13;
        uint8_t t15 = gm_l3;
        if (!t15) goto ie14;
        unsigned __int128 t17 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        gm_t3d480iurj7vi t18;
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
        gm_t3d480iurj7vi t21;
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
        uint8_t t22 = ((((uint8_t *)&t13))[0] != 0);
        F.ret = t13;
        goto gm_fret;
        goto me2;
mn10:;
ma4:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        unsigned __int128 t25 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t25;
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            gm_t88rcucg1b7uk t29 = gm_l2;
            uint32_t t30 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t30;
        }
        {
            gm_t3d480iurj7vi t31;
            uint32_t t33 = gm_l5;
            uint8_t t34 = (t33 >= ((uint32_t)128ULL));
            if (!t34) goto ie32;
            unsigned __int128 t36 = gm_l4;
            unsigned __int128 t37 = t36;
            gm_t3d480iurj7vi t38;
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
            unsigned __int128 t40 = gm_l4;
            uint32_t t41 = gm_l5;
            unsigned __int128 t42 = (unsigned __int128)t41;
            unsigned __int128 n43 = (unsigned __int128)t42;
            if (n43 >= 128) gm_trap_shift((uint64_t)n43, "u128", gm_srcuro7l7pd15c2, 323, 57);
            unsigned __int128 r44 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n43);
            bool o45 = (unsigned __int128)(r44 >> (int)n43) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            if (o45) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 323, 57);
            unsigned __int128 t46 = r44;
            unsigned __int128 r47; bool o48 = __builtin_sub_overflow(t46, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 323, 56);
            unsigned __int128 t49 = r47;
            unsigned __int128 t50 = (unsigned __int128)(t40 & t49);
            unsigned __int128 t51 = t50;
            gm_t3d480iurj7vi t52;
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
            uint8_t t53 = ((((uint8_t *)&t31))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto me2;
mn23:;
ma5:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t55 != ((uint8_t)10ULL)) goto mn54;
        uint32_t t56 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l6 = t56;
        {
            void * t59 = gm_l0;
            void * t60 = (void *)(((uint8_t *)t59) + 8);
            gm_t88rcucg1b7uk t61 = gm_l2;
            gm_t53at80pfdus3 t62;
            gm_f_kind_ghvav22((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
            if (t63 != ((uint8_t)17ULL)) goto ie58;
            uint32_t t64 = GM_LD(uint32_t, ((((uint8_t *)&t62) + 8) + 0));
            gm_l7 = t64;
            {
                void * t67 = gm_l0;
                void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                uint32_t t69 = gm_l7;
                void * t70 = gm_f_adt_mpqv8qe(t68, t69);
                uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t70) + 24) + 0));
                if (t71 != ((uint8_t)1ULL)) goto ie66;
                gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t70) + 24) + 8) + 0));
                gm_l8 = t72;
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((((uint8_t *)t70) + 24) + 8) + 8));
                gm_l9 = t73;
                {
                    uint32_t t75 = gm_l6;
                    uint64_t t76 = (uint64_t)t75;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srcuro7l7pd15c2, 328, 35);
                    gm_ta24rd13gupia t79;
                    gm_f_to_i128_o7ud1ue((void *)&t79, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t77 * 72) + 24)));
                    __int128 t80;
                    if (((((uint8_t *)&t79))[16] != 0)) {
                        __int128 t81 = GM_LD(__int128, (((uint8_t *)&t79) + 0));
                        t80 = t81;
                    } else {
                        t80 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                    }
                    gm_l10 = t80;
                }
                {
                    void * t82 = gm_l0;
                    __int128 t83 = gm_l10;
                    unsigned __int128 t84 = (unsigned __int128)t83;
                    gm_t6evimt84lndq t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t85 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t85 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t85 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t85 + 16), t84);
                    gm_t88rcucg1b7uk t86 = gm_l8;
                    gm_f_bits_of_ilgn83m((void *)&(*(gm_t3d480iurj7vi *)gm_out), (void *)NULL, cx, NULL, t82, &t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix66;
ie66:;
                {
                    gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t89;
                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sfali8nl85hc9));
                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t90;
                    gm_f_unsupported_531q51r((void *)&t90, (void *)NULL, cx, NULL, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 8, 0);
                    ((uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 16, t90);
                    goto gm_fdone;
                }
ix66:;
            }
            goto ix58;
ie58:;
            {
                gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t94;
                gm_f_unsupported_531q51r((void *)&t94, (void *)NULL, cx, NULL, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 16, t94);
                goto gm_fdone;
            }
ix58:;
        }
        goto me2;
mn54:;
ma6:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t96 != ((uint8_t)3ULL)) goto mn95;
        void * t97 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l11 = t97;
        {
            void * t99 = gm_l0;
            void * t100 = (void *)(((uint8_t *)t99) + 8);
            gm_t88rcucg1b7uk t101 = gm_l2;
            gm_t57euh612ligr t102;
            gm_f_bits_layout_23rect9((void *)&t102, (void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcmkompbl09il t103;
            if (((((uint8_t *)&t102))[24] != 0)) {
                t103 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t102) + 0));
            } else {
                gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t105;
                GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t106;
                gm_f_unsupported_531q51r((void *)&t106, (void *)NULL, cx, NULL, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 8, 0);
                ((uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 16, t106);
                goto gm_fdone;
            }
            gm_l12 = t103;
        }
        {
            gm_l13 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                void * t108 = gm_l11;
                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)t108) + 0));
                gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                gm_l14 = t110;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t112 = gm_l15;
                    uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t114 = (t112 < t113);
                    if (!t114) goto lb111;
                    {
                        gm_thf8as07kj3uj t117;
                        memset((uint8_t *)&t117 + 8, 0, 8);
                        uint64_t t118 = gm_l15;
                        GM_ST(uint64_t, (uint8_t *)&t117 + 0, t118);
                        uint64_t t119 = gm_l15;
                        uint64_t t120 = (uintptr_t)t119;
                        uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        memcpy((uint8_t *)&t117 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t120 * 48), 48);
                        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&t117) + 0));
                        gm_l16 = t124;
                        F.l17 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t117) + 16));
                        {
                            uint64_t t126 = gm_l16;
                            uint64_t t127 = (uintptr_t)t126;
                            uint64_t t128 = GM_LD(uintptr_t, (((uint8_t *)&gm_l12) + 8) + 8);
                            if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcuro7l7pd15c2, 335, 22);
                            gm_tov4mtrfp5e7v t129 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l12) + 8)) + t127 * 12));
                            gm_l18 = t129;
                        }
                        {
                            unsigned __int128 t130 = gm_l13;
                            void * t131 = gm_l0;
                            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
                            gm_t3d480iurj7vi t133;
                            gm_f_bits_of_ilgn83m((void *)&t133, (void *)NULL, cx, NULL, t131, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l17)), t132);
                            if (gm_unw.kind) {
                                goto gm_ll115;
                            }
                            if (((((uint8_t *)&t133))[0] != 0)) {
                                gm_t3d480iurj7vi t134;
                                GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t134 + 8, 0);
                                ((uint8_t *)&t134)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t134 + 16), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t133) + 16)));
                                F.ret = t134;
                                goto gm_fret;
                            }
                            unsigned __int128 t135 = GM_LD(unsigned __int128, (((uint8_t *)&t133) + 16));
                            uint32_t t136 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 0));
                            unsigned __int128 t137 = (unsigned __int128)t136;
                            unsigned __int128 n138 = (unsigned __int128)t137;
                            if (n138 >= 128) gm_trap_shift((uint64_t)n138, "u128", gm_srcuro7l7pd15c2, 336, 24);
                            unsigned __int128 r139 = (unsigned __int128)((unsigned __int128)t135 << (int)n138);
                            bool o140 = (unsigned __int128)(r139 >> (int)n138) != t135;
                            if (o140) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 336, 24);
                            unsigned __int128 t141 = r139;
                            unsigned __int128 t142 = (unsigned __int128)(t130 | t141);
                            gm_l13 = t142;
                        }
                        goto me122;
mn123:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 334, 13);
me122:;
                    }
lc111:;
                    {
                        uint64_t t144 = gm_l15;
                        uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                        uint64_t t147 = r145;
                        gm_l15 = t147;
                    }
                }
gm_ll115:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb111; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc111; }
                }
                goto gm_lad;
lb111:;
            }
        }
        {
            unsigned __int128 t148 = gm_l13;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 16, t148);
            goto gm_fdone;
        }
        goto me2;
mn95:;
ma7:;
        gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_b16_8 t151;
        GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sfali8nl85hc9));
        GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(24));
        gm_t08olq97rscn3 t152;
        gm_f_unsupported_531q51r((void *)&t152, (void *)NULL, cx, NULL, t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 8, 0);
        ((uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t3d480iurj7vi *)gm_out) + 16, t152);
        goto gm_fdone;
        goto me2;
mn149:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 319, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_cell_storage_1rtp280 F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* key */
    uint64_t gm_l4; /* addr */
    uint32_t gm_l5; /* i */
    uint32_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 3528);
        uint64_t t7 = gm_l4;
        gm_t9805qtonbsag t8 = gm_f_get_5ktivom(t6, t7);
        if (!((((uint8_t *)&t8))[4] != 0)) goto ie4;
        uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&t8));
        gm_l5 = t9;
        {
            uint32_t t11 = gm_l5;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 3488) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_srcuro7l7pd15c2, 41, 43);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 3488) + 0)) + t13 * 32) + 16));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 3488) + 0) + 8);
        uint32_t t20 = (uint32_t)t19;
        gm_l6 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 1272);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sbe61ep7tock5));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
        gm_b16_8 t24 = gm_l3;
        gm_b16_8 t25 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t25;
    }
    {
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 3488);
        gm_to3r6jqo24gs1 t28;
        memset((uint8_t *)&t28 + 12, 0, 4);
        void * t29 = gm_l1;
        GM_ST(void *, (uint8_t *)&t28 + 0, t29);
        gm_t88rcucg1b7uk t30 = gm_l2;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t28 + 8, t30);
        gm_b16_8 t31 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t31);
        gm_tftb1odc739t5 t32 = gm_f_push_dffnss0(t27, &t28);
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcuro7l7pd15c2, 44, 5);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    {
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 3528);
        uint64_t t36 = gm_l4;
        uint32_t t37 = gm_l6;
        gm_tftb1odc739t5 t38 = gm_f_put_uldb5pe(t35, t36, t37);
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcuro7l7pd15c2, 45, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        gm_b16_8 t40 = gm_l7;
        uint8_t t41 = 0;
        F.ret = t40;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cell_type_0jgf3ue F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    uint32_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 3528);
        void * t4 = gm_l1;
        uint64_t t5 = (uintptr_t)t4;
        gm_t9805qtonbsag t6 = gm_f_get_5ktivom(t3, t5);
        uint32_t t7;
        if (((((uint8_t *)&t6))[4] != 0)) {
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_tkn42u7na3t81 t9;
            memset(&t9, 0, sizeof t9);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        void * t11 = gm_l0;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 3488) + 0));
        gm_b16_8 t13 = t12;
        uint32_t t14 = gm_l2;
        uint64_t t15 = (uint64_t)t14;
        uint64_t t16 = (uintptr_t)t15;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_srcuro7l7pd15c2, 305, 12);
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t16 * 32) + 8));
        gm_tkn42u7na3t81 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[4] = 1;
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_storage_0d7nj5d F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* s */
    uint32_t gm_l5; /* id */
    void * gm_l6; /* def */
    void * gm_l7; /* cell */
    GM_STACK_CHECK(&F, 580u);
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
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l4;
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_t88rcucg1b7uk t7 = gm_l2;
        gm_t9805qtonbsag t8 = gm_f_const_id_2mlh0og(t5, GM_CONV(gm_cseahvg8v7p0vs, t6), GM_CONV(gm_csguankko4lmhd, t7));
        uint32_t t9;
        if (((((uint8_t *)&t8))[4] != 0)) {
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_tl874t2atnlh3 t11 = gm_l3;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0ggo8ek7kfrt));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(33));
            gm_t08olq97rscn3 t13;
            gm_f_unsupported_531q51r((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t13);
            goto gm_fdone;
        }
        gm_l5 = t9;
    }
    {
        void * t14 = gm_l4;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 1968) + 0));
        gm_b16_8 t16 = t15;
        uint32_t t17 = gm_l5;
        uint64_t t18 = (uint64_t)t17;
        uint64_t t19 = (uintptr_t)t18;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&t16) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_srcuro7l7pd15c2, 33, 13);
        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t16)) + t19 * 88);
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l6;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 80));
        void * t24;
        if ((GM_LD(void *, ((uint8_t *)&t23)) != 0)) {
            void * t25 = GM_LD(void *, (((uint8_t *)&t23) + 0));
            t24 = t25;
        } else {
            gm_tl874t2atnlh3 t26 = gm_l3;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s3js9g3l46oiv));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(39));
            gm_t08olq97rscn3 t28;
            gm_f_unsupported_531q51r((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        gm_l7 = t24;
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l7;
        gm_t88rcucg1b7uk t31 = gm_l2;
        gm_tfeab279c16rr t32;
        void * t33 = gm_l0;
        void * t34 = gm_l6;
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 32));
        gm_b16_8 t36 = gm_f_module_key_m4hm4f5((void *)NULL, cx, NULL, t33, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 0, t36);
        void * t37 = gm_l6;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 16, t38);
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_t88rcucg1b7uk t41 = gm_l2;
        gm_b16_8 t42 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t32 + 32, t42);
        gm_tabb710d9mq2e t43;
        gm_f_format_17eulka((void *)&t43, (void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcuro7l7pd15c2, 35, 36);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_b16_8 t46 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t29, t30, t31, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t46);
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
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_consts_6ibghrc F;
    void * gm_l0; /* g */
    uint64_t gm_l3; /* i */
    gm_to3r6jqo24gs1 gm_l4; /* sc */
    gm_b16_8 gm_l5; /* name */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* al */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l12; /* sb */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tob1snuii5bme gm_l15; /* r */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* gi */
    gm_b16_8 gm_l20; /* t */
    uint64_t gm_l21; /* size */
    uint64_t gm_l22; /* al */
    gm_b16_8 gm_l23; /* link */
    gm_b16_8 gm_l24; /* tls */
    void * gm_l26; /* target */
    void * gm_l27; /* cell */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l32; /* sb */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_tob1snuii5bme gm_l35; /* r */
    gm_to3r6jqo24gs1 gm_l36; /* sc */
    gm_b16_8 gm_l37; /* name */
    uint64_t gm_l38; /* size */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_b16_8 gm_l43; /* sb */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_tob1snuii5bme gm_l46; /* r */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l50; /* name */
    gm_b16_8 gm_l51; /* sb */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_tf1bjg81u0glv gm_l54; /* r */
    gm_b16_8 gm_l55; /* target */
    GM_STACK_CHECK(&F, 5263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s31vj4bjbj0gj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcuro7l7pd15c2, 53, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb6;
            {
                for (;;) {
                    uint64_t t10 = gm_l3;
                    void * t11 = gm_l0;
                    uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 3488) + 0) + 8);
                    uint8_t t13 = (t10 < t12);
                    if (!t13) goto lb9;
                    {
                        uint64_t t16 = gm_l3;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l0;
                        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 3488) + 0) + 8);
                        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcuro7l7pd15c2, 59, 19);
                        gm_to3r6jqo24gs1 t20 = GM_LD(gm_to3r6jqo24gs1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 3488) + 0)) + t17 * 32));
                        gm_l4 = t20;
                    }
                    {
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                        gm_l5 = t21;
                    }
                    {
                        void * t22 = gm_l0;
                        gm_b16_8 t23 = gm_l5;
                        GM_ST(gm_b16_8, (((uint8_t *)t22) + 3584), t23);
                    }
                    {
                        uint64_t t24 = gm_l3;
                        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 62, 13);
                        uint64_t t27 = r25;
                        gm_l3 = t27;
                    }
                    {
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 8);
                        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t31 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_l6 = t31;
                    }
                    {
                        void * t32 = gm_l0;
                        void * t33 = (void *)(((uint8_t *)t32) + 8);
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t35 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        uint64_t t36 = (t35 > ((uint64_t)1ULL) ? t35 : ((uint64_t)1ULL));
                        gm_l7 = t36;
                    }
                    {
                        void * t37 = (void *)((uint8_t *)&F.l1);
                        gm_tgbgti82mf879 t38;
                        gm_b16_8 t39 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
                        uint64_t t40 = gm_l6;
                        uint64_t t41 = (t40 > ((uint64_t)1ULL) ? t40 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t38 + 16, t41);
                        uint64_t t42 = gm_l7;
                        GM_ST(uint64_t, (uint8_t *)&t38 + 24, t42);
                        gm_tftb1odc739t5 t43 = gm_f_write_60kg76i(t37, &t38);
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcuro7l7pd15c2, 65, 13);
                        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l8, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                    }
                    {
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            uint64_t t46 = gm_l6;
                            gm_l9 = t46;
                        }
                        {
                            for (;;) {
                                uint64_t t48 = gm_l10;
                                uint64_t t49 = gm_l9;
                                uint8_t t50 = (t48 < t49);
                                if (!t50) goto lb47;
                                {
                                    uint64_t t53 = gm_l10;
                                    {
                                        void * t57 = (void *)((uint8_t *)&F.l8);
                                        gm_tftb1odc739t5 t58 = gm_f_push_t83rg2m(t57, ((uint8_t)0ULL));
                                        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcuro7l7pd15c2, 67, 32);
                                        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
                                    }
                                    goto me54;
mn55:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 67, 13);
me54:;
                                }
lc47:;
                                {
                                    uint64_t t61 = gm_l10;
                                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                    uint64_t t64 = r62;
                                    gm_l10 = t64;
                                }
                            }
lb47:;
                        }
                    }
                    {
                        gm_f_list_opmir2c((void *)&F.l11, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                    }
                    {
                        void * t65 = gm_l0;
                        void * t66 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
                        gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                        void * t69 = (void *)((uint8_t *)&F.l11);
                        gm_tqgtd5gtgj65s t70;
                        gm_f_value_bytes_qm8egmj((void *)&t70, (void *)NULL, cx, NULL, t65, &(*(gm_t6evimt84lndq *)(((uint8_t *)t66) + 0)), t67, t68, ((uint64_t)0ULL), t69);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        if (((((uint8_t *)&t70))[0] != 0)) {
                            gm_t9t4u07j77scv t71;
                            GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                            ((uint8_t *)&t71)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t71 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t70) + 8)));
                            F.ret = t71;
                            goto gm_fret;
                        }
                        gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 8));
                    }
                    {
                        uint64_t t74 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                        uint8_t t75 = (t74 > ((uint64_t)0ULL));
                        if (!t75) goto ie73;
                        {
                            void * t77 = gm_l0;
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_b16_8 t80 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t77, t79);
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                            gm_l12 = t80;
                        }
                        {
                            void * t81 = (void *)((uint8_t *)&F.l2);
                            gm_tftksr4op114e t82;
                            gm_b16_8 t83 = gm_l5;
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
                            gm_b16_8 t84 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t84);
                            uint64_t t85 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t82 + 32, t85);
                            gm_tftb1odc739t5 t86 = gm_f_write_snsbmln(t81, &t82);
                            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcuro7l7pd15c2, 72, 17);
                            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                        }
                        goto ix73;
ie73:;
ix73:;
                    }
                    {
                        {
                            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_l13 = t89;
                        }
                        {
                            gm_l14 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t91 = gm_l14;
                                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                uint8_t t93 = (t91 < t92);
                                if (!t93) goto lb90;
                                {
                                    uint64_t t96 = gm_l14;
                                    uint64_t t97 = (uintptr_t)t96;
                                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    gm_tob1snuii5bme t101 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t97 * 32));
                                    gm_l15 = t101;
                                    {
                                        void * t103 = (void *)((uint8_t *)&F.l2);
                                        gm_thddbqb6snabm t104;
                                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                        GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
                                        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 24));
                                        GM_ST(uint64_t, (uint8_t *)&t104 + 16, t106);
                                        gm_b16_8 t107 = gm_l5;
                                        GM_ST(gm_b16_8, (uint8_t *)&t104 + 24, t107);
                                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 0));
                                        GM_ST(uint64_t, (uint8_t *)&t104 + 40, t108);
                                        gm_tftb1odc739t5 t109 = gm_f_write_th5g0ba(t103, &t104);
                                        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srcuro7l7pd15c2, 75, 17);
                                        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                                    }
                                    goto me99;
mn100:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 74, 13);
me99:;
                                }
lc90:;
                                {
                                    uint64_t t112 = gm_l14;
                                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                                    uint64_t t115 = r113;
                                    gm_l14 = t115;
                                }
                            }
lb90:;
                        }
                    }
lc9:;
                }
gm_ll14:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
                }
                goto gm_ll7;
lb9:;
            }
            {
                void * t116 = gm_l0;
                gm_tqgtd5gtgj65s t117;
                gm_f_drain_leoibma((void *)&t117, (void *)NULL, cx, NULL, t116);
                if (gm_unw.kind) {
                    goto gm_ll7;
                }
                if (((((uint8_t *)&t117))[0] != 0)) {
                    gm_t9t4u07j77scv t118;
                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                    ((uint8_t *)&t118)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t118 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t117) + 8)));
                    F.ret = t118;
                    goto gm_fret;
                }
                gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 8));
            }
            {
                uint64_t t121 = gm_l3;
                void * t122 = gm_l0;
                uint64_t t123 = GM_LD(uintptr_t, ((((uint8_t *)t122) + 3488) + 0) + 8);
                uint8_t t124 = ((t121 == t123));
                if (!t124) goto ie120;
                {
                    goto lb6;
                }
                goto ix120;
ie120:;
ix120:;
            }
lc6:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
        }
        goto gm_lad;
lb6:;
    }
    {
        {
            void * t127 = gm_l0;
            void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
            void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((((uint8_t *)t129) + 1448) + 0));
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
            gm_l16 = t131;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t133 = gm_l17;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t135 = (t133 < t134);
                if (!t135) goto lb132;
                {
                    gm_t964gsbl2irde t138;
                    uint64_t t139 = gm_l17;
                    GM_ST(uint64_t, (uint8_t *)&t138 + 0, t139);
                    uint64_t t140 = gm_l17;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    memcpy((uint8_t *)&t138 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t141 * 104), 104);
                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&t138) + 0));
                    gm_l18 = t145;
                    F.l19 = (*(gm_tq7qjpkmqmc5s *)(((uint8_t *)&t138) + 8));
                    {
                        void * t147 = gm_l0;
                        uint64_t t148 = gm_l18;
                        gm_b16_8 t149 = gm_f_global_tag_gs7s3p6((void *)NULL, cx, NULL, t147, t148);
                        if (gm_unw.kind) {
                            goto gm_ll136;
                        }
                        gm_l20 = t149;
                    }
                    {
                        void * t150 = gm_l0;
                        uint8_t *t151 = (((uint8_t *)t150) + 3584);
                        gm_b16_8 t152 = gm_l20;
                        gm_tnpifv20irnph t153;
                        GM_ST(gm_b16_8, ((uint8_t *)&t153 + 0), t152);
                        gm_tabb710d9mq2e t154;
                        gm_f_format_0o96lei((void *)&t154, (void *)NULL, cx, NULL, t153);
                        if (gm_unw.kind) {
                            goto gm_ll136;
                        }
                        if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 8, gm_srcuro7l7pd15c2, 84, 27);
                        gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
                        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                        GM_ST(gm_b16_8, t151, t156);
                    }
                    {
                        void * t157 = gm_l0;
                        void * t158 = (void *)(((uint8_t *)t157) + 8);
                        gm_t88rcucg1b7uk t159 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                        uint64_t t160 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t158, t159);
                        if (gm_unw.kind) {
                            goto gm_ll136;
                        }
                        gm_l21 = t160;
                    }
                    {
                        void * t161 = gm_l0;
                        void * t162 = (void *)(((uint8_t *)t161) + 8);
                        gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                        uint64_t t164 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t162, t163);
                        if (gm_unw.kind) {
                            goto gm_ll136;
                        }
                        gm_tf5srh4mp4b3i t165 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l19) + 80));
                        uint64_t t166;
                        if (((((uint8_t *)&t165))[8] != 0)) {
                            uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&t165) + 0));
                            t166 = t167;
                        } else {
                            t166 = ((uint64_t)1ULL);
                        }
                        uint64_t t168 = (t164 > t166 ? t164 : t166);
                        uint64_t t169 = (t168 > ((uint64_t)1ULL) ? t168 : ((uint64_t)1ULL));
                        gm_l22 = t169;
                    }
                    {
                        gm_th2lqbo77pvsc t171 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 56));
                        gm_b16_8 t172;
                        GM_ST(void *, (uint8_t *)&t172, (void *)(gm_scc54gv4a7k04));
                        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(5));
                        gm_th2lqbo77pvsc t173;
                        GM_ST(uint8_t, (uint8_t *)&t173 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t173 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t173 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t173), t172);
                        ((uint8_t *)&t173)[16] = 1;
                        uint8_t t174 = (gm_eq0ogca82g1f(((uint8_t *)&t171), ((uint8_t *)&t173)));
                        if (!t174) goto ie170;
                        {
                            void * t176 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t177 = gm_l20;
                            gm_tnpifv20irnph t178;
                            GM_ST(gm_b16_8, ((uint8_t *)&t178 + 0), t177);
                            gm_tftb1odc739t5 t179 = gm_f_write_80k6mh9(t176, t178);
                            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcuro7l7pd15c2, 90, 13);
                            gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                        }
                        {
                            goto lc132;
                        }
                        goto ix170;
ie170:;
ix170:;
                    }
                    {
                        if (!(((((uint8_t *)&F.l19) + 56))[16] != 0)) goto ie181;
                        gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 56));
                        gm_l23 = t182;
                        {
                            void * t184 = (void *)((uint8_t *)&F.l1);
                            gm_t6j6smj7qoctu t185;
                            gm_b16_8 t186 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t185 + 0, t186);
                            gm_b16_8 t187 = gm_l23;
                            gm_b16_8 t188 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t187);
                            if (gm_unw.kind) {
                                goto gm_ll136;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t185 + 16, t188);
                            gm_tftb1odc739t5 t189 = gm_f_write_u9o0juu(t184, &t185);
                            if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_srcuro7l7pd15c2, 94, 13);
                            gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
                        }
                        {
                            goto lc132;
                        }
                        goto ix181;
ie181:;
ix181:;
                    }
                    {
                        gm_b16_8 t191;
                        uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t193) goto ie192;
                        gm_b16_8 t195;
                        GM_ST(void *, (uint8_t *)&t195, (void *)(gm_slpqve1dqssq0));
                        GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(7));
                        gm_b16_8 t196 = t195;
                        t191 = t196;
                        goto ix192;
ie192:;
                        gm_b16_8 t198;
                        GM_ST(void *, (uint8_t *)&t198, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(0));
                        gm_b16_8 t199 = t198;
                        t191 = t199;
ix192:;
                        gm_l24 = t191;
                    }
                    {
                        void * t200 = (void *)((uint8_t *)&F.l1);
                        gm_teqsi7sd08g9f t201;
                        gm_b16_8 t202 = gm_l24;
                        GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t202);
                        gm_b16_8 t203 = gm_l20;
                        GM_ST(gm_b16_8, (uint8_t *)&t201 + 16, t203);
                        uint64_t t204 = gm_l21;
                        uint64_t t205 = (t204 > ((uint64_t)1ULL) ? t204 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t201 + 32, t205);
                        uint64_t t206 = gm_l22;
                        GM_ST(uint64_t, (uint8_t *)&t201 + 40, t206);
                        gm_tftb1odc739t5 t207 = gm_f_write_lket48d(t200, &t201);
                        if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 1, gm_srcuro7l7pd15c2, 98, 9);
                        gm_b0_1 t208 = GM_LD(gm_b0_1, (((uint8_t *)&t207) + 1));
                    }
                    {
                        gm_f_list_uvfu1k1((void *)&F.l25, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll136;
                        }
                    }
                    {
                        void * t209;
                        uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t211) goto ie210;
                        void * t213 = (void *)((uint8_t *)&F.l25);
                        void * t214 = t213;
                        t209 = t214;
                        goto ix210;
ie210:;
                        void * t216 = (void *)((uint8_t *)&F.l2);
                        void * t217 = t216;
                        t209 = t217;
ix210:;
                        gm_l26 = t209;
                    }
                    {
                        void * t219 = GM_LD(void *, (((uint8_t *)&F.l19) + 96));
                        if (!(GM_LD(void *, ((uint8_t *)&t219)) != 0)) goto ie218;
                        void * t220 = GM_LD(void *, ((uint8_t *)&t219));
                        gm_l27 = t220;
                        {
                            gm_f_list_uvfu1k1((void *)&F.l28, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll136;
                            }
                        }
                        {
                            {
                                gm_l30 = ((uint64_t)0ULL);
                            }
                            {
                                uint64_t t223 = gm_l21;
                                gm_l29 = t223;
                            }
                            {
                                for (;;) {
                                    uint64_t t225 = gm_l30;
                                    uint64_t t226 = gm_l29;
                                    uint8_t t227 = (t225 < t226);
                                    if (!t227) goto lb224;
                                    {
                                        uint64_t t230 = gm_l30;
                                        {
                                            void * t234 = (void *)((uint8_t *)&F.l28);
                                            gm_tftb1odc739t5 t235 = gm_f_push_t83rg2m(t234, ((uint8_t)0ULL));
                                            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_srcuro7l7pd15c2, 105, 32);
                                            gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
                                        }
                                        goto me231;
mn232:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 105, 13);
me231:;
                                    }
lc224:;
                                    {
                                        uint64_t t238 = gm_l30;
                                        uint64_t r239; bool o240 = __builtin_add_overflow(t238, ((uint64_t)1ULL), &r239);
                                        uint64_t t241 = r239;
                                        gm_l30 = t241;
                                    }
                                }
lb224:;
                            }
                        }
                        {
                            gm_f_list_opmir2c((void *)&F.l31, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll136;
                            }
                        }
                        {
                            void * t242 = gm_l0;
                            void * t243 = gm_l27;
                            gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                            gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                            void * t246 = (void *)((uint8_t *)&F.l31);
                            gm_tqgtd5gtgj65s t247;
                            gm_f_value_bytes_qm8egmj((void *)&t247, (void *)NULL, cx, NULL, t242, &(*(gm_t6evimt84lndq *)(((uint8_t *)t243) + 0)), t244, t245, ((uint64_t)0ULL), t246);
                            if (gm_unw.kind) {
                                goto gm_ll136;
                            }
                            if (((((uint8_t *)&t247))[0] != 0)) {
                                gm_t9t4u07j77scv t248;
                                GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
                                ((uint8_t *)&t248)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t248 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t247) + 8)));
                                F.ret = t248;
                                goto gm_fret;
                            }
                            gm_b0_1 t249 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 8));
                        }
                        {
                            uint64_t t251 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 0) + 8);
                            uint8_t t252 = (t251 > ((uint64_t)0ULL));
                            if (!t252) goto ie250;
                            {
                                void * t254 = gm_l0;
                                gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                                gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
                                gm_b16_8 t257 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t254, t256);
                                if (gm_unw.kind) {
                                    goto gm_ll136;
                                }
                                gm_l32 = t257;
                            }
                            {
                                void * t258 = gm_l26;
                                gm_tftksr4op114e t259;
                                gm_b16_8 t260 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t259 + 0, t260);
                                gm_b16_8 t261 = gm_l32;
                                GM_ST(gm_b16_8, (uint8_t *)&t259 + 16, t261);
                                uint64_t t262 = GM_LD(uintptr_t, (((uint8_t *)&F.l28) + 0) + 8);
                                GM_ST(uint64_t, (uint8_t *)&t259 + 32, t262);
                                gm_tftb1odc739t5 t263 = gm_f_write_dhp21l9(t258, &t259);
                                if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_srcuro7l7pd15c2, 110, 17);
                                gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
                            }
                            goto ix250;
ie250:;
ix250:;
                        }
                        {
                            {
                                gm_b16_8 t266 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l33 = t266;
                            }
                            {
                                gm_l34 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t268 = gm_l34;
                                    uint64_t t269 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    uint8_t t270 = (t268 < t269);
                                    if (!t270) goto lb267;
                                    {
                                        uint64_t t273 = gm_l34;
                                        uint64_t t274 = (uintptr_t)t273;
                                        uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        gm_tob1snuii5bme t278 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t274 * 32));
                                        gm_l35 = t278;
                                        {
                                            void * t280 = gm_l26;
                                            gm_thddbqb6snabm t281;
                                            gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                            GM_ST(gm_b16_8, (uint8_t *)&t281 + 0, t282);
                                            uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 24));
                                            GM_ST(uint64_t, (uint8_t *)&t281 + 16, t283);
                                            gm_b16_8 t284 = gm_l20;
                                            GM_ST(gm_b16_8, (uint8_t *)&t281 + 24, t284);
                                            uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 0));
                                            GM_ST(uint64_t, (uint8_t *)&t281 + 40, t285);
                                            gm_tftb1odc739t5 t286 = gm_f_write_vlrefbu(t280, &t281);
                                            if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 1, gm_srcuro7l7pd15c2, 113, 17);
                                            gm_b0_1 t287 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 1));
                                        }
                                        goto me276;
mn277:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 112, 13);
me276:;
                                    }
lc267:;
                                    {
                                        uint64_t t289 = gm_l34;
                                        uint64_t r290; bool o291 = __builtin_add_overflow(t289, ((uint64_t)1ULL), &r290);
                                        uint64_t t292 = r290;
                                        gm_l34 = t292;
                                    }
                                }
lb267:;
                            }
                        }
                        goto ix218;
ie218:;
ix218:;
                    }
                    {
                        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                        if (!t294) goto ie293;
                        {
                            void * t296 = (void *)((uint8_t *)&F.l1);
                            gm_th2d9c7aju8hp t297;
                            gm_b16_8 t298 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 0, t298);
                            gm_b16_8 t299 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 16, t299);
                            gm_b16_8 t300 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 32, t300);
                            gm_b16_8 t301 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 48, t301);
                            gm_b16_8 t302 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 64, t302);
                            gm_b16_8 t303 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 80, t303);
                            gm_b16_8 t304 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t297 + 96, t304);
                            gm_tftb1odc739t5 t305 = gm_f_write_qbs7gku(t296, &t297);
                            if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 1, gm_srcuro7l7pd15c2, 117, 13);
                            gm_b0_1 t306 = GM_LD(gm_b0_1, (((uint8_t *)&t305) + 1));
                        }
                        {
                            void * t307 = gm_l0;
                            void * t308 = (void *)(((uint8_t *)t307) + 1928);
                            gm_t6j6smj7qoctu t309;
                            gm_b16_8 t310 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t309 + 0, t310);
                            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t309 + 16, t311);
                            gm_tftb1odc739t5 t312 = gm_f_write_ufh35j8(t308, &t309);
                            if (((((uint8_t *)&t312))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t312) + 1, gm_srcuro7l7pd15c2, 118, 13);
                            gm_b0_1 t313 = GM_LD(gm_b0_1, (((uint8_t *)&t312) + 1));
                        }
                        goto ix293;
ie293:;
ix293:;
                    }
                    goto me143;
mn144:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 82, 5);
me143:;
                }
lc132:;
                {
                    uint64_t t315 = gm_l17;
                    uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                    uint64_t t318 = r316;
                    gm_l17 = t318;
                }
            }
gm_ll136:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb132; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc132; }
            }
            goto gm_lad;
lb132:;
        }
    }
    {
        for (;;) {
            uint64_t t320 = gm_l3;
            void * t321 = gm_l0;
            uint64_t t322 = GM_LD(uintptr_t, ((((uint8_t *)t321) + 3488) + 0) + 8);
            uint8_t t323 = (t320 < t322);
            if (!t323) goto lb319;
            {
                uint64_t t326 = gm_l3;
                uint64_t t327 = (uintptr_t)t326;
                void * t328 = gm_l0;
                uint64_t t329 = GM_LD(uintptr_t, ((((uint8_t *)t328) + 3488) + 0) + 8);
                if (t327 >= (uintptr_t)(t329)) gm_trap_index(t327, t329, gm_srcuro7l7pd15c2, 123, 15);
                gm_to3r6jqo24gs1 t330 = GM_LD(gm_to3r6jqo24gs1, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t328) + 3488) + 0)) + t327 * 32));
                gm_l36 = t330;
            }
            {
                gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 16));
                gm_l37 = t331;
            }
            {
                void * t332 = gm_l0;
                gm_b16_8 t333 = gm_l37;
                GM_ST(gm_b16_8, (((uint8_t *)t332) + 3584), t333);
            }
            {
                uint64_t t334 = gm_l3;
                uint64_t r335; bool o336 = __builtin_add_overflow(t334, ((uint64_t)1ULL), &r335);
                if (o336) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 126, 9);
                uint64_t t337 = r335;
                gm_l3 = t337;
            }
            {
                void * t338 = gm_l0;
                void * t339 = (void *)(((uint8_t *)t338) + 8);
                gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                uint64_t t341 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t339, t340);
                if (gm_unw.kind) {
                    goto gm_ll324;
                }
                gm_l38 = t341;
            }
            {
                void * t342 = (void *)((uint8_t *)&F.l1);
                gm_tgbgti82mf879 t343;
                gm_b16_8 t344 = gm_l37;
                GM_ST(gm_b16_8, (uint8_t *)&t343 + 0, t344);
                uint64_t t345 = gm_l38;
                uint64_t t346 = (t345 > ((uint64_t)1ULL) ? t345 : ((uint64_t)1ULL));
                GM_ST(uint64_t, (uint8_t *)&t343 + 16, t346);
                void * t347 = gm_l0;
                void * t348 = (void *)(((uint8_t *)t347) + 8);
                gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                uint64_t t350 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t348, t349);
                if (gm_unw.kind) {
                    goto gm_ll324;
                }
                uint64_t t351 = (t350 > ((uint64_t)1ULL) ? t350 : ((uint64_t)1ULL));
                GM_ST(uint64_t, (uint8_t *)&t343 + 24, t351);
                gm_tftb1odc739t5 t352 = gm_f_write_60kg76i(t342, &t343);
                if (((((uint8_t *)&t352))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t352) + 1, gm_srcuro7l7pd15c2, 128, 9);
                gm_b0_1 t353 = GM_LD(gm_b0_1, (((uint8_t *)&t352) + 1));
            }
            {
                gm_f_list_uvfu1k1((void *)&F.l39, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll324;
                }
            }
            {
                {
                    gm_l41 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t355 = gm_l38;
                    gm_l40 = t355;
                }
                {
                    for (;;) {
                        uint64_t t357 = gm_l41;
                        uint64_t t358 = gm_l40;
                        uint8_t t359 = (t357 < t358);
                        if (!t359) goto lb356;
                        {
                            uint64_t t362 = gm_l41;
                            {
                                void * t366 = (void *)((uint8_t *)&F.l39);
                                gm_tftb1odc739t5 t367 = gm_f_push_t83rg2m(t366, ((uint8_t)0ULL));
                                if (((((uint8_t *)&t367))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t367) + 1, gm_srcuro7l7pd15c2, 130, 28);
                                gm_b0_1 t368 = GM_LD(gm_b0_1, (((uint8_t *)&t367) + 1));
                            }
                            goto me363;
mn364:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 130, 9);
me363:;
                        }
lc356:;
                        {
                            uint64_t t370 = gm_l41;
                            uint64_t r371; bool o372 = __builtin_add_overflow(t370, ((uint64_t)1ULL), &r371);
                            uint64_t t373 = r371;
                            gm_l41 = t373;
                        }
                    }
lb356:;
                }
            }
            {
                gm_f_list_opmir2c((void *)&F.l42, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll324;
                }
            }
            {
                void * t374 = gm_l0;
                void * t375 = GM_LD(void *, (((uint8_t *)&gm_l36) + 0));
                gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                gm_b16_8 t377 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                void * t378 = (void *)((uint8_t *)&F.l42);
                gm_tqgtd5gtgj65s t379;
                gm_f_value_bytes_qm8egmj((void *)&t379, (void *)NULL, cx, NULL, t374, &(*(gm_t6evimt84lndq *)(((uint8_t *)t375) + 0)), t376, t377, ((uint64_t)0ULL), t378);
                if (gm_unw.kind) {
                    goto gm_ll324;
                }
                if (((((uint8_t *)&t379))[0] != 0)) {
                    gm_t9t4u07j77scv t380;
                    GM_ST(uint8_t, (uint8_t *)&t380 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t380 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t380 + 4, 0);
                    ((uint8_t *)&t380)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t380 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t379) + 8)));
                    F.ret = t380;
                    goto gm_fret;
                }
                gm_b0_1 t381 = GM_LD(gm_b0_1, (((uint8_t *)&t379) + 8));
            }
            {
                uint64_t t383 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                uint8_t t384 = (t383 > ((uint64_t)0ULL));
                if (!t384) goto ie382;
                {
                    void * t386 = gm_l0;
                    gm_b16_8 t387 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                    gm_b16_8 t388 = GM_LD(gm_b16_8, ((uint8_t *)&t387));
                    gm_b16_8 t389 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t386, t388);
                    if (gm_unw.kind) {
                        goto gm_ll324;
                    }
                    gm_l43 = t389;
                }
                {
                    void * t390 = (void *)((uint8_t *)&F.l2);
                    gm_tftksr4op114e t391;
                    gm_b16_8 t392 = gm_l37;
                    GM_ST(gm_b16_8, (uint8_t *)&t391 + 0, t392);
                    gm_b16_8 t393 = gm_l43;
                    GM_ST(gm_b16_8, (uint8_t *)&t391 + 16, t393);
                    uint64_t t394 = GM_LD(uintptr_t, (((uint8_t *)&F.l39) + 0) + 8);
                    GM_ST(uint64_t, (uint8_t *)&t391 + 32, t394);
                    gm_tftb1odc739t5 t395 = gm_f_write_snsbmln(t390, &t391);
                    if (((((uint8_t *)&t395))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t395) + 1, gm_srcuro7l7pd15c2, 135, 13);
                    gm_b0_1 t396 = GM_LD(gm_b0_1, (((uint8_t *)&t395) + 1));
                }
                goto ix382;
ie382:;
ix382:;
            }
            {
                {
                    gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)&F.l42) + 0));
                    gm_l44 = t398;
                }
                {
                    gm_l45 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t400 = gm_l45;
                        uint64_t t401 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        uint8_t t402 = (t400 < t401);
                        if (!t402) goto lb399;
                        {
                            uint64_t t405 = gm_l45;
                            uint64_t t406 = (uintptr_t)t405;
                            uint64_t t407 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                            gm_tob1snuii5bme t410 = GM_LD(gm_tob1snuii5bme, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t406 * 32));
                            gm_l46 = t410;
                            {
                                void * t412 = (void *)((uint8_t *)&F.l2);
                                gm_thddbqb6snabm t413;
                                gm_b16_8 t414 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                GM_ST(gm_b16_8, (uint8_t *)&t413 + 0, t414);
                                uint64_t t415 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 24));
                                GM_ST(uint64_t, (uint8_t *)&t413 + 16, t415);
                                gm_b16_8 t416 = gm_l37;
                                GM_ST(gm_b16_8, (uint8_t *)&t413 + 24, t416);
                                uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 0));
                                GM_ST(uint64_t, (uint8_t *)&t413 + 40, t417);
                                gm_tftb1odc739t5 t418 = gm_f_write_th5g0ba(t412, &t413);
                                if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 1, gm_srcuro7l7pd15c2, 138, 13);
                                gm_b0_1 t419 = GM_LD(gm_b0_1, (((uint8_t *)&t418) + 1));
                            }
                            goto me408;
mn409:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 137, 9);
me408:;
                        }
lc399:;
                        {
                            uint64_t t421 = gm_l45;
                            uint64_t r422; bool o423 = __builtin_add_overflow(t421, ((uint64_t)1ULL), &r422);
                            uint64_t t424 = r422;
                            gm_l45 = t424;
                        }
                    }
lb399:;
                }
            }
lc319:;
        }
gm_ll324:;
        if (gm_unw.frame == &F.h && gm_unw.label == 7) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb319; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc319; }
        }
        goto gm_lad;
lb319:;
    }
    {
        void * t425 = gm_l0;
        gm_tqgtd5gtgj65s t426;
        gm_f_drain_leoibma((void *)&t426, (void *)NULL, cx, NULL, t425);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t426))[0] != 0)) {
            gm_t9t4u07j77scv t427;
            GM_ST(uint8_t, (uint8_t *)&t427 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t427 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t427 + 4, 0);
            ((uint8_t *)&t427)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t427 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t426) + 8)));
            F.ret = t427;
            goto gm_fret;
        }
        gm_b0_1 t428 = GM_LD(gm_b0_1, (((uint8_t *)&t426) + 8));
    }
    {
        uint64_t t430 = gm_l3;
        void * t431 = gm_l0;
        uint64_t t432 = GM_LD(uintptr_t, ((((uint8_t *)t431) + 3488) + 0) + 8);
        uint8_t t433 = (!(t430 == t432));
        if (!t433) goto ie429;
        {
            gm_tl874t2atnlh3 t435 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t436;
            GM_ST(void *, (uint8_t *)&t436, (void *)(gm_s2uimijqjekbd));
            GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(66));
            gm_t08olq97rscn3 t437;
            gm_f_unsupported_531q51r((void *)&t437, (void *)NULL, cx, NULL, t435, t436);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 8, t437);
            goto gm_fdone;
        }
        goto ix429;
ie429:;
ix429:;
    }
    {
        {
            void * t439 = gm_l0;
            gm_b16_8 t440 = GM_LD(gm_b16_8, ((((uint8_t *)t439) + 2552) + 0));
            gm_l47 = t440;
        }
        {
            gm_l48 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t442 = gm_l48;
                uint64_t t443 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                uint8_t t444 = (t442 < t443);
                if (!t444) goto lb441;
                {
                    uint64_t t447 = gm_l48;
                    uint64_t t448 = (uintptr_t)t447;
                    uint64_t t449 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    gm_t4rau9abddl2u t452 = GM_LD(gm_t4rau9abddl2u, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t448 * 104));
                    F.l49 = t452;
                    {
                        gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&F.l49) + 88));
                        gm_l50 = t454;
                    }
                    {
                        void * t455 = (void *)((uint8_t *)&F.l1);
                        gm_tgbgti82mf879 t456;
                        gm_b16_8 t457 = gm_l50;
                        GM_ST(gm_b16_8, (uint8_t *)&t456 + 0, t457);
                        void * t458 = gm_l0;
                        void * t459 = (void *)(((uint8_t *)t458) + 8);
                        gm_t88rcucg1b7uk t460 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l49) + 0));
                        uint64_t t461 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t459, t460);
                        if (gm_unw.kind) {
                            goto gm_ll445;
                        }
                        uint64_t t462 = (t461 > ((uint64_t)1ULL) ? t461 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t456 + 16, t462);
                        void * t463 = gm_l0;
                        void * t464 = (void *)(((uint8_t *)t463) + 8);
                        gm_t88rcucg1b7uk t465 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l49) + 0));
                        uint64_t t466 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t464, t465);
                        if (gm_unw.kind) {
                            goto gm_ll445;
                        }
                        uint64_t t467 = (t466 > ((uint64_t)1ULL) ? t466 : ((uint64_t)1ULL));
                        GM_ST(uint64_t, (uint8_t *)&t456 + 24, t467);
                        gm_tftb1odc739t5 t468 = gm_f_write_60kg76i(t455, &t456);
                        if (((((uint8_t *)&t468))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t468) + 1, gm_srcuro7l7pd15c2, 146, 9);
                        gm_b0_1 t469 = GM_LD(gm_b0_1, (((uint8_t *)&t468) + 1));
                    }
                    {
                        uint64_t t471 = GM_LD(uintptr_t, ((((uint8_t *)&F.l49) + 8) + 0) + 8);
                        uint8_t t472 = (t471 > ((uint64_t)0ULL));
                        if (!t472) goto ie470;
                        {
                            void * t474 = gm_l0;
                            gm_b16_8 t475 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l49) + 8) + 0));
                            gm_b16_8 t476 = GM_LD(gm_b16_8, ((uint8_t *)&t475));
                            gm_b16_8 t477 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t474, t476);
                            if (gm_unw.kind) {
                                goto gm_ll445;
                            }
                            gm_l51 = t477;
                        }
                        {
                            void * t478 = (void *)((uint8_t *)&F.l2);
                            gm_tftksr4op114e t479;
                            gm_b16_8 t480 = gm_l50;
                            GM_ST(gm_b16_8, (uint8_t *)&t479 + 0, t480);
                            gm_b16_8 t481 = gm_l51;
                            GM_ST(gm_b16_8, (uint8_t *)&t479 + 16, t481);
                            uint64_t t482 = GM_LD(uintptr_t, ((((uint8_t *)&F.l49) + 8) + 0) + 8);
                            GM_ST(uint64_t, (uint8_t *)&t479 + 32, t482);
                            gm_tftb1odc739t5 t483 = gm_f_write_snsbmln(t478, &t479);
                            if (((((uint8_t *)&t483))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t483) + 1, gm_srcuro7l7pd15c2, 149, 13);
                            gm_b0_1 t484 = GM_LD(gm_b0_1, (((uint8_t *)&t483) + 1));
                        }
                        goto ix470;
ie470:;
ix470:;
                    }
                    {
                        {
                            gm_b16_8 t486 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l49) + 48) + 0));
                            gm_l52 = t486;
                        }
                        {
                            gm_l53 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t488 = gm_l53;
                                uint64_t t489 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                uint8_t t490 = (t488 < t489);
                                if (!t490) goto lb487;
                                {
                                    uint64_t t493 = gm_l53;
                                    uint64_t t494 = (uintptr_t)t493;
                                    uint64_t t495 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                                    gm_tf1bjg81u0glv t498 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t494 * 24));
                                    gm_l54 = t498;
                                    {
                                        void * t500 = gm_l0;
                                        gm_b16_8 t501 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                                        gm_b16_8 t502 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t500, t501);
                                        if (gm_unw.kind) {
                                            goto gm_ll491;
                                        }
                                        gm_l55 = t502;
                                    }
                                    {
                                        void * t503 = (void *)((uint8_t *)&F.l2);
                                        gm_tftksr4op114e t504;
                                        gm_b16_8 t505 = gm_l55;
                                        GM_ST(gm_b16_8, (uint8_t *)&t504 + 0, t505);
                                        gm_b16_8 t506 = gm_l50;
                                        GM_ST(gm_b16_8, (uint8_t *)&t504 + 16, t506);
                                        uint64_t t507 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 0));
                                        GM_ST(uint64_t, (uint8_t *)&t504 + 32, t507);
                                        gm_tftb1odc739t5 t508 = gm_f_write_t09mk5p(t503, &t504);
                                        if (((((uint8_t *)&t508))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t508) + 1, gm_srcuro7l7pd15c2, 153, 13);
                                        gm_b0_1 t509 = GM_LD(gm_b0_1, (((uint8_t *)&t508) + 1));
                                    }
                                    goto me496;
mn497:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 151, 9);
me496:;
                                }
lc487:;
                                {
                                    uint64_t t511 = gm_l53;
                                    uint64_t r512; bool o513 = __builtin_add_overflow(t511, ((uint64_t)1ULL), &r512);
                                    uint64_t t514 = r512;
                                    gm_l53 = t514;
                                }
                            }
gm_ll491:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb487; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc487; }
                            }
                            goto gm_ll445;
lb487:;
                        }
                    }
                    goto me450;
mn451:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 144, 5);
me450:;
                }
lc441:;
                {
                    uint64_t t516 = gm_l48;
                    uint64_t r517; bool o518 = __builtin_add_overflow(t516, ((uint64_t)1ULL), &r517);
                    uint64_t t519 = r517;
                    gm_l48 = t519;
                }
            }
gm_ll445:;
            if (gm_unw.frame == &F.h && gm_unw.label == 10) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb441; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc441; }
            }
            goto gm_lad;
lb441:;
        }
    }
    {
        void * t520 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t521;
        GM_ST(void *, (uint8_t *)&t521, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t521 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t522 = gm_f_extend_0cs85gs(t520, t521);
        if (((((uint8_t *)&t522))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t522) + 1, gm_srcuro7l7pd15c2, 156, 5);
        gm_b0_1 t523 = GM_LD(gm_b0_1, (((uint8_t *)&t522) + 1));
    }
    {
        gm_t6j6smj7qoctu t524;
        gm_b16_8 t525 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t526 = GM_LD(gm_b16_8, ((uint8_t *)&t525));
        GM_ST(gm_b16_8, (uint8_t *)&t524 + 0, t526);
        gm_b16_8 t527 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t528 = GM_LD(gm_b16_8, ((uint8_t *)&t527));
        GM_ST(gm_b16_8, (uint8_t *)&t524 + 16, t528);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out))[0] = 0;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_t9t4u07j77scv *)gm_out) + 8, t524);
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
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_place_reloc_blfij5d F;
    void * gm_l0; /* g */
    gm_tkaapcii86udk gm_l1; /* pl */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    uint64_t gm_l3; /* off */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* root */
    uint64_t gm_l7; /* addend */
    gm_t88rcucg1b7uk gm_l8; /* cur */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_b16_8 gm_l14; /* elems */
    uint32_t gm_l15; /* id */
    GM_STACK_CHECK(&F, 1322u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)a1) + 0));
        gm_tkn42u7na3t81 t6 = gm_f_cell_type_0jgf3ue(t4, t5);
        if (!((((uint8_t *)&t6))[4] != 0)) goto ie3;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t6));
        gm_l4 = t7;
        gm_t88rcucg1b7uk t9 = gm_l4;
        gm_t88rcucg1b7uk t10 = t9;
        t2 = t10;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t11;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_t88rcucg1b7uk t17 = t16;
        t11 = t17;
        goto ix12;
ie12:;
        gm_t88rcucg1b7uk t18;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
        uint8_t t21 = ((t20 == ((uint64_t)1ULL)));
        if (!t21) goto ie19;
        void * t22 = GM_LD(void *, (((uint8_t *)a1) + 0));
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 0) + 0));
        if (t23 != ((uint8_t)3ULL)) goto ie19;
        void * t24 = GM_LD(void *, ((((uint8_t *)t22) + 0) + 16));
        gm_l5 = t24;
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
        void * t29 = (void *)(((uint8_t *)t28) + 8);
        void * t30 = gm_l5;
        uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 0) + 8);
        uint64_t t32 = (uint64_t)t31;
        gm_t88rcucg1b7uk t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t29, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t35 = t34;
        t18 = t35;
        goto ix19;
ie19:;
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sqrl3kgomorsv));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(38));
        gm_t08olq97rscn3 t39;
        gm_f_unsupported_531q51r((void *)&t39, (void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tt9fudfifskca *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 8, t39);
        goto gm_fdone;
        t18 = GM_ZERO(gm_t88rcucg1b7uk);
ix19:;
        t11 = t18;
ix12:;
        t2 = t11;
ix3:;
        gm_l6 = t2;
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        gm_t88rcucg1b7uk t40 = gm_l6;
        gm_l8 = t40;
    }
    {
        {
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
            gm_l9 = t42;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t44 = gm_l10;
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t46 = (t44 < t45);
                if (!t46) goto lb43;
                {
                    uint64_t t49 = gm_l10;
                    uint64_t t50 = (uintptr_t)t49;
                    uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t50 * 8));
                    uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)&t52));
                    gm_l11 = t55;
                    {
                        void * t57 = gm_l0;
                        void * t58 = (void *)(((uint8_t *)t57) + 8);
                        gm_t88rcucg1b7uk t59 = gm_l8;
                        gm_t53at80pfdus3 t60;
                        gm_f_kind_ghvav22((void *)&t60, t58, GM_CONV(gm_csguankko4lmhd, t59));
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                        switch (t67) { case ((uint8_t)0ULL): goto ma65; case ((uint8_t)1ULL): goto ma65; case ((uint8_t)2ULL): goto ma65; case ((uint8_t)3ULL): goto ma65; case ((uint8_t)4ULL): goto ma65; case ((uint8_t)5ULL): goto ma65; case ((uint8_t)6ULL): goto ma65; case ((uint8_t)7ULL): goto ma65; case ((uint8_t)8ULL): goto ma65; case ((uint8_t)9ULL): goto ma65; case ((uint8_t)10ULL): goto ma65; case ((uint8_t)11ULL): goto ma62; case ((uint8_t)12ULL): goto ma63; case ((uint8_t)13ULL): goto ma65; case ((uint8_t)14ULL): goto ma65; case ((uint8_t)15ULL): goto ma65; case ((uint8_t)16ULL): goto ma65; case ((uint8_t)17ULL): goto ma64; case ((uint8_t)18ULL): goto ma65; case ((uint8_t)19ULL): goto ma65; case ((uint8_t)20ULL): goto ma65; case ((uint8_t)21ULL): goto ma65; case ((uint8_t)22ULL): goto ma65; case ((uint8_t)23ULL): goto ma65; case ((uint8_t)24ULL): goto ma65; case ((uint8_t)25ULL): goto ma65; case ((uint8_t)26ULL): goto ma65; default: goto ma65; }
ma62:;
                        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                        if (t69 != ((uint8_t)11ULL)) goto mn68;
                        uint64_t t70 = GM_LD(uint64_t, ((((uint8_t *)&t60) + 8) + 0));
                        gm_l12 = t70;
                        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t60) + 8) + 8));
                        gm_l13 = t71;
                        {
                            uint64_t t73 = gm_l7;
                            uint64_t t74 = gm_l11;
                            void * t75 = gm_l0;
                            void * t76 = (void *)(((uint8_t *)t75) + 8);
                            gm_t88rcucg1b7uk t77 = gm_l13;
                            uint64_t t78 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t76, t77);
                            if (gm_unw.kind) {
                                goto gm_ll47;
                            }
                            uint64_t r79; bool o80 = __builtin_mul_overflow(t74, t78, &r79);
                            if (o80) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 284, 27);
                            uint64_t t81 = r79;
                            uint64_t r82; bool o83 = __builtin_add_overflow(t73, t81, &r82);
                            if (o83) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 284, 17);
                            uint64_t t84 = r82;
                            gm_l7 = t84;
                        }
                        {
                            uint64_t t86 = gm_l11;
                            uint64_t t87 = gm_l12;
                            uint8_t t88 = (t86 < t87);
                            if (!t88) goto ie85;
                            {
                                gm_t88rcucg1b7uk t90 = gm_l13;
                                gm_l8 = t90;
                            }
                            goto ix85;
ie85:;
ix85:;
                        }
                        goto me61;
mn68:;
ma63:;
                        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                        if (t92 != ((uint8_t)12ULL)) goto mn91;
                        gm_b16_8 t93 = GM_LD(gm_b16_8, ((((uint8_t *)&t60) + 8) + 0));
                        gm_l14 = t93;
                        {
                            uint64_t t95 = gm_l7;
                            void * t96 = gm_l0;
                            void * t97 = (void *)(((uint8_t *)t96) + 8);
                            gm_t88rcucg1b7uk t98 = gm_l8;
                            gm_b16_8 t99 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t97, t98);
                            if (gm_unw.kind) {
                                goto gm_ll47;
                            }
                            gm_b16_8 t100 = t99;
                            uint64_t t101 = gm_l11;
                            uint64_t t102 = (uint64_t)t101;
                            uint64_t t103 = (uintptr_t)t102;
                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&t100) + 8);
                            if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcuro7l7pd15c2, 288, 27);
                            uint64_t t105 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t100)) + t103 * 8));
                            uint64_t r106; bool o107 = __builtin_add_overflow(t95, t105, &r106);
                            if (o107) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 288, 17);
                            uint64_t t108 = r106;
                            gm_l7 = t108;
                        }
                        {
                            gm_b16_8 t109 = gm_l14;
                            gm_b16_8 t110 = t109;
                            uint64_t t111 = gm_l11;
                            uint64_t t112 = (uint64_t)t111;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&t110) + 8);
                            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srcuro7l7pd15c2, 289, 23);
                            gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t110)) + t113 * 4));
                            gm_l8 = t115;
                        }
                        goto me61;
mn91:;
ma64:;
                        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                        if (t117 != ((uint8_t)17ULL)) goto mn116;
                        uint32_t t118 = GM_LD(uint32_t, ((((uint8_t *)&t60) + 8) + 0));
                        gm_l15 = t118;
                        {
                            void * t121 = gm_l0;
                            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
                            uint32_t t123 = gm_l15;
                            void * t124 = gm_f_adt_mpqv8qe(t122, t123);
                            uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t124) + 24) + 0));
                            if (t125 != ((uint8_t)0ULL)) goto ie120;
                            gm_tpautr4br7h3l t126 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t124) + 24) + 8));
                            F.l16 = t126;
                            {
                                uint64_t t128 = gm_l7;
                                void * t129 = gm_l0;
                                void * t130 = (void *)(((uint8_t *)t129) + 8);
                                gm_t88rcucg1b7uk t131 = gm_l8;
                                gm_b16_8 t132 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t130, t131);
                                if (gm_unw.kind) {
                                    goto gm_ll47;
                                }
                                gm_b16_8 t133 = t132;
                                uint64_t t134 = gm_l11;
                                uint64_t t135 = (uint64_t)t134;
                                uint64_t t136 = (uintptr_t)t135;
                                uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&t133) + 8);
                                if (t136 >= (uintptr_t)(t137)) gm_trap_index(t136, t137, gm_srcuro7l7pd15c2, 293, 27);
                                uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t133)) + t136 * 8));
                                uint64_t r139; bool o140 = __builtin_add_overflow(t128, t138, &r139);
                                if (o140) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 293, 17);
                                uint64_t t141 = r139;
                                gm_l7 = t141;
                            }
                            {
                                gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                                gm_b16_8 t143 = t142;
                                uint64_t t144 = gm_l11;
                                uint64_t t145 = (uint64_t)t144;
                                uint64_t t146 = (uintptr_t)t145;
                                uint64_t t147 = GM_LD(uintptr_t, ((uint8_t *)&t143) + 8);
                                if (t146 >= (uintptr_t)(t147)) gm_trap_index(t146, t147, gm_srcuro7l7pd15c2, 294, 23);
                                gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t143)) + t146 * 40) + 16));
                                gm_l8 = t148;
                            }
                            goto ix120;
ie120:;
                            {
                                gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                gm_b16_8 t151;
                                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(38));
                                gm_t08olq97rscn3 t152;
                                gm_f_unsupported_531q51r((void *)&t152, (void *)NULL, cx, NULL, t150, t151);
                                if (gm_unw.kind) {
                                    goto gm_ll47;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tt9fudfifskca *)gm_out))[0] = 1;
                                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 8, t152);
                                goto gm_fdone;
                            }
ix120:;
                        }
                        goto me61;
mn116:;
ma65:;
                        gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        gm_b16_8 t155;
                        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sqrl3kgomorsv));
                        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(38));
                        gm_t08olq97rscn3 t156;
                        gm_f_unsupported_531q51r((void *)&t156, (void *)NULL, cx, NULL, t154, t155);
                        if (gm_unw.kind) {
                            goto gm_ll47;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tt9fudfifskca *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 8, t156);
                        goto gm_fdone;
                        goto me61;
mn153:;
ma66:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 282, 9);
me61:;
                    }
                    goto me53;
mn54:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 281, 5);
me53:;
                }
lc43:;
                {
                    uint64_t t158 = gm_l10;
                    uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                    uint64_t t161 = r159;
                    gm_l10 = t161;
                }
            }
gm_ll47:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
            }
            goto gm_lad;
lb43:;
        }
    }
    {
        gm_tob1snuii5bme t162;
        uint64_t t163 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t162 + 0, t163);
        void * t164 = gm_l0;
        void * t165 = GM_LD(void *, (((uint8_t *)a1) + 0));
        gm_t88rcucg1b7uk t166 = gm_l6;
        gm_t3877pk7lv748 t167;
        void * t168 = gm_l0;
        gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)t168) + 3584));
        GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t169);
        uint64_t t170 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t167 + 16, t170);
        gm_tabb710d9mq2e t171;
        gm_f_format_317bju7((void *)&t171, (void *)NULL, cx, NULL, &t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t171))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t171) + 8, gm_srcuro7l7pd15c2, 299, 69);
        gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&t171) + 8));
        gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
        gm_b16_8 t174 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t164, t165, t166, t173);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t162 + 8, t174);
        uint64_t t175 = gm_l7;
        GM_ST(uint64_t, (uint8_t *)&t162 + 24, t175);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tt9fudfifskca *)gm_out))[0] = 0;
        GM_ST(gm_tob1snuii5bme, (uint8_t *)&(*(gm_tt9fudfifskca *)gm_out) + 8, t162);
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
inline __attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_store_int_mfkaqk5 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* out */
    uint64_t gm_l2; /* off */
    unsigned __int128 gm_l3; /* v */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint64_t gm_l8; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l4;
            gm_l5 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = gm_l5;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l7 = t13;
                    {
                        uint64_t t15;
                        void * t17 = gm_l0;
                        uint8_t t18 = GM_LD(uint8_t, ((((((uint8_t *)t17) + 8) + 8) + 0) + 8));
                        if (!t18) goto ie16;
                        uint64_t t20 = gm_l7;
                        uint64_t t21 = t20;
                        t15 = t21;
                        goto ix16;
ie16:;
                        uint64_t t23 = gm_l4;
                        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
                        if (o25) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 311, 61);
                        uint64_t t26 = r24;
                        uint64_t t27 = gm_l7;
                        uint64_t r28; bool o29 = __builtin_sub_overflow(t26, t27, &r28);
                        if (o29) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 311, 61);
                        uint64_t t30 = r28;
                        uint64_t t31 = t30;
                        t15 = t31;
ix16:;
                        gm_l8 = t15;
                    }
                    {
                        gm_b16_8 t32 = gm_l1;
                        gm_b16_8 t33 = t32;
                        uint64_t t34 = gm_l2;
                        uint64_t t35 = gm_l8;
                        uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
                        if (o37) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 312, 14);
                        uint64_t t38 = r36;
                        uint64_t t39 = (uint64_t)t38;
                        uint64_t t40 = (uintptr_t)t39;
                        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&t33) + 8);
                        if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_srcuro7l7pd15c2, 312, 9);
                        unsigned __int128 t42 = gm_l3;
                        uint64_t t43 = gm_l7;
                        uint64_t r44; bool o45 = __builtin_mul_overflow(((uint64_t)8ULL), t43, &r44);
                        if (o45) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 312, 51);
                        uint64_t t46 = r44;
                        unsigned __int128 t47 = (unsigned __int128)t46;
                        unsigned __int128 n48 = (unsigned __int128)t47;
                        if (n48 >= 128) gm_trap_shift((uint64_t)n48, "u128", gm_srcuro7l7pd15c2, 312, 41);
                        unsigned __int128 t49 = (unsigned __int128)(t42 >> (int)n48);
                        uint8_t t50 = (uint8_t)t49;
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t33)) + t40 * 1), t50);
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 310, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t52 = gm_l6;
                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                    uint64_t t55 = r53;
                    gm_l6 = t55;
                }
            }
lb4:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5) {
    struct gm_fr_value_bytes_qm8egmj F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* off */
    void * gm_l5; /* relocs */
    unsigned __int128 gm_l6; /* bits */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    uint64_t gm_l11; /* at */
    double gm_l12; /* x */
    uint8_t gm_l13; /* b */
    void * gm_l14; /* x */
    gm_t88rcucg1b7uk gm_l15; /* inner */
    void * gm_l16; /* x */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    void * gm_l19; /* x */
    gm_t88rcucg1b7uk gm_l20; /* err */
    gm_b16_8 gm_l21; /* offs */
    uint32_t gm_l22; /* index */
    void * gm_l23; /* payload */
    uint32_t gm_l24; /* id */
    gm_t88rcucg1b7uk gm_l25; /* tag_ty */
    gm_b16_8 gm_l26; /* variants */
    gm_b16_8 gm_l28; /* offs */
    __int128 gm_l29; /* tag */
    void * gm_l30; /* x */
    gm_t88rcucg1b7uk gm_l31; /* pt */
    void * gm_l32; /* a */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* esz */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* elems */
    gm_b16_8 gm_l40; /* offs */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* k */
    uint32_t gm_l45; /* id */
    gm_t88rcucg1b7uk gm_l47; /* u */
    unsigned __int128 gm_l48; /* raw */
    gm_b16_8 gm_l49; /* offs */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_tkaapcii86udk gm_l54; /* pl */
    gm_t88rcucg1b7uk gm_l55; /* p */
    gm_t88rcucg1b7uk gm_l56; /* p */
    gm_t88rcucg1b7uk gm_l57; /* inner */
    gm_t88rcucg1b7uk gm_l58; /* p */
    gm_t88rcucg1b7uk gm_l59; /* p */
    gm_t88rcucg1b7uk gm_l60; /* pointee */
    gm_tob1snuii5bme gm_l61; /* r */
    gm_tkaapcii86udk gm_l62; /* start */
    uint64_t gm_l63; /* len */
    gm_t88rcucg1b7uk gm_l64; /* elem */
    uint64_t gm_l65; /* p */
    gm_tob1snuii5bme gm_l66; /* r */
    uint32_t gm_l67; /* f */
    GM_STACK_CHECK(&F, 6564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            switch (t18) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; default: goto ma17; }
ma4:;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t20 != ((uint8_t)0ULL)) goto mn19;
            unsigned __int128 t21 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l6 = t21;
            {
                void * t23 = gm_l0;
                void * t24 = (void *)(((uint8_t *)t23) + 8);
                gm_t88rcucg1b7uk t25 = gm_l2;
                uint64_t t26 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t26;
            }
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t28 = gm_l7;
                    gm_l8 = t28;
                }
                {
                    for (;;) {
                        uint64_t t30 = gm_l9;
                        uint64_t t31 = gm_l8;
                        uint8_t t32 = (t30 < t31);
                        if (!t32) goto lb29;
                        {
                            uint64_t t35 = gm_l9;
                            uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                            gm_l10 = t38;
                            {
                                uint64_t t40;
                                void * t42 = gm_l0;
                                uint8_t t43 = GM_LD(uint8_t, ((((((uint8_t *)t42) + 8) + 8) + 0) + 8));
                                if (!t43) goto ie41;
                                uint64_t t45 = gm_l10;
                                uint64_t t46 = t45;
                                t40 = t46;
                                goto ix41;
ie41:;
                                uint64_t t48 = gm_l7;
                                uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                                if (o50) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 166, 69);
                                uint64_t t51 = r49;
                                uint64_t t52 = gm_l10;
                                uint64_t r53; bool o54 = __builtin_sub_overflow(t51, t52, &r53);
                                if (o54) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 166, 69);
                                uint64_t t55 = r53;
                                uint64_t t56 = t55;
                                t40 = t56;
ix41:;
                                gm_l11 = t40;
                            }
                            {
                                gm_b16_8 t57 = gm_l3;
                                gm_b16_8 t58 = t57;
                                uint64_t t59 = gm_l4;
                                uint64_t t60 = gm_l11;
                                uint64_t r61; bool o62 = __builtin_add_overflow(t59, t60, &r61);
                                if (o62) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 167, 22);
                                uint64_t t63 = r61;
                                uint64_t t64 = (uint64_t)t63;
                                uint64_t t65 = (uintptr_t)t64;
                                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&t58) + 8);
                                if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srcuro7l7pd15c2, 167, 17);
                                unsigned __int128 t67 = gm_l6;
                                uint64_t t68 = gm_l10;
                                uint64_t r69; bool o70 = __builtin_mul_overflow(((uint64_t)8ULL), t68, &r69);
                                if (o70) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 167, 62);
                                uint64_t t71 = r69;
                                unsigned __int128 t72 = (unsigned __int128)t71;
                                unsigned __int128 n73 = (unsigned __int128)t72;
                                if (n73 >= 128) gm_trap_shift((uint64_t)n73, "u128", gm_srcuro7l7pd15c2, 167, 49);
                                unsigned __int128 t74 = (unsigned __int128)(t67 >> (int)n73);
                                uint8_t t75 = (uint8_t)t74;
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t58)) + t65 * 1), t75);
                            }
                            goto me36;
mn37:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 165, 13);
me36:;
                        }
lc29:;
                        {
                            uint64_t t77 = gm_l9;
                            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                            uint64_t t80 = r78;
                            gm_l9 = t80;
                        }
                    }
lb29:;
                }
            }
            goto me3;
mn19:;
ma5:;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t82 != ((uint8_t)1ULL)) goto mn81;
            double t83 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
            gm_l12 = t83;
            {
                void * t85 = gm_l0;
                void * t86 = (void *)(((uint8_t *)t85) + 8);
                gm_t88rcucg1b7uk t87 = gm_l2;
                gm_t53at80pfdus3 t88;
                gm_f_kind_ghvav22((void *)&t88, t86, GM_CONV(gm_csguankko4lmhd, t87));
                uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
                switch (t94) { case ((uint8_t)0ULL): goto ma92; case ((uint8_t)1ULL): goto ma92; case ((uint8_t)2ULL): goto ma92; case ((uint8_t)3ULL): goto ma90; case ((uint8_t)4ULL): goto ma92; case ((uint8_t)5ULL): goto ma92; case ((uint8_t)6ULL): goto ma92; case ((uint8_t)7ULL): goto ma92; case ((uint8_t)8ULL): goto ma92; case ((uint8_t)9ULL): goto ma92; case ((uint8_t)10ULL): goto ma92; case ((uint8_t)11ULL): goto ma92; case ((uint8_t)12ULL): goto ma92; case ((uint8_t)13ULL): goto ma92; case ((uint8_t)14ULL): goto ma92; case ((uint8_t)15ULL): goto ma92; case ((uint8_t)16ULL): goto ma92; case ((uint8_t)17ULL): goto ma92; case ((uint8_t)18ULL): goto ma92; case ((uint8_t)19ULL): goto ma92; case ((uint8_t)20ULL): goto ma92; case ((uint8_t)21ULL): goto ma92; case ((uint8_t)22ULL): goto ma92; case ((uint8_t)23ULL): goto ma92; case ((uint8_t)24ULL): goto ma92; case ((uint8_t)25ULL): goto ma92; case ((uint8_t)26ULL): goto ma92; default: goto ma92; }
ma90:;
                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
                if (t96 != ((uint8_t)3ULL)) goto mn95;
                uint16_t t97 = GM_LD(uint16_t, (((uint8_t *)&t88) + 8));
                if (!(t97 == ((uint16_t)64ULL))) goto mn95;
                void * t98 = gm_l0;
                gm_b16_8 t99 = gm_l3;
                uint64_t t100 = gm_l4;
                double t101 = gm_l12;
                uint64_t t102;
                memset(&t102, 0, sizeof t102); memcpy(&t102, ((uint8_t *)&t101), sizeof t102 < 8 ? sizeof t102 : 8);
                unsigned __int128 t103 = (unsigned __int128)t102;
                gm_f_store_int_mfkaqk5(t98, t99, t100, t103, ((uint64_t)8ULL));
                goto me89;
mn95:;
ma91:;
                uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t88) + 0));
                if (t105 != ((uint8_t)3ULL)) goto mn104;
                uint16_t t106 = GM_LD(uint16_t, (((uint8_t *)&t88) + 8));
                if (!(t106 == ((uint16_t)32ULL))) goto mn104;
                void * t107 = gm_l0;
                gm_b16_8 t108 = gm_l3;
                uint64_t t109 = gm_l4;
                double t110 = gm_l12;
                float t111 = (float)((double)t110);
                uint32_t t112;
                memset(&t112, 0, sizeof t112); memcpy(&t112, ((uint8_t *)&t111), sizeof t112 < 4 ? sizeof t112 : 4);
                unsigned __int128 t113 = (unsigned __int128)t112;
                gm_f_store_int_mfkaqk5(t107, t108, t109, t113, ((uint64_t)4ULL));
                goto me89;
mn104:;
ma92:;
                gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t116;
                GM_ST(void *, (uint8_t *)&t116, (void *)(gm_skff6pclobvnu));
                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(39));
                gm_t08olq97rscn3 t117;
                gm_f_unsupported_531q51r((void *)&t117, (void *)NULL, cx, NULL, t115, t116);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t117);
                goto gm_fdone;
                goto me89;
mn114:;
ma93:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 171, 13);
me89:;
            }
            goto me3;
mn81:;
ma6:;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t119 != ((uint8_t)2ULL)) goto mn118;
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l13 = t120;
            {
                gm_b16_8 t122 = gm_l3;
                gm_b16_8 t123 = t122;
                uint64_t t124 = gm_l4;
                uint64_t t125 = (uint64_t)t124;
                uint64_t t126 = (uintptr_t)t125;
                uint64_t t127 = GM_LD(uintptr_t, ((uint8_t *)&t123) + 8);
                if (t126 >= (uintptr_t)(t127)) gm_trap_index(t126, t127, gm_srcuro7l7pd15c2, 177, 23);
                uint8_t *t128 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t123)) + t126 * 1);
                uint8_t t129;
                uint8_t t131 = gm_l13;
                if (!t131) goto ie130;
                uint8_t t133 = ((uint8_t)1ULL);
                t129 = t133;
                goto ix130;
ie130:;
                uint8_t t135 = ((uint8_t)0ULL);
                t129 = t135;
ix130:;
                GM_ST(uint8_t, t128, t129);
            }
            goto me3;
mn118:;
ma7:;
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t137 != ((uint8_t)6ULL)) goto mn136;
            goto me3;
mn136:;
ma8:;
            uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t140 != ((uint8_t)7ULL)) goto mn139;
            void * t141 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l14 = t141;
            {
                void * t144 = gm_l0;
                void * t145 = (void *)(((uint8_t *)t144) + 8);
                gm_t88rcucg1b7uk t146 = gm_l2;
                gm_t53at80pfdus3 t147;
                gm_f_kind_ghvav22((void *)&t147, t145, GM_CONV(gm_csguankko4lmhd, t146));
                uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&t147) + 0));
                if (t148 != ((uint8_t)13ULL)) goto ie143;
                gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t147) + 8));
                gm_l15 = t149;
                {
                    void * t151 = gm_l0;
                    void * t152 = gm_l14;
                    gm_t88rcucg1b7uk t153 = gm_l15;
                    gm_b16_8 t154 = gm_l3;
                    uint64_t t155 = gm_l4;
                    void * t156 = gm_l5;
                    gm_tqgtd5gtgj65s t157;
                    gm_f_value_bytes_qm8egmj((void *)&t157, (void *)NULL, cx, NULL, t151, &(*(gm_t6evimt84lndq *)((uint8_t *)t152)), t153, t154, t155, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t157))[0] != 0)) {
                        gm_tqgtd5gtgj65s t158;
                        GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                        ((uint8_t *)&t158)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t158 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t157) + 8)));
                        F.ret = t158;
                        goto gm_fret;
                    }
                    gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 8));
                }
                {
                    void * t161 = gm_l0;
                    void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
                    void * t163 = GM_LD(void *, (((uint8_t *)t162) + 0));
                    gm_t88rcucg1b7uk t164 = gm_l15;
                    uint8_t t165 = gm_f_nullable_pointer_7qfmn6o(t163, GM_CONV(gm_csguankko4lmhd, t164));
                    uint8_t t166 = (uint8_t)!t165;
                    if (!t166) goto ie160;
                    {
                        gm_b16_8 t168 = gm_l3;
                        gm_b16_8 t169 = t168;
                        uint64_t t170 = gm_l4;
                        uint64_t t171 = (uintptr_t)((uint64_t)1ULL);
                        void * t172 = gm_l0;
                        void * t173 = (void *)(((uint8_t *)t172) + 8);
                        gm_t88rcucg1b7uk t174 = gm_l2;
                        gm_b16_8 t175 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t173, t174);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&t175) + 8);
                        if (t171 >= (uintptr_t)(t176)) gm_trap_index(t171, t176, gm_srcuro7l7pd15c2, 182, 67);
                        uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t175)) + t171 * 8));
                        uint64_t r178; bool o179 = __builtin_add_overflow(t170, t177, &r178);
                        if (o179) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 182, 61);
                        uint64_t t180 = r178;
                        uint64_t t181 = (uint64_t)t180;
                        uint64_t t182 = (uintptr_t)t181;
                        uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&t169) + 8);
                        if (t182 >= (uintptr_t)(t183)) gm_trap_index(t182, t183, gm_srcuro7l7pd15c2, 182, 56);
                        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t169)) + t182 * 1), ((uint8_t)1ULL));
                    }
                    goto ix160;
ie160:;
ix160:;
                }
                goto ix143;
ie143:;
                {
                    gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t186;
                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sg1uf2uigo01q));
                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(26));
                    gm_t08olq97rscn3 t187;
                    gm_f_unsupported_531q51r((void *)&t187, (void *)NULL, cx, NULL, t185, t186);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t187);
                    goto gm_fdone;
                }
ix143:;
            }
            goto me3;
mn139:;
ma9:;
            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t189 != ((uint8_t)8ULL)) goto mn188;
            void * t190 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l16 = t190;
            {
                void * t193 = gm_l0;
                void * t194 = (void *)(((uint8_t *)t193) + 8);
                gm_t88rcucg1b7uk t195 = gm_l2;
                gm_t53at80pfdus3 t196;
                gm_f_kind_ghvav22((void *)&t196, t194, GM_CONV(gm_csguankko4lmhd, t195));
                uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&t196) + 0));
                if (t197 != ((uint8_t)14ULL)) goto ie192;
                gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t196) + 8) + 4));
                gm_l17 = t198;
                {
                    void * t200 = gm_l0;
                    void * t201 = (void *)(((uint8_t *)t200) + 8);
                    gm_t88rcucg1b7uk t202 = gm_l2;
                    gm_b16_8 t203 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t201, t202);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t203;
                }
                {
                    void * t204 = gm_l0;
                    void * t205 = gm_l16;
                    gm_t88rcucg1b7uk t206 = gm_l17;
                    gm_b16_8 t207 = gm_l3;
                    uint64_t t208 = gm_l4;
                    uint64_t t209 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    if (t209 >= (uintptr_t)(t210)) gm_trap_index(t209, t210, gm_srcuro7l7pd15c2, 187, 50);
                    uint64_t t211 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t209 * 8));
                    uint64_t r212; bool o213 = __builtin_add_overflow(t208, t211, &r212);
                    if (o213) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 187, 44);
                    uint64_t t214 = r212;
                    void * t215 = gm_l5;
                    gm_tqgtd5gtgj65s t216;
                    gm_f_value_bytes_qm8egmj((void *)&t216, (void *)NULL, cx, NULL, t204, &(*(gm_t6evimt84lndq *)((uint8_t *)t205)), t206, t207, t214, t215);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t216))[0] != 0)) {
                        gm_tqgtd5gtgj65s t217;
                        GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                        ((uint8_t *)&t217)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t217 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t216) + 8)));
                        F.ret = t217;
                        goto gm_fret;
                    }
                    gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 8));
                }
                goto ix192;
ie192:;
                {
                    gm_tl874t2atnlh3 t220 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t221;
                    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sb8jvk2vbau9p));
                    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t222;
                    gm_f_unsupported_531q51r((void *)&t222, (void *)NULL, cx, NULL, t220, t221);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t222);
                    goto gm_fdone;
                }
ix192:;
            }
            goto me3;
mn188:;
ma10:;
            uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t224 != ((uint8_t)9ULL)) goto mn223;
            void * t225 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l19 = t225;
            {
                void * t228 = gm_l0;
                void * t229 = (void *)(((uint8_t *)t228) + 8);
                gm_t88rcucg1b7uk t230 = gm_l2;
                gm_t53at80pfdus3 t231;
                gm_f_kind_ghvav22((void *)&t231, t229, GM_CONV(gm_csguankko4lmhd, t230));
                uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&t231) + 0));
                if (t232 != ((uint8_t)14ULL)) goto ie227;
                gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t231) + 8) + 0));
                gm_l20 = t233;
                {
                    void * t235 = gm_l0;
                    void * t236 = (void *)(((uint8_t *)t235) + 8);
                    gm_t88rcucg1b7uk t237 = gm_l2;
                    gm_b16_8 t238 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t236, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l21 = t238;
                }
                {
                    gm_b16_8 t239 = gm_l3;
                    gm_b16_8 t240 = t239;
                    uint64_t t241 = gm_l4;
                    uint64_t t242 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t243 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    if (t242 >= (uintptr_t)(t243)) gm_trap_index(t242, t243, gm_srcuro7l7pd15c2, 192, 24);
                    uint64_t t244 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t242 * 8));
                    uint64_t r245; bool o246 = __builtin_add_overflow(t241, t244, &r245);
                    if (o246) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 192, 18);
                    uint64_t t247 = r245;
                    uint64_t t248 = (uint64_t)t247;
                    uint64_t t249 = (uintptr_t)t248;
                    uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&t240) + 8);
                    if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_srcuro7l7pd15c2, 192, 13);
                    GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t240)) + t249 * 1), ((uint8_t)1ULL));
                }
                {
                    void * t251 = gm_l0;
                    void * t252 = gm_l19;
                    gm_t88rcucg1b7uk t253 = gm_l20;
                    gm_b16_8 t254 = gm_l3;
                    uint64_t t255 = gm_l4;
                    uint64_t t256 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                    if (t256 >= (uintptr_t)(t257)) gm_trap_index(t256, t257, gm_srcuro7l7pd15c2, 193, 51);
                    uint64_t t258 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t256 * 8));
                    uint64_t r259; bool o260 = __builtin_add_overflow(t255, t258, &r259);
                    if (o260) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 193, 45);
                    uint64_t t261 = r259;
                    void * t262 = gm_l5;
                    gm_tqgtd5gtgj65s t263;
                    gm_f_value_bytes_qm8egmj((void *)&t263, (void *)NULL, cx, NULL, t251, &(*(gm_t6evimt84lndq *)((uint8_t *)t252)), t253, t254, t261, t262);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t263))[0] != 0)) {
                        gm_tqgtd5gtgj65s t264;
                        GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                        ((uint8_t *)&t264)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t264 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t263) + 8)));
                        F.ret = t264;
                        goto gm_fret;
                    }
                    gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 8));
                }
                goto ix227;
ie227:;
                {
                    gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t268;
                    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_sb8jvk2vbau9p));
                    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(24));
                    gm_t08olq97rscn3 t269;
                    gm_f_unsupported_531q51r((void *)&t269, (void *)NULL, cx, NULL, t267, t268);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t269);
                    goto gm_fdone;
                }
ix227:;
            }
            goto me3;
mn223:;
ma11:;
            uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t271 != ((uint8_t)10ULL)) goto mn270;
            uint32_t t272 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
            gm_l22 = t272;
            void * t273 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
            gm_l23 = t273;
            {
                void * t276 = gm_l0;
                void * t277 = (void *)(((uint8_t *)t276) + 8);
                gm_t88rcucg1b7uk t278 = gm_l2;
                gm_t53at80pfdus3 t279;
                gm_f_kind_ghvav22((void *)&t279, t277, GM_CONV(gm_csguankko4lmhd, t278));
                uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
                if (t280 != ((uint8_t)17ULL)) goto ie275;
                uint32_t t281 = GM_LD(uint32_t, ((((uint8_t *)&t279) + 8) + 0));
                gm_l24 = t281;
                {
                    void * t284 = gm_l0;
                    void * t285 = GM_LD(void *, (((uint8_t *)t284) + 0));
                    uint32_t t286 = gm_l24;
                    void * t287 = gm_f_adt_mpqv8qe(t285, t286);
                    uint8_t t288 = GM_LD(uint8_t, ((((uint8_t *)t287) + 24) + 0));
                    if (t288 != ((uint8_t)1ULL)) goto ie283;
                    gm_t88rcucg1b7uk t289 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t287) + 24) + 8) + 0));
                    gm_l25 = t289;
                    gm_b16_8 t290 = GM_LD(gm_b16_8, (((((uint8_t *)t287) + 24) + 8) + 8));
                    gm_l26 = t290;
                    {
                        gm_b16_8 t292 = gm_l26;
                        gm_b16_8 t293 = t292;
                        uint32_t t294 = gm_l22;
                        uint64_t t295 = (uint64_t)t294;
                        uint64_t t296 = (uintptr_t)t295;
                        uint64_t t297 = GM_LD(uintptr_t, ((uint8_t *)&t293) + 8);
                        if (t296 >= (uintptr_t)(t297)) gm_trap_index(t296, t297, gm_srcuro7l7pd15c2, 198, 19);
                        gm_tckdobunaulee t298 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t293)) + t296 * 72));
                        F.l27 = t298;
                    }
                    {
                        void * t299 = gm_l0;
                        void * t300 = (void *)(((uint8_t *)t299) + 8);
                        gm_t88rcucg1b7uk t301 = gm_l2;
                        gm_b16_8 t302 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t300, t301);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l28 = t302;
                    }
                    {
                        gm_ta24rd13gupia t303;
                        gm_f_to_i128_o7ud1ue((void *)&t303, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 24)));
                        __int128 t304;
                        if (((((uint8_t *)&t303))[16] != 0)) {
                            __int128 t305 = GM_LD(__int128, (((uint8_t *)&t303) + 0));
                            t304 = t305;
                        } else {
                            t304 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                        }
                        gm_l29 = t304;
                    }
                    {
                        void * t306 = gm_l0;
                        __int128 t307 = gm_l29;
                        unsigned __int128 t308 = (unsigned __int128)t307;
                        gm_t6evimt84lndq t309;
                        GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t309 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t309 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t309 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t309 + 16), t308);
                        gm_t88rcucg1b7uk t310 = gm_l25;
                        gm_b16_8 t311 = gm_l3;
                        uint64_t t312 = gm_l4;
                        uint64_t t313 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t314 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        if (t313 >= (uintptr_t)(t314)) gm_trap_index(t313, t314, gm_srcuro7l7pd15c2, 201, 79);
                        uint64_t t315 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t313 * 8));
                        uint64_t r316; bool o317 = __builtin_add_overflow(t312, t315, &r316);
                        if (o317) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 201, 73);
                        uint64_t t318 = r316;
                        void * t319 = gm_l5;
                        gm_tqgtd5gtgj65s t320;
                        gm_f_value_bytes_qm8egmj((void *)&t320, (void *)NULL, cx, NULL, t306, &t309, t310, t311, t318, t319);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t320))[0] != 0)) {
                            gm_tqgtd5gtgj65s t321;
                            GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
                            ((uint8_t *)&t321)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t321 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t320) + 8)));
                            F.ret = t321;
                            goto gm_fret;
                        }
                        gm_b0_1 t322 = GM_LD(gm_b0_1, (((uint8_t *)&t320) + 8));
                    }
                    {
                        void * t324 = gm_l23;
                        if (!(GM_LD(void *, ((uint8_t *)&t324)) != 0)) goto ie323;
                        void * t325 = GM_LD(void *, ((uint8_t *)&t324));
                        gm_l30 = t325;
                        {
                            gm_tkn42u7na3t81 t327 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l27) + 16));
                            gm_t88rcucg1b7uk t328;
                            if (((((uint8_t *)&t327))[4] != 0)) {
                                gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t327) + 0));
                                t328 = t329;
                            } else {
                                gm_tl874t2atnlh3 t330 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                gm_b16_8 t331;
                                GM_ST(void *, (uint8_t *)&t331, (void *)(gm_sa6mes8ijqmmk));
                                GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(29));
                                gm_t08olq97rscn3 t332;
                                gm_f_unsupported_531q51r((void *)&t332, (void *)NULL, cx, NULL, t330, t331);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t332);
                                goto gm_fdone;
                            }
                            gm_l31 = t328;
                        }
                        {
                            void * t333 = gm_l0;
                            void * t334 = gm_l30;
                            gm_t88rcucg1b7uk t335 = gm_l31;
                            gm_b16_8 t336 = gm_l3;
                            uint64_t t337 = gm_l4;
                            uint64_t t338 = (uintptr_t)((uint64_t)1ULL);
                            uint64_t t339 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                            if (t338 >= (uintptr_t)(t339)) gm_trap_index(t338, t339, gm_srcuro7l7pd15c2, 204, 54);
                            uint64_t t340 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t338 * 8));
                            uint64_t r341; bool o342 = __builtin_add_overflow(t337, t340, &r341);
                            if (o342) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 204, 48);
                            uint64_t t343 = r341;
                            void * t344 = gm_l5;
                            gm_tqgtd5gtgj65s t345;
                            gm_f_value_bytes_qm8egmj((void *)&t345, (void *)NULL, cx, NULL, t333, &(*(gm_t6evimt84lndq *)((uint8_t *)t334)), t335, t336, t343, t344);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (((((uint8_t *)&t345))[0] != 0)) {
                                gm_tqgtd5gtgj65s t346;
                                GM_ST(uint8_t, (uint8_t *)&t346 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t346 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t346 + 4, 0);
                                ((uint8_t *)&t346)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t346 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t345) + 8)));
                                F.ret = t346;
                                goto gm_fret;
                            }
                            gm_b0_1 t347 = GM_LD(gm_b0_1, (((uint8_t *)&t345) + 8));
                        }
                        goto ix323;
ie323:;
ix323:;
                    }
                    goto ix283;
ie283:;
                    {
                        gm_tl874t2atnlh3 t349 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        gm_b16_8 t350;
                        GM_ST(void *, (uint8_t *)&t350, (void *)(gm_sa6mes8ijqmmk));
                        GM_ST(uintptr_t, (uint8_t *)&t350 + 8, (uintptr_t)(29));
                        gm_t08olq97rscn3 t351;
                        gm_f_unsupported_531q51r((void *)&t351, (void *)NULL, cx, NULL, t349, t350);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t351);
                        goto gm_fdone;
                    }
ix283:;
                }
                goto ix275;
ie275:;
                {
                    gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t354;
                    GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sa6mes8ijqmmk));
                    GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(29));
                    gm_t08olq97rscn3 t355;
                    gm_f_unsupported_531q51r((void *)&t355, (void *)NULL, cx, NULL, t353, t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t355);
                    goto gm_fdone;
                }
ix275:;
            }
            goto me3;
mn270:;
ma12:;
            uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t357 != ((uint8_t)3ULL)) goto mn356;
            void * t358 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l32 = t358;
            {
                void * t360 = gm_l0;
                void * t361 = (void *)(((uint8_t *)t360) + 8);
                gm_t88rcucg1b7uk t362 = gm_l2;
                gm_t53at80pfdus3 t363;
                gm_f_kind_ghvav22((void *)&t363, t361, GM_CONV(gm_csguankko4lmhd, t362));
                uint8_t t370 = GM_LD(uint8_t, (((uint8_t *)&t363) + 0));
                switch (t370) { case ((uint8_t)0ULL): goto ma368; case ((uint8_t)1ULL): goto ma368; case ((uint8_t)2ULL): goto ma368; case ((uint8_t)3ULL): goto ma368; case ((uint8_t)4ULL): goto ma368; case ((uint8_t)5ULL): goto ma368; case ((uint8_t)6ULL): goto ma368; case ((uint8_t)7ULL): goto ma368; case ((uint8_t)8ULL): goto ma368; case ((uint8_t)9ULL): goto ma368; case ((uint8_t)10ULL): goto ma368; case ((uint8_t)11ULL): goto ma365; case ((uint8_t)12ULL): goto ma366; case ((uint8_t)13ULL): goto ma368; case ((uint8_t)14ULL): goto ma368; case ((uint8_t)15ULL): goto ma368; case ((uint8_t)16ULL): goto ma368; case ((uint8_t)17ULL): goto ma367; case ((uint8_t)18ULL): goto ma368; case ((uint8_t)19ULL): goto ma368; case ((uint8_t)20ULL): goto ma368; case ((uint8_t)21ULL): goto ma368; case ((uint8_t)22ULL): goto ma368; case ((uint8_t)23ULL): goto ma368; case ((uint8_t)24ULL): goto ma368; case ((uint8_t)25ULL): goto ma368; case ((uint8_t)26ULL): goto ma368; default: goto ma368; }
ma365:;
                uint8_t t372 = GM_LD(uint8_t, (((uint8_t *)&t363) + 0));
                if (t372 != ((uint8_t)11ULL)) goto mn371;
                gm_t88rcucg1b7uk t373 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t363) + 8) + 8));
                gm_l33 = t373;
                {
                    void * t375 = gm_l0;
                    void * t376 = (void *)(((uint8_t *)t375) + 8);
                    gm_t88rcucg1b7uk t377 = gm_l33;
                    uint64_t t378 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t376, t377);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l34 = t378;
                }
                {
                    {
                        void * t380 = gm_l32;
                        gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)t380) + 0));
                        gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
                        gm_l35 = t382;
                    }
                    {
                        gm_l36 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t384 = gm_l36;
                            uint64_t t385 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                            uint8_t t386 = (t384 < t385);
                            if (!t386) goto lb383;
                            {
                                gm_thf8as07kj3uj t389;
                                memset((uint8_t *)&t389 + 8, 0, 8);
                                uint64_t t390 = gm_l36;
                                GM_ST(uint64_t, (uint8_t *)&t389 + 0, t390);
                                uint64_t t391 = gm_l36;
                                uint64_t t392 = (uintptr_t)t391;
                                uint64_t t393 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                memcpy((uint8_t *)&t389 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t392 * 48), 48);
                                uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&t389) + 0));
                                gm_l37 = t396;
                                F.l38 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t389) + 16));
                                {
                                    void * t398 = gm_l0;
                                    gm_t6evimt84lndq t399 = F.l38;
                                    gm_t88rcucg1b7uk t400 = gm_l33;
                                    gm_b16_8 t401 = gm_l3;
                                    uint64_t t402 = gm_l4;
                                    uint64_t t403 = gm_l37;
                                    uint64_t t404 = (uint64_t)t403;
                                    uint64_t t405 = gm_l34;
                                    uint64_t r406; bool o407 = __builtin_mul_overflow(t404, t405, &r406);
                                    if (o407) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 211, 92);
                                    uint64_t t408 = r406;
                                    uint64_t r409; bool o410 = __builtin_add_overflow(t402, t408, &r409);
                                    if (o410) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 211, 86);
                                    uint64_t t411 = r409;
                                    void * t412 = gm_l5;
                                    gm_tqgtd5gtgj65s t413;
                                    gm_f_value_bytes_qm8egmj((void *)&t413, (void *)NULL, cx, NULL, t398, &t399, t400, t401, t411, t412);
                                    if (gm_unw.kind) {
                                        goto gm_ll387;
                                    }
                                    if (((((uint8_t *)&t413))[0] != 0)) {
                                        gm_tqgtd5gtgj65s t414;
                                        GM_ST(uint8_t, (uint8_t *)&t414 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t414 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t414 + 4, 0);
                                        ((uint8_t *)&t414)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t414 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t413) + 8)));
                                        F.ret = t414;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t415 = GM_LD(gm_b0_1, (((uint8_t *)&t413) + 8));
                                }
                                goto me394;
mn395:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 211, 21);
me394:;
                            }
lc383:;
                            {
                                uint64_t t417 = gm_l36;
                                uint64_t r418; bool o419 = __builtin_add_overflow(t417, ((uint64_t)1ULL), &r418);
                                uint64_t t420 = r418;
                                gm_l36 = t420;
                            }
                        }
gm_ll387:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb383; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc383; }
                        }
                        goto gm_lad;
lb383:;
                    }
                }
                goto me364;
mn371:;
ma366:;
                uint8_t t422 = GM_LD(uint8_t, (((uint8_t *)&t363) + 0));
                if (t422 != ((uint8_t)12ULL)) goto mn421;
                gm_b16_8 t423 = GM_LD(gm_b16_8, ((((uint8_t *)&t363) + 8) + 0));
                gm_l39 = t423;
                {
                    void * t425 = gm_l0;
                    void * t426 = (void *)(((uint8_t *)t425) + 8);
                    gm_t88rcucg1b7uk t427 = gm_l2;
                    gm_b16_8 t428 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t426, t427);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l40 = t428;
                }
                {
                    {
                        void * t430 = gm_l32;
                        gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)t430) + 0));
                        gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                        gm_l41 = t432;
                    }
                    {
                        gm_l42 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t434 = gm_l42;
                            uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                            uint8_t t436 = (t434 < t435);
                            if (!t436) goto lb433;
                            {
                                gm_thf8as07kj3uj t439;
                                memset((uint8_t *)&t439 + 8, 0, 8);
                                uint64_t t440 = gm_l42;
                                GM_ST(uint64_t, (uint8_t *)&t439 + 0, t440);
                                uint64_t t441 = gm_l42;
                                uint64_t t442 = (uintptr_t)t441;
                                uint64_t t443 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                                memcpy((uint8_t *)&t439 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t442 * 48), 48);
                                uint64_t t446 = GM_LD(uint64_t, (((uint8_t *)&t439) + 0));
                                gm_l43 = t446;
                                F.l44 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t439) + 16));
                                {
                                    void * t448 = gm_l0;
                                    uint64_t t449 = gm_l43;
                                    uint64_t t450 = (uintptr_t)t449;
                                    uint64_t t451 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                                    if (t450 >= (uintptr_t)(t451)) gm_trap_index(t450, t451, gm_srcuro7l7pd15c2, 215, 75);
                                    gm_t88rcucg1b7uk t452 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t450 * 4));
                                    gm_b16_8 t453 = gm_l3;
                                    uint64_t t454 = gm_l4;
                                    uint64_t t455 = gm_l43;
                                    uint64_t t456 = (uintptr_t)t455;
                                    uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    if (t456 >= (uintptr_t)(t457)) gm_trap_index(t456, t457, gm_srcuro7l7pd15c2, 215, 96);
                                    uint64_t t458 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t456 * 8));
                                    uint64_t r459; bool o460 = __builtin_add_overflow(t454, t458, &r459);
                                    if (o460) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 215, 90);
                                    uint64_t t461 = r459;
                                    void * t462 = gm_l5;
                                    gm_tqgtd5gtgj65s t463;
                                    gm_f_value_bytes_qm8egmj((void *)&t463, (void *)NULL, cx, NULL, t448, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l44)), t452, t453, t461, t462);
                                    if (gm_unw.kind) {
                                        goto gm_ll437;
                                    }
                                    if (((((uint8_t *)&t463))[0] != 0)) {
                                        gm_tqgtd5gtgj65s t464;
                                        GM_ST(uint8_t, (uint8_t *)&t464 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t464 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t464 + 4, 0);
                                        ((uint8_t *)&t464)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t464 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t463) + 8)));
                                        F.ret = t464;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t465 = GM_LD(gm_b0_1, (((uint8_t *)&t463) + 8));
                                }
                                goto me444;
mn445:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 215, 21);
me444:;
                            }
lc433:;
                            {
                                uint64_t t467 = gm_l42;
                                uint64_t r468; bool o469 = __builtin_add_overflow(t467, ((uint64_t)1ULL), &r468);
                                uint64_t t470 = r468;
                                gm_l42 = t470;
                            }
                        }
gm_ll437:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb433; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc433; }
                        }
                        goto gm_lad;
lb433:;
                    }
                }
                goto me364;
mn421:;
ma367:;
                uint8_t t472 = GM_LD(uint8_t, (((uint8_t *)&t363) + 0));
                if (t472 != ((uint8_t)17ULL)) goto mn471;
                uint32_t t473 = GM_LD(uint32_t, ((((uint8_t *)&t363) + 8) + 0));
                gm_l45 = t473;
                void * t474 = gm_l0;
                void * t475 = GM_LD(void *, (((uint8_t *)t474) + 0));
                uint32_t t476 = gm_l45;
                void * t477 = gm_f_adt_mpqv8qe(t475, t476);
                uint8_t t480 = GM_LD(uint8_t, ((((uint8_t *)t477) + 24) + 0));
                if (t480 != ((uint8_t)0ULL)) goto mn479;
                gm_tpautr4br7h3l t481 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t477) + 24) + 8));
                F.l46 = t481;
                {
                    if (!(((((uint8_t *)&F.l46) + 40))[4] != 0)) goto ie483;
                    gm_t88rcucg1b7uk t484 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l46) + 40));
                    gm_l47 = t484;
                    {
                        void * t486 = gm_l0;
                        gm_t88rcucg1b7uk t487 = gm_l2;
                        gm_t3d480iurj7vi t488;
                        gm_f_bits_of_ilgn83m((void *)&t488, (void *)NULL, cx, NULL, t486, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l1)), t487);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t488))[0] != 0)) {
                            gm_tqgtd5gtgj65s t489;
                            GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
                            ((uint8_t *)&t489)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t489 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t488) + 16)));
                            F.ret = t489;
                            goto gm_fret;
                        }
                        unsigned __int128 t490 = GM_LD(unsigned __int128, (((uint8_t *)&t488) + 16));
                        gm_l48 = t490;
                    }
                    {
                        void * t491 = gm_l0;
                        unsigned __int128 t492 = gm_l48;
                        gm_t6evimt84lndq t493;
                        GM_ST(uint8_t, (uint8_t *)&t493 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t493 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t493 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t493 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t493 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t493 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t493 + 0), ((uint8_t)0ULL));
                        GM_ST(unsigned __int128, ((uint8_t *)&t493 + 16), t492);
                        gm_t88rcucg1b7uk t494 = gm_l47;
                        gm_b16_8 t495 = gm_l3;
                        uint64_t t496 = gm_l4;
                        void * t497 = gm_l5;
                        gm_f_value_bytes_qm8egmj((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t491, &t493, t494, t495, t496, t497);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    goto ix483;
ie483:;
ix483:;
                }
                {
                    void * t498 = gm_l0;
                    void * t499 = (void *)(((uint8_t *)t498) + 8);
                    gm_t88rcucg1b7uk t500 = gm_l2;
                    gm_b16_8 t501 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t499, t500);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t501;
                }
                {
                    {
                        void * t503 = gm_l32;
                        gm_b16_8 t504 = GM_LD(gm_b16_8, (((uint8_t *)t503) + 0));
                        gm_b16_8 t505 = GM_LD(gm_b16_8, ((uint8_t *)&t504));
                        gm_l50 = t505;
                    }
                    {
                        gm_l51 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t507 = gm_l51;
                            uint64_t t508 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                            uint8_t t509 = (t507 < t508);
                            if (!t509) goto lb506;
                            {
                                gm_thf8as07kj3uj t512;
                                memset((uint8_t *)&t512 + 8, 0, 8);
                                uint64_t t513 = gm_l51;
                                GM_ST(uint64_t, (uint8_t *)&t512 + 0, t513);
                                uint64_t t514 = gm_l51;
                                uint64_t t515 = (uintptr_t)t514;
                                uint64_t t516 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                                memcpy((uint8_t *)&t512 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t515 * 48), 48);
                                uint64_t t519 = GM_LD(uint64_t, (((uint8_t *)&t512) + 0));
                                gm_l52 = t519;
                                F.l53 = (*(gm_t6evimt84lndq *)(((uint8_t *)&t512) + 16));
                                {
                                    void * t521 = gm_l0;
                                    uint64_t t522 = gm_l52;
                                    uint64_t t523 = (uintptr_t)t522;
                                    uint64_t t524 = GM_LD(uintptr_t, (((uint8_t *)&F.l46) + 0) + 8);
                                    if (t523 >= (uintptr_t)(t524)) gm_trap_index(t523, t524, gm_srcuro7l7pd15c2, 224, 79);
                                    gm_t88rcucg1b7uk t525 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l46) + 0)) + t523 * 40) + 16));
                                    gm_b16_8 t526 = gm_l3;
                                    uint64_t t527 = gm_l4;
                                    uint64_t t528 = gm_l52;
                                    uint64_t t529 = (uintptr_t)t528;
                                    uint64_t t530 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                                    if (t529 >= (uintptr_t)(t530)) gm_trap_index(t529, t530, gm_srcuro7l7pd15c2, 224, 107);
                                    uint64_t t531 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l49)) + t529 * 8));
                                    uint64_t r532; bool o533 = __builtin_add_overflow(t527, t531, &r532);
                                    if (o533) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 224, 101);
                                    uint64_t t534 = r532;
                                    void * t535 = gm_l5;
                                    gm_tqgtd5gtgj65s t536;
                                    gm_f_value_bytes_qm8egmj((void *)&t536, (void *)NULL, cx, NULL, t521, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l53)), t525, t526, t534, t535);
                                    if (gm_unw.kind) {
                                        goto gm_ll510;
                                    }
                                    if (((((uint8_t *)&t536))[0] != 0)) {
                                        gm_tqgtd5gtgj65s t537;
                                        GM_ST(uint8_t, (uint8_t *)&t537 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t537 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t537 + 4, 0);
                                        ((uint8_t *)&t537)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t537 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t536) + 8)));
                                        F.ret = t537;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t538 = GM_LD(gm_b0_1, (((uint8_t *)&t536) + 8));
                                }
                                goto me517;
mn518:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 224, 25);
me517:;
                            }
lc506:;
                            {
                                uint64_t t540 = gm_l51;
                                uint64_t r541; bool o542 = __builtin_add_overflow(t540, ((uint64_t)1ULL), &r541);
                                uint64_t t543 = r541;
                                gm_l51 = t543;
                            }
                        }
gm_ll510:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb506; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc506; }
                        }
                        goto gm_lad;
lb506:;
                    }
                }
                goto me478;
mn479:;
                gm_tl874t2atnlh3 t545 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t546;
                GM_ST(void *, (uint8_t *)&t546, (void *)(gm_s67fd8s6hf84e));
                GM_ST(uintptr_t, (uint8_t *)&t546 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t547;
                gm_f_unsupported_531q51r((void *)&t547, (void *)NULL, cx, NULL, t545, t546);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t547);
                goto gm_fdone;
                goto me478;
mn544:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 217, 32);
me478:;
                goto me364;
mn471:;
ma368:;
                gm_tl874t2atnlh3 t549 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t550;
                GM_ST(void *, (uint8_t *)&t550, (void *)(gm_s67fd8s6hf84e));
                GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t551;
                gm_f_unsupported_531q51r((void *)&t551, (void *)NULL, cx, NULL, t549, t550);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t551);
                goto gm_fdone;
                goto me364;
mn548:;
ma369:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 208, 13);
me364:;
            }
            goto me3;
mn356:;
ma13:;
            uint8_t t553 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t553 != ((uint8_t)4ULL)) goto mn552;
            gm_tkaapcii86udk t554 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
            gm_l54 = t554;
            {
                void * t556 = gm_l0;
                void * t557 = (void *)(((uint8_t *)t556) + 8);
                gm_t88rcucg1b7uk t558 = gm_l2;
                gm_t53at80pfdus3 t559;
                gm_f_kind_ghvav22((void *)&t559, t557, GM_CONV(gm_csguankko4lmhd, t558));
                gm_t88rcucg1b7uk t560;
                uint8_t t567 = GM_LD(uint8_t, (((uint8_t *)&t559) + 0));
                switch (t567) { case ((uint8_t)0ULL): goto ma565; case ((uint8_t)1ULL): goto ma565; case ((uint8_t)2ULL): goto ma565; case ((uint8_t)3ULL): goto ma565; case ((uint8_t)4ULL): goto ma565; case ((uint8_t)5ULL): goto ma565; case ((uint8_t)6ULL): goto ma565; case ((uint8_t)7ULL): goto ma565; case ((uint8_t)8ULL): goto ma562; case ((uint8_t)9ULL): goto ma563; case ((uint8_t)10ULL): goto ma565; case ((uint8_t)11ULL): goto ma565; case ((uint8_t)12ULL): goto ma565; case ((uint8_t)13ULL): goto ma564; case ((uint8_t)14ULL): goto ma565; case ((uint8_t)15ULL): goto ma565; case ((uint8_t)16ULL): goto ma565; case ((uint8_t)17ULL): goto ma565; case ((uint8_t)18ULL): goto ma565; case ((uint8_t)19ULL): goto ma565; case ((uint8_t)20ULL): goto ma565; case ((uint8_t)21ULL): goto ma565; case ((uint8_t)22ULL): goto ma565; case ((uint8_t)23ULL): goto ma565; case ((uint8_t)24ULL): goto ma565; case ((uint8_t)25ULL): goto ma565; case ((uint8_t)26ULL): goto ma565; default: goto ma565; }
ma562:;
                uint8_t t569 = GM_LD(uint8_t, (((uint8_t *)&t559) + 0));
                if (t569 != ((uint8_t)8ULL)) goto mn568;
                gm_t88rcucg1b7uk t570 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t559) + 8) + 4));
                gm_l55 = t570;
                gm_t88rcucg1b7uk t571 = gm_l55;
                t560 = t571;
                goto me561;
mn568:;
ma563:;
                uint8_t t573 = GM_LD(uint8_t, (((uint8_t *)&t559) + 0));
                if (t573 != ((uint8_t)9ULL)) goto mn572;
                gm_t88rcucg1b7uk t574 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t559) + 8) + 4));
                gm_l56 = t574;
                gm_t88rcucg1b7uk t575 = gm_l56;
                t560 = t575;
                goto me561;
mn572:;
ma564:;
                uint8_t t577 = GM_LD(uint8_t, (((uint8_t *)&t559) + 0));
                if (t577 != ((uint8_t)13ULL)) goto mn576;
                gm_t88rcucg1b7uk t578 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t559) + 8));
                gm_l57 = t578;
                void * t579 = gm_l0;
                void * t580 = (void *)(((uint8_t *)t579) + 8);
                gm_t88rcucg1b7uk t581 = gm_l57;
                gm_t53at80pfdus3 t582;
                gm_f_kind_ghvav22((void *)&t582, t580, GM_CONV(gm_csguankko4lmhd, t581));
                gm_t88rcucg1b7uk t583;
                uint8_t t589 = GM_LD(uint8_t, (((uint8_t *)&t582) + 0));
                switch (t589) { case ((uint8_t)0ULL): goto ma587; case ((uint8_t)1ULL): goto ma587; case ((uint8_t)2ULL): goto ma587; case ((uint8_t)3ULL): goto ma587; case ((uint8_t)4ULL): goto ma587; case ((uint8_t)5ULL): goto ma587; case ((uint8_t)6ULL): goto ma587; case ((uint8_t)7ULL): goto ma587; case ((uint8_t)8ULL): goto ma585; case ((uint8_t)9ULL): goto ma586; case ((uint8_t)10ULL): goto ma587; case ((uint8_t)11ULL): goto ma587; case ((uint8_t)12ULL): goto ma587; case ((uint8_t)13ULL): goto ma587; case ((uint8_t)14ULL): goto ma587; case ((uint8_t)15ULL): goto ma587; case ((uint8_t)16ULL): goto ma587; case ((uint8_t)17ULL): goto ma587; case ((uint8_t)18ULL): goto ma587; case ((uint8_t)19ULL): goto ma587; case ((uint8_t)20ULL): goto ma587; case ((uint8_t)21ULL): goto ma587; case ((uint8_t)22ULL): goto ma587; case ((uint8_t)23ULL): goto ma587; case ((uint8_t)24ULL): goto ma587; case ((uint8_t)25ULL): goto ma587; case ((uint8_t)26ULL): goto ma587; default: goto ma587; }
ma585:;
                uint8_t t591 = GM_LD(uint8_t, (((uint8_t *)&t582) + 0));
                if (t591 != ((uint8_t)8ULL)) goto mn590;
                gm_t88rcucg1b7uk t592 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t582) + 8) + 4));
                gm_l58 = t592;
                gm_t88rcucg1b7uk t593 = gm_l58;
                t583 = t593;
                goto me584;
mn590:;
ma586:;
                uint8_t t595 = GM_LD(uint8_t, (((uint8_t *)&t582) + 0));
                if (t595 != ((uint8_t)9ULL)) goto mn594;
                gm_t88rcucg1b7uk t596 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t582) + 8) + 4));
                gm_l59 = t596;
                gm_t88rcucg1b7uk t597 = gm_l59;
                t583 = t597;
                goto me584;
mn594:;
ma587:;
                gm_tl874t2atnlh3 t599 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t600;
                GM_ST(void *, (uint8_t *)&t600, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t600 + 8, (uintptr_t)(35));
                gm_t08olq97rscn3 t601;
                gm_f_unsupported_531q51r((void *)&t601, (void *)NULL, cx, NULL, t599, t600);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t601);
                goto gm_fdone;
                goto me584;
mn598:;
ma588:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 235, 37);
me584:;
                t560 = t583;
                goto me561;
mn576:;
ma565:;
                gm_tl874t2atnlh3 t603 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t604;
                GM_ST(void *, (uint8_t *)&t604, (void *)(gm_sgiqpj781s1a9));
                GM_ST(uintptr_t, (uint8_t *)&t604 + 8, (uintptr_t)(35));
                gm_t08olq97rscn3 t605;
                gm_f_unsupported_531q51r((void *)&t605, (void *)NULL, cx, NULL, t603, t604);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t605);
                goto gm_fdone;
                goto me561;
mn602:;
ma566:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 232, 27);
me561:;
                gm_l60 = t560;
            }
            {
                void * t606 = gm_l0;
                gm_t88rcucg1b7uk t607 = gm_l60;
                uint64_t t608 = gm_l4;
                gm_tt9fudfifskca t609;
                gm_f_place_reloc_blfij5d((void *)&t609, (void *)NULL, cx, NULL, t606, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l54)), t607, t608);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t609))[0] != 0)) {
                    gm_tqgtd5gtgj65s t610;
                    GM_ST(uint8_t, (uint8_t *)&t610 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t610 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t610 + 4, 0);
                    ((uint8_t *)&t610)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t610 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t609) + 8)));
                    F.ret = t610;
                    goto gm_fret;
                }
                gm_l61 = (*(gm_tob1snuii5bme *)(((uint8_t *)&t609) + 8));
            }
            {
                void * t611 = gm_l5;
                gm_tftb1odc739t5 t612 = gm_f_push_vgtfcjf(t611, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l61)));
                if (((((uint8_t *)&t612))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t612) + 1, gm_srcuro7l7pd15c2, 243, 13);
                gm_b0_1 t613 = GM_LD(gm_b0_1, (((uint8_t *)&t612) + 1));
            }
            goto me3;
mn552:;
ma14:;
            uint8_t t615 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t615 != ((uint8_t)5ULL)) goto mn614;
            gm_tkaapcii86udk t616 = GM_LD(gm_tkaapcii86udk, ((((uint8_t *)&F.l1) + 16) + 0));
            gm_l62 = t616;
            uint64_t t617 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 16) + 24));
            gm_l63 = t617;
            {
                void * t620 = gm_l0;
                void * t621 = (void *)(((uint8_t *)t620) + 8);
                gm_t88rcucg1b7uk t622 = gm_l2;
                gm_t53at80pfdus3 t623;
                gm_f_kind_ghvav22((void *)&t623, t621, GM_CONV(gm_csguankko4lmhd, t622));
                uint8_t t624 = GM_LD(uint8_t, (((uint8_t *)&t623) + 0));
                if (t624 != ((uint8_t)10ULL)) goto ie619;
                gm_t88rcucg1b7uk t625 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t623) + 8) + 4));
                gm_l64 = t625;
                {
                    void * t627 = gm_l0;
                    uint64_t t628 = GM_LD(uint64_t, ((((((uint8_t *)t627) + 8) + 8) + 0) + 0));
                    gm_l65 = t628;
                }
                {
                    uint8_t t630 = 0;
                    uint64_t t631 = gm_l63;
                    uint8_t t632 = (t631 > ((uint64_t)0ULL));
                    if (!t632) {
                        uint64_t t633 = GM_LD(uintptr_t, (((uint8_t *)&gm_l62) + 8) + 8);
                        uint8_t t634 = (t633 > ((uint64_t)0ULL));
                        t630 = t634;
                    } else t630 = 1;
                    if (!t630) goto ie629;
                    {
                        void * t636 = gm_l0;
                        gm_t88rcucg1b7uk t637 = gm_l64;
                        uint64_t t638 = gm_l4;
                        gm_tt9fudfifskca t639;
                        gm_f_place_reloc_blfij5d((void *)&t639, (void *)NULL, cx, NULL, t636, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l62)), t637, t638);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t639))[0] != 0)) {
                            gm_tqgtd5gtgj65s t640;
                            GM_ST(uint8_t, (uint8_t *)&t640 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t640 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t640 + 4, 0);
                            ((uint8_t *)&t640)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t640 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t639) + 8)));
                            F.ret = t640;
                            goto gm_fret;
                        }
                        gm_l66 = (*(gm_tob1snuii5bme *)(((uint8_t *)&t639) + 8));
                    }
                    {
                        void * t641 = gm_l5;
                        gm_tftb1odc739t5 t642 = gm_f_push_vgtfcjf(t641, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l66)));
                        if (((((uint8_t *)&t642))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t642) + 1, gm_srcuro7l7pd15c2, 250, 17);
                        gm_b0_1 t643 = GM_LD(gm_b0_1, (((uint8_t *)&t642) + 1));
                    }
                    goto ix629;
ie629:;
ix629:;
                }
                {
                    void * t644 = gm_l0;
                    gm_b16_8 t645 = gm_l3;
                    uint64_t t646 = gm_l4;
                    uint64_t t647 = gm_l65;
                    uint64_t r648; bool o649 = __builtin_add_overflow(t646, t647, &r648);
                    if (o649) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 252, 30);
                    uint64_t t650 = r648;
                    uint64_t t651 = gm_l63;
                    unsigned __int128 t652 = (unsigned __int128)t651;
                    uint64_t t653 = gm_l65;
                    gm_f_store_int_mfkaqk5(t644, t645, t650, t652, t653);
                }
                goto ix619;
ie619:;
                {
                    gm_tl874t2atnlh3 t655 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_b16_8 t656;
                    GM_ST(void *, (uint8_t *)&t656, (void *)(gm_svlcdvcv2bp94));
                    GM_ST(uintptr_t, (uint8_t *)&t656 + 8, (uintptr_t)(23));
                    gm_t08olq97rscn3 t657;
                    gm_f_unsupported_531q51r((void *)&t657, (void *)NULL, cx, NULL, t655, t656);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t657);
                    goto gm_fdone;
                }
ix619:;
            }
            goto me3;
mn614:;
ma15:;
            uint8_t t659 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t659 != ((uint8_t)11ULL)) goto mn658;
            uint32_t t660 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l67 = t660;
            {
                void * t662 = gm_l0;
                uint32_t t663 = gm_l67;
                gm_b0_1 t664 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t662, t663);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t665 = gm_l0;
                void * t666 = (void *)(((uint8_t *)t665) + 8);
                gm_t88rcucg1b7uk t667 = gm_l2;
                gm_t53at80pfdus3 t668;
                gm_f_kind_ghvav22((void *)&t668, t666, GM_CONV(gm_csguankko4lmhd, t667));
                uint8_t t674 = GM_LD(uint8_t, (((uint8_t *)&t668) + 0));
                switch (t674) { case ((uint8_t)0ULL): goto ma672; case ((uint8_t)1ULL): goto ma672; case ((uint8_t)2ULL): goto ma672; case ((uint8_t)3ULL): goto ma672; case ((uint8_t)4ULL): goto ma672; case ((uint8_t)5ULL): goto ma672; case ((uint8_t)6ULL): goto ma672; case ((uint8_t)7ULL): goto ma672; case ((uint8_t)8ULL): goto ma672; case ((uint8_t)9ULL): goto ma672; case ((uint8_t)10ULL): goto ma672; case ((uint8_t)11ULL): goto ma672; case ((uint8_t)12ULL): goto ma672; case ((uint8_t)13ULL): goto ma671; case ((uint8_t)14ULL): goto ma672; case ((uint8_t)15ULL): goto ma672; case ((uint8_t)16ULL): goto ma670; case ((uint8_t)17ULL): goto ma672; case ((uint8_t)18ULL): goto ma672; case ((uint8_t)19ULL): goto ma672; case ((uint8_t)20ULL): goto ma672; case ((uint8_t)21ULL): goto ma672; case ((uint8_t)22ULL): goto ma672; case ((uint8_t)23ULL): goto ma672; case ((uint8_t)24ULL): goto ma672; case ((uint8_t)25ULL): goto ma672; case ((uint8_t)26ULL): goto ma672; default: goto ma672; }
ma670:;
                uint8_t t676 = GM_LD(uint8_t, (((uint8_t *)&t668) + 0));
                if (t676 != ((uint8_t)16ULL)) goto mn675;
                void * t677 = gm_l5;
                gm_tob1snuii5bme t678;
                uint64_t t679 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t678 + 0, t679);
                void * t680 = gm_l0;
                uint32_t t681 = gm_l67;
                gm_b16_8 t682 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t680, t681);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t683;
                GM_ST(gm_b16_8, ((uint8_t *)&t683 + 0), t682);
                gm_tabb710d9mq2e t684;
                gm_f_format_be3fee1((void *)&t684, (void *)NULL, cx, NULL, t683);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t684))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t684) + 8, gm_srcuro7l7pd15c2, 257, 69);
                gm_b16_8 t685 = GM_LD(gm_b16_8, (((uint8_t *)&t684) + 8));
                gm_b16_8 t686 = GM_LD(gm_b16_8, ((uint8_t *)&t685));
                GM_ST(gm_b16_8, (uint8_t *)&t678 + 8, t686);
                GM_ST(uint64_t, (uint8_t *)&t678 + 24, ((uint64_t)0ULL));
                gm_tftb1odc739t5 t687 = gm_f_push_vgtfcjf(t677, &t678);
                if (((((uint8_t *)&t687))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t687) + 1, gm_srcuro7l7pd15c2, 257, 30);
                gm_b0_1 t688 = GM_LD(gm_b0_1, (((uint8_t *)&t687) + 1));
                goto me669;
mn675:;
ma671:;
                uint8_t t690 = GM_LD(uint8_t, (((uint8_t *)&t668) + 0));
                if (t690 != ((uint8_t)13ULL)) goto mn689;
                void * t691 = gm_l5;
                gm_tob1snuii5bme t692;
                uint64_t t693 = gm_l4;
                GM_ST(uint64_t, (uint8_t *)&t692 + 0, t693);
                void * t694 = gm_l0;
                uint32_t t695 = gm_l67;
                gm_b16_8 t696 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t694, t695);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t697;
                GM_ST(gm_b16_8, ((uint8_t *)&t697 + 0), t696);
                gm_tabb710d9mq2e t698;
                gm_f_format_be3fee1((void *)&t698, (void *)NULL, cx, NULL, t697);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t698))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t698) + 8, gm_srcuro7l7pd15c2, 258, 72);
                gm_b16_8 t699 = GM_LD(gm_b16_8, (((uint8_t *)&t698) + 8));
                gm_b16_8 t700 = GM_LD(gm_b16_8, ((uint8_t *)&t699));
                GM_ST(gm_b16_8, (uint8_t *)&t692 + 8, t700);
                GM_ST(uint64_t, (uint8_t *)&t692 + 24, ((uint64_t)0ULL));
                gm_tftb1odc739t5 t701 = gm_f_push_vgtfcjf(t691, &t692);
                if (((((uint8_t *)&t701))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t701) + 1, gm_srcuro7l7pd15c2, 258, 33);
                gm_b0_1 t702 = GM_LD(gm_b0_1, (((uint8_t *)&t701) + 1));
                goto me669;
mn689:;
ma672:;
                gm_tl874t2atnlh3 t704 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                gm_b16_8 t705;
                GM_ST(void *, (uint8_t *)&t705, (void *)(gm_sbe7f0bkvca75));
                GM_ST(uintptr_t, (uint8_t *)&t705 + 8, (uintptr_t)(42));
                gm_t08olq97rscn3 t706;
                gm_f_unsupported_531q51r((void *)&t706, (void *)NULL, cx, NULL, t704, t705);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t706);
                goto gm_fdone;
                goto me669;
mn703:;
ma673:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 256, 13);
me669:;
            }
            goto me3;
mn658:;
ma16:;
            uint8_t t710 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t710 != ((uint8_t)12ULL)) goto pa709;
            goto po708;
pa709:;
            uint8_t t712 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t712 != ((uint8_t)13ULL)) goto pa711;
            goto po708;
pa711:;
            goto mn707;
po708:;
            gm_tl874t2atnlh3 t713 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_b16_8 t714;
            GM_ST(void *, (uint8_t *)&t714, (void *)(gm_sbbavs945cjhp));
            GM_ST(uintptr_t, (uint8_t *)&t714 + 8, (uintptr_t)(31));
            gm_t08olq97rscn3 t715;
            gm_f_unsupported_531q51r((void *)&t715, (void *)NULL, cx, NULL, t713, t714);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t715);
            goto gm_fdone;
            goto me3;
mn707:;
ma17:;
            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 162, 5);
me3:;
        }
    }
    gm_b0_1 t716;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t716);
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
