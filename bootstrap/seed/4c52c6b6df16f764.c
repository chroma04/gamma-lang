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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
struct gm_fr_for_body_9nsvqlg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_s23qjlr {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_uri_map_mhud6ak {
    gm_frame h;
    gm_ttiitul0263ld l1; /* m */
    gm_ttiitul0263ld ret;
    gm_b0_1 w0;
};
struct gm_fr_watched_files_changed_qvnrc63 {
    gm_frame h;
    void * l0; /* srv */
    gm_b0_1 ret;
    gm_b0_1 w1;
};
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_9nsvqlg(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_iv0iced(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui39jfe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
extern uint8_t gm_c0gtrsff3e8[9] __attribute__((aligned(1)));
extern uint8_t gm_c0lficu5ug9[4] __attribute__((aligned(1)));
extern uint8_t gm_c0oemf1frl4[7] __attribute__((aligned(1)));
extern uint8_t gm_c0tggmkj5d0[1] __attribute__((aligned(1)));
extern uint8_t gm_c12u1ovcpek[14] __attribute__((aligned(1)));
extern uint8_t gm_c1q4po7noqk[12] __attribute__((aligned(1)));
extern uint8_t gm_c1sfg0li8h3[50] __attribute__((aligned(1)));
extern uint8_t gm_c2tmtf1q8pq[87] __attribute__((aligned(1)));
extern uint8_t gm_c3kocbd3l5h[4] __attribute__((aligned(1)));
extern uint8_t gm_c433gi9623u[17] __attribute__((aligned(1)));
extern uint8_t gm_c78m49i35ia[19] __attribute__((aligned(1)));
extern uint8_t gm_c7dg59gauug[10] __attribute__((aligned(1)));
extern uint8_t gm_ca1bh7jkvd3[9] __attribute__((aligned(1)));
extern uint8_t gm_cb2fg0a9k46[14] __attribute__((aligned(1)));
extern uint8_t gm_cbbajrmlhcc[3] __attribute__((aligned(1)));
extern uint8_t gm_cbdf5obmbv8[12] __attribute__((aligned(1)));
extern uint8_t gm_cbkngm2o96p[5] __attribute__((aligned(1)));
extern uint8_t gm_cbp17rggt3d[12] __attribute__((aligned(1)));
extern uint8_t gm_cbru991fg1t[11] __attribute__((aligned(1)));
extern uint8_t gm_cc37olh9uak[12] __attribute__((aligned(1)));
extern uint8_t gm_ccb8g03q3co[13] __attribute__((aligned(1)));
extern uint8_t gm_cdps8qsftpu[4] __attribute__((aligned(4)));
extern uint8_t gm_ce961isd2oi[10] __attribute__((aligned(1)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cf6v325lv09[14] __attribute__((aligned(1)));
extern uint8_t gm_cfa74gl0ote[4] __attribute__((aligned(1)));
extern uint8_t gm_cfas3u4spp8[9] __attribute__((aligned(1)));
extern uint8_t gm_cfkvti2s7oq[12] __attribute__((aligned(1)));
extern uint8_t gm_cftil4g103c[5] __attribute__((aligned(1)));
extern uint8_t gm_cfuaut7oa51[1] __attribute__((aligned(1)));
extern uint8_t gm_cg5i3k2h7tu[11] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e_4[16] __attribute__((aligned(8)));
extern uint8_t gm_ch4udh4cc5j[12] __attribute__((aligned(1)));
extern uint8_t gm_cibj24dglm2[5] __attribute__((aligned(1)));
extern uint8_t gm_ciuj8hs0q3l[8] __attribute__((aligned(8)));
extern uint8_t gm_cjts5fiuldu[192] __attribute__((aligned(8)));
extern uint8_t gm_ck3j0r2lm3c[586] __attribute__((aligned(1)));
extern uint8_t gm_ck4pksg2re9[4] __attribute__((aligned(1)));
extern uint8_t gm_cka88d5t8lu_3[16] __attribute__((aligned(8)));
extern uint8_t gm_cldb10luqdu[13] __attribute__((aligned(1)));
extern uint8_t gm_cmdrqgqs203[15] __attribute__((aligned(1)));
extern uint8_t gm_cndda2t88s7[6] __attribute__((aligned(1)));
extern uint8_t gm_cngietrs56f[1] __attribute__((aligned(1)));
extern uint8_t gm_cnohofor2q6[16] __attribute__((aligned(8)));
extern uint8_t gm_cog1l56ulfv[8] __attribute__((aligned(8)));
extern uint8_t gm_cp5tb1hpgdo[3] __attribute__((aligned(1)));
extern uint8_t gm_cprueqv60kg[1] __attribute__((aligned(1)));
extern uint8_t gm_crri3fcfba6[86] __attribute__((aligned(1)));
extern uint8_t gm_cs5feb7nj6r[3] __attribute__((aligned(1)));
extern uint8_t gm_csd3p6j3fi7[16] __attribute__((aligned(8)));
extern uint8_t gm_cssal36m00k[16] __attribute__((aligned(8)));
extern uint8_t gm_csu7k86qqgd[256] __attribute__((aligned(4)));
extern uint8_t gm_ct6asj8mekt[34] __attribute__((aligned(1)));
extern uint8_t gm_ct6pvba5oah[13] __attribute__((aligned(1)));
extern uint8_t gm_cte1ut67vb8[11] __attribute__((aligned(1)));
extern uint8_t gm_ctebdbqpdl1[3] __attribute__((aligned(1)));
extern uint8_t gm_cu04o97732p[7] __attribute__((aligned(1)));
extern uint8_t gm_cubqnpdse2d[4] __attribute__((aligned(1)));
extern uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
extern uint8_t gm_cvetthildsc[12] __attribute__((aligned(1)));
extern uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
extern void *const gm_hl4lv3uiadds[];
extern void *const gm_hl8iqbv5s08g[];
extern void *const gm_hltn0orqptuk[];
extern void *const gm_hluov08dhrrb[];
extern uint8_t gm_k1b2sgalr12[40] __attribute__((aligned(8)));
extern uint8_t gm_k70n01rs29g[40] __attribute__((aligned(8)));
extern uint8_t gm_kcrp199kueb[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_kfgeo9idkmp[40] __attribute__((aligned(8)));
extern uint8_t gm_kfqhpvvf5lv[40] __attribute__((aligned(8)));
extern uint8_t gm_kha5d1oq7pb[40] __attribute__((aligned(8)));
extern uint8_t gm_kignd7l31vo[40] __attribute__((aligned(8)));
extern uint8_t gm_kk6gotfp7pf[40] __attribute__((aligned(8)));
extern uint8_t gm_klfj89orh68[40] __attribute__((aligned(8)));
extern uint8_t gm_kli6567itlv[40] __attribute__((aligned(8)));
extern uint8_t gm_klim7pfh497[40] __attribute__((aligned(8)));
extern uint8_t gm_km03kohh9a4[40] __attribute__((aligned(8)));
extern uint8_t gm_kn6hjdk6jie[40] __attribute__((aligned(8)));
extern uint8_t gm_knasprnpdnf[40] __attribute__((aligned(8)));
extern uint8_t gm_krd7aj3c16u[40] __attribute__((aligned(8)));
extern uint8_t gm_krg6ee1n6ld[40] __attribute__((aligned(8)));
extern uint8_t gm_kvm0nu8rto4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00hinobqu5k5[41];
extern const uint8_t gm_s00li8b55l40s[17];
extern const uint8_t gm_s014h3o6j2aj2[21];
extern const uint8_t gm_s01kpv06m7g4n[66];
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s026u6q948evn[5];
extern const uint8_t gm_s06925hae81ss[41];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s08mr7037nglf[5];
extern const uint8_t gm_s0c2823k4ul3o[17];
extern const uint8_t gm_s0d1hchv6cdbo[11];
extern const uint8_t gm_s0fibs3hmu8k5[41];
extern const uint8_t gm_s0iditmthmvpu[21];
extern const uint8_t gm_s0j2j9kdc2obh[17];
extern const uint8_t gm_s0jrrhhm4skoj[15];
extern const uint8_t gm_s0k8snlvukl86[66];
extern const uint8_t gm_s0kvnv9teor3q[4];
extern const uint8_t gm_s0m07bui94rjq[8];
extern const uint8_t gm_s0m6lnv6m2d3p[28];
extern const uint8_t gm_s0nfu0i7mcaca[18];
extern const uint8_t gm_s0vltqrmpakvk[17];
extern const uint8_t gm_s1387c681koge[41];
extern const uint8_t gm_s1757p1g7h4me[42];
extern const uint8_t gm_s17cjlkd2o01f[29];
extern const uint8_t gm_s1b629p19p3qq[4];
extern const uint8_t gm_s1ctgkvj02ag0[34];
extern const uint8_t gm_s1fc8hs0lmr3u[9];
extern const uint8_t gm_s1fcd4t8vd2g5[30];
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s20qcu4sv6mcn[19];
extern const uint8_t gm_s221m8qkvkf71[23];
extern const uint8_t gm_s22cf6j01irhc[41];
extern const uint8_t gm_s24mnh1a25k0f[31];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s26emr7fhbmjr[22];
extern const uint8_t gm_s2aj4fqmgmn1n[67];
extern const uint8_t gm_s2aj89r6sdims[28];
extern const uint8_t gm_s2e0u34gjtsqn[11];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s2haaj13v41fd[25];
extern const uint8_t gm_s2ht50jletjjv[23];
extern const uint8_t gm_s2iprgandb78l[8];
extern const uint8_t gm_s2kc17j0uoe9m[23];
extern const uint8_t gm_s2l6bc41uiup9[10];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s2nh23o3oe191[19];
extern const uint8_t gm_s2r03t2n892pm[27];
extern const uint8_t gm_s310nl8k9dnl2[59];
extern const uint8_t gm_s31ag3l5et116[14];
extern const uint8_t gm_s32428dkdndn7[141];
extern const uint8_t gm_s34o1ojfv2p3d[17];
extern const uint8_t gm_s36ugqba56le0[18];
extern const uint8_t gm_s3brj349ts8fs[9];
extern const uint8_t gm_s3hg5go4uihat[52];
extern const uint8_t gm_s3i94v2v4ifm3[17];
extern const uint8_t gm_s3jpgh4kdh4qh[15];
extern const uint8_t gm_s3k1sno8a2ipv[70];
extern const uint8_t gm_s3kfdt7s6c0hi[15];
extern const uint8_t gm_s3o5gaeteeggu[5];
extern const uint8_t gm_s3oap7aihaveg[11];
extern const uint8_t gm_s3rnim0uidvn4[41];
extern const uint8_t gm_s3rph20cdvn0u[16];
extern const uint8_t gm_s3vfg72f8aupk[20];
extern const uint8_t gm_s41h7dv6tes4i[57];
extern const uint8_t gm_s4231ufcv20f0[12];
extern const uint8_t gm_s42h828lb45uf[19];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4672be3dhb96[11];
extern const uint8_t gm_s4bovq0eevpad[23];
extern const uint8_t gm_s4bqq8347n0tp[5];
extern const uint8_t gm_s4dgh055v8im1[43];
extern const uint8_t gm_s4du6i4h6s7qo[32];
extern const uint8_t gm_s4dun9lv4h66e[4];
extern const uint8_t gm_s4j1eg5g5a64h[5];
extern const uint8_t gm_s4m37qfrs0qib[9];
extern const uint8_t gm_s4qg9pu9r16jq[7];
extern const uint8_t gm_s4t1alq7juu7a[67];
extern const uint8_t gm_s4v8j7s5sr5c1[66];
extern const uint8_t gm_s53ccf6bilp0b[4];
extern const uint8_t gm_s54d3qh188ekk[41];
extern const uint8_t gm_s551g0bkhdser[6];
extern const uint8_t gm_s5ejk09ngvect[3];
extern const uint8_t gm_s5gn6cmh4thug[38];
extern const uint8_t gm_s5j2jdmako4im[37];
extern const uint8_t gm_s5lrfo6g7933d[50];
extern const uint8_t gm_s5m5egkp3rutu[118];
extern const uint8_t gm_s5pgobtluqp1f[29];
extern const uint8_t gm_s5qoh8o6sn63e[24];
extern const uint8_t gm_s5u1jd4mc0941[32];
extern const uint8_t gm_s5ujsjbtr77bt[14];
extern const uint8_t gm_s5v9d9i0gsl8c[8];
extern const uint8_t gm_s5v9muv1jh89m[75];
extern const uint8_t gm_s65ir5du2gd8b[9];
extern const uint8_t gm_s6edba3m7mc9a[6];
extern const uint8_t gm_s6fmmm148r6o3[12];
extern const uint8_t gm_s6ioufa56mnlh[5];
extern const uint8_t gm_s6jncssts4sq8[40];
extern const uint8_t gm_s6juej7e0fhbb[8];
extern const uint8_t gm_s6m48vdr7h6mr[77];
extern const uint8_t gm_s6mimndcv0cge[4];
extern const uint8_t gm_s6o30d8d984s9[8];
extern const uint8_t gm_s6qe25ct4l9uk[34];
extern const uint8_t gm_s6rbdi4l8b3ii[18];
extern const uint8_t gm_s6tc15kuvnm0c[26];
extern const uint8_t gm_s73vhpsrl3o41[31];
extern const uint8_t gm_s78lhtg84qh95[9];
extern const uint8_t gm_s7bpkc0l992tl[17];
extern const uint8_t gm_s7c48l6el9tbd[12];
extern const uint8_t gm_s7c8ep4trp7bk[7];
extern const uint8_t gm_s7ekc2mcasshc[10];
extern const uint8_t gm_s7hc5dru68qbt[15];
extern const uint8_t gm_s7j04h96r4716[18];
extern const uint8_t gm_s7j2u3ofu66j8[5];
extern const uint8_t gm_s7kt2bei71pgo[4];
extern const uint8_t gm_s7l9fi1pqlcp5[4];
extern const uint8_t gm_s7mc3uqhc5a41[14];
extern const uint8_t gm_s7mie2m30h1s9[28];
extern const uint8_t gm_s7n79dja87bqg[28];
extern const uint8_t gm_s7ntlu0vonte9[12];
extern const uint8_t gm_s7o8vapurd6a7[12];
extern const uint8_t gm_s7s5thcg1q6e2[26];
extern const uint8_t gm_s7ulscihp75vl[73];
extern const uint8_t gm_s8075t766i1f0[50];
extern const uint8_t gm_s816ceel7tvfh[18];
extern const uint8_t gm_s898uefj5c9k6[28];
extern const uint8_t gm_s8a33u6s5sjkv[20];
extern const uint8_t gm_s8bhtqu29n7b4[39];
extern const uint8_t gm_s8c4bejqsu1da[48];
extern const uint8_t gm_s8do3iqjvck3j[45];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_s8lkplgn6m7u9[8];
extern const uint8_t gm_s8n4rd07albkt[16];
extern const uint8_t gm_s8pa6bhk4as9k[2];
extern const uint8_t gm_s8pfpkocrh9h6[4];
extern const uint8_t gm_s8pjuqfuormq3[51];
extern const uint8_t gm_s8q1hgp14vfur[7];
extern const uint8_t gm_s8s03uo3h8dsp[9];
extern const uint8_t gm_s8s99m7843kb6[10];
extern const uint8_t gm_s8uvhki57nqp6[30];
extern const uint8_t gm_s91c3em0lmpov[37];
extern const uint8_t gm_s91vsko3h6v6r[10];
extern const uint8_t gm_s97bho1civrt3[16];
extern const uint8_t gm_s985n04e0mov8[16];
extern const uint8_t gm_s989kd388iles[5];
extern const uint8_t gm_s993c5b6eq922[54];
extern const uint8_t gm_s99ll9oc7rh5u[15];
extern const uint8_t gm_s9ap8gk0oj12e[73];
extern const uint8_t gm_s9b81q4kouna6[11];
extern const uint8_t gm_s9cg9729snds3[14];
extern const uint8_t gm_s9hqndl9se5nc[31];
extern const uint8_t gm_s9j0jokaa6cqi[113];
extern const uint8_t gm_s9o2cv7g71nbc[41];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_s9ram1a46hdut[9];
extern const uint8_t gm_s9s0gbnmtpvjl[4];
extern const uint8_t gm_s9scq8a60n125[74];
extern const uint8_t gm_s9uc7or8diipg[35];
extern const uint8_t gm_s9um3j04r7jaj[7];
extern const uint8_t gm_sa01gg636lapd[100];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_sa0f63dcuo6q9[57];
extern const uint8_t gm_sa64nb2j51s60[19];
extern const uint8_t gm_sa91t1vl6p044[34];
extern const uint8_t gm_sa954a25u62up[46];
extern const uint8_t gm_sabhgcltm0thi[8];
extern const uint8_t gm_sabq7k0io9ql3[66];
extern const uint8_t gm_sachcj4qd96of[48];
extern const uint8_t gm_sacs352aeakr8[18];
extern const uint8_t gm_saean4ms0g52k[8];
extern const uint8_t gm_safll42cn0mu7[31];
extern const uint8_t gm_sagtv6bs2sviu[111];
extern const uint8_t gm_sah2hl0iapccb[8];
extern const uint8_t gm_saj4ijvbmtohg[9];
extern const uint8_t gm_sajfarek3i4jp[44];
extern const uint8_t gm_sal83f6sn6m3n[78];
extern const uint8_t gm_saqv3n8kbtahh[14];
extern const uint8_t gm_sas0hkqll4io0[10];
extern const uint8_t gm_sat7ifv94mb2p[40];
extern const uint8_t gm_savr1gmccp3v7[20];
extern const uint8_t gm_sb2nno7n67rvd[4];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sb6j3egm9oljk[5];
extern const uint8_t gm_sb7e7uf3pvdko[26];
extern const uint8_t gm_sbeoq5iu4cto8[11];
extern const uint8_t gm_sbfpg6hac36v9[19];
extern const uint8_t gm_sbj6flp3i82cp[32];
extern const uint8_t gm_sbkdsdb6p9kk7[4];
extern const uint8_t gm_sbp9encpv0e61[75];
extern const uint8_t gm_sbqq5iijjahf9[5];
extern const uint8_t gm_sbqv06vnui8sl[88];
extern const uint8_t gm_sbvet41ukod2k[16];
extern const uint8_t gm_sbvsnnbg6vc88[23];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_sc96am1bplmoh[80];
extern const uint8_t gm_sc9u21orrlttr[7];
extern const uint8_t gm_sccjki3qgi8m7[15];
extern const uint8_t gm_scd8m51mtmjv9[19];
extern const uint8_t gm_scfl995gsjrop[58];
extern const uint8_t gm_sck9nihohhsb7[15];
extern const uint8_t gm_scmob1k27904r[25];
extern const uint8_t gm_scnb9tl13g4lt[8];
extern const uint8_t gm_sd1ldutuv1qfu[12];
extern const uint8_t gm_sd5imeip8klsb[40];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_sd7ar5jm4rru0[48];
extern const uint8_t gm_sdahvc1997d01[14];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_sdd9sds4mgn8p[6];
extern const uint8_t gm_sdgqla1nhnp8n[34];
extern const uint8_t gm_sdh1dnevn1j10[8];
extern const uint8_t gm_sdle4vcf3dhti[3];
extern const uint8_t gm_sdm57kru4ksf7[7];
extern const uint8_t gm_sdo8qcbf74mp0[7];
extern const uint8_t gm_sdo9t6m49dsco[5];
extern const uint8_t gm_sdrg69bmlm0tm[30];
extern const uint8_t gm_sds03accef1j8[6];
extern const uint8_t gm_sdskn4nn8f7q0[5];
extern const uint8_t gm_sdtlgdl6ed52a[14];
extern const uint8_t gm_sdupocj1qjnur[7];
extern const uint8_t gm_se1krm9tn2oaa[3];
extern const uint8_t gm_se3b4jalim8at[6];
extern const uint8_t gm_seb1p7qlbq9p0[39];
extern const uint8_t gm_seglpsc899j08[11];
extern const uint8_t gm_seh91qmsr2tfr[8];
extern const uint8_t gm_sejinh96g1530[26];
extern const uint8_t gm_sejjhtlr11ojd[60];
extern const uint8_t gm_sekdm5lqg7176[43];
extern const uint8_t gm_sel0tu7gvv1na[4];
extern const uint8_t gm_sel1fj24qk32p[15];
extern const uint8_t gm_seno3sej6pod2[34];
extern const uint8_t gm_seo2gq6ouvlf6[59];
extern const uint8_t gm_seplj9699cert[10];
extern const uint8_t gm_setcl7798ov89[4];
extern const uint8_t gm_setnqjs5cth99[45];
extern const uint8_t gm_setofg57no9qt[34];
extern const uint8_t gm_sevf8q26aov70[6];
extern const uint8_t gm_sevm6b5otfta5[21];
extern const uint8_t gm_sf1qb0mllbs1p[34];
extern const uint8_t gm_sf1v2j94ne72t[16];
extern const uint8_t gm_sf5t1pig3221t[17];
extern const uint8_t gm_sf9hl2ppv6mim[8];
extern const uint8_t gm_sfag3nu74qn7r[31];
extern const uint8_t gm_sfb033os1o8m5[41];
extern const uint8_t gm_sfne4e7hfo6ag[51];
extern const uint8_t gm_sfq3o7cad07rv[14];
extern const uint8_t gm_sfqmvjqk1sdmt[6];
extern const uint8_t gm_sft2smqlktfvo[19];
extern const uint8_t gm_sg1c4njjf4gg0[12];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sg8sp4mbs6q31[34];
extern const uint8_t gm_sgc0gatbmtea3[13];
extern const uint8_t gm_sge9itd5fie17[21];
extern const uint8_t gm_sgemqs85b1iol[9];
extern const uint8_t gm_sgjci31f2rs96[49];
extern const uint8_t gm_sgk14ir7dv1gf[6];
extern const uint8_t gm_sgmqtrfjl8vp3[52];
extern const uint8_t gm_sgnvdmoebn4i7[16];
extern const uint8_t gm_sgpr1l6rnrmel[20];
extern const uint8_t gm_sgsm5p4jqmf3k[43];
extern const uint8_t gm_sh540objcvln0[25];
extern const uint8_t gm_sh630b08j7vfk[84];
extern const uint8_t gm_sh74p5jjd1e00[4];
extern const uint8_t gm_shbtdicfd8lcc[12];
extern const uint8_t gm_shem79vgdue54[5];
extern const uint8_t gm_sheobcjeiocck[16];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shlvd8v9thcc8[9];
extern const uint8_t gm_shmsiqao0jnvn[42];
extern const uint8_t gm_sho8l3vae133f[8];
extern const uint8_t gm_shpr5sdqmule5[9];
extern const uint8_t gm_shqja2jbdo2o0[58];
extern const uint8_t gm_sht1158338t3d[11];
extern const uint8_t gm_shv3ttlq9l359[15];
extern const uint8_t gm_si1j2hanh22ll[16];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_si7fe9i76cdgb[13];
extern const uint8_t gm_si82ti7ep94be[11];
extern const uint8_t gm_siapf4ssb1jv4[5];
extern const uint8_t gm_siho9h2ssfvv9[62];
extern const uint8_t gm_sio5n0k35hfb4[65];
extern const uint8_t gm_sivh46n0690a4[4];
extern const uint8_t gm_sj2a9rjbqmk4h[8];
extern const uint8_t gm_sj3fn9etjoihf[116];
extern const uint8_t gm_sj5ke3qalt8me[9];
extern const uint8_t gm_sj696i9uo1a9s[14];
extern const uint8_t gm_sjbhp9l7seful[7];
extern const uint8_t gm_sjh4l64ncn038[52];
extern const uint8_t gm_sjijer52q1op6[7];
extern const uint8_t gm_sjmccqdb90fnl[3];
extern const uint8_t gm_sjmngec2on365[5];
extern const uint8_t gm_sjndugdkuh68b[58];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjp4ps1gkcjl1[8];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_sjqjrfqdmp9ao[7];
extern const uint8_t gm_sjvtpaes95ji4[27];
extern const uint8_t gm_sk09uplg1v4qc[16];
extern const uint8_t gm_sk2jb5rv3689k[34];
extern const uint8_t gm_skagf3vj30re5[17];
extern const uint8_t gm_skau91fmi0eei[42];
extern const uint8_t gm_skf4n3tc7nif9[5];
extern const uint8_t gm_skgir5knhpsin[9];
extern const uint8_t gm_skhs0ffupj1to[30];
extern const uint8_t gm_skiffh4urhl7e[14];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_skm1g73974f87[4];
extern const uint8_t gm_sknpneumdgqfs[38];
extern const uint8_t gm_sko342193gvqe[41];
extern const uint8_t gm_skp2amifebhq6[41];
extern const uint8_t gm_skpgi6v1lj8m9[19];
extern const uint8_t gm_skti0sb80pmgp[12];
extern const uint8_t gm_sktk629v7lnuq[5];
extern const uint8_t gm_sktnfpvsdatf3[22];
extern const uint8_t gm_skvak5nkmd757[5];
extern const uint8_t gm_skvko85ql2e6c[62];
extern const uint8_t gm_skvqiv7no78c2[9];
extern const uint8_t gm_sl7pe4skltkbu[9];
extern const uint8_t gm_sl802incsej58[19];
extern const uint8_t gm_sl85di9lce6ej[128];
extern const uint8_t gm_sl9bj93djc5da[38];
extern const uint8_t gm_sl9h8e795pkr4[29];
extern const uint8_t gm_slgrfk23llffe[13];
extern const uint8_t gm_sllaecjankikg[63];
extern const uint8_t gm_slmeqnp27emg7[9];
extern const uint8_t gm_slogre7vmagad[35];
extern const uint8_t gm_slqgr0k308e4c[30];
extern const uint8_t gm_slqtjddhdsdtj[17];
extern const uint8_t gm_slv0rdrsocvrt[111];
extern const uint8_t gm_sm0gqinn3n1e9[15];
extern const uint8_t gm_sm294gmh9789j[39];
extern const uint8_t gm_sm2iv87joi925[9];
extern const uint8_t gm_sm8koj69v1cfv[5];
extern const uint8_t gm_sm9ij4qld7msj[25];
extern const uint8_t gm_smg3p90in08sb[10];
extern const uint8_t gm_smgioupbmjajr[14];
extern const uint8_t gm_smikni2v4ab3d[4];
extern const uint8_t gm_smk9p6gbbjn5j[9];
extern const uint8_t gm_smn3oh253jlna[10];
extern const uint8_t gm_smnqha6lvgiqp[41];
extern const uint8_t gm_smovtms5ssgir[88];
extern const uint8_t gm_smpg2ulm74cim[9];
extern const uint8_t gm_smt0v9p72f507[42];
extern const uint8_t gm_sn048vvii9ap5[83];
extern const uint8_t gm_sn1r7qad8j2p2[5];
extern const uint8_t gm_sn1sp4f9hibg6[10];
extern const uint8_t gm_sn6kdlfrvokdu[7];
extern const uint8_t gm_sn9h18hshc2p3[60];
extern const uint8_t gm_snbihmvq8hfip[29];
extern const uint8_t gm_snch62us408pn[22];
extern const uint8_t gm_sne1lgqg4j81f[27];
extern const uint8_t gm_sneng1d1gdc17[16];
extern const uint8_t gm_sniol5ip67k9b[15];
extern const uint8_t gm_snj8kbefit27t[14];
extern const uint8_t gm_sno5a4e997kfa[41];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_snqkf6som9i0n[16];
extern const uint8_t gm_snqqr4590docj[17];
extern const uint8_t gm_snv0i92n56sv0[11];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so9fl7jecsmrb[27];
extern const uint8_t gm_sobma8mhfoeul[6];
extern const uint8_t gm_sogev0d3re2th[11];
extern const uint8_t gm_soh8b8t1v3pmj[56];
extern const uint8_t gm_soim4k70n6bc7[3];
extern const uint8_t gm_sojpbm4f7rq5f[29];
extern const uint8_t gm_son8q7pl3pv8g[72];
extern const uint8_t gm_sonl5k4jilrbc[13];
extern const uint8_t gm_sono5n5lrjd7h[10];
extern const uint8_t gm_soo0vjpt9f1du[41];
extern const uint8_t gm_sos62dipmhb8k[6];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_sp1f33a1vtc66[11];
extern const uint8_t gm_sp20qfi70dn0k[33];
extern const uint8_t gm_sp6ona76fn0di[13];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_spg4ktuno2hf7[51];
extern const uint8_t gm_spi406027g4jo[19];
extern const uint8_t gm_spjtf1vahteom[41];
extern const uint8_t gm_spmhk8jcpdrb0[41];
extern const uint8_t gm_spnjhvas2rum4[15];
extern const uint8_t gm_spp0f644is6jb[12];
extern const uint8_t gm_sprj2pveuq1p8[13];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_sq1d6esradpbv[21];
extern const uint8_t gm_sq8qkmb2jr755[7];
extern const uint8_t gm_sqa5gdfklkjii[39];
extern const uint8_t gm_sqadouunp729f[67];
extern const uint8_t gm_sqb33oobvt73q[11];
extern const uint8_t gm_sqis5ba78j7cv[42];
extern const uint8_t gm_sqtouugmg03b1[5];
extern const uint8_t gm_sr249jcbafdbm[8];
extern const uint8_t gm_sr4utq83odb88[8];
extern const uint8_t gm_sr7tnhofue6s5[42];
extern const uint8_t gm_sr8jfj391gf5o[3];
extern const uint8_t gm_sr8m5cvuum3fb[5];
extern const uint8_t gm_sr9lljg4m84am[10];
extern const uint8_t gm_srbaplcuib5iq[89];
extern const uint8_t gm_srbber3ovh7gi[9];
extern const char gm_src10tru552hn49[];
extern const char gm_src6d57grm686e3[];
extern const char gm_src7hlf32pj8vid[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srcij34jbkb4qlc[];
extern const char gm_srcn0mgmph5u0oo[];
extern const char gm_srcnhb827gevj6u[];
extern const char gm_srcp2qdr7uqsmui[];
extern const char gm_srct4c97p4hek1p[];
extern const uint8_t gm_srj33esferjmk[41];
extern const uint8_t gm_srjnkb77s24ld[7];
extern const uint8_t gm_srlt17ne6o897[46];
extern const uint8_t gm_srnn7edb2gm48[42];
extern const uint8_t gm_srol27dl4nd2t[41];
extern const uint8_t gm_sromjese8e416[14];
extern const uint8_t gm_srorgjlg51brd[26];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_srsa6dr3d9ut4[41];
extern const uint8_t gm_srucsf9466hv7[5];
extern const uint8_t gm_ss0fg79bi9l5f[6];
extern const uint8_t gm_ss1enqh0qsqn8[14];
extern const uint8_t gm_ss52pcn7gi1ca[25];
extern const uint8_t gm_ss8c96h4bt3ai[8];
extern const uint8_t gm_ss9rpos7stctm[34];
extern const uint8_t gm_ssce05hnjd595[32];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_ssgjh7i7oqvi4[24];
extern const uint8_t gm_ssou2pjir6lie[9];
extern const uint8_t gm_sspmmsm16lqb6[29];
extern const uint8_t gm_ssunro5c9jura[14];
extern const uint8_t gm_ssvnvjq0tn1qr[70];
extern const uint8_t gm_st1taj2v8ug6r[13];
extern const uint8_t gm_stcdo02knddjn[7];
extern const uint8_t gm_std40ghjh6qib[13];
extern const uint8_t gm_stehcnejvoo24[6];
extern const uint8_t gm_stft4dc50tdce[8];
extern const uint8_t gm_stk9qd6rl9jdj[21];
extern const uint8_t gm_su2m3cb0nts7p[3];
extern const uint8_t gm_su625mqe35g86[49];
extern const uint8_t gm_su6414ei8c1ir[10];
extern const uint8_t gm_su854nb6gvg03[7];
extern const uint8_t gm_su9718bmbci33[4];
extern const uint8_t gm_su99evi25asg4[6];
extern const uint8_t gm_sueadt05fevtm[18];
extern const uint8_t gm_sugavfshit856[10];
extern const uint8_t gm_sujn7dir07k4o[41];
extern const uint8_t gm_sujpen8u5kmon[10];
extern const uint8_t gm_sumso4uptitm5[10];
extern const uint8_t gm_sumv38t63phct[14];
extern const uint8_t gm_suqogp9sflci8[12];
extern const uint8_t gm_suuu1avbapfid[21];
extern const uint8_t gm_sv12lv80frbvd[29];
extern const uint8_t gm_sv1f0l9ks3lvu[25];
extern const uint8_t gm_sv3olmne7h13s[4];
extern const uint8_t gm_svdospumf3o67[14];
extern const uint8_t gm_sves946fckobv[104];
extern const uint8_t gm_svghqphscbkj1[7];
extern const uint8_t gm_svgk3n4sk1uat[9];
extern const uint8_t gm_svhflm4d80ub1[62];
extern const uint8_t gm_svj5vmviieugq[9];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const uint8_t gm_svlfsqdbbpmt2[52];
extern const uint8_t gm_svli4h2db9erk[7];
extern const uint8_t gm_svov1cn84ftlm[66];
extern const uint8_t gm_svst1vafj6uk2[13];
extern const uint8_t gm_svt17hrer9pjd[7];
extern const gm_field gm_ty0lfe1s2pin_f[];
extern const gm_type gm_ty1ak5vhpqj4;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_9nsvqlg(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_9nsvqlg F;
    gm_tpe1hv3fekn05 gm_l0; /* <item> */
    gm_tpe1hv3fekn05 gm_l1; /* kv */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tpe1hv3fekn05 t3 = GM_LD(gm_tpe1hv3fekn05, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_watched_files_changed_qvnrc63 *)((&F.h)->up))->l0));
        void * t6 = (void *)(((uint8_t *)t5) + 120);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t8 = gm_f_put_5c6qgui(t6, t7, ((uint8_t)1));
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 520, 44);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 520, 9);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_s23qjlr(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_s23qjlr F;
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tggvvh3lncvcg t3 = GM_LD(gm_tggvvh3lncvcg, ((uint8_t *)&F.l0));
    F.l1 = t3;
    {
        void * t5 = (void *)(((uint8_t *)&((struct gm_fr_uri_map_mhud6ak *)((&F.h)->up))->l1) + 0);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_tftb1odc739t5 t8 = gm_f_put_7shfu9u(t5, t6, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcfpg6jsb3cfp6, 555, 36);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 555, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
const char gm_src10tru552hn49[] = "compiler/lint/syntax.gm";
const char gm_src6d57grm686e3[] = "compiler/check/template.gm";
const char gm_src7hlf32pj8vid[] = "compiler/cgen/constarg.gm";
const char gm_srcfpg6jsb3cfp6[] = "compiler/lsp/server.gm";
const char gm_srcij34jbkb4qlc[] = "compiler/load/semver.gm";
const char gm_srcn0mgmph5u0oo[] = "compiler/check/handlers.gm";
const char gm_srcnhb827gevj6u[] = "compiler/check/codegen.gm";
const char gm_srcp2qdr7uqsmui[] = "compiler/types/display.gm";
const char gm_srct4c97p4hek1p[] = "compiler/syntax/literal.gm";
const gm_field gm_ty0lfe1s2pin_f[] = {{"outputs", 0, &gm_ty467d5cm1d5}, {"inputs", 4, &gm_ty467d5cm1d5}};
const gm_type gm_ty1ak5vhpqj4 = {.size = 16, .align = 16, .kind = GM_K_INT, .bits = 128, .is_signed = false};
const gm_type gm_ty1ak5vhpqj4;
const uint8_t gm_s00hinobqu5k5[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000%\002\000\000\021\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s00li8b55l40s[17] = "\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s014h3o6j2aj2[21] = "\012[[package]]\012name = ";
const uint8_t gm_s01kpv06m7g4n[66] = "with `else e =>`, the condition may fail only on an error (\302\2478.2)";
const uint8_t gm_s02419ud122hi[8] = "checked";
const uint8_t gm_s026u6q948evn[5] = "None";
const uint8_t gm_s06925hae81ss[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000<\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s06m3met1mb9v[3] = "`\012";
const uint8_t gm_s08mr7037nglf[5] = "not ";
const uint8_t gm_s0c2823k4ul3o[17] = "bad archive path";
const uint8_t gm_s0d1hchv6cdbo[11] = "`mode` in ";
const uint8_t gm_s0fibs3hmu8k5[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000R\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s0iditmthmvpu[21] = "unknown constraint `";
const uint8_t gm_s0j2j9kdc2obh[17] = " has no branch `";
const uint8_t gm_s0jrrhhm4skoj[15] = "    return !a[";
const uint8_t gm_s0k8snlvukl86[66] = "; its elements in the order of the variants are the slice `a[..]`";
const uint8_t gm_s0kvnv9teor3q[4] = "Ctz";
const uint8_t gm_s0m07bui94rjq[8] = "gm_fret";
const uint8_t gm_s0m6lnv6m2d3p[28] = "compiler/linting/linting.gm";
const uint8_t gm_s0nfu0i7mcaca[18] = "gm_wasi_rewinding";
const uint8_t gm_s0vltqrmpakvk[17] = "package checkout";
const uint8_t gm_s1387c681koge[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\223\001\000\000S\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s1757p1g7h4me[42] = "{} must be parenthesized in this position";
const uint8_t gm_s17cjlkd2o01f[29] = "#define GM_FREESTANDING 1\012{}";
const uint8_t gm_s1b629p19p3qq[4] = " ::";
const uint8_t gm_s1ctgkvj02ag0[34] = "cannot slice a value of type `{}`";
const uint8_t gm_s1fc8hs0lmr3u[9] = "gm_flush";
const uint8_t gm_s1fcd4t8vd2g5[30] = "the {} \"{}\" is declared twice";
const uint8_t gm_s1pvh8pfrs3ir[7] = "a name";
const uint8_t gm_s20qcu4sv6mcn[19] = "-Wl,-z,stack-size=";
const uint8_t gm_s221m8qkvkf71[23] = "` has no declaration `";
const uint8_t gm_s22cf6j01irhc[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\237\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s24mnh1a25k0f[31] = "this call may move the items `";
const uint8_t gm_s26crtotu14pn[3] = "_t";
const uint8_t gm_s26emr7fhbmjr[22] = " besides the receiver";
const uint8_t gm_s2aj4fqmgmn1n[67] = "` takes `bool` values; on the lanes of a mask use `&`, `|` and `~`";
const uint8_t gm_s2aj89r6sdims[28] = " = gm_fiber_cont(inst->fw, ";
const uint8_t gm_s2e0u34gjtsqn[11] = "` failed: ";
const uint8_t gm_s2gnpj7k4l22n[2] = "c";
const uint8_t gm_s2haaj13v41fd[25] = "too many values for `{}`";
const uint8_t gm_s2ht50jletjjv[23] = "documentSymbolProvider";
const uint8_t gm_s2iprgandb78l[8] = "pat-err";
const uint8_t gm_s2kc17j0uoe9m[23] = "cannot read CFLAGS: {}";
const uint8_t gm_s2l6bc41uiup9[10] = "gm_poison";
const uint8_t gm_s2mtoab140jp6[4] = "max";
const uint8_t gm_s2nh23o3oe191[19] = "File name too long";
const uint8_t gm_s2r03t2n892pm[27] = "{}((void *){}, {}, NULL{})";
const uint8_t gm_s310nl8k9dnl2[59] = "`main` cannot be exported: it is the program's entry point";
const uint8_t gm_s31ag3l5et116[14] = " r = GM_ZERO(";
const uint8_t gm_s32428dkdndn7[141] = "an array length computed from a type parameter is not supported in a type; use a slice, or make the length a compile-time parameter (\302\24716.5)";
const uint8_t gm_s34o1ojfv2p3d[17] = ", NULL, cx, NULL";
const uint8_t gm_s36ugqba56le0[18] = "repository mirror";
const uint8_t gm_s3brj349ts8fs[9] = "wildcard";
const uint8_t gm_s3hg5go4uihat[52] = "` is a method of `Instant` or `Duration`: write `x.";
const uint8_t gm_s3i94v2v4ifm3[17] = "textDocumentSync";
const uint8_t gm_s3jpgh4kdh4qh[15] = "gm_clz({}, {})";
const uint8_t gm_s3k1sno8a2ipv[70] = "`{}` is already declared in this function; locals may not be shadowed";
const uint8_t gm_s3kfdt7s6c0hi[15] = " > (uintptr_t)";
const uint8_t gm_s3o5gaeteeggu[5] = "Make";
const uint8_t gm_s3oap7aihaveg[11] = ", b + i * ";
const uint8_t gm_s3rnim0uidvn4[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000'\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s3rph20cdvn0u[16] = "\012gm_fprop:\012    ";
const uint8_t gm_s3vfg72f8aupk[20] = "error: cannot read ";
const uint8_t gm_s41h7dv6tes4i[57] = "` is permitted only in the body of a `ctl` clause (\302\24714)";
const uint8_t gm_s4231ufcv20f0[12] = "{} + i * {}";
const uint8_t gm_s42h828lb45uf[19] = "__atomic_fetch_and";
const uint8_t gm_s43cq4krigs2b[10] = "RangeIncl";
const uint8_t gm_s4672be3dhb96[11] = "gm_xa{}_{}";
const uint8_t gm_s4bovq0eevpad[23] = "cannot convert from {}";
const uint8_t gm_s4bqq8347n0tp[5] = "\220\001\000\000";
const uint8_t gm_s4dgh055v8im1[43] = "expected a continuation `cont(R)`, found `";
const uint8_t gm_s4du6i4h6s7qo[32] = "{} cannot be compared with `is`";
const uint8_t gm_s4dun9lv4h66e[4] = "(&r";
const uint8_t gm_s4j1eg5g5a64h[5] = "rev=";
const uint8_t gm_s4m37qfrs0qib[9] = "--locked";
const uint8_t gm_s4qg9pu9r16jq[7] = "(void)";
const uint8_t gm_s4t1alq7juu7a[67] = "`--profile={}` needs a package, but no gamma.toml was found for {}";
const uint8_t gm_s4v8j7s5sr5c1[66] = ") is not in the cache, and --offline was given; run `gamma fetch`";
const uint8_t gm_s53ccf6bilp0b[4] = "i16";
const uint8_t gm_s54d3qh188ekk[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\003\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_s551g0bkhdser[6] = "to_ms";
const uint8_t gm_s5ejk09ngvect[3] = "CC";
const uint8_t gm_s5gn6cmh4thug[38] = "` is too large: an object is at most ";
const uint8_t gm_s5j2jdmako4im[37] = "`is` is not defined for `{}` (\302\2477.6)";
const uint8_t gm_s5lrfo6g7933d[50] = "comparison operators cannot be chained; use `and`";
const uint8_t gm_s5m5egkp3rutu[118] = "this performs `{}` inside a `with` that handles `{}`, which might be the same effect for some type arguments (\302\24716.6)";
const uint8_t gm_s5pgobtluqp1f[29] = "the URL `{}` starts with `-`";
const uint8_t gm_s5qoh8o6sn63e[24] = " = gm_resume(inst->fw, ";
const uint8_t gm_s5u1jd4mc0941[32] = "struct __attribute__((packed)) ";
const uint8_t gm_s5ujsjbtr77bt[14] = "    Fetching ";
const uint8_t gm_s5v9d9i0gsl8c[8] = "{}/{}{}";
const uint8_t gm_s5v9muv1jh89m[75] = "a handler is declared for `^{}`: install it from a variable with `with &x`";
const uint8_t gm_s65ir5du2gd8b[9] = "{}.p{}.o";
const uint8_t gm_s6edba3m7mc9a[6] = "Right";
const uint8_t gm_s6fmmm148r6o3[12] = "command_run";
const uint8_t gm_s6ioufa56mnlh[5] = "v + ";
const uint8_t gm_s6jncssts4sq8[40] = "division by zero in constant expression";
const uint8_t gm_s6juej7e0fhbb[8] = "mingw32";
const uint8_t gm_s6m48vdr7h6mr[77] = "invalid {} name `{}`: use letters, digits and `_`, not starting with a digit";
const uint8_t gm_s6mimndcv0cge[4] = "`; ";
const uint8_t gm_s6o30d8d984s9[8] = ", alt: ";
const uint8_t gm_s6qe25ct4l9uk[34] = "(GM_LD(uintptr_t, {} + {}) == {})";
const uint8_t gm_s6rbdi4l8b3ii[18] = "const uint8_t *ev";
const uint8_t gm_s6tc15kuvnm0c[26] = "attributes on expressions";
const uint8_t gm_s73vhpsrl3o41[31] = "cannot slice a value of type `";
const uint8_t gm_s78lhtg84qh95[9] = "named = ";
const uint8_t gm_s7bpkc0l992tl[17] = "<core>/target.gm";
const uint8_t gm_s7c48l6el9tbd[12] = "), (double)";
const uint8_t gm_s7c8ep4trp7bk[7] = "` of `";
const uint8_t gm_s7ekc2mcasshc[10] = ", column ";
const uint8_t gm_s7hc5dru68qbt[15] = "->frame = &F; ";
const uint8_t gm_s7j04h96r4716[18] = "pat-range {} {}{}";
const uint8_t gm_s7j2u3ofu66j8[5] = "kind";
const uint8_t gm_s7kt2bei71pgo[4] = "Ord";
const uint8_t gm_s7l9fi1pqlcp5[4] = "fmt";
const uint8_t gm_s7mc3uqhc5a41[14] = "registrations";
const uint8_t gm_s7mie2m30h1s9[28] = "` is not an operator; use `";
const uint8_t gm_s7n79dja87bqg[28] = "(unsigned __int128)({})({})";
const uint8_t gm_s7ntlu0vonte9[12] = "tuple-field";
const uint8_t gm_s7o8vapurd6a7[12] = "    return ";
const uint8_t gm_s7s5thcg1q6e2[26] = "(uint8_t)__builtin_isnan(";
const uint8_t gm_s7ulscihp75vl[73] = "cannot rename `{}`: it is declared in a dependency, outside this package";
const uint8_t gm_s8075t766i1f0[50] = "this handler has no clause for the operation `{}`";
const uint8_t gm_s816ceel7tvfh[18] = "`-o` needs a path";
const uint8_t gm_s898uefj5c9k6[28] = "the archive of dependency `";
const uint8_t gm_s8a33u6s5sjkv[20] = "static int gm_entry";
const uint8_t gm_s8bhtqu29n7b4[39] = " given (in an instantiation used here)";
const uint8_t gm_s8c4bejqsu1da[48] = "` must be a relative path inside the repository";
const uint8_t gm_s8do3iqjvck3j[45] = "`{}` needs exactly {} elements, but {} given";
const uint8_t gm_s8gf7bnv27fi9[7] = "{}: {}";
const uint8_t gm_s8lkplgn6m7u9[8] = "gm_buf ";
const uint8_t gm_s8n4rd07albkt[16] = "a target triple";
const uint8_t gm_s8pa6bhk4as9k[2] = "\004";
const uint8_t gm_s8pfpkocrh9h6[4] = "w{}";
const uint8_t gm_s8pjuqfuormq3[51] = "the core library does not declare `SourceLocation`";
const uint8_t gm_s8q1hgp14vfur[7] = "gm_rop";
const uint8_t gm_s8s03uo3h8dsp[9] = "{} {}{};";
const uint8_t gm_s8s99m7843kb6[10] = "is_sorted";
const uint8_t gm_s8uvhki57nqp6[30] = "the server is not initialized";
const uint8_t gm_s91c3em0lmpov[37] = "`gamma lint --rules` lists the rules";
const uint8_t gm_s91vsko3h6v6r[10] = "slicing `";
const uint8_t gm_s97bho1civrt3[16] = "intrinsic `@{}`";
const uint8_t gm_s985n04e0mov8[16] = "->inst = inst; ";
const uint8_t gm_s989kd388iles[5] = ")]);";
const uint8_t gm_s993c5b6eq922[54] = "this `match` never completes: each of its arms leaves";
const uint8_t gm_s99ll9oc7rh5u[15] = "` called with ";
const uint8_t gm_s9ap8gk0oj12e[73] = "the clause for `{}` takes {} parameters (the first is the handler value)";
const uint8_t gm_s9b81q4kouna6[11] = " < 0 || ((";
const uint8_t gm_s9cg9729snds3[14] = "` takes two `";
const uint8_t gm_s9hqndl9se5nc[31] = "empty identifier segment in {}";
const uint8_t gm_s9j0jokaa6cqi[113] = "` is a constant, but a bare name in a pattern always binds a new name; compare with a guard instead (`x if x is ";
const uint8_t gm_s9o2cv7g71nbc[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\0006\001\000\000'\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s9pknf9g05okt[3] = "--";
const uint8_t gm_s9ram1a46hdut[9] = "\013\000\000\000\000\000\000\000";
const uint8_t gm_s9s0gbnmtpvjl[4] = "<<|";
const uint8_t gm_s9scq8a60n125[74] = "`.to(F)` converts numeric lanes to `f32` or `f64` lanes, not `{}` to `{}`";
const uint8_t gm_s9uc7or8diipg[35] = "`@prefetch` takes a pointer, not `";
const uint8_t gm_s9um3j04r7jaj[7] = "</ul>\012";
const uint8_t gm_sa01gg636lapd[100] = "a name in a `match` pattern that matches only a present optional, leaving `null` to a catch-all arm";
const uint8_t gm_sa0amdcmvej1l[14] = "foreign block";
const uint8_t gm_sa0f63dcuo6q9[57] = "`{}` points into the items of `{}`, which may have moved";
const uint8_t gm_sa64nb2j51s60[19] = "cannot list {}: {}";
const uint8_t gm_sa91t1vl6p044[34] = "`_` can only be assigned with `=`";
const uint8_t gm_sa954a25u62up[46] = "fmt: buffer too small for the formatted float";
const uint8_t gm_sabhgcltm0thi[8] = "; i++) ";
const uint8_t gm_sabq7k0io9ql3[66] = "`@variants` needs a concrete union type, not the type parameter `";
const uint8_t gm_sachcj4qd96of[48] = "); gamma.lock was edited or the cached archive ";
const uint8_t gm_sacs352aeakr8[18] = "a dead allocation";
const uint8_t gm_saean4ms0g52k[8] = "size_of";
const uint8_t gm_safll42cn0mu7[31] = "expected a value, found a type";
const uint8_t gm_sagtv6bs2sviu[111] = "a handler of `{}` that applies to `{}` is already declared; a type may match only one declaration of an effect";
const uint8_t gm_sah2hl0iapccb[8] = "Effects";
const uint8_t gm_saj4ijvbmtohg[9] = "<stdin>:";
const uint8_t gm_sajfarek3i4jp[44] = "`dependencies` in gamma.toml is not a table";
const uint8_t gm_sal83f6sn6m3n[78] = "this `break` gives a `with` a `{}`, but the `with` has the type `{}` (\302\24712.7)";
const uint8_t gm_saqv3n8kbtahh[14] = "gm_net_listen";
const uint8_t gm_sas0hkqll4io0[10] = "[profile.";
const uint8_t gm_sat7ifv94mb2p[40] = "dependency cycle between declarations: ";
const uint8_t gm_savr1gmccp3v7[20] = "not a symbolic link";
const uint8_t gm_sb2nno7n67rvd[4] = "u64";
const uint8_t gm_sb54ui2amic32[2] = "\"";
const uint8_t gm_sb6j3egm9oljk[5] = "argn";
const uint8_t gm_sb7e7uf3pvdko[26] = "compiler/lsp/workspace.gm";
const uint8_t gm_sbeoq5iu4cto8[11] = "named = {}";
const uint8_t gm_sbfpg6hac36v9[19] = "`{}` is not a type";
const uint8_t gm_sbj6flp3i82cp[32] = "rename it, for example to `{}_`";
const uint8_t gm_sbkdsdb6p9kk7[4] = "'s ";
const uint8_t gm_sbp9encpv0e61[75] = "` has a length that is a compile-time parameter; construct it with `fill:`";
const uint8_t gm_sbqq5iijjahf9[5] = "Auto";
const uint8_t gm_sbqv06vnui8sl[88] = "`#align` goes directly before a `struct` or `union` literal, or on a `#global` (\302\24720.2)";
const uint8_t gm_sbvet41ukod2k[16] = "` mentions the ";
const uint8_t gm_sbvsnnbg6vc88[23] = "     Created package `";
const uint8_t gm_sbvutvpa8pe3d[3] = "()";
const uint8_t gm_sc8o4oaph9gbr[2] = ",";
const uint8_t gm_sc96am1bplmoh[80] = "((__int128)({}) >= {} && ((__int128)({}) < 0 || (unsigned __int128)({}) <= {}))";
const uint8_t gm_sc9u21orrlttr[7] = "--deny";
const uint8_t gm_sccjki3qgi8m7[15] = "+-*/%<>&|~=:.\\";
const uint8_t gm_scd8m51mtmjv9[19] = "identical-branches";
const uint8_t gm_scfl995gsjrop[58] = "the fields of a `#bits` struct have no address (\302\2475.10.5)";
const uint8_t gm_sck9nihohhsb7[15] = "{}.{}.tmp-{}.o";
const uint8_t gm_scmob1k27904r[25] = "to begin the struct body";
const uint8_t gm_scnb9tl13g4lt[8] = "Module ";
const uint8_t gm_sd1ldutuv1qfu[12] = "size_t argn";
const uint8_t gm_sd5imeip8klsb[40] = "`.{}` needs {}, and `{}` has `{}` lanes";
const uint8_t gm_sd70ob31745fn[4] = "-{}";
const uint8_t gm_sd7ar5jm4rru0[48] = "this pattern is for `{}`, but the value is `{}`";
const uint8_t gm_sdahvc1997d01[14] = "GAMMA_C_UNITS";
const uint8_t gm_sdbvaa960ee0f[5] = " -> ";
const uint8_t gm_sdd9sds4mgn8p[6] = "fence";
const uint8_t gm_sdgqla1nhnp8n[34] = "out of memory allocating {} bytes";
const uint8_t gm_sdh1dnevn1j10[8] = "LaneAbs";
const uint8_t gm_sdle4vcf3dhti[3] = "rp";
const uint8_t gm_sdm57kru4ksf7[7] = ", v + ";
const uint8_t gm_sdo8qcbf74mp0[7] = " given";
const uint8_t gm_sdo9t6m49dsco[5] = ") { ";
const uint8_t gm_sdrg69bmlm0tm[30] = "this constant at compile time";
const uint8_t gm_sds03accef1j8[6] = "IsNan";
const uint8_t gm_sdskn4nn8f7q0[5] = "{} \077";
const uint8_t gm_sdtlgdl6ed52a[14] = "proc_read_out";
const uint8_t gm_sdupocj1qjnur[7] = ")) >> ";
const uint8_t gm_se1krm9tn2oaa[3] = ") ";
const uint8_t gm_se3b4jalim8at[6] = " tag;";
const uint8_t gm_seb1p7qlbq9p0[39] = "gm_mix_float(h, (double)GM_LD({}, {}))";
const uint8_t gm_seglpsc899j08[11] = "the type `";
const uint8_t gm_seh91qmsr2tfr[8] = "Mul({})";
const uint8_t gm_sejinh96g1530[26] = "compiler/lsp/transport.gm";
const uint8_t gm_sejjhtlr11ojd[60] = "a shift amount must have an unsigned integer type, not `{}`";
const uint8_t gm_sekdm5lqg7176[43] = ": source file is not valid UTF-8 (at byte ";
const uint8_t gm_sel0tu7gvv1na[4] = "LL)";
const uint8_t gm_sel1fj24qk32p[15] = "    return h;\012";
const uint8_t gm_seno3sej6pod2[34] = "an output needs a `^T` pointer to";
const uint8_t gm_seo2gq6ouvlf6[59] = "`add` needs `--path <dir>`, `--git <url>` or `--url <url>`";
const uint8_t gm_seplj9699cert[10] = "not found";
const uint8_t gm_setcl7798ov89[4] = "Opt";
const uint8_t gm_setnqjs5cth99[45] = "expected an integer literal or constant here";
const uint8_t gm_setofg57no9qt[34] = "element {} of `{}` is given twice";
const uint8_t gm_sevf8q26aov70[6] = "  at ";
const uint8_t gm_sevm6b5otfta5[21] = "duplicate field `{}`";
const uint8_t gm_sf1qb0mllbs1p[34] = "the template has {}, but {} given";
const uint8_t gm_sf1v2j94ne72t[16] = "the operation `";
const uint8_t gm_sf5t1pig3221t[17] = "\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sf9hl2ppv6mim[8] = "--quiet";
const uint8_t gm_sfag3nu74qn7r[31] = "`dependencies` must be a table";
const uint8_t gm_sfb033os1o8m5[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\363\000\000\000K\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sfne4e7hfo6ag[51] = "; `v[..]` is a slice of its elements, which has `.";
const uint8_t gm_sfq3o7cad07rv[14] = "gm_mix64(h, (";
const uint8_t gm_sfqmvjqk1sdmt[6] = "cont(";
const uint8_t gm_sft2smqlktfvo[19] = "(uint8_t)({} != 0)";
const uint8_t gm_sg1c4njjf4gg0[12] = "gm_io_error";
const uint8_t gm_sg1h22ve4uqmq[4] = "dot";
const uint8_t gm_sg8sp4mbs6q31[34] = "Too many levels of symbolic links";
const uint8_t gm_sgc0gatbmtea3[13] = "check_source";
const uint8_t gm_sge9itd5fie17[21] = "lib/std/json/json.gm";
const uint8_t gm_sgemqs85b1iol[9] = "pow_wrap";
const uint8_t gm_sgjci31f2rs96[49] = "the core library's range types are not available";
const uint8_t gm_sgk14ir7dv1gf[6] = ":exit";
const uint8_t gm_sgmqtrfjl8vp3[52] = "`{}` is an effect set, not a single effect (\302\24716.6)";
const uint8_t gm_sgnvdmoebn4i7[16] = "calls of fields";
const uint8_t gm_sgpr1l6rnrmel[20] = "trailing characters";
const uint8_t gm_sgsm5p4jqmf3k[43] = "    gm_flush();\012    return (uint8_t)GM_LD(";
const uint8_t gm_sh540objcvln0[25] = ": c-sources of package `";
const uint8_t gm_sh630b08j7vfk[84] = "this performs the effect `{}`, which the function does not declare (add `needs {}`)";
const uint8_t gm_sh74p5jjd1e00[4] = "0{}";
const uint8_t gm_shbtdicfd8lcc[12] = "cannot run ";
const uint8_t gm_shem79vgdue54[5] = ")(((";
const uint8_t gm_sheobcjeiocck[16] = ", 0xAA, sizeof ";
const uint8_t gm_shi7nidbc64qf[4] = " *)";
const uint8_t gm_shlvd8v9thcc8[9] = "\000\020\000\000\000\000\000\000";
const uint8_t gm_shmsiqao0jnvn[42] = "dependency `{}`: {} has no directory `{}`";
const uint8_t gm_sho8l3vae133f[8] = ":cancel";
const uint8_t gm_shpr5sdqmule5[9] = "unsigned";
const uint8_t gm_shqja2jbdo2o0[58] = "a handler parameter needs a type: `T: type` or `N: usize`";
const uint8_t gm_sht1158338t3d[11] = "(gm_spec){";
const uint8_t gm_shv3ttlq9l359[15] = "static inline ";
const uint8_t gm_si1j2hanh22ll[16] = "activeSignature";
const uint8_t gm_si62ejtp0vbct[3] = "<<";
const uint8_t gm_si7fe9i76cdgb[13] = "struct gm_dv";
const uint8_t gm_si82ti7ep94be[11] = "/bin/clang";
const uint8_t gm_siapf4ssb1jv4[5] = "zero";
const uint8_t gm_siho9h2ssfvv9[62] = "the continuation may be left unconsumed on this path (\302\24714.2)";
const uint8_t gm_sio5n0k35hfb4[65] = "wildcard req ({}) must be the only comparator in the version req";
const uint8_t gm_sivh46n0690a4[4] = "new";
const uint8_t gm_sj2a9rjbqmk4h[8] = "cannot ";
const uint8_t gm_sj3fn9etjoihf[116] = "a second-class value (a function literal or handler) may only be bound with `::`, not stored in a variable (\302\24711.6)";
const uint8_t gm_sj5ke3qalt8me[9] = "did_open";
const uint8_t gm_sj696i9uo1a9s[14] = "call indirect";
const uint8_t gm_sjbhp9l7seful[7] = "FsSeek";
const uint8_t gm_sjh4l64ncn038[52] = "gm_mix64(h, (uint64_t)(uintptr_t)GM_LD(void *, {}))";
const uint8_t gm_sjijer52q1op6[7] = "let {}";
const uint8_t gm_sjmccqdb90fnl[3] = "u8";
const uint8_t gm_sjmngec2on365[5] = "SysV";
const uint8_t gm_sjndugdkuh68b[58] = "the type `{}` is too large: an object is at most {} bytes";
const uint8_t gm_sjnkguo0atpm5[3] = "`)";
const uint8_t gm_sjp4ps1gkcjl1[8] = ") - 1))";
const uint8_t gm_sjq55q61p743j[3] = "0.";
const uint8_t gm_sjqjrfqdmp9ao[7] = "ident ";
const uint8_t gm_sjvtpaes95ji4[27] = "(({})GM_PTR_UNTAG({}))({})";
const uint8_t gm_sk09uplg1v4qc[16] = "gm_alloc_check(";
const uint8_t gm_sk2jb5rv3689k[34] = "cannot find the module directory ";
const uint8_t gm_skagf3vj30re5[17] = "uint8_t p{}[{}];";
const uint8_t gm_skau91fmi0eei[42] = "` is not a git dependency of this package";
const uint8_t gm_skf4n3tc7nif9[5] = "</a>";
const uint8_t gm_skgir5knhpsin[9] = "{} {} {}";
const uint8_t gm_skhs0ffupj1to[30] = " & (((unsigned __int128)1 << ";
const uint8_t gm_skiffh4urhl7e[14] = "duplicate key";
const uint8_t gm_skissj10nclhr[6] = "param";
const uint8_t gm_skm1g73974f87[4] = "-|=";
const uint8_t gm_sknpneumdgqfs[38] = "field `{}` is supplied by two spreads";
const uint8_t gm_sko342193gvqe[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000y\002\000\000\014\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_skp2amifebhq6[41] = "`debug-info` in {} must be true or false";
const uint8_t gm_skpgi6v1lj8m9[19] = "[] __asm__(GM_SYM(";
const uint8_t gm_skti0sb80pmgp[12] = "clock_sleep";
const uint8_t gm_sktk629v7lnuq[5] = "Fast";
const uint8_t gm_sktnfpvsdatf3[22] = "textDocument/didClose";
const uint8_t gm_skvak5nkmd757[5] = "keep";
const uint8_t gm_skvko85ql2e6c[62] = "` at an earlier use (\302\2476.1); annotate it, e.g. `n: usize = 0`";
const uint8_t gm_skvqiv7no78c2[9] = "returned";
const uint8_t gm_sl7pe4skltkbu[9] = ", .fw = ";
const uint8_t gm_sl802incsej58[19] = "for this operation";
const uint8_t gm_sl85di9lce6ej[128] = ": not every `{}` is a valid `{}`, since a field has a type with invalid bit patterns; build it with `{}(field: \342\200\246)` (\302\2475.10.5)";
const uint8_t gm_sl9bj93djc5da[38] = "a `{:` placeholder has no closing `}`";
const uint8_t gm_sl9h8e795pkr4[29] = "` is private to the module `";
const uint8_t gm_slgrfk23llffe[13] = "addr-of-temp";
const uint8_t gm_sllaecjankikg[63] = "the literal is too large for `{}` (it would round to infinity)";
const uint8_t gm_slmeqnp27emg7[9] = "gm_node ";
const uint8_t gm_slogre7vmagad[35] = "` is missing from the core library";
const uint8_t gm_slqgr0k308e4c[30] = "unsupported lockfile version ";
const uint8_t gm_slqtjddhdsdtj[17] = "\000\000\000\000\000\000\000\000\356\003\000\000\000\000\000\000";
const uint8_t gm_slv0rdrsocvrt[111] = "a test returns no value, so `try` cannot propagate from it; write `ok(x)` to fail the test on an error (\302\2473.8)";
const uint8_t gm_sm0gqinn3n1e9[15] = "value-param {}";
const uint8_t gm_sm294gmh9789j[39] = "the URL `{}` contains the character {}";
const uint8_t gm_sm2iv87joi925[9] = "index_of";
const uint8_t gm_sm8koj69v1cfv[5] = " = a";
const uint8_t gm_sm9ij4qld7msj[25] = "(GM_LD(void *, {}) != 0)";
const uint8_t gm_smg3p90in08sb[10] = "_optional";
const uint8_t gm_smgioupbmjajr[14] = "FsSetModified";
const uint8_t gm_smikni2v4ab3d[4] = "app";
const uint8_t gm_smk9p6gbbjn5j[9] = " warning";
const uint8_t gm_smn3oh253jlna[10] = "elem = {}";
const uint8_t gm_smnqha6lvgiqp[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000/\001\000\000\013\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000";
const uint8_t gm_smovtms5ssgir[88] = "a continuation can be consumed only by a clause of the `with` that produced it (\302\24714.2)";
const uint8_t gm_smpg2ulm74cim[9] = "));\012    ";
const uint8_t gm_smt0v9p72f507[42] = " is not a WebAssembly module I can read: ";
const uint8_t gm_sn048vvii9ap5[83] = "`{}` is constructed by naming each variant of `{}`: `{}({}: \342\200\246)`, or with `fill:`";
const uint8_t gm_sn1r7qad8j2p2[5] = "ix{}";
const uint8_t gm_sn1sp4f9hibg6[10] = "a pattern";
const uint8_t gm_sn6kdlfrvokdu[7] = "{}.env";
const uint8_t gm_sn9h18hshc2p3[60] = "this condition always succeeds; use a plain binding instead";
const uint8_t gm_snbihmvq8hfip[29] = ")(((unsigned __int128)GM_LD(";
const uint8_t gm_snch62us408pn[22] = "several are available";
const uint8_t gm_sne1lgqg4j81f[27] = "`{}` is a foreign function";
const uint8_t gm_sneng1d1gdc17[16] = "gm_thread_spawn";
const uint8_t gm_sniol5ip67k9b[15] = "the address of";
const uint8_t gm_snj8kbefit27t[14] = "cached builds";
const uint8_t gm_sno5a4e997kfa[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000P\000\000\000\034\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_snoai0j6d9u36[7] = "target";
const uint8_t gm_snomh93h7tumv[14] = "dependencies.";
const uint8_t gm_snqkf6som9i0n[16] = "gm_fd_above_std";
const uint8_t gm_snqqr4590docj[17] = "\000\000\000\000\000\000\000\000\242\005\000\000\000\000\000\000";
const uint8_t gm_snv0i92n56sv0[11] = "parse_with";
const uint8_t gm_so31m6feg820p[4] = "f{}";
const uint8_t gm_so514oj5nbhcu[2] = "\012";
const uint8_t gm_so9fl7jecsmrb[27] = ") gm_trap_shift((uint64_t)";
const uint8_t gm_sobma8mhfoeul[6] = "[] = ";
const uint8_t gm_sogev0d3re2th[11] = "call fn {}";
const uint8_t gm_soh8b8t1v3pmj[56] = "this conversion gives the literal's variable the type `";
const uint8_t gm_soim4k70n6bc7[3] = "ok";
const uint8_t gm_sojpbm4f7rq5f[29] = "__atomic_compare_exchange_n(";
const uint8_t gm_son8q7pl3pv8g[72] = "` contains itself by value and would be infinitely large; use a pointer";
const uint8_t gm_sonl5k4jilrbc[13] = "SpawnOptions";
const uint8_t gm_sono5n5lrjd7h[10] = "gm_node *";
const uint8_t gm_soo0vjpt9f1du[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000}\000\000\0004\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sos62dipmhb8k[6] = "-xzmf";
const uint8_t gm_southrlg4k72m[6] = " and ";
const uint8_t gm_sp1f33a1vtc66[11] = "gm_scratch";
const uint8_t gm_sp20qfi70dn0k[33] = "` is bound twice in this pattern";
const uint8_t gm_sp6ona76fn0di[13] = "range bounds";
const uint8_t gm_sp7cdd0n91vli[8] = "` and `";
const uint8_t gm_spg4ktuno2hf7[51] = "snake_case, SCREAMING_SNAKE_CASE or UpperCamelCase";
const uint8_t gm_spi406027g4jo[19] = "kind = GM_K_OPAQUE";
const uint8_t gm_spjtf1vahteom[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000M\000\000\0001\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_spmhk8jcpdrb0[41] = "cannot run `curl` (is it installed\077): {}";
const uint8_t gm_spnjhvas2rum4[15] = "expected ident";
const uint8_t gm_spp0f644is6jb[12] = "tag_size = ";
const uint8_t gm_sprj2pveuq1p8[13] = " bytes, not ";
const uint8_t gm_sptnm8c6c52dr[7] = "global";
const uint8_t gm_sq1d6esradpbv[21] = "present-only-binding";
const uint8_t gm_sq8qkmb2jr755[7] = "mips64";
const uint8_t gm_sqa5gdfklkjii[39] = "a float literal cannot have the type `";
const uint8_t gm_sqadouunp729f[67] = "a clause of a `fn` operation can only produce a value; it cannot `";
const uint8_t gm_sqb33oobvt73q[11] = "this `try`";
const uint8_t gm_sqis5ba78j7cv[42] = "attribute `#{}` does not apply to a union";
const uint8_t gm_sqtouugmg03b1[5] = " */\012";
const uint8_t gm_sr249jcbafdbm[8] = "builtin";
const uint8_t gm_sr4utq83odb88[8] = "path = ";
const uint8_t gm_sr7tnhofue6s5[42] = "no banner and prompts, even on a terminal";
const uint8_t gm_sr8jfj391gf5o[3] = "^\012";
const uint8_t gm_sr8m5cvuum3fb[5] = ".cx;";
const uint8_t gm_sr9lljg4m84am[10] = "GAMMA_STD";
const uint8_t gm_srbaplcuib5iq[89] = "gamma: `--package` needs the input to be in a package (a directory with a `gamma.toml`)\012";
const uint8_t gm_srbber3ovh7gi[9] = "\\\000\000\000\000\000\000\000";
const uint8_t gm_srj33esferjmk[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\230\001\000\000\024\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_srjnkb77s24ld[7] = "fb->fn";
const uint8_t gm_srlt17ne6o897[46] = "{} is not a WebAssembly module I can read: {}";
const uint8_t gm_srnn7edb2gm48[42] = "another function is already exported as `";
const uint8_t gm_srol27dl4nd2t[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\216\000\000\000*\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sromjese8e416[14] = "unwrap-result";
const uint8_t gm_srorgjlg51brd[26] = "compiler/check/generic.gm";
const uint8_t gm_srri8c6al1v1g[5] = "with";
const uint8_t gm_srsa6dr3d9ut4[41] = "`{}` does not apply to the lanes of `{}`";
const uint8_t gm_srucsf9466hv7[5] = "eval";
const uint8_t gm_ss0fg79bi9l5f[6] = " + {}";
const uint8_t gm_ss1enqh0qsqn8[14] = "    Updating ";
const uint8_t gm_ss52pcn7gi1ca[25] = "`in` is not defined for ";
const uint8_t gm_ss8c96h4bt3ai[8] = "command";
const uint8_t gm_ss9rpos7stctm[34] = "the archive unpacks to more than ";
const uint8_t gm_ssce05hnjd595[32] = "after the patterns of the `for`";
const uint8_t gm_ssdi9g220if8h[2] = "v";
const uint8_t gm_ssgjh7i7oqvi4[24] = "the foreign declaration";
const uint8_t gm_ssou2pjir6lie[9] = "checksum";
const uint8_t gm_sspmmsm16lqb6[29] = "] != 0; h = gm_mix64(h, e);\012";
const uint8_t gm_ssunro5c9jura[14] = "gm_fs_symlink";
const uint8_t gm_ssvnvjq0tn1qr[70] = "only a `#soa` array variable (a place) can be sliced, not a temporary";
const uint8_t gm_st1taj2v8ug6r[13] = "gm_hb_record";
const uint8_t gm_stcdo02knddjn[7] = "{:.2}s";
const uint8_t gm_std40ghjh6qib[13] = "gm_bt *gm_Bp";
const uint8_t gm_stehcnejvoo24[6] = "FsCwd";
const uint8_t gm_stft4dc50tdce[8] = "Shuffle";
const uint8_t gm_stk9qd6rl9jdj[21] = " byte(s) at address ";
const uint8_t gm_su2m3cb0nts7p[3] = "db";
const uint8_t gm_su625mqe35g86[49] = "a handler literal is second-class and cannot be ";
const uint8_t gm_su6414ei8c1ir[10] = "\"fixes\":[";
const uint8_t gm_su854nb6gvg03[7] = "--time";
const uint8_t gm_su9718bmbci33[4] = ">>=";
const uint8_t gm_su99evi25asg4[6] = ".code";
const uint8_t gm_sueadt05fevtm[18] = " to dependencies\012";
const uint8_t gm_sugavfshit856[10] = "; return ";
const uint8_t gm_sujn7dir07k4o[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000w\002\000\000\012\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sujpen8u5kmon[10] = " >> (int)";
const uint8_t gm_sumso4uptitm5[10] = "build run";
const uint8_t gm_sumv38t63phct[14] = " uint8_t : 8;";
const uint8_t gm_suqogp9sflci8[12] = "(int64_t){}";
const uint8_t gm_suuu1avbapfid[21] = "uninitialized unions";
const uint8_t gm_sv12lv80frbvd[29] = "type `{}` has no column `{}`";
const uint8_t gm_sv1f0l9ks3lvu[25] = "too many arguments: the ";
const uint8_t gm_sv3olmne7h13s[4] = "Sat";
const uint8_t gm_svdospumf3o67[14] = "      Adding ";
const uint8_t gm_sves946fckobv[104] = "the template must be a compile-time string: a literal, a constant, or a `string` compile-time parameter";
const uint8_t gm_svghqphscbkj1[7] = "{}__{}";
const uint8_t gm_svgk3n4sk1uat[9] = ", found ";
const uint8_t gm_svhflm4d80ub1[62] = "the continuation may already have been consumed here (\302\24714.2)";
const uint8_t gm_svj5vmviieugq[9] = "--verify";
const uint8_t gm_svlcdvcv2bp94[24] = "this compile-time slice";
const uint8_t gm_svlfsqdbbpmt2[52] = "`.bits` can only produce plain-data types, not `{}`";
const uint8_t gm_svli4h2db9erk[7] = "gm_g{}";
const uint8_t gm_svov1cn84ftlm[66] = "gm_fret:\012    memcpy(gm_out, &F.ret, sizeof F.ret);\012gm_fdone:\012    ";
const uint8_t gm_svst1vafj6uk2[13] = "gm_path_call";
const uint8_t gm_svt17hrer9pjd[7] = "Unions";
uint8_t gm_c0gtrsff3e8[9] __attribute__((aligned(1)));
uint8_t gm_c0lficu5ug9[4] __attribute__((aligned(1)));
uint8_t gm_c0oemf1frl4[7] __attribute__((aligned(1)));
uint8_t gm_c0tggmkj5d0[1] __attribute__((aligned(1)));
uint8_t gm_c12u1ovcpek[14] __attribute__((aligned(1)));
uint8_t gm_c1q4po7noqk[12] __attribute__((aligned(1)));
uint8_t gm_c1sfg0li8h3[50] __attribute__((aligned(1)));
uint8_t gm_c2tmtf1q8pq[87] __attribute__((aligned(1)));
uint8_t gm_c3kocbd3l5h[4] __attribute__((aligned(1)));
uint8_t gm_c433gi9623u[17] __attribute__((aligned(1)));
uint8_t gm_c78m49i35ia[19] __attribute__((aligned(1)));
uint8_t gm_c7dg59gauug[10] __attribute__((aligned(1)));
uint8_t gm_ca1bh7jkvd3[9] __attribute__((aligned(1)));
uint8_t gm_cb2fg0a9k46[14] __attribute__((aligned(1)));
uint8_t gm_cbbajrmlhcc[3] __attribute__((aligned(1)));
uint8_t gm_cbdf5obmbv8[12] __attribute__((aligned(1)));
uint8_t gm_cbkngm2o96p[5] __attribute__((aligned(1)));
uint8_t gm_cbp17rggt3d[12] __attribute__((aligned(1)));
uint8_t gm_cbru991fg1t[11] __attribute__((aligned(1)));
uint8_t gm_cc37olh9uak[12] __attribute__((aligned(1)));
uint8_t gm_ccb8g03q3co[13] __attribute__((aligned(1)));
uint8_t gm_cdps8qsftpu[4] __attribute__((aligned(4)));
uint8_t gm_ce961isd2oi[10] __attribute__((aligned(1)));
uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
uint8_t gm_cf6v325lv09[14] __attribute__((aligned(1)));
uint8_t gm_cfa74gl0ote[4] __attribute__((aligned(1)));
uint8_t gm_cfas3u4spp8[9] __attribute__((aligned(1)));
uint8_t gm_cfkvti2s7oq[12] __attribute__((aligned(1)));
uint8_t gm_cftil4g103c[5] __attribute__((aligned(1)));
uint8_t gm_cfuaut7oa51[1] __attribute__((aligned(1)));
uint8_t gm_cg5i3k2h7tu[11] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e_4[16] __attribute__((aligned(8)));
uint8_t gm_ch4udh4cc5j[12] __attribute__((aligned(1)));
uint8_t gm_cibj24dglm2[5] __attribute__((aligned(1)));
uint8_t gm_ciuj8hs0q3l[8] __attribute__((aligned(8)));
uint8_t gm_cjts5fiuldu[192] __attribute__((aligned(8)));
uint8_t gm_ck3j0r2lm3c[586] __attribute__((aligned(1)));
uint8_t gm_ck4pksg2re9[4] __attribute__((aligned(1)));
uint8_t gm_cka88d5t8lu_3[16] __attribute__((aligned(8)));
uint8_t gm_cldb10luqdu[13] __attribute__((aligned(1)));
uint8_t gm_cmdrqgqs203[15] __attribute__((aligned(1)));
uint8_t gm_cndda2t88s7[6] __attribute__((aligned(1)));
uint8_t gm_cngietrs56f[1] __attribute__((aligned(1)));
uint8_t gm_cnohofor2q6[16] __attribute__((aligned(8)));
uint8_t gm_cog1l56ulfv[8] __attribute__((aligned(8)));
uint8_t gm_cp5tb1hpgdo[3] __attribute__((aligned(1)));
uint8_t gm_cprueqv60kg[1] __attribute__((aligned(1)));
uint8_t gm_crri3fcfba6[86] __attribute__((aligned(1)));
uint8_t gm_cs5feb7nj6r[3] __attribute__((aligned(1)));
uint8_t gm_csd3p6j3fi7[16] __attribute__((aligned(8)));
uint8_t gm_cssal36m00k[16] __attribute__((aligned(8)));
uint8_t gm_csu7k86qqgd[256] __attribute__((aligned(4)));
uint8_t gm_ct6asj8mekt[34] __attribute__((aligned(1)));
uint8_t gm_ct6pvba5oah[13] __attribute__((aligned(1)));
uint8_t gm_cte1ut67vb8[11] __attribute__((aligned(1)));
uint8_t gm_ctebdbqpdl1[3] __attribute__((aligned(1)));
uint8_t gm_cu04o97732p[7] __attribute__((aligned(1)));
uint8_t gm_cubqnpdse2d[4] __attribute__((aligned(1)));
uint8_t gm_cukabrstocl[432] __attribute__((aligned(8)));
uint8_t gm_cvetthildsc[12] __attribute__((aligned(1)));
uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
uint8_t gm_k1b2sgalr12[40] __attribute__((aligned(8)));
uint8_t gm_k70n01rs29g[40] __attribute__((aligned(8)));
uint8_t gm_kcrp199kueb[40] __attribute__((aligned(8)));
uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
uint8_t gm_kfgeo9idkmp[40] __attribute__((aligned(8)));
uint8_t gm_kfqhpvvf5lv[40] __attribute__((aligned(8)));
uint8_t gm_kha5d1oq7pb[40] __attribute__((aligned(8)));
uint8_t gm_kignd7l31vo[40] __attribute__((aligned(8)));
uint8_t gm_kk6gotfp7pf[40] __attribute__((aligned(8)));
uint8_t gm_klfj89orh68[40] __attribute__((aligned(8)));
uint8_t gm_kli6567itlv[40] __attribute__((aligned(8)));
uint8_t gm_klim7pfh497[40] __attribute__((aligned(8)));
uint8_t gm_km03kohh9a4[40] __attribute__((aligned(8)));
uint8_t gm_kn6hjdk6jie[40] __attribute__((aligned(8)));
uint8_t gm_knasprnpdnf[40] __attribute__((aligned(8)));
uint8_t gm_krd7aj3c16u[40] __attribute__((aligned(8)));
uint8_t gm_krg6ee1n6ld[40] __attribute__((aligned(8)));
uint8_t gm_kvm0nu8rto4[40] __attribute__((aligned(8)));
void *const gm_hl4lv3uiadds[] = {(void *)gm_f_for_body_iv0iced};
void *const gm_hl8iqbv5s08g[] = {(void *)gm_f_for_body_s23qjlr};
void *const gm_hltn0orqptuk[] = {(void *)gm_f_for_body_9nsvqlg};
void *const gm_hluov08dhrrb[] = {(void *)gm_f_for_body_ui39jfe};
