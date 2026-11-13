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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
gm_b0_1 gm_dtvslis4o40u_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_terksaa(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_c0fhj0gi05s[2] __attribute__((aligned(1)));
extern uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
extern uint8_t gm_c2t4q8gsqsl[3] __attribute__((aligned(1)));
extern uint8_t gm_c32pk0vs0kc[5] __attribute__((aligned(1)));
extern uint8_t gm_c3i8biff6gm[11] __attribute__((aligned(1)));
extern uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
extern uint8_t gm_c4f2cgckmo6[7] __attribute__((aligned(1)));
extern uint8_t gm_c50rfu0mliv[4000] __attribute__((aligned(8)));
extern uint8_t gm_c54lbva5qt3[10] __attribute__((aligned(1)));
extern uint8_t gm_c5v8jddvmh5[13] __attribute__((aligned(1)));
extern uint8_t gm_c6iptqebh84[6] __attribute__((aligned(1)));
extern uint8_t gm_c708i8j8tcn[6] __attribute__((aligned(1)));
extern uint8_t gm_c7bojpcss70[13] __attribute__((aligned(1)));
extern uint8_t gm_c8ps00qu8ur[9] __attribute__((aligned(1)));
extern uint8_t gm_c8r6sk2ruhm[7] __attribute__((aligned(1)));
extern uint8_t gm_ca34i8s7nv3[10] __attribute__((aligned(1)));
extern uint8_t gm_cas4sbdf6ub[12] __attribute__((aligned(1)));
extern uint8_t gm_cbe5loot5ou[9] __attribute__((aligned(1)));
extern uint8_t gm_cbvjitbgoki[16] __attribute__((aligned(1)));
extern uint8_t gm_cc8mmf5go3e[4] __attribute__((aligned(1)));
extern uint8_t gm_cch1hm6o00s[13] __attribute__((aligned(1)));
extern uint8_t gm_ccimdvgv2uo[5] __attribute__((aligned(1)));
extern uint8_t gm_cd923jeri8l[13] __attribute__((aligned(1)));
extern uint8_t gm_cdvl4a09e32[18] __attribute__((aligned(1)));
extern uint8_t gm_ceivjfkfaf1[5] __attribute__((aligned(1)));
extern uint8_t gm_cenbs8unhg6[4] __attribute__((aligned(1)));
extern uint8_t gm_ceperl1a3fj[5] __attribute__((aligned(1)));
extern uint8_t gm_cfj1ctfn4pv[16] __attribute__((aligned(8)));
extern uint8_t gm_cgmt38nbmrp[17] __attribute__((aligned(1)));
extern uint8_t gm_ch5ouqda8cs[51] __attribute__((aligned(1)));
extern uint8_t gm_ch70o9r96kg[16] __attribute__((aligned(1)));
extern uint8_t gm_ci404lai93v[10] __attribute__((aligned(1)));
extern uint8_t gm_ci7lmabjiao[2] __attribute__((aligned(1)));
extern uint8_t gm_ci8na0i4d9p[15] __attribute__((aligned(1)));
extern uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
extern uint8_t gm_cislt4mt8ol[12] __attribute__((aligned(1)));
extern uint8_t gm_cj99fe4p60v[9] __attribute__((aligned(1)));
extern uint8_t gm_cjbp1kou1j6[11] __attribute__((aligned(1)));
extern uint8_t gm_ck2s18je8ki[4] __attribute__((aligned(1)));
extern uint8_t gm_clj49qvgrpn[8] __attribute__((aligned(1)));
extern uint8_t gm_cm7u9m3ofdj[10] __attribute__((aligned(1)));
extern uint8_t gm_cm9aodfhip7[5] __attribute__((aligned(1)));
extern uint8_t gm_cn0e8uksoq0[7] __attribute__((aligned(1)));
extern uint8_t gm_cnb3jfedmgn[3] __attribute__((aligned(1)));
extern uint8_t gm_cnfml933dkb[2] __attribute__((aligned(1)));
extern uint8_t gm_cnlt8qov7cb[16] __attribute__((aligned(1)));
extern uint8_t gm_cnue4rlljrd[13] __attribute__((aligned(1)));
extern uint8_t gm_co0v8nc5lqu[13] __attribute__((aligned(1)));
extern uint8_t gm_coqlk8uvhm0[2] __attribute__((aligned(1)));
extern uint8_t gm_cormelbvjcp[9] __attribute__((aligned(1)));
extern uint8_t gm_cp1bmt1lpnn[15] __attribute__((aligned(1)));
extern uint8_t gm_cpirdtbcd2f[4] __attribute__((aligned(1)));
extern uint8_t gm_cq9lk6ce28a[4] __attribute__((aligned(1)));
extern uint8_t gm_cr9tkd1skqs[4] __attribute__((aligned(1)));
extern uint8_t gm_crb4pqq2igg[6] __attribute__((aligned(1)));
extern uint8_t gm_crh3j0ho75u[12] __attribute__((aligned(1)));
extern uint8_t gm_crthrtmpk9i[2] __attribute__((aligned(1)));
extern uint8_t gm_ctapjbcspvf[4] __attribute__((aligned(1)));
extern uint8_t gm_ctd4c52p0f8[60] __attribute__((aligned(1)));
extern uint8_t gm_cthm7vq6g2f[13] __attribute__((aligned(1)));
extern uint8_t gm_ctkboukdglo[8] __attribute__((aligned(8)));
extern uint8_t gm_cugpmqkho2v[16] __attribute__((aligned(1)));
extern void *const gm_hdvslis4o40u[];
extern void *const gm_hl6s5gct9vkh[];
extern uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
extern uint8_t gm_k5u3oseesd7[40] __attribute__((aligned(8)));
extern uint8_t gm_k6r9cr7441u[40] __attribute__((aligned(8)));
extern uint8_t gm_k8b8p636a5m[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o17uvn1g2[40] __attribute__((aligned(8)));
extern uint8_t gm_kau7m62eplk[40] __attribute__((aligned(8)));
extern uint8_t gm_kbamu2ordri[40] __attribute__((aligned(8)));
extern uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
extern uint8_t gm_kebr2u4173v[40] __attribute__((aligned(8)));
extern uint8_t gm_khibdd7i0ml[40] __attribute__((aligned(8)));
extern uint8_t gm_khrljgb4g9j[40] __attribute__((aligned(8)));
extern uint8_t gm_klh5uk0bvu4[40] __attribute__((aligned(8)));
extern uint8_t gm_knj30d2h126[40] __attribute__((aligned(8)));
extern uint8_t gm_kpc4646t5v3[40] __attribute__((aligned(8)));
extern uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00odc15r3q8f[16];
extern const uint8_t gm_s00pihipe0jga[6];
extern const uint8_t gm_s0147vgtvilnq[19];
extern const uint8_t gm_s046jqe2j7k9s[5];
extern const uint8_t gm_s07uh61s060o9[25];
extern const uint8_t gm_s09tkid4cqjmm[62];
extern const uint8_t gm_s0doambt0iqhe[4];
extern const uint8_t gm_s0ek1hibl01t2[47];
extern const uint8_t gm_s0f7epcithn0r[18];
extern const uint8_t gm_s0fg7tcl0p4ij[13];
extern const uint8_t gm_s0h9r2boh81d2[65];
extern const uint8_t gm_s0hfsnjdrghcm[9];
extern const uint8_t gm_s0johk446liuj[28];
extern const uint8_t gm_s0kajv5j53ub7[38];
extern const uint8_t gm_s0l40gug6qpuq[17];
extern const uint8_t gm_s0nad8bs1qlvl[76];
extern const uint8_t gm_s0u9ru4tiqbe7[9];
extern const uint8_t gm_s0unahdn5j99v[14];
extern const uint8_t gm_s12khqa42i680[29];
extern const uint8_t gm_s12scbfvbvl4s[3];
extern const uint8_t gm_s13b13uieof5u[12];
extern const uint8_t gm_s187rb18i1kgk[4];
extern const uint8_t gm_s1cn5ab5opg9f[131];
extern const uint8_t gm_s1crsce68cmnd[20];
extern const uint8_t gm_s1h3hvq26g2dl[47];
extern const uint8_t gm_s1idibqkta392[7];
extern const uint8_t gm_s1lfsmjv8evdo[9];
extern const uint8_t gm_s1n4l05blor5r[40];
extern const uint8_t gm_s1r58jmps9mcc[7];
extern const uint8_t gm_s1tdqlhdsnu5l[14];
extern const uint8_t gm_s1ul47vja8nnc[16];
extern const uint8_t gm_s1vmtsmtbio1j[15];
extern const uint8_t gm_s20t5qopq8n9u[5];
extern const uint8_t gm_s27am6v0m91fv[87];
extern const uint8_t gm_s2ma5cp41jk1a[7];
extern const uint8_t gm_s2r62ofaabkh1[41];
extern const uint8_t gm_s2svafu2na27p[17];
extern const uint8_t gm_s2u0i5o1ej1mg[10];
extern const uint8_t gm_s2ud1phv1fk5e[15];
extern const uint8_t gm_s2uilckebopb3[12];
extern const uint8_t gm_s2v0mmbl3fojp[55];
extern const uint8_t gm_s31vj4bjbj0gj[36];
extern const uint8_t gm_s3hali1afgel3[28];
extern const uint8_t gm_s3hrsavigl7la[19];
extern const uint8_t gm_s3ngn7me40qa4[5];
extern const uint8_t gm_s41sp7ht9l505[67];
extern const uint8_t gm_s42b9o00en8pl[34];
extern const uint8_t gm_s449uaav7f0l0[11];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s467d5cm1d5us[4];
extern const uint8_t gm_s480gcu6od53p[61];
extern const uint8_t gm_s48o8olsbqnph[12];
extern const uint8_t gm_s495moo6tjeh0[20];
extern const uint8_t gm_s4ccl84nrk27k[55];
extern const uint8_t gm_s4cdme6b7eogn[56];
extern const uint8_t gm_s4e3q3n6msi6m[6];
extern const uint8_t gm_s4emvvn1d7f2t[6];
extern const uint8_t gm_s4hv5fsmnuq3r[15];
extern const uint8_t gm_s4inmcuim4088[46];
extern const uint8_t gm_s4j4d3l9da8i6[41];
extern const uint8_t gm_s4m2m0c0t74ak[46];
extern const uint8_t gm_s4puodol85qqo[55];
extern const uint8_t gm_s4qg6lddtahb8[4];
extern const uint8_t gm_s4t89div5i959[80];
extern const uint8_t gm_s4v6k193oo0cr[14];
extern const uint8_t gm_s50fbjkgs3ote[11];
extern const uint8_t gm_s54emlnl8lmoe[38];
extern const uint8_t gm_s54jmbv61vfjv[12];
extern const uint8_t gm_s557bv2sfqcom[6];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s589bsqckia3s[8];
extern const uint8_t gm_s5958kaiuh54g[84];
extern const uint8_t gm_s5cmg64f6av8k[70];
extern const uint8_t gm_s5cpmumv7vkcr[7];
extern const uint8_t gm_s5dhdddu73608[25];
extern const uint8_t gm_s5ea2jdgum6o9[33];
extern const uint8_t gm_s5glpb7fbtk4c[27];
extern const uint8_t gm_s5hr2c7nkokp9[70];
extern const uint8_t gm_s5k6emuk51u47[27];
extern const uint8_t gm_s5kvaetfehsvi[13];
extern const uint8_t gm_s5n7j87m9k09e[36];
extern const uint8_t gm_s5ndb61nne1fg[58];
extern const uint8_t gm_s5qukklsuqpi4[17];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s5s758m9u7gle[26];
extern const uint8_t gm_s5v7o1bkddp5s[7];
extern const uint8_t gm_s616b6iju29ec[5];
extern const uint8_t gm_s62an8gcfpfbj[53];
extern const uint8_t gm_s64vef3if6vik[23];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s66b184memqnd[4];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s6ab2s9s2fif9[9];
extern const uint8_t gm_s6aiq6b6gsv1o[18];
extern const uint8_t gm_s6cchf3tcs9e4[21];
extern const uint8_t gm_s6ecg17hs903q[9];
extern const uint8_t gm_s6iln1emufss5[20];
extern const uint8_t gm_s6r46262afe2h[7];
extern const uint8_t gm_s6sv4vhunlp6l[9];
extern const uint8_t gm_s6u49d04cqlg1[17];
extern const uint8_t gm_s6uegtmhpu1qn[41];
extern const uint8_t gm_s6uo8vuok1rk4[10];
extern const uint8_t gm_s70h6okja64qq[25];
extern const uint8_t gm_s73vt0nuu7890[28];
extern const uint8_t gm_s77fggjckb5ln[41];
extern const uint8_t gm_s7cu0m5qc7arr[43];
extern const uint8_t gm_s7fto4aj9q7su[5];
extern const uint8_t gm_s7gn2sla7drif[3];
extern const uint8_t gm_s7i80akiq0d95[5];
extern const uint8_t gm_s7j165k7b6shl[32];
extern const uint8_t gm_s7pkv0u1oe9rv[102];
extern const uint8_t gm_s7qhcf46f682d[21];
extern const uint8_t gm_s7t1ot7rgnuch[11];
extern const uint8_t gm_s7umjdfqes00m[52];
extern const uint8_t gm_s7uovnbe5u8va[13];
extern const uint8_t gm_s829ksoojqn6l[41];
extern const uint8_t gm_s84p47580vnhr[12];
extern const uint8_t gm_s8gkio2134kd6[70];
extern const uint8_t gm_s8krlr0c92m1s[15];
extern const uint8_t gm_s8o3vqolbfl8t[11];
extern const uint8_t gm_s8ok287s0vtoq[38];
extern const uint8_t gm_s8pk5tcnrk6p2[8];
extern const uint8_t gm_s8q575q0mjt4k[6];
extern const uint8_t gm_s8r83g99aqvfj[52];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s8sv99c8q8gbc[71];
extern const uint8_t gm_s9lds9mpotrru[15];
extern const uint8_t gm_s9qu470anjl4i[8];
extern const uint8_t gm_s9r2a03bnhj06[58];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_s9rujqsrrjcho[110];
extern const uint8_t gm_s9tjtf1373ti5[5];
extern const uint8_t gm_s9tr38mqso4n7[93];
extern const uint8_t gm_s9uhkcs7rcfn8[14];
extern const uint8_t gm_sa0l93d5jbk7k[69];
extern const uint8_t gm_sa525m041ir3n[17];
extern const uint8_t gm_saja8vc684oud[41];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_san7he7q20qtj[7];
extern const uint8_t gm_saoprtd6rfhf7[4];
extern const uint8_t gm_sapidpi7me2ee[95];
extern const uint8_t gm_saq0ier99o235[35];
extern const uint8_t gm_sasgv00j2p5nu[18];
extern const uint8_t gm_sb2nfo787ropg[6];
extern const uint8_t gm_sb3sdaa9odag6[13];
extern const uint8_t gm_sbe9uv194cpmc[16];
extern const uint8_t gm_sbf1f7fu8bvpt[5];
extern const uint8_t gm_sbfc2b8kquvq8[9];
extern const uint8_t gm_sbgcsq0m3astj[43];
extern const uint8_t gm_sbhui4f7fb5q5[9];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sblo01ng5f1c2[1308];
extern const uint8_t gm_sblsu018nl4o3[3];
extern const uint8_t gm_sbq9k9kvdokbf[20];
extern const uint8_t gm_sbsb3nmbfgbv2[4];
extern const uint8_t gm_sbsu8rcurukea[25];
extern const uint8_t gm_sbui4p06qp6ck[29];
extern const uint8_t gm_sbupitjaj376b[34];
extern const uint8_t gm_sc0gdocdik6sv[7];
extern const uint8_t gm_sc1ac3a85htl8[12];
extern const uint8_t gm_sc3ht2jgt5ee4[689];
extern const uint8_t gm_sc61gdfjf1pvd[38];
extern const uint8_t gm_sccadmt684uri[23];
extern const uint8_t gm_sccm0heds95r6[60];
extern const uint8_t gm_scd599tsljaee[5];
extern const uint8_t gm_scdj734mhh9f0[17];
extern const uint8_t gm_scj1kuqp0242l[6];
extern const uint8_t gm_scnj362636897[25];
extern const uint8_t gm_scold1pf1iq8n[11];
extern const uint8_t gm_scq7qb5df34b0[14];
extern const uint8_t gm_scrrset1ln8bf[41];
extern const uint8_t gm_sct07pl02n02h[27];
extern const uint8_t gm_sct93mkmvl99q[20];
extern const uint8_t gm_scueeuhqhtotc[30];
extern const uint8_t gm_sd11ghdf4ciqp[6];
extern const uint8_t gm_sd1d50j1ea4vf[46];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sd5fg79k33phf[37];
extern const uint8_t gm_sd5iqorsg0rc4[73];
extern const uint8_t gm_sd6d9fvngpchj[14];
extern const uint8_t gm_sddtbfibb719p[41];
extern const uint8_t gm_sdeq555foclko[3];
extern const uint8_t gm_sdg7bdfivjinb[3];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdjbnp04akbbb[50];
extern const uint8_t gm_sdlumbvroiufa[3];
extern const uint8_t gm_sdmbb3udlgkpo[8];
extern const uint8_t gm_sdnbbauegomd2[7];
extern const uint8_t gm_sdnjmvrkc1r7r[12];
extern const uint8_t gm_sdslahffv7339[17];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_se64as7gfqfsc[36];
extern const uint8_t gm_seak3hcpfjfp2[70];
extern const uint8_t gm_sefrps06g7jfu[11];
extern const uint8_t gm_seg9l1usqbs3j[4];
extern const uint8_t gm_segcvuj6bu7e4[17];
extern const uint8_t gm_seje0bncv3tch[17];
extern const uint8_t gm_seor1k5gntiuc[52];
extern const uint8_t gm_sepo1h9bofnod[30];
extern const uint8_t gm_setv7ejg35cg9[43];
extern const uint8_t gm_sevfgu4cub43a[41];
extern const uint8_t gm_sf4qi8ut6u3p1[5];
extern const uint8_t gm_sf7l7ca9coehi[85];
extern const uint8_t gm_sf9regh1oaao3[14];
extern const uint8_t gm_sfd9hctnph9ef[12];
extern const uint8_t gm_sfdq8ss0dnpe2[8];
extern const uint8_t gm_sfj8otp2pq7qq[55];
extern const uint8_t gm_sfku76mf5fn5d[9];
extern const uint8_t gm_sfm6vc4bhnr4s[44];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfpfdag7t7v0f[16];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sfrku7jk647q0[83];
extern const uint8_t gm_sfv61btcutqd4[21];
extern const uint8_t gm_sg0997vvtiund[65];
extern const uint8_t gm_sg4u1qvcufkum[5];
extern const uint8_t gm_sg5uvbf14q54s[21];
extern const uint8_t gm_sg7920c5gghpv[13];
extern const uint8_t gm_sg9102k54psou[16];
extern const uint8_t gm_sg9g5p2e4brnh[62];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgh0dvu15b9ta[10];
extern const uint8_t gm_sghk06svnbp7m[40];
extern const uint8_t gm_sgls2f653rbea[17];
extern const uint8_t gm_sgqcrkl2nh339[4];
extern const uint8_t gm_sgs4qi36v3o5v[8];
extern const uint8_t gm_sgs96ai7rj93j[17];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_sh08f8dq9lenu[41];
extern const uint8_t gm_sh5k7nstldaeg[7];
extern const uint8_t gm_sh8t790g5lerb[12];
extern const uint8_t gm_shcu73rsj445k[13];
extern const uint8_t gm_shes0dtqt9sqq[28];
extern const uint8_t gm_shido3ioit5oe[10];
extern const uint8_t gm_shjfpng3ei3du[82];
extern const uint8_t gm_shl0ucmfrvk77[54];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shrelr880pj0m[7];
extern const uint8_t gm_shtiokhdiebr1[3];
extern const uint8_t gm_shtvnkmjp804q[23];
extern const uint8_t gm_si00d7fs7b5aq[10];
extern const uint8_t gm_si0567rndd8dl[17];
extern const uint8_t gm_si2s3gai2c29i[16];
extern const uint8_t gm_si33msf4t0sbn[8];
extern const uint8_t gm_si3f75dnj7pb9[26];
extern const uint8_t gm_si430gujtnl9u[11];
extern const uint8_t gm_sifq8gd3d748j[36];
extern const uint8_t gm_sigr245qbgr4o[14];
extern const uint8_t gm_sijd693t5ccnb[4];
extern const uint8_t gm_sim03rp8pjg4e[44];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sinogveu00g7m[36];
extern const uint8_t gm_sipf690cm2c6f[41];
extern const uint8_t gm_siqa65grpcsk9[99];
extern const uint8_t gm_siqsngf0mfo86[9];
extern const uint8_t gm_sitbu884cbr46[96];
extern const uint8_t gm_siun1n8hqooca[9];
extern const uint8_t gm_sj2ngf6ro6oja[65];
extern const uint8_t gm_sj62oio2kg58e[20];
extern const uint8_t gm_sj8b32e846r90[11];
extern const uint8_t gm_sjdbk1ada1ddm[8];
extern const uint8_t gm_sjdv8icqpfhc5[6];
extern const uint8_t gm_sjeh66tmmrhop[3];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_sjfrmur8rvp24[34];
extern const uint8_t gm_sjiaitbr3bp01[10];
extern const uint8_t gm_sjibktbbpgpq2[17];
extern const uint8_t gm_sjkqdj35ksd9p[3];
extern const uint8_t gm_sjn3oh87mil2m[333];
extern const uint8_t gm_sjngmi12bbcu5[30];
extern const uint8_t gm_sjq0dshsov83m[20];
extern const uint8_t gm_sjqt811hvtpi7[25];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_sk0bnuggr6sri[14];
extern const uint8_t gm_sk1ojn82dahdn[13];
extern const uint8_t gm_sk5asgkllekl2[14];
extern const uint8_t gm_sk8b751vs73ev[67];
extern const uint8_t gm_sk8fkllqjst4j[13];
extern const uint8_t gm_ska23dbjvthl3[15];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skg1c046vhqs5[45];
extern const uint8_t gm_skj1a463v050q[79];
extern const uint8_t gm_skjhai33494u2[9];
extern const uint8_t gm_sko4cgkdijve3[57];
extern const uint8_t gm_sks4g7rc6a1af[16];
extern const uint8_t gm_sksl30l9t8a1n[41];
extern const uint8_t gm_sl2lo3hki3t4d[36];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sl55obh2ok05j[4];
extern const uint8_t gm_sl7p05f6l6cga[10];
extern const uint8_t gm_sl8fmedd9kk64[34];
extern const uint8_t gm_slasd9g26mfnt[12];
extern const uint8_t gm_slbmfdf2lr7u3[15];
extern const uint8_t gm_sletp8nk609ee[17];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const uint8_t gm_slh6d6uosdbkg[35];
extern const uint8_t gm_slikoo8r5oupm[18];
extern const uint8_t gm_sllgtfi45c44a[8];
extern const uint8_t gm_sllrmadmbrcvv[54];
extern const uint8_t gm_slmqf5nrs9f6b[13];
extern const uint8_t gm_slphk0n858mv9[13];
extern const uint8_t gm_slqtt99r493q9[102];
extern const uint8_t gm_slskjtl6alfrd[6];
extern const uint8_t gm_sm06nsg0msdgd[9];
extern const uint8_t gm_sm3fgo8acha61[13];
extern const uint8_t gm_sm9iv4f7cjs9t[62];
extern const uint8_t gm_smdggivmb7eg1[41];
extern const uint8_t gm_smeu8adf6pdko[38];
extern const uint8_t gm_smfprcpea3ibe[10];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smhli04hme60d[30];
extern const uint8_t gm_smhs805pum0dn[26];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_smk33hl0qg9hs[4];
extern const uint8_t gm_smr2fm6cfetj8[5];
extern const uint8_t gm_smraeij5q6ubc[11];
extern const uint8_t gm_smsrao7h22muv[61];
extern const uint8_t gm_smuti066jdthl[24];
extern const uint8_t gm_smvgcg1j21t6v[27];
extern const uint8_t gm_sn449k8j7bj3v[51];
extern const uint8_t gm_sn88c2jg5b9gv[4];
extern const uint8_t gm_snb8htg7a7p5u[16];
extern const uint8_t gm_snbn61h56c5fj[52];
extern const uint8_t gm_snftqbg249ljm[41];
extern const uint8_t gm_sngdu4l8vh3l8[5];
extern const uint8_t gm_snh86nvrv0us3[19];
extern const uint8_t gm_snhmu20ccir10[16];
extern const uint8_t gm_snlpcspgan3ri[16];
extern const uint8_t gm_sno2v4rqn9iav[61];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_so3t10be8g68d[10];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_sod9r7r024h9q[6];
extern const uint8_t gm_soefp7bosmvt1[12];
extern const uint8_t gm_soo0b43e4t7sn[12];
extern const uint8_t gm_sopacnadlsnva[99];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const uint8_t gm_sovqp4emsh0q8[5];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp0hmouoaumsl[10];
extern const uint8_t gm_sp0kv61jd1btm[5];
extern const uint8_t gm_sp6l5tr58ip76[23];
extern const uint8_t gm_spchsf3taendr[3];
extern const uint8_t gm_spdkbvjffkg4b[11];
extern const uint8_t gm_speaekk0qc740[9];
extern const uint8_t gm_spfe58u39phsl[21];
extern const uint8_t gm_spnqca4mofck7[6];
extern const uint8_t gm_spnr4ftjbm9m9[61];
extern const uint8_t gm_sps1dthki4jpu[50];
extern const uint8_t gm_spsjkc9sqdvl6[61];
extern const uint8_t gm_spts1f8sg22km[18];
extern const uint8_t gm_spu32ed945op3[49];
extern const uint8_t gm_sq0573h7j28rq[23];
extern const uint8_t gm_sq12h87ngj5nl[9];
extern const uint8_t gm_sq9f472jpmh59[64];
extern const uint8_t gm_sq9f8k00ocil6[35];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_sqaji2d91cvct[19];
extern const uint8_t gm_sqc0827akcmi8[40];
extern const uint8_t gm_sqhghq789nvm0[8];
extern const uint8_t gm_sqj9769nr8pm0[48];
extern const uint8_t gm_sqlorrkumh3an[23];
extern const uint8_t gm_sqmqlaupa64jq[7];
extern const uint8_t gm_sqqaphhmqv39s[5];
extern const uint8_t gm_sqqdirgtqa30r[8];
extern const uint8_t gm_sqslcbfraugjg[8];
extern const uint8_t gm_sr00p3fdg5qn3[19];
extern const uint8_t gm_sr1bm2v312fv6[15];
extern const uint8_t gm_sr43s52rmecnf[33];
extern const uint8_t gm_sr462lclo52fq[8];
extern const uint8_t gm_sr87tgv3ltq65[13];
extern const uint8_t gm_srav7444q331a[37];
extern const uint8_t gm_srb1158jlk9u7[75];
extern const char gm_src8kcterbt0p7b[];
extern const char gm_src9gqs06mvt4n2[];
extern const char gm_srce9tqfqu9u3el[];
extern const char gm_srcg0ns322676oc[];
extern const char gm_srcgs96ai7rj93j[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcp2vt6rk8827h[];
extern const char gm_srcq0uibesf3n7k[];
extern const char gm_srcrvqeupiu1766[];
extern const uint8_t gm_sreop6h89ulb0[18];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srhms7dhd79bq[15];
extern const uint8_t gm_srjdrn9cldsu5[57];
extern const uint8_t gm_srq72vm6d96b6[41];
extern const uint8_t gm_sru9l3qtrdjko[84];
extern const uint8_t gm_srv05plrkjhf8[7];
extern const uint8_t gm_ss3kah49g222u[33];
extern const uint8_t gm_ss3op0i298nem[69];
extern const uint8_t gm_ss7moisvq3l8h[6];
extern const uint8_t gm_ssd7h3tuipgq5[55];
extern const uint8_t gm_sse5id8rmv069[8];
extern const uint8_t gm_ssi8bk0pvfr56[22];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_ssnb4or831s5l[78];
extern const uint8_t gm_ssob5j6sas4h7[58];
extern const uint8_t gm_st2s0ov9emddj[12];
extern const uint8_t gm_st2vlvkf674c2[250];
extern const uint8_t gm_st518h9bgt9n7[22];
extern const uint8_t gm_st7d4oeklkoma[11];
extern const uint8_t gm_stco696vp9tto[14];
extern const uint8_t gm_std2slsc0ssrl[11];
extern const uint8_t gm_std59a9b6lpnn[10];
extern const uint8_t gm_stdefoi5sk91c[12];
extern const uint8_t gm_stfhv28imtfkb[6];
extern const uint8_t gm_stjlu12rqiie7[13];
extern const uint8_t gm_stk678hojgu6q[24];
extern const uint8_t gm_stkh9r1ip5q2v[35];
extern const uint8_t gm_stmdki8kna56a[24];
extern const uint8_t gm_stpnhgp46l3tu[9];
extern const uint8_t gm_strsld72u30ds[9];
extern const uint8_t gm_su01gdacg3ppp[9];
extern const uint8_t gm_su1mil1q3eqgp[16];
extern const uint8_t gm_su61qr04nq335[13];
extern const uint8_t gm_su635cbqj0rpd[3];
extern const uint8_t gm_su6s568b7b4kr[5];
extern const uint8_t gm_sub1g4k5l4c07[11];
extern const uint8_t gm_subcm6amchfvv[5];
extern const uint8_t gm_sucs0qmdapqe8[41];
extern const uint8_t gm_suk7nc9hc39vu[13];
extern const uint8_t gm_sup2jn5hml593[6];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const uint8_t gm_sv0pi8lprf2k8[55];
extern const uint8_t gm_sv5o4caljvbmj[28];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const uint8_t gm_svfa039r0e6te[33];
extern const uint8_t gm_svgn73l5s66lk[7];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svkkls864r4t4[30];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svkrg5dctlh9m[7];
extern const uint8_t gm_svl4l2v56ma30[8];
extern const uint8_t gm_svln5ui59rjil[6];
extern const uint8_t gm_svmck4pckkpai[41];
extern const uint8_t gm_svmo2cukmep6f[9];
extern const uint8_t gm_svom2l7bom2pm[71];
extern const uint8_t gm_svtut88rj5dr8[15];
extern const uint8_t gm_svu2guaq187mi[9];
extern const gm_type gm_tyiiln09e647;
const char gm_src8kcterbt0p7b[] = "compiler/fmt/classify.gm";
const char gm_src9gqs06mvt4n2[] = "compiler/pkg/build.gm";
const char gm_srce9tqfqu9u3el[] = "compiler/fmt/fmt.gm";
const char gm_srcg0ns322676oc[] = "compiler/load/manifest.gm";
const char gm_srcgs96ai7rj93j[] = "compiler/main.gm";
const char gm_srclv0dcbbfhves[] = "compiler/sema/sema.gm";
const char gm_srcoj9bno4fi1q4[] = "compiler/pkg/sync.gm";
const char gm_srcp2vt6rk8827h[] = "compiler/driver/paths.gm";
const char gm_srcq0uibesf3n7k[] = "lib/std/fiber/waiters/waiters.gm";
const char gm_srcrvqeupiu1766[] = "compiler/interp/place.gm";
const gm_type gm_tyiiln09e647 = {.size = 16, .align = 16, .kind = GM_K_INT, .bits = 128, .is_signed = true};
const gm_type gm_tyiiln09e647;
const uint8_t gm_s00odc15r3q8f[16] = "invalid value `";
const uint8_t gm_s00pihipe0jga[6] = "ToPtr";
const uint8_t gm_s0147vgtvilnq[19] = "` does not embed `";
const uint8_t gm_s046jqe2j7k9s[5] = "lb{}";
const uint8_t gm_s07uh61s060o9[25] = "expected a constant here";
const uint8_t gm_s09tkid4cqjmm[62] = "` is an effect set, not a type; use it after `needs` (\302\24716.6)";
const uint8_t gm_s0doambt0iqhe[4] = "\\u{";
const uint8_t gm_s0ek1hibl01t2[47] = "`^` dereferences a pointer `^T`, but this is `";
const uint8_t gm_s0f7epcithn0r[18] = "kind = GM_K_ARRAY";
const uint8_t gm_s0fg7tcl0p4ij[13] = "(uintptr_t)0";
const uint8_t gm_s0h9r2boh81d2[65] = "(void *code, void *env, gm_node *cx, const void *d, uintptr_t n)";
const uint8_t gm_s0hfsnjdrghcm[9] = "\000@\000\000\000\000\000\000";
const uint8_t gm_s0johk446liuj[28] = "` does not apply to a field";
const uint8_t gm_s0kajv5j53ub7[38] = "not supported by the C back end yet: ";
const uint8_t gm_s0l40gug6qpuq[17] = " && memcmp(mp, \"";
const uint8_t gm_s0nad8bs1qlvl[76] = "the field `{}` of `{}` is visible only in the package of its module (\302\2473.3)";
const uint8_t gm_s0u9ru4tiqbe7[9] = "register";
const uint8_t gm_s0unahdn5j99v[14] = "complex {} {}";
const uint8_t gm_s12khqa42i680[29] = "a literal is too large for `";
const uint8_t gm_s12scbfvbvl4s[3] = "me";
const uint8_t gm_s13b13uieof5u[12] = "gm_proc_put";
const uint8_t gm_s187rb18i1kgk[4] = "Min";
const uint8_t gm_s1cn5ab5opg9f[131] = "fibers (`with \342\200\246 on`) need a context switch, which the runtime has only for x86-64, AArch64 and s390x; this freestanding target (";
const uint8_t gm_s1crsce68cmnd[20] = "GM_RUN_EXPORT(gm_xt";
const uint8_t gm_s1h3hvq26g2dl[47] = "`{}` is a module, which is not a value (\302\2473.2)";
const uint8_t gm_s1idibqkta392[7] = "value ";
const uint8_t gm_s1lfsmjv8evdo[9] = "a vector";
const uint8_t gm_s1n4l05blor5r[40] = "`_` cannot be the name of a declaration";
const uint8_t gm_s1r58jmps9mcc[7] = "coerce";
const uint8_t gm_s1tdqlhdsnu5l[14] = "this returns ";
const uint8_t gm_s1ul47vja8nnc[16] = "gm_cover_counts";
const uint8_t gm_s1vmtsmtbio1j[15] = "cannot create ";
const uint8_t gm_s20t5qopq8n9u[5] = "args";
const uint8_t gm_s27am6v0m91fv[87] = "`--target` cannot be combined with `--mode=safe`, which builds for the host with Fil-C";
const uint8_t gm_s2ma5cp41jk1a[7] = "Center";
const uint8_t gm_s2r62ofaabkh1[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\351\001\000\000!\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s2svafu2na27p[17] = "`~` on this type";
const uint8_t gm_s2u0i5o1ej1mg[10] = "the URL `";
const uint8_t gm_s2ud1phv1fk5e[15] = "gm_fs_open_dir";
const uint8_t gm_s2uilckebopb3[12] = "gm_f16_from";
const uint8_t gm_s2v0mmbl3fojp[55] = "` is visible only in the package of its module (\302\2473.3)";
const uint8_t gm_s31vj4bjbj0gj[36] = "static void gm_init_consts(void) {\012";
const uint8_t gm_s3hali1afgel3[28] = "a fiber's function returned";
const uint8_t gm_s3hrsavigl7la[19] = "to begin the block";
const uint8_t gm_s3ngn7me40qa4[5] = "arch";
const uint8_t gm_s41sp7ht9l505[67] = "unknown lint rule `{}` in [lint] (`gamma lint --rules` lists them)";
const uint8_t gm_s42b9o00en8pl[34] = "`{}` is not an operator; use `{}`";
const uint8_t gm_s449uaav7f0l0[11] = "--asyncify";
const uint8_t gm_s460n81rl2c9c[5] = "'\\n'";
const uint8_t gm_s467d5cm1d5us[4] = "u32";
const uint8_t gm_s480gcu6od53p[61] = "invalid memory access: write to read-only memory at address ";
const uint8_t gm_s48o8olsbqnph[12] = ".body_cx = ";
const uint8_t gm_s495moo6tjeh0[20] = "the slice method `.";
const uint8_t gm_s4ccl84nrk27k[55] = "cannot read {}: No such file or directory (os error 2)";
const uint8_t gm_s4cdme6b7eogn[56] = "dependency `{}` has both `{}` and `{}`; use one of them";
const uint8_t gm_s4e3q3n6msi6m[6] = ")));\012";
const uint8_t gm_s4emvvn1d7f2t[6] = "(v + ";
const uint8_t gm_s4hv5fsmnuq3r[15] = "; return 0; }\012";
const uint8_t gm_s4inmcuim4088[46] = "an expression left over from generic checking";
const uint8_t gm_s4j4d3l9da8i6[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000I\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s4m2m0c0t74ak[46] = "apply the fixes of the warnings that have one";
const uint8_t gm_s4puodol85qqo[55] = "an arithmetic operand of a shift must be parenthesized";
const uint8_t gm_s4qg6lddtahb8[4] = "\302\265s";
const uint8_t gm_s4t89div5i959[80] = "an array indexed by a type needs a concrete enum type, not the type parameter `";
const uint8_t gm_s4v6k193oo0cr[14] = "gm_proc_write";
const uint8_t gm_s50fbjkgs3ote[11] = "); return ";
const uint8_t gm_s54emlnl8lmoe[38] = ": stream did not contain valid UTF-8\012";
const uint8_t gm_s54jmbv61vfjv[12] = "placeholder";
const uint8_t gm_s557bv2sfqcom[6] = "enum ";
const uint8_t gm_s579fb7l5l6df[3] = "fn";
const uint8_t gm_s589bsqckia3s[8] = "Sys({})";
const uint8_t gm_s5958kaiuh54g[84] = "the members of a tuple compared by `in` must have one type, but found `{}` and `{}`";
const uint8_t gm_s5cmg64f6av8k[70] = "internal error: an integer operator on a value that is not an integer";
const uint8_t gm_s5cpmumv7vkcr[7] = "error\012";
const uint8_t gm_s5dhdddu73608[25] = "unwrap of error value {}";
const uint8_t gm_s5ea2jdgum6o9[33] = ") gm_trap(\"integer overflow in `";
const uint8_t gm_s5glpb7fbtk4c[27] = "GM_ST(void *, (uint8_t *)&";
const uint8_t gm_s5hr2c7nkokp9[70] = "`, whose operations can exit or suspend, unless it handles it (\302\2478.8)";
const uint8_t gm_s5k6emuk51u47[27] = "` is not defined on floats";
const uint8_t gm_s5kvaetfehsvi[13] = "is_signed = ";
const uint8_t gm_s5n7j87m9k09e[36] = "`{}` requires {} operands, not `{}`";
const uint8_t gm_s5ndb61nne1fg[58] = "`: `subdir` must be a relative path inside the repository";
const uint8_t gm_s5qukklsuqpi4[17] = "unused-parameter";
const uint8_t gm_s5rv6umk4akl0[6] = "while";
const uint8_t gm_s5s758m9u7gle[26] = "gm_cancel_cont(inst->fw, ";
const uint8_t gm_s5v7o1bkddp5s[7] = "unpack";
const uint8_t gm_s616b6iju29ec[5] = "push";
const uint8_t gm_s62an8gcfpfbj[53] = "a union's tag type must be an integer type, not `{}`";
const uint8_t gm_s64vef3if6vik[23] = "to close `pub(package`";
const uint8_t gm_s65qt8qo64cje[3] = "-o";
const uint8_t gm_s66b184memqnd[4] = " > ";
const uint8_t gm_s66fbu3uhl900[20] = "unknown lint rule `";
const uint8_t gm_s6ab2s9s2fif9[9] = ")) | (((";
const uint8_t gm_s6aiq6b6gsv1o[18] = "character literal";
const uint8_t gm_s6cchf3tcs9e4[21] = "` has no operation `";
const uint8_t gm_s6ecg17hs903q[9] = ".next = ";
const uint8_t gm_s6iln1emufss5[20] = "unmatched closing `";
const uint8_t gm_s6r46262afe2h[7] = ")) - (";
const uint8_t gm_s6sv4vhunlp6l[9] = " became ";
const uint8_t gm_s6u49d04cqlg1[17] = "the clause for `";
const uint8_t gm_s6uegtmhpu1qn[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\021\000\000\000X\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s6uo8vuok1rk4[10] = "name = {}";
const uint8_t gm_s70h6okja64qq[25] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_s73vt0nuu7890[28] = "` (supplied by this spread)";
const uint8_t gm_s77fggjckb5ln[41] = "the directory {} exists and is not empty";
const uint8_t gm_s7cu0m5qc7arr[43] = " repl; :help for commands, :quit to leave\012";
const uint8_t gm_s7fto4aj9q7su[5] = "Sub(";
const uint8_t gm_s7gn2sla7drif[3] = "nf";
const uint8_t gm_s7i80akiq0d95[5] = "Hash";
const uint8_t gm_s7j165k7b6shl[32] = "double free of heap block {:#x}";
const uint8_t gm_s7pkv0u1oe9rv[102] = "Safe builds compile with the Fil-C compiler (https://fil-c.org), but the C compiler `{}` is not Fil-C";
const uint8_t gm_s7qhcf46f682d[21] = "kind = GM_K_OPTIONAL";
const uint8_t gm_s7t1ot7rgnuch[11] = ", (double)";
const uint8_t gm_s7umjdfqes00m[52] = "`{}` needs an element for every variant: missing {}";
const uint8_t gm_s7uovnbe5u8va[13] = "gm_fs_remove";
const uint8_t gm_s829ksoojqn6l[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\212\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s84p47580vnhr[12] = "proc_launch";
const uint8_t gm_s8gkio2134kd6[70] = "this array lives until the end of the block of the statement it is in";
const uint8_t gm_s8krlr0c92m1s[15] = ",\"end_offset\":";
const uint8_t gm_s8o3vqolbfl8t[11] = "clock_wall";
const uint8_t gm_s8ok287s0vtoq[38] = "the `url` of dependency `{}` is empty";
const uint8_t gm_s8pk5tcnrk6p2[8] = "LaneMin";
const uint8_t gm_s8q575q0mjt4k[6] = " the ";
const uint8_t gm_s8r83g99aqvfj[52] = "` does not exist on a freestanding target (\302\2473.7.1)";
const uint8_t gm_s8sv6qdepk5ba[4] = "std";
const uint8_t gm_s8sv99c8q8gbc[71] = "`, since a field has a type with invalid bit patterns; build it with `";
const uint8_t gm_s9lds9mpotrru[15] = "gm_posix_spawn";
const uint8_t gm_s9qu470anjl4i[8] = "warning";
const uint8_t gm_s9r2a03bnhj06[58] = "internal error: a shift by a value that is not an integer";
const uint8_t gm_s9r64t1166q1f[5] = "test";
const uint8_t gm_s9rujqsrrjcho[110] = "` of a `#bits` struct must be `bool`, an integer, an enum with an explicit tag type or another `#bits` struct";
const uint8_t gm_s9tjtf1373ti5[5] = "_ = ";
const uint8_t gm_s9tr38mqso4n7[93] = "`needs not` ends the signature: it means the empty effect set, not \"everything but\" (\302\24719.2)";
const uint8_t gm_s9uhkcs7rcfn8[14] = "gm_cmd_unsafe";
const uint8_t gm_sa0l93d5jbk7k[69] = "destructuring is not allowed at top level; bind each name separately";
const uint8_t gm_sa525m041ir3n[17] = "does not target ";
const uint8_t gm_saja8vc684oud[41] = "invalid `git` URL of dependency `{}`: {}";
const uint8_t gm_salin070nim24[30] = "signed integer or float lanes";
const uint8_t gm_san7he7q20qtj[7] = "__size";
const uint8_t gm_saoprtd6rfhf7[4] = ")->";
const uint8_t gm_sapidpi7me2ee[95] = "gamma: cannot run this program here: foreign code and assembly are not available on this host\012";
const uint8_t gm_saq0ier99o235[35] = "` cannot take a receiver of type `";
const uint8_t gm_sasgv00j2p5nu[18] = "--connect-timeout";
const uint8_t gm_sb2nfo787ropg[6] = "the `";
const uint8_t gm_sb3sdaa9odag6[13] = "capabilities";
const uint8_t gm_sbe9uv194cpmc[16] = "` (known keys: ";
const uint8_t gm_sbf1f7fu8bvpt[5] = " or ";
const uint8_t gm_sbfc2b8kquvq8[9] = "float 0x";
const uint8_t gm_sbgcsq0m3astj[43] = "this `if` gives the value of its condition";
const uint8_t gm_sbhui4f7fb5q5[9] = "Variants";
const uint8_t gm_sbjj41cb5lph3[60] = "internal error: unresolved coercion reached the interpreter";
const uint8_t gm_sblo01ng5f1c2[1308] = ":root { color-scheme: light dark; --fg: #1f2328; --bg: #ffffff; --muted: #59636e; --code-bg: #f3f4f6; --rule: #d1d9e0; --link: #0550ae; }\012@media (prefers-color-scheme: dark) { :root { --fg: #e6edf3; --bg: #0d1117; --muted: #9198a1; --code-bg: #161b22; --rule: #30363d; --link: #58a6ff; } }\012body { margin: 0 auto; max-width: 52rem; padding: 1.5rem 1rem 4rem; background: var(--bg); color: var(--fg); font: 16px/1.55 system-ui, sans-serif; }\012a { color: var(--link); }\012h1, h2, h3 { line-height: 1.25; }\012h2 { margin-top: 2.5rem; padding-bottom: .3rem; border-bottom: 1px solid var(--rule); }\012h3 { margin-top: 2rem; }\012code, pre { font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }\012code { background: var(--code-bg); padding: .1em .3em; border-radius: 4px; }\012pre { background: var(--code-bg); padding: .75rem 1rem; border-radius: 6px; overflow-x: auto; }\012pre code { background: none; padding: 0; }\012table { border-collapse: collapse; }\012th, td { border: 1px solid var(--rule); padding: .3rem .6rem; text-align: left; vertical-align: top; }\012blockquote { margin-left: 0; padding-left: 1rem; border-left: 3px solid var(--rule); color: var(--muted); }\012.contents { columns: 14rem; column-gap: 2rem; padding-left: 1.2rem; }\012.members dt { margin-top: .5rem; }\012.members dd { margin-left: 1.5rem; }\012";
const uint8_t gm_sblsu018nl4o3[3] = "cc";
const uint8_t gm_sbq9k9kvdokbf[20] = "compiler/pkg/git.gm";
const uint8_t gm_sbsb3nmbfgbv2[4] = "Hex";
const uint8_t gm_sbsu8rcurukea[25] = "this operator on `bool`s";
const uint8_t gm_sbui4p06qp6ck[29] = "((((unsigned __int128)1) << ";
const uint8_t gm_sbupitjaj376b[34] = " m;\012    memset(&m, 0, sizeof m);\012";
const uint8_t gm_sc0gdocdik6sv[7] = "<item>";
const uint8_t gm_sc1ac3a85htl8[12] = "    if ((a[";
const uint8_t gm_sc3ht2jgt5ee4[689] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sc61gdfjf1pvd[38] = " names cannot destructure a tuple of ";
const uint8_t gm_sccadmt684uri[23] = "invalid library name `";
const uint8_t gm_sccm0heds95r6[60] = "`@variant_name` takes a value of a tagged union type, not `";
const uint8_t gm_scd599tsljaee[5] = "Cast";
const uint8_t gm_scdj734mhh9f0[17] = "__atomic_load_n(";
const uint8_t gm_scj1kuqp0242l[6] = "gm_hd";
const uint8_t gm_scnj362636897[25] = "a temporary `#soa` array";
const uint8_t gm_scold1pf1iq8n[11] = ", .hctx = ";
const uint8_t gm_scq7qb5df34b0[14] = "a declaration";
const uint8_t gm_scrrset1ln8bf[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\263\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sct07pl02n02h[27] = "invalid leading zero in {}";
const uint8_t gm_sct93mkmvl99q[20] = "memset((uint8_t *)&";
const uint8_t gm_scueeuhqhtotc[30] = "cannot unpack the archive: {}";
const uint8_t gm_sd11ghdf4ciqp[6] = "rules";
const uint8_t gm_sd1d50j1ea4vf[46] = "this `if` gives the negation of its condition";
const uint8_t gm_sd4ibvv64si9n[45] = "internal error: float widening of non-floats";
const uint8_t gm_sd4qrpv3lpilp[4] = "u{}";
const uint8_t gm_sd5fg79k33phf[37] = "static void {}(void *req, gm_cont k)";
const uint8_t gm_sd5iqorsg0rc4[73] = "`@variants({})` needs a union without payloads, but variant `{}` has one";
const uint8_t gm_sd6d9fvngpchj[14] = "gm_cover_init";
const uint8_t gm_sddtbfibb719p[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\034\001\000\000\005\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sdeq555foclko[3] = "+=";
const uint8_t gm_sdg7bdfivjinb[3] = "Ge";
const uint8_t gm_sdi44r3kug6n6[14] = "gm_node *inst";
const uint8_t gm_sdjbnp04akbbb[50] = "`[_]T(fill: x)` cannot infer the length; write it";
const uint8_t gm_sdlumbvroiufa[3] = "Fs";
const uint8_t gm_sdmbb3udlgkpo[8] = ": goto ";
const uint8_t gm_sdnbbauegomd2[7] = "sizeof";
const uint8_t gm_sdnjmvrkc1r7r[12] = "gm_with_go(";
const uint8_t gm_sdslahffv7339[17] = "    uint8_t loc[";
const uint8_t gm_se19nf1421e37[4] = "Neg";
const uint8_t gm_se64as7gfqfsc[36] = ")-1) gm_trap(\"integer overflow in `";
const uint8_t gm_seak3hcpfjfp2[70] = "an array length is a constant `usize` or an enum type (`[E]T`), and `";
const uint8_t gm_sefrps06g7jfu[11] = "a slice of";
const uint8_t gm_seg9l1usqbs3j[4] = "Bin";
const uint8_t gm_segcvuj6bu7e4[17] = "positionEncoding";
const uint8_t gm_seje0bncv3tch[17] = "\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_seor1k5gntiuc[52] = "` needs a package, but no gamma.toml was found for ";
const uint8_t gm_sepo1h9bofnod[30] = "expected {} digits after `{}`";
const uint8_t gm_setv7ejg35cg9[43] = "gamma: the program has no `main` function\012";
const uint8_t gm_sevfgu4cub43a[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\350\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sf4qi8ut6u3p1[5] = "\015\012\015\012";
const uint8_t gm_sf7l7ca9coehi[85] = "a type declared inside a function cannot be generic; declare it at top level (\302\2473.5)";
const uint8_t gm_sf9regh1oaao3[14] = " = __builtin_";
const uint8_t gm_sfd9hctnph9ef[12] = "fs_set_mode";
const uint8_t gm_sfdq8ss0dnpe2[8] = "--rules";
const uint8_t gm_sfj8otp2pq7qq[55] = "without the marker it is private to its module (\302\2473.3)";
const uint8_t gm_sfku76mf5fn5d[9] = "fs_close";
const uint8_t gm_sfm6vc4bhnr4s[44] = ", the limit of this implementation (\302\24723.5)";
const uint8_t gm_sfm9elkq6bn2e[2] = "`";
const uint8_t gm_sfpfdag7t7v0f[16] = "gm_fs_read_link";
const uint8_t gm_sfpopfqbubj89[35] = "`@type_of(e)` takes one expression";
const uint8_t gm_sfrku7jk647q0[83] = "targets a machine without `__int128`, which the C back end needs (a 64-bit target)";
const uint8_t gm_sfv61btcutqd4[21] = "-Wl,-z,stack-size={}";
const uint8_t gm_sg0997vvtiund[65] = "this calls a function value whose target is not known statically";
const uint8_t gm_sg4u1qvcufkum[5] = " yet";
const uint8_t gm_sg5uvbf14q54s[21] = " __attribute__(({}))";
const uint8_t gm_sg7920c5gghpv[13] = "gm_heap_free";
const uint8_t gm_sg9102k54psou[16] = "expr_to_pattern";
const uint8_t gm_sg9g5p2e4brnh[62] = "{}:{}: dependency `{}` is a url dependency; run `gamma fetch`";
const uint8_t gm_sgbumttiou4de[8] = "effects";
const uint8_t gm_sgh0dvu15b9ta[10] = "((void *)";
const uint8_t gm_sghk06svnbp7m[40] = "too many arguments: the function takes ";
const uint8_t gm_sgls2f653rbea[17] = "parse_effect_lit";
const uint8_t gm_sgqcrkl2nh339[4] = "Dir";
const uint8_t gm_sgs4qi36v3o5v[8] = "bit-not";
const uint8_t gm_sgs96ai7rj93j[17] = "compiler/main.gm";
const uint8_t gm_sgvvkov5kvprt[10] = "copy_from";
const uint8_t gm_sh08f8dq9lenu[41] = "if the function must take it, name it `_";
const uint8_t gm_sh5k7nstldaeg[7] = " [N]T`";
const uint8_t gm_sh8t790g5lerb[12] = "sort_by_key";
const uint8_t gm_shcu73rsj445k[13] = ",\"end_line\":";
const uint8_t gm_shes0dtqt9sqq[28] = "{} is not an array or slice";
const uint8_t gm_shido3ioit5oe[10] = "(a + i * ";
const uint8_t gm_shjfpng3ei3du[82] = "`{}` does not apply to complex numbers; they have `+`, `-`, `*` and `/` (\302\2477.4.7)";
const uint8_t gm_shl0ucmfrvk77[54] = "`--timeout` needs a positive number of seconds, not `";
const uint8_t gm_shofa8pdml9fb[13] = "pub(package)";
const uint8_t gm_shrelr880pj0m[7] = ", NULL";
const uint8_t gm_shtiokhdiebr1[3] = "-=";
const uint8_t gm_shtvnkmjp804q[23] = ";\012static void gm_ginit";
const uint8_t gm_si00d7fs7b5aq[10] = "--backend";
const uint8_t gm_si0567rndd8dl[17] = "\000\000\000\000\000\000\000\000P\000\000\000\000\000\000\000";
const uint8_t gm_si2s3gai2c29i[16] = "else capture {}";
const uint8_t gm_si33msf4t0sbn[8] = "--force";
const uint8_t gm_si3f75dnj7pb9[26] = "more than one input given";
const uint8_t gm_si430gujtnl9u[11] = "GM_ST(uint";
const uint8_t gm_sifq8gd3d748j[36] = "the element for `{}` is given twice";
const uint8_t gm_sigr245qbgr4o[14] = "gm_trap_slice";
const uint8_t gm_sijd693t5ccnb[4] = "box";
const uint8_t gm_sim03rp8pjg4e[44] = "    for (uintptr_t i = 0; i < n; i++) if (!";
const uint8_t gm_sinmpfqtbo9in[5] = "trap";
const uint8_t gm_sinogveu00g7m[36] = ": the process cannot be waited for\012";
const uint8_t gm_sipf690cm2c6f[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\361\000\000\000%\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_siqa65grpcsk9[99] = "an exported function's effects must be empty or `{Sys}`: C callers cannot supply handlers (\302\24719.4)";
const uint8_t gm_siqsngf0mfo86[9] = "\014\000\000\000\000\000\000\000";
const uint8_t gm_sitbu884cbr46[96] = "the `else` of a pattern binding cannot capture an error; match the error with `!e` in a `match`";
const uint8_t gm_siun1n8hqooca[9] = "an error";
const uint8_t gm_sj2ngf6ro6oja[65] = "a generic function cannot have a calling convention other than C";
const uint8_t gm_sj62oio2kg58e[20] = ", &p, sizeof p); }\012";
const uint8_t gm_sj8b32e846r90[11] = "pat-range ";
const uint8_t gm_sjdbk1ada1ddm[8] = ", gm_r)";
const uint8_t gm_sjdv8icqpfhc5[6] = "MacOS";
const uint8_t gm_sjeh66tmmrhop[3] = "kb";
const uint8_t gm_sjfg2291kjugh[4] = "---";
const uint8_t gm_sjfrmur8rvp24[34] = "` (in an instantiation used here)";
const uint8_t gm_sjiaitbr3bp01[10] = "reference";
const uint8_t gm_sjibktbbpgpq2[17] = "untyped-complex ";
const uint8_t gm_sjkqdj35ksd9p[3] = "wn";
const uint8_t gm_sjn3oh87mil2m[333] = "pub Os :: union {{ Linux, MacOS, Windows, FreeBSD, Wasi, Freestanding }}\012pub Arch :: union {{ X86_64, Aarch64, Riscv64, S390x, Wasm32, Other }}\012pub Endian :: union {{ Little, Big }}\012pub Mode :: union {{ Debug, Checked, Safe, Fast }}\012pub os :: Os.{}\012pub arch :: Arch.{}\012pub endian :: Endian.{}\012pub ptr_bits :: {}\012pub mode :: Mode.{}\012";
const uint8_t gm_sjngmi12bbcu5[30] = "a `match` that no arm matches";
const uint8_t gm_sjq0dshsov83m[20] = "first declared here";
const uint8_t gm_sjqt811hvtpi7[25] = "(({} != 0) == ({} != 0))";
const uint8_t gm_sk04qg20jmac0[4] = "{}\012";
const uint8_t gm_sk0bnuggr6sri[14] = "proc_write_in";
const uint8_t gm_sk1ojn82dahdn[13] = "gm_rt_switch";
const uint8_t gm_sk5asgkllekl2[14] = "gm_filc_entry";
const uint8_t gm_sk8b751vs73ev[67] = "a `#soa` operation in a generic function that was not instantiated";
const uint8_t gm_sk8fkllqjst4j[13] = "builtin type";
const uint8_t gm_ska23dbjvthl3[15] = "renameProvider";
const uint8_t gm_skfdo9qggtrbp[6] = "(a + ";
const uint8_t gm_skg1c046vhqs5[45] = "with completed while {} fiber{} {} suspended";
const uint8_t gm_skj1a463v050q[79] = "if (!gm_unw.kind) gm_trap(\"internal error: a `ctl` clause finished normally\", ";
const uint8_t gm_skjhai33494u2[9] = "\370\377\377\377\377\377\377\377";
const uint8_t gm_sko4cgkdijve3[57] = "`: `archive` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_sks4g7rc6a1af[16] = "(GM_LD(void *, ";
const uint8_t gm_sksl30l9t8a1n[41] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000,\003\000\000\020\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sl2lo3hki3t4d[36] = "unclosed inline table, expected `}`";
const uint8_t gm_sl3q52ep6aa53[2] = ".";
const uint8_t gm_sl4ns6hobicht[6] = "range";
const uint8_t gm_sl55obh2ok05j[4] = "{}=";
const uint8_t gm_sl7p05f6l6cga[10] = "lex parse";
const uint8_t gm_sl8fmedd9kk64[34] = "a declaration after the attribute";
const uint8_t gm_slasd9g26mfnt[12] = "attribute #";
const uint8_t gm_slbmfdf2lr7u3[15] = "gm_ctl_perform";
const uint8_t gm_sletp8nk609ee[17] = "\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000";
const uint8_t gm_slf2kl43ogurh[17] = "division by zero";
const uint8_t gm_slh6d6uosdbkg[35] = "expected a closing run of exactly ";
const uint8_t gm_slikoo8r5oupm[18] = "this trap message";
const uint8_t gm_sllgtfi45c44a[8] = "archive";
const uint8_t gm_sllrmadmbrcvv[54] = "this condition is always false: the branch never runs";
const uint8_t gm_slmqf5nrs9f6b[13] = "((uint8_t)0)";
const uint8_t gm_slphk0n858mv9[13] = " type, not `";
const uint8_t gm_slqtt99r493q9[102] = "`, which points outside the package through other links; links must stay inside the package directory";
const uint8_t gm_slskjtl6alfrd[6] = "clean";
const uint8_t gm_sm06nsg0msdgd[9] = "markdown";
const uint8_t gm_sm3fgo8acha61[13] = "__riscv_xlen";
const uint8_t gm_sm9iv4f7cjs9t[62] = "a `while` loop has no value, so `break` takes no operand here";
const uint8_t gm_smdggivmb7eg1[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000V\000\000\000(\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_smeu8adf6pdko[38] = ")));\012    memset(loc, 0, sizeof loc);\012";
const uint8_t gm_smfprcpea3ibe[10] = "proc_wait";
const uint8_t gm_smglbbtmdmip2[6] = "where";
const uint8_t gm_smhli04hme60d[30] = "unknown format spec `{{:{}}}`";
const uint8_t gm_smhs805pum0dn[26] = "top-level intrinsic calls";
const uint8_t gm_smi7k9v6u7cmo[11] = "`@type_of`";
const uint8_t gm_smk33hl0qg9hs[4] = "h{}";
const uint8_t gm_smr2fm6cfetj8[5] = "pr{}";
const uint8_t gm_smraeij5q6ubc[11] = "parameter ";
const uint8_t gm_smsrao7h22muv[61] = "internal error: a conversion to a value that is not a vector";
const uint8_t gm_smuti066jdthl[24] = "{}/{}/{}/{}/{}/{}/{}/{}";
const uint8_t gm_smvgcg1j21t6v[27] = "use `}}` for a literal `}`";
const uint8_t gm_sn449k8j7bj3v[51] = "`@stack_size` takes one function: `@stack_size(f)`";
const uint8_t gm_sn88c2jg5b9gv[4] = "`--";
const uint8_t gm_snb8htg7a7p5u[16] = "identifier `{}`";
const uint8_t gm_snbn61h56c5fj[52] = "__atomic_compare_exchange_n({}, &{}, {}, 0, {}, {})";
const uint8_t gm_snftqbg249ljm[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\014\002\000\000:\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sngdu4l8vh3l8[5] = "-not";
const uint8_t gm_snh86nvrv0us3[19] = "`not not x` is `x`";
const uint8_t gm_snhmu20ccir10[16] = "missing field `";
const uint8_t gm_snlpcspgan3ri[16] = "(({})[{}] != 0)";
const uint8_t gm_sno2v4rqn9iav[61] = "`#{}` makes {} of an array type with a constant length: `{}`";
const uint8_t gm_snokcvhtidt97[6] = "_Bool";
const uint8_t gm_so3t10be8g68d[10] = "gm_ga{}()";
const uint8_t gm_so78814tnt5g5[2] = "a";
const uint8_t gm_so7em1q8clnib[8] = "a label";
const uint8_t gm_sod9r7r024h9q[6] = " bits";
const uint8_t gm_soefp7bosmvt1[12] = "remove `()`";
const uint8_t gm_soo0b43e4t7sn[12] = "layout of `";
const uint8_t gm_sopacnadlsnva[99] = "`@asm` takes a template, outputs, inputs and clobbers: `@asm(template, outputs, inputs, clobbers)`";
const uint8_t gm_sou5f0qr4vdgr[8] = "core/{}";
const uint8_t gm_sovqp4emsh0q8[5] = "_.+-";
const uint8_t gm_sp02qqgucmpt7[3] = " (";
const uint8_t gm_sp0hmouoaumsl[10] = " = GM_LD(";
const uint8_t gm_sp0kv61jd1btm[5] = "-l{}";
const uint8_t gm_sp6l5tr58ip76[23] = "this stops the program";
const uint8_t gm_spchsf3taendr[3] = "\\f";
const uint8_t gm_spdkbvjffkg4b[11] = ") < GM_LD(";
const uint8_t gm_speaekk0qc740[9] = "gm_cv_u_";
const uint8_t gm_spfe58u39phsl[21] = "cannot infer which `";
const uint8_t gm_spnqca4mofck7[6] = " - 1)";
const uint8_t gm_spnr4ftjbm9m9[61] = "internal error: a conversion of a value that is not a vector";
const uint8_t gm_sps1dthki4jpu[50] = "cannot ask the C compiler `{}` for its target:\012{}";
const uint8_t gm_spsjkc9sqdvl6[61] = "dependency `{}` of `{}`: the path `{}` leaves the repository";
const uint8_t gm_spts1f8sg22km[18] = ")) gm_trap_index(";
const uint8_t gm_spu32ed945op3[49] = "gm_bt B = { gm_bt_top \077 gm_bt_top->prev : NULL, ";
const uint8_t gm_sq0573h7j28rq[23] = "invalid profile name `";
const uint8_t gm_sq12h87ngj5nl[9] = ", true);";
const uint8_t gm_sq9f472jpmh59[64] = "`else` needs an optional or a result on its left, but this is `";
const uint8_t gm_sq9f8k00ocil6[35] = "` is private to its module (\302\2473.3)";
const uint8_t gm_sq9pusqm0kq2f[7] = "gm_out";
const uint8_t gm_sqa5rf17c57e4[5] = "`{}`";
const uint8_t gm_sqaji2d91cvct[19] = "; const void *argp";
const uint8_t gm_sqc0827akcmi8[40] = "this loop never ends: it has no `break`";
const uint8_t gm_sqhghq789nvm0[8] = "decimal";
const uint8_t gm_sqj9769nr8pm0[48] = "numeric field names may not have a leading zero";
const uint8_t gm_sqlorrkumh3an[23] = "` is not an identifier";
const uint8_t gm_sqmqlaupa64jq[7] = "cursor";
const uint8_t gm_sqqaphhmqv39s[5] = "&a{}";
const uint8_t gm_sqqdirgtqa30r[8] = ") == 0)";
const uint8_t gm_sqslcbfraugjg[8] = " type `";
const uint8_t gm_sr00p3fdg5qn3[19] = " does not exist: `";
const uint8_t gm_sr1bm2v312fv6[15] = "UpperCamelCase";
const uint8_t gm_sr43s52rmecnf[33] = "`cache clean` takes no arguments";
const uint8_t gm_sr462lclo52fq[8] = ": \342\200\246)`";
const uint8_t gm_sr87tgv3ltq65[13] = "type-name {}";
const uint8_t gm_srav7444q331a[37] = "` cannot be constructed outside its ";
const uint8_t gm_srb1158jlk9u7[75] = "`#callconv` applies only to a function, a foreign function or a `^fn` type";
const uint8_t gm_sreop6h89ulb0[18] = " must be a string";
const uint8_t gm_srfagufvviad7[6] = "bench";
const uint8_t gm_srhms7dhd79bq[15] = "gm_clock_sleep";
const uint8_t gm_srjdrn9cldsu5[57] = "cannot read `{}`: No such file or directory (os error 2)";
const uint8_t gm_srq72vm6d96b6[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000I\001\000\000\021\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sru9l3qtrdjko[84] = "constant contains a pointer to memory that does not outlive compile-time evaluation";
const uint8_t gm_srv05plrkjhf8[7] = "  std\012";
const uint8_t gm_ss3kah49g222u[33] = "(void)env; (void)cx; (void)inst;";
const uint8_t gm_ss3op0i298nem[69] = "void *env = fb->env; gm_node *cx = fb->cx; gm_node *inst = fb->inst;";
const uint8_t gm_ss7moisvq3l8h[6] = " is `";
const uint8_t gm_ssd7h3tuipgq5[55] = "` is not available on vectors: a lane cannot be `null`";
const uint8_t gm_sse5id8rmv069[8] = ", and `";
const uint8_t gm_ssi8bk0pvfr56[22] = "`{}` cannot be called";
const uint8_t gm_ssj1a8k9nspq4[82] = "attributes here must precede a `struct`, a `union`, an array type or a `^fn` type";
const uint8_t gm_ssnb4or831s5l[78] = "`#soa` precedes an array or slice type of a struct: `#soa [N]S` or `#soa []S`";
const uint8_t gm_ssob5j6sas4h7[58] = "The runtime of programs compiled by the Gamma C back end.";
const uint8_t gm_st2s0ov9emddj[12] = "stable_sort";
const uint8_t gm_st2vlvkf674c2[250] = "`; they have `reduce_sum`, `reduce_min`, `reduce_max`, `min`, `max`, `abs`, `to`, `shuffle`, `store` and the comparisons `lanes_eq`, `lanes_ne`, `lanes_lt`, `lanes_le`, `lanes_gt` and `lanes_ge`, and masks `all`, `any`, `count` and `select` (\302\24721.3)";
const uint8_t gm_st518h9bgt9n7[22] = ", &ml, sizeof ml); }\012";
const uint8_t gm_st7d4oeklkoma[11] = "gm_rt_lock";
const uint8_t gm_stco696vp9tto[14] = "gm_buf_free(&";
const uint8_t gm_std2slsc0ssrl[11] = "gm_hb_slot";
const uint8_t gm_std59a9b6lpnn[10] = "ReduceSum";
const uint8_t gm_stdefoi5sk91c[12] = "ProcReadOut";
const uint8_t gm_stfhv28imtfkb[6] = "goto ";
const uint8_t gm_stjlu12rqiie7[13] = "--speed-time";
const uint8_t gm_stk678hojgu6q[24] = "from_tag on a non-union";
const uint8_t gm_stkh9r1ip5q2v[35] = "indexing this type at compile time";
const uint8_t gm_stmdki8kna56a[24] = "` is a foreign function";
const uint8_t gm_stpnhgp46l3tu[9] = "wasm-opt";
const uint8_t gm_strsld72u30ds[9] = "(double)";
const uint8_t gm_su01gdacg3ppp[9] = "_f[] = {";
const uint8_t gm_su1mil1q3eqgp[16] = "((uint8_t *){})";
const uint8_t gm_su61qr04nq335[13] = "gm_atag_used";
const uint8_t gm_su635cbqj0rpd[3] = " \077";
const uint8_t gm_su6s568b7b4kr[5] = "wn{}";
const uint8_t gm_sub1g4k5l4c07[11] = "into_slice";
const uint8_t gm_subcm6amchfvv[5] = "ULL)";
const uint8_t gm_sucs0qmdapqe8[41] = "expected a value, found the effect set `";
const uint8_t gm_suk7nc9hc39vu[13] = "parse_string";
const uint8_t gm_sup2jn5hml593[6] = "ident";
const uint8_t gm_suuphrufuvh4m[12] = "GM_LONG_FN ";
const uint8_t gm_sv0pi8lprf2k8[55] = "` is not a target triple (such as `aarch64-linux-gnu`)";
const uint8_t gm_sv5o4caljvbmj[28] = "extra `=`, expected nothing";
const uint8_t gm_svaibtfuu0gf2[48] = "gamma: cannot create a temporary directory in `";
const uint8_t gm_svfa039r0e6te[33] = "invalid {} string, expected `{}`";
const uint8_t gm_svgn73l5s66lk[7] = "wasm32";
const uint8_t gm_svhavo13qha5k[4] = ")((";
const uint8_t gm_svkkls864r4t4[30] = "` does not fit the tag type `";
const uint8_t gm_svklio0qebpog[9] = "constant";
const uint8_t gm_svkrg5dctlh9m[7] = "{}[{}]";
const uint8_t gm_svl4l2v56ma30[8] = "effect ";
const uint8_t gm_svln5ui59rjil[6] = "Other";
const uint8_t gm_svmck4pckkpai[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\0002\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_svmo2cukmep6f[9] = "{}__size";
const uint8_t gm_svom2l7bom2pm[71] = "a variable declared with `:=` that is never assigned, so `::` would do";
const uint8_t gm_svtut88rj5dr8[15] = "gamma: `gamma ";
const uint8_t gm_svu2guaq187mi[9] = "asyncify";
uint8_t gm_c0fhj0gi05s[2] __attribute__((aligned(1)));
uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
uint8_t gm_c2t4q8gsqsl[3] __attribute__((aligned(1)));
uint8_t gm_c32pk0vs0kc[5] __attribute__((aligned(1)));
uint8_t gm_c3i8biff6gm[11] __attribute__((aligned(1)));
uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
uint8_t gm_c4f2cgckmo6[7] __attribute__((aligned(1)));
uint8_t gm_c50rfu0mliv[4000] __attribute__((aligned(8)));
uint8_t gm_c54lbva5qt3[10] __attribute__((aligned(1)));
uint8_t gm_c5v8jddvmh5[13] __attribute__((aligned(1)));
uint8_t gm_c6iptqebh84[6] __attribute__((aligned(1)));
uint8_t gm_c708i8j8tcn[6] __attribute__((aligned(1)));
uint8_t gm_c7bojpcss70[13] __attribute__((aligned(1)));
uint8_t gm_c8ps00qu8ur[9] __attribute__((aligned(1)));
uint8_t gm_c8r6sk2ruhm[7] __attribute__((aligned(1)));
uint8_t gm_ca34i8s7nv3[10] __attribute__((aligned(1)));
uint8_t gm_cas4sbdf6ub[12] __attribute__((aligned(1)));
uint8_t gm_cbe5loot5ou[9] __attribute__((aligned(1)));
uint8_t gm_cbvjitbgoki[16] __attribute__((aligned(1)));
uint8_t gm_cc8mmf5go3e[4] __attribute__((aligned(1)));
uint8_t gm_cch1hm6o00s[13] __attribute__((aligned(1)));
uint8_t gm_ccimdvgv2uo[5] __attribute__((aligned(1)));
uint8_t gm_cd923jeri8l[13] __attribute__((aligned(1)));
uint8_t gm_cdvl4a09e32[18] __attribute__((aligned(1)));
uint8_t gm_ceivjfkfaf1[5] __attribute__((aligned(1)));
uint8_t gm_cenbs8unhg6[4] __attribute__((aligned(1)));
uint8_t gm_ceperl1a3fj[5] __attribute__((aligned(1)));
uint8_t gm_cfj1ctfn4pv[16] __attribute__((aligned(8)));
uint8_t gm_cgmt38nbmrp[17] __attribute__((aligned(1)));
uint8_t gm_ch5ouqda8cs[51] __attribute__((aligned(1)));
uint8_t gm_ch70o9r96kg[16] __attribute__((aligned(1)));
uint8_t gm_ci404lai93v[10] __attribute__((aligned(1)));
uint8_t gm_ci7lmabjiao[2] __attribute__((aligned(1)));
uint8_t gm_ci8na0i4d9p[15] __attribute__((aligned(1)));
uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
uint8_t gm_cislt4mt8ol[12] __attribute__((aligned(1)));
uint8_t gm_cj99fe4p60v[9] __attribute__((aligned(1)));
uint8_t gm_cjbp1kou1j6[11] __attribute__((aligned(1)));
uint8_t gm_ck2s18je8ki[4] __attribute__((aligned(1)));
uint8_t gm_clj49qvgrpn[8] __attribute__((aligned(1)));
uint8_t gm_cm7u9m3ofdj[10] __attribute__((aligned(1)));
uint8_t gm_cm9aodfhip7[5] __attribute__((aligned(1)));
uint8_t gm_cn0e8uksoq0[7] __attribute__((aligned(1)));
uint8_t gm_cnb3jfedmgn[3] __attribute__((aligned(1)));
uint8_t gm_cnfml933dkb[2] __attribute__((aligned(1)));
uint8_t gm_cnlt8qov7cb[16] __attribute__((aligned(1)));
uint8_t gm_cnue4rlljrd[13] __attribute__((aligned(1)));
uint8_t gm_co0v8nc5lqu[13] __attribute__((aligned(1)));
uint8_t gm_coqlk8uvhm0[2] __attribute__((aligned(1)));
uint8_t gm_cormelbvjcp[9] __attribute__((aligned(1)));
uint8_t gm_cp1bmt1lpnn[15] __attribute__((aligned(1)));
uint8_t gm_cpirdtbcd2f[4] __attribute__((aligned(1)));
uint8_t gm_cq9lk6ce28a[4] __attribute__((aligned(1)));
uint8_t gm_cr9tkd1skqs[4] __attribute__((aligned(1)));
uint8_t gm_crb4pqq2igg[6] __attribute__((aligned(1)));
uint8_t gm_crh3j0ho75u[12] __attribute__((aligned(1)));
uint8_t gm_crthrtmpk9i[2] __attribute__((aligned(1)));
uint8_t gm_ctapjbcspvf[4] __attribute__((aligned(1)));
uint8_t gm_ctd4c52p0f8[60] __attribute__((aligned(1)));
uint8_t gm_cthm7vq6g2f[13] __attribute__((aligned(1)));
uint8_t gm_ctkboukdglo[8] __attribute__((aligned(8)));
uint8_t gm_cugpmqkho2v[16] __attribute__((aligned(1)));
uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
uint8_t gm_k5u3oseesd7[40] __attribute__((aligned(8)));
uint8_t gm_k6r9cr7441u[40] __attribute__((aligned(8)));
uint8_t gm_k8b8p636a5m[40] __attribute__((aligned(8)));
uint8_t gm_k9o17uvn1g2[40] __attribute__((aligned(8)));
uint8_t gm_kau7m62eplk[40] __attribute__((aligned(8)));
uint8_t gm_kbamu2ordri[40] __attribute__((aligned(8)));
uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
uint8_t gm_kebr2u4173v[40] __attribute__((aligned(8)));
uint8_t gm_khibdd7i0ml[40] __attribute__((aligned(8)));
uint8_t gm_khrljgb4g9j[40] __attribute__((aligned(8)));
uint8_t gm_klh5uk0bvu4[40] __attribute__((aligned(8)));
uint8_t gm_knj30d2h126[40] __attribute__((aligned(8)));
uint8_t gm_kpc4646t5v3[40] __attribute__((aligned(8)));
uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
void *const gm_hdvslis4o40u[] = {(void *)gm_dtvslis4o40u_0};
void *const gm_hl6s5gct9vkh[] = {(void *)gm_f_for_body_terksaa};
