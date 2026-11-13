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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_th2lqbo77pvsc f7; } gm_tk50ot32aqf1o;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint64_t f0; gm_tk50ot32aqf1o f1; } gm_tdvdcnci326i5;
_Static_assert(sizeof(gm_tk50ot32aqf1o) == 136 && _Alignof(gm_tk50ot32aqf1o) == 8 && offsetof(gm_tk50ot32aqf1o, f0) == 0 && offsetof(gm_tk50ot32aqf1o, f1) == 8 && offsetof(gm_tk50ot32aqf1o, f2) == 24 && offsetof(gm_tk50ot32aqf1o, f3) == 40 && offsetof(gm_tk50ot32aqf1o, f4) == 64 && offsetof(gm_tk50ot32aqf1o, f5) == 80 && offsetof(gm_tk50ot32aqf1o, f6) == 96 && offsetof(gm_tk50ot32aqf1o, f7) == 112, "layout of `Decl`");
_Static_assert(sizeof(gm_tdvdcnci326i5) == 144 && _Alignof(gm_tdvdcnci326i5) == 8 && offsetof(gm_tdvdcnci326i5, f0) == 0 && offsetof(gm_tdvdcnci326i5, f1) == 8, "layout of `(i: usize, x: Decl)`");
struct gm_fr_declaration_ids_8ptbmk3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* seen */
    gm_tk50ot32aqf1o l4; /* d */
    gm_trqfvmjtt4sij l5; /* base */
    gm_b16_8 ret;
};
struct gm_fr_html_declaration_0tb9vv8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_html_index_rfen8qq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* body */
    gm_tud7oovfvnf43 l5; /* m */
    gm_b16_8 ret;
};
struct gm_fr_html_module_aegjhhp {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* body */
    gm_tk50ot32aqf1o l11; /* d */
    gm_tk50ot32aqf1o l19; /* d */
    gm_b16_8 ret;
};
struct gm_fr_html_page_121hit7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqh04os15sq2(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_declaration_ids_8ptbmk3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_into_fhlr21n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_ius7uca(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ru8569b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_html_declaration_0tb9vv8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_html_index_rfen8qq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_html_module_aegjhhp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_html_page_121hit7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_inline_into_arphbtu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_fmcurbp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_into_o6canda(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_section_9ecia9e(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_summary_18md0dp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unwrap_lines_59i2isg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_url_path_ul3c89a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7iu999j(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dk2c2kf(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_epd64m2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hcp79lf(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qgsuhvi(void * a0, gm_tnpifv20irnph a1);
extern uint8_t gm_cpm8oqgl16t[10] __attribute__((aligned(1)));
extern uint8_t gm_cssal36m00k[16] __attribute__((aligned(8)));
extern const uint8_t gm_s01aerdf23b33[14];
extern const uint8_t gm_s3154a2sfmm32[18];
extern const uint8_t gm_s7ss5dueeuc9a[5];
extern const uint8_t gm_s8ei36h3rj566[14];
extern const uint8_t gm_s9um3j04r7jaj[7];
extern const uint8_t gm_sapu6l8enc7d7[7];
extern const uint8_t gm_scv3jvaidk3vn[21];
extern const uint8_t gm_sel2d9vtaa0ba[19];
extern const uint8_t gm_serd0h8povggs[12];
extern const uint8_t gm_set8ntrenb1qd[18];
extern const uint8_t gm_sfq6b6v45hv1v[64];
extern const uint8_t gm_sfvci5f269lfp[13];
extern const uint8_t gm_sgrv5qufpkvf8[7];
extern const uint8_t gm_shkhe2ptevja3[18];
extern const uint8_t gm_skesaudrffe2q[55];
extern const uint8_t gm_sl24u2f29gnqn[25];
extern const uint8_t gm_sld7tlsq8opg9[15];
extern const uint8_t gm_soashnodb7lsu[23];
extern const uint8_t gm_spsbmlrti644m[11];
extern const uint8_t gm_sqf9pgeqvvbb4[77];
extern const char gm_srcm2bronu107ea[];
extern const uint8_t gm_srplcc0p49iui[9];
extern const uint8_t gm_ssrq4cnmtqbuh[83];
extern const uint8_t gm_svj2mudio0eed[18];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b16_8 gm_f_declaration_ids_8ptbmk3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_declaration_ids_8ptbmk3 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    uint8_t gm_l9; /* <tmp> */
    uint8_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* id */
    uint32_t gm_l13; /* n */
    GM_STACK_CHECK(&F, 759u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_epvducg((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 48));
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
                    gm_tk50ot32aqf1o t16 = GM_LD(gm_tk50ot32aqf1o, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 136));
                    F.l4 = t16;
                    {
                        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            gm_l6 = t19;
                        }
                        {
                            gm_l7 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t21 = gm_l7;
                                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                uint8_t t23 = (t21 < t22);
                                if (!t23) goto lb20;
                                {
                                    uint64_t t26 = gm_l7;
                                    uint64_t t27 = (uintptr_t)t26;
                                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t27 * 1));
                                    uint8_t t30 = t29;
                                    uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)&t30));
                                    gm_l8 = t33;
                                    {
                                        uint8_t t36 = gm_l8;
                                        uint8_t t37 = (uint8_t)(t36 & ((uint8_t)192ULL));
                                        uint8_t t38 = ((t37 == ((uint8_t)128ULL)));
                                        if (!t38) goto ie35;
                                        {
                                            goto lc20;
                                        }
                                        goto ix35;
ie35:;
ix35:;
                                    }
                                    {
                                        void * t40 = (void *)((uint8_t *)&F.l5);
                                        uint8_t t41;
                                        uint8_t t43 = 0;
                                        uint8_t t44 = 0;
                                        uint8_t t45 = 0;
                                        {
                                            uint8_t t47 = gm_l8;
                                            gm_l9 = t47;
                                        }
                                        uint8_t t48 = 0;
                                        uint8_t t49 = gm_l9;
                                        uint8_t t50 = (((uint8_t)97ULL) <= t49);
                                        if (t50) {
                                            uint8_t t51 = gm_l9;
                                            uint8_t t52 = (t51 <= ((uint8_t)122ULL));
                                            t48 = t52;
                                        } else t48 = 0;
                                        uint8_t t53 = t48;
                                        if (!t53) {
                                            {
                                                uint8_t t55 = gm_l8;
                                                gm_l10 = t55;
                                            }
                                            uint8_t t56 = 0;
                                            uint8_t t57 = gm_l10;
                                            uint8_t t58 = (((uint8_t)65ULL) <= t57);
                                            if (t58) {
                                                uint8_t t59 = gm_l10;
                                                uint8_t t60 = (t59 <= ((uint8_t)90ULL));
                                                t56 = t60;
                                            } else t56 = 0;
                                            uint8_t t61 = t56;
                                            t45 = t61;
                                        } else t45 = 1;
                                        if (!t45) {
                                            {
                                                uint8_t t63 = gm_l8;
                                                gm_l11 = t63;
                                            }
                                            uint8_t t64 = 0;
                                            uint8_t t65 = gm_l11;
                                            uint8_t t66 = (((uint8_t)48ULL) <= t65);
                                            if (t66) {
                                                uint8_t t67 = gm_l11;
                                                uint8_t t68 = (t67 <= ((uint8_t)57ULL));
                                                t64 = t68;
                                            } else t64 = 0;
                                            uint8_t t69 = t64;
                                            t44 = t69;
                                        } else t44 = 1;
                                        if (!t44) {
                                            uint8_t t70 = gm_l8;
                                            uint8_t t71 = ((t70 == ((uint8_t)95ULL)));
                                            t43 = t71;
                                        } else t43 = 1;
                                        if (!t43) goto ie42;
                                        uint8_t t73 = gm_l8;
                                        uint8_t t74 = t73;
                                        t41 = t74;
                                        goto ix42;
ie42:;
                                        uint8_t t76 = ((uint8_t)45ULL);
                                        t41 = t76;
ix42:;
                                        gm_tftb1odc739t5 t77 = gm_f_push_t83rg2m(t40, t41);
                                        gm_tftb1odc739t5 t78 = t77;
                                        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcm2bronu107ea, 82, 13);
                                        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                                    }
                                    goto me31;
mn32:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 80, 9);
me31:;
                                }
lc20:;
                                {
                                    uint64_t t81 = gm_l7;
                                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                                    uint64_t t84 = r82;
                                    gm_l7 = t84;
                                }
                            }
lb20:;
                        }
                    }
                    {
                        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t86 = t85;
                        gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                        gm_l12 = t87;
                    }
                    {
                        gm_l13 = ((uint32_t)2ULL);
                    }
                    {
                        for (;;) {
                            uint8_t t89 = 0;
                            gm_b16_8 t90 = gm_l12;
                            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                            gm_b16_8 t92 = t91;
                            uint8_t t93 = 0;
                            gm_b16_8 t94 = t90;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t92) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t92))) + i * 16, ((uint8_t *)&t94))) { t93 = 1; break; }
                            uint8_t t95 = (uint8_t)(t93);
                            if (!t95) {
                                uint8_t t96 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_srplcc0p49iui, 8) == 0));
                                t89 = t96;
                            } else t89 = 1;
                            if (!t89) goto lb88;
                            {
                                gm_tommf4j9upomc t99;
                                memset((uint8_t *)&t99 + 20, 0, 4);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t99 + 0, t100);
                                uint32_t t101 = gm_l13;
                                GM_ST(uint32_t, (uint8_t *)&t99 + 16, t101);
                                gm_tabb710d9mq2e t102;
                                gm_f_format_ius7uca((void *)&t102, (void *)NULL, cx, NULL, &t99);
                                if (gm_unw.kind) {
                                    goto gm_ll97;
                                }
                                gm_tabb710d9mq2e t103 = t102;
                                if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srcm2bronu107ea, 87, 18);
                                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
                                gm_b16_8 t105 = t104;
                                gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                gm_l12 = t106;
                            }
                            {
                                uint32_t t107 = gm_l13;
                                uint32_t r108; bool o109 = __builtin_add_overflow(t107, ((uint32_t)1ULL), &r108);
                                if (o109) gm_trap("integer overflow in `+`", gm_srcm2bronu107ea, 88, 13);
                                uint32_t t110 = r108;
                                gm_l13 = t110;
                            }
lc88:;
                        }
gm_ll97:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb88; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc88; }
                        }
                        goto gm_ll9;
lb88:;
                    }
                    {
                        void * t111 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t112 = gm_l12;
                        gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
                        gm_tftb1odc739t5 t114 = t113;
                        if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srcm2bronu107ea, 90, 9);
                        gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 78, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t117 = gm_l3;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l3 = t120;
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
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t122 = 0;
        F.ret = t121;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_html_declaration_0tb9vv8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_html_declaration_0tb9vv8 F;
    void * gm_l0; /* out */
    void * gm_l1; /* d */
    gm_b16_8 gm_l2; /* id */
    gm_b16_8 gm_l3; /* doc */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* mem */
    GM_STACK_CHECK(&F, 674u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_tnpifv20irnph t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
        gm_tftb1odc739t5 t5 = gm_f_write_qgsuhvi(t2, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcm2bronu107ea, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = gm_l1;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 8));
        void * t10 = gm_l0;
        gm_b0_1 t11 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sl24u2f29gnqn));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(24));
        gm_tftb1odc739t5 t14 = gm_f_extend_0cs85gs(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcm2bronu107ea, 98, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        void * t17 = gm_l1;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 24));
        void * t19 = gm_l0;
        gm_b0_1 t20 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sld7tlsq8opg9));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
        gm_tftb1odc739t5 t24 = t23;
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcm2bronu107ea, 100, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        void * t27 = gm_l1;
        if (!(((((uint8_t *)t27) + 40))[16] != 0)) goto ie26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 40));
        gm_l3 = t28;
        {
            gm_b16_8 t30 = gm_l3;
            void * t31 = gm_l0;
            gm_b0_1 t32 = gm_f_render_into_o6canda((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = gm_l1;
        uint64_t t35 = GM_LD(uintptr_t, (((uint8_t *)t34) + 80) + 8);
        uint8_t t36 = (t35 > ((uint64_t)0ULL));
        if (!t36) goto ie33;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l1;
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t39) + 64));
            gm_tnpifv20irnph t41;
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
            gm_tftb1odc739t5 t42 = gm_f_write_dk2c2kf(t38, t41);
            gm_tftb1odc739t5 t43 = t42;
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcm2bronu107ea, 103, 9);
            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
        }
        {
            {
                void * t46 = gm_l1;
                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 80));
                gm_l4 = t47;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t49 = gm_l5;
                    uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t51 = (t49 < t50);
                    if (!t51) goto lb48;
                    {
                        uint64_t t54 = gm_l5;
                        uint64_t t55 = (uintptr_t)t54;
                        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t6j6smj7qoctu t59 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t55 * 32));
                        gm_l6 = t59;
                        {
                            void * t61 = gm_l0;
                            gm_b16_8 t62;
                            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_spsbmlrti644m));
                            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(10));
                            gm_tftb1odc739t5 t63 = gm_f_extend_0cs85gs(t61, t62);
                            gm_tftb1odc739t5 t64 = t63;
                            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_srcm2bronu107ea, 105, 13);
                            gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                        }
                        {
                            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                            void * t67 = gm_l0;
                            gm_b0_1 t68 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t66, t67);
                            if (gm_unw.kind) {
                                goto gm_ll52;
                            }
                        }
                        {
                            void * t69 = gm_l0;
                            gm_b16_8 t70;
                            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_svj2mudio0eed));
                            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(17));
                            gm_tftb1odc739t5 t71 = gm_f_extend_0cs85gs(t69, t70);
                            gm_tftb1odc739t5 t72 = t71;
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcm2bronu107ea, 107, 13);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
                        {
                            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                            gm_b16_8 t75 = gm_f_unwrap_lines_59i2isg((void *)NULL, cx, NULL, t74);
                            if (gm_unw.kind) {
                                goto gm_ll52;
                            }
                            void * t76 = gm_l0;
                            gm_b0_1 t77 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_ll52;
                            }
                        }
                        {
                            void * t78 = gm_l0;
                            gm_b16_8 t79;
                            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sapu6l8enc7d7));
                            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(6));
                            gm_tftb1odc739t5 t80 = gm_f_extend_0cs85gs(t78, t79);
                            gm_tftb1odc739t5 t81 = t80;
                            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcm2bronu107ea, 109, 13);
                            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                        }
                        goto me57;
mn58:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 104, 9);
me57:;
                    }
lc48:;
                    {
                        uint64_t t84 = gm_l5;
                        uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                        uint64_t t87 = r85;
                        gm_l5 = t87;
                    }
                }
gm_ll52:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
                }
                goto gm_lad;
lb48:;
            }
        }
        {
            void * t88 = gm_l0;
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sgrv5qufpkvf8));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
            gm_tftb1odc739t5 t90 = gm_f_extend_0cs85gs(t88, t89);
            gm_tftb1odc739t5 t91 = t90;
            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcm2bronu107ea, 111, 9);
            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
        }
        goto ix33;
ie33:;
ix33:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_html_index_rfen8qq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_html_index_rfen8qq F;
    gm_b16_8 gm_l0; /* title */
    gm_b16_8 gm_l1; /* modules */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 638u);
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
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7ss5dueeuc9a));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcm2bronu107ea, 118, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_b16_8 t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t9 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_ssrq4cnmtqbuh));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(82));
        gm_tftb1odc739t5 t12 = gm_f_extend_0cs85gs(t10, t11);
        gm_tftb1odc739t5 t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcm2bronu107ea, 120, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        {
            gm_b16_8 t16 = gm_l1;
            gm_l3 = t16;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l4;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l4;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tud7oovfvnf43 t28 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 80));
                    F.l5 = t28;
                    {
                        void * t30 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t31;
                        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_set8ntrenb1qd));
                        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(17));
                        gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                        gm_tftb1odc739t5 t33 = t32;
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcm2bronu107ea, 122, 9);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                    }
                    {
                        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 16));
                        gm_b16_8 t36 = gm_f_url_path_ul3c89a((void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        void * t37 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t38 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t36, t37);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t40;
                        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s01aerdf23b33));
                        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(13));
                        gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
                        gm_tftb1odc739t5 t42 = t41;
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcm2bronu107ea, 124, 9);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    {
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        void * t45 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t46 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t44, t45);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                    }
                    {
                        void * t47 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t48;
                        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_scv3jvaidk3vn));
                        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(20));
                        gm_tftb1odc739t5 t49 = gm_f_extend_0cs85gs(t47, t48);
                        gm_tftb1odc739t5 t50 = t49;
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srcm2bronu107ea, 126, 9);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    {
                        void * t52 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t53 = gm_f_summary_18md0dp((void *)NULL, cx, NULL, t52);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        void * t54 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t55 = gm_f_inline_into_arphbtu((void *)NULL, cx, NULL, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                    }
                    {
                        void * t56 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t57;
                        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_serd0h8povggs));
                        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(11));
                        gm_tftb1odc739t5 t58 = gm_f_extend_0cs85gs(t56, t57);
                        gm_tftb1odc739t5 t59 = t58;
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcm2bronu107ea, 128, 9);
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 121, 5);
me26:;
                }
lc17:;
                {
                    uint64_t t62 = gm_l4;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l4 = t65;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        void * t66 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sel2d9vtaa0ba));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
        gm_tftb1odc739t5 t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcm2bronu107ea, 130, 5);
        gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
    }
    {
        gm_b16_8 t71 = gm_l0;
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t73 = t72;
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_b16_8 t75 = gm_f_html_page_121hit7((void *)NULL, cx, NULL, t71, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = 0;
        F.ret = t75;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_html_module_aegjhhp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_html_module_aegjhhp F;
    void * gm_l0; /* m */
    gm_th2lqbo77pvsc gm_l1; /* index */
    gm_b16_8 gm_l3; /* link */
    gm_b16_8 gm_l4; /* ids */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l7; /* kind */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l14; /* kind */
    uint8_t gm_l15; /* found */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_b16_8 gm_l20; /* title */
    GM_STACK_CHECK(&F, 2297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3154a2sfmm32));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcm2bronu107ea, 38, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = gm_l0;
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 0));
        void * t9 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t10 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s8ei36h3rj566));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(13));
        gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
        gm_tftb1odc739t5 t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcm2bronu107ea, 40, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie16;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l3 = t17;
        {
            void * t19 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sfvci5f269lfp));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(12));
            gm_tftb1odc739t5 t21 = gm_f_extend_0cs85gs(t19, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcm2bronu107ea, 42, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            gm_b16_8 t24 = gm_l3;
            void * t25 = (void *)((uint8_t *)&F.l2);
            gm_b0_1 t26 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t27 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_soashnodb7lsu));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(22));
            gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
            gm_tftb1odc739t5 t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcm2bronu107ea, 44, 9);
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 32) + 8);
        uint8_t t35 = (t34 > ((uint64_t)0ULL));
        if (!t35) goto ie32;
        {
            void * t37 = (void *)((uint8_t *)&F.l2);
            void * t38 = gm_l0;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 32));
            gm_b16_8 t40 = gm_f_render_fmcurbp((void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t37, t40);
            gm_tftb1odc739t5 t42 = t41;
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcm2bronu107ea, 46, 24);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t44 = gm_l0;
        gm_b16_8 t45 = gm_f_declaration_ids_8ptbmk3((void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t45;
    }
    {
        void * t47 = gm_l0;
        uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)t47) + 48) + 8);
        uint8_t t49 = (t48 > ((uint64_t)0ULL));
        if (!t49) goto ie46;
        {
            void * t51 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_skesaudrffe2q));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(54));
            gm_tftb1odc739t5 t53 = gm_f_extend_0cs85gs(t51, t52);
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcm2bronu107ea, 49, 9);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            {
                void * t57 = ((uint8_t *)gm_cpm8oqgl16t);
                if ((uintptr_t)0 > (uintptr_t)10 || (uintptr_t)10 > (uintptr_t)10) gm_trap_slice((uintptr_t)0, (uintptr_t)10, 10, gm_srcm2bronu107ea, 50, 9);
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t57 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)((uintptr_t)10 - (uintptr_t)0));
                gm_l5 = t58;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t60 = gm_l6;
                    uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t62 = (t60 < t61);
                    if (!t62) goto lb59;
                    {
                        uint64_t t65 = gm_l6;
                        uint64_t t66 = (uintptr_t)t65;
                        uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t9qkh0rsooc8d t70 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t66 * 1));
                        gm_l7 = t70;
                        {
                            {
                                void * t73 = gm_l0;
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t73) + 48));
                                gm_b16_8 t75 = t74;
                                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                                gm_l8 = t76;
                            }
                            {
                                gm_l9 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t78 = gm_l9;
                                    uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    uint8_t t80 = (t78 < t79);
                                    if (!t80) goto lb77;
                                    {
                                        gm_tdvdcnci326i5 t83;
                                        uint64_t t84 = gm_l9;
                                        GM_ST(uint64_t, (uint8_t *)&t83 + 0, t84);
                                        uint64_t t85 = gm_l9;
                                        uint64_t t86 = (uintptr_t)t85;
                                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                        memcpy((uint8_t *)&t83 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t86 * 136), 136);
                                        gm_tdvdcnci326i5 t88 = t83;
                                        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&t88) + 0));
                                        gm_l10 = t91;
                                        gm_tk50ot32aqf1o t92 = GM_LD(gm_tk50ot32aqf1o, (((uint8_t *)&t88) + 8));
                                        F.l11 = t92;
                                        {
                                            gm_t9qkh0rsooc8d t95 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l11) + 0));
                                            gm_t9qkh0rsooc8d t96 = gm_l7;
                                            gm_t9qkh0rsooc8d t97 = t95;
                                            gm_t9qkh0rsooc8d t98 = t96;
                                            uint8_t t99 = (!gm_eqh04os15sq2(((uint8_t *)&t97), ((uint8_t *)&t98)));
                                            if (!t99) goto ie94;
                                            {
                                                goto lc77;
                                            }
                                            goto ix94;
ie94:;
ix94:;
                                        }
                                        {
                                            void * t101 = (void *)((uint8_t *)&F.l2);
                                            uint64_t t102 = gm_l10;
                                            uint64_t t103 = (uintptr_t)t102;
                                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                            if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcm2bronu107ea, 53, 58);
                                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t103 * 16));
                                            gm_tnpifv20irnph t106;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                                            gm_tftb1odc739t5 t107 = gm_f_write_hcp79lf(t101, t106);
                                            gm_tftb1odc739t5 t108 = t107;
                                            if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcm2bronu107ea, 53, 17);
                                            gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                        }
                                        {
                                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 8));
                                            void * t111 = (void *)((uint8_t *)&F.l2);
                                            gm_b0_1 t112 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t110, t111);
                                            if (gm_unw.kind) {
                                                goto gm_ll81;
                                            }
                                        }
                                        {
                                            void * t113 = (void *)((uint8_t *)&F.l2);
                                            gm_b16_8 t114;
                                            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_shkhe2ptevja3));
                                            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(17));
                                            gm_tftb1odc739t5 t115 = gm_f_extend_0cs85gs(t113, t114);
                                            gm_tftb1odc739t5 t116 = t115;
                                            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srcm2bronu107ea, 55, 17);
                                            gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                                        }
                                        goto me89;
mn90:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 51, 13);
me89:;
                                    }
lc77:;
                                    {
                                        uint64_t t119 = gm_l9;
                                        uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                                        uint64_t t122 = r120;
                                        gm_l9 = t122;
                                    }
                                }
gm_ll81:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
                                }
                                goto gm_ll63;
lb77:;
                            }
                        }
                        goto me68;
mn69:;
                        gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 50, 9);
me68:;
                    }
lc59:;
                    {
                        uint64_t t124 = gm_l6;
                        uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                        uint64_t t127 = r125;
                        gm_l6 = t127;
                    }
                }
gm_ll63:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
                }
                goto gm_lad;
lb59:;
            }
        }
        {
            void * t128 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s9um3j04r7jaj));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(6));
            gm_tftb1odc739t5 t130 = gm_f_extend_0cs85gs(t128, t129);
            gm_tftb1odc739t5 t131 = t130;
            if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srcm2bronu107ea, 58, 9);
            gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        {
            void * t134 = ((uint8_t *)gm_cpm8oqgl16t);
            if ((uintptr_t)0 > (uintptr_t)10 || (uintptr_t)10 > (uintptr_t)10) gm_trap_slice((uintptr_t)0, (uintptr_t)10, 10, gm_srcm2bronu107ea, 60, 5);
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(((uint8_t *)t134 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)((uintptr_t)10 - (uintptr_t)0));
            gm_l12 = t135;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t137 = gm_l13;
                uint64_t t138 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t139 = (t137 < t138);
                if (!t139) goto lb136;
                {
                    uint64_t t142 = gm_l13;
                    uint64_t t143 = (uintptr_t)t142;
                    uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t9qkh0rsooc8d t147 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t143 * 1));
                    gm_l14 = t147;
                    {
                        gm_l15 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t150 = gm_l0;
                            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)t150) + 48));
                            gm_b16_8 t152 = t151;
                            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                            gm_l16 = t153;
                        }
                        {
                            gm_l17 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t155 = gm_l17;
                                uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                uint8_t t157 = (t155 < t156);
                                if (!t157) goto lb154;
                                {
                                    gm_tdvdcnci326i5 t160;
                                    uint64_t t161 = gm_l17;
                                    GM_ST(uint64_t, (uint8_t *)&t160 + 0, t161);
                                    uint64_t t162 = gm_l17;
                                    uint64_t t163 = (uintptr_t)t162;
                                    uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    memcpy((uint8_t *)&t160 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t163 * 136), 136);
                                    gm_tdvdcnci326i5 t165 = t160;
                                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&t165) + 0));
                                    gm_l18 = t168;
                                    gm_tk50ot32aqf1o t169 = GM_LD(gm_tk50ot32aqf1o, (((uint8_t *)&t165) + 8));
                                    F.l19 = t169;
                                    {
                                        gm_t9qkh0rsooc8d t172 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l19) + 0));
                                        gm_t9qkh0rsooc8d t173 = gm_l14;
                                        gm_t9qkh0rsooc8d t174 = t172;
                                        gm_t9qkh0rsooc8d t175 = t173;
                                        uint8_t t176 = (!gm_eqh04os15sq2(((uint8_t *)&t174), ((uint8_t *)&t175)));
                                        if (!t176) goto ie171;
                                        {
                                            goto lc154;
                                        }
                                        goto ix171;
ie171:;
ix171:;
                                    }
                                    {
                                        uint8_t t179 = gm_l15;
                                        uint8_t t180 = (uint8_t)!t179;
                                        if (!t180) goto ie178;
                                        {
                                            void * t182 = (void *)((uint8_t *)&F.l2);
                                            gm_t9qkh0rsooc8d t183 = gm_l14;
                                            gm_b16_8 t184 = gm_f_section_9ecia9e(t183);
                                            gm_tnpifv20irnph t185;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                                            gm_tftb1odc739t5 t186 = gm_f_write_7iu999j(t182, t185);
                                            gm_tftb1odc739t5 t187 = t186;
                                            if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 1, gm_srcm2bronu107ea, 65, 17);
                                            gm_b0_1 t188 = GM_LD(gm_b0_1, (((uint8_t *)&t187) + 1));
                                        }
                                        {
                                            gm_l15 = ((uint8_t)1);
                                        }
                                        goto ix178;
ie178:;
ix178:;
                                    }
                                    {
                                        void * t189 = (void *)((uint8_t *)&F.l2);
                                        void * t190 = (void *)((uint8_t *)&F.l19);
                                        uint64_t t191 = gm_l18;
                                        uint64_t t192 = (uintptr_t)t191;
                                        uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                                        if (t192 >= (uintptr_t)(t193)) gm_trap_index(t192, t193, gm_srcm2bronu107ea, 68, 41);
                                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t192 * 16));
                                        gm_b0_1 t195 = gm_f_html_declaration_0tb9vv8((void *)NULL, cx, NULL, t189, t190, t194);
                                        if (gm_unw.kind) {
                                            goto gm_ll158;
                                        }
                                    }
                                    goto me166;
mn167:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 62, 9);
me166:;
                                }
lc154:;
                                {
                                    uint64_t t197 = gm_l17;
                                    uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                                    uint64_t t200 = r198;
                                    gm_l17 = t200;
                                }
                            }
gm_ll158:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb154; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc154; }
                            }
                            goto gm_ll140;
lb154:;
                        }
                    }
                    goto me145;
mn146:;
                    gm_trap("no match arm matched the scrutinee", gm_srcm2bronu107ea, 60, 5);
me145:;
                }
lc136:;
                {
                    uint64_t t202 = gm_l13;
                    uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                    uint64_t t205 = r203;
                    gm_l13 = t205;
                }
            }
gm_ll140:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb136; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc136; }
            }
            goto gm_lad;
lb136:;
        }
    }
    {
        void * t206 = gm_l0;
        gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)t206) + 0));
        gm_tnpifv20irnph t208;
        GM_ST(gm_b16_8, ((uint8_t *)&t208 + 0), t207);
        gm_tabb710d9mq2e t209;
        gm_f_format_ru8569b((void *)&t209, (void *)NULL, cx, NULL, t208);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t210 = t209;
        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 8, gm_srcm2bronu107ea, 71, 14);
        gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t210) + 8));
        gm_l20 = t211;
    }
    {
        gm_b16_8 t212 = gm_l20;
        gm_b16_8 t213 = t212;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
        gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t216 = t215;
        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
        gm_b16_8 t218 = gm_f_html_page_121hit7((void *)NULL, cx, NULL, t214, t217);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t219 = 0;
        F.ret = t218;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_html_page_121hit7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_html_page_121hit7 F;
    gm_b16_8 gm_l0; /* title */
    gm_b16_8 gm_l1; /* body */
    GM_STACK_CHECK(&F, 269u);
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
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfq6b6v45hv1v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
        gm_tftb1odc739t5 t4 = gm_f_extend_0cs85gs(t2, t3);
        gm_tftb1odc739t5 t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcm2bronu107ea, 28, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sqf9pgeqvvbb4));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(76));
        gm_tftb1odc739t5 t9 = gm_f_extend_0cs85gs(t7, t8);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcm2bronu107ea, 29, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        gm_b16_8 t12 = gm_l0;
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t14 = gm_f_escape_into_fhlr21n((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = (void *)((uint8_t *)&F.l2);
        gm_t6j6smj7qoctu t16;
        gm_b16_8 t17 = GM_LD(gm_b16_8, gm_cssal36m00k);
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t17);
        gm_b16_8 t18 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t18);
        gm_tftb1odc739t5 t19 = gm_f_write_epd64m2(t15, &t16);
        gm_tftb1odc739t5 t20 = t19;
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcm2bronu107ea, 31, 5);
        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
    }
    {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        uint8_t t25 = 0;
        F.ret = t24;
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
