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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_b16_8 a[7]; } gm_tdv3dkjvo1l7t;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tdv3dkjvo1l7t) == 112 && _Alignof(gm_tdv3dkjvo1l7t) == 8 && offsetof(gm_tdv3dkjvo1l7t, a) == 0, "layout of `[7]string`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; } gm_tji6flc1b40vj;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
_Static_assert(sizeof(gm_tji6flc1b40vj) == 56 && _Alignof(gm_tji6flc1b40vj) == 8 && offsetof(gm_tji6flc1b40vj, f0) == 0 && offsetof(gm_tji6flc1b40vj, f1) == 16 && offsetof(gm_tji6flc1b40vj, f2) == 32, "layout of `TreeEntry`");
struct gm_fr_chars_232hkk6 {
    gm_frame h;
    uint64_t l1; /* n */
    uint64_t ret;
    gm_b0_1 w0;
};
struct gm_fr_check_link_chains_sttldqn {
    gm_frame h;
    gm_tji6flc1b40vj l3; /* e */
    gm_trqfvmjtt4sij l5; /* dir */
    gm_tl572d7579hns ret;
    gm_b0_1 w1;
};
struct gm_fr_did_close_f9lpcgi {
    gm_frame h;
    gm_b16_8 l5; /* dir */
    gm_trqfvmjtt4sij l8; /* old */
    gm_trqfvmjtt4sij l9; /* others */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_1vrv15m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_d80h595 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_g303nmp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_sf9naq7 {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_tv3u4f0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_path_less_il538gm {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l4; /* y */
    uint8_t ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
struct gm_fr_unpack_into_kqij1d2 {
    gm_frame h;
    gm_t9bmh80edou9c l6; /* names_args */
    gm_tipro5npl6rd8 l7; /* names_run */
    gm_t9bmh80edou9c l8; /* types_args */
    gm_tipro5npl6rd8 l9; /* types_run */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l12; /* types */
    gm_tdv3dkjvo1l7t l21; /* args */
    gm_tipro5npl6rd8 l22; /* r */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
gm_t3mf58gv9kf3n gm_dt3t1u951rul_0(void *env, gm_node *cx, gm_node *inst);
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n9765ef(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
extern uint8_t gm_c05hsm9hbk3[13] __attribute__((aligned(1)));
extern uint8_t gm_c1e13bm8153[15] __attribute__((aligned(1)));
extern uint8_t gm_c1m4oddes6p[6] __attribute__((aligned(1)));
extern uint8_t gm_c24smpco9rb[39] __attribute__((aligned(1)));
extern uint8_t gm_c2pkvkqaboa[13] __attribute__((aligned(1)));
extern uint8_t gm_c30tdq2p48i[1] __attribute__((aligned(1)));
extern uint8_t gm_c3d3485bcf8[11] __attribute__((aligned(1)));
extern uint8_t gm_c43le3enhbn[4] __attribute__((aligned(1)));
extern uint8_t gm_c4e612inl87[7] __attribute__((aligned(1)));
extern uint8_t gm_c5ad7a620rp[7] __attribute__((aligned(1)));
extern uint8_t gm_c5c748e2jnu[18] __attribute__((aligned(1)));
extern uint8_t gm_c6h27aetc34[8] __attribute__((aligned(1)));
extern uint8_t gm_c6m8tnj6ja2[3] __attribute__((aligned(1)));
extern uint8_t gm_c6r62i0838h[15] __attribute__((aligned(1)));
extern uint8_t gm_c7eeuhrqkm5[6] __attribute__((aligned(1)));
extern uint8_t gm_c7jg7fig0r2[2] __attribute__((aligned(1)));
extern uint8_t gm_c7mc3uaor1r_2[80] __attribute__((aligned(8)));
extern uint8_t gm_c866866ffqn[5] __attribute__((aligned(1)));
extern uint8_t gm_c8g6p8jp14b[96] __attribute__((aligned(8)));
extern uint8_t gm_c8je55vlhur[2] __attribute__((aligned(1)));
extern uint8_t gm_c9j710of7ks[5] __attribute__((aligned(1)));
extern uint8_t gm_ca31fp0eevd[14] __attribute__((aligned(1)));
extern uint8_t gm_cal7njs7es4[16] __attribute__((aligned(8)));
extern uint8_t gm_cb60tlkkq8o[152] __attribute__((aligned(8)));
extern uint8_t gm_cbvqd04tf7m[14] __attribute__((aligned(1)));
extern uint8_t gm_ccln5rkhiat[4] __attribute__((aligned(1)));
extern uint8_t gm_cd0k2qvgiv6[14] __attribute__((aligned(1)));
extern uint8_t gm_cdb4ji8adev[14] __attribute__((aligned(1)));
extern uint8_t gm_cdmg1tph9ts[4] __attribute__((aligned(1)));
extern uint8_t gm_cdvqet46qb2[1697] __attribute__((aligned(1)));
extern uint8_t gm_ce3m8cj68o5[96] __attribute__((aligned(1)));
extern uint8_t gm_ceht42n5ud9[11] __attribute__((aligned(1)));
extern uint8_t gm_cf36eeqbs3g[49] __attribute__((aligned(1)));
extern uint8_t gm_cf7tq30884g[3] __attribute__((aligned(1)));
extern uint8_t gm_cf807b860br[24] __attribute__((aligned(8)));
extern uint8_t gm_cfl6qoievuv[20] __attribute__((aligned(1)));
extern uint8_t gm_cfrmv39c8o8[5] __attribute__((aligned(1)));
extern uint8_t gm_cg59ak5330k[6] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e_5[16] __attribute__((aligned(8)));
extern uint8_t gm_chb4ud0t7b8[14] __attribute__((aligned(1)));
extern uint8_t gm_chsnc6dou61[8] __attribute__((aligned(1)));
extern uint8_t gm_ci39cltltdi[8] __attribute__((aligned(8)));
extern uint8_t gm_cj38t56vaf8[9] __attribute__((aligned(1)));
extern uint8_t gm_cj5upfc0qar[19] __attribute__((aligned(1)));
extern uint8_t gm_cj9acci4r11[13] __attribute__((aligned(1)));
extern uint8_t gm_cjfp4khah06[80] __attribute__((aligned(8)));
extern uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
extern uint8_t gm_cjnbk2fbr2e[8] __attribute__((aligned(1)));
extern uint8_t gm_cmb1h2b437i[32] __attribute__((aligned(1)));
extern uint8_t gm_cndnhrbr4l9[13] __attribute__((aligned(1)));
extern uint8_t gm_cnfi0pu838c[18] __attribute__((aligned(1)));
extern uint8_t gm_cnlq7csqqrf[1] __attribute__((aligned(8)));
extern uint8_t gm_cpgkkof10tf[8] __attribute__((aligned(1)));
extern uint8_t gm_cposd8mg3i2[9] __attribute__((aligned(1)));
extern uint8_t gm_cpvhkqdiis0[7] __attribute__((aligned(1)));
extern uint8_t gm_cq1a48dg0dm[11] __attribute__((aligned(1)));
extern uint8_t gm_cqs26buo28m[9] __attribute__((aligned(1)));
extern uint8_t gm_cs26fgf43so[16] __attribute__((aligned(1)));
extern uint8_t gm_csn873hhpsk[8] __attribute__((aligned(1)));
extern uint8_t gm_csugnm93cnb[6] __attribute__((aligned(1)));
extern uint8_t gm_ct5id4947nt[144] __attribute__((aligned(8)));
extern uint8_t gm_ctbqbqsgd4s[8] __attribute__((aligned(1)));
extern uint8_t gm_cu60un6gh68[12] __attribute__((aligned(1)));
extern uint8_t gm_cuhj0mdsgck[41] __attribute__((aligned(1)));
extern uint8_t gm_cul3e2cnjhd[13] __attribute__((aligned(1)));
extern uint8_t gm_cuqbj62gqhf_3[16] __attribute__((aligned(8)));
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hl8ph80s8bgt[];
extern void *const gm_hlhtabotcjhg[];
extern void *const gm_hlnaa5ptob1s[];
extern void *const gm_hlp1oga9ehto[];
extern void *const gm_hlq1reae6pf9[];
extern void *const gm_hlth8t37utgd[];
extern void *const gm_hlvedue473cr[];
extern uint8_t gm_k08q1k9tq78[40] __attribute__((aligned(8)));
extern uint8_t gm_k2afmplo64l[40] __attribute__((aligned(8)));
extern uint8_t gm_k30q9glr4ue[40] __attribute__((aligned(8)));
extern uint8_t gm_k3n1bpabq7f[40] __attribute__((aligned(8)));
extern uint8_t gm_k66sf6gvqqo[40] __attribute__((aligned(8)));
extern uint8_t gm_k6khjohkcto[40] __attribute__((aligned(8)));
extern uint8_t gm_k7bjnihrifk[40] __attribute__((aligned(8)));
extern uint8_t gm_k9foool24ve[40] __attribute__((aligned(8)));
extern uint8_t gm_kav3dhibiev[40] __attribute__((aligned(8)));
extern uint8_t gm_kbfuu8gel4m[40] __attribute__((aligned(8)));
extern uint8_t gm_kcnkkrl6ngl[40] __attribute__((aligned(8)));
extern uint8_t gm_kh1ssdqq46n[40] __attribute__((aligned(8)));
extern uint8_t gm_khb6ap185f0[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqc35oao72[40] __attribute__((aligned(8)));
extern uint8_t gm_kkm55bntrhj[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8b626vj63[40] __attribute__((aligned(8)));
extern uint8_t gm_klkc6gi5fh5[40] __attribute__((aligned(8)));
extern uint8_t gm_kp923qoaivn[40] __attribute__((aligned(8)));
extern uint8_t gm_kqfbmlr3820[40] __attribute__((aligned(8)));
extern uint8_t gm_kqj05ihe9qd[40] __attribute__((aligned(8)));
extern uint8_t gm_kqn03v7t2qt[40] __attribute__((aligned(8)));
extern uint8_t gm_krt0ppukvqv[40] __attribute__((aligned(8)));
extern uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
extern uint8_t gm_ktu15664o6a[40] __attribute__((aligned(8)));
extern uint8_t gm_kukq3ep6ok2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s01fuch54uo3k[11];
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s07iqjq52hems[5];
extern const uint8_t gm_s0886ibkvne07[52];
extern const uint8_t gm_s0ajsem4h8nft[58];
extern const uint8_t gm_s0dggtblbl7jq[30];
extern const uint8_t gm_s0g8veu8021ch[3];
extern const uint8_t gm_s0i05enq0tmg0[44];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0j9p1q607a2j[4];
extern const uint8_t gm_s0k6n2snpiqj9[12];
extern const uint8_t gm_s0ncqips7lm6r[8];
extern const uint8_t gm_s0of5gcf1i7ic[29];
extern const uint8_t gm_s0r71f5repum2[21];
extern const uint8_t gm_s0rru461jb8nu[7];
extern const uint8_t gm_s0sivi9n1gpid[74];
extern const uint8_t gm_s0thiorolmqbb[14];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s104g112fsi3t[3];
extern const uint8_t gm_s1448opa4b1od[17];
extern const uint8_t gm_s19m2tlro6kap[45];
extern const uint8_t gm_s1b2h3bb9to0f[10];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1de3v91lp97g[106];
extern const uint8_t gm_s1fgl5v192ho5[81];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s1h6moflekgjg[15];
extern const uint8_t gm_s1i6nhfib8r9u[11];
extern const uint8_t gm_s1j5pceke7iuk[5];
extern const uint8_t gm_s1oiqt105ulvb[20];
extern const uint8_t gm_s1olfuo31ml9r[64];
extern const uint8_t gm_s1pg64k1de1co[15];
extern const uint8_t gm_s1r9a0bclv408[17];
extern const uint8_t gm_s1vcu5tk6vbma[67];
extern const uint8_t gm_s227dnd7n32vg[12];
extern const uint8_t gm_s25tpihkedo40[12];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s27qdg2qrqsr9[25];
extern const uint8_t gm_s28ggq4c4bg4u[32];
extern const uint8_t gm_s2aov9df7ef50[53];
extern const uint8_t gm_s2c24uim36k06[36];
extern const uint8_t gm_s2ct6k0fls3pb[12];
extern const uint8_t gm_s2f5ea3l0ep8h[9];
extern const uint8_t gm_s2ha7brjf2gmp[7];
extern const uint8_t gm_s2jlhlq33qoal[12];
extern const uint8_t gm_s2kg2oqqdqf2r[23];
extern const uint8_t gm_s2l500n2vri8f[77];
extern const uint8_t gm_s2nmq6ak9nl9d[5];
extern const uint8_t gm_s2sodn6uclrlt[63];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s302tqgcs0oai[49];
extern const uint8_t gm_s3123vvv2kgff[20];
extern const uint8_t gm_s34sbia23trj3[3];
extern const uint8_t gm_s35u3nlsc2qa0[27];
extern const uint8_t gm_s38jl7kelm73v[19];
extern const uint8_t gm_s39bdeo03pq18[9];
extern const uint8_t gm_s3d3pns6au9om[61];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s3kumt3nslbnp[9];
extern const uint8_t gm_s3m5ql5voef28[6];
extern const uint8_t gm_s3ml7p21f55q7[14];
extern const uint8_t gm_s3ng5nguvd452[5];
extern const uint8_t gm_s3nnnhoq632g4[14];
extern const uint8_t gm_s3pj34lce6rqp[10];
extern const uint8_t gm_s3plkb3in5251[9];
extern const uint8_t gm_s3qu2ujaek2lq[41];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s433110cvrv0u[5];
extern const uint8_t gm_s44unipo34g8t[20];
extern const uint8_t gm_s47gv9lvssm4o[13];
extern const uint8_t gm_s48l8h2aj8b20[41];
extern const uint8_t gm_s4bv7heri08lj[24];
extern const uint8_t gm_s4h6hphsrrhvo[14];
extern const uint8_t gm_s4hs9h62coiag[4001];
extern const uint8_t gm_s4mghosb8ogmj[56];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4ujp5fsfjtut[8];
extern const uint8_t gm_s4un3os52adn8[18];
extern const uint8_t gm_s4v9747a9b85a[11];
extern const uint8_t gm_s50qkjucptpgl[8];
extern const uint8_t gm_s51jnn7biksj8[68];
extern const uint8_t gm_s52ntohgadejq[12];
extern const uint8_t gm_s52od60hi0cpi[21];
extern const uint8_t gm_s541gi95epov5[25];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s55ncecttah5f[69];
extern const uint8_t gm_s57rgvjb5mj5a[8];
extern const uint8_t gm_s5a6hd5ouael9[37];
extern const uint8_t gm_s5detsdj9r6dm[25];
extern const uint8_t gm_s5g0cpgo64peg[104];
extern const uint8_t gm_s5gl145lqr39l[15];
extern const uint8_t gm_s5iqbpk190ist[14];
extern const uint8_t gm_s5k0vuc678fh1[8];
extern const uint8_t gm_s5ol08dgk75gg[17];
extern const uint8_t gm_s5rf946siff22[29];
extern const uint8_t gm_s677r76pa1oks[7];
extern const uint8_t gm_s67lg9pjq86ie[55];
extern const uint8_t gm_s68uiiatavvqh[18];
extern const uint8_t gm_s6aq8jknpl0n5[27];
extern const uint8_t gm_s6clc0pmaeksj[18];
extern const uint8_t gm_s6d7j0gejjq91[41];
extern const uint8_t gm_s6jlko9gsl7im[26];
extern const uint8_t gm_s6kdp6nkaj7o9[12];
extern const uint8_t gm_s6m88j5u3flig[6];
extern const uint8_t gm_s6mvc5jp4o5mi[30];
extern const uint8_t gm_s6n2mkqhnsjpk[10];
extern const uint8_t gm_s6nqhdcqlaqpu[44];
extern const uint8_t gm_s6qbnfe2f321h[4];
extern const uint8_t gm_s6sdj2gjsdah5[18];
extern const uint8_t gm_s6tdh0p5an5le[51];
extern const uint8_t gm_s6tn8kl12f5ru[27];
extern const uint8_t gm_s6unagr666v56[22];
extern const uint8_t gm_s72tasmt4pg49[26];
extern const uint8_t gm_s73f72842a67m[45];
extern const uint8_t gm_s73jhka2t3avj[41];
extern const uint8_t gm_s75gqsar0656b[50];
extern const uint8_t gm_s76hg0po5usup[9];
extern const uint8_t gm_s76i2uidi0pjj[54];
extern const uint8_t gm_s77ek30pr0obt[70];
extern const uint8_t gm_s78m6s8sukfpa[13];
extern const uint8_t gm_s799mjibj8396[10];
extern const uint8_t gm_s7d1d0uk71q3u[19];
extern const uint8_t gm_s7ifc1fku673p[5];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s7o7e6piqr3hn[14];
extern const uint8_t gm_s7r1917rmmif7[21];
extern const uint8_t gm_s7ri8monh3c4h[8];
extern const uint8_t gm_s7st0fkehmujt[69];
extern const uint8_t gm_s7tgpca9g8npn[11];
extern const uint8_t gm_s7unnudd0kkri[14];
extern const uint8_t gm_s7upcfgaqqnlq[433];
extern const uint8_t gm_s7uql3407tind[10];
extern const uint8_t gm_s81r46mpa8ra9[29];
extern const uint8_t gm_s846hbv2ad5li[17];
extern const uint8_t gm_s86s8nstj5qij[54];
extern const uint8_t gm_s8abg3vemofr6[4];
extern const uint8_t gm_s8f0u3tjempi4[53];
extern const uint8_t gm_s8gtcifve79ai[9];
extern const uint8_t gm_s8hh4pp2rpnlj[43];
extern const uint8_t gm_s8k87f60ciakv[41];
extern const uint8_t gm_s8ooubi1nm7vq[5];
extern const uint8_t gm_s8ph5bt10ogvd[10];
extern const uint8_t gm_s8q0uvsg4b37p[41];
extern const uint8_t gm_s8qs8rpj34te9[28];
extern const uint8_t gm_s8t43j22je03d[41];
extern const uint8_t gm_s8tq0rdmthkth[16];
extern const uint8_t gm_s8u6lv8hq3sn2[23];
extern const uint8_t gm_s91vt5u8rghg3[3];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_s948vtbtkq957[32];
extern const uint8_t gm_s9646k6ls9lbt[51];
extern const uint8_t gm_s9659cpb4brmg[36];
extern const uint8_t gm_s98a5ud3vqhvr[85];
extern const uint8_t gm_s99scb3gc7vkm[29];
extern const uint8_t gm_s9a0n41a8i82u[4];
extern const uint8_t gm_s9ab1ohnfcjkc[6];
extern const uint8_t gm_s9agk8q084c5k[8];
extern const uint8_t gm_s9b8bk12k1acq[53];
extern const uint8_t gm_s9d2uvo0cndd7[4];
extern const uint8_t gm_s9e7dd3qkr93v[41];
extern const uint8_t gm_s9j4gjjc4pmge[41];
extern const uint8_t gm_s9jd8cvhje3jm[26];
extern const uint8_t gm_s9kclu2dkmivs[24];
extern const uint8_t gm_s9nm0mpn8r8ov[40];
extern const uint8_t gm_s9qbs07c2ca5q[5];
extern const uint8_t gm_s9u3cqhrfmcru[13];
extern const uint8_t gm_s9v7ektal8f2e[18];
extern const uint8_t gm_sa09bp3uo6b22[25];
extern const uint8_t gm_sa26g0ksgdkma[10];
extern const uint8_t gm_sa53epbg9iarq[10];
extern const uint8_t gm_sa5lo120id503[26];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sa9s55cr1nc6b[1698];
extern const uint8_t gm_sabu5uehm65n2[22];
extern const uint8_t gm_sacmu7hr5dknu[88];
extern const uint8_t gm_sad9qq5tf3nqa[36];
extern const uint8_t gm_sak07p89ce10t[10];
extern const uint8_t gm_sakkgen9m77d6[16];
extern const uint8_t gm_sandsuergllg3[30];
extern const uint8_t gm_sap32k6of7kh3[66];
extern const uint8_t gm_sar77rocuptb5[31];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_savi5cfvaqp3a[15];
extern const uint8_t gm_savs0ep448c40[33];
extern const uint8_t gm_sb7685j14iksa[41];
extern const uint8_t gm_sb7t8ia5pj1l5[19];
extern const uint8_t gm_sb809qf68db4f[9];
extern const uint8_t gm_sb85ua3p3jm63[9];
extern const uint8_t gm_sbaogg9p69n9f[38];
extern const uint8_t gm_sbcjfjm1sdn3u[42];
extern const uint8_t gm_sbe2lidu87fed[61];
extern const uint8_t gm_sbgqovvv31o11[14];
extern const uint8_t gm_sbh7smhjots7v[9];
extern const uint8_t gm_sbol12a8gjr17[9];
extern const uint8_t gm_sbsmevjmneb3d[55];
extern const uint8_t gm_sbstgmps1gd24[10];
extern const uint8_t gm_sbtpqmkmtr4s9[38];
extern const uint8_t gm_sc092629f9on4[15];
extern const uint8_t gm_sc0i66qc606nr[13];
extern const uint8_t gm_sc24ftfhihd1h[4];
extern const uint8_t gm_sc2tdpp2mbdis[2];
extern const uint8_t gm_sc30ktgbs45q0[4];
extern const uint8_t gm_sc6uqn3rvm1p8[56];
extern const uint8_t gm_sc829ikr3c206[10];
extern const uint8_t gm_sc8v94psr8v3s[21];
extern const uint8_t gm_sc92utnrdaeo1[12];
extern const uint8_t gm_scbq2ng311vcu[18];
extern const uint8_t gm_scela8hmfe29j[9];
extern const uint8_t gm_scjbu6gm2hrhf[8];
extern const uint8_t gm_sclhssivsh27a[37];
extern const uint8_t gm_scon5d5hl81e6[17];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_scq4j3j3c00j6[41];
extern const uint8_t gm_scq91tgm9lph4[6];
extern const uint8_t gm_scrj4ri245rj1[41];
extern const uint8_t gm_scrjehcehrfpv[15];
extern const uint8_t gm_scsl23nf1o7fu[13];
extern const uint8_t gm_scvmbht29gva9[7];
extern const uint8_t gm_sd28nb0n7lqmc[13];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sd7p31mtitiq9[3];
extern const uint8_t gm_sda02md5lagkh[11];
extern const uint8_t gm_sdrrh4gk917l3[10];
extern const uint8_t gm_sds0tnbefk9cl[20];
extern const uint8_t gm_sdvkaj73n9goh[24];
extern const uint8_t gm_se055duut4b83[32];
extern const uint8_t gm_se2a9lqlhae47[22];
extern const uint8_t gm_se6hcvav1sk3o[5];
extern const uint8_t gm_se7ecnl2agot9[12];
extern const uint8_t gm_se84lb2ddmuai[21];
extern const uint8_t gm_se8us8vie9a8a[37];
extern const uint8_t gm_sedqspk7o41v4[3];
extern const uint8_t gm_sefk7cselekki[3];
extern const uint8_t gm_sekmurq2ua2sf[6];
extern const uint8_t gm_sel5oh1k2839e[12];
extern const uint8_t gm_semavgcmso3pp[33];
extern const uint8_t gm_sencmbgnvrve5[18];
extern const uint8_t gm_seplhq3i7l38b[195];
extern const uint8_t gm_sescofr8m4g3o[5];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sf0f3lvsd76jf[17];
extern const uint8_t gm_sf1f5se9222sg[35];
extern const uint8_t gm_sf3ii7gekd9ll[13];
extern const uint8_t gm_sf4ahu9mfer7v[5];
extern const uint8_t gm_sf6buv3gbt82t[29];
extern const uint8_t gm_sfa00o5gkcj29[5];
extern const uint8_t gm_sfc624hllrs4g[6];
extern const uint8_t gm_sfhbo1m6f9ql4[35];
extern const uint8_t gm_sfjc41f66fqb2[10];
extern const uint8_t gm_sfjuf1jo6fusr[8];
extern const uint8_t gm_sfkrpkv21b1rq[4];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfll40jgr4opl[4];
extern const uint8_t gm_sfmm1kh41nanh[15];
extern const uint8_t gm_sfmov9pb6t5lt[14];
extern const uint8_t gm_sfocmetde6c9f[26];
extern const uint8_t gm_sfu1s4a4bo4he[25];
extern const uint8_t gm_sg5khilhaakst[16];
extern const uint8_t gm_sg6dqv3rmh13r[59];
extern const uint8_t gm_sg71mqv9mtu78[33];
extern const uint8_t gm_sg9fnspse4evr[22];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgcj0gjfdljat[11];
extern const uint8_t gm_sggmh06s4b0cs[23];
extern const uint8_t gm_sgksioc7kn741[22];
extern const uint8_t gm_sgp5gt1voa0tq[12];
extern const uint8_t gm_sgr8khb1g0pr9[17];
extern const uint8_t gm_sgskdjmsdpnlb[19];
extern const uint8_t gm_sgu2tengos987[41];
extern const uint8_t gm_sgvln0vb20p94[17];
extern const uint8_t gm_sh0h8t5us2qkm[15];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_sh4ufq9bak51r[13];
extern const uint8_t gm_sh54qlvpeqnin[10];
extern const uint8_t gm_sh9sks72142ms[17];
extern const uint8_t gm_shb3g0iah7668[18];
extern const uint8_t gm_shb5mqv6etf1t[5];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shm5oj4temhh6[33];
extern const uint8_t gm_shq8kvmdjhmfg[4];
extern const uint8_t gm_shqn2dgvujf5o[13];
extern const uint8_t gm_shr0v68m3hbkr[16];
extern const uint8_t gm_shuj8drmk4t93[9];
extern const uint8_t gm_shv2qb39iqelf[31];
extern const uint8_t gm_shvoch74oh0kq[59];
extern const uint8_t gm_shvp36midgf58[10];
extern const uint8_t gm_si2i26md2o6k8[26];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_si8shacml9b6h[14];
extern const uint8_t gm_sicfped8s368m[58];
extern const uint8_t gm_sicq1bvp13t5d[23];
extern const uint8_t gm_sideqrlbldrh7[26];
extern const uint8_t gm_sie1pq1m4mo8b[29];
extern const uint8_t gm_sijvlope91krv[31];
extern const uint8_t gm_sikg061j3im7q[2];
extern const uint8_t gm_siklni1kauva6[6];
extern const uint8_t gm_sing8pfld8brt[3];
extern const uint8_t gm_sinoph5a0op58[10];
extern const uint8_t gm_sisjr6ep7j5j0[79];
extern const uint8_t gm_sj2pdhqdnnh86[3];
extern const uint8_t gm_sj4o6222mnbmq[16];
extern const uint8_t gm_sj815koajgkmb[6];
extern const uint8_t gm_sj8ni1it038ec[18];
extern const uint8_t gm_sj8ptihcm0srb[51];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_sjds8o3l7mmnl[5];
extern const uint8_t gm_sjguhtmsnuhlj[58];
extern const uint8_t gm_sjig620oug367[9];
extern const uint8_t gm_sjk9nricijji8[45];
extern const uint8_t gm_sjkg27kmogh66[7];
extern const uint8_t gm_sjn4krvshoajd[26];
extern const uint8_t gm_sjnl36ocas0o1[11];
extern const uint8_t gm_sjrimf03ml0ct[13];
extern const uint8_t gm_sk2dpusgkm375[22];
extern const uint8_t gm_sk4bpl5a3cqoi[23];
extern const uint8_t gm_sk4tdtvin0g62[31];
extern const uint8_t gm_sk71kk4665fg9[34];
extern const uint8_t gm_sk9qhfibomcn3[48];
extern const uint8_t gm_skdalh0o0hret[59];
extern const uint8_t gm_skfks8nrdfdgv[5];
extern const uint8_t gm_skg16mqffvnq6[4];
extern const uint8_t gm_skijrg68314qg[9];
extern const uint8_t gm_skl3b78lo1gln[5];
extern const uint8_t gm_skl3cimaihkgu[26];
extern const uint8_t gm_skmr2rgcse4gr[14];
extern const uint8_t gm_skqa2fjpbsodp[9];
extern const uint8_t gm_skqb5p67mhjp2[43];
extern const uint8_t gm_skucss3rkslf9[57];
extern const uint8_t gm_skue5huof65fj[80];
extern const uint8_t gm_sl6m7ka86q6ca[12];
extern const uint8_t gm_sl7562rl6kuvh[7];
extern const uint8_t gm_sl935l3jslu0o[28];
extern const uint8_t gm_sl9i9hcgkt8fd[97];
extern const uint8_t gm_sl9mgk5ho4kmr[29];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_sl9pujidpnv0r[4];
extern const uint8_t gm_sla1l2pt1mfvt[3];
extern const uint8_t gm_slbfl8l7n523s[7];
extern const uint8_t gm_slbn3chj3p1e1[49];
extern const uint8_t gm_slfpvqm0f02j6[8];
extern const uint8_t gm_slg1gdqpr4sdg[49];
extern const uint8_t gm_slhn01etuat95[9];
extern const uint8_t gm_slj50kiksg47f[3];
extern const uint8_t gm_sljdair45oqft[31];
extern const uint8_t gm_slku641rnhls8[7];
extern const uint8_t gm_slni8vfv3lequ[22];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_slq0l63qf1dvs[11];
extern const uint8_t gm_slu3lsfv09uua[25];
extern const uint8_t gm_sm139pecc5n3p[6];
extern const uint8_t gm_sm1iup9eb4u28[13];
extern const uint8_t gm_sm30gjvs8dlav[7];
extern const uint8_t gm_sm399snoi2v85[3];
extern const uint8_t gm_sm9otrmuq5hp3[34];
extern const uint8_t gm_smatmsv0oedot[8];
extern const uint8_t gm_smbmfla2idsdo[68];
extern const uint8_t gm_smjg62rnkmnon[21];
extern const uint8_t gm_smlifs2umbvsq[8];
extern const uint8_t gm_smmus0krp2svr[6];
extern const uint8_t gm_smovt8m5cpa13[7];
extern const uint8_t gm_smtmd013g2edr[4];
extern const uint8_t gm_smu1qqop2463k[53];
extern const uint8_t gm_smuh6lae5oil8[68];
extern const uint8_t gm_smv1ru2lmia7k[41];
extern const uint8_t gm_smv20k805bk9c[15];
extern const uint8_t gm_sn47urokcort4[18];
extern const uint8_t gm_sn62ecr79ngvo[7];
extern const uint8_t gm_sn68oj251cu21[38];
extern const uint8_t gm_snd69jrnj9kdn[42];
extern const uint8_t gm_snemlnnsbdb13[46];
extern const uint8_t gm_sni9iu6vnvsjk[44];
extern const uint8_t gm_snk3kkdsb32a9[129];
extern const uint8_t gm_snl4vfgn6qv9m[10];
extern const uint8_t gm_snlqf7m58jjdu[5];
extern const uint8_t gm_snm77g00so1si[12];
extern const uint8_t gm_snvn4jaklkik3[65];
extern const uint8_t gm_so873adl39g7s[19];
extern const uint8_t gm_so8cik47e3h2s[19];
extern const uint8_t gm_soaev64pou0k6[54];
extern const uint8_t gm_sob5t135sl7q9[34];
extern const uint8_t gm_soc64bhm712u9[65];
extern const uint8_t gm_sod1lb2gquo9d[41];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_soh97sflbds87[11];
extern const uint8_t gm_soimgd0jo62fr[28];
extern const uint8_t gm_sokpg5qd8r9va[20];
extern const uint8_t gm_sonhdjr4nc418[10];
extern const uint8_t gm_soq0ecbboa994[21];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_sou4p9915dc16[15];
extern const uint8_t gm_sp02l2e4viomq[26];
extern const uint8_t gm_sp1784q48t8s2[4];
extern const uint8_t gm_sp4518j1p6r9m[19];
extern const uint8_t gm_sp6bf9nji9g1j[9];
extern const uint8_t gm_sp7619gun38fi[63];
extern const uint8_t gm_spb7dvkqr71v2[77];
extern const uint8_t gm_spfg8ncmd7s76[14];
extern const uint8_t gm_spghmfn4rjv14[22];
extern const uint8_t gm_spuk9sqnoo3ai[13];
extern const uint8_t gm_sq0p2ifj52tjf[8];
extern const uint8_t gm_sq0vrp87uto2u[12];
extern const uint8_t gm_sq2ta0ln2qvqo[47];
extern const uint8_t gm_sq8b2uior35pk[12];
extern const uint8_t gm_sq98g0itpb0cn[19];
extern const uint8_t gm_sq9lne9r5j3u2[8];
extern const uint8_t gm_sqg9do4ma3giu[6];
extern const uint8_t gm_sqi7tb51umq69[13];
extern const uint8_t gm_sqof8dgnasnqk[69];
extern const uint8_t gm_sqr1rjjq3dpuh[17];
extern const uint8_t gm_sqt833mkm755s[12];
extern const uint8_t gm_sr12ukrgmispo[10];
extern const uint8_t gm_sr1l176jjo6jp[5];
extern const uint8_t gm_sr23o038u76nd[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr4vl7ov3g7t6[55];
extern const uint8_t gm_sr6i6ans386ga[45];
extern const uint8_t gm_sr7sfbbsona7q[10];
extern const uint8_t gm_srar03v1ldkqi[49];
extern const uint8_t gm_srbj99hcdek7f[18];
extern const char gm_src3s76fqo1a9jk[];
extern const char gm_src4nj79mj9jk57[];
extern const char gm_src7hmnlrtmirgt[];
extern const char gm_src8m8lg034q48k[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srce5lfiiur8udn[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srcgr7vbgb5cqmg[];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcintcpduj3u2g[];
extern const char gm_srcip69en21ht7v[];
extern const char gm_srcn623bb2dv5no[];
extern const char gm_srcngug8co99ok1[];
extern const char gm_srcochlrb3nk64u[];
extern const char gm_srcp0l171g2m7ho[];
extern const char gm_srcslgepk183b7r[];
extern const char gm_srcutmnmd444bu0[];
extern const uint8_t gm_srf1g3vv4d1md[12];
extern const uint8_t gm_srg30626j66d1[140];
extern const uint8_t gm_srgao1ir81l50[14];
extern const uint8_t gm_srjqfuok5f7nb[40];
extern const uint8_t gm_srkbv7g2spehb[33];
extern const uint8_t gm_srli7179p3npp[41];
extern const uint8_t gm_srnn7fn5ngpbk[17];
extern const uint8_t gm_srojjig2k2m5d[13];
extern const uint8_t gm_srua7hn34dkbr[8];
extern const uint8_t gm_srv7efdnr211j[12];
extern const uint8_t gm_srvc4tdn4aqfv[7];
extern const uint8_t gm_srvl2rg1hpk9a[16];
extern const uint8_t gm_ss27lm4pmhcij[45];
extern const uint8_t gm_ss5kcsomld87u[41];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_ss6hmjpokkg23[13];
extern const uint8_t gm_ss9d51lhe1orq[46];
extern const uint8_t gm_ssbb728kngg91[79];
extern const uint8_t gm_sscekt5vbls0f[18];
extern const uint8_t gm_ssdho4pv9rajn[55];
extern const uint8_t gm_sse4og0vn1mc5[33];
extern const uint8_t gm_ssevkle1ss77c[13];
extern const uint8_t gm_ssfhrckkphjsd[6];
extern const uint8_t gm_sskmdsnq9bigv[10];
extern const uint8_t gm_ssld67umkdenq[20];
extern const uint8_t gm_sso4tmqr8gin2[20];
extern const uint8_t gm_ssqbjasrp4m5m[22];
extern const uint8_t gm_ssqd2isjrp8nh[11];
extern const uint8_t gm_ssqmni9f9s44u[34];
extern const uint8_t gm_sssjusbjeb8fr[7];
extern const uint8_t gm_ssud7c614kqjt[5];
extern const uint8_t gm_ssulg3jrm3q2v[16];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st6dd0k5nckh3[58];
extern const uint8_t gm_st85s3r88gecq[14];
extern const uint8_t gm_st8i5uhn201op[37];
extern const uint8_t gm_stb42390ups9k[6];
extern const uint8_t gm_steftqaruo8k5[8];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_stkc6efbvodm1[14];
extern const uint8_t gm_stlra8b089nel[53];
extern const uint8_t gm_stnv0suiruu5q[11];
extern const uint8_t gm_stooh1vej0ram[16];
extern const uint8_t gm_stot0rovnaivg[7];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_strca8t49bpqu[2];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_stv59gn45ivj9[37];
extern const uint8_t gm_stv6f87bn9v0e[7];
extern const uint8_t gm_su0h0fbu8l4ri[30];
extern const uint8_t gm_su28d8kir9fan[10];
extern const uint8_t gm_su43dt5mihp55[22];
extern const uint8_t gm_su58q188fvc2a[12];
extern const uint8_t gm_su5dv07glhc06[37];
extern const uint8_t gm_su5tga7voctrl[7];
extern const uint8_t gm_subnd19q8jmpf[41];
extern const uint8_t gm_sueo2nge7h013[14];
extern const uint8_t gm_suiaoknmgprea[41];
extern const uint8_t gm_sujli4dml3ipq[8];
extern const uint8_t gm_sunk599c58krq[41];
extern const uint8_t gm_suu5j5c20v4ok[15];
extern const uint8_t gm_suvioac7tr94t[10];
extern const uint8_t gm_sv0285a7k6k1l[13];
extern const uint8_t gm_sv4jvo5eoeo8t[7];
extern const uint8_t gm_svdsal9bavsj8[21];
extern const uint8_t gm_svgiumrfk8vkf[73];
extern const uint8_t gm_svivu54u4uhtg[8];
extern const uint8_t gm_svj2mudio0eed[18];
extern const uint8_t gm_svj967tkdfgka[15];
extern const uint8_t gm_svjl1hr0jvvbv[9];
extern const uint8_t gm_svknrff8m4a97[41];
extern const uint8_t gm_svml0iv6h5a82[48];
extern const uint8_t gm_svq539fg740gg[7];
extern const uint8_t gm_svrid2hlmcfue[41];
extern const uint8_t gm_svsbh0404ivr6[6];
extern const uint8_t gm_svtktff6b3snn[3];
extern const uint8_t gm_svurqcoqldl32[83];
extern const gm_type gm_tyo86ks9qkvt;
extern const gm_variant gm_tyo86ks9qkvt_v[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_th2lqbo77pvsc t2;
    gm_f_parent_lk0iero((void *)&t2, (void *)NULL, cx, NULL, t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t3 = t2;
    gm_b16_8 t4;
    if (((((uint8_t *)&t3))[16] != 0)) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
        t4 = t6;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1vrv15m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1vrv15m F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_path_less_il538gm *)((&F.h)->up))->l2);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcgr7vbgb5cqmg, 185, 34);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 185, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_d80h595(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_for_body_d80h595 F;
    uint32_t gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint32_t t2 = t1;
    {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcgr7vbgb5cqmg, 206, 25);
        uint64_t t9 = r7;
        GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_chars_232hkk6 *)((&F.h)->up))->l1), t9);
    }
    goto me3;
mn4:;
    gm_trap("no match arm matched the scrutinee", gm_srcgr7vbgb5cqmg, 206, 5);
me3:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g303nmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_g303nmp F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_link_chains_sttldqn *)((&F.h)->up))->l5);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srcbq9k9kvdokbf, 295, 42);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 295, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_sf9naq7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_sf9naq7 F;
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->l5));
        gm_b16_8 t9 = t7;
        gm_b16_8 t10 = t8;
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        if (!t11) goto ie5;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            ((struct gm_fr_did_close_f9lpcgi *)((&F.h)->up))->ret = t13;
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t14;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 448, 5);
me1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tv3u4f0 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_unpack_into_kqij1d2 *)((&F.h)->up))->l10);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        gm_tftb1odc739t5 t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src3s76fqo1a9jk, 102, 44);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 102, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
const char gm_src4nj79mj9jk57[] = "compiler/sema/soa.gm";
const char gm_src7hmnlrtmirgt[] = "compiler/cgen/vector.gm";
const char gm_src8m8lg034q48k[] = "compiler/parse/lits.gm";
const char gm_srce5lfiiur8udn[] = "compiler/cgen/debug.gm";
const char gm_srch12rfrebvus5[] = "compiler/interp/word.gm";
const char gm_srcintcpduj3u2g[] = "lib/std/hash/sha256/sha256.gm";
const char gm_srcip69en21ht7v[] = "lib/std/arena/arena.gm";
const char gm_srcn623bb2dv5no[] = "compiler/driver/flags.gm";
const char gm_srcngug8co99ok1[] = "compiler/check/methods.gm";
const char gm_srcochlrb3nk64u[] = "compiler/cgen/layout.gm";
const char gm_srcp0l171g2m7ho[] = "compiler/interp/fiber.gm";
const char gm_srcslgepk183b7r[] = "lib/std/text/text.gm";
const char gm_srcutmnmd444bu0[] = "compiler/interp/vm_compile.gm";
const gm_type gm_tyo86ks9qkvt = {.size = 1, .align = 1, .name = "AllocError", .kind = GM_K_UNION, .nvariants = 1, .variants = gm_tyo86ks9qkvt_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyo86ks9qkvt;
const uint8_t gm_s01fuch54uo3k[11] = "return {};";
const uint8_t gm_s02ru83e3fffc[3] = ">=";
const uint8_t gm_s07iqjq52hems[5] = "==> ";
const uint8_t gm_s0886ibkvne07[52] = "a `@write` sink is a `fn(bytes: []const u8)`, not `";
const uint8_t gm_s0ajsem4h8nft[58] = "` can reach the end of its body without returning a value";
const uint8_t gm_s0dggtblbl7jq[30] = "this operator at compile time";
const uint8_t gm_s0g8veu8021ch[3] = "\\b";
const uint8_t gm_s0i05enq0tmg0[44] = "{} has an unknown key `{}` (known keys: {})";
const uint8_t gm_s0iqiubfm7e7e[7] = "string";
const uint8_t gm_s0j9p1q607a2j[4] = "F64";
const uint8_t gm_s0k6n2snpiqj9[12] = "unary_value";
const uint8_t gm_s0ncqips7lm6r[8] = "GIT_DIR";
const uint8_t gm_s0of5gcf1i7ic[29] = "unknown `cache` subcommand `";
const uint8_t gm_s0r71f5repum2[21] = "unsupported source `";
const uint8_t gm_s0rru461jb8nu[7] = "Signed";
const uint8_t gm_s0sivi9n1gpid[74] = "parameters with defaults may be followed only by parameters with defaults";
const uint8_t gm_s0thiorolmqbb[14] = "__powerpc64__";
const uint8_t gm_s0ugop4l95o1j[6] = "store";
const uint8_t gm_s104g112fsi3t[3] = "60";
const uint8_t gm_s1448opa4b1od[17] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000";
const uint8_t gm_s19m2tlro6kap[45] = "`null` matches only optionals, but this is `";
const uint8_t gm_s1b2h3bb9to0f[10] = "they were";
const uint8_t gm_s1bpuqd44lur7[7] = "GM_LD(";
const uint8_t gm_s1de3v91lp97g[106] = "a function pointer type with a calling convention other than C must have effects at most `{Sys}` (\302\24719.2)";
const uint8_t gm_s1fgl5v192ho5[81] = "missing escaped value, expected `b`, `e`, `f`, `n`, `r`, `\\`, `\"`, `x`, `u`, `U`";
const uint8_t gm_s1ft2nrvnhcbp[10] = "trap_hook";
const uint8_t gm_s1h6moflekgjg[15] = "cannot read {}";
const uint8_t gm_s1i6nhfib8r9u[11] = "unclosed `";
const uint8_t gm_s1j5pceke7iuk[5] = "secs";
const uint8_t gm_s1oiqt105ulvb[20] = "GM_NOINLINE static ";
const uint8_t gm_s1olfuo31ml9r[64] = "a spread passes the rest arguments only on its own: `f(a, ..t)`";
const uint8_t gm_s1pg64k1de1co[15] = "gm_net_connect";
const uint8_t gm_s1r9a0bclv408[17] = "gm_filc_starting";
const uint8_t gm_s1vcu5tk6vbma[67] = "` has no methods; `a[..]` is a slice of its elements, which has `.";
const uint8_t gm_s227dnd7n32vg[12] = "includeText";
const uint8_t gm_s25tpihkedo40[12] = " *volatile ";
const uint8_t gm_s26c80h2pm6a7[26] = "rotation of a non-integer";
const uint8_t gm_s27qdg2qrqsr9[25] = "booleans are not ordered";
const uint8_t gm_s28ggq4c4bg4u[32] = "((unsigned __int128)({}) <= {})";
const uint8_t gm_s2aov9df7ef50[53] = "`!p` matches the error of a result, but this is `{}`";
const uint8_t gm_s2c24uim36k06[36] = "cannot find the module directory {}";
const uint8_t gm_s2ct6k0fls3pb[12] = "__aarch64__";
const uint8_t gm_s2f5ea3l0ep8h[9] = "elements";
const uint8_t gm_s2ha7brjf2gmp[7] = ") & 1)";
const uint8_t gm_s2jlhlq33qoal[12] = "write it: `";
const uint8_t gm_s2kg2oqqdqf2r[23] = "unknown lint rule `{}`";
const uint8_t gm_s2l500n2vri8f[77] = "cannot rename `{}` to `{}`: it would hide the declaration `{}` of the module";
const uint8_t gm_s2nmq6ak9nl9d[5] = "'\\''";
const uint8_t gm_s2sodn6uclrlt[63] = "this writes to a function whose target is not known statically";
const uint8_t gm_s2urait735ofq[5] = " :: ";
const uint8_t gm_s302tqgcs0oai[49] = "static __attribute__((aligned(4))) {}{} {}_c({})";
const uint8_t gm_s3123vvv2kgff[20] = "hoist_iterator_call";
const uint8_t gm_s34sbia23trj3[3] = " [";
const uint8_t gm_s35u3nlsc2qa0[27] = "` in the first alternative";
const uint8_t gm_s38jl7kelm73v[19] = "` takes one `bool`";
const uint8_t gm_s39bdeo03pq18[9] = " #align(";
const uint8_t gm_s3d3pns6au9om[61] = "` needs a `path = \"\342\200\246\"`, a `git = \"\342\200\246\"` or a `url = \"\342\200\246\"`";
const uint8_t gm_s3gnqv0up712m[61] = "internal error: `abs_diff` of a value that is not an integer";
const uint8_t gm_s3kumt3nslbnp[9] = "match : ";
const uint8_t gm_s3m5ql5voef28[6] = "{}.{}";
const uint8_t gm_s3ml7p21f55q7[14] = "shift amount ";
const uint8_t gm_s3ng5nguvd452[5] = "Rotl";
const uint8_t gm_s3nnnhoq632g4[14] = "gm_run_export";
const uint8_t gm_s3pj34lce6rqp[10] = "gm_b{}_{}";
const uint8_t gm_s3plkb3in5251[9] = "memcpy(&";
const uint8_t gm_s3qu2ujaek2lq[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s3s50a53ci7rk[7] = "is-not";
const uint8_t gm_s433110cvrv0u[5] = "repo";
const uint8_t gm_s44unipo34g8t[20] = "{} (finished in {})";
const uint8_t gm_s47gv9lvssm4o[13] = "gm_err_errno";
const uint8_t gm_s48l8h2aj8b20[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\030\001\000\0008\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s4bv7heri08lj[24] = "unsupported source `{}`";
const uint8_t gm_s4h6hphsrrhvo[14] = "` given twice";
const uint8_t gm_s4hs9h62coiag[4001] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000)\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000)\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\0003\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\0007\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000&\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\226\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\0003\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000<\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000b\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\222\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000,\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000W\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000A\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000+\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\\\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000`\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000W\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000&\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000,\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000,\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000-\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000;\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000-\000\000\000\000\000\000\000";
const uint8_t gm_s4mghosb8ogmj[56] = "variant `{}` has a payload; write `.{}(_)` to ignore it";
const uint8_t gm_s4oil3epcv041[3] = "`@";
const uint8_t gm_s4ujp5fsfjtut[8] = "evok \077 ";
const uint8_t gm_s4un3os52adn8[18] = ") uintptr_t gm_w[";
const uint8_t gm_s4v9747a9b85a[11] = "let-tuple ";
const uint8_t gm_s50qkjucptpgl[8] = "gm_clz(";
const uint8_t gm_s51jnn7biksj8[68] = "no other module uses it, and other packages do not import a program";
const uint8_t gm_s52ntohgadejq[12] = "unreachable";
const uint8_t gm_s52od60hi0cpi[21] = "` can only be called";
const uint8_t gm_s541gi95epov5[25] = "a variant name after `.`";
const uint8_t gm_s542rn9shphg8[16] = "` is never used";
const uint8_t gm_s55ncecttah5f[69] = ") - 1)) != 0) gm_trap(\"allocation alignment is not a power of two\", ";
const uint8_t gm_s57rgvjb5mj5a[8] = "MaskAll";
const uint8_t gm_s5a6hd5ouael9[37] = "(unsigned __int128)({})GM_LD({}, {})";
const uint8_t gm_s5detsdj9r6dm[25] = "connection reset by peer";
const uint8_t gm_s5g0cpgo64peg[104] = " exists and was not generated by gamma, so it is not overwritten; choose another output name with `-o`\012";
const uint8_t gm_s5gl145lqr39l[15] = "GIT_COMMON_DIR";
const uint8_t gm_s5iqbpk190ist[14] = "assigning to ";
const uint8_t gm_s5k0vuc678fh1[8] = "EnvExit";
const uint8_t gm_s5ol08dgk75gg[17] = "\000\000\000\000\000\000\000\000\033\005\000\000\000\000\000\000";
const uint8_t gm_s5rf946siff22[29] = "));\012    } else {\012        (((";
const uint8_t gm_s677r76pa1oks[7] = "const ";
const uint8_t gm_s67lg9pjq86ie[55] = "struct patterns name their fields: `S(field: pattern)`";
const uint8_t gm_s68uiiatavvqh[18] = "part of the cycle";
const uint8_t gm_s6aq8jknpl0n5[27] = "/* ---- generated ---- */\012";
const uint8_t gm_s6clc0pmaeksj[18] = "unknown package `";
const uint8_t gm_s6d7j0gejjq91[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000&\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s6jlko9gsl7im[26] = "] __attribute__((aligned(";
const uint8_t gm_s6kdp6nkaj7o9[12] = "--work-tree";
const uint8_t gm_s6m88j5u3flig[6] = "place";
const uint8_t gm_s6mvc5jp4o5mi[30] = "), nb = GM_LD(uintptr_t, b + ";
const uint8_t gm_s6n2mkqhnsjpk[10] = "` names `";
const uint8_t gm_s6nqhdcqlaqpu[44] = "`from_tag` is defined only for enums, not `";
const uint8_t gm_s6qbnfe2f321h[4] = "Pow";
const uint8_t gm_s6sdj2gjsdah5[18] = "GM_INT_TO_PTR({})";
const uint8_t gm_s6tdh0p5an5le[51] = "an `@asm` constraint must be a compile-time string";
const uint8_t gm_s6tn8kl12f5ru[27] = "{} are not ordered (\302\2477.5)";
const uint8_t gm_s6unagr666v56[22] = "compiler/interp/bc.gm";
const uint8_t gm_s72tasmt4pg49[26] = " elements, but 1 is given";
const uint8_t gm_s73f72842a67m[45] = "`{}` applies to {}, not to the lanes of `{}`";
const uint8_t gm_s73jhka2t3avj[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\204\001\000\000!\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s75gqsar0656b[50] = "a float literal cannot have the integer type `{}`";
const uint8_t gm_s76hg0po5usup[9] = "        ";
const uint8_t gm_s76i2uidi0pjj[54] = "internal error: a `never` expression produced a value";
const uint8_t gm_s77ek30pr0obt[70] = "` is not a first-class type, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_s78m6s8sukfpa[13] = "` has both `";
const uint8_t gm_s799mjibj8396[10] = "run_bench";
const uint8_t gm_s7d1d0uk71q3u[19] = "` has no variant `";
const uint8_t gm_s7ifc1fku673p[5] = "gm_g";
const uint8_t gm_s7nnecs6i9eki[41] = ": No such file or directory (os error 2)";
const uint8_t gm_s7o7e6piqr3hn[14] = "system_output";
const uint8_t gm_s7r1917rmmif7[21] = "gamma: cannot write ";
const uint8_t gm_s7ri8monh3c4h[8] = "env_var";
const uint8_t gm_s7st0fkehmujt[69] = "` cannot be inlined at every call site: it can call itself (\302\24711.10)";
const uint8_t gm_s7tgpca9g8npn[11] = "(double){}";
const uint8_t gm_s7unnudd0kkri[14] = "documentation";
const uint8_t gm_s7upcfgaqqnlq[433] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s7uql3407tind[10] = "is_finite";
const uint8_t gm_s81r46mpa8ra9[29] = "const uint8_t *v, uint64_t h";
const uint8_t gm_s846hbv2ad5li[17] = "unreachable code";
const uint8_t gm_s86s8nstj5qij[54] = "`{}` is a reserved type name and cannot be redeclared";
const uint8_t gm_s8abg3vemofr6[4] = "` (";
const uint8_t gm_s8f0u3tjempi4[53] = "`#` (the base prefix) goes with `x`, `X`, `o` or `b`";
const uint8_t gm_s8gtcifve79ai[9] = "align-of";
const uint8_t gm_s8hh4pp2rpnlj[43] = "GM_STACK_CHECK_SHALLOW(&F, GM_TEMP_BYTES);";
const uint8_t gm_s8k87f60ciakv[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\245\001\000\000\034\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s8ooubi1nm7vq[5] = "me{}";
const uint8_t gm_s8ph5bt10ogvd[10] = "sha256:{}";
const uint8_t gm_s8q0uvsg4b37p[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\244\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s8qs8rpj34te9[28] = "expected a numeric constant";
const uint8_t gm_s8t43j22je03d[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\357\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s8tq0rdmthkth[16] = "gamma-register-";
const uint8_t gm_s8u6lv8hq3sn2[23] = "duplicate variant `{}`";
const uint8_t gm_s91vt5u8rghg3[3] = "})";
const uint8_t gm_s920j0georape[5] = "deny";
const uint8_t gm_s948vtbtkq957[32] = "gm_poison(&F, 0xDD, sizeof F); ";
const uint8_t gm_s9646k6ls9lbt[51] = " (`--all` documents the modules below a directory)";
const uint8_t gm_s9659cpb4brmg[36] = "too many arguments: the {} takes {}";
const uint8_t gm_s98a5ud3vqhvr[85] = "); non-ASCII characters are only allowed in comments, strings and character literals";
const uint8_t gm_s99scb3gc7vkm[29] = "this use fixed the type as `";
const uint8_t gm_s9a0n41a8i82u[4] = " }\012";
const uint8_t gm_s9ab1ohnfcjkc[6] = "prog-";
const uint8_t gm_s9agk8q084c5k[8] = "fn-ref ";
const uint8_t gm_s9b8bk12k1acq[53] = "the library name as a string literal after `foreign`";
const uint8_t gm_s9d2uvo0cndd7[4] = "(_)";
const uint8_t gm_s9e7dd3qkr93v[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\024\001\000\000\022\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s9j4gjjc4pmge[41] = " bytes (`@min_stack`), but this one has ";
const uint8_t gm_s9jd8cvhje3jm[26] = "{}; `Signed` is now `Neg`";
const uint8_t gm_s9kclu2dkmivs[24] = "`; compare with a guard";
const uint8_t gm_s9nm0mpn8r8ov[40] = "expected `{}`, found a complex constant";
const uint8_t gm_s9qbs07c2ca5q[5] = "repl";
const uint8_t gm_s9u3cqhrfmcru[13] = "gm_write_out";
const uint8_t gm_s9v7ektal8f2e[18] = "gm_stack_overflow";
const uint8_t gm_sa09bp3uo6b22[25] = "null pointer dereference";
const uint8_t gm_sa26g0ksgdkma[10] = "cont-type";
const uint8_t gm_sa53epbg9iarq[10] = "AtomicCas";
const uint8_t gm_sa5lo120id503[26] = "vectors have no method `.";
const uint8_t gm_sa6mes8ijqmmk[30] = "this compile-time union value";
const uint8_t gm_sa9s55cr1nc6b[1698] = "usage: gamma <command> [options] <file>\012\012commands:\012  lex     print the tokens of source files\012  parse   check the syntax of source files (--dump prints the syntax tree)\012  modules load a program's modules without checking them and print the module graph\012  decls   resolve a program's declarations without checking bodies and print them\012  tir     check a program and print the typed program the checker produces\012  check   type-check a program (a .gm file or a module directory)\012  doc     write the API documentation of a module as Markdown or HTML (gamma doc --help)\012  lint    check a package with its tests and report the findings of the lint rules\012  fix     check a program and apply the fixes its diagnostics suggest\012  build   compile a program to a native executable via the system C compiler\012  run     compile a program via C and run it, or run it in the interpreter (--backend=interp);\012          arguments after the file go to the program\012  test    run the test declarations of a module and of the modules of its package\012          that it imports, compiled into one executable via C\012  bench   run the bench declarations the same way and report the time per run\012          (compiled in fast mode unless --mode is given)\012  new, init, fetch, update, add, remove, cache\012          the package manager (gamma help)\012  fmt     format source files in place: gamma fmt [--check] <file or directory>... (\"-\" filters stdin to stdout)\012  repl    interactive read-eval-print loop on the interpreter: gamma repl [--quiet] [file]\012          (loads the file's declarations)\012  lsp     run the language server on standard input and output\012  help    print this message\012  version print the compiler version\012\012options:\012";
const uint8_t gm_sabu5uehm65n2[22] = "static const gm_type ";
const uint8_t gm_sacmu7hr5dknu[88] = "a field cannot hold a second-class function; use a function pointer `^fn(\342\200\246)` (\302\24711.6)";
const uint8_t gm_sad9qq5tf3nqa[36] = "`_` cannot be declared at top level";
const uint8_t gm_sak07p89ce10t[10] = "gm_strcmp";
const uint8_t gm_sakkgen9m77d6[16] = " uint64_t : 64;";
const uint8_t gm_sandsuergllg3[30] = "a constraint such as `Ord(T)`";
const uint8_t gm_sap32k6of7kh3[66] = "`#{}` goes directly before a `struct` or `union` literal (\302\24720.2)";
const uint8_t gm_sar77rocuptb5[31] = "    gm_flush();\012    return 0;\012";
const uint8_t gm_sartclcojin4f[52] = "internal error: `@hash` seed that is not an integer";
const uint8_t gm_savi5cfvaqp3a[15] = "GM_LD(void *, ";
const uint8_t gm_savs0ep448c40[33] = "({})(({}){} {} (int)(({}) & {}))";
const uint8_t gm_sb7685j14iksa[41] = "the function has no parameter named `{}`";
const uint8_t gm_sb7t8ia5pj1l5[19] = "#callconv(.Win64) ";
const uint8_t gm_sb809qf68db4f[9] = "\000\000\020\000\000\000\000\000";
const uint8_t gm_sb85ua3p3jm63[9] = "\003\000\000\000\000\000\000\000";
const uint8_t gm_sbaogg9p69n9f[38] = "a generic function cannot be exported";
const uint8_t gm_sbcjfjm1sdn3u[42] = "the `git` URL of dependency `{}` is empty";
const uint8_t gm_sbe2lidu87fed[61] = "a compile-time argument must be a constant expression, but `";
const uint8_t gm_sbgqovvv31o11[14] = "gm_fiber_pool";
const uint8_t gm_sbh7smhjots7v[9] = "float {}";
const uint8_t gm_sbol12a8gjr17[9] = "<iter{}>";
const uint8_t gm_sbsmevjmneb3d[55] = "`{}` does not exist on a freestanding target (\302\2473.7.1)";
const uint8_t gm_sbstgmps1gd24[10] = "gm_unwind";
const uint8_t gm_sbtpqmkmtr4s9[38] = "as the failure order of `@atomic_cas`";
const uint8_t gm_sc092629f9on4[15] = "pat-variant {}";
const uint8_t gm_sc0i66qc606nr[13] = "#simd [4]f32";
const uint8_t gm_sc24ftfhihd1h[4] = "Shr";
const uint8_t gm_sc2tdpp2mbdis[2] = "\003";
const uint8_t gm_sc30ktgbs45q0[4] = "ok(";
const uint8_t gm_sc6uqn3rvm1p8[56] = "cannot spread a value of type `{}` into named arguments";
const uint8_t gm_sc829ikr3c206[10] = "powerpc64";
const uint8_t gm_sc8v94psr8v3s[21] = "` is private (\302\2473.3)";
const uint8_t gm_sc92utnrdaeo1[12] = "handlers {}";
const uint8_t gm_scbq2ng311vcu[18] = "positionEncodings";
const uint8_t gm_scela8hmfe29j[9] = "gm_field";
const uint8_t gm_scjbu6gm2hrhf[8] = "{}.data";
const uint8_t gm_sclhssivsh27a[37] = "this compile-time evaluation calls `";
const uint8_t gm_scon5d5hl81e6[17] = "parse_struct_lit";
const uint8_t gm_scp1b2sk2nlmr[8] = "{error}";
const uint8_t gm_scq4j3j3c00j6[41] = ": not the start of a live heap block of ";
const uint8_t gm_scq91tgm9lph4[6] = "Linux";
const uint8_t gm_scrj4ri245rj1[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000+\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_scrjehcehrfpv[15] = "    (void)cx;\012";
const uint8_t gm_scsl23nf1o7fu[13] = "release_page";
const uint8_t gm_scvmbht29gva9[7] = "not {}";
const uint8_t gm_sd28nb0n7lqmc[13] = ", (uint64_t)";
const uint8_t gm_sd3b7a2bnmpef[25] = "reached `@unreachable()`";
const uint8_t gm_sd7p31mtitiq9[3] = "*=";
const uint8_t gm_sda02md5lagkh[11] = "atomic_max";
const uint8_t gm_sdrrh4gk917l3[10] = "{} for {}";
const uint8_t gm_sds0tnbefk9cl[20] = ", __ATOMIC_RELAXED)";
const uint8_t gm_sdvkaj73n9goh[24] = "address space exhausted";
const uint8_t gm_se055duut4b83[32] = "cannot spread a value of type `";
const uint8_t gm_se2a9lqlhae47[22] = "gamma: cannot write `";
const uint8_t gm_se6hcvav1sk3o[5] = "@\000\000\000";
const uint8_t gm_se7ecnl2agot9[12] = "lower_ascii";
const uint8_t gm_se84lb2ddmuai[21] = "`fn`, `esc` or `ctl`";
const uint8_t gm_se8us8vie9a8a[37] = "` overflows or divides by zero with ";
const uint8_t gm_sedqspk7o41v4[3] = "do";
const uint8_t gm_sefk7cselekki[3] = " v";
const uint8_t gm_sekmurq2ua2sf[6] = "when ";
const uint8_t gm_sel5oh1k2839e[12] = "variants = ";
const uint8_t gm_semavgcmso3pp[33] = "the name of the foreign variable";
const uint8_t gm_sencmbgnvrve5[18] = "unknown type `{}`";
const uint8_t gm_seplhq3i7l38b[195] = "`{}` is generic, so it needs its compile-time arguments to be a function value: write `{}(\342\200\246)` with only the types and `::` values, or give a function pointer type that determines them (\302\24716.7)";
const uint8_t gm_sescofr8m4g3o[5] = "kc{}";
const uint8_t gm_seue7mq07v0oc[6] = "Range";
const uint8_t gm_sf0f3lvsd76jf[17] = ") gm_trap_slice(";
const uint8_t gm_sf1f5se9222sg[35] = "` cannot be called at compile time";
const uint8_t gm_sf3ii7gekd9ll[13] = "handler-decl";
const uint8_t gm_sf4ahu9mfer7v[5] = "and ";
const uint8_t gm_sf6buv3gbt82t[29] = "(unsigned __int128)({} != 0)";
const uint8_t gm_sfa00o5gkcj29[5] = "{}_c";
const uint8_t gm_sfc624hllrs4g[6] = "gm_ls";
const uint8_t gm_sfhbo1m6f9ql4[35] = "`.make(n)` makes a `#soa` slice; `";
const uint8_t gm_sfjc41f66fqb2[10] = "value of ";
const uint8_t gm_sfjuf1jo6fusr[8] = "{}.rt.c";
const uint8_t gm_sfkrpkv21b1rq[4] = "%, ";
const uint8_t gm_sflee3fpg0btv[5] = "rune";
const uint8_t gm_sfll40jgr4opl[4] = "`](";
const uint8_t gm_sfmm1kh41nanh[15] = "wildcard req (";
const uint8_t gm_sfmov9pb6t5lt[14] = "numeric lanes";
const uint8_t gm_sfocmetde6c9f[26] = "gm_mix64(h, ({})[0] != 0)";
const uint8_t gm_sfu1s4a4bo4he[25] = "invalid leading zero in ";
const uint8_t gm_sg5khilhaakst[16] = "gdb_printers.py";
const uint8_t gm_sg6dqv3rmh13r[59] = "typedef struct {{ _Alignas({}) uintptr_t gm_w[{}]; }} {};\012";
const uint8_t gm_sg71mqv9mtu78[33] = "), GM_LD(uintptr_t, (uint8_t *)&";
const uint8_t gm_sg9fnspse4evr[22] = "no WebAssembly header";
const uint8_t gm_sgankvlme129s[3] = "  ";
const uint8_t gm_sgcj0gjfdljat[11] = ",\"notes\":[";
const uint8_t gm_sggmh06s4b0cs[23] = "an integer, for a base";
const uint8_t gm_sgksioc7kn741[22] = "compiler/tir/clone.gm";
const uint8_t gm_sgp5gt1voa0tq[12] = "finish_call";
const uint8_t gm_sgr8khb1g0pr9[17] = "free_decl_values";
const uint8_t gm_sgskdjmsdpnlb[19] = "gm_fiber_slab_used";
const uint8_t gm_sgu2tengos987[41] = "internal error: a field of a #bits field";
const uint8_t gm_sgvln0vb20p94[17] = "    __repl_v := ";
const uint8_t gm_sh0h8t5us2qkm[15] = "invalid escape";
const uint8_t gm_sh4niqnmh73co[6] = "deref";
const uint8_t gm_sh4ufq9bak51r[13] = ") uint8_t b[";
const uint8_t gm_sh54qlvpeqnin[10] = "#soa {}{}";
const uint8_t gm_sh9sks72142ms[17] = "` is not an enum";
const uint8_t gm_shb3g0iah7668[18] = "gm_free_cstr_list";
const uint8_t gm_shb5mqv6etf1t[5] = "ceil";
const uint8_t gm_shge5n743ua4h[3] = ":=";
const uint8_t gm_shm5oj4temhh6[33] = "    if (ta != tb) return false;\012";
const uint8_t gm_shq8kvmdjhmfg[4] = "j32";
const uint8_t gm_shqn2dgvujf5o[13] = "node_modules";
const uint8_t gm_shr0v68m3hbkr[16] = "gm_heap_release";
const uint8_t gm_shuj8drmk4t93[9] = "run_test";
const uint8_t gm_shv2qb39iqelf[31] = "`; handle the error explicitly";
const uint8_t gm_shvoch74oh0kq[59] = "only a `#soa` slice from `.make(n)` is freed, not an array";
const uint8_t gm_shvp36midgf58[10] = "FsOpenDir";
const uint8_t gm_si2i26md2o6k8[26] = "calling a value of type `";
const uint8_t gm_si731m8b0sr6s[4] = "{};";
const uint8_t gm_si8shacml9b6h[14] = "gm_net_accept";
const uint8_t gm_sicfped8s368m[58] = "this compile-time string points outside its data (\302\24717.4)";
const uint8_t gm_sicq1bvp13t5d[23] = "unexpected character `";
const uint8_t gm_sideqrlbldrh7[26] = "constant `@asm` templates";
const uint8_t gm_sie1pq1m4mo8b[29] = "the operands of a comparison";
const uint8_t gm_sijvlope91krv[31] = "this assigns a place to itself";
const uint8_t gm_sikg061j3im7q[2] = "j";
const uint8_t gm_siklni1kauva6[6] = "(&{})";
const uint8_t gm_sing8pfld8brt[3] = "gm";
const uint8_t gm_sinoph5a0op58[10] = "fast_math";
const uint8_t gm_sisjr6ep7j5j0[79] = "only the first field of a struct may be `using`, and it must be a single field";
const uint8_t gm_sj2pdhqdnnh86[3] = "* ";
const uint8_t gm_sj4o6222mnbmq[16] = "a tuple element";
const uint8_t gm_sj815koajgkmb[6] = "gm_dt";
const uint8_t gm_sj8ni1it038ec[18] = "Foreign functions";
const uint8_t gm_sj8ptihcm0srb[51] = "`{}` is not an operator; compare with `{}` (\302\2477.6)";
const uint8_t gm_sjb60mo5hejk2[6] = "round";
const uint8_t gm_sjds8o3l7mmnl[5] = "\"\"\"\012";
const uint8_t gm_sjguhtmsnuhlj[58] = "a constant cannot hold `{}`, which contains `{}` (\302\24717.4)";
const uint8_t gm_sjig620oug367[9] = "Handlers";
const uint8_t gm_sjk9nricijji8[45] = "`: install it from a variable with `with &x`";
const uint8_t gm_sjkg27kmogh66[7] = "<std>/";
const uint8_t gm_sjn4krvshoajd[26] = "integer overflow in `abs`";
const uint8_t gm_sjnl36ocas0o1[11] = "constraint";
const uint8_t gm_sjrimf03ml0ct[13] = "an intrinsic";
const uint8_t gm_sk2dpusgkm375[22] = "the module directory ";
const uint8_t gm_sk4bpl5a3cqoi[23] = "expected a value for `";
const uint8_t gm_sk4tdtvin0g62[31] = "they take {} and {} parameters";
const uint8_t gm_sk71kk4665fg9[34] = "expected comma after {}, found {}";
const uint8_t gm_sk9qhfibomcn3[48] = "`@hash(x, seed)` takes a value and a `u64` seed";
const uint8_t gm_skdalh0o0hret[59] = "a lane index of `.shuffle` must be a compile-time constant";
const uint8_t gm_skfks8nrdfdgv[5] = "\012LH:";
const uint8_t gm_skg16mqffvnq6[4] = "\342\200\246";
const uint8_t gm_skijrg68314qg[9] = "packages";
const uint8_t gm_skl3b78lo1gln[5] = "tail";
const uint8_t gm_skl3cimaihkgu[26] = "`; run it with `for x in ";
const uint8_t gm_skmr2rgcse4gr[14] = "volatile_load";
const uint8_t gm_skqa2fjpbsodp[9] = "net_peer";
const uint8_t gm_skqb5p67mhjp2[43] = "the module directory {} has no `.gm` files";
const uint8_t gm_skucss3rkslf9[57] = "`#{}` takes no arguments and is written once: `#{} [N]T`";
const uint8_t gm_skue5huof65fj[80] = "`for` iterates over a range, an array, slice or tuple, or a call to an iterator";
const uint8_t gm_sl6m7ka86q6ca[12] = "; B.line = ";
const uint8_t gm_sl7562rl6kuvh[7] = "rev={}";
const uint8_t gm_sl935l3jslu0o[28] = "invalid unicode escape `\\u{";
const uint8_t gm_sl9i9hcgkt8fd[97] = "`.bits` needs equal sizes, but `{}` has {} bytes and `{}` has {} (in an instantiation used here)";
const uint8_t gm_sl9mgk5ho4kmr[29] = "unsupported conversion to {}";
const uint8_t gm_sl9nvct67or45[3] = "if";
const uint8_t gm_sl9pujidpnv0r[4] = "o{}";
const uint8_t gm_sla1l2pt1mfvt[3] = "os";
const uint8_t gm_slbfl8l7n523s[7] = " = 1; ";
const uint8_t gm_slbn3chj3p1e1[49] = "`{}` shifts integer lanes, not the lanes of `{}`";
const uint8_t gm_slfpvqm0f02j6[8] = "ldflags";
const uint8_t gm_slg1gdqpr4sdg[49] = "a constant's initializer may not perform effects";
const uint8_t gm_slhn01etuat95[9] = "\017\374\377\377\377\377\377\377";
const uint8_t gm_slj50kiksg47f[3] = "ma";
const uint8_t gm_sljdair45oqft[31] = " are not inside any allocation";
const uint8_t gm_slku641rnhls8[7] = "gm_src";
const uint8_t gm_slni8vfv3lequ[22] = "too many values for `";
const uint8_t gm_slnkfiltk5b43[57] = "internal error: not supported by the interpreter yet: {}";
const uint8_t gm_slq0l63qf1dvs[11] = "(uint64_t)";
const uint8_t gm_slu3lsfv09uua[25] = "integer and `bool` lanes";
const uint8_t gm_sm139pecc5n3p[6] = "clamp";
const uint8_t gm_sm1iup9eb4u28[13] = "this `break`";
const uint8_t gm_sm30gjvs8dlav[7] = "{}.exe";
const uint8_t gm_sm399snoi2v85[3] = "-D";
const uint8_t gm_sm9otrmuq5hp3[34] = "this `return` leaves the function";
const uint8_t gm_smatmsv0oedot[8] = "fs_stat";
const uint8_t gm_smbmfla2idsdo[68] = "gamma cannot evaluate this `when` condition yet: it is not a `bool`";
const uint8_t gm_smjg62rnkmnon[21] = "({} {} {} \077 {} : {})";
const uint8_t gm_smlifs2umbvsq[8] = ", nb = ";
const uint8_t gm_smmus0krp2svr[6] = "Sleep";
const uint8_t gm_smovt8m5cpa13[7] = "GM_ST(";
const uint8_t gm_smtmd013g2edr[4] = "Shl";
const uint8_t gm_smu1qqop2463k[53] = "gm_fmin(gm_fmax((double){}, (double){}), (double){})";
const uint8_t gm_smuh6lae5oil8[68] = "Alloc + Out + Err + In + Clock + Rand + Fs + Net + Proc + Env + Sys";
const uint8_t gm_smv1ru2lmia7k[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000u\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_smv20k805bk9c[15] = "{} {} {}: {}{}";
const uint8_t gm_sn47urokcort4[18] = "permission denied";
const uint8_t gm_sn62ecr79ngvo[7] = "` on `";
const uint8_t gm_sn68oj251cu21[38] = "this use cannot be renamed on its own";
const uint8_t gm_snd69jrnj9kdn[42] = "`: it is the name of the standard library";
const uint8_t gm_snemlnnsbdb13[46] = "cannot mix `and` and `or` without parentheses";
const uint8_t gm_sni9iu6vnvsjk[44] = "`: widths are 1 to 128 with no leading zero";
const uint8_t gm_snk3kkdsb32a9[129] = "revision {} of `{}` ({}) is no longer available in the repository (a tag moved or history was rewritten\077); run `gamma update {}`";
const uint8_t gm_snl4vfgn6qv9m[10] = "memmove(&";
const uint8_t gm_snlqf7m58jjdu[5] = "cast";
const uint8_t gm_snm77g00so1si[12] = ")`, `x.sat(";
const uint8_t gm_snvn4jaklkik3[65] = ") > (unsigned __int128)UINTPTR_MAX) \077 UINTPTR_MAX : (uintptr_t)(";
const uint8_t gm_so873adl39g7s[19] = "gm_trap_misaligned";
const uint8_t gm_so8cik47e3h2s[19] = ";\012static int gm_xt";
const uint8_t gm_soaev64pou0k6[54] = "`{}` is not allowed in an untyped constant expression";
const uint8_t gm_sob5t135sl7q9[34] = "the package name must be a string";
const uint8_t gm_soc64bhm712u9[65] = "a deferred statement may not `break` or `continue` out of itself";
const uint8_t gm_sod1lb2gquo9d[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\341\002\000\000\016\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sofbtko7t8taa[5] = "then";
const uint8_t gm_soh97sflbds87[11] = "{} (*)({})";
const uint8_t gm_soimgd0jo62fr[28] = "compiler/check/iterators.gm";
const uint8_t gm_sokpg5qd8r9va[20] = "static void *const ";
const uint8_t gm_sonhdjr4nc418[10] = "; memcpy(";
const uint8_t gm_soq0ecbboa994[21] = " holds an empty path";
const uint8_t gm_sosa5l0k6d34c[15] = ", (uint8_t *)&";
const uint8_t gm_sou4p9915dc16[15] = "gm_thread_take";
const uint8_t gm_sp02l2e4viomq[26] = "unexpected character `{}`";
const uint8_t gm_sp1784q48t8s2[4] = " };";
const uint8_t gm_sp4518j1p6r9m[19] = " *gm_Fp = (struct ";
const uint8_t gm_sp6bf9nji9g1j[9] = "severity";
const uint8_t gm_sp7619gun38fi[63] = "`{}` is visible only in the package of the module `{}` (\302\2473.3)";
const uint8_t gm_spb7dvkqr71v2[77] = "no gamma.toml found in {} or any parent directory (`gamma init` creates one)";
const uint8_t gm_spfg8ncmd7s76[14] = "gm_cover_dump";
const uint8_t gm_spghmfn4rjv14[22] = "timed out after {} s\012";
const uint8_t gm_spuk9sqnoo3ai[13] = "'\\u{{{:x}}}'";
const uint8_t gm_sq0p2ifj52tjf[8] = "context";
const uint8_t gm_sq0vrp87uto2u[12] = "global_addr";
const uint8_t gm_sq2ta0ln2qvqo[47] = "`.{}` needs an array variable, not a temporary";
const uint8_t gm_sq8b2uior35pk[12] = "File exists";
const uint8_t gm_sq98g0itpb0cn[19] = ") has the SHA-256 ";
const uint8_t gm_sq9lne9r5j3u2[8] = " name `";
const uint8_t gm_sqg9do4ma3giu[6] = "S390x";
const uint8_t gm_sqi7tb51umq69[13] = "gm_write_err";
const uint8_t gm_sqof8dgnasnqk[69] = "array elements are written positionally; this spread has named parts";
const uint8_t gm_sqr1rjjq3dpuh[17] = "` has no field `";
const uint8_t gm_sqt833mkm755s[12] = "clone_block";
const uint8_t gm_sr12ukrgmispo[10] = "</strong>";
const uint8_t gm_sr1l176jjo6jp[5] = ";\012}\012";
const uint8_t gm_sr23o038u76nd[13] = "` combines `";
const uint8_t gm_sr4vl7ov3g7t6[55] = "a declaration starts here, so this `{}` was not closed";
const uint8_t gm_sr6i6ans386ga[45] = "`try` is not allowed in a deferred statement";
const uint8_t gm_sr7sfbbsona7q[10] = "ls-remote";
const uint8_t gm_srar03v1ldkqi[49] = "the root module of a program must declare `main`";
const uint8_t gm_srbj99hcdek7f[18] = "gm_net_local_port";
const uint8_t gm_srf1g3vv4d1md[12] = "gm_ctx_init";
const uint8_t gm_srg30626j66d1[140] = "`{}` cannot be passed to a variadic parameter: use a C-compatible scalar of at least C `int` width or `f64`, converting explicitly (\302\24719.2)";
const uint8_t gm_srgao1ir81l50[14] = "     Skipped ";
const uint8_t gm_srjqfuok5f7nb[40] = "an `if` whose branches are all the same";
const uint8_t gm_srkbv7g2spehb[33] = ": write `z`, not `-lz` or a path";
const uint8_t gm_srli7179p3npp[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\177\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_srnn7fn5ngpbk[17] = "hoist_iter_value";
const uint8_t gm_srojjig2k2m5d[13] = "slicing `{}`";
const uint8_t gm_srua7hn34dkbr[8] = ".{}({})";
const uint8_t gm_srv7efdnr211j[12] = "globPattern";
const uint8_t gm_srvc4tdn4aqfv[7] = "--tags";
const uint8_t gm_srvl2rg1hpk9a[16] = "this performs `";
const uint8_t gm_ss27lm4pmhcij[45] = "(({})(((unsigned __int128){}u << 64) | {}u))";
const uint8_t gm_ss5kcsomld87u[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\203\000\000\000`\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_ss6g2koqn63bf[5] = "Test";
const uint8_t gm_ss6hmjpokkg23[13] = "keyword `{}`";
const uint8_t gm_ss9d51lhe1orq[46] = "`--all` and `--older-than` cannot be combined";
const uint8_t gm_ssbb728kngg91[79] = "`{}` has a field `{}`, so a method of that name could never be called (\302\2477.10)";
const uint8_t gm_sscekt5vbls0f[18] = ") gm_trap_lanes(\"";
const uint8_t gm_ssdho4pv9rajn[55] = "variant `{}` has a single payload; use one sub-pattern";
const uint8_t gm_sse4og0vn1mc5[33] = "({})((__int128)({} << {}) >> {})";
const uint8_t gm_ssevkle1ss77c[13] = "`: a use at ";
const uint8_t gm_ssfhrckkphjsd[6] = "short";
const uint8_t gm_sskmdsnq9bigv[10] = "emit_char";
const uint8_t gm_ssld67umkdenq[20] = "(uint8_t)({} == {})";
const uint8_t gm_sso4tmqr8gin2[20] = " is not aligned to ";
const uint8_t gm_ssqbjasrp4m5m[22] = "with completed while ";
const uint8_t gm_ssqd2isjrp8nh[11] = "ExitStatus";
const uint8_t gm_ssqmni9f9s44u[34] = "`{}(\342\200\246)` takes exactly one value";
const uint8_t gm_sssjusbjeb8fr[7] = "tag={}";
const uint8_t gm_ssud7c614kqjt[5] = "gm_v";
const uint8_t gm_ssulg3jrm3q2v[16] = " is not loaded\012";
const uint8_t gm_st52k2850n5r5[5] = " >= ";
const uint8_t gm_st6dd0k5nckh3[58] = "`#allow` takes the names of lint rules as string literals";
const uint8_t gm_st85s3r88gecq[14] = "gamma: {}: {}";
const uint8_t gm_st8i5uhn201op[37] = "\"\012version = \"0.1.0\"\012\012[dependencies]\012";
const uint8_t gm_stb42390ups9k[6] = "type ";
const uint8_t gm_steftqaruo8k5[8] = "reverse";
const uint8_t gm_stgqtu1f7bco8[8] = "<entry>";
const uint8_t gm_stkc6efbvodm1[14] = "\012version = 1\012";
const uint8_t gm_stlra8b089nel[53] = "the template of `@asm` must be a compile-time string";
const uint8_t gm_stnv0suiruu5q[11] = "did_change";
const uint8_t gm_stooh1vej0ram[16] = "unused-variable";
const uint8_t gm_stot0rovnaivg[7] = "--dump";
const uint8_t gm_str26682vod5l[38] = "a function has one calling convention";
const uint8_t gm_strca8t49bpqu[2] = "m";
const uint8_t gm_sts5j6f5pkov0[5] = "void";
const uint8_t gm_stv59gn45ivj9[37] = "lone leading surrogate in hex escape";
const uint8_t gm_stv6f87bn9v0e[7] = "native";
const uint8_t gm_su0h0fbu8l4ri[30] = "`{}` needs a mutable receiver";
const uint8_t gm_su28d8kir9fan[10] = "pat-bind ";
const uint8_t gm_su43dt5mihp55[22] = "((struct {} *){})->{}";
const uint8_t gm_su58q188fvc2a[12] = "atomic_swap";
const uint8_t gm_su5dv07glhc06[37] = "an import binding cannot have a type";
const uint8_t gm_su5tga7voctrl[7] = "gamma ";
const uint8_t gm_subnd19q8jmpf[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000C\001\000\000^\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_sueo2nge7h013[14] = "ULL << 64) | ";
const uint8_t gm_suiaoknmgprea[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\264\001\000\000\"\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sujli4dml3ipq[8] = "message";
const uint8_t gm_sunk599c58krq[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000!\002\000\000\020\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_suu5j5c20v4ok[15] = "flag_offset = ";
const uint8_t gm_suvioac7tr94t[10] = "__PPC64__";
const uint8_t gm_sv0285a7k6k1l[13] = "with_operand";
const uint8_t gm_sv4jvo5eoeo8t[7] = "--bare";
const uint8_t gm_svdsal9bavsj8[21] = "`}}` to close the {}";
const uint8_t gm_svgiumrfk8vkf[73] = "the template has {}, but a `{}` is given (in an instantiation used here)";
const uint8_t gm_svivu54u4uhtg[8] = "jsonrpc";
const uint8_t gm_svj2mudio0eed[18] = "</code></dt>\012<dd>";
const uint8_t gm_svj967tkdfgka[15] = "goto gm_fdone;";
const uint8_t gm_svjl1hr0jvvbv[9] = "GM_CONV(";
const uint8_t gm_svknrff8m4a97[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000W\001\000\000\014\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_svml0iv6h5a82[48] = "gm_mix64(h, (uint64_t)(uintptr_t)GM_LD(void *, ";
const uint8_t gm_svq539fg740gg[7] = ", &evn";
const uint8_t gm_svrid2hlmcfue[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\024\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_svsbh0404ivr6[6] = "add `";
const uint8_t gm_svtktff6b3snn[3] = "so";
const uint8_t gm_svurqcoqldl32[83] = "a rest parameter must be the last parameter (other than `@caller_location()` ones)";
uint8_t gm_c05hsm9hbk3[13] __attribute__((aligned(1)));
uint8_t gm_c1e13bm8153[15] __attribute__((aligned(1)));
uint8_t gm_c1m4oddes6p[6] __attribute__((aligned(1)));
uint8_t gm_c24smpco9rb[39] __attribute__((aligned(1)));
uint8_t gm_c2pkvkqaboa[13] __attribute__((aligned(1)));
uint8_t gm_c30tdq2p48i[1] __attribute__((aligned(1)));
uint8_t gm_c3d3485bcf8[11] __attribute__((aligned(1)));
uint8_t gm_c43le3enhbn[4] __attribute__((aligned(1)));
uint8_t gm_c4e612inl87[7] __attribute__((aligned(1)));
uint8_t gm_c5ad7a620rp[7] __attribute__((aligned(1)));
uint8_t gm_c5c748e2jnu[18] __attribute__((aligned(1)));
uint8_t gm_c6h27aetc34[8] __attribute__((aligned(1)));
uint8_t gm_c6m8tnj6ja2[3] __attribute__((aligned(1)));
uint8_t gm_c6r62i0838h[15] __attribute__((aligned(1)));
uint8_t gm_c7eeuhrqkm5[6] __attribute__((aligned(1)));
uint8_t gm_c7jg7fig0r2[2] __attribute__((aligned(1)));
uint8_t gm_c7mc3uaor1r_2[80] __attribute__((aligned(8)));
uint8_t gm_c866866ffqn[5] __attribute__((aligned(1)));
uint8_t gm_c8g6p8jp14b[96] __attribute__((aligned(8)));
uint8_t gm_c8je55vlhur[2] __attribute__((aligned(1)));
uint8_t gm_c9j710of7ks[5] __attribute__((aligned(1)));
uint8_t gm_ca31fp0eevd[14] __attribute__((aligned(1)));
uint8_t gm_cal7njs7es4[16] __attribute__((aligned(8)));
uint8_t gm_cb60tlkkq8o[152] __attribute__((aligned(8)));
uint8_t gm_cbvqd04tf7m[14] __attribute__((aligned(1)));
uint8_t gm_ccln5rkhiat[4] __attribute__((aligned(1)));
uint8_t gm_cd0k2qvgiv6[14] __attribute__((aligned(1)));
uint8_t gm_cdb4ji8adev[14] __attribute__((aligned(1)));
uint8_t gm_cdmg1tph9ts[4] __attribute__((aligned(1)));
uint8_t gm_cdvqet46qb2[1697] __attribute__((aligned(1)));
uint8_t gm_ce3m8cj68o5[96] __attribute__((aligned(1)));
uint8_t gm_ceht42n5ud9[11] __attribute__((aligned(1)));
uint8_t gm_cf36eeqbs3g[49] __attribute__((aligned(1)));
uint8_t gm_cf7tq30884g[3] __attribute__((aligned(1)));
uint8_t gm_cf807b860br[24] __attribute__((aligned(8)));
uint8_t gm_cfl6qoievuv[20] __attribute__((aligned(1)));
uint8_t gm_cfrmv39c8o8[5] __attribute__((aligned(1)));
uint8_t gm_cg59ak5330k[6] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e_5[16] __attribute__((aligned(8)));
uint8_t gm_chb4ud0t7b8[14] __attribute__((aligned(1)));
uint8_t gm_chsnc6dou61[8] __attribute__((aligned(1)));
uint8_t gm_ci39cltltdi[8] __attribute__((aligned(8)));
uint8_t gm_cj38t56vaf8[9] __attribute__((aligned(1)));
uint8_t gm_cj5upfc0qar[19] __attribute__((aligned(1)));
uint8_t gm_cj9acci4r11[13] __attribute__((aligned(1)));
uint8_t gm_cjfp4khah06[80] __attribute__((aligned(8)));
uint8_t gm_cjlj93gp0or[24] __attribute__((aligned(8)));
uint8_t gm_cjnbk2fbr2e[8] __attribute__((aligned(1)));
uint8_t gm_cmb1h2b437i[32] __attribute__((aligned(1)));
uint8_t gm_cndnhrbr4l9[13] __attribute__((aligned(1)));
uint8_t gm_cnfi0pu838c[18] __attribute__((aligned(1)));
uint8_t gm_cnlq7csqqrf[1] __attribute__((aligned(8)));
uint8_t gm_cpgkkof10tf[8] __attribute__((aligned(1)));
uint8_t gm_cposd8mg3i2[9] __attribute__((aligned(1)));
uint8_t gm_cpvhkqdiis0[7] __attribute__((aligned(1)));
uint8_t gm_cq1a48dg0dm[11] __attribute__((aligned(1)));
uint8_t gm_cqs26buo28m[9] __attribute__((aligned(1)));
uint8_t gm_cs26fgf43so[16] __attribute__((aligned(1)));
uint8_t gm_csn873hhpsk[8] __attribute__((aligned(1)));
uint8_t gm_csugnm93cnb[6] __attribute__((aligned(1)));
uint8_t gm_ct5id4947nt[144] __attribute__((aligned(8)));
uint8_t gm_ctbqbqsgd4s[8] __attribute__((aligned(1)));
uint8_t gm_cu60un6gh68[12] __attribute__((aligned(1)));
uint8_t gm_cuhj0mdsgck[41] __attribute__((aligned(1)));
uint8_t gm_cul3e2cnjhd[13] __attribute__((aligned(1)));
uint8_t gm_cuqbj62gqhf_3[16] __attribute__((aligned(8)));
uint8_t gm_k08q1k9tq78[40] __attribute__((aligned(8)));
uint8_t gm_k2afmplo64l[40] __attribute__((aligned(8)));
uint8_t gm_k30q9glr4ue[40] __attribute__((aligned(8)));
uint8_t gm_k3n1bpabq7f[40] __attribute__((aligned(8)));
uint8_t gm_k66sf6gvqqo[40] __attribute__((aligned(8)));
uint8_t gm_k6khjohkcto[40] __attribute__((aligned(8)));
uint8_t gm_k7bjnihrifk[40] __attribute__((aligned(8)));
uint8_t gm_k9foool24ve[40] __attribute__((aligned(8)));
uint8_t gm_kav3dhibiev[40] __attribute__((aligned(8)));
uint8_t gm_kbfuu8gel4m[40] __attribute__((aligned(8)));
uint8_t gm_kcnkkrl6ngl[40] __attribute__((aligned(8)));
uint8_t gm_kh1ssdqq46n[40] __attribute__((aligned(8)));
uint8_t gm_khb6ap185f0[40] __attribute__((aligned(8)));
uint8_t gm_kjqc35oao72[40] __attribute__((aligned(8)));
uint8_t gm_kkm55bntrhj[40] __attribute__((aligned(8)));
uint8_t gm_kl8b626vj63[40] __attribute__((aligned(8)));
uint8_t gm_klkc6gi5fh5[40] __attribute__((aligned(8)));
uint8_t gm_kp923qoaivn[40] __attribute__((aligned(8)));
uint8_t gm_kqfbmlr3820[40] __attribute__((aligned(8)));
uint8_t gm_kqj05ihe9qd[40] __attribute__((aligned(8)));
uint8_t gm_kqn03v7t2qt[40] __attribute__((aligned(8)));
uint8_t gm_krt0ppukvqv[40] __attribute__((aligned(8)));
uint8_t gm_ktdkih9htpv[40] __attribute__((aligned(8)));
uint8_t gm_ktu15664o6a[40] __attribute__((aligned(8)));
uint8_t gm_kukq3ep6ok2[40] __attribute__((aligned(8)));
void *const gm_hd3t1u951rul[] = {(void *)gm_dt3t1u951rul_0};
void *const gm_hl8ph80s8bgt[] = {(void *)gm_f_for_body_g303nmp};
void *const gm_hlhtabotcjhg[] = {(void *)gm_f_for_body_gp0er0a};
void *const gm_hlnaa5ptob1s[] = {(void *)gm_f_for_body_d80h595};
void *const gm_hlp1oga9ehto[] = {(void *)gm_f_for_body_sf9naq7};
void *const gm_hlq1reae6pf9[] = {(void *)gm_f_for_body_tv3u4f0};
void *const gm_hlth8t37utgd[] = {(void *)gm_f_for_body_n9765ef};
void *const gm_hlvedue473cr[] = {(void *)gm_f_for_body_1vrv15m};
