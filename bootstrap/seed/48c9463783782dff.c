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
typedef struct gm_csqbqksqco2bm6 { uint32_t f0; void * f1; } gm_csqbqksqco2bm6;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csqbqksqco2bm6) == 16 && _Alignof(gm_csqbqksqco2bm6) == 8 && offsetof(gm_csqbqksqco2bm6, f0) == 0 && offsetof(gm_csqbqksqco2bm6, f1) == 8, "layout of `FieldPat`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
struct gm_fr_clone_block_5tu7v2v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* stmts */
    gm_trqfvmjtt4sij l14; /* out */
    void * ret;
};
struct gm_fr_clone_conds_ljrasoa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_expr_af0u1qc {
    gm_frame h;
    gm_tsth9qu1dv0g3 l25; /* callee */
    gm_tsth9qu1dv0g3 l27; /* c */
    gm_trqfvmjtt4sij l43; /* out */
    gm_t710lsrq1m8gk l52; /* x */
    gm_th1g69d7vpipg l61; /* sink */
    gm_th1g69d7vpipg l64; /* sink */
    gm_t710lsrq1m8gk l66; /* w */
    gm_trqfvmjtt4sij l67; /* hs */
    gm_teti411eujvf4 l74; /* k */
    void * ret;
};
struct gm_fr_clone_exprs_umh77j3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_clone_opt_plt4ceg {
    gm_frame h;
    void * ret;
};
struct gm_fr_clone_pat_cq2uu68 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* out */
    gm_trqfvmjtt4sij l14; /* out */
    gm_tb38v5s9j1r32 l18; /* k */
    void * ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uda0n0p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i33ccm0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ges19gd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_31airv7(void * a0, gm_csqbqksqco2bm6 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_kg8c0kikv62[40] __attribute__((aligned(8)));
extern uint8_t gm_kl8b626vj63[40] __attribute__((aligned(8)));
extern uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgksioc7kn741[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_opt_plt4ceg F;
    void * gm_l0; /* e */
    void * gm_l1; /* x */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        void * t7 = gm_l1;
        void * t8 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uda0n0p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uda0n0p F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_i33ccm0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i33ccm0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_uda0n0p();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rtces00();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 311, 83);
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
__attribute__((aligned(4))) void * gm_f_clone_block_5tu7v2v(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_block_5tu7v2v F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* s */
    uint32_t gm_l5; /* l */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* x */
    gm_b16_8 gm_l8; /* ls */
    void * gm_l9; /* x */
    void * gm_l10; /* p */
    void * gm_l11; /* v */
    gm_b16_8 gm_l12; /* ps */
    void * gm_l13; /* v */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* p */
    void * gm_l18; /* x */
    uint8_t gm_l19; /* is_err */
    void * gm_l20; /* x */
    GM_STACK_CHECK(&F, 1266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ir0h3tc_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_trcmmd12cefb1 t16 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        switch (t27) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma25; default: goto ma26; }
ma19:;
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t29 != ((uint8_t)0ULL)) goto mn28;
                        uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l5 = t30;
                        if ((GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn28;
                        void * t31 = (void *)((uint8_t *)&F.l1);
                        uint32_t t32 = gm_l5;
                        gm_ttggq7ng4kp3p t33;
                        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t32);
                        GM_ST(void *, ((uint8_t *)&t33 + 8), ((void *)0));
                        gm_trcmmd12cefb1 t34;
                        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t34 + 8), t33);
                        gm_tftb1odc739t5 t35 = gm_f_push_d6c76ud_2(t31, &t34);
                        gm_tftb1odc739t5 t36 = t35;
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcgksioc7kn741, 90, 30);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        goto me18;
mn28:;
ma20:;
                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t39 != ((uint8_t)0ULL)) goto mn38;
                        uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t40;
                        if (!(GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8)) != 0)) goto mn38;
                        void * t41 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t41;
                        void * t42 = (void *)((uint8_t *)&F.l1);
                        uint32_t t43 = gm_l6;
                        void * t44 = gm_l7;
                        void * t45 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t44);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_ttggq7ng4kp3p t46;
                        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t43);
                        GM_ST(void *, ((uint8_t *)&t46 + 8), t45);
                        gm_trcmmd12cefb1 t47;
                        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t47 + 8), t46);
                        gm_tftb1odc739t5 t48 = gm_f_push_d6c76ud_2(t42, &t47);
                        gm_tftb1odc739t5 t49 = t48;
                        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcgksioc7kn741, 91, 27);
                        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
                        goto me18;
mn38:;
ma21:;
                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t52 != ((uint8_t)1ULL)) goto mn51;
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l8 = t53;
                        void * t54 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l9 = t54;
                        void * t55 = (void *)((uint8_t *)&F.l1);
                        gm_tpe1hv3fekn05 t56;
                        gm_b16_8 t57 = gm_l8;
                        GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
                        void * t58 = gm_l9;
                        void * t59 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t58);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(void *, (uint8_t *)&t56 + 16, t59);
                        gm_trcmmd12cefb1 t60;
                        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t60 + 8), t56);
                        gm_tftb1odc739t5 t61 = gm_f_push_d6c76ud_2(t55, &t60);
                        gm_tftb1odc739t5 t62 = t61;
                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcgksioc7kn741, 92, 33);
                        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                        goto me18;
mn51:;
ma22:;
                        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t65 != ((uint8_t)2ULL)) goto mn64;
                        void * t66 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l10 = t66;
                        void * t67 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l11 = t67;
                        void * t68 = (void *)((uint8_t *)&F.l1);
                        void * t69 = gm_l10;
                        void * t70 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t69);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        void * t71 = gm_l11;
                        void * t72 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t71);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_t3mf58gv9kf3n t73;
                        GM_ST(void *, ((uint8_t *)&t73 + 0), t70);
                        GM_ST(void *, ((uint8_t *)&t73 + 8), t72);
                        gm_trcmmd12cefb1 t74;
                        GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t74 + 8), t73);
                        gm_tftb1odc739t5 t75 = gm_f_push_d6c76ud_2(t68, &t74);
                        gm_tftb1odc739t5 t76 = t75;
                        if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcgksioc7kn741, 93, 30);
                        gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                        goto me18;
mn64:;
ma23:;
                        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t79 != ((uint8_t)3ULL)) goto mn78;
                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l12 = t80;
                        void * t81 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                        gm_l13 = t81;
                        {
                            gm_f_list_pmp06kq((void *)&F.l14, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                        {
                            {
                                gm_b16_8 t84 = gm_l12;
                                gm_l15 = t84;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t86 = gm_l16;
                                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t88 = (t86 < t87);
                                    if (!t88) goto lb85;
                                    {
                                        uint64_t t91 = gm_l16;
                                        uint64_t t92 = (uintptr_t)t91;
                                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        void * t94 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t92 * 8));
                                        void * t95 = t94;
                                        void * t98 = GM_LD(void *, ((uint8_t *)&t95));
                                        gm_l17 = t98;
                                        {
                                            void * t100 = (void *)((uint8_t *)&F.l14);
                                            void * t101 = gm_l17;
                                            void * t102 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t101);
                                            if (gm_unw.kind) {
                                                goto gm_ll89;
                                            }
                                            gm_tftb1odc739t5 t103 = gm_f_push_r2k6rjf(t100, t102);
                                            gm_tftb1odc739t5 t104 = t103;
                                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srcgksioc7kn741, 96, 31);
                                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                                        }
                                        goto me96;
mn97:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 96, 17);
me96:;
                                    }
lc85:;
                                    {
                                        uint64_t t107 = gm_l16;
                                        uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                                        uint64_t t110 = r108;
                                        gm_l16 = t110;
                                    }
                                }
gm_ll89:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb85; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc85; }
                                }
                                goto gm_ll9;
lb85:;
                            }
                        }
                        {
                            void * t111 = (void *)((uint8_t *)&F.l1);
                            gm_tpe1hv3fekn05 t112;
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t112 + 0, t113);
                            void * t114 = gm_l13;
                            void * t115 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t114);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            GM_ST(void *, (uint8_t *)&t112 + 16, t115);
                            gm_trcmmd12cefb1 t116;
                            GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)3ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t116 + 8), t112);
                            gm_tftb1odc739t5 t117 = gm_f_push_d6c76ud_2(t111, &t116);
                            gm_tftb1odc739t5 t118 = t117;
                            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcgksioc7kn741, 97, 17);
                            gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                        }
                        goto me18;
mn78:;
ma24:;
                        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t121 != ((uint8_t)4ULL)) goto mn120;
                        void * t122 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l18 = t122;
                        void * t123 = (void *)((uint8_t *)&F.l1);
                        void * t124 = gm_l18;
                        void * t125 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t124);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_trcmmd12cefb1 t126;
                        GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t126 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t126 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)4ULL));
                        GM_ST(void *, ((uint8_t *)&t126 + 8), t125);
                        gm_tftb1odc739t5 t127 = gm_f_push_d6c76ud_2(t123, &t126);
                        gm_tftb1odc739t5 t128 = t127;
                        if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srcgksioc7kn741, 99, 25);
                        gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                        goto me18;
mn120:;
ma25:;
                        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t131 != ((uint8_t)5ULL)) goto mn130;
                        uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l19 = t132;
                        void * t133 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l20 = t133;
                        void * t134 = (void *)((uint8_t *)&F.l1);
                        uint8_t t135 = gm_l19;
                        void * t136 = gm_l20;
                        void * t137 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t136);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tgtub0v94d5u5 t138;
                        GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t138 + 0), t135);
                        GM_ST(void *, ((uint8_t *)&t138 + 8), t137);
                        gm_trcmmd12cefb1 t139;
                        GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t139 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t139 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t139 + 8), t138);
                        gm_tftb1odc739t5 t140 = gm_f_push_d6c76ud_2(t134, &t139);
                        gm_tftb1odc739t5 t141 = t140;
                        if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srcgksioc7kn741, 100, 34);
                        gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
                        goto me18;
mn130:;
ma26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 89, 9);
me18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 88, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t144 = gm_l3;
                    uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                    uint64_t t147 = r145;
                    gm_l3 = t147;
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
        gm_tgabaikr08mjg t148;
        memset((uint8_t *)&t148 + 36, 0, 4);
        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t148 + 0, t149);
        void * t150 = gm_l0;
        void * t151 = GM_LD(void *, (((uint8_t *)t150) + 16));
        void * t152 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t148 + 16, t152);
        void * t153 = gm_l0;
        gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t153) + 24));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t148 + 24, t154);
        gm_tq7s98ihonkju t155 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t148, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmlih2j5ae5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t156 = t155;
        if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_srcgksioc7kn741, 103, 12);
        void * t157 = GM_LD(void *, (((uint8_t *)&t156) + 8));
        uint8_t t158 = 0;
        F.ret = t157;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_conds_ljrasoa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_conds_ljrasoa F;
    gm_b16_8 gm_l0; /* cond */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tlr9dp305htm7 gm_l4; /* c */
    void * gm_l5; /* x */
    void * gm_l6; /* p */
    void * gm_l7; /* v */
    GM_STACK_CHECK(&F, 396u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_v5d1o9b_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                        gm_l5 = t20;
                        void * t21 = (void *)((uint8_t *)&F.l1);
                        void * t22 = gm_l5;
                        void * t23 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tlr9dp305htm7 t24;
                        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
                        GM_ST(void *, ((uint8_t *)&t24 + 8), t23);
                        gm_tftb1odc739t5 t25 = gm_f_push_eb7kbhp_2(t21, &t24);
                        gm_tftb1odc739t5 t26 = t25;
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcgksioc7kn741, 110, 25);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                        goto me17;
mn18:;
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                        if (t29 != ((uint8_t)1ULL)) goto mn28;
                        void * t30 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 0));
                        gm_l6 = t30;
                        void * t31 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                        gm_l7 = t31;
                        void * t32 = (void *)((uint8_t *)&F.l1);
                        void * t33 = gm_l6;
                        void * t34 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t33);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        void * t35 = gm_l7;
                        void * t36 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_t3mf58gv9kf3n t37;
                        GM_ST(void *, ((uint8_t *)&t37 + 0), t34);
                        GM_ST(void *, ((uint8_t *)&t37 + 8), t36);
                        gm_tlr9dp305htm7 t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t38 + 8), t37);
                        gm_tftb1odc739t5 t39 = gm_f_push_eb7kbhp_2(t32, &t38);
                        gm_tftb1odc739t5 t40 = t39;
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcgksioc7kn741, 111, 29);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        goto me17;
mn28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 109, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 108, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t43 = gm_l3;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l3 = t46;
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
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t48 = 0;
        F.ret = t47;
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
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_expr_af0u1qc F;
    void * gm_l0; /* e */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint32_t gm_l3; /* i */
    void * gm_l4; /* b */
    void * gm_l5; /* i */
    uint8_t gm_l6; /* c */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    void * gm_l8; /* a */
    void * gm_l9; /* a */
    void * gm_l10; /* a */
    void * gm_l11; /* a */
    gm_t9qkh0rsooc8d gm_l12; /* op */
    void * gm_l13; /* a */
    gm_ts6oqppjh0ba3 gm_l14; /* op */
    void * gm_l15; /* a */
    void * gm_l16; /* b */
    uint8_t gm_l17; /* and_ */
    void * gm_l18; /* a */
    void * gm_l19; /* b */
    gm_tmn7n0n9raou5 gm_l20; /* c */
    void * gm_l21; /* a */
    gm_tvdg94bk7aa22 gm_l22; /* op */
    gm_b16_8 gm_l23; /* args */
    gm_tandvbt63uk7n gm_l24; /* x */
    gm_b16_8 gm_l26; /* args */
    void * gm_l28; /* x */
    gm_b16_8 gm_l29; /* xs */
    gm_b16_8 gm_l30; /* xs */
    void * gm_l31; /* a */
    gm_ttggq7ng4kp3p gm_l32; /* v */
    void * gm_l33; /* a */
    void * gm_l34; /* a */
    uint8_t gm_l35; /* r */
    gm_t3dcoc7077t1q gm_l36; /* x */
    gm_t083kd7b1kkag gm_l37; /* x */
    void * gm_l38; /* a */
    void * gm_l39; /* b */
    uint8_t gm_l40; /* n */
    void * gm_l41; /* sc */
    gm_b16_8 gm_l42; /* arms */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l46; /* a */
    void * gm_l47; /* a */
    void * gm_l48; /* b */
    uint8_t gm_l49; /* n */
    void * gm_l50; /* b */
    gm_trl2khmuphncd gm_l51; /* x */
    gm_teo9d4mncjfdu gm_l53; /* x */
    uint32_t gm_l54; /* l */
    void * gm_l55; /* v */
    void * gm_l56; /* a */
    void * gm_l57; /* k */
    void * gm_l58; /* v */
    void * gm_l59; /* k */
    void * gm_l60; /* w */
    void * gm_l62; /* x */
    void * gm_l63; /* w */
    void * gm_l65; /* x */
    gm_b16_8 gm_l68; /* <tmp> */
    uint64_t gm_l69; /* <tmp> */
    gm_t4lnf71mruqpt gm_l70; /* h */
    void * gm_l71; /* x */
    gm_b16_8 gm_l72; /* decls */
    void * gm_l73; /* x */
    GM_STACK_CHECK(&F, 7339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_teti411eujvf4 t3;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t44) { case ((uint8_t)0ULL): goto ma42; case ((uint8_t)1ULL): goto ma42; case ((uint8_t)2ULL): goto ma42; case ((uint8_t)3ULL): goto ma42; case ((uint8_t)4ULL): goto ma42; case ((uint8_t)5ULL): goto ma42; case ((uint8_t)6ULL): goto ma42; case ((uint8_t)7ULL): goto ma42; case ((uint8_t)8ULL): goto ma42; case ((uint8_t)9ULL): goto ma42; case ((uint8_t)10ULL): goto ma42; case ((uint8_t)11ULL): goto ma42; case ((uint8_t)12ULL): goto ma42; case ((uint8_t)13ULL): goto ma42; case ((uint8_t)14ULL): goto ma42; case ((uint8_t)15ULL): goto ma42; case ((uint8_t)16ULL): goto ma42; case ((uint8_t)17ULL): goto ma42; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma9; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma42; case ((uint8_t)27ULL): goto ma42; case ((uint8_t)28ULL): goto ma42; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma15; case ((uint8_t)32ULL): goto ma16; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma19; case ((uint8_t)35ULL): goto ma20; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma22; case ((uint8_t)38ULL): goto ma23; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma25; case ((uint8_t)41ULL): goto ma26; case ((uint8_t)42ULL): goto ma27; case ((uint8_t)43ULL): goto ma28; case ((uint8_t)44ULL): goto ma29; case ((uint8_t)45ULL): goto ma30; case ((uint8_t)46ULL): goto ma31; case ((uint8_t)47ULL): goto ma32; case ((uint8_t)48ULL): goto ma33; case ((uint8_t)49ULL): goto ma34; case ((uint8_t)50ULL): goto ma42; case ((uint8_t)51ULL): goto ma35; case ((uint8_t)52ULL): goto ma36; case ((uint8_t)53ULL): goto ma37; case ((uint8_t)54ULL): goto ma38; case ((uint8_t)55ULL): goto ma39; case ((uint8_t)56ULL): goto ma40; case ((uint8_t)57ULL): goto ma18; case ((uint8_t)58ULL): goto ma41; default: goto ma42; }
ma5:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)18ULL)) goto mn45;
        void * t47 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t47;
        void * t48 = gm_l1;
        void * t49 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)18ULL));
        GM_ST(void *, ((uint8_t *)&t50 + 8), t49);
        t3 = t50;
        goto me4;
mn45:;
ma6:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)19ULL)) goto mn51;
        void * t53 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t53;
        uint32_t t54 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l3 = t54;
        void * t55 = gm_l2;
        void * t56 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t57 = gm_l3;
        gm_tbl8il92l97ut t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t58 + 0), t56);
        GM_ST(uint32_t, ((uint8_t *)&t58 + 8), t57);
        gm_teti411eujvf4 t59;
        memset(&t59, 0, sizeof t59);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t59 + 8), t58);
        t3 = t59;
        goto me4;
mn51:;
ma7:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t61 != ((uint8_t)20ULL)) goto mn60;
        void * t62 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t62;
        void * t63 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t63;
        uint8_t t64 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l6 = t64;
        gm_tlcl4u558l6p5 t65;
        memset((uint8_t *)&t65 + 17, 0, 7);
        void * t66 = gm_l4;
        void * t67 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t65 + 0, t67);
        void * t68 = gm_l5;
        void * t69 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t65 + 8, t69);
        uint8_t t70 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&t65 + 16, t70);
        gm_teti411eujvf4 t71;
        memset(&t71, 0, sizeof t71);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t71 + 8), t65);
        t3 = t71;
        goto me4;
mn60:;
ma8:;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t73 != ((uint8_t)21ULL)) goto mn72;
        gm_tao63pk3r18v5 t74 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t74;
        gm_tao63pk3r18v5 t75;
        memset((uint8_t *)&t75 + 25, 0, 7);
        void * t76 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
        void * t77 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t75 + 0, t77);
        void * t78 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
        void * t79 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t75 + 8, t79);
        void * t80 = GM_LD(void *, (((uint8_t *)&gm_l7) + 16));
        void * t81 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t75 + 16, t81);
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 24));
        GM_ST(uint8_t, (uint8_t *)&t75 + 24, t82);
        gm_teti411eujvf4 t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t83 + 8), t75);
        t3 = t83;
        goto me4;
mn72:;
ma9:;
        uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t85 != ((uint8_t)22ULL)) goto mn84;
        void * t86 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t86;
        void * t87 = gm_l8;
        void * t88 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t89;
        memset(&t89, 0, sizeof t89);
        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t89 + 8), t88);
        t3 = t89;
        goto me4;
mn84:;
ma10:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t91 != ((uint8_t)23ULL)) goto mn90;
        void * t92 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t92;
        void * t93 = gm_l9;
        void * t94 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t95;
        memset(&t95, 0, sizeof t95);
        GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)23ULL));
        GM_ST(void *, ((uint8_t *)&t95 + 8), t94);
        t3 = t95;
        goto me4;
mn90:;
ma11:;
        uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t97 != ((uint8_t)24ULL)) goto mn96;
        void * t98 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t98;
        void * t99 = gm_l10;
        void * t100 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t101;
        memset(&t101, 0, sizeof t101);
        GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t101 + 8), t100);
        t3 = t101;
        goto me4;
mn96:;
ma12:;
        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t103 != ((uint8_t)25ULL)) goto mn102;
        void * t104 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t104;
        void * t105 = gm_l11;
        void * t106 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t107;
        memset(&t107, 0, sizeof t107);
        GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)25ULL));
        GM_ST(void *, ((uint8_t *)&t107 + 8), t106);
        t3 = t107;
        goto me4;
mn102:;
ma13:;
        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t109 != ((uint8_t)29ULL)) goto mn108;
        gm_t9qkh0rsooc8d t110 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l12 = t110;
        void * t111 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t111;
        gm_t9qkh0rsooc8d t112 = gm_l12;
        void * t113 = gm_l13;
        void * t114 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8ghgl2jj0qha t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t115 + 0), t112);
        GM_ST(void *, ((uint8_t *)&t115 + 8), t114);
        gm_teti411eujvf4 t116;
        memset(&t116, 0, sizeof t116);
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t116 + 8), t115);
        t3 = t116;
        goto me4;
mn108:;
ma14:;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t118 != ((uint8_t)30ULL)) goto mn117;
        gm_ts6oqppjh0ba3 t119 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l14 = t119;
        void * t120 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l15 = t120;
        void * t121 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l16 = t121;
        gm_trl9d7m1k5lbd t122;
        memset((uint8_t *)&t122 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t123 = gm_l14;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t122 + 0, t123);
        void * t124 = gm_l15;
        void * t125 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t122 + 8, t125);
        void * t126 = gm_l16;
        void * t127 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t122 + 16, t127);
        gm_teti411eujvf4 t128;
        memset(&t128, 0, sizeof t128);
        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)30ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t128 + 8), t122);
        t3 = t128;
        goto me4;
mn117:;
ma15:;
        uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t130 != ((uint8_t)31ULL)) goto mn129;
        uint8_t t131 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l17 = t131;
        void * t132 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l18 = t132;
        void * t133 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l19 = t133;
        gm_ta612fil812pm t134;
        memset((uint8_t *)&t134 + 1, 0, 7);
        uint8_t t135 = gm_l17;
        GM_ST(uint8_t, (uint8_t *)&t134 + 0, t135);
        void * t136 = gm_l18;
        void * t137 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t134 + 8, t137);
        void * t138 = gm_l19;
        void * t139 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t134 + 16, t139);
        gm_teti411eujvf4 t140;
        memset(&t140, 0, sizeof t140);
        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)31ULL));
        GM_ST(gm_ta612fil812pm, ((uint8_t *)&t140 + 8), t134);
        t3 = t140;
        goto me4;
mn129:;
ma16:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t142 != ((uint8_t)32ULL)) goto mn141;
        gm_tmn7n0n9raou5 t143 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l20 = t143;
        void * t144 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l21 = t144;
        gm_tmn7n0n9raou5 t145 = gm_l20;
        void * t146 = gm_l21;
        void * t147 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8jsc4n9vp2ib t148;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t148 + 0), t145);
        GM_ST(void *, ((uint8_t *)&t148 + 8), t147);
        gm_teti411eujvf4 t149;
        memset(&t149, 0, sizeof t149);
        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)32ULL));
        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t149 + 8), t148);
        t3 = t149;
        goto me4;
mn141:;
ma17:;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t151 != ((uint8_t)33ULL)) goto mn150;
        gm_tvdg94bk7aa22 t152 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l22 = t152;
        gm_b16_8 t153 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l23 = t153;
        gm_tt8o5kebou62r t154;
        memset((uint8_t *)&t154 + 12, 0, 4);
        gm_tvdg94bk7aa22 t155 = gm_l22;
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t154 + 0, t155);
        gm_b16_8 t156 = gm_l23;
        gm_b16_8 t157 = t156;
        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
        gm_b16_8 t159 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t154 + 16, t159);
        gm_teti411eujvf4 t160;
        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t160 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t160 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t160 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t160 + 8), t154);
        t3 = t160;
        goto me4;
mn150:;
ma18:;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t162 != ((uint8_t)57ULL)) goto mn161;
        gm_tandvbt63uk7n t163 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t2) + 8) + 8));
        gm_l24 = t163;
        gm_tandvbt63uk7n t164;
        memset((uint8_t *)&t164 + 4, 0, 4);
        gm_tesaltf0mhcki t165 = GM_LD(gm_tesaltf0mhcki, (((uint8_t *)&gm_l24) + 0));
        GM_ST(gm_tesaltf0mhcki, (uint8_t *)&t164 + 0, t165);
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
        gm_b16_8 t167 = t166;
        gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
        gm_b16_8 t169 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t164 + 8, t169);
        gm_teti411eujvf4 t170;
        memset(&t170, 0, sizeof t170);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tandvbt63uk7n, ((uint8_t *)&t170 + 8), t164);
        t3 = t170;
        goto me4;
mn161:;
ma19:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t172 != ((uint8_t)34ULL)) goto mn171;
        gm_tsth9qu1dv0g3 t173 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l25 = t173;
        gm_b16_8 t174 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 40));
        gm_l26 = t174;
        {
            gm_tsth9qu1dv0g3 t176 = F.l25;
            F.l27 = t176;
        }
        {
            uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 0));
            if (t178 != ((uint8_t)2ULL)) goto ie177;
            void * t179 = GM_LD(void *, (((uint8_t *)&F.l25) + 8));
            gm_l28 = t179;
            {
                void * t181 = gm_l28;
                void * t182 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tsth9qu1dv0g3 t183;
                GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t183 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t183 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t183 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)2ULL));
                GM_ST(void *, ((uint8_t *)&t183 + 8), t182);
                F.l27 = t183;
            }
            goto ix177;
ie177:;
ix177:;
        }
        gm_to76jli3bqddq t184;
        memcpy((uint8_t *)&t184 + 0, ((uint8_t *)&F.l27), 40);
        gm_b16_8 t185 = gm_l26;
        gm_b16_8 t186 = t185;
        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
        gm_b16_8 t188 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t184 + 40, t188);
        gm_teti411eujvf4 t189;
        GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t189 + 8), t184);
        gm_teti411eujvf4 t190 = t189;
        t3 = t190;
        goto me4;
mn171:;
ma20:;
        uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t192 != ((uint8_t)35ULL)) goto mn191;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l29 = t193;
        gm_b16_8 t194 = gm_l29;
        gm_b16_8 t195 = t194;
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
        gm_b16_8 t197 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t196);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t198;
        memset(&t198, 0, sizeof t198);
        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t198 + 8), t197);
        t3 = t198;
        goto me4;
mn191:;
ma21:;
        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t200 != ((uint8_t)36ULL)) goto mn199;
        gm_b16_8 t201 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l30 = t201;
        gm_b16_8 t202 = gm_l30;
        gm_b16_8 t203 = t202;
        gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
        gm_b16_8 t205 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t204);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t206;
        memset(&t206, 0, sizeof t206);
        GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t206 + 8), t205);
        t3 = t206;
        goto me4;
mn199:;
ma22:;
        uint8_t t208 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t208 != ((uint8_t)37ULL)) goto mn207;
        void * t209 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l31 = t209;
        void * t210 = gm_l31;
        void * t211 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t212;
        memset(&t212, 0, sizeof t212);
        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)37ULL));
        GM_ST(void *, ((uint8_t *)&t212 + 8), t211);
        t3 = t212;
        goto me4;
mn207:;
ma23:;
        uint8_t t214 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t214 != ((uint8_t)38ULL)) goto mn213;
        gm_ttggq7ng4kp3p t215 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t2) + 8) + 8));
        gm_l32 = t215;
        uint32_t t216 = GM_LD(uint32_t, (((uint8_t *)&gm_l32) + 0));
        void * t217 = GM_LD(void *, (((uint8_t *)&gm_l32) + 8));
        void * t218 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t217);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t219;
        GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t219 + 0), t216);
        GM_ST(void *, ((uint8_t *)&t219 + 8), t218);
        gm_teti411eujvf4 t220;
        memset(&t220, 0, sizeof t220);
        GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)38ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t220 + 8), t219);
        t3 = t220;
        goto me4;
mn213:;
ma24:;
        uint8_t t222 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t222 != ((uint8_t)39ULL)) goto mn221;
        void * t223 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l33 = t223;
        void * t224 = gm_l33;
        void * t225 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t224);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t226;
        memset(&t226, 0, sizeof t226);
        GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)39ULL));
        GM_ST(void *, ((uint8_t *)&t226 + 8), t225);
        t3 = t226;
        goto me4;
mn221:;
ma25:;
        uint8_t t228 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t228 != ((uint8_t)40ULL)) goto mn227;
        void * t229 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l34 = t229;
        uint8_t t230 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l35 = t230;
        void * t231 = gm_l34;
        void * t232 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t231);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t233 = gm_l35;
        gm_tsplfr33dsvji t234;
        GM_ST(uint8_t, (uint8_t *)&t234 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t234 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t234 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t234 + 0), t232);
        GM_ST(uint8_t, ((uint8_t *)&t234 + 8), t233);
        gm_teti411eujvf4 t235;
        memset(&t235, 0, sizeof t235);
        GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)40ULL));
        GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t235 + 8), t234);
        t3 = t235;
        goto me4;
mn227:;
ma26:;
        uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t237 != ((uint8_t)41ULL)) goto mn236;
        gm_t3dcoc7077t1q t238 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t2) + 8) + 8));
        gm_l36 = t238;
        gm_t3dcoc7077t1q t239;
        void * t240 = GM_LD(void *, (((uint8_t *)&gm_l36) + 0));
        void * t241 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t239 + 0, t241);
        gm_t9805qtonbsag t242 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l36) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t239 + 8, t242);
        void * t243 = GM_LD(void *, (((uint8_t *)&gm_l36) + 16));
        void * t244 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t243);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t239 + 16, t244);
        gm_teti411eujvf4 t245;
        memset(&t245, 0, sizeof t245);
        GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)41ULL));
        GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&t245 + 8), t239);
        t3 = t245;
        goto me4;
mn236:;
ma27:;
        uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t247 != ((uint8_t)42ULL)) goto mn246;
        gm_t083kd7b1kkag t248 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t2) + 8) + 8));
        gm_l37 = t248;
        gm_t083kd7b1kkag t249;
        memset((uint8_t *)&t249 + 20, 0, 4);
        void * t250 = GM_LD(void *, (((uint8_t *)&gm_l37) + 0));
        void * t251 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t250);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t249 + 0, t251);
        gm_t9805qtonbsag t252 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l37) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t249 + 8, t252);
        uint32_t t253 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 16));
        GM_ST(uint32_t, (uint8_t *)&t249 + 16, t253);
        gm_teti411eujvf4 t254;
        memset(&t254, 0, sizeof t254);
        GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)42ULL));
        GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t254 + 8), t249);
        t3 = t254;
        goto me4;
mn246:;
ma28:;
        uint8_t t256 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t256 != ((uint8_t)43ULL)) goto mn255;
        void * t257 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l38 = t257;
        void * t258 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l39 = t258;
        uint8_t t259 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l40 = t259;
        gm_tlcl4u558l6p5 t260;
        memset((uint8_t *)&t260 + 17, 0, 7);
        void * t261 = gm_l38;
        void * t262 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t261);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t260 + 0, t262);
        void * t263 = gm_l39;
        void * t264 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t263);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t260 + 8, t264);
        uint8_t t265 = gm_l40;
        GM_ST(uint8_t, (uint8_t *)&t260 + 16, t265);
        gm_teti411eujvf4 t266;
        memset(&t266, 0, sizeof t266);
        GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)43ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t266 + 8), t260);
        t3 = t266;
        goto me4;
mn255:;
ma29:;
        uint8_t t268 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t268 != ((uint8_t)44ULL)) goto mn267;
        void * t269 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l41 = t269;
        gm_b16_8 t270 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l42 = t270;
        {
            gm_f_list_58nr42v_3((void *)&F.l43, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t273 = gm_l42;
                gm_l44 = t273;
            }
            {
                gm_l45 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t275 = gm_l45;
                    uint64_t t276 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    uint8_t t277 = (t275 < t276);
                    if (!t277) goto lb274;
                    {
                        uint64_t t280 = gm_l45;
                        uint64_t t281 = (uintptr_t)t280;
                        uint64_t t282 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        gm_tbbqefuhv2r8h t285 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t281 * 24));
                        gm_l46 = t285;
                        {
                            void * t287 = (void *)((uint8_t *)&F.l43);
                            gm_tbbqefuhv2r8h t288;
                            void * t289 = GM_LD(void *, (((uint8_t *)&gm_l46) + 0));
                            void * t290 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t289);
                            if (gm_unw.kind) {
                                goto gm_ll278;
                            }
                            GM_ST(void *, (uint8_t *)&t288 + 0, t290);
                            void * t291 = GM_LD(void *, (((uint8_t *)&gm_l46) + 8));
                            void * t292 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t291);
                            if (gm_unw.kind) {
                                goto gm_ll278;
                            }
                            GM_ST(void *, (uint8_t *)&t288 + 8, t292);
                            void * t293 = GM_LD(void *, (((uint8_t *)&gm_l46) + 16));
                            void * t294 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t293);
                            if (gm_unw.kind) {
                                goto gm_ll278;
                            }
                            GM_ST(void *, (uint8_t *)&t288 + 16, t294);
                            gm_tftb1odc739t5 t295 = gm_f_push_bpovr2k_3(t287, &t288);
                            gm_tftb1odc739t5 t296 = t295;
                            if (((((uint8_t *)&t296))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t296) + 1, gm_srcgksioc7kn741, 38, 29);
                            gm_b0_1 t297 = GM_LD(gm_b0_1, (((uint8_t *)&t296) + 1));
                        }
                        goto me283;
mn284:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 38, 13);
me283:;
                    }
lc274:;
                    {
                        uint64_t t299 = gm_l45;
                        uint64_t r300; bool o301 = __builtin_add_overflow(t299, ((uint64_t)1ULL), &r300);
                        uint64_t t302 = r300;
                        gm_l45 = t302;
                    }
                }
gm_ll278:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb274; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc274; }
                }
                goto gm_lad;
lb274:;
            }
        }
        gm_tkaapcii86udk t303;
        void * t304 = gm_l41;
        void * t305 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t304);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t303 + 0, t305);
        gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t303 + 8, t306);
        gm_teti411eujvf4 t307;
        memset(&t307, 0, sizeof t307);
        GM_ST(uint8_t, ((uint8_t *)&t307 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t307 + 8), t303);
        gm_teti411eujvf4 t308 = t307;
        t3 = t308;
        goto me4;
mn267:;
ma30:;
        uint8_t t310 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t310 != ((uint8_t)45ULL)) goto mn309;
        void * t311 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l47 = t311;
        void * t312 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l48 = t312;
        uint8_t t313 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l49 = t313;
        gm_tlcl4u558l6p5 t314;
        memset((uint8_t *)&t314 + 17, 0, 7);
        void * t315 = gm_l47;
        void * t316 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t314 + 0, t316);
        void * t317 = gm_l48;
        void * t318 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t317);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t314 + 8, t318);
        uint8_t t319 = gm_l49;
        GM_ST(uint8_t, (uint8_t *)&t314 + 16, t319);
        gm_teti411eujvf4 t320;
        memset(&t320, 0, sizeof t320);
        GM_ST(uint8_t, ((uint8_t *)&t320 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t320 + 8), t314);
        t3 = t320;
        goto me4;
mn309:;
ma31:;
        uint8_t t322 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t322 != ((uint8_t)46ULL)) goto mn321;
        void * t323 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l50 = t323;
        void * t324 = gm_l50;
        void * t325 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t324);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t326;
        memset(&t326, 0, sizeof t326);
        GM_ST(uint8_t, ((uint8_t *)&t326 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t326 + 8), t325);
        t3 = t326;
        goto me4;
mn321:;
ma32:;
        uint8_t t328 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t328 != ((uint8_t)47ULL)) goto mn327;
        gm_trl2khmuphncd t329 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 8) + 8));
        gm_l51 = t329;
        gm_trl2khmuphncd t330;
        gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 0));
        gm_b16_8 t332 = t331;
        gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
        gm_b16_8 t334 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t333);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t330 + 0, t334);
        void * t335 = GM_LD(void *, (((uint8_t *)&gm_l51) + 16));
        void * t336 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t335);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t330 + 16, t336);
        void * t337 = GM_LD(void *, (((uint8_t *)&gm_l51) + 24));
        void * t338 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t337);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t330 + 24, t338);
        gm_teti411eujvf4 t339;
        GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t339 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t339 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t339 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)47ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t339 + 8), t330);
        t3 = t339;
        goto me4;
mn327:;
ma33:;
        uint8_t t341 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t341 != ((uint8_t)48ULL)) goto mn340;
        gm_t710lsrq1m8gk t342 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l52 = t342;
        gm_t710lsrq1m8gk t343;
        memset((uint8_t *)&t343 + 4, 0, 4);
        uint32_t t344 = GM_LD(uint32_t, (((uint8_t *)&F.l52) + 0));
        GM_ST(uint32_t, (uint8_t *)&t343 + 0, t344);
        gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&F.l52) + 8));
        gm_b16_8 t346 = t345;
        gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
        gm_b16_8 t348 = gm_f_clone_conds_ljrasoa((void *)NULL, cx, NULL, t347);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t343 + 8, t348);
        void * t349 = GM_LD(void *, (((uint8_t *)&F.l52) + 24));
        void * t350 = gm_f_clone_block_5tu7v2v((void *)NULL, cx, NULL, t349);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t343 + 24, t350);
        void * t351 = GM_LD(void *, (((uint8_t *)&F.l52) + 32));
        void * t352 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t351);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t343 + 32, t352);
        gm_teti411eujvf4 t353;
        GM_ST(uint8_t, (uint8_t *)&t353 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t353 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t353 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t353 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t353 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t353 + 0), ((uint8_t)48ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t353 + 8), t343);
        t3 = t353;
        goto me4;
mn340:;
ma34:;
        uint8_t t355 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t355 != ((uint8_t)49ULL)) goto mn354;
        gm_teo9d4mncjfdu t356 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t2) + 8) + 8));
        gm_l53 = t356;
        gm_teo9d4mncjfdu t357;
        memset((uint8_t *)&t357 + 12, 0, 4);
        gm_te9hrk0u35rev t358 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l53) + 0));
        GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t357 + 0, t358);
        void * t359 = GM_LD(void *, (((uint8_t *)&gm_l53) + 16));
        void * t360 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t359);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t357 + 16, t360);
        gm_teti411eujvf4 t361;
        memset(&t361, 0, sizeof t361);
        GM_ST(uint8_t, ((uint8_t *)&t361 + 0), ((uint8_t)49ULL));
        GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t361 + 8), t357);
        t3 = t361;
        goto me4;
mn354:;
ma35:;
        uint8_t t363 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t363 != ((uint8_t)51ULL)) goto mn362;
        uint32_t t364 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l54 = t364;
        void * t365 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l55 = t365;
        uint32_t t366 = gm_l54;
        void * t367 = gm_l55;
        void * t368 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t367);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t369;
        GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t369 + 0), t366);
        GM_ST(void *, ((uint8_t *)&t369 + 8), t368);
        gm_teti411eujvf4 t370;
        memset(&t370, 0, sizeof t370);
        GM_ST(uint8_t, ((uint8_t *)&t370 + 0), ((uint8_t)51ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t370 + 8), t369);
        t3 = t370;
        goto me4;
mn362:;
ma36:;
        uint8_t t372 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t372 != ((uint8_t)52ULL)) goto mn371;
        void * t373 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l56 = t373;
        void * t374 = gm_l56;
        void * t375 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t374);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t376;
        memset(&t376, 0, sizeof t376);
        GM_ST(uint8_t, ((uint8_t *)&t376 + 0), ((uint8_t)52ULL));
        GM_ST(void *, ((uint8_t *)&t376 + 8), t375);
        t3 = t376;
        goto me4;
mn371:;
ma37:;
        uint8_t t378 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t378 != ((uint8_t)53ULL)) goto mn377;
        void * t379 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l57 = t379;
        void * t380 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l58 = t380;
        void * t381 = gm_l57;
        void * t382 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t381);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t383 = gm_l58;
        void * t384 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t383);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t385;
        GM_ST(void *, ((uint8_t *)&t385 + 0), t382);
        GM_ST(void *, ((uint8_t *)&t385 + 8), t384);
        gm_teti411eujvf4 t386;
        memset(&t386, 0, sizeof t386);
        GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)53ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t386 + 8), t385);
        t3 = t386;
        goto me4;
mn377:;
ma38:;
        uint8_t t388 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t388 != ((uint8_t)54ULL)) goto mn387;
        void * t389 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l59 = t389;
        void * t390 = gm_l59;
        void * t391 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t390);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_teti411eujvf4 t392;
        memset(&t392, 0, sizeof t392);
        GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)54ULL));
        GM_ST(void *, ((uint8_t *)&t392 + 8), t391);
        t3 = t392;
        goto me4;
mn387:;
ma39:;
        uint8_t t394 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t394 != ((uint8_t)55ULL)) goto mn393;
        void * t395 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t395;
        {
            void * t397 = gm_l60;
            gm_th1g69d7vpipg t398 = GM_LD(gm_th1g69d7vpipg, (((uint8_t *)t397) + 0));
            F.l61 = t398;
        }
        {
            void * t400 = gm_l60;
            uint8_t t401 = GM_LD(uint8_t, ((((uint8_t *)t400) + 0) + 0));
            if (t401 != ((uint8_t)1ULL)) goto ie399;
            void * t402 = GM_LD(void *, ((((uint8_t *)t400) + 0) + 8));
            gm_l62 = t402;
            {
                void * t404 = gm_l62;
                void * t405 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t404);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th1g69d7vpipg t406;
                GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t406 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t406 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t406 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)1ULL));
                GM_ST(void *, ((uint8_t *)&t406 + 8), t405);
                F.l61 = t406;
            }
            goto ix399;
ie399:;
ix399:;
        }
        gm_tntn2tf15puib t407;
        memcpy((uint8_t *)&t407 + 0, ((uint8_t *)&F.l61), 40);
        void * t408 = gm_l60;
        gm_b16_8 t409 = GM_LD(gm_b16_8, (((uint8_t *)t408) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 40, t409);
        void * t410 = gm_l60;
        gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)t410) + 56));
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 56, t411);
        void * t412 = gm_l60;
        gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)t412) + 72));
        gm_b16_8 t414 = t413;
        gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&t414));
        gm_b16_8 t416 = gm_f_clone_exprs_umh77j3((void *)NULL, cx, NULL, t415);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 72, t416);
        gm_tq7s98ihonkju t417 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t407, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl8b626vj63)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t418 = t417;
        if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 8, gm_srcgksioc7kn741, 53, 28);
        void * t419 = GM_LD(void *, (((uint8_t *)&t418) + 8));
        gm_teti411eujvf4 t420;
        memset(&t420, 0, sizeof t420);
        GM_ST(uint8_t, ((uint8_t *)&t420 + 0), ((uint8_t)55ULL));
        GM_ST(void *, ((uint8_t *)&t420 + 8), t419);
        gm_teti411eujvf4 t421 = t420;
        t3 = t421;
        goto me4;
mn393:;
ma40:;
        uint8_t t423 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t423 != ((uint8_t)56ULL)) goto mn422;
        void * t424 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l63 = t424;
        {
            void * t426 = gm_l63;
            gm_th1g69d7vpipg t427 = GM_LD(gm_th1g69d7vpipg, (((uint8_t *)t426) + 0));
            F.l64 = t427;
        }
        {
            void * t429 = gm_l63;
            uint8_t t430 = GM_LD(uint8_t, ((((uint8_t *)t429) + 0) + 0));
            if (t430 != ((uint8_t)1ULL)) goto ie428;
            void * t431 = GM_LD(void *, ((((uint8_t *)t429) + 0) + 8));
            gm_l65 = t431;
            {
                void * t433 = gm_l65;
                void * t434 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th1g69d7vpipg t435;
                GM_ST(uint8_t, (uint8_t *)&t435 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t435 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t435 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t435 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t435 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t435 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t435 + 0), ((uint8_t)1ULL));
                GM_ST(void *, ((uint8_t *)&t435 + 8), t434);
                F.l64 = t435;
            }
            goto ix428;
ie428:;
ix428:;
        }
        gm_t9i38ih07bojq t436;
        memset((uint8_t *)&t436 + 60, 0, 4);
        memcpy((uint8_t *)&t436 + 0, ((uint8_t *)&F.l64), 40);
        void * t437 = gm_l63;
        void * t438 = GM_LD(void *, (((uint8_t *)t437) + 40));
        void * t439 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t438);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t436 + 40, t439);
        void * t440 = gm_l63;
        void * t441 = GM_LD(void *, (((uint8_t *)t440) + 48));
        void * t442 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t441);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t436 + 48, t442);
        void * t443 = gm_l63;
        uint32_t t444 = GM_LD(uint32_t, (((uint8_t *)t443) + 56));
        GM_ST(uint32_t, (uint8_t *)&t436 + 56, t444);
        gm_tq7s98ihonkju t445 = gm_f_new_ges19gd((void *)NULL, cx, NULL, &t436, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg8c0kikv62)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t446 = t445;
        if (((((uint8_t *)&t446))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t446) + 8, gm_srcgksioc7kn741, 58, 36);
        void * t447 = GM_LD(void *, (((uint8_t *)&t446) + 8));
        gm_teti411eujvf4 t448;
        memset(&t448, 0, sizeof t448);
        GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)56ULL));
        GM_ST(void *, ((uint8_t *)&t448 + 8), t447);
        gm_teti411eujvf4 t449 = t448;
        t3 = t449;
        goto me4;
mn422:;
ma41:;
        uint8_t t451 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t451 != ((uint8_t)58ULL)) goto mn450;
        gm_t710lsrq1m8gk t452 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t2) + 8) + 8));
        F.l66 = t452;
        {
            gm_f_list_p0pjtrh((void *)&F.l67, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t455 = GM_LD(gm_b16_8, (((uint8_t *)&F.l66) + 8));
                gm_l68 = t455;
            }
            {
                gm_l69 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t457 = gm_l69;
                    uint64_t t458 = GM_LD(uintptr_t, ((uint8_t *)&gm_l68) + 8);
                    uint8_t t459 = (t457 < t458);
                    if (!t459) goto lb456;
                    {
                        uint64_t t462 = gm_l69;
                        uint64_t t463 = (uintptr_t)t462;
                        uint64_t t464 = GM_LD(uintptr_t, ((uint8_t *)&gm_l68) + 8);
                        gm_t4lnf71mruqpt t467 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l68)) + t463 * 32));
                        gm_l70 = t467;
                        {
                            uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                            if (t471 != ((uint8_t)0ULL)) goto mn470;
                            void * t472 = GM_LD(void *, (((uint8_t *)&gm_l70) + 8));
                            gm_l71 = t472;
                            void * t473 = (void *)((uint8_t *)&F.l67);
                            void * t474 = gm_l71;
                            void * t475 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t474);
                            if (gm_unw.kind) {
                                goto gm_ll460;
                            }
                            gm_t4lnf71mruqpt t476;
                            GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t476 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t476 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t476 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)0ULL));
                            GM_ST(void *, ((uint8_t *)&t476 + 8), t475);
                            gm_tftb1odc739t5 t477 = gm_f_push_f5fsh94(t473, &t476);
                            gm_tftb1odc739t5 t478 = t477;
                            if (((((uint8_t *)&t478))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t478) + 1, gm_srcgksioc7kn741, 64, 36);
                            gm_b0_1 t479 = GM_LD(gm_b0_1, (((uint8_t *)&t478) + 1));
                            goto me469;
mn470:;
                            uint8_t t481 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                            if (t481 != ((uint8_t)1ULL)) goto mn480;
                            gm_b16_8 t482 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l70) + 8) + 0));
                            gm_l72 = t482;
                            void * t483 = GM_LD(void *, ((((uint8_t *)&gm_l70) + 8) + 16));
                            gm_l73 = t483;
                            void * t484 = (void *)((uint8_t *)&F.l67);
                            gm_tpe1hv3fekn05 t485;
                            gm_b16_8 t486 = gm_l72;
                            GM_ST(gm_b16_8, (uint8_t *)&t485 + 0, t486);
                            void * t487 = gm_l73;
                            void * t488 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t487);
                            if (gm_unw.kind) {
                                goto gm_ll460;
                            }
                            GM_ST(void *, (uint8_t *)&t485 + 16, t488);
                            gm_t4lnf71mruqpt t489;
                            GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t489 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t489 + 8), t485);
                            gm_tftb1odc739t5 t490 = gm_f_push_f5fsh94(t484, &t489);
                            gm_tftb1odc739t5 t491 = t490;
                            if (((((uint8_t *)&t491))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t491) + 1, gm_srcgksioc7kn741, 65, 40);
                            gm_b0_1 t492 = GM_LD(gm_b0_1, (((uint8_t *)&t491) + 1));
                            goto me469;
mn480:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 63, 17);
me469:;
                        }
                        goto me465;
mn466:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 62, 13);
me465:;
                    }
lc456:;
                    {
                        uint64_t t494 = gm_l69;
                        uint64_t r495; bool o496 = __builtin_add_overflow(t494, ((uint64_t)1ULL), &r495);
                        uint64_t t497 = r495;
                        gm_l69 = t497;
                    }
                }
gm_ll460:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb456; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc456; }
                }
                goto gm_lad;
lb456:;
            }
        }
        gm_t710lsrq1m8gk t498;
        memset((uint8_t *)&t498 + 4, 0, 4);
        uint32_t t499 = GM_LD(uint32_t, (((uint8_t *)&F.l66) + 0));
        GM_ST(uint32_t, (uint8_t *)&t498 + 0, t499);
        gm_b16_8 t500 = GM_LD(gm_b16_8, (((uint8_t *)&F.l67) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t498 + 8, t500);
        void * t501 = GM_LD(void *, (((uint8_t *)&F.l66) + 24));
        void * t502 = gm_f_clone_opt_plt4ceg((void *)NULL, cx, NULL, t501);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t498 + 24, t502);
        void * t503 = GM_LD(void *, (((uint8_t *)&F.l66) + 32));
        void * t504 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t503);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t498 + 32, t504);
        gm_teti411eujvf4 t505;
        GM_ST(uint8_t, (uint8_t *)&t505 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t505 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t505 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t505 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t505 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t505 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t505 + 8), t498);
        gm_teti411eujvf4 t506 = t505;
        t3 = t506;
        goto me4;
mn450:;
ma42:;
        void * t508 = gm_l0;
        gm_teti411eujvf4 t509 = GM_LD(gm_teti411eujvf4, (((uint8_t *)t508) + 8));
        t3 = t509;
        goto me4;
mn507:;
ma43:;
        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 7, 19);
me4:;
        F.l74 = t3;
    }
    {
        void * t510 = gm_l0;
        gm_t88rcucg1b7uk t511 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t510) + 0));
        void * t512 = gm_l0;
        gm_tl874t2atnlh3 t513 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t512) + 72));
        void * t514 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t511, &(*(gm_teti411eujvf4 *)((uint8_t *)&F.l74)), t513);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t515 = 0;
        F.ret = t514;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_clone_exprs_umh77j3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_clone_exprs_umh77j3 F;
    gm_b16_8 gm_l0; /* xs */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_vp1djv1_2((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l2 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = gm_l2;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l4 = t14;
                    {
                        void * t16 = (void *)((uint8_t *)&F.l1);
                        uint64_t t17 = gm_l4;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_srcgksioc7kn741, 82, 47);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t18 * 88);
                        void * t21 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t20);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t22 = gm_f_push_d943vgp_2(t16, &(*(gm_t69m56meqefs8 *)((uint8_t *)t21)));
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcgksioc7kn741, 82, 26);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 82, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
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
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t31 = 0;
        F.ret = t30;
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
extern __attribute__((aligned(4))) void * gm_f_clone_opt_plt4ceg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_clone_pat_cq2uu68(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_clone_pat_cq2uu68 F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    void * gm_l2; /* q */
    void * gm_l3; /* q */
    void * gm_l4; /* q */
    uint32_t gm_l5; /* v */
    uint32_t gm_l6; /* v */
    void * gm_l7; /* q */
    gm_b16_8 gm_l8; /* fs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l12; /* f */
    gm_b16_8 gm_l13; /* ps */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    void * gm_l17; /* q */
    GM_STACK_CHECK(&F, 2080u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tb38v5s9j1r32 t3;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma12; default: goto ma13; }
ma5:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)2ULL)) goto mn16;
        void * t18 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t18;
        void * t19 = gm_l1;
        void * t20 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tb38v5s9j1r32 t21;
        memset(&t21, 0, sizeof t21);
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
        t3 = t21;
        goto me4;
mn16:;
ma6:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)5ULL)) goto mn22;
        void * t24 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t24;
        void * t25 = gm_l2;
        void * t26 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tb38v5s9j1r32 t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        GM_ST(void *, ((uint8_t *)&t27 + 8), t26);
        t3 = t27;
        goto me4;
mn22:;
ma7:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)6ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t30;
        void * t31 = gm_l3;
        void * t32 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tb38v5s9j1r32 t33;
        memset(&t33, 0, sizeof t33);
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)6ULL));
        GM_ST(void *, ((uint8_t *)&t33 + 8), t32);
        t3 = t33;
        goto me4;
mn28:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)7ULL)) goto mn34;
        void * t36 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t36;
        void * t37 = gm_l4;
        void * t38 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tb38v5s9j1r32 t39;
        memset(&t39, 0, sizeof t39);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)7ULL));
        GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
        t3 = t39;
        goto me4;
mn34:;
ma9:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)8ULL)) goto mn40;
        uint32_t t42 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t42;
        if ((GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn40;
        uint32_t t43 = gm_l5;
        gm_ttggq7ng4kp3p t44;
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t43);
        GM_ST(void *, ((uint8_t *)&t44 + 8), ((void *)0));
        gm_tb38v5s9j1r32 t45;
        memset(&t45, 0, sizeof t45);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)8ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t45 + 8), t44);
        t3 = t45;
        goto me4;
mn40:;
ma10:;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t47 != ((uint8_t)8ULL)) goto mn46;
        uint32_t t48 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l6 = t48;
        if (!(GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8)) != 0)) goto mn46;
        void * t49 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t49;
        uint32_t t50 = gm_l6;
        void * t51 = gm_l7;
        void * t52 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t53;
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t53 + 0), t50);
        GM_ST(void *, ((uint8_t *)&t53 + 8), t52);
        gm_tb38v5s9j1r32 t54;
        memset(&t54, 0, sizeof t54);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)8ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t54 + 8), t53);
        t3 = t54;
        goto me4;
mn46:;
ma11:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)9ULL)) goto mn55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t57;
        {
            gm_f_list_i33ccm0((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t60 = gm_l8;
                gm_l10 = t60;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t62 = gm_l11;
                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t64 = (t62 < t63);
                    if (!t64) goto lb61;
                    {
                        uint64_t t67 = gm_l11;
                        uint64_t t68 = (uintptr_t)t67;
                        uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_ttggq7ng4kp3p t72 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t68 * 16));
                        gm_l12 = t72;
                        {
                            void * t74 = (void *)((uint8_t *)&F.l9);
                            uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 0));
                            void * t76 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                            void * t77 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t76);
                            if (gm_unw.kind) {
                                goto gm_ll65;
                            }
                            gm_ttggq7ng4kp3p t78;
                            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t78 + 0), t75);
                            GM_ST(void *, ((uint8_t *)&t78 + 8), t77);
                            gm_tftb1odc739t5 t79 = gm_f_push_31airv7(t74, GM_CONV(gm_csqbqksqco2bm6, t78));
                            gm_tftb1odc739t5 t80 = t79;
                            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srcgksioc7kn741, 127, 27);
                            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                        }
                        goto me70;
mn71:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 127, 13);
me70:;
                    }
lc61:;
                    {
                        uint64_t t83 = gm_l11;
                        uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                        uint64_t t86 = r84;
                        gm_l11 = t86;
                    }
                }
gm_ll65:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb61; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc61; }
                }
                goto gm_lad;
lb61:;
            }
        }
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_tb38v5s9j1r32 t88;
        memset(&t88, 0, sizeof t88);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)9ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 8), t87);
        gm_tb38v5s9j1r32 t89 = t88;
        t3 = t89;
        goto me4;
mn55:;
ma12:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t91 != ((uint8_t)10ULL)) goto mn90;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l13 = t92;
        {
            gm_f_list_nh3d5t2((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t95 = gm_l13;
                gm_l15 = t95;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t97 = gm_l16;
                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t99 = (t97 < t98);
                    if (!t99) goto lb96;
                    {
                        uint64_t t102 = gm_l16;
                        uint64_t t103 = (uintptr_t)t102;
                        uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        void * t105 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t103 * 8));
                        void * t106 = t105;
                        void * t109 = GM_LD(void *, ((uint8_t *)&t106));
                        gm_l17 = t109;
                        {
                            void * t111 = (void *)((uint8_t *)&F.l14);
                            void * t112 = gm_l17;
                            void * t113 = gm_f_clone_pat_cq2uu68((void *)NULL, cx, NULL, t112);
                            if (gm_unw.kind) {
                                goto gm_ll100;
                            }
                            gm_tftb1odc739t5 t114 = gm_f_push_u2jka7o(t111, t113);
                            gm_tftb1odc739t5 t115 = t114;
                            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcgksioc7kn741, 132, 27);
                            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                        }
                        goto me107;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 132, 13);
me107:;
                    }
lc96:;
                    {
                        uint64_t t118 = gm_l16;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l16 = t121;
                    }
                }
gm_ll100:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb96; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc96; }
                }
                goto gm_lad;
lb96:;
            }
        }
        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        gm_tb38v5s9j1r32 t123;
        memset(&t123, 0, sizeof t123);
        GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t123 + 8), t122);
        gm_tb38v5s9j1r32 t124 = t123;
        t3 = t124;
        goto me4;
mn90:;
ma13:;
        void * t126 = gm_l0;
        gm_tb38v5s9j1r32 t127 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t126) + 8));
        t3 = t127;
        goto me4;
mn125:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcgksioc7kn741, 118, 18);
me4:;
        F.l18 = t3;
    }
    {
        void * t128 = gm_l0;
        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t128) + 0));
        void * t130 = gm_l0;
        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t130) + 120));
        void * t132 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t129, &(*(gm_tb38v5s9j1r32 *)((uint8_t *)&F.l18)), t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t133 = 0;
        F.ret = t132;
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
