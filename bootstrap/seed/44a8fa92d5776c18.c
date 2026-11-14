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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 a[12]; } gm_t61mja4eihh2b;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tmqm4tk26rmca;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t61mja4eihh2b) == 192 && _Alignof(gm_t61mja4eihh2b) == 8 && offsetof(gm_t61mja4eihh2b, a) == 0, "layout of `[12]string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; uint32_t f2; uint8_t pe[4]; } gm_tbah3mk9eqfec;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_th2lqbo77pvsc f0; uint64_t f1; } gm_tbiaaj0r9cjr2;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tplpbqf3duvn3 f0; uint8_t f1; uint8_t pe[3]; } gm_thdmr9qicg35r;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tmqm4tk26rmca) == 40 && _Alignof(gm_tmqm4tk26rmca) == 8 && offsetof(gm_tmqm4tk26rmca, f0) == 0 && offsetof(gm_tmqm4tk26rmca, f1) == 8 && offsetof(gm_tmqm4tk26rmca, f2) == 24, "layout of `(bench: bool, name: string, span: Span)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_toceffb697p0u f0; uint8_t f1; uint8_t pe[3]; } gm_trj4m8pa99qp7;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbah3mk9eqfec) == 48 && _Alignof(gm_tbah3mk9eqfec) == 8 && offsetof(gm_tbah3mk9eqfec, f0) == 0 && offsetof(gm_tbah3mk9eqfec, f1) == 16 && offsetof(gm_tbah3mk9eqfec, f2) == 40, "layout of `TestCase`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbiaaj0r9cjr2) == 32 && _Alignof(gm_tbiaaj0r9cjr2) == 8 && offsetof(gm_tbiaaj0r9cjr2, f0) == 0 && offsetof(gm_tbiaaj0r9cjr2, f1) == 24, "layout of `(label: ?string, module: usize)`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thdmr9qicg35r) == 12 && _Alignof(gm_thdmr9qicg35r) == 4 && offsetof(gm_thdmr9qicg35r, f0) == 0 && offsetof(gm_thdmr9qicg35r, f1) == 8, "layout of `?(template: u32, ty: Ty)`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trj4m8pa99qp7) == 20 && _Alignof(gm_trj4m8pa99qp7) == 4 && offsetof(gm_trj4m8pa99qp7, f0) == 0 && offsetof(gm_trj4m8pa99qp7, f1) == 16, "layout of `?NeedsAt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; gm_thdmr9qicg35r f2; uint8_t pe[4]; } gm_tti6qnvf9jg1p;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tti6qnvf9jg1p) == 72 && _Alignof(gm_tti6qnvf9jg1p) == 8 && offsetof(gm_tti6qnvf9jg1p, f0) == 0 && offsetof(gm_tti6qnvf9jg1p, f1) == 40 && offsetof(gm_tti6qnvf9jg1p, f2) == 56, "layout of `InferTable`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
typedef struct { void * f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_tti6qnvf9jg1p f4; gm_t88rcucg1b7uk f5; uint8_t p6[4]; gm_tnpifv20irnph f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint32_t f9; uint8_t p10[4]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_t9805qtonbsag f13; gm_trj4m8pa99qp7 f14; uint8_t p15[4]; gm_trqfvmjtt4sij f15; uint8_t f16; uint8_t p17[7]; gm_trqfvmjtt4sij f17; uint8_t f18; uint8_t p19[7]; uint64_t f19; gm_trqfvmjtt4sij f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; uint8_t f24; uint8_t p25[7]; gm_trqfvmjtt4sij f25; uint8_t f26; uint8_t p27[3]; gm_tkn42u7na3t81 f27; uint8_t p28[4]; gm_trqfvmjtt4sij f28; gm_t403eokj7ifg9 f29; gm_trqfvmjtt4sij f30; gm_trqfvmjtt4sij f31; gm_trqfvmjtt4sij f32; gm_trqfvmjtt4sij f33; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; uint8_t f38; uint8_t p39[7]; gm_trqfvmjtt4sij f39; uint8_t f40; uint8_t p41[7]; void * f41; } gm_tqgtfeqqnkjsa;
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_tqgtfeqqnkjsa) == 1304 && _Alignof(gm_tqgtfeqqnkjsa) == 8 && offsetof(gm_tqgtfeqqnkjsa, f0) == 0 && offsetof(gm_tqgtfeqqnkjsa, f1) == 8 && offsetof(gm_tqgtfeqqnkjsa, f2) == 16 && offsetof(gm_tqgtfeqqnkjsa, f3) == 56 && offsetof(gm_tqgtfeqqnkjsa, f4) == 96 && offsetof(gm_tqgtfeqqnkjsa, f5) == 168 && offsetof(gm_tqgtfeqqnkjsa, f6) == 176 && offsetof(gm_tqgtfeqqnkjsa, f7) == 192 && offsetof(gm_tqgtfeqqnkjsa, f8) == 232 && offsetof(gm_tqgtfeqqnkjsa, f9) == 272 && offsetof(gm_tqgtfeqqnkjsa, f10) == 280 && offsetof(gm_tqgtfeqqnkjsa, f11) == 320 && offsetof(gm_tqgtfeqqnkjsa, f12) == 360 && offsetof(gm_tqgtfeqqnkjsa, f13) == 400 && offsetof(gm_tqgtfeqqnkjsa, f14) == 408 && offsetof(gm_tqgtfeqqnkjsa, f15) == 432 && offsetof(gm_tqgtfeqqnkjsa, f16) == 472 && offsetof(gm_tqgtfeqqnkjsa, f17) == 480 && offsetof(gm_tqgtfeqqnkjsa, f18) == 520 && offsetof(gm_tqgtfeqqnkjsa, f19) == 528 && offsetof(gm_tqgtfeqqnkjsa, f20) == 536 && offsetof(gm_tqgtfeqqnkjsa, f21) == 576 && offsetof(gm_tqgtfeqqnkjsa, f22) == 616 && offsetof(gm_tqgtfeqqnkjsa, f23) == 656 && offsetof(gm_tqgtfeqqnkjsa, f24) == 696 && offsetof(gm_tqgtfeqqnkjsa, f25) == 704 && offsetof(gm_tqgtfeqqnkjsa, f26) == 744 && offsetof(gm_tqgtfeqqnkjsa, f27) == 748 && offsetof(gm_tqgtfeqqnkjsa, f28) == 760 && offsetof(gm_tqgtfeqqnkjsa, f29) == 800 && offsetof(gm_tqgtfeqqnkjsa, f30) == 920 && offsetof(gm_tqgtfeqqnkjsa, f31) == 960 && offsetof(gm_tqgtfeqqnkjsa, f32) == 1000 && offsetof(gm_tqgtfeqqnkjsa, f33) == 1040 && offsetof(gm_tqgtfeqqnkjsa, f34) == 1080 && offsetof(gm_tqgtfeqqnkjsa, f35) == 1120 && offsetof(gm_tqgtfeqqnkjsa, f36) == 1160 && offsetof(gm_tqgtfeqqnkjsa, f37) == 1200 && offsetof(gm_tqgtfeqqnkjsa, f38) == 1240 && offsetof(gm_tqgtfeqqnkjsa, f39) == 1248 && offsetof(gm_tqgtfeqqnkjsa, f40) == 1288 && offsetof(gm_tqgtfeqqnkjsa, f41) == 1296, "layout of `FnCx`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ambient_effects_opm47k8 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_t61mja4eihh2b l3; /* names */
    gm_tnpifv20irnph ret;
};
struct gm_fr_bench_entry_7sencrr {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_check_test_890fm8h {
    gm_frame h;
    gm_tqgtfeqqnkjsa l9; /* cx */
    gm_tapdr2ag17fh7 l10; /* fb */
    gm_t9805qtonbsag ret;
};
struct gm_fr_check_tests_ogqv9b0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* others */
    gm_trqfvmjtt4sij l4; /* seen */
    gm_tp8h583g7s5hd l7; /* o */
    gm_trqfvmjtt4sij l9; /* order */
    gm_trqfvmjtt4sij l17; /* names */
    gm_tp8h583g7s5hd l21; /* o */
    gm_tcfiotghd1s5k l22; /* t */
    gm_tmqm4tk26rmca l27; /* n */
    gm_t4gut2c9fj1kt l29; /* d */
    gm_tbah3mk9eqfec l31; /* case */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7rk46p9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e0ikd1p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ovbc4ec {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_63hn4i7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_39bo35q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gr12ft8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hf6eeru {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_runner_call_vg4071p {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* args */
    void * ret;
};
struct gm_fr_sort_by_1jkm5as {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_test_entry_0360kis {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* effects */
    gm_t9805qtonbsag ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_ambient_effects_opm47k8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bench_entry_7sencrr(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_check_test_890fm8h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_tests_ogqv9b0(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7rk46p9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e0ikd1p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ovbc4ec(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_finish_m8t0599(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_63hn4i7(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_fn_body_37q3l3a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_fn_cx_hgvq15e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void gm_f_format_c8e400b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_oihs60d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_introsort_up884nq(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_39bo35q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gr12ft8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hf6eeru(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_label_460dnlb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_4rr8b8l(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d0s1j1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ha7377a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t79guol(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_runner_call_vg4071p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sort_by_1jkm5as(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_test_entry_0360kis(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s2oe2s4851fv5[4];
extern const uint8_t gm_s2sghqjh0qbk9[8];
extern const uint8_t gm_s4gt1buiaiaiq[3];
extern const uint8_t gm_s799mjibj8396[10];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sdlumbvroiufa[3];
extern const uint8_t gm_shuj8drmk4t93[9];
extern const uint8_t gm_sihs6d097ocev[5];
extern const uint8_t gm_sjq0dshsov83m[20];
extern const uint8_t gm_smbfsgie9fgg4[4];
extern const uint8_t gm_sn937bfucthvg[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srch470kggqbqqi[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_ssga7g3v3nnl7[10];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_sub9t1fj6sv37[6];
extern const uint8_t gm_suhl0206dfbdm[5];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7rk46p9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7rk46p9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e0ikd1p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e0ikd1p F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ovbc4ec(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ovbc4ec F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_63hn4i7(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_63hn4i7 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
    gm_b16_8 t5 = t2;
    gm_b16_8 t6 = t4;
    uint8_t t7 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_39bo35q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_39bo35q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_7rk46p9();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_gr12ft8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gr12ft8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ovbc4ec();
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
inline __attribute__((aligned(4))) void gm_f_list_hf6eeru(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hf6eeru F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_e0ikd1p();
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
inline __attribute__((aligned(4))) void gm_f_sort_by_1jkm5as(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_1jkm5as F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = gm_l1;
        gm_f_introsort_up884nq(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_ambient_effects_opm47k8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_ambient_effects_opm47k8 F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* test */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* name */
    gm_tiilmfd16v3fc gm_l8; /* eref */
    GM_STACK_CHECK(&F, 738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_084fcd6((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn937bfucthvg));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srqehfttj6ngk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 16, t3);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_smbfsgie9fgg4));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 32, t4);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sa902c1tbtg0i));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 48, t5);
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s1qrqfh656g4q));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 64, t6);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s4gt1buiaiaiq));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 80, t7);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sub9t1fj6sv37));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 96, t8);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sihs6d097ocev));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 112, t9);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sdlumbvroiufa));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 128, t10);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s2oe2s4851fv5));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 144, t11);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_suhl0206dfbdm));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 160, t12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_ss6g2koqn63bf));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&F.l3 + 176, t13);
    }
    {
        {
            void * t15 = ((uint8_t *)&F.l3);
            if ((uintptr_t)0 > (uintptr_t)12 || (uintptr_t)12 > (uintptr_t)12) gm_trap_slice((uintptr_t)0, (uintptr_t)12, 12, gm_srch470kggqbqqi, 53, 30);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t15 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)((uintptr_t)12 - (uintptr_t)0));
            gm_b16_8 t17 = t16;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_l4 = t18;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t20 = gm_l5;
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    gm_tf1bjg81u0glv t25;
                    uint64_t t26 = gm_l5;
                    GM_ST(uint64_t, (uint8_t *)&t25 + 0, t26);
                    uint64_t t27 = gm_l5;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t28 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t25 + 8, t30);
                    gm_tf1bjg81u0glv t31 = t25;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
                    gm_l6 = t34;
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                    gm_l7 = t35;
                    {
                        uint64_t t38 = gm_l6;
                        uint8_t t39 = ((t38 == ((uint64_t)11ULL)));
                        if (!t39) goto ie37;
                        uint8_t t40 = gm_l1;
                        uint8_t t41 = (uint8_t)!t40;
                        if (!t41) goto ie37;
                        {
                            goto lb19;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    {
                        void * t43 = gm_l0;
                        gm_b16_8 t44 = gm_l7;
                        gm_tqu3m2l3n8mnc t45;
                        gm_f_prelude_effect_gilrmm5((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                        gm_tqu3m2l3n8mnc t46 = t45;
                        gm_tiilmfd16v3fc t47;
                        if (((((uint8_t *)&t46))[24] != 0)) {
                            gm_tiilmfd16v3fc t48 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t46) + 0));
                            t47 = t48;
                        } else {
                            goto lc19;
                        }
                        gm_l8 = t47;
                    }
                    {
                        void * t49 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t50 = gm_f_push_d1r4h19(t49, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l8)));
                        gm_tftb1odc739t5 t51 = t50;
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srch470kggqbqqi, 56, 9);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 53, 5);
me32:;
                }
lc19:;
                {
                    uint64_t t54 = gm_l5;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l5 = t57;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
    }
    {
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t59 = t58;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        gm_tnpifv20irnph t61 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_bench_entry_7sencrr(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_bench_entry_7sencrr F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* call */
    uint64_t gm_l4; /* module */
    uint32_t gm_l5; /* inner */
    GM_STACK_CHECK(&F, 351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = gm_l2;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s799mjibj8396));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(9));
        void * t6 = gm_f_runner_call_vg4071p((void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t7 = t6;
        void * t8;
        if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) {
            void * t9 = GM_LD(void *, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_t9805qtonbsag t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l3 = t8;
    }
    {
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 344) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srch470kggqbqqi, 99, 15);
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 344) + 0)) + t14 * 136) + 32));
        gm_l4 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s2sghqjh0qbk9));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
        gm_tl874t2atnlh3 t21 = gm_l2;
        uint64_t t22 = gm_l4;
        gm_b16_8 t23 = gm_f_empty_slice_he774nl();
        void * t24 = gm_l0;
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        gm_t88rcucg1b7uk t26 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t27 = gm_l0;
        gm_tnpifv20irnph t28 = gm_f_ambient_effects_opm47k8((void *)NULL, cx, NULL, t27, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t29 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t18, t19, t20, t21, t22, t23, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t29;
    }
    {
        uint32_t t30 = gm_l5;
        uint64_t t31 = (uint64_t)t30;
        uint64_t t32 = (uintptr_t)t31;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 384) + 0) + 8);
        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srch470kggqbqqi, 101, 5);
        void * t35 = gm_l3;
        GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 384) + 0)) + t32 * 64) + 40), t35);
    }
    {
        uint32_t t36 = gm_l5;
        gm_t9805qtonbsag t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t37), t36);
        ((uint8_t *)&t37)[4] = 1;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_check_test_890fm8h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, void * a4, uint8_t a5) {
    struct gm_fr_check_test_890fm8h F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* body */
    uint8_t gm_l5; /* bench */
    gm_tnpifv20irnph gm_l6; /* effects */
    gm_b16_8 gm_l7; /* label */
    uint32_t gm_l8; /* f */
    void * gm_l11; /* b */
    void * gm_l12; /* def */
    GM_STACK_CHECK(&F, 861u);
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
        gm_tnpifv20irnph t3 = gm_f_ambient_effects_opm47k8((void *)NULL, cx, NULL, t2, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t3;
    }
    {
        gm_t6j6smj7qoctu t4;
        gm_b16_8 t5;
        uint8_t t7 = gm_l5;
        if (!t7) goto ie6;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_srfagufvviad7));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
        gm_b16_8 t10 = t9;
        t5 = t10;
        goto ix6;
ie6:;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s9r64t1166q1f));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
        gm_b16_8 t13 = t12;
        t5 = t13;
ix6:;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t14 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_oihs60d((void *)&t15, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srch470kggqbqqi, 65, 14);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l7 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t9qkh0rsooc8d t19;
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
        gm_b16_8 t20 = gm_l7;
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_tl874t2atnlh3 t23 = gm_l3;
        uint64_t t24 = gm_l1;
        gm_b16_8 t25 = gm_f_empty_slice_he774nl();
        void * t26 = gm_l0;
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        gm_t88rcucg1b7uk t28 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t29 = gm_l6;
        uint32_t t30 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t18, t19, t22, t23, t24, t25, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t30;
    }
    {
        void * t31 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t31) + 672), ((uint8_t)1));
    }
    {
        void * t32 = gm_l0;
        uint64_t t33 = gm_l1;
        void * t34 = gm_l0;
        void * t35 = (void *)(((uint8_t *)t34) + 8);
        gm_t88rcucg1b7uk t36 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t37 = gm_l6;
        gm_f_fn_cx_hgvq15e((void *)&F.l9, (void *)NULL, cx, NULL, t32, t33, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t38 = gm_l8;
        gm_t9805qtonbsag t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t39), t38);
        ((uint8_t *)&t39)[4] = 1;
        GM_ST(gm_t9805qtonbsag, (((uint8_t *)&F.l9) + 400), t39);
    }
    {
        void * t40 = gm_l4;
        gm_t79rbdv4rafe5 t41 = GM_LD(gm_t79rbdv4rafe5, ((uint8_t *)t40));
        gm_tapdr2ag17fh7 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t42 + 8), t41);
        F.l10 = t42;
    }
    {
        void * t43 = (void *)((uint8_t *)&F.l9);
        void * t44 = (void *)((uint8_t *)&F.l10);
        void * t45 = gm_f_fn_body_37q3l3a((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t45;
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l9);
        void * t47 = gm_l11;
        gm_b0_1 t48 = gm_f_finish_m8t0599((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t49) + 672), ((uint8_t)0));
    }
    {
        uint32_t t50 = gm_l8;
        uint64_t t51 = (uint64_t)t50;
        uint64_t t52 = (uintptr_t)t51;
        void * t53 = gm_l0;
        uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)t53) + 384) + 0) + 8);
        if (t52 >= (uintptr_t)(t54)) gm_trap_index(t52, t54, gm_srch470kggqbqqi, 74, 13);
        void * t55 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t53) + 384) + 0)) + t52 * 64);
        gm_l12 = t55;
    }
    {
        void * t56 = gm_l12;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 16) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)t56) + 24), t57);
    }
    {
        void * t58 = gm_l12;
        void * t59 = gm_l11;
        GM_ST(void *, (((uint8_t *)t58) + 40), t59);
    }
    {
        gm_t9805qtonbsag t60;
        uint8_t t62 = gm_l5;
        if (!t62) goto ie61;
        void * t64 = gm_l0;
        uint32_t t65 = gm_l8;
        gm_tl874t2atnlh3 t66 = gm_l3;
        gm_t9805qtonbsag t67 = gm_f_bench_entry_7sencrr((void *)NULL, cx, NULL, t64, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t68 = t67;
        t60 = t68;
        goto ix61;
ie61:;
        void * t70 = gm_l0;
        uint32_t t71 = gm_l8;
        gm_tl874t2atnlh3 t72 = gm_l3;
        gm_t9805qtonbsag t73 = gm_f_test_entry_0360kis((void *)NULL, cx, NULL, t70, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t74 = t73;
        t60 = t74;
ix61:;
        uint8_t t75 = 0;
        F.ret = t60;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_tests_ogqv9b0(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_check_tests_ogqv9b0 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    uint64_t gm_l2; /* package */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l8; /* m */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t3877pk7lv748 gm_l12; /* x */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tbiaaj0r9cjr2 gm_l15; /* place */
    uint64_t gm_l16; /* module */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l23; /* text */
    gm_toceffb697p0u gm_l24; /* prev */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_tl874t2atnlh3 gm_l28; /* p */
    uint32_t gm_l30; /* entry */
    GM_STACK_CHECK(&F, 2423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 200) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srch470kggqbqqi, 9, 16);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 200) + 0)) + t3 * 184) + 136));
        gm_l2 = t7;
    }
    {
        gm_f_list_39bo35q((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_5dhgg4i((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 280) + 0));
            gm_l5 = t11;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l6;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l6;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tp8h583g7s5hd t23 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t19 * 168));
                    F.l7 = t23;
                    {
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 200) + 0) + 8);
                        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_srch470kggqbqqi, 13, 15);
                        void * t30 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 200) + 0)) + t26 * 184);
                        gm_l8 = t30;
                    }
                    {
                        uint8_t t32 = 0;
                        uint8_t t33 = 0;
                        uint8_t t34 = 0;
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                        uint64_t t36 = gm_l1;
                        uint8_t t37 = ((t35 == t36));
                        if (!t37) {
                            void * t38 = gm_l8;
                            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 136));
                            uint64_t t40 = gm_l2;
                            uint8_t t41 = (!(t39 == t40));
                            t34 = t41;
                        } else t34 = 1;
                        if (!t34) {
                            void * t42 = gm_l8;
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 112));
                            t33 = t43;
                        } else t33 = 1;
                        if (!t33) {
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                            gm_b16_8 t46 = t45;
                            uint8_t t47 = 0;
                            uint64_t t48 = t44;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t46) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t46))) + i * 8, ((uint8_t *)&t48))) { t47 = 1; break; }
                            uint8_t t49 = (uint8_t)(t47);
                            t32 = t49;
                        } else t32 = 1;
                        if (!t32) goto ie31;
                        {
                            goto lc12;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    {
                        void * t51 = (void *)((uint8_t *)&F.l4);
                        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                        gm_tftb1odc739t5 t53 = gm_f_push_ucgmlo2(t51, t52);
                        gm_tftb1odc739t5 t54 = t53;
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srch470kggqbqqi, 15, 9);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                    }
                    {
                        void * t56 = (void *)((uint8_t *)&F.l3);
                        gm_t3877pk7lv748 t57;
                        void * t58 = gm_l0;
                        void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
                        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t61 = gm_f_module_label_460dnlb((void *)NULL, cx, NULL, t59, t60);
                        if (gm_unw.kind) {
                            goto gm_ll16;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t61);
                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 0));
                        GM_ST(uint64_t, (uint8_t *)&t57 + 16, t62);
                        gm_tftb1odc739t5 t63 = gm_f_push_ha7377a(t56, &t57);
                        gm_tftb1odc739t5 t64 = t63;
                        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srch470kggqbqqi, 16, 9);
                        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 12, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t67 = gm_l6;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l6 = t70;
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
        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_closure t72 = ((gm_closure){GM_PTR_TAG(gm_f_fn_63hn4i7), NULL, cx});
        gm_f_sort_by_1jkm5as(t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_hf6eeru((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t73 = (void *)((uint8_t *)&F.l9);
        gm_tbiaaj0r9cjr2 t74;
        gm_th2lqbo77pvsc t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t74 + 0, t75);
        uint64_t t76 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t74 + 24, t76);
        gm_tftb1odc739t5 t77 = gm_f_push_4rr8b8l(t73, &t74);
        gm_tftb1odc739t5 t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srch470kggqbqqi, 20, 5);
        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
    }
    {
        {
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l10 = t81;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t83 = gm_l11;
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t85 = (t83 < t84);
                if (!t85) goto lb82;
                {
                    uint64_t t88 = gm_l11;
                    uint64_t t89 = (uintptr_t)t88;
                    uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t3877pk7lv748 t93 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t89 * 24));
                    gm_l12 = t93;
                    {
                        void * t95 = (void *)((uint8_t *)&F.l9);
                        gm_tbiaaj0r9cjr2 t96;
                        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                        gm_th2lqbo77pvsc t98;
                        GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t98), t97);
                        ((uint8_t *)&t98)[16] = 1;
                        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t96 + 0, t98);
                        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 16));
                        GM_ST(uint64_t, (uint8_t *)&t96 + 24, t99);
                        gm_tftb1odc739t5 t100 = gm_f_push_4rr8b8l(t95, &t96);
                        gm_tftb1odc739t5 t101 = t100;
                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srch470kggqbqqi, 21, 29);
                        gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                    }
                    goto me91;
mn92:;
                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 21, 5);
me91:;
                }
lc82:;
                {
                    uint64_t t104 = gm_l11;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                    uint64_t t107 = r105;
                    gm_l11 = t107;
                }
            }
lb82:;
        }
    }
    {
        {
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l13 = t109;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t111 = gm_l14;
                uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t113 = (t111 < t112);
                if (!t113) goto lb110;
                {
                    uint64_t t116 = gm_l14;
                    uint64_t t117 = (uintptr_t)t116;
                    uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    gm_tbiaaj0r9cjr2 t121 = GM_LD(gm_tbiaaj0r9cjr2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t117 * 32));
                    gm_l15 = t121;
                    {
                        uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 24));
                        gm_l16 = t123;
                    }
                    {
                        gm_f_list_gr12ft8((void *)&F.l17, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll114;
                        }
                    }
                    {
                        {
                            gm_l19 = ((uint64_t)0ULL);
                        }
                        {
                            void * t125 = gm_l0;
                            void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
                            uint64_t t127 = GM_LD(uintptr_t, ((((uint8_t *)t126) + 280) + 0) + 8);
                            gm_l18 = t127;
                        }
                        {
                            for (;;) {
                                uint64_t t129 = gm_l19;
                                uint64_t t130 = gm_l18;
                                uint8_t t131 = (t129 < t130);
                                if (!t131) goto lb128;
                                {
                                    uint64_t t134 = gm_l19;
                                    uint64_t t135 = t134;
                                    uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                                    gm_l20 = t138;
                                    {
                                        uint64_t t140 = gm_l20;
                                        uint64_t t141 = (uintptr_t)t140;
                                        void * t142 = gm_l0;
                                        void * t143 = GM_LD(void *, (((uint8_t *)t142) + 0));
                                        uint64_t t144 = GM_LD(uintptr_t, ((((uint8_t *)t143) + 280) + 0) + 8);
                                        if (t141 >= (uintptr_t)(t144)) gm_trap_index(t141, t144, gm_srch470kggqbqqi, 27, 18);
                                        gm_tp8h583g7s5hd t145 = GM_LD(gm_tp8h583g7s5hd, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t143) + 280) + 0)) + t141 * 168));
                                        F.l21 = t145;
                                    }
                                    {
                                        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l21) + 0));
                                        uint64_t t148 = gm_l16;
                                        uint8_t t149 = (!(t147 == t148));
                                        if (!t149) goto ie146;
                                        {
                                            goto lc128;
                                        }
                                        goto ix146;
ie146:;
ix146:;
                                    }
                                    {
                                        uint8_t t152 = GM_LD(uint8_t, (((((uint8_t *)&F.l21) + 8) + 64) + 0));
                                        if (t152 != ((uint8_t)3ULL)) goto ie151;
                                        gm_tcfiotghd1s5k t153 = GM_LD(gm_tcfiotghd1s5k, (((((uint8_t *)&F.l21) + 8) + 64) + 8));
                                        F.l22 = t153;
                                        {
                                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l22) + 0) + 0));
                                            gm_b16_8 t156 = t155;
                                            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                            gm_l23 = t157;
                                        }
                                        {
                                            gm_toceffb697p0u t158;
                                            memset(&t158, 0, sizeof t158);
                                            gm_l24 = t158;
                                        }
                                        {
                                            {
                                                gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                                                gm_l25 = t160;
                                            }
                                            {
                                                gm_l26 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t162 = gm_l26;
                                                    uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                                    uint8_t t164 = (t162 < t163);
                                                    if (!t164) goto lb161;
                                                    {
                                                        uint64_t t167 = gm_l26;
                                                        uint64_t t168 = (uintptr_t)t167;
                                                        uint64_t t169 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                                        gm_tmqm4tk26rmca t172 = GM_LD(gm_tmqm4tk26rmca, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t168 * 40));
                                                        F.l27 = t172;
                                                        {
                                                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&F.l27) + 0));
                                                            uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
                                                            uint8_t t177 = ((t175 == t176));
                                                            if (!t177) goto ie174;
                                                            gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 8));
                                                            gm_b16_8 t179 = gm_l23;
                                                            gm_b16_8 t180 = t178;
                                                            gm_b16_8 t181 = t179;
                                                            uint8_t t182 = (gm_eq0iqiubfm7e(((uint8_t *)&t180), ((uint8_t *)&t181)));
                                                            if (!t182) goto ie174;
                                                            {
                                                                gm_tl874t2atnlh3 t184 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l27) + 24));
                                                                gm_toceffb697p0u t185;
                                                                GM_ST(uint8_t, (uint8_t *)&t185 + 13, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t185 + 14, 0);
                                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t185), t184);
                                                                ((uint8_t *)&t185)[12] = 1;
                                                                gm_l24 = t185;
                                                            }
                                                            goto ix174;
ie174:;
ix174:;
                                                        }
                                                        goto me170;
mn171:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 32, 13);
me170:;
                                                    }
lc161:;
                                                    {
                                                        uint64_t t187 = gm_l26;
                                                        uint64_t r188; bool o189 = __builtin_add_overflow(t187, ((uint64_t)1ULL), &r188);
                                                        uint64_t t190 = r188;
                                                        gm_l26 = t190;
                                                    }
                                                }
lb161:;
                                            }
                                        }
                                        {
                                            if (!((((uint8_t *)&gm_l24))[12] != 0)) goto ie191;
                                            gm_tl874t2atnlh3 t192 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l24));
                                            gm_l28 = t192;
                                            {
                                                gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l22) + 24));
                                                gm_t6j6smj7qoctu t195;
                                                gm_b16_8 t196;
                                                uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
                                                if (!t198) goto ie197;
                                                gm_b16_8 t200;
                                                GM_ST(void *, (uint8_t *)&t200, (void *)(gm_ssga7g3v3nnl7));
                                                GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(9));
                                                gm_b16_8 t201 = t200;
                                                t196 = t201;
                                                goto ix197;
ie197:;
                                                gm_b16_8 t203;
                                                GM_ST(void *, (uint8_t *)&t203, (void *)(gm_s9r64t1166q1f));
                                                GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(4));
                                                gm_b16_8 t204 = t203;
                                                t196 = t204;
ix197:;
                                                GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
                                                gm_b16_8 t205 = gm_l23;
                                                GM_ST(gm_b16_8, (uint8_t *)&t195 + 16, t205);
                                                gm_tabb710d9mq2e t206;
                                                gm_f_format_c8e400b((void *)&t206, (void *)NULL, cx, NULL, &t195);
                                                if (gm_unw.kind) {
                                                    goto gm_ll132;
                                                }
                                                gm_tabb710d9mq2e t207 = t206;
                                                if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_srch470kggqbqqi, 36, 46);
                                                gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
                                                gm_b16_8 t209 = t208;
                                                gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
                                                gm_f_error_s6hn871((void *)&F.l29, (void *)NULL, cx, NULL, t194, t210);
                                                if (gm_unw.kind) {
                                                    goto gm_ll132;
                                                }
                                            }
                                            {
                                                void * t211 = (void *)((uint8_t *)&F.l29);
                                                gm_tl874t2atnlh3 t212 = gm_l28;
                                                gm_b16_8 t213;
                                                GM_ST(void *, (uint8_t *)&t213, (void *)(gm_sjq0dshsov83m));
                                                GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(19));
                                                gm_f_add_label_50fj7vv(t211, GM_CONV(gm_cseahvg8v7p0vs, t212), t213);
                                            }
                                            {
                                                void * t214 = gm_l0;
                                                gm_b0_1 t215 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t214, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l29)));
                                                if (gm_unw.kind) {
                                                    goto gm_ll132;
                                                }
                                            }
                                            {
                                                goto lc128;
                                            }
                                            goto ix191;
ie191:;
ix191:;
                                        }
                                        {
                                            void * t216 = (void *)((uint8_t *)&F.l17);
                                            gm_tmqm4tk26rmca t217;
                                            memset((uint8_t *)&t217 + 1, 0, 7);
                                            memset((uint8_t *)&t217 + 36, 0, 4);
                                            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
                                            GM_ST(uint8_t, (uint8_t *)&t217 + 0, t218);
                                            gm_b16_8 t219 = gm_l23;
                                            GM_ST(gm_b16_8, (uint8_t *)&t217 + 8, t219);
                                            gm_tl874t2atnlh3 t220 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l22) + 24));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t217 + 24, t220);
                                            gm_tftb1odc739t5 t221 = gm_f_push_t79guol(t216, &t217);
                                            gm_tftb1odc739t5 t222 = t221;
                                            if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_srch470kggqbqqi, 41, 13);
                                            gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
                                        }
                                        {
                                            void * t224 = gm_l0;
                                            uint64_t t225 = gm_l16;
                                            gm_b16_8 t226 = gm_l23;
                                            gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l22) + 24));
                                            void * t228 = (void *)(((uint8_t *)&F.l22) + 40);
                                            uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
                                            gm_t9805qtonbsag t230 = gm_f_check_test_890fm8h((void *)NULL, cx, NULL, t224, t225, t226, t227, t228, t229);
                                            if (gm_unw.kind) {
                                                goto gm_ll132;
                                            }
                                            gm_t9805qtonbsag t231 = t230;
                                            uint32_t t232;
                                            if (((((uint8_t *)&t231))[4] != 0)) {
                                                uint32_t t233 = GM_LD(uint32_t, (((uint8_t *)&t231) + 0));
                                                t232 = t233;
                                            } else {
                                                goto lc128;
                                            }
                                            gm_l30 = t232;
                                        }
                                        {
                                            memset((uint8_t *)&F.l31 + 44, 0, 4);
                                            gm_b16_8 t234 = gm_l23;
                                            GM_ST(gm_b16_8, (uint8_t *)&F.l31 + 0, t234);
                                            memcpy((uint8_t *)&F.l31 + 16, (((uint8_t *)&gm_l15) + 0), 24);
                                            uint32_t t235 = gm_l30;
                                            GM_ST(uint32_t, (uint8_t *)&F.l31 + 40, t235);
                                        }
                                        {
                                            uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
                                            if (!t237) goto ie236;
                                            {
                                                void * t239 = gm_l0;
                                                void * t240 = (void *)(((uint8_t *)t239) + 1008);
                                                gm_tftb1odc739t5 t241 = gm_f_push_d0s1j1e(t240, &(*(gm_tbah3mk9eqfec *)((uint8_t *)&F.l31)));
                                                gm_tftb1odc739t5 t242 = t241;
                                                if (((((uint8_t *)&t242))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t242) + 1, gm_srch470kggqbqqi, 44, 26);
                                                gm_b0_1 t243 = GM_LD(gm_b0_1, (((uint8_t *)&t242) + 1));
                                            }
                                            goto ix236;
ie236:;
                                            {
                                                void * t245 = gm_l0;
                                                void * t246 = (void *)(((uint8_t *)t245) + 968);
                                                gm_tftb1odc739t5 t247 = gm_f_push_d0s1j1e(t246, &(*(gm_tbah3mk9eqfec *)((uint8_t *)&F.l31)));
                                                gm_tftb1odc739t5 t248 = t247;
                                                if (((((uint8_t *)&t248))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t248) + 1, gm_srch470kggqbqqi, 44, 57);
                                                gm_b0_1 t249 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 1));
                                            }
ix236:;
                                        }
                                        goto ix151;
ie151:;
                                        {
                                            goto lc128;
                                        }
ix151:;
                                    }
                                    goto me136;
mn137:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 26, 9);
me136:;
                                }
lc128:;
                                {
                                    uint64_t t252 = gm_l19;
                                    uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                                    uint64_t t255 = r253;
                                    gm_l19 = t255;
                                }
                            }
gm_ll132:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb128; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc128; }
                            }
                            goto gm_ll114;
lb128:;
                        }
                    }
                    goto me119;
mn120:;
                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 22, 5);
me119:;
                }
lc110:;
                {
                    uint64_t t257 = gm_l14;
                    uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                    uint64_t t260 = r258;
                    gm_l14 = t260;
                }
            }
gm_ll114:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb110; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc110; }
            }
            goto gm_lad;
lb110:;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_63hn4i7(void * a0, void * a1);
inline __attribute__((aligned(4))) void * gm_f_runner_call_vg4071p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3) {
    struct gm_fr_runner_call_vg4071p F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* runner */
    uint64_t gm_l4; /* prelude */
    uint64_t gm_l5; /* d */
    uint32_t gm_l6; /* g */
    gm_t88rcucg1b7uk gm_l7; /* fn_ty */
    GM_STACK_CHECK(&F, 1213u);
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
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)t3) + 320));
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint64_t t11 = gm_l4;
        gm_b16_8 t12 = gm_l3;
        gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
        gm_tf5srh4mp4b3i t14 = t13;
        uint64_t t15;
        if (((((uint8_t *)&t14))[8] != 0)) {
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            uint8_t t17 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l5 = t15;
    }
    {
        void * t19 = gm_l0;
        uint64_t t20 = gm_l5;
        gm_t6qooom8qa2do t21;
        gm_f_resolve_decl_pus042v((void *)&t21, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6qooom8qa2do t22 = t21;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t23 != ((uint8_t)5ULL)) goto ie18;
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&t22) + 8));
        gm_l6 = t24;
        {
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 8);
            gm_tddtjfggdat0d t28;
            memset((uint8_t *)&t28 + 20, 0, 4);
            memset((uint8_t *)&t28 + 41, 0, 7);
            gm_b16_8 t29 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t30 = t29;
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t31);
            void * t32 = gm_l0;
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            gm_t88rcucg1b7uk t34 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t28 + 16, t34);
            void * t35 = gm_l0;
            gm_tnpifv20irnph t36 = gm_f_ambient_effects_opm47k8((void *)NULL, cx, NULL, t35, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tnpifv20irnph, (uint8_t *)&t28 + 24, t36);
            GM_ST(uint8_t, (uint8_t *)&t28 + 40, ((uint8_t)0));
            gm_t53at80pfdus3 t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t37 + 8), t28);
            gm_t88rcucg1b7uk t38 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t27, &t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t38;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t39 = (void *)((uint8_t *)&F.l8);
            gm_t88rcucg1b7uk t40 = gm_l7;
            uint32_t t41 = gm_l1;
            gm_teti411eujvf4 t42;
            memset(&t42, 0, sizeof t42);
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)26ULL));
            GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t41);
            gm_tl874t2atnlh3 t43 = gm_l2;
            void * t44 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t40, &t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t45 = gm_f_push_d943vgp_2(t39, &(*(gm_t69m56meqefs8 *)((uint8_t *)t44)));
            gm_tftb1odc739t5 t46 = t45;
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srch470kggqbqqi, 112, 5);
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
        }
        {
            void * t48 = gm_l0;
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to76jli3bqddq t51;
            uint32_t t52 = gm_l6;
            gm_tsth9qu1dv0g3 t53;
            memset(&t53, 0, sizeof t53);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t53 + 8), t52);
            GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t51 + 0, t53);
            gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t51 + 40, t54);
            gm_teti411eujvf4 t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)34ULL));
            GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t55 + 8), t51);
            gm_tl874t2atnlh3 t56 = gm_l2;
            void * t57 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix18;
ie18:;
        {
            uint8_t t60 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_test_entry_0360kis(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_test_entry_0360kis F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* call */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tiilmfd16v3fc gm_l7; /* e */
    uint64_t gm_l8; /* module */
    uint32_t gm_l9; /* entry */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tl874t2atnlh3 t4 = gm_l2;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_shuj8drmk4t93));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(8));
        void * t6 = gm_f_runner_call_vg4071p((void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t7 = t6;
        void * t8;
        if ((GM_LD(void *, ((uint8_t *)&t7)) != 0)) {
            void * t9 = GM_LD(void *, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            gm_t9805qtonbsag t10;
            memset(&t10, 0, sizeof t10);
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l3 = t8;
    }
    {
        gm_f_list_084fcd6((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t13 = gm_l0;
            gm_tnpifv20irnph t14 = gm_f_ambient_effects_opm47k8((void *)NULL, cx, NULL, t13, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 0));
            gm_l5 = t16;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l6;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tiilmfd16v3fc t28 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t24 * 24));
                    gm_l7 = t28;
                    {
                        void * t31 = gm_l0;
                        void * t32 = (void *)(((uint8_t *)t31) + 8);
                        uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 0));
                        gm_b16_8 t34 = gm_f_effect_name_prpp0ip(t32, t33);
                        gm_t0uuhaljsf410 t35;
                        gm_b16_8 t36;
                        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sn937bfucthvg));
                        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(3));
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 0, t36);
                        gm_b16_8 t37;
                        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_srqehfttj6ngk));
                        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 16, t37);
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_smbfsgie9fgg4));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 32, t38);
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sa902c1tbtg0i));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(3));
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 48, t39);
                        gm_t0uuhaljsf410 t40 = t35;
                        uint8_t t41 = 0;
                        gm_b16_8 t42 = t34;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 0, ((uint8_t *)&t42))) t41 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 16, ((uint8_t *)&t42))) t41 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 32, ((uint8_t *)&t42))) t41 = 1;
                        if (gm_eq0iqiubfm7e(((uint8_t *)&t40) + 48, ((uint8_t *)&t42))) t41 = 1;
                        uint8_t t43 = (uint8_t)(t41);
                        if (!t43) goto ie30;
                        {
                            void * t45 = (void *)((uint8_t *)&F.l4);
                            gm_tftb1odc739t5 t46 = gm_f_push_d1r4h19(t45, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l7)));
                            gm_tftb1odc739t5 t47 = t46;
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srch470kggqbqqi, 87, 71);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto ix30;
ie30:;
ix30:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srch470kggqbqqi, 86, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t50 = gm_l6;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l6 = t53;
                }
            }
lb17:;
        }
    }
    {
        uint32_t t54 = gm_l1;
        uint64_t t55 = (uint64_t)t54;
        uint64_t t56 = (uintptr_t)t55;
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uintptr_t, ((((uint8_t *)t57) + 344) + 0) + 8);
        if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_srch470kggqbqqi, 89, 15);
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t57) + 344) + 0)) + t56 * 136) + 32));
        gm_l8 = t59;
    }
    {
        void * t60 = gm_l0;
        gm_t9qkh0rsooc8d t61;
        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_stgqtu1f7bco8));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(7));
        gm_tl874t2atnlh3 t63 = gm_l2;
        uint64_t t64 = gm_l8;
        gm_b16_8 t65 = gm_f_empty_slice_he774nl();
        void * t66 = gm_l0;
        void * t67 = (void *)(((uint8_t *)t66) + 8);
        gm_t88rcucg1b7uk t68 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t70 = t69;
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
        gm_tnpifv20irnph t72 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t73 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t60, t61, t62, t63, t64, t65, t68, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t73;
    }
    {
        uint32_t t74 = gm_l9;
        uint64_t t75 = (uint64_t)t74;
        uint64_t t76 = (uintptr_t)t75;
        void * t77 = gm_l0;
        uint64_t t78 = GM_LD(uintptr_t, ((((uint8_t *)t77) + 384) + 0) + 8);
        if (t76 >= (uintptr_t)(t78)) gm_trap_index(t76, t78, gm_srch470kggqbqqi, 91, 5);
        void * t79 = gm_l3;
        GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t77) + 384) + 0)) + t76 * 64) + 40), t79);
    }
    {
        uint32_t t80 = gm_l9;
        gm_t9805qtonbsag t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t81), t80);
        ((uint8_t *)&t81)[4] = 1;
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
