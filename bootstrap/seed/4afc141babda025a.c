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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { gm_b16_8 f0; gm_t8vuock3jnv59 f1; } gm_tfkvqluahdpno;
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint64_t f0; gm_t8vuock3jnv59 f1; } gm_tsnpee0o8kei7;
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_tfkvqluahdpno) == 40 && _Alignof(gm_tfkvqluahdpno) == 8 && offsetof(gm_tfkvqluahdpno, f0) == 0 && offsetof(gm_tfkvqluahdpno, f1) == 16, "layout of `Member`");
_Static_assert(sizeof(gm_tsnpee0o8kei7) == 32 && _Alignof(gm_tsnpee0o8kei7) == 8 && offsetof(gm_tsnpee0o8kei7, f0) == 0 && offsetof(gm_tsnpee0o8kei7, f1) == 8, "layout of `(i: usize, x: Value)`");
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_as_array_nfvj8pk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_bool_g9idbdk {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_as_i64_iojmg3q {
    gm_frame h;
    gm_t913bb735pvt5 ret;
};
struct gm_fr_as_str_9rhg9v0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_u64_0obhk18 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bool_value_j9bnrp8 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_field_7904fsq {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_fn_t0qgm7n {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_95caidg {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_is_true_3osltpn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_0202sdh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_path2_enc7vaj {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_put_91stqpj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_08k4kf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_to_text_2cum4i7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_write_value_5haur8t {
    gm_frame h;
    gm_b16_8 l10; /* ms */
    gm_trqfvmjtt4sij l11; /* order */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_array_nfvj8pk_c(gm_t8vuock3jnv59 a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk(const void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk_c(gm_t8vuock3jnv59 a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_as_i64_iojmg3q(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_as_i64_iojmg3q_c(gm_t8vuock3jnv59 a0);
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_9rhg9v0_c(gm_t8vuock3jnv59 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_as_u64_0obhk18(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_as_u64_0obhk18_c(gm_t8vuock3jnv59 a0);
inline __attribute__((aligned(4))) void gm_f_bool_value_j9bnrp8(void *gm_out, uint8_t a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_bool_value_j9bnrp8_c(uint8_t a0);
__attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_qmfcuqk_2(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_field_7904fsq_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_t0qgm7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_get_fjdb5ou(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn_c(gm_t8vuock3jnv59 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_string_n2ks51u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_path2_enc7vaj_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_m55rf90_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_text_ls49dp6_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_d1vui09(gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_value_pkuqal3_c(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_value_5haur8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_array_k32ana3 F;
    gm_b16_8 gm_l0; /* items */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_0202sdh_3((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_extend_qmfcuqk_2(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgp1oqfehgsjb, 42, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_t8vuock3jnv59 t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)4ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
        uint8_t t9 = 0;
        F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_array_nfvj8pk F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* xs */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 57, 41);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_bool_g9idbdk F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tqs5p4n2tq19v t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)1ULL)) goto mn3;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    uint8_t t6 = gm_l1;
    gm_tqs5p4n2tq19v t7;
    GM_ST(uint8_t, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[1] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_tqs5p4n2tq19v t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 62, 37);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_9rhg9v0 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)3ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 52, 38);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_bool_value_j9bnrp8(void *gm_out, uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bool_value_j9bnrp8 F;
    uint8_t gm_l0; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_t8vuock3jnv59 t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t2 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t2 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    GM_ST(uint8_t, ((uint8_t *)&t2 + 8), t1);
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi_3 F;
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
inline __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_7904fsq F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* key */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l1;
    gm_t3ph3kbiqt1ah t2;
    gm_f_get_fjdb5ou((void *)&t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t1);
    gm_t3ph3kbiqt1ah t3 = t2;
    gm_t8vuock3jnv59 t4;
    if (((((uint8_t *)&t3))[24] != 0)) {
        gm_t8vuock3jnv59 t5 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_t8vuock3jnv59 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        t4 = t6;
    }
    F.ret = t4;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_t0qgm7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_t0qgm7n F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)t1));
    uint64_t t3 = (uintptr_t)t2;
    uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_write_value_5haur8t *)((&F.h)->up))->l10) + 8);
    if (t3 >= (uintptr_t)(t4)) gm_trap_index(t3, t4, gm_srcgp1oqfehgsjb, 121, 67);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_value_5haur8t *)((&F.h)->up))->l10)) + t3 * 40) + 0));
    void * t6 = gm_l1;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)t6));
    uint64_t t8 = (uintptr_t)t7;
    uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&((struct gm_fr_write_value_5haur8t *)((&F.h)->up))->l10) + 8);
    if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcgp1oqfehgsjb, 121, 80);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&((struct gm_fr_write_value_5haur8t *)((&F.h)->up))->l10)) + t8 * 40) + 0));
    gm_b16_8 t11 = t5;
    gm_b16_8 t12 = t10;
    uint8_t t13 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12));
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0) {
    struct gm_fr_int_95caidg F;
    int64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1 = gm_l0;
    gm_tm98ap4112hee t2;
    GM_ST(int64_t, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_gej351p((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcgp1oqfehgsjb, 33, 74);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
    gm_b16_8 t6 = t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_tnpifv20irnph t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
    gm_t8vuock3jnv59 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t9 + 8), t8);
    F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_true_3osltpn F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    uint8_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 31u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)1ULL)) goto mn3;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    uint8_t t6 = gm_l1;
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 89, 36);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi_3();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt_2();
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
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_obj_aoa7fl7 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_792g2go_2((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_path2_enc7vaj F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_b16_8 gm_l1; /* a */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l1;
    gm_t8vuock3jnv59 t2;
    gm_f_field_7904fsq((void *)&t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)), t1);
    gm_b16_8 t3 = gm_l2;
    gm_f_field_7904fsq((void *)&(*(gm_t8vuock3jnv59 *)gm_out), &t2, t3);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sort_by_08k4kf2(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_08k4kf2 F;
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
        gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_ls49dp6 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_t8vuock3jnv59 t2;
    GM_ST(uint8_t, (uint8_t *)&t2 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t2 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t2 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 8), t1);
    F.ret = t2;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_text_2cum4i7 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_value_5haur8t((void *)NULL, cx, NULL, t2, &(*(gm_t8vuock3jnv59 *)((uint8_t *)a0)));
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
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_pkuqal3 F;
    void * gm_l0; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 0) + 0));
    gm_t8vuock3jnv59 t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 8), t2);
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
extern __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) void gm_f_as_array_nfvj8pk(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_array_nfvj8pk_c(gm_t8vuock3jnv59 a0) { gm_th2lqbo77pvsc gm_r; gm_f_as_array_nfvj8pk(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk(const void * a0);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_as_bool_g9idbdk_c(gm_t8vuock3jnv59 a0) { return gm_f_as_bool_g9idbdk(&a0); }
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_as_i64_iojmg3q(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_i64_iojmg3q F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_tnpifv20irnph gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t3 != ((uint8_t)2ULL)) goto ie2;
        gm_tnpifv20irnph t4 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a0) + 8));
        gm_l1 = t4;
        {
            uint8_t t7 = 0;
            uint8_t t8 = 0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
            gm_tf5srh4mp4b3i t10 = gm_f_index_of_hdeg351(t9, ((uint8_t)46ULL));
            gm_tf5srh4mp4b3i t11 = t10;
            uint8_t t12;
            if (((((uint8_t *)&t11))[8] != 0)) goto mn14;
            t12 = ((uint8_t)0);
            goto me13;
mn14:;
            t12 = ((uint8_t)1);
            goto me13;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 70, 8);
me13:;
            if (!t12) {
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
                gm_tf5srh4mp4b3i t17 = gm_f_index_of_hdeg351(t16, ((uint8_t)101ULL));
                gm_tf5srh4mp4b3i t18 = t17;
                uint8_t t19;
                if (((((uint8_t *)&t18))[8] != 0)) goto mn21;
                t19 = ((uint8_t)0);
                goto me20;
mn21:;
                t19 = ((uint8_t)1);
                goto me20;
mn22:;
                gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 70, 44);
me20:;
                t8 = t19;
            } else t8 = 1;
            if (!t8) {
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
                gm_tf5srh4mp4b3i t24 = gm_f_index_of_hdeg351(t23, ((uint8_t)69ULL));
                gm_tf5srh4mp4b3i t25 = t24;
                uint8_t t26;
                if (((((uint8_t *)&t25))[8] != 0)) goto mn28;
                t26 = ((uint8_t)0);
                goto me27;
mn28:;
                t26 = ((uint8_t)1);
                goto me27;
mn29:;
                gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 70, 80);
me27:;
                t7 = t26;
            } else t7 = 1;
            if (!t7) goto ie6;
            {
                gm_t913bb735pvt5 t31;
                memset(&t31, 0, sizeof t31);
                uint8_t t32 = 0;
                F.ret = t31;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_tnpifv20irnph t33 = gm_l1;
            gm_t913bb735pvt5 t34 = gm_f_to_i64_d1vui09(t33);
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t913bb735pvt5 t37;
            memset(&t37, 0, sizeof t37);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_as_i64_iojmg3q_c(gm_t8vuock3jnv59 a0) { return gm_f_as_i64_iojmg3q(&a0); }
extern __attribute__((aligned(4))) void gm_f_as_str_9rhg9v0(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_as_str_9rhg9v0_c(gm_t8vuock3jnv59 a0) { gm_th2lqbo77pvsc gm_r; gm_f_as_str_9rhg9v0(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_as_u64_0obhk18(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_u64_0obhk18 F;
    gm_t8vuock3jnv59 gm_l0; /* v */
    gm_tnpifv20irnph gm_l1; /* n */
    uint64_t gm_l2; /* x */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 392u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t3 != ((uint8_t)2ULL)) goto ie2;
        gm_tnpifv20irnph t4 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a0) + 8));
        gm_l1 = t4;
        {
            uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)&gm_l1) + 0) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
            if (!t8) goto ie6;
            {
                gm_tf5srh4mp4b3i t10;
                memset(&t10, 0, sizeof t10);
                uint8_t t11 = 0;
                F.ret = t10;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
                gm_l3 = t13;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l4;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t21 * 1));
                        uint8_t t24 = t23;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        gm_l5 = t27;
                        {
                            {
                                uint8_t t31 = gm_l5;
                                gm_l6 = t31;
                            }
                            uint8_t t32 = 0;
                            uint8_t t33 = gm_l6;
                            uint8_t t34 = (((uint8_t)48ULL) <= t33);
                            if (t34) {
                                uint8_t t35 = gm_l6;
                                uint8_t t36 = (t35 <= ((uint8_t)57ULL));
                                t32 = t36;
                            } else t32 = 0;
                            uint8_t t37 = (uint8_t)!t32;
                            uint8_t t38 = t37;
                            if (!t38) goto ie29;
                            {
                                gm_tf5srh4mp4b3i t40;
                                memset(&t40, 0, sizeof t40);
                                uint8_t t41 = 0;
                                F.ret = t40;
                                goto gm_fret;
                            }
                            goto ix29;
ie29:;
ix29:;
                        }
                        {
                            uint8_t t42 = gm_l5;
                            uint8_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint8_t)48ULL), &r43);
                            if (o44) gm_trap("integer overflow in `-`", gm_srcgp1oqfehgsjb, 81, 18);
                            uint8_t t45 = r43;
                            uint64_t t46 = (uint64_t)t45;
                            gm_l7 = t46;
                        }
                        {
                            uint64_t t48 = gm_l2;
                            uint64_t t49 = gm_l7;
                            uint64_t r50; bool o51 = __builtin_sub_overflow(((uint64_t)18446744073709551615ULL), t49, &r50);
                            if (o51) gm_trap("integer overflow in `-`", gm_srcgp1oqfehgsjb, 82, 17);
                            uint64_t t52 = r50;
                            if (((uint64_t)10ULL) == 0) gm_trap("division by zero", gm_srcgp1oqfehgsjb, 82, 16);
                            uint64_t t53 = (uint64_t)(t52 / ((uint64_t)10ULL));
                            uint8_t t54 = (t48 > t53);
                            if (!t54) goto ie47;
                            {
                                gm_tf5srh4mp4b3i t56;
                                memset(&t56, 0, sizeof t56);
                                uint8_t t57 = 0;
                                F.ret = t56;
                                goto gm_fret;
                            }
                            goto ix47;
ie47:;
ix47:;
                        }
                        {
                            uint64_t t58 = gm_l2;
                            uint64_t r59; bool o60 = __builtin_mul_overflow(t58, ((uint64_t)10ULL), &r59);
                            if (o60) gm_trap("integer overflow in `*`", gm_srcgp1oqfehgsjb, 83, 13);
                            uint64_t t61 = r59;
                            uint64_t t62 = gm_l7;
                            uint64_t r63; bool o64 = __builtin_add_overflow(t61, t62, &r63);
                            if (o64) gm_trap("integer overflow in `+`", gm_srcgp1oqfehgsjb, 83, 13);
                            uint64_t t65 = r63;
                            gm_l2 = t65;
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 79, 5);
me25:;
                    }
lc14:;
                    {
                        uint64_t t67 = gm_l4;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                        uint64_t t70 = r68;
                        gm_l4 = t70;
                    }
                }
lb14:;
            }
        }
        {
            uint64_t t71 = gm_l2;
            gm_tf5srh4mp4b3i t72;
            GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t72 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t72), t71);
            ((uint8_t *)&t72)[8] = 1;
            uint8_t t73 = 0;
            F.ret = t72;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_tf5srh4mp4b3i t75;
            memset(&t75, 0, sizeof t75);
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_as_u64_0obhk18_c(gm_t8vuock3jnv59 a0) { return gm_f_as_u64_0obhk18(&a0); }
extern __attribute__((aligned(4))) void gm_f_bool_value_j9bnrp8(void *gm_out, uint8_t a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_bool_value_j9bnrp8_c(uint8_t a0) { gm_t8vuock3jnv59 gm_r; gm_f_bool_value_j9bnrp8(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_field_7904fsq(void *gm_out, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_field_7904fsq_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1) { gm_t8vuock3jnv59 gm_r; gm_f_field_7904fsq(&gm_r, &a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_fn_t0qgm7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_true_3osltpn_c(gm_t8vuock3jnv59 a0) { return gm_f_is_true_3osltpn(&a0); }
extern __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) void gm_f_path2_enc7vaj(void *gm_out, const void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_path2_enc7vaj_c(gm_t8vuock3jnv59 a0, gm_b16_8 a1, gm_b16_8 a2) { gm_t8vuock3jnv59 gm_r; gm_f_path2_enc7vaj(&gm_r, &a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_put_91stqpj F;
    void * gm_l0; /* o */
    gm_b16_8 gm_l1; /* key */
    gm_t8vuock3jnv59 gm_l2; /* v */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            gm_l3 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = gm_l3;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = t11;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l5 = t15;
                    {
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        void * t20 = gm_l0;
                        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 0) + 0) + 8);
                        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcgp1oqfehgsjb, 18, 12);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 0) + 0)) + t19 * 40) + 0));
                        gm_b16_8 t23 = gm_l1;
                        gm_b16_8 t24 = t22;
                        gm_b16_8 t25 = t23;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie17;
                        {
                            uint64_t t28 = gm_l5;
                            uint64_t t29 = (uintptr_t)t28;
                            void * t30 = gm_l0;
                            uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 0) + 0) + 8);
                            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcgp1oqfehgsjb, 19, 13);
                            gm_t8vuock3jnv59 t32 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)a2));
                            GM_ST(gm_t8vuock3jnv59, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 0) + 0)) + t29 * 40) + 16), t32);
                        }
                        {
                            gm_b0_1 t33;
                            uint8_t t34 = 0;
                            F.ret = t33;
                            goto gm_fret;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 17, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t36 = gm_l4;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l4 = t39;
                }
            }
lb5:;
        }
    }
    {
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 0);
        gm_tfkvqluahdpno t42;
        gm_b16_8 t43 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        memcpy((uint8_t *)&t42 + 16, ((uint8_t *)a2), 24);
        gm_tftb1odc739t5 t44 = gm_f_push_m55rf90_2(t41, &t42);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcgp1oqfehgsjb, 23, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_text_ls49dp6_c(gm_b16_8 a0) { gm_t8vuock3jnv59 gm_r; gm_f_text_ls49dp6(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_to_text_2cum4i7(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_t8vuock3jnv59 gm_f_value_pkuqal3_c(void * a0) { gm_t8vuock3jnv59 gm_r; gm_f_value_pkuqal3(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_write_value_5haur8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_write_value_5haur8t F;
    void * gm_l0; /* out */
    gm_t8vuock3jnv59 gm_l1; /* v */
    uint8_t gm_l2; /* b */
    gm_tnpifv20irnph gm_l3; /* n */
    gm_b16_8 gm_l4; /* s */
    gm_b16_8 gm_l5; /* xs */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t8vuock3jnv59 gm_l9; /* x */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    uint8_t gm_l15; /* last */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* j */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* k */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 1383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; default: goto ma9; }
ma3:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        void * t13 = gm_l0;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t15 = gm_f_extend_0cs85gs(t13, t14);
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srcgp1oqfehgsjb, 98, 18);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        goto me2;
mn11:;
ma4:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)1ULL)) goto mn18;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t20;
        void * t21 = gm_l0;
        gm_b16_8 t22;
        uint8_t t24 = gm_l2;
        if (!t24) goto ie23;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sehko5bb96s6p));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(4));
        gm_b16_8 t27 = t26;
        t22 = t27;
        goto ix23;
ie23:;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sogehh8t1m7tn));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
        gm_b16_8 t30 = t29;
        t22 = t30;
ix23:;
        gm_tftb1odc739t5 t31 = gm_f_extend_0cs85gs(t21, t22);
        gm_tftb1odc739t5 t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcgp1oqfehgsjb, 99, 21);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        goto me2;
mn18:;
ma5:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t35 != ((uint8_t)2ULL)) goto mn34;
        gm_tnpifv20irnph t36 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l1) + 8));
        gm_l3 = t36;
        void * t37 = gm_l0;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
        gm_tftb1odc739t5 t40 = t39;
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srcgp1oqfehgsjb, 100, 23);
        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
        goto me2;
mn34:;
ma6:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t43 != ((uint8_t)3ULL)) goto mn42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        gm_l4 = t44;
        gm_b16_8 t45 = gm_l4;
        void * t46 = gm_l0;
        gm_b0_1 t47 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me2;
mn42:;
ma7:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t49 != ((uint8_t)4ULL)) goto mn48;
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        gm_l5 = t50;
        {
            void * t52 = gm_l0;
            gm_tftb1odc739t5 t53 = gm_f_push_t83rg2m(t52, ((uint8_t)91ULL));
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcgp1oqfehgsjb, 103, 13);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            {
                gm_b16_8 t57 = gm_l5;
                gm_b16_8 t58 = t57;
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                gm_l6 = t59;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t61 = gm_l7;
                    uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t63 = (t61 < t62);
                    if (!t63) goto lb60;
                    {
                        gm_tsnpee0o8kei7 t66;
                        uint64_t t67 = gm_l7;
                        GM_ST(uint64_t, (uint8_t *)&t66 + 0, t67);
                        uint64_t t68 = gm_l7;
                        uint64_t t69 = (uintptr_t)t68;
                        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        memcpy((uint8_t *)&t66 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t69 * 24), 24);
                        gm_tsnpee0o8kei7 t71 = t66;
                        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&t71) + 0));
                        gm_l8 = t74;
                        gm_t8vuock3jnv59 t75 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t71) + 8));
                        gm_l9 = t75;
                        {
                            uint64_t t78 = gm_l8;
                            uint8_t t79 = (t78 > ((uint64_t)0ULL));
                            if (!t79) goto ie77;
                            {
                                void * t81 = gm_l0;
                                gm_tftb1odc739t5 t82 = gm_f_push_t83rg2m(t81, ((uint8_t)44ULL));
                                gm_tftb1odc739t5 t83 = t82;
                                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srcgp1oqfehgsjb, 105, 28);
                                gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                            }
                            goto ix77;
ie77:;
ix77:;
                        }
                        {
                            void * t85 = gm_l0;
                            gm_b0_1 t86 = gm_f_write_value_5haur8t((void *)NULL, cx, NULL, t85, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l9)));
                            if (gm_unw.kind) {
                                goto gm_ll64;
                            }
                        }
                        goto me72;
mn73:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 104, 13);
me72:;
                    }
lc60:;
                    {
                        uint64_t t88 = gm_l7;
                        uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                        uint64_t t91 = r89;
                        gm_l7 = t91;
                    }
                }
gm_ll64:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb60; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc60; }
                }
                goto gm_lad;
lb60:;
            }
        }
        {
            void * t92 = gm_l0;
            gm_tftb1odc739t5 t93 = gm_f_push_t83rg2m(t92, ((uint8_t)93ULL));
            gm_tftb1odc739t5 t94 = t93;
            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcgp1oqfehgsjb, 108, 13);
            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
        }
        goto me2;
mn48:;
ma8:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t97 != ((uint8_t)5ULL)) goto mn96;
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
        F.l10 = t98;
        {
            gm_f_list_5dhgg4i((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
        }
        {
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                gm_l12 = t102;
            }
            {
                for (;;) {
                    uint64_t t104 = gm_l13;
                    uint64_t t105 = gm_l12;
                    uint8_t t106 = (t104 < t105);
                    if (!t106) goto lb103;
                    {
                        uint64_t t109 = gm_l13;
                        uint64_t t110 = t109;
                        uint64_t t113 = GM_LD(uint64_t, ((uint8_t *)&t110));
                        gm_l14 = t113;
                        {
                            gm_l15 = ((uint8_t)1);
                        }
                        {
                            {
                                uint64_t t116 = gm_l14;
                                uint64_t r117; bool o118 = __builtin_add_overflow(t116, ((uint64_t)1ULL), &r117);
                                if (o118) gm_trap("integer overflow in `+`", gm_srcgp1oqfehgsjb, 116, 27);
                                uint64_t t119 = r117;
                                gm_l17 = t119;
                            }
                            {
                                uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                                gm_l16 = t120;
                            }
                            {
                                for (;;) {
                                    uint64_t t122 = gm_l17;
                                    uint64_t t123 = gm_l16;
                                    uint8_t t124 = (t122 < t123);
                                    if (!t124) goto lb121;
                                    {
                                        uint64_t t127 = gm_l17;
                                        uint64_t t128 = t127;
                                        uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&t128));
                                        gm_l18 = t131;
                                        {
                                            uint64_t t134 = gm_l18;
                                            uint64_t t135 = (uintptr_t)t134;
                                            uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                                            if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_srcgp1oqfehgsjb, 117, 24);
                                            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l10)) + t135 * 40) + 0));
                                            uint64_t t138 = gm_l14;
                                            uint64_t t139 = (uintptr_t)t138;
                                            uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                                            if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_srcgp1oqfehgsjb, 117, 37);
                                            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l10)) + t139 * 40) + 0));
                                            gm_b16_8 t142 = t137;
                                            gm_b16_8 t143 = t141;
                                            uint8_t t144 = (gm_eq0iqiubfm7e(((uint8_t *)&t142), ((uint8_t *)&t143)));
                                            if (!t144) goto ie133;
                                            {
                                                gm_l15 = ((uint8_t)0);
                                            }
                                            goto ix133;
ie133:;
ix133:;
                                        }
                                        goto me129;
mn130:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 116, 17);
me129:;
                                    }
lc121:;
                                    {
                                        uint64_t t147 = gm_l17;
                                        uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                        uint64_t t150 = r148;
                                        gm_l17 = t150;
                                    }
                                }
lb121:;
                            }
                        }
                        {
                            uint8_t t152 = gm_l15;
                            if (!t152) goto ie151;
                            {
                                void * t154 = (void *)((uint8_t *)&F.l11);
                                uint64_t t155 = gm_l14;
                                gm_tftb1odc739t5 t156 = gm_f_push_ucgmlo2(t154, t155);
                                gm_tftb1odc739t5 t157 = t156;
                                if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srcgp1oqfehgsjb, 119, 27);
                                gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
                            }
                            goto ix151;
ie151:;
ix151:;
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 114, 13);
me111:;
                    }
lc103:;
                    {
                        uint64_t t160 = gm_l13;
                        uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                        uint64_t t163 = r161;
                        gm_l13 = t163;
                    }
                }
lb103:;
            }
        }
        {
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
            gm_closure t165 = ((gm_closure){(void *)gm_f_fn_t0qgm7n, (void *)&F.h, cx});
            gm_f_sort_by_08k4kf2(t164, t165);
            if (gm_unw.kind) {
                goto gm_ld99_1;
            }
        }
        {
            void * t166 = gm_l0;
            gm_tftb1odc739t5 t167 = gm_f_push_t83rg2m(t166, ((uint8_t)123ULL));
            gm_tftb1odc739t5 t168 = t167;
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_srcgp1oqfehgsjb, 122, 13);
            gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
        }
        {
            {
                gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t172 = t171;
                gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                gm_l19 = t173;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t175 = gm_l20;
                    uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t177 = (t175 < t176);
                    if (!t177) goto lb174;
                    {
                        uint64_t t180 = gm_l20;
                        uint64_t t181 = gm_l20;
                        uint64_t t182 = (uintptr_t)t181;
                        uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        uint64_t t184 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t182 * 8));
                        gm_t2lqu8jsj511d t185;
                        GM_ST(uint64_t, ((uint8_t *)&t185 + 0), t180);
                        GM_ST(uint64_t, ((uint8_t *)&t185 + 8), t184);
                        gm_t2lqu8jsj511d t186 = t185;
                        uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&t186) + 0));
                        gm_l21 = t189;
                        uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&t186) + 8));
                        gm_l22 = t190;
                        {
                            uint64_t t193 = gm_l21;
                            uint8_t t194 = (t193 > ((uint64_t)0ULL));
                            if (!t194) goto ie192;
                            {
                                void * t196 = gm_l0;
                                gm_tftb1odc739t5 t197 = gm_f_push_t83rg2m(t196, ((uint8_t)44ULL));
                                gm_tftb1odc739t5 t198 = t197;
                                if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 1, gm_srcgp1oqfehgsjb, 124, 28);
                                gm_b0_1 t199 = GM_LD(gm_b0_1, (((uint8_t *)&t198) + 1));
                            }
                            goto ix192;
ie192:;
ix192:;
                        }
                        {
                            uint64_t t200 = gm_l22;
                            uint64_t t201 = (uintptr_t)t200;
                            uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                            if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_srcgp1oqfehgsjb, 125, 34);
                            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l10)) + t201 * 40) + 0));
                            void * t204 = gm_l0;
                            gm_b0_1 t205 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t203, t204);
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                        }
                        {
                            void * t206 = gm_l0;
                            gm_tftb1odc739t5 t207 = gm_f_push_t83rg2m(t206, ((uint8_t)58ULL));
                            gm_tftb1odc739t5 t208 = t207;
                            if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srcgp1oqfehgsjb, 126, 17);
                            gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
                        }
                        {
                            void * t210 = gm_l0;
                            uint64_t t211 = gm_l22;
                            uint64_t t212 = (uintptr_t)t211;
                            uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&F.l10) + 8);
                            if (t212 >= (uintptr_t)(t213)) gm_trap_index(t212, t213, gm_srcgp1oqfehgsjb, 127, 34);
                            gm_b0_1 t214 = gm_f_write_value_5haur8t((void *)NULL, cx, NULL, t210, &(*(gm_t8vuock3jnv59 *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&F.l10)) + t212 * 40) + 16)));
                            if (gm_unw.kind) {
                                goto gm_ll178;
                            }
                        }
                        goto me187;
mn188:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 123, 13);
me187:;
                    }
lc174:;
                    {
                        uint64_t t216 = gm_l20;
                        uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                        uint64_t t219 = r217;
                        gm_l20 = t219;
                    }
                }
gm_ll178:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb174; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc174; }
                }
                goto gm_ld99_1;
lb174:;
            }
        }
        {
            void * t220 = gm_l0;
            gm_tftb1odc739t5 t221 = gm_f_push_t83rg2m(t220, ((uint8_t)125ULL));
            gm_tftb1odc739t5 t222 = t221;
            if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_srcgp1oqfehgsjb, 129, 13);
            gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
        }
        {
            void * t224 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_d5p9g61(t224);
        }
        goto gm_ls225;
gm_ld99_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t226 = (void *)((uint8_t *)&F.l11);
            gm_f_deinit_d5p9g61(t226);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls225:;
        goto me2;
mn96:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcgp1oqfehgsjb, 97, 5);
me2:;
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
