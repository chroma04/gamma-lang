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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint64_t f0; int64_t f1; uint8_t f2; uint8_t pe[7]; } gm_tomokrc78hhu1;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tomokrc78hhu1) == 24 && _Alignof(gm_tomokrc78hhu1) == 8 && offsetof(gm_tomokrc78hhu1, f0) == 0 && offsetof(gm_tomokrc78hhu1, f1) == 8 && offsetof(gm_tomokrc78hhu1, f2) == 16, "layout of `Scaled`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
struct gm_fr_abs_v7cf214 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_837b4po {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_d8sd4qc {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_empty_slice_d914i5u {
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
struct gm_fr_from_f64_1parp9j {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* num */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_is_even_3kgcpjs {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_rb46l9j {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_od1e7nd {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_new_2jv6hag {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* num */
    gm_tbci5ospk5nnn l1; /* den */
    gm_tbci5ospk5nnn l2; /* g */
    gm_tbci5ospk5nnn l3; /* n */
    gm_tbci5ospk5nnn l4; /* d */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_round_scaled_7btaume {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tbci5ospk5nnn l4; /* n */
    gm_tbci5ospk5nnn l5; /* d */
    gm_tbci5ospk5nnn l8; /* num */
    gm_tbci5ospk5nnn l9; /* den */
    gm_tbci5ospk5nnn l10; /* qi */
    gm_tbci5ospk5nnn l11; /* ri */
    gm_tomokrc78hhu1 ret;
};
struct gm_fr_scaled_ufcgnm9 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* mantissa */
    gm_tbci5ospk5nnn l4; /* num */
    gm_tbci5ospk5nnn l6; /* den */
    gm_tbci5ospk5nnn l7; /* five */
    gm_tngtuiuskn938 l9; /* qr */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_sub_mler6q8 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_to_f16_0pcu0pq {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_to_f32_8bnhsji {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    float ret;
};
struct gm_fr_to_f64_4avhrht {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
};
struct gm_fr_to_float_o1iop8t {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    double ret;
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
struct gm_fr_write_v26t591 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_b0_1 ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4_c(gm_tngtuiuskn938 a0, gm_tngtuiuskn938 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_gcd_iabkui0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2_c(gm_tngtuiuskn938 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j_c(gm_tngtuiuskn938 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u_c(gm_tngtuiuskn938 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_round_scaled_7btaume(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2, int64_t a3);
__attribute__((aligned(4))) void gm_f_scaled_ufcgnm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_scn0pl9eq3k2k[24];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_suqsoolo29or4[24];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_abs_v7cf214 F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), ((uint8_t)0));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_837b4po F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_add_420lj6o((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_cmp_9aseneh F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t3 = gm_f_cmp_i1knrgu(&t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_even_3kgcpjs F;
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (!t3) {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srclv3pj5es0f8o, 344, 63);
        uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a0) + 8) + 0)) + t4 * 4));
        uint32_t t7 = (uint32_t)(t6 & ((uint32_t)1ULL));
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t1 = t8;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t2 = ((t1 == ((uint64_t)1ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_rb46l9j F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t2 = ((t1 == ((uint64_t)0ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    uint8_t t3 = (!(t1 == t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_trqfvmjtt4sij t10;
    gm_f_mag_mul_if5s38k((void *)&t10, (void *)NULL, cx, NULL, t6, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t3, &t10);
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
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_od1e7nd F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1;
    gm_f_neg_agd5c5k((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_mler6q8 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_sub_pktm5cr((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_zero_s25l990((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1);
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
extern __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_d8sd4qc F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)a1)));
        if (!t3) goto ie2;
        {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_scn0pl9eq3k2k));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(23));
            gm_b16_8 t6 = t5;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8), gm_src6b5gn9iggr1p, 102, 22);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_tbci5ospk5nnn t7;
        gm_f_mul_nfj5rjv((void *)&t7, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t8;
        gm_f_mul_nfj5rjv((void *)&t8, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t7, &t8);
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
extern __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4_c(gm_tngtuiuskn938 a0, gm_tngtuiuskn938 a1) { return gm_f_eq_ceufbo4(&a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_from_f64_1parp9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0) {
    struct gm_fr_from_f64_1parp9j F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* b */
    uint64_t gm_l2; /* field */
    uint64_t gm_l3; /* frac */
    uint64_t gm_l4; /* m */
    int64_t gm_l5; /* e */
    GM_STACK_CHECK(&F, 548u);
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
        if (n6 >= 64) gm_trap_shift((uint64_t)n6, "u64", gm_src6b5gn9iggr1p, 229, 15);
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
        uint64_t t11;
        uint64_t t13 = gm_l2;
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        uint64_t t16 = gm_l3;
        uint64_t t17 = t16;
        t11 = t17;
        goto ix12;
ie12:;
        uint64_t t19 = gm_l3;
        unsigned __int128 n20 = (unsigned __int128)((uint64_t)52ULL);
        if (n20 >= 64) gm_trap_shift((uint64_t)n20, "u64", gm_src6b5gn9iggr1p, 232, 48);
        uint64_t r21 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n20);
        bool o22 = (uint64_t)(r21 >> (int)n20) != ((uint64_t)1ULL);
        if (o22) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 232, 48);
        uint64_t t23 = r21;
        uint64_t t24 = (uint64_t)(t19 | t23);
        uint64_t t25 = t24;
        t11 = t25;
ix12:;
        gm_l4 = t11;
    }
    {
        uint64_t t26;
        uint64_t t28 = gm_l2;
        uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
        if (!t29) goto ie27;
        uint64_t t31 = ((uint64_t)1ULL);
        t26 = t31;
        goto ix27;
ie27:;
        uint64_t t33 = gm_l2;
        uint64_t t34 = t33;
        t26 = t34;
ix27:;
        int64_t t35 = (int64_t)t26;
        int64_t r36; bool o37 = __builtin_sub_overflow(t35, ((int64_t)1075LL), &r36);
        if (o37) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 233, 10);
        int64_t t38 = r36;
        gm_l5 = t38;
    }
    {
        uint64_t t39 = gm_l4;
        gm_f_from_u64_ud13q5v((void *)&F.l6, (void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t41 = gm_l1;
        unsigned __int128 n42 = (unsigned __int128)((uint64_t)63ULL);
        if (n42 >= 64) gm_trap_shift((uint64_t)n42, "u64", gm_src6b5gn9iggr1p, 235, 9);
        uint64_t t43 = (uint64_t)(t41 >> (int)n42);
        uint8_t t44 = ((t43 == ((uint64_t)1ULL)));
        if (!t44) goto ie40;
        {
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        int64_t t47 = gm_l5;
        uint8_t t48 = (t47 >= ((int64_t)0LL));
        if (!t48) goto ie46;
        {
            gm_tbci5ospk5nnn t50 = F.l6;
            int64_t t51 = gm_l5;
            uint64_t t52 = (uint64_t)t51;
            gm_tbci5ospk5nnn t53;
            gm_f_shl_qqecl81((void *)&t53, (void *)NULL, cx, NULL, &t50, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        gm_tbci5ospk5nnn t54 = F.l6;
        gm_tbci5ospk5nnn t55;
        gm_f_one_08qg1op((void *)&t55, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t56 = gm_l5;
        if (t56 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 237, 47);
        int64_t t57 = (int64_t)((uint64_t)0 - (uint64_t)t56);
        uint64_t t58 = (uint64_t)t57;
        gm_tbci5ospk5nnn t59;
        gm_f_shl_qqecl81((void *)&t59, (void *)NULL, cx, NULL, &t55, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t54, &t59);
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
extern __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2_c(gm_tngtuiuskn938 a0) { return gm_f_is_integer_ab2ohk2(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j_c(gm_tngtuiuskn938 a0) { return gm_f_is_negative_rb46l9j(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u_c(gm_tngtuiuskn938 a0) { return gm_f_is_zero_5aept6u(&a0); }
extern __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_2jv6hag F;
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        uint8_t t3 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (!t3) goto ie2;
        {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_suqsoolo29or4));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(23));
            gm_b16_8 t6 = t5;
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8), gm_src6b5gn9iggr1p, 20, 24);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_gcd_iabkui0((void *)&F.l2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tbci5ospk5nnn t7 = F.l0;
        F.l3 = t7;
    }
    {
        gm_tbci5ospk5nnn t8 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        F.l4 = t8;
    }
    {
        uint8_t t10 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)));
        uint8_t t11 = (uint8_t)!t10;
        if (!t11) goto ie9;
        gm_tbci5ospk5nnn t12 = F.l2;
        gm_tbci5ospk5nnn t13;
        gm_f_one_08qg1op((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int32_t t14 = gm_f_cmp_i1knrgu(&t12, &t13);
        uint8_t t15 = (t14 > ((int32_t)0LL));
        if (!t15) goto ie9;
        {
            gm_f_div_f0qrfbr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_div_f0qrfbr((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t18 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
        if (!t18) goto ie17;
        {
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        memcpy((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, ((uint8_t *)&F.l3), 48);
        memcpy((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, ((uint8_t *)&F.l4), 48);
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
__attribute__((aligned(4))) void gm_f_round_scaled_7btaume(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2, int64_t a3) {
    struct gm_fr_round_scaled_7btaume F;
    uint32_t gm_l1; /* p */
    int64_t gm_l2; /* emin */
    int64_t gm_l3; /* emax */
    int64_t gm_l6; /* e0 */
    int64_t gm_l7; /* k */
    uint64_t gm_l12; /* q */
    uint8_t gm_l13; /* sticky */
    int64_t gm_l14; /* len */
    int64_t gm_l15; /* e */
    int64_t gm_l16; /* drop */
    int64_t gm_l17; /* ue */
    uint64_t gm_l18; /* m */
    uint64_t gm_l19; /* rest */
    uint64_t gm_l20; /* half */
    uint8_t gm_l21; /* up */
    int64_t gm_l22; /* lead */
    GM_STACK_CHECK(&F, 1257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_abs_v7cf214((void *)&F.l4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)a0) + 48));
        F.l5 = t2;
    }
    {
        uint8_t t4 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
        if (!t4) goto ie3;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, ((int64_t)0LL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint64_t t6 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
        int64_t t7 = (int64_t)t6;
        uint64_t t8 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
        int64_t t9 = (int64_t)t8;
        int64_t r10; bool o11 = __builtin_sub_overflow(t7, t9, &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 155, 11);
        int64_t t12 = r10;
        gm_l6 = t12;
    }
    {
        int64_t t14 = gm_l6;
        int64_t t15 = gm_l3;
        int64_t r16; bool o17 = __builtin_add_overflow(t15, ((int64_t)3LL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 156, 13);
        int64_t t18 = r16;
        uint8_t t19 = (t14 > t18);
        if (!t19) goto ie13;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, ((int64_t)0LL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)1));
            goto gm_fdone;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        int64_t t22 = gm_l6;
        int64_t t23 = gm_l2;
        uint32_t t24 = gm_l1;
        int64_t t25 = (int64_t)t24;
        int64_t r26; bool o27 = __builtin_sub_overflow(t23, t25, &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 157, 13);
        int64_t t28 = r26;
        int64_t r29; bool o30 = __builtin_sub_overflow(t28, ((int64_t)3LL), &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 157, 13);
        int64_t t31 = r29;
        uint8_t t32 = (t22 < t31);
        if (!t32) goto ie21;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, ((int64_t)0LL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        uint32_t t34 = gm_l1;
        int64_t t35 = (int64_t)t34;
        int64_t r36; bool o37 = __builtin_add_overflow(t35, ((int64_t)2LL), &r36);
        if (o37) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 159, 10);
        int64_t t38 = r36;
        int64_t t39 = gm_l6;
        int64_t r40; bool o41 = __builtin_sub_overflow(t38, t39, &r40);
        if (o41) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 159, 10);
        int64_t t42 = r40;
        gm_l7 = t42;
    }
    {
        gm_tbci5ospk5nnn t43 = F.l4;
        F.l8 = t43;
    }
    {
        gm_tbci5ospk5nnn t44 = F.l5;
        F.l9 = t44;
    }
    {
        int64_t t46 = gm_l7;
        uint8_t t47 = (t46 > ((int64_t)0LL));
        if (!t47) goto ie45;
        {
            gm_tbci5ospk5nnn t49 = F.l4;
            int64_t t50 = gm_l7;
            uint64_t t51 = (uint64_t)t50;
            gm_f_shl_qqecl81((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), (void *)NULL, cx, NULL, &t49, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix45;
ie45:;
        int64_t t53 = gm_l7;
        uint8_t t54 = (t53 < ((int64_t)0LL));
        if (!t54) goto ie52;
        {
            gm_tbci5ospk5nnn t56 = F.l5;
            int64_t t57 = gm_l7;
            if (t57 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 162, 73);
            int64_t t58 = (int64_t)((uint64_t)0 - (uint64_t)t57);
            uint64_t t59 = (uint64_t)t58;
            gm_f_shl_qqecl81((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)), (void *)NULL, cx, NULL, &t56, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix52;
ie52:;
ix52:;
ix45:;
    }
    {
        gm_tngtuiuskn938 t60;
        gm_f_div_rem_sc09dcc((void *)&t60, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t61 = t60;
        F.l10 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t61 + 0);
        F.l11 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t61 + 48);
    }
    {
        gm_tf5srh4mp4b3i t62 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
        gm_tf5srh4mp4b3i t63 = t62;
        if (!((((uint8_t *)&t63))[8] != 0)) gm_trap("unwrap of null", gm_src6b5gn9iggr1p, 164, 10);
        uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&t63));
        gm_l12 = t64;
    }
    {
        uint8_t t65 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
        uint8_t t66 = (uint8_t)!t65;
        gm_l13 = t66;
    }
    {
        uint64_t t67 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
        int64_t t68 = (int64_t)t67;
        gm_l14 = t68;
    }
    {
        int64_t t69 = gm_l14;
        int64_t r70; bool o71 = __builtin_sub_overflow(t69, ((int64_t)1LL), &r70);
        if (o71) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 168, 10);
        int64_t t72 = r70;
        int64_t t73 = gm_l7;
        int64_t r74; bool o75 = __builtin_sub_overflow(t72, t73, &r74);
        if (o75) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 168, 10);
        int64_t t76 = r74;
        gm_l15 = t76;
    }
    {
        int64_t t77 = gm_l14;
        uint32_t t78 = gm_l1;
        int64_t t79 = (int64_t)t78;
        int64_t r80; bool o81 = __builtin_sub_overflow(t77, t79, &r80);
        if (o81) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 169, 13);
        int64_t t82 = r80;
        gm_l16 = t82;
    }
    {
        int64_t t84 = gm_l15;
        int64_t t85 = gm_l2;
        uint8_t t86 = (t84 < t85);
        if (!t86) goto ie83;
        {
            int64_t t88 = gm_l16;
            int64_t t89 = gm_l2;
            int64_t t90 = gm_l15;
            int64_t r91; bool o92 = __builtin_sub_overflow(t89, t90, &r91);
            if (o92) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 170, 27);
            int64_t t93 = r91;
            int64_t r94; bool o95 = __builtin_add_overflow(t88, t93, &r94);
            if (o95) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 170, 19);
            int64_t t96 = r94;
            gm_l16 = t96;
        }
        goto ix83;
ie83:;
ix83:;
    }
    {
        int64_t t97 = gm_l16;
        int64_t t98 = gm_l7;
        int64_t r99; bool o100 = __builtin_sub_overflow(t97, t98, &r99);
        if (o100) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 171, 11);
        int64_t t101 = r99;
        gm_l17 = t101;
    }
    {
        int64_t t103 = gm_l16;
        uint8_t t104 = (t103 >= ((int64_t)64LL));
        if (!t104) goto ie102;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            int64_t t106 = gm_l17;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, t106);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix102;
ie102:;
ix102:;
    }
    {
        uint64_t t107 = gm_l12;
        int64_t t108 = gm_l16;
        uint64_t t109 = (uint64_t)t108;
        unsigned __int128 n110 = (unsigned __int128)t109;
        if (n110 >= 64) gm_trap_shift((uint64_t)n110, "u64", gm_src6b5gn9iggr1p, 173, 10);
        uint64_t t111 = (uint64_t)(t107 >> (int)n110);
        gm_l18 = t111;
    }
    {
        uint64_t t112 = gm_l12;
        uint64_t t113 = gm_l18;
        int64_t t114 = gm_l16;
        uint64_t t115 = (uint64_t)t114;
        unsigned __int128 n116 = (unsigned __int128)t115;
        if (n116 >= 64) gm_trap_shift((uint64_t)n116, "u64", gm_src6b5gn9iggr1p, 174, 18);
        uint64_t r117 = (uint64_t)((uint64_t)t113 << (int)n116);
        bool o118 = (uint64_t)(r117 >> (int)n116) != t113;
        if (o118) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 174, 18);
        uint64_t t119 = r117;
        uint64_t r120; bool o121 = __builtin_sub_overflow(t112, t119, &r120);
        if (o121) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 174, 13);
        uint64_t t122 = r120;
        gm_l19 = t122;
    }
    {
        int64_t t123 = gm_l16;
        int64_t r124; bool o125 = __builtin_sub_overflow(t123, ((int64_t)1LL), &r124);
        if (o125) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 175, 28);
        int64_t t126 = r124;
        uint64_t t127 = (uint64_t)t126;
        unsigned __int128 n128 = (unsigned __int128)t127;
        if (n128 >= 64) gm_trap_shift((uint64_t)n128, "u64", gm_src6b5gn9iggr1p, 175, 17);
        uint64_t r129 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n128);
        bool o130 = (uint64_t)(r129 >> (int)n128) != ((uint64_t)1ULL);
        if (o130) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 175, 17);
        uint64_t t131 = r129;
        gm_l20 = t131;
    }
    {
        uint8_t t132 = 0;
        uint64_t t133 = gm_l19;
        uint64_t t134 = gm_l20;
        uint8_t t135 = (t133 > t134);
        if (!t135) {
            uint8_t t136 = 0;
            uint64_t t137 = gm_l19;
            uint64_t t138 = gm_l20;
            uint8_t t139 = ((t137 == t138));
            if (t139) {
                uint8_t t140 = 0;
                uint8_t t141 = gm_l13;
                if (!t141) {
                    uint64_t t142 = gm_l18;
                    uint64_t t143 = (uint64_t)(t142 & ((uint64_t)1ULL));
                    uint8_t t144 = ((t143 == ((uint64_t)1ULL)));
                    t140 = t144;
                } else t140 = 1;
                t136 = t140;
            } else t136 = 0;
            t132 = t136;
        } else t132 = 1;
        gm_l21 = t132;
    }
    {
        uint8_t t146 = gm_l21;
        if (!t146) goto ie145;
        {
            uint64_t t148 = gm_l18;
            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 177, 13);
            uint64_t t151 = r149;
            gm_l18 = t151;
        }
        goto ix145;
ie145:;
ix145:;
    }
    {
        uint64_t t153 = gm_l18;
        uint8_t t154 = ((t153 == ((uint64_t)0ULL)));
        if (!t154) goto ie152;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            int64_t t156 = gm_l17;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, t156);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)0));
            goto gm_fdone;
        }
        goto ix152;
ie152:;
ix152:;
    }
    {
        uint64_t t157 = gm_l18;
        uint32_t t158 = (uint32_t)gm_clz(((unsigned __int128)(uint64_t)(t157) & (((unsigned __int128)1 << 64) - 1)), 64);
        int64_t t159 = (int64_t)t158;
        int64_t r160; bool o161 = __builtin_sub_overflow(((int64_t)64LL), t159, &r160);
        if (o161) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 180, 13);
        int64_t t162 = r160;
        int64_t r163; bool o164 = __builtin_sub_overflow(t162, ((int64_t)1LL), &r163);
        if (o164) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 180, 13);
        int64_t t165 = r163;
        gm_l22 = t165;
    }
    {
        int64_t t167 = gm_l22;
        int64_t t168 = gm_l17;
        int64_t r169; bool o170 = __builtin_add_overflow(t167, t168, &r169);
        if (o170) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 181, 8);
        int64_t t171 = r169;
        int64_t t172 = gm_l3;
        uint8_t t173 = (t171 > t172);
        if (!t173) goto ie166;
        {
            memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, ((uint64_t)0ULL));
            int64_t t175 = gm_l17;
            GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, t175);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)1));
            goto gm_fdone;
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        memset((uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 17, 0, 7);
        uint64_t t176 = gm_l18;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 0, t176);
        int64_t t177 = gm_l17;
        GM_ST(int64_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 8, t177);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tomokrc78hhu1 *)gm_out) + 16, ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_scaled_ufcgnm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2) {
    struct gm_fr_scaled_ufcgnm9 F;
    uint32_t gm_l1; /* base */
    int64_t gm_l2; /* scale */
    uint32_t gm_l3; /* k */
    uint32_t gm_l5; /* twos */
    uint32_t gm_l8; /* fives */
    GM_STACK_CHECK(&F, 568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        int64_t t3 = gm_l2;
        uint8_t t4 = (t3 >= ((int64_t)0LL));
        if (!t4) goto ie2;
        {
            gm_tbci5ospk5nnn t6 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
            uint32_t t7 = gm_l1;
            uint64_t t8 = (uint64_t)t7;
            gm_tbci5ospk5nnn t9;
            gm_f_from_u64_ud13q5v((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            int64_t t10 = gm_l2;
            uint32_t t11 = (uint32_t)t10;
            gm_tbci5ospk5nnn t12;
            gm_f_pow_mhlb5fr((void *)&t12, (void *)NULL, cx, NULL, &t9, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t13;
            gm_f_mul_nfj5rjv((void *)&t13, (void *)NULL, cx, NULL, &t6, &t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t13);
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
        uint8_t t15 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        if (!t15) goto ie14;
        {
            gm_f_zero_9r6o7sj((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        int64_t t17 = gm_l2;
        if (t17 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 45, 11);
        int64_t t18 = (int64_t)((uint64_t)0 - (uint64_t)t17);
        uint32_t t19 = (uint32_t)t18;
        gm_l3 = t19;
    }
    {
        gm_tbci5ospk5nnn t20 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        F.l4 = t20;
    }
    {
        uint32_t t21 = gm_l3;
        gm_l5 = t21;
    }
    {
        for (;;) {
            uint32_t t23 = gm_l5;
            uint8_t t24 = (t23 > ((uint32_t)0ULL));
            if (!t24) goto lb22;
            uint8_t t25 = gm_f_is_even_3kgcpjs(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)));
            if (!t25) goto lb22;
            {
                gm_f_shr_4jp99a9((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), ((uint64_t)1ULL));
                if (gm_unw.kind) {
                    goto gm_ll26;
                }
            }
            {
                uint32_t t28 = gm_l5;
                uint32_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint32_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 50, 9);
                uint32_t t31 = r29;
                gm_l5 = t31;
            }
lc22:;
        }
gm_ll26:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
        }
        goto gm_lad;
lb22:;
    }
    {
        gm_tbci5ospk5nnn t32;
        gm_f_one_08qg1op((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t33 = gm_l5;
        uint64_t t34 = (uint64_t)t33;
        gm_f_shl_qqecl81((void *)&F.l6, (void *)NULL, cx, NULL, &t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t36 = gm_l1;
        uint8_t t37 = ((t36 == ((uint32_t)10ULL)));
        if (!t37) goto ie35;
        {
            gm_f_from_u64_ud13q5v((void *)&F.l7, (void *)NULL, cx, NULL, ((uint64_t)5ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint32_t t39 = gm_l3;
            gm_l8 = t39;
        }
        {
            for (;;) {
                uint32_t t41 = gm_l8;
                uint8_t t42 = (t41 > ((uint32_t)0ULL));
                if (!t42) goto lb40;
                {
                    gm_f_div_rem_sc09dcc((void *)&F.l9, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l4)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                    if (gm_unw.kind) {
                        goto gm_ll43;
                    }
                }
                {
                    uint8_t t46 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l9) + 48)));
                    uint8_t t47 = (uint8_t)!t46;
                    if (!t47) goto ie45;
                    {
                        goto lb40;
                    }
                    goto ix45;
ie45:;
ix45:;
                }
                {
                    gm_tbci5ospk5nnn t49 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l9) + 0));
                    F.l4 = t49;
                }
                {
                    uint32_t t50 = gm_l8;
                    uint32_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint32_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 60, 13);
                    uint32_t t53 = r51;
                    gm_l8 = t53;
                }
lc40:;
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
            }
            goto gm_lad;
lb40:;
        }
        {
            gm_tbci5ospk5nnn t54 = F.l6;
            uint32_t t55 = gm_l8;
            gm_tbci5ospk5nnn t56;
            gm_f_pow_mhlb5fr((void *)&t56, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_mul_nfj5rjv((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &t54, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        memcpy((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, ((uint8_t *)&F.l4), 48);
        memcpy((uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, ((uint8_t *)&F.l6), 48);
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
extern __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_f16_0pcu0pq F;
    uint8_t gm_l1; /* neg */
    gm_tomokrc78hhu1 gm_l2; /* s */
    double gm_l3; /* v */
    int64_t gm_l4; /* ue */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t2 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)a0)));
        gm_l1 = t2;
    }
    {
        gm_f_round_scaled_7btaume((void *)&gm_l2, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)a0)), ((uint32_t)11ULL), ((int64_t)-14LL), ((int64_t)15LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 16));
        if (!t4) goto ie3;
        {
            double t6;
            uint8_t t8 = gm_l1;
            if (!t8) goto ie7;
            double t10 = (((double)-1e0) / ((double)0e0));
            double t11 = t10;
            t6 = t11;
            goto ix7;
ie7:;
            double t13 = (((double)1e0) / ((double)0e0));
            double t14 = t13;
            t6 = t14;
ix7:;
            uint8_t t15 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        double t17 = (double)(t16);
        gm_l3 = t17;
    }
    {
        int64_t t18 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l4 = t18;
    }
    {
        for (;;) {
            int64_t t20 = gm_l4;
            uint8_t t21 = (t20 > ((int64_t)0LL));
            if (!t21) goto lb19;
            {
                double t24 = gm_l3;
                double t25 = (t24 * ((double)2e0));
                gm_l3 = t25;
            }
            {
                int64_t t26 = gm_l4;
                int64_t r27; bool o28 = __builtin_sub_overflow(t26, ((int64_t)1LL), &r27);
                if (o28) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 258, 9);
                int64_t t29 = r27;
                gm_l4 = t29;
            }
lc19:;
        }
lb19:;
    }
    {
        for (;;) {
            int64_t t31 = gm_l4;
            uint8_t t32 = (t31 < ((int64_t)0LL));
            if (!t32) goto lb30;
            {
                double t35 = gm_l3;
                double t36 = (t35 / ((double)2e0));
                gm_l3 = t36;
            }
            {
                int64_t t37 = gm_l4;
                int64_t r38; bool o39 = __builtin_add_overflow(t37, ((int64_t)1LL), &r38);
                if (o39) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 262, 9);
                int64_t t40 = r38;
                gm_l4 = t40;
            }
lc30:;
        }
lb30:;
    }
    {
        double t41;
        uint8_t t43 = gm_l1;
        if (!t43) goto ie42;
        double t45 = gm_l3;
        double t46 = -t45;
        double t47 = t46;
        t41 = t47;
        goto ix42;
ie42:;
        double t49 = gm_l3;
        double t50 = t49;
        t41 = t50;
ix42:;
        uint8_t t51 = 0;
        F.ret = t41;
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
__attribute__((aligned(4))) float gm_f_to_f32_8bnhsji(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_f32_8bnhsji F;
    uint32_t gm_l1; /* sign */
    gm_tomokrc78hhu1 gm_l2; /* s */
    uint64_t gm_l3; /* m */
    int64_t gm_l4; /* ue */
    uint32_t gm_l5; /* field */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l1 = ((uint32_t)0ULL);
    }
    {
        uint8_t t3 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)a0)));
        if (!t3) goto ie2;
        {
            gm_l1 = ((uint32_t)2147483648ULL);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_round_scaled_7btaume((void *)&gm_l2, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)a0)), ((uint32_t)24ULL), ((int64_t)-126LL), ((int64_t)127LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 16));
        if (!t6) goto ie5;
        {
            uint32_t t8 = gm_l1;
            uint32_t t9 = (uint32_t)(t8 | ((uint32_t)2139095040ULL));
            uint32_t t10 = t9;
            float t11;
            memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 4 ? sizeof t11 : 4);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie13;
        {
            uint32_t t17 = gm_l1;
            uint32_t t18 = t17;
            float t19;
            memset(&t19, 0, sizeof t19); memcpy(&t19, ((uint8_t *)&t18), sizeof t19 < 4 ? sizeof t19 : 4);
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        gm_l3 = t21;
    }
    {
        int64_t t22 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l4 = t22;
    }
    {
        uint64_t t24 = gm_l3;
        unsigned __int128 n25 = (unsigned __int128)((uint64_t)24ULL);
        if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_src6b5gn9iggr1p, 217, 14);
        uint64_t r26 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n25);
        bool o27 = (uint64_t)(r26 >> (int)n25) != ((uint64_t)1ULL);
        if (o27) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 217, 14);
        uint64_t t28 = r26;
        uint8_t t29 = (t24 >= t28);
        if (!t29) goto ie23;
        {
            uint64_t t31 = gm_l3;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)1ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_src6b5gn9iggr1p, 218, 9);
            uint64_t t33 = (uint64_t)(t31 >> (int)n32);
            gm_l3 = t33;
        }
        {
            int64_t t34 = gm_l4;
            int64_t r35; bool o36 = __builtin_add_overflow(t34, ((int64_t)1LL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 219, 9);
            int64_t t37 = r35;
            gm_l4 = t37;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t39 = gm_l3;
        unsigned __int128 n40 = (unsigned __int128)((uint64_t)23ULL);
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_src6b5gn9iggr1p, 221, 13);
        uint64_t r41 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n40);
        bool o42 = (uint64_t)(r41 >> (int)n40) != ((uint64_t)1ULL);
        if (o42) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 221, 13);
        uint64_t t43 = r41;
        uint8_t t44 = (t39 < t43);
        if (!t44) goto ie38;
        {
            uint32_t t46 = gm_l1;
            uint64_t t47 = gm_l3;
            uint32_t t48 = (uint32_t)t47;
            uint32_t t49 = (uint32_t)(t46 | t48);
            uint32_t t50 = t49;
            float t51;
            memset(&t51, 0, sizeof t51); memcpy(&t51, ((uint8_t *)&t50), sizeof t51 < 4 ? sizeof t51 : 4);
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        int64_t t53 = gm_l4;
        int64_t r54; bool o55 = __builtin_add_overflow(t53, ((int64_t)23LL), &r54);
        if (o55) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 222, 15);
        int64_t t56 = r54;
        int64_t r57; bool o58 = __builtin_add_overflow(t56, ((int64_t)127LL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 222, 15);
        int64_t t59 = r57;
        uint32_t t60 = (uint32_t)t59;
        gm_l5 = t60;
    }
    {
        uint32_t t61 = gm_l1;
        uint32_t t62 = gm_l5;
        unsigned __int128 n63 = (unsigned __int128)((uint64_t)23ULL);
        if (n63 >= 32) gm_trap_shift((uint64_t)n63, "u32", gm_src6b5gn9iggr1p, 223, 21);
        uint32_t r64 = (uint32_t)((uint32_t)t62 << (int)n63);
        bool o65 = (uint32_t)(r64 >> (int)n63) != t62;
        uint32_t t66 = r64;
        uint32_t t67 = (uint32_t)(t61 | t66);
        uint64_t t68 = gm_l3;
        uint32_t t69 = (uint32_t)t68;
        unsigned __int128 n70 = (unsigned __int128)((uint64_t)23ULL);
        if (n70 >= 32) gm_trap_shift((uint64_t)n70, "u32", gm_src6b5gn9iggr1p, 223, 54);
        uint32_t r71 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n70);
        bool o72 = (uint32_t)(r71 >> (int)n70) != ((uint32_t)1ULL);
        uint32_t t73 = r71;
        uint32_t r74; bool o75 = __builtin_sub_overflow(t69, t73, &r74);
        if (o75) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 223, 38);
        uint32_t t76 = r74;
        uint32_t t77 = (uint32_t)(t67 | t76);
        uint32_t t78 = t77;
        float t79;
        memset(&t79, 0, sizeof t79); memcpy(&t79, ((uint8_t *)&t78), sizeof t79 < 4 ? sizeof t79 : 4);
        uint8_t t80 = 0;
        F.ret = t79;
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
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_f64_4avhrht F;
    uint64_t gm_l1; /* sign */
    gm_tomokrc78hhu1 gm_l2; /* s */
    uint64_t gm_l3; /* m */
    int64_t gm_l4; /* ue */
    uint64_t gm_l5; /* field */
    GM_STACK_CHECK(&F, 377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        uint8_t t3 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)a0)));
        if (!t3) goto ie2;
        {
            gm_l1 = ((uint64_t)9223372036854775808ULL);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_round_scaled_7btaume((void *)&gm_l2, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)a0)), ((uint32_t)53ULL), ((int64_t)-1022LL), ((int64_t)1023LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 16));
        if (!t6) goto ie5;
        {
            uint64_t t8 = gm_l1;
            uint64_t t9 = (uint64_t)(t8 | ((uint64_t)9218868437227405312ULL));
            uint64_t t10 = t9;
            double t11;
            memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 8 ? sizeof t11 : 8);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie13;
        {
            uint64_t t17 = gm_l1;
            uint64_t t18 = t17;
            double t19;
            memset(&t19, 0, sizeof t19); memcpy(&t19, ((uint8_t *)&t18), sizeof t19 < 8 ? sizeof t19 : 8);
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        gm_l3 = t21;
    }
    {
        int64_t t22 = GM_LD(int64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l4 = t22;
    }
    {
        uint64_t t24 = gm_l3;
        unsigned __int128 n25 = (unsigned __int128)((uint64_t)53ULL);
        if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_src6b5gn9iggr1p, 195, 14);
        uint64_t r26 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n25);
        bool o27 = (uint64_t)(r26 >> (int)n25) != ((uint64_t)1ULL);
        if (o27) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 195, 14);
        uint64_t t28 = r26;
        uint8_t t29 = (t24 >= t28);
        if (!t29) goto ie23;
        {
            uint64_t t31 = gm_l3;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)1ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_src6b5gn9iggr1p, 197, 9);
            uint64_t t33 = (uint64_t)(t31 >> (int)n32);
            gm_l3 = t33;
        }
        {
            int64_t t34 = gm_l4;
            int64_t r35; bool o36 = __builtin_add_overflow(t34, ((int64_t)1LL), &r35);
            if (o36) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 198, 9);
            int64_t t37 = r35;
            gm_l4 = t37;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t39 = gm_l3;
        unsigned __int128 n40 = (unsigned __int128)((uint64_t)52ULL);
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_src6b5gn9iggr1p, 200, 13);
        uint64_t r41 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n40);
        bool o42 = (uint64_t)(r41 >> (int)n40) != ((uint64_t)1ULL);
        if (o42) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 200, 13);
        uint64_t t43 = r41;
        uint8_t t44 = (t39 < t43);
        if (!t44) goto ie38;
        {
            uint64_t t46 = gm_l1;
            uint64_t t47 = gm_l3;
            uint64_t t48 = (uint64_t)(t46 | t47);
            uint64_t t49 = t48;
            double t50;
            memset(&t50, 0, sizeof t50); memcpy(&t50, ((uint8_t *)&t49), sizeof t50 < 8 ? sizeof t50 : 8);
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        int64_t t52 = gm_l4;
        int64_t r53; bool o54 = __builtin_add_overflow(t52, ((int64_t)52LL), &r53);
        if (o54) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 204, 15);
        int64_t t55 = r53;
        int64_t r56; bool o57 = __builtin_add_overflow(t55, ((int64_t)1023LL), &r56);
        if (o57) gm_trap("integer overflow in `+`", gm_src6b5gn9iggr1p, 204, 15);
        int64_t t58 = r56;
        uint64_t t59 = (uint64_t)t58;
        gm_l5 = t59;
    }
    {
        uint64_t t60 = gm_l1;
        uint64_t t61 = gm_l5;
        unsigned __int128 n62 = (unsigned __int128)((uint64_t)52ULL);
        if (n62 >= 64) gm_trap_shift((uint64_t)n62, "u64", gm_src6b5gn9iggr1p, 205, 21);
        uint64_t r63 = (uint64_t)((uint64_t)t61 << (int)n62);
        bool o64 = (uint64_t)(r63 >> (int)n62) != t61;
        if (o64) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 205, 21);
        uint64_t t65 = r63;
        uint64_t t66 = (uint64_t)(t60 | t65);
        uint64_t t67 = gm_l3;
        unsigned __int128 n68 = (unsigned __int128)((uint64_t)52ULL);
        if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_src6b5gn9iggr1p, 205, 42);
        uint64_t r69 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n68);
        bool o70 = (uint64_t)(r69 >> (int)n68) != ((uint64_t)1ULL);
        if (o70) gm_trap("integer overflow in `<<`", gm_src6b5gn9iggr1p, 205, 42);
        uint64_t t71 = r69;
        uint64_t r72; bool o73 = __builtin_sub_overflow(t67, t71, &r72);
        if (o73) gm_trap("integer overflow in `-`", gm_src6b5gn9iggr1p, 205, 37);
        uint64_t t74 = r72;
        uint64_t t75 = (uint64_t)(t66 | t74);
        uint64_t t76 = t75;
        double t77;
        memset(&t77, 0, sizeof t77); memcpy(&t77, ((uint8_t *)&t76), sizeof t77 < 8 ? sizeof t77 : 8);
        uint8_t t78 = 0;
        F.ret = t77;
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
inline __attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1) {
    struct gm_fr_to_float_o1iop8t F;
    uint16_t gm_l1; /* bits */
    GM_STACK_CHECK(&F, 55u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)16ULL)));
        if (!t4) goto ie2;
        {
            double t6 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint16_t t9 = gm_l1;
        uint8_t t10 = ((t9 == ((uint16_t)32ULL)));
        if (!t10) goto ie8;
        {
            float t12 = gm_f_to_f32_8bnhsji((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            double t13 = (double)t12;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        double t15 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1) {
    struct gm_fr_write_v26t591 F;
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t5 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
        if (!t5) goto ie4;
        {
            gm_b0_1 t7;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t9 = gm_l1;
        gm_tftb1odc739t5 t10 = gm_f_push_t83rg2m(t9, ((uint8_t)47ULL));
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src6b5gn9iggr1p, 130, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        void * t13 = gm_l1;
        gm_b0_1 t14 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 48)), t13);
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
extern __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
