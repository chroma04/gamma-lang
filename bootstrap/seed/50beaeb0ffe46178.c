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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tsoiri44d0i6q f0; uint8_t f1; uint8_t pe[3]; } gm_tqnlpelep1582;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
_Static_assert(sizeof(gm_tqnlpelep1582) == 12 && _Alignof(gm_tqnlpelep1582) == 4 && offsetof(gm_tqnlpelep1582, f0) == 0 && offsetof(gm_tqnlpelep1582, f1) == 8, "layout of `?ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tqnlpelep1582 v1; } f1; } gm_tr6norheal11n;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tr6norheal11n) == 16 && _Alignof(gm_tr6norheal11n) == 4 && offsetof(gm_tr6norheal11n, f0) == 0 && offsetof(gm_tr6norheal11n, f1) == 4, "layout of `IoError!?ExitStatus`");
gm_b0_1 gm_dt3ip07hju0a_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_1(void *env, gm_node *cx, gm_node *inst, void * a1, void * a2, gm_b16_8 a3, gm_b16_8 a4);
gm_b0_1 gm_dt3ip07hju0a_2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
gm_b0_1 gm_dt3ip07hju0a_4(void *env, gm_node *cx, gm_node *inst, void * a1, gm_b16_8 a2);
gm_b0_1 gm_dt3ip07hju0a_5(void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a1, gm_b16_8 a2);
gm_t62ebbi2i5rkd gm_dtaqemfrshlp_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, const void * a2);
gm_thf5kr0is9u9i gm_dtaqemfrshlp_1(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtaqemfrshlp_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
void gm_dtaqemfrshlp_3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_tlmjd3nndlf3r gm_dtaqemfrshlp_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_tr6norheal11n gm_dtaqemfrshlp_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_ttln0d0hd70en gm_dtaqemfrshlp_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ji90d8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ghum27l(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_c24kimhrphq[11] __attribute__((aligned(1)));
extern uint8_t gm_c26gi09a298[63] __attribute__((aligned(1)));
extern uint8_t gm_c2elhq7l2vf[4] __attribute__((aligned(1)));
extern uint8_t gm_c3a039uv4qb[16] __attribute__((aligned(1)));
extern uint8_t gm_c3nhitb2vug[10] __attribute__((aligned(1)));
extern uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_c4tkno4ihu4[3] __attribute__((aligned(1)));
extern uint8_t gm_c653lc0efgo[3] __attribute__((aligned(1)));
extern uint8_t gm_c6d5506jf7q[8] __attribute__((aligned(8)));
extern uint8_t gm_c6ef9harlnq[59] __attribute__((aligned(1)));
extern uint8_t gm_c6ojdgkpe75[3] __attribute__((aligned(1)));
extern uint8_t gm_c714gka9l4t[1] __attribute__((aligned(1)));
extern uint8_t gm_c7ivta2baoi[6] __attribute__((aligned(1)));
extern uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
extern uint8_t gm_c7p3n1736t1[4] __attribute__((aligned(4)));
extern uint8_t gm_c9b5effiaau[60] __attribute__((aligned(1)));
extern uint8_t gm_c9gbtic0o97[5] __attribute__((aligned(1)));
extern uint8_t gm_caafgbc6m0v[18] __attribute__((aligned(1)));
extern uint8_t gm_cai164r033u[16] __attribute__((aligned(8)));
extern uint8_t gm_cckdrlo8348[24] __attribute__((aligned(1)));
extern uint8_t gm_ce5dkit4sfq[12] __attribute__((aligned(1)));
extern uint8_t gm_cecimmkvaop[17] __attribute__((aligned(1)));
extern uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
extern uint8_t gm_cf51emcsnfi[16] __attribute__((aligned(1)));
extern uint8_t gm_cf5loitcbc7[13] __attribute__((aligned(1)));
extern uint8_t gm_cfalqv4rsam[7] __attribute__((aligned(1)));
extern uint8_t gm_cfclddbf5rj[9] __attribute__((aligned(1)));
extern uint8_t gm_cfv7g68ca5r[8] __attribute__((aligned(1)));
extern uint8_t gm_chkob26flgg[9] __attribute__((aligned(1)));
extern uint8_t gm_chrcl04adpj[25] __attribute__((aligned(1)));
extern uint8_t gm_chthounu1pg[8] __attribute__((aligned(1)));
extern uint8_t gm_ci5i3g6ghat[6] __attribute__((aligned(1)));
extern uint8_t gm_cigdn81ldc9[16] __attribute__((aligned(8)));
extern uint8_t gm_civobsgahf6[15] __attribute__((aligned(1)));
extern uint8_t gm_cjnmu2s2207[16] __attribute__((aligned(1)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
extern uint8_t gm_clps76qnh98[3] __attribute__((aligned(1)));
extern uint8_t gm_cm2dkubhtop[8] __attribute__((aligned(8)));
extern uint8_t gm_cm9r8d61pap[21] __attribute__((aligned(1)));
extern uint8_t gm_cmumimn4u17[6] __attribute__((aligned(1)));
extern uint8_t gm_cnc12norsln[7] __attribute__((aligned(1)));
extern uint8_t gm_cpk359ca82u[26] __attribute__((aligned(1)));
extern uint8_t gm_cplg36ag8b5[15] __attribute__((aligned(1)));
extern uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
extern uint8_t gm_cq22cjd7d4i[13] __attribute__((aligned(1)));
extern uint8_t gm_cq3tl31s253[2] __attribute__((aligned(1)));
extern uint8_t gm_cqr2aac1us2[7] __attribute__((aligned(1)));
extern uint8_t gm_credir2fcqk[9] __attribute__((aligned(1)));
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern uint8_t gm_cskujdbhaf5[12] __attribute__((aligned(1)));
extern uint8_t gm_css0is294pa[5] __attribute__((aligned(1)));
extern uint8_t gm_ctmke2ostj8[12] __attribute__((aligned(1)));
extern uint8_t gm_ctngbflld19[16] __attribute__((aligned(8)));
extern uint8_t gm_ctsgq16sumf[3] __attribute__((aligned(1)));
extern uint8_t gm_cu7epimqni2[3] __attribute__((aligned(1)));
extern uint8_t gm_curvqf1hf03[77] __attribute__((aligned(1)));
extern uint8_t gm_cuvdtuneoi6[10] __attribute__((aligned(1)));
extern uint8_t gm_cvo51u5rfub[14] __attribute__((aligned(1)));
extern void *const gm_hd3ip07hju0a[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hl5p278go8bp[];
extern void *const gm_hle6hel8vh71[];
extern void *const gm_hlh176qr0nti[];
extern void *const gm_hlqklhf369l0[];
extern uint8_t gm_k34avs9rqsl[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k8o4k1d1for[40] __attribute__((aligned(8)));
extern uint8_t gm_kbri4nir86d[40] __attribute__((aligned(8)));
extern uint8_t gm_keokac8bchm[40] __attribute__((aligned(8)));
extern uint8_t gm_kjh6c1ct3mq[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5pit792cd[40] __attribute__((aligned(8)));
extern uint8_t gm_kpbp8co2u50[40] __attribute__((aligned(8)));
extern uint8_t gm_ksci05qti98[40] __attribute__((aligned(8)));
extern uint8_t gm_ku0iiu0o88g[40] __attribute__((aligned(8)));
extern uint8_t gm_kutnfkdjl3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kv6tql7n7ah[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0039h44ked10[39];
extern const uint8_t gm_s018g1qkt4p93[9];
extern const uint8_t gm_s09r417b6q8b4[14];
extern const uint8_t gm_s0bmhg7t703ts[22];
extern const uint8_t gm_s0i4qvihaki34[41];
extern const uint8_t gm_s0lh70hs4of8e[22];
extern const uint8_t gm_s0r9vb1qh90ue[116];
extern const uint8_t gm_s0tfhmgv1pal7[9];
extern const uint8_t gm_s122r7j2imgmj[22];
extern const uint8_t gm_s18e8dmribdb3[7];
extern const uint8_t gm_s1aecjev6ahg8[5];
extern const uint8_t gm_s1ak5vhpqj4ju[5];
extern const uint8_t gm_s1d20j48bjjln[12];
extern const uint8_t gm_s1d982oimmu24[7];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1hbm05s14qo4[30];
extern const uint8_t gm_s1hha8fc2kd79[18];
extern const uint8_t gm_s1ishc3mvan41[31];
extern const uint8_t gm_s1j5448m6ruti[10];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1lekaltijhvv[8];
extern const uint8_t gm_s1nvbb638rr96[8];
extern const uint8_t gm_s1objv452j1q2[11];
extern const uint8_t gm_s1tqe3mtj6vb1[8];
extern const uint8_t gm_s20ceq2pdpd1i[14];
extern const uint8_t gm_s26cjdp185rsj[16];
extern const uint8_t gm_s27jprdv4pn6e[10];
extern const uint8_t gm_s2a4d8jj74v6o[49];
extern const uint8_t gm_s2a9t8smkevg0[4];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s2eu0g6qdje9o[41];
extern const uint8_t gm_s2qk17n09116b[14];
extern const uint8_t gm_s2r68j08i2lia[5];
extern const uint8_t gm_s2sghqjh0qbk9[8];
extern const uint8_t gm_s2u7kalhku9i5[152];
extern const uint8_t gm_s2v8uomf8pkfv[13];
extern const uint8_t gm_s312ibu3ccl18[12];
extern const uint8_t gm_s315r4147rf48[40];
extern const uint8_t gm_s329kcr9363eu[11];
extern const uint8_t gm_s3385032lmmqq[24];
extern const uint8_t gm_s33fm62gcdbhu[11];
extern const uint8_t gm_s361hl447ggcv[20];
extern const uint8_t gm_s387icqqu8ab9[97];
extern const uint8_t gm_s39o4eejklnd6[12];
extern const uint8_t gm_s3a1gf31m3ei6[10];
extern const uint8_t gm_s3cu59itn73k7[23];
extern const uint8_t gm_s3d0v42kavhv7[19];
extern const uint8_t gm_s3j0d9e0k3atk[5];
extern const uint8_t gm_s3mbo1s262isg[14];
extern const uint8_t gm_s3md83t7b4thr[10];
extern const uint8_t gm_s3nknbqjesra1[19];
extern const uint8_t gm_s3phpt25k93cc[39];
extern const uint8_t gm_s3pufl09hvbbf[6];
extern const uint8_t gm_s3sgbpnnlr6sb[5];
extern const uint8_t gm_s4108vvmvu0gn[70];
extern const uint8_t gm_s49o1oiv7k0po[4];
extern const uint8_t gm_s4acc6d5101lp[45];
extern const uint8_t gm_s4be485tkfn41[15];
extern const uint8_t gm_s4dqb8b35j0gp[24];
extern const uint8_t gm_s4gcag2e2r0cj[31];
extern const uint8_t gm_s4j3ecogbjkvn[13];
extern const uint8_t gm_s4jrltvkedvou[31];
extern const uint8_t gm_s4k5q4ehrakfe[15];
extern const uint8_t gm_s4ld5r1oi34jh[18];
extern const uint8_t gm_s4tgvb7s2dbuj[20];
extern const uint8_t gm_s4ufkrqv2ag7g[7];
extern const uint8_t gm_s4v09sn49l08n[7];
extern const uint8_t gm_s52jn4s18md6n[16];
extern const uint8_t gm_s5aj93skvhpnl[17];
extern const uint8_t gm_s5bh8t3p9l4nk[16];
extern const uint8_t gm_s5bohs6opthjm[24];
extern const uint8_t gm_s5dpb04jti9nb[17];
extern const uint8_t gm_s5dqpb4hur24f[37];
extern const uint8_t gm_s5g5rfc96krge[9];
extern const uint8_t gm_s5gkblcfbl1mv[121];
extern const uint8_t gm_s5hp4n46rtp6j[15];
extern const uint8_t gm_s5j95frjqt5af[8];
extern const uint8_t gm_s5jtsqa43tb6m[14];
extern const uint8_t gm_s5k1c58je5d4s[25];
extern const uint8_t gm_s5le7rpnp1iq6[9];
extern const uint8_t gm_s5nig6ebl26qo[61];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s5qpqp0tch0l9[7];
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_s5u07ahpa78ud[27];
extern const uint8_t gm_s5us2hgd173vu[27];
extern const uint8_t gm_s60n1k0cqjuaq[144];
extern const uint8_t gm_s63jrdfu5o5ps[9];
extern const uint8_t gm_s67h6d8kdg7qc[167];
extern const uint8_t gm_s6aamtn65omr1[7];
extern const uint8_t gm_s6dpovcf17va4[4];
extern const uint8_t gm_s6i2dst792b3t[54];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6npjseknrjhj[41];
extern const uint8_t gm_s6o3bl9uijlja[22];
extern const uint8_t gm_s6o3rebeo47ka[16];
extern const uint8_t gm_s6qihcjfe8481[10];
extern const uint8_t gm_s6vg88ce880ha[13];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s70jg155fkn11[41];
extern const uint8_t gm_s75m3dt9015lh[15];
extern const uint8_t gm_s79el5a3kii78[12];
extern const uint8_t gm_s79r5ktousup8[157];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s7ah1q5ah26rj[12];
extern const uint8_t gm_s7atji10aaq5k[7];
extern const uint8_t gm_s7bpqsd6i8gc3[15];
extern const uint8_t gm_s7cn2q9qfeafa[79];
extern const uint8_t gm_s7doqph2gqafo[60];
extern const uint8_t gm_s7f8crhgefl4u[28];
extern const uint8_t gm_s7j1ioljmsuka[19];
extern const uint8_t gm_s7u3qhd03rfo3[17];
extern const uint8_t gm_s7ucknluds45h[5];
extern const uint8_t gm_s7v0kgsq3q7vf[57];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s80t4sc5bf88n[82];
extern const uint8_t gm_s81g8i8i3husc[4518];
extern const uint8_t gm_s81ppjm26jd5s[148];
extern const uint8_t gm_s81vh0gpuu9rd[36];
extern const uint8_t gm_s86js8lt7ibda[48];
extern const uint8_t gm_s89j7s4avcmfs[28];
extern const uint8_t gm_s8de5r2vp8807[20];
extern const uint8_t gm_s8eis4410oprq[21];
extern const uint8_t gm_s8f69f7tnslut[16];
extern const uint8_t gm_s8krdq6a1r5nt[53];
extern const uint8_t gm_s8l5u1ln9m2k0[58];
extern const uint8_t gm_s8oua93imbc1q[15];
extern const uint8_t gm_s8ovik98r39ct[13];
extern const uint8_t gm_s8rsdirj4dmun[4];
extern const uint8_t gm_s8sl52ot187i2[50];
extern const uint8_t gm_s8uplmiglsqkm[15];
extern const uint8_t gm_s8v8krjfns655[20];
extern const uint8_t gm_s8vgs1ersvo58[8];
extern const uint8_t gm_s959ni36b80a4[4];
extern const uint8_t gm_s95mahatn8od3[41];
extern const uint8_t gm_s9aatfmgqhb47[7];
extern const uint8_t gm_s9co2b6h6nhos[16];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9f47s6tkuln3[42];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_s9lc9q0nba0ng[8];
extern const uint8_t gm_s9lh30p1g8vti[54];
extern const uint8_t gm_s9ol82jrvi3ma[4];
extern const uint8_t gm_s9sev457i3j7n[5];
extern const uint8_t gm_s9tmvssfpqc93[21];
extern const uint8_t gm_s9tnrhv72lohp[8];
extern const uint8_t gm_s9vg8k4mbj38f[22];
extern const uint8_t gm_sa027i133kcv3[4];
extern const uint8_t gm_sa349un8b060v[19];
extern const uint8_t gm_sa9sfrgc5vaao[5];
extern const uint8_t gm_sae23ckujbpp9[107];
extern const uint8_t gm_safiul2d9a6fl[14];
extern const uint8_t gm_safvj03783ie0[65];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal17k00cm1q9[23];
extern const uint8_t gm_samr3e6eqsavu[27];
extern const uint8_t gm_sannl4idt68d4[42];
extern const uint8_t gm_saqgvkkehjsjb[33];
extern const uint8_t gm_saqhrs072atsq[10];
extern const uint8_t gm_sar0vrndv51ep[14];
extern const uint8_t gm_sar7ovt8nasdo[8];
extern const uint8_t gm_sau1b8ivpr0us[8];
extern const uint8_t gm_saunlif71pbir[16];
extern const uint8_t gm_sb5it581p5nll[5];
extern const uint8_t gm_sb7lq2a8rdpq3[10];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_sb9dglh7ekrt2[7];
extern const uint8_t gm_sb9k4ip91d7oa[6];
extern const uint8_t gm_sb9k60eb8i01r[41];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbf8eactntf1g[21];
extern const uint8_t gm_sbgj0biroecr0[13];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sblao71d2av8h[22];
extern const uint8_t gm_sbngfgv31nmur[61];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_sbrifoc7ea21m[7];
extern const uint8_t gm_sbrtnmpf78kqq[10];
extern const uint8_t gm_sbs1lj6q8rfiu[44];
extern const uint8_t gm_sc103nruhmor7[3];
extern const uint8_t gm_sc24a25pi012e[8];
extern const uint8_t gm_scb1vtgm37aqf[41];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_sck2rq9li012q[23];
extern const uint8_t gm_sckeklr60tlq3[34];
extern const uint8_t gm_scn1hel0olv2p[41];
extern const uint8_t gm_scnb11g2ghrd0[35];
extern const uint8_t gm_scohuf4dfj32r[37];
extern const uint8_t gm_scp8gj1kjauec[29];
extern const uint8_t gm_scte4ush0iqha[7];
extern const uint8_t gm_sd7guj32ltqh8[47];
extern const uint8_t gm_sd7vrpqmmkbm0[111];
extern const uint8_t gm_sdcigk5ajbcfi[68];
extern const uint8_t gm_sdh44op4kc2is[15];
extern const uint8_t gm_sdib10lkj65nm[45];
extern const uint8_t gm_sdib2k2ql9mgl[9];
extern const uint8_t gm_sdjba9ailb8m1[4];
extern const uint8_t gm_sdk5n6a42f7nl[3];
extern const uint8_t gm_sdlbqh39uq50t[13];
extern const uint8_t gm_sdllavet8v9kb[41];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_sdujhgrk0oln8[5];
extern const uint8_t gm_sdvpeftn9n4aa[7];
extern const uint8_t gm_se3smlbkt8d02[37];
extern const uint8_t gm_se3u415f6s7p1[13];
extern const uint8_t gm_se7h6nse0tc33[26];
extern const uint8_t gm_se7oqjo2sonp9[9];
extern const uint8_t gm_se8cpd8s09lkc[13];
extern const uint8_t gm_sedk3p1dc2it3[17];
extern const uint8_t gm_sedq8b22f9agn[91];
extern const uint8_t gm_sefm4vafv1a3l[3];
extern const uint8_t gm_seh4hl8ei3cha[20];
extern const uint8_t gm_sehgil1ui47md[7];
extern const uint8_t gm_seiuio4eghuvu[19];
extern const uint8_t gm_sel2c30qvm74f[7];
extern const uint8_t gm_senjrqtakqrde[16];
extern const uint8_t gm_set3pi1jhetbi[7];
extern const uint8_t gm_sf3fmcr9mrflm[15];
extern const uint8_t gm_sf5llhuhdlajp[9];
extern const uint8_t gm_sf8e06i5juo6a[29];
extern const uint8_t gm_sfaeuqh39av6i[28];
extern const uint8_t gm_sfeuhas7a05k6[6];
extern const uint8_t gm_sfir6p8a22i6s[31];
extern const uint8_t gm_sfknvqdnjah7l[18];
extern const uint8_t gm_sfm5elvivaoue[41];
extern const uint8_t gm_sfr3tv65k63d4[16];
extern const uint8_t gm_sfsnsm5njae7n[19];
extern const uint8_t gm_sg1uf2uigo01q[27];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg3ba4vpic0jb[19];
extern const uint8_t gm_sg46oe1nh64ub[9];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgbcvt6rqctvd[7];
extern const uint8_t gm_sgge76tb2bmvi[32];
extern const uint8_t gm_sgip9ekbho6mc[11];
extern const uint8_t gm_sgm565ktbj9ua[12];
extern const uint8_t gm_sgn9uq6nipp5t[33];
extern const uint8_t gm_sgrpmaenv85i1[73];
extern const uint8_t gm_sgsd94uuisj8t[73];
extern const uint8_t gm_sh1v5m99cfmp8[41];
extern const uint8_t gm_sh50kc23mi3vj[36];
extern const uint8_t gm_sh7mmr8b17j77[29];
extern const uint8_t gm_sh9g09icusqpu[14];
extern const uint8_t gm_shc0s7vbfquc0[20];
extern const uint8_t gm_shdh4dd2ejebj[158];
extern const uint8_t gm_sheqs83o4tgp8[45];
extern const uint8_t gm_shh36vc8p1t4e[7];
extern const uint8_t gm_shhak6l2rrn43[15];
extern const uint8_t gm_shheebrtu0dtr[41];
extern const uint8_t gm_shj3jonnfj09m[8];
extern const uint8_t gm_shja5bmmh8ag2[30];
extern const uint8_t gm_shkhe2ptevja3[18];
extern const uint8_t gm_shki2pqrgmpd1[19];
extern const uint8_t gm_shlilv0hq3bqu[33];
extern const uint8_t gm_shmdne5cle16i[8];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shqseesnqrpjf[22];
extern const uint8_t gm_shrlc4g1vtjsu[47];
extern const uint8_t gm_sht5g388qmj1f[15];
extern const uint8_t gm_shv4snn6f8hcr[29];
extern const uint8_t gm_shvgs2jq7ohhl[41];
extern const uint8_t gm_si2v1nj23o22k[41];
extern const uint8_t gm_si4k9tiofr8t2[7];
extern const uint8_t gm_si5jb1hd2qqp1[12];
extern const uint8_t gm_si6sr5gqkcui7[6];
extern const uint8_t gm_si8s9ids0otre[44];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sigl9de72ni6d[16];
extern const uint8_t gm_siiln09e6475q[5];
extern const uint8_t gm_silt1tnb0phmp[3];
extern const uint8_t gm_sj0qpc1n9qui9[7];
extern const uint8_t gm_sj1m58fkg956m[8];
extern const uint8_t gm_sj3ehailc2pgo[52];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_sjchrspn764h6[30];
extern const uint8_t gm_sje5p1r4d76uk[57];
extern const uint8_t gm_sje7od1nuputq[921];
extern const uint8_t gm_sjerq54hvns79[11];
extern const uint8_t gm_sjfqm9gvu1om9[8];
extern const uint8_t gm_sjhnkp7qigia6[41];
extern const uint8_t gm_sjid61i849qic[3];
extern const uint8_t gm_sjm2vqdj9hddi[51];
extern const uint8_t gm_sjmmigkb19htg[4];
extern const uint8_t gm_sjojf6rrbl49u[6];
extern const uint8_t gm_sjqmjo9ak50j8[5];
extern const uint8_t gm_sjrg55dooppac[6];
extern const uint8_t gm_sk31jlojfl0rr[8];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_sk88tp2337lf6[13];
extern const uint8_t gm_ska87i9f7924f[8];
extern const uint8_t gm_skefd3dps40es[40];
extern const uint8_t gm_skeo8nnnc4o7f[40];
extern const uint8_t gm_skgcaidqnnhjr[11];
extern const uint8_t gm_skj7cpsc7j6g9[5];
extern const uint8_t gm_skmo18eln23n6[16];
extern const uint8_t gm_skn7gps778eo2[3];
extern const uint8_t gm_sko48juemga53[34];
extern const uint8_t gm_sktopm8j9jc57[802];
extern const uint8_t gm_skvch52visg8i[13];
extern const uint8_t gm_sl0vvvamhv0to[12];
extern const uint8_t gm_sl1a3p7pocs82[15];
extern const uint8_t gm_sl2oiu9s0celr[49];
extern const uint8_t gm_sl2sr5fl46c73[48];
extern const uint8_t gm_sl63bnit79nfo[41];
extern const uint8_t gm_sl66hhhp1j2a6[14];
extern const uint8_t gm_slahfmk66v1v4[15];
extern const uint8_t gm_slc0d2s6km3s5[5];
extern const uint8_t gm_slc58j6a4e39a[56];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slj7i6lishtfg[47];
extern const uint8_t gm_sll7gknpe999d[4];
extern const uint8_t gm_slr574u01mn8m[17];
extern const uint8_t gm_slshl1j266n3l[7];
extern const uint8_t gm_slsmq9u0kk3l3[6];
extern const uint8_t gm_sm1kt2tr7m23b[4];
extern const uint8_t gm_sm3gke457vdm3[5];
extern const uint8_t gm_sm4ujt9jgj9au[8];
extern const uint8_t gm_sm5balq7hen2r[3];
extern const uint8_t gm_sm8049sr7hsf6[41];
extern const uint8_t gm_sm94b3smoinpd[60];
extern const uint8_t gm_smcnje2cdb03e[3];
extern const uint8_t gm_smdj5floh97je[10];
extern const uint8_t gm_smggdkb5phvp0[56];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_smrahl8ppkcu9[34];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_smsf0v36m37b1[66];
extern const uint8_t gm_smu6g4sneo92j[6];
extern const uint8_t gm_smu8iti9fc903[85];
extern const uint8_t gm_sn0ac8j2fho01[22];
extern const uint8_t gm_sn0d6pm4psq1h[21];
extern const uint8_t gm_sn1puqmdafub0[14];
extern const uint8_t gm_sn6gnsehoh5ru[7];
extern const uint8_t gm_snaenr23tbgmb[23];
extern const uint8_t gm_snhskkd5kfgfa[31];
extern const uint8_t gm_snk6krn32vlub[10];
extern const uint8_t gm_snls9i3gr3n3f[41];
extern const uint8_t gm_snqmbfl2fr85m[17];
extern const uint8_t gm_snqmeaue6lcfo[9];
extern const uint8_t gm_snrrubbi4s05g[16];
extern const uint8_t gm_snud2jia3fe8d[18];
extern const uint8_t gm_so1gbme93p3pe[5];
extern const uint8_t gm_so25i3en68imc[27];
extern const uint8_t gm_so2dhrd52m05k[49];
extern const uint8_t gm_so3ukp67pds22[190];
extern const uint8_t gm_so48b6sdoerku[18];
extern const uint8_t gm_sodgjk6pvm3b4[21];
extern const uint8_t gm_sodivosk5r8rb[2];
extern const uint8_t gm_sojb2hhp0ln7r[31];
extern const uint8_t gm_solsl3u2gfc4f[7];
extern const uint8_t gm_sp0ce5j43v7u2[26];
extern const uint8_t gm_sp0rb1clvmpp5[5];
extern const uint8_t gm_sp1srk1sv0crk[52];
extern const uint8_t gm_sp24uj950qpkl[16];
extern const uint8_t gm_sp46pm3sna4bl[86];
extern const uint8_t gm_sp796mgrgp4mj[45];
extern const uint8_t gm_sp8i1c1kf79ks[3];
extern const uint8_t gm_spaa4sseck2fg[45];
extern const uint8_t gm_spaflckd9bou5[29];
extern const uint8_t gm_spasf3abnfbjh[8];
extern const uint8_t gm_spbdk1kpko0k2[141];
extern const uint8_t gm_spducfcn5c083[69];
extern const uint8_t gm_spervk999ikim[8];
extern const uint8_t gm_spf5cnp0l9k4n[449];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spl4qnmq2arsc[13];
extern const uint8_t gm_spnr89edksdqp[4];
extern const uint8_t gm_spt7rbjl7ucc9[8];
extern const uint8_t gm_sq0bes3qstkub[15];
extern const uint8_t gm_sq2es9bij87b7[14];
extern const uint8_t gm_sq32stff79ol1[22];
extern const uint8_t gm_sq6e9d3107t3s[11];
extern const uint8_t gm_sq8839lqmgrd7[11];
extern const uint8_t gm_sq8aqd2uj6ppq[12];
extern const uint8_t gm_sq9qefps1uqei[10];
extern const uint8_t gm_sqa29afvhov50[6];
extern const uint8_t gm_sqbbhnhgkc31j[14];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqg70d5uem21e[35];
extern const uint8_t gm_sqit0ai5v3joc[23];
extern const uint8_t gm_sqk1q73sjq658[24];
extern const uint8_t gm_sqm6u7rqmi1il[19];
extern const uint8_t gm_sqn7kgtu67hrg[42];
extern const uint8_t gm_sqqvi1jr97u13[10];
extern const uint8_t gm_sqrsljl0ejj01[41];
extern const uint8_t gm_sqt8gk1nsci25[5];
extern const uint8_t gm_squ4ohvqdqaap[79];
extern const uint8_t gm_sqvn1en6c2n9e[32];
extern const uint8_t gm_sqvvajmjm3fif[11];
extern const uint8_t gm_sr0hr1ep2j2vv[5];
extern const uint8_t gm_sr5mkle2k9poq[28];
extern const uint8_t gm_sr899jbhl9noj[3];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src0m6lnv6m2d3p[];
extern const char gm_src2pgaip05o19o[];
extern const char gm_src3dhhc4laniae[];
extern const char gm_src4tam33qo20j7[];
extern const char gm_src6unagr666v56[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_src9vg8k4mbj38f[];
extern const char gm_srca29n3fkrpbdt[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcihhtlsvd7f6i[];
extern const char gm_srclt70oog8j9io[];
extern const char gm_srcr9oeab0h5mup[];
extern const char gm_srct4pcukr5kqhq[];
extern const char gm_srcu4ee17t4impf[];
extern const char gm_srcue4ionbe9lgh[];
extern const uint8_t gm_sremdpsmf4ala[50];
extern const uint8_t gm_srg9h7ti0cg3a[25];
extern const uint8_t gm_sri1d6adh4kkk[11];
extern const uint8_t gm_srj7urnaf6a1u[9];
extern const uint8_t gm_srjcgu24i74p9[48];
extern const uint8_t gm_srk1d80eamfhd[15];
extern const uint8_t gm_srq25fqvivo2q[73];
extern const uint8_t gm_srrm1sevfig2o[3];
extern const uint8_t gm_srs4r9h62u04l[13];
extern const uint8_t gm_sru4dh297ccek[19];
extern const uint8_t gm_ss03naco6s5l7[5];
extern const uint8_t gm_ss3i577tvjetf[8];
extern const uint8_t gm_ss4337v5p4tj3[5];
extern const uint8_t gm_ss54u8us11c4v[42];
extern const uint8_t gm_ssfs2ebod65fl[38];
extern const uint8_t gm_ssgn9mppmu3vl[16];
extern const uint8_t gm_sshhi6fn3vfaa[45];
extern const uint8_t gm_sshi9ffc6nkc4[10];
extern const uint8_t gm_ssi4rrma2q00g[7];
extern const uint8_t gm_ssi95ri2h4d02[28];
extern const uint8_t gm_ssici0eo813ko[17];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_ssr91slt3ah1c[11];
extern const uint8_t gm_ssrd6bs7gcs4g[29];
extern const uint8_t gm_sstppps49t867[11];
extern const uint8_t gm_sstqe1228ic6k[51];
extern const uint8_t gm_ssvci9220kuoj[45];
extern const uint8_t gm_ssvcneuafnfds[8];
extern const uint8_t gm_st0jtdoe1b7a6[15];
extern const uint8_t gm_st28rv7m1lueb[9];
extern const uint8_t gm_st3m5iduoqh8b[5];
extern const uint8_t gm_st3rnlu97af0r[63];
extern const uint8_t gm_st4r3sao0kja5[6];
extern const uint8_t gm_st53sho4849cg[18];
extern const uint8_t gm_st62vtcrpam9i[17];
extern const uint8_t gm_st6ksn77egld4[13];
extern const uint8_t gm_stadskj9k22h7[30];
extern const uint8_t gm_stcd71a4feia0[10];
extern const uint8_t gm_std877jr2ed05[29];
extern const uint8_t gm_stddp43pjd9r1[51];
extern const uint8_t gm_sterslo87taeu[39];
extern const uint8_t gm_stfp83525k3nr[7];
extern const uint8_t gm_stj45lnsahel3[30];
extern const uint8_t gm_stlojc09k1lf1[41];
extern const uint8_t gm_stmgel74lrtrn[33];
extern const uint8_t gm_stn52kuiq0jk2[12];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_stqlepc346jgk[85];
extern const uint8_t gm_stum5f38vfqa2[15];
extern const uint8_t gm_stvja3baka61k[8];
extern const uint8_t gm_su20i5iji4ao3[4];
extern const uint8_t gm_su38t05utsk53[67];
extern const uint8_t gm_su3ahsl9p584f[9];
extern const uint8_t gm_su41v0g030vmk[95];
extern const uint8_t gm_su6m1tso8ssol[41];
extern const uint8_t gm_sufp89bevlq87[5];
extern const uint8_t gm_suhlp72acbph4[43];
extern const uint8_t gm_suia0naoru7mp[7];
extern const uint8_t gm_sukj4damah3dd[85];
extern const uint8_t gm_sulvvqvd5gcql[15];
extern const uint8_t gm_sumvvvaqn43sl[10];
extern const uint8_t gm_sut7mhrkhqp77[56];
extern const uint8_t gm_suup20cg9lo2d[8];
extern const uint8_t gm_suvsl6ie8luas[15];
extern const uint8_t gm_sv0odl408mcff[24];
extern const uint8_t gm_sv1bu4l4kq188[8];
extern const uint8_t gm_sv52j7o6hr9j8[10];
extern const uint8_t gm_sv56kn0notafb[17];
extern const uint8_t gm_sv6dkmq38siqt[27];
extern const uint8_t gm_sv7tbc2gvmpa1[100];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_sv96uhaheg6dq[41];
extern const uint8_t gm_svdcmt7tps0ma[57];
extern const uint8_t gm_sve2u8a7eapjo[18];
extern const uint8_t gm_svirf1u8t89n0[47];
extern const uint8_t gm_svl0gta90bsdr[49];
extern const uint8_t gm_svmt0iljnadus[4];
extern const uint8_t gm_svpr02j6er8vd[15];
extern const uint8_t gm_svsf0pu3t8u6m[7];
const char gm_src0ih77f43mhf2[] = "compiler/types/types.gm";
const char gm_src0m6lnv6m2d3p[] = "compiler/linting/linting.gm";
const char gm_src2pgaip05o19o[] = "compiler/load/when.gm";
const char gm_src3dhhc4laniae[] = "compiler/repl/repl.gm";
const char gm_src4tam33qo20j7[] = "compiler/check/stack_size.gm";
const char gm_src6unagr666v56[] = "compiler/interp/bc.gm";
const char gm_src7u0d314efpdm[] = "compiler/pkg/link.gm";
const char gm_src9vg8k4mbj38f[] = "compiler/eval/eval.gm";
const char gm_srca29n3fkrpbdt[] = "compiler/cgen/borrow.gm";
const char gm_srccfhjr4r9mdef[] = "compiler/cgen/units.gm";
const char gm_srcihhtlsvd7f6i[] = "compiler/cgen/fiber.gm";
const char gm_srclt70oog8j9io[] = "compiler/pkg/edit.gm";
const char gm_srcr9oeab0h5mup[] = "lib/std/fiber/fiber.gm";
const char gm_srct4pcukr5kqhq[] = "compiler/check/perform.gm";
const char gm_srcu4ee17t4impf[] = "compiler/pkg/lock.gm";
const char gm_srcue4ionbe9lgh[] = "compiler/eval/ops.gm";
const uint8_t gm_s0039h44ked10[39] = "unreachable: the key was just inserted";
const uint8_t gm_s018g1qkt4p93[9] = "tmp_next";
const uint8_t gm_s09r417b6q8b4[14] = "--show-output";
const uint8_t gm_s0bmhg7t703ts[22] = "`--{}` takes no value";
const uint8_t gm_s0i4qvihaki34[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\217\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s0lh70hs4of8e[22] = "didChangeWatchedFiles";
const uint8_t gm_s0r9vb1qh90ue[116] = "`: targets have no instruction that divides integer lanes, so the cost would be hidden; divide the lanes one by one";
const uint8_t gm_s0tfhmgv1pal7[9] = "location";
const uint8_t gm_s122r7j2imgmj[22] = "to close the tag type";
const uint8_t gm_s18e8dmribdb3[7] = ")0 : (";
const uint8_t gm_s1aecjev6ahg8[5] = "Add(";
const uint8_t gm_s1ak5vhpqj4ju[5] = "u128";
const uint8_t gm_s1d20j48bjjln[12] = "__FreeBSD__";
const uint8_t gm_s1d982oimmu24[7] = "git+{}";
const uint8_t gm_s1dbslfck7laf[3] = "*%";
const uint8_t gm_s1f440st4mnkp[2] = "<";
const uint8_t gm_s1hbm05s14qo4[30] = "`, which leaves its directory";
const uint8_t gm_s1hha8fc2kd79[18] = "remove the `else`";
const uint8_t gm_s1ishc3mvan41[31] = "math vectors have no method `.";
const uint8_t gm_s1j5448m6ruti[10] = "HeapAlloc";
const uint8_t gm_s1jboc8lkvac0[6] = "field";
const uint8_t gm_s1lekaltijhvv[8] = "prog-{}";
const uint8_t gm_s1nvbb638rr96[8] = "->arg, ";
const uint8_t gm_s1objv452j1q2[11] = "this slice";
const uint8_t gm_s1tqe3mtj6vb1[8] = "struct ";
const uint8_t gm_s20ceq2pdpd1i[14] = "parse_foreign";
const uint8_t gm_s26cjdp185rsj[16] = "    if (tg == (";
const uint8_t gm_s27jprdv4pn6e[10] = "passing `";
const uint8_t gm_s2a4d8jj74v6o[49] = "`_` may only go between digits, expected nothing";
const uint8_t gm_s2a9t8smkevg0[4] = "Get";
const uint8_t gm_s2dssfe3261t2[2] = "2";
const uint8_t gm_s2eu0g6qdje9o[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000e\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s2qk17n09116b[14] = "-bit integer>";
const uint8_t gm_s2r68j08i2lia[5] = "<tr>";
const uint8_t gm_s2sghqjh0qbk9[8] = "<bench>";
const uint8_t gm_s2u7kalhku9i5[152] = "the package contains the symbolic link `{}` -> `{}`, which points outside the package through other links; links must stay inside the package directory";
const uint8_t gm_s2v8uomf8pkfv[13] = " ta = GM_LD(";
const uint8_t gm_s312ibu3ccl18[12] = "{}|{}|{}|{}";
const uint8_t gm_s315r4147rf48[40] = "GM_PTR_ADD({}, {}((uintptr_t){} * {}u))";
const uint8_t gm_s329kcr9363eu[11] = "{}({}, {})";
const uint8_t gm_s3385032lmmqq[24] = "integer overflow in `%`";
const uint8_t gm_s33fm62gcdbhu[11] = "targets a ";
const uint8_t gm_s361hl447ggcv[20] = "compiler/fix/fix.gm";
const uint8_t gm_s387icqqu8ab9[97] = "\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000";
const uint8_t gm_s39o4eejklnd6[12] = "a file name";
const uint8_t gm_s3a1gf31m3ei6[10] = "(uint8_t)";
const uint8_t gm_s3cu59itn73k7[23] = "__attribute__((cold)) ";
const uint8_t gm_s3d0v42kavhv7[19] = "lib/std/str/str.gm";
const uint8_t gm_s3j0d9e0k3atk[5] = "plus";
const uint8_t gm_s3mbo1s262isg[14] = "`@stack_size(";
const uint8_t gm_s3md83t7b4thr[10] = "this `in`";
const uint8_t gm_s3nknbqjesra1[19] = "array construction";
const uint8_t gm_s3phpt25k93cc[39] = "generic instantiations nest more than ";
const uint8_t gm_s3pufl09hvbbf[6] = "arm64";
const uint8_t gm_s3sgbpnnlr6sb[5] = " are";
const uint8_t gm_s4108vvmvu0gn[70] = "`{}` modifies its receiver, which must be a variable, not a temporary";
const uint8_t gm_s49o1oiv7k0po[4] = "Oct";
const uint8_t gm_s4acc6d5101lp[45] = " parameters (the first is the handler value)";
const uint8_t gm_s4be485tkfn41[15] = "nvariants = {}";
const uint8_t gm_s4dqb8b35j0gp[24] = "textDocument/codeAction";
const uint8_t gm_s4gcag2e2r0cj[31] = "    return gm_mix64(h, bits);\012";
const uint8_t gm_s4j3ecogbjkvn[13] = "textDocument";
const uint8_t gm_s4jrltvkedvou[31] = "((gm_closure){{{}, NULL, {}}})";
const uint8_t gm_s4k5q4ehrakfe[15] = "(void *){}, {}";
const uint8_t gm_s4ld5r1oi34jh[18] = "the import path `";
const uint8_t gm_s4tgvb7s2dbuj[20] = ": warning: module `";
const uint8_t gm_s4ufkrqv2ag7g[7] = "--fail";
const uint8_t gm_s4v09sn49l08n[7] = "_union";
const uint8_t gm_s52jn4s18md6n[16] = " != 0) memmove(";
const uint8_t gm_s5aj93skvhpnl[17] = ")` or `.checked(";
const uint8_t gm_s5bh8t3p9l4nk[16] = "refs/gamma/head";
const uint8_t gm_s5bohs6opthjm[24] = "    return 0;\012#undef F\012";
const uint8_t gm_s5dpb04jti9nb[17] = "to begin a block";
const uint8_t gm_s5dqpb4hur24f[37] = "({} < {} \077 {} : ({} > {} \077 {} : {}))";
const uint8_t gm_s5g5rfc96krge[9] = "\036\000\000\000\000\000\000\000";
const uint8_t gm_s5gkblcfbl1mv[121] = "this `errdefer` never runs: it is the last statement of its block, so nothing after it can leave the block with an error";
const uint8_t gm_s5hp4n46rtp6j[15] = "fmt_shortest16";
const uint8_t gm_s5j95frjqt5af[8] = "{}[={}]";
const uint8_t gm_s5jtsqa43tb6m[14] = "gm_trap_lanes";
const uint8_t gm_s5k1c58je5d4s[25] = "integer overflow in `{}`";
const uint8_t gm_s5le7rpnp1iq6[9] = "FileKind";
const uint8_t gm_s5nig6ebl26qo[61] = "`#trap_hook` applies only on a freestanding target (\302\2473.7.1)";
const uint8_t gm_s5qkai093c1q0[6] = "value";
const uint8_t gm_s5qpqp0tch0l9[7] = "[{}]{}";
const uint8_t gm_s5t1b3eks758h[27] = "unterminated block comment";
const uint8_t gm_s5u07ahpa78ud[27] = "(({}){})({}{}, {}, NULL{})";
const uint8_t gm_s5us2hgd173vu[27] = "to close the argument list";
const uint8_t gm_s60n1k0cqjuaq[144] = "`{}` of dependency `{}` names `{}`, which is not inside the package; paths in a dependency's [build] must be relative and stay in its directory";
const uint8_t gm_s63jrdfu5o5ps[9] = "volatile";
const uint8_t gm_s67h6d8kdg7qc[167] = "a name in the position of an optional matches only a present value (\302\2479.3); to bind the optional as it is, absent value included, write `\077` before the name (`.V(\077x)`)";
const uint8_t gm_s6aamtn65omr1[7] = "LaneNe";
const uint8_t gm_s6dpovcf17va4[4] = "F32";
const uint8_t gm_s6i2dst792b3t[54] = "expected a compile-time `{}`, found `{}` of type `{}`";
const uint8_t gm_s6iuue4hdrbt9[5] = "safe";
const uint8_t gm_s6npjseknrjhj[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0008\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s6o3bl9uijlja[22] = "        uint64_t k = ";
const uint8_t gm_s6o3rebeo47ka[16] = "a present value";
const uint8_t gm_s6qihcjfe8481[10] = "type_name";
const uint8_t gm_s6vg88ce880ha[13] = "unused-label";
const uint8_t gm_s6vvtrmtnt3j1[5] = "bits";
const uint8_t gm_s70jg155fkn11[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\023\001\000\000\012\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s75m3dt9015lh[15] = "(gm_node *cx, ";
const uint8_t gm_s79el5a3kii78[12] = "gamma-test-";
const uint8_t gm_s79r5ktousup8[157] = "checksum mismatch for package {} (expected {}, found {}); gamma.lock was edited or the cached archive {} was modified (`gamma cache clean --all` removes it)";
const uint8_t gm_s79s3h98matgr[6] = "error";
const uint8_t gm_s7ah1q5ah26rj[12] = "not a union";
const uint8_t gm_s7atji10aaq5k[7] = " lift ";
const uint8_t gm_s7bpqsd6i8gc3[15] = "gamma: {}\012\012{}\012";
const uint8_t gm_s7cn2q9qfeafa[79] = "` does not apply to complex numbers; they have `+`, `-`, `*` and `/` (\302\2477.4.7)";
const uint8_t gm_s7doqph2gqafo[60] = "internal error: a `ctl` operation performed outside a fiber";
const uint8_t gm_s7f8crhgefl4u[28] = "` does not match the open `";
const uint8_t gm_s7j1ioljmsuka[19] = "the default branch";
const uint8_t gm_s7u3qhd03rfo3[17] = "unsupported URI ";
const uint8_t gm_s7ucknluds45h[5] = "gm_l";
const uint8_t gm_s7v0kgsq3q7vf[57] = "`#callconv` takes a convention name: `#callconv(.Win64)`";
const uint8_t gm_s80p0ld1j6rfg[10] = "link_name";
const uint8_t gm_s80t4sc5bf88n[82] = "a thread runs a `^fn(arg: rawptr) needs Sys` with the C calling convention, not `";
const uint8_t gm_s81g8i8i3husc[4518] = "\012#define GM_NOINLINE __attribute__((noinline))\012static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {\012    const uint8_t *b = p;\012    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }\012    return h;\012}\012/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */\012static uint64_t gm_mix64(uint64_t h, uint64_t x) {\012    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }\012    return h;\012}\012/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */\012static uint64_t gm_hash_int(uint64_t h, __int128 v) {\012    for (;;) {\012        uint8_t byte = (uint8_t)v; v >>= 8;\012        h ^= byte; h *= 0x100000001b3ULL;\012        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;\012    }\012    return h;\012}\012static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {\012    for (;;) {\012        uint8_t byte = (uint8_t)v; v >>= 8;\012        h ^= byte; h *= 0x100000001b3ULL;\012        if (v == 0 && !(byte & 0x80)) break;\012        if (v == 0) { h *= 0x100000001b3ULL; break; }\012    }\012    return h;\012}\012static double gm_fmin(double x, double y) {\012    if (x != x) return y;\012    if (y != y) return x;\012    if (x == 0 && y == 0) return __builtin_signbit(x) \077 x : y;\012    return x < y \077 x : y;\012}\012static double gm_fmax(double x, double y) {\012    if (x != x) return y;\012    if (y != y) return x;\012    if (x == 0 && y == 0) return __builtin_signbit(x) \077 y : x;\012    return x > y \077 x : y;\012}\012/* base ^ e by repeated squaring, wrapping at the width of T; *o is set when a product that\012 * contributes to the result overflowed T (so the true result does not fit T). */\012#define GM_POW_FN(name, T)                                                      \\\012    static T name(T b, uint64_t e, bool *o) {                                   \\\012        T r = 1;                                                                \\\012        for (;;) {                                                              \\\012            if ((e & 1) && __builtin_mul_overflow(r, b, &r)) *o = true;        \\\012            e >>= 1;                                                            \\\012            if (!e) return r;                                                   \\\012            if (__builtin_mul_overflow(b, b, &b)) *o = true;                    \\\012        }                                                                       \\\012    }\012GM_POW_FN(gm_pow_u8, uint8_t)\012GM_POW_FN(gm_pow_i8, int8_t)\012GM_POW_FN(gm_pow_u16, uint16_t)\012GM_POW_FN(gm_pow_i16, int16_t)\012GM_POW_FN(gm_pow_u32, uint32_t)\012GM_POW_FN(gm_pow_i32, int32_t)\012GM_POW_FN(gm_pow_u64, uint64_t)\012GM_POW_FN(gm_pow_i64, int64_t)\012GM_POW_FN(gm_pow_u128, unsigned __int128)\012GM_POW_FN(gm_pow_i128, __int128)\012/* Bit counts of the low `bits` bits of x (the caller masks the rest). */\012static unsigned __int128 gm_clz(unsigned __int128 x, int bits) {\012    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;\012    int n = hi \077 __builtin_clzll(hi) : lo \077 64 + __builtin_clzll(lo) : 128;\012    return n - (128 - bits);\012}\012static unsigned __int128 gm_ctz(unsigned __int128 x, int bits) {\012    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;\012    int n = lo \077 __builtin_ctzll(lo) : hi \077 64 + __builtin_ctzll(hi) : 128;\012    return n < bits \077 n : bits;\012}\012static unsigned __int128 gm_popcount(unsigned __int128 x) {\012    return __builtin_popcountll((uint64_t)x) + __builtin_popcountll((uint64_t)(x >> 64));\012}\012static unsigned __int128 gm_bswap(unsigned __int128 x, int bits) {\012    unsigned __int128 r = ((unsigned __int128)__builtin_bswap64((uint64_t)x) << 64) | __builtin_bswap64((uint64_t)(x >> 64));\012    return r >> (128 - bits);\012}\012static unsigned __int128 gm_bitreverse(unsigned __int128 x, int bits) {\012    unsigned __int128 r = 0;\012    for (int i = 0; i < 4; i++) {\012        uint32_t w = (uint32_t)(x >> (32 * i));\012        w = ((w >> 1) & 0x55555555u) | ((w & 0x55555555u) << 1);\012        w = ((w >> 2) & 0x33333333u) | ((w & 0x33333333u) << 2);\012        w = ((w >> 4) & 0x0f0f0f0fu) | ((w & 0x0f0f0f0fu) << 4);\012        w = __builtin_bswap32(w);\012        r |= (unsigned __int128)w << (32 * (3 - i));\012    }\012    return r >> (128 - bits);\012}\012/* Rotates the low `bits` bits of x left by k (taken modulo bits). */\012static unsigned __int128 gm_rotl(unsigned __int128 x, uint64_t k, int bits) {\012    unsigned __int128 mask = bits == 128 \077 ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);\012    x &= mask;\012    k %= (uint64_t)bits;\012    if (k == 0) return x;\012    return ((x << k) | (x >> ((uint64_t)bits - k))) & mask;\012}\012";
const uint8_t gm_s81ppjm26jd5s[148] = "the copy shares its elements with the original but not its length, so what is added through it is lost; to change the original, take its address: `";
const uint8_t gm_s81vh0gpuu9rd[36] = "a float literal of a non-float type";
const uint8_t gm_s86js8lt7ibda[48] = "(gm_node *cx, const gm_type *d, const void *v, ";
const uint8_t gm_s89j7s4avcmfs[28] = "expected an integer literal";
const uint8_t gm_s8de5r2vp8807[20] = "` is supplied twice";
const uint8_t gm_s8eis4410oprq[21] = "` is returned, but `";
const uint8_t gm_s8f69f7tnslut[16] = "gm_net_shutdown";
const uint8_t gm_s8krdq6a1r5nt[53] = "this compares a value with itself, so it is always `";
const uint8_t gm_s8l5u1ln9m2k0[58] = "`: use letters, digits and `_`, not starting with a digit";
const uint8_t gm_s8oua93imbc1q[15] = "gm_set_timeout";
const uint8_t gm_s8ovik98r39ct[13] = "uint8_t gm_g";
const uint8_t gm_s8rsdirj4dmun[4] = "doc";
const uint8_t gm_s8sl52ot187i2[50] = "a string literal naming the module after `import`";
const uint8_t gm_s8uplmiglsqkm[15] = "the level of `";
const uint8_t gm_s8v8krjfns655[20] = "gamma-watched-files";
const uint8_t gm_s8vgs1ersvo58[8] = "collect";
const uint8_t gm_s959ni36b80a4[4] = "));";
const uint8_t gm_s95mahatn8od3[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\226\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s9aatfmgqhb47[7] = "break ";
const uint8_t gm_s9co2b6h6nhos[16] = "` in one `with`";
const uint8_t gm_s9es2tos1bhfp[5] = "else";
const uint8_t gm_s9f47s6tkuln3[42] = "`: No such file or directory (os error 2)";
const uint8_t gm_s9grtrvtmi962[8] = "uint8_t";
const uint8_t gm_s9lc9q0nba0ng[8] = "gm_args";
const uint8_t gm_s9lh30p1g8vti[54] = "; `v[..]` is a slice of its elements, which has `.{}`";
const uint8_t gm_s9ol82jrvi3ma[4] = "*)(";
const uint8_t gm_s9sev457i3j7n[5] = " = {";
const uint8_t gm_s9tmvssfpqc93[21] = ", h);\012    return h;\012";
const uint8_t gm_s9tnrhv72lohp[8] = ", not `";
const uint8_t gm_s9vg8k4mbj38f[22] = "compiler/eval/eval.gm";
const uint8_t gm_sa027i133kcv3[4] = "F16";
const uint8_t gm_sa349un8b060v[19] = "unsigned __int128 ";
const uint8_t gm_sa9sfrgc5vaao[5] = " >> ";
const uint8_t gm_sae23ckujbpp9[107] = "regular string literals may not contain line terminators; use a raw string (backticks) for multi-line text";
const uint8_t gm_safiul2d9a6fl[14] = "unterminated ";
const uint8_t gm_safvj03783ie0[65] = "declare it with `::` to make it a compile-time constant (\302\24717.2)";
const uint8_t gm_sajhum9b4nr96[2] = "+";
const uint8_t gm_sal17k00cm1q9[23] = "({})(({}){} {} ({}){})";
const uint8_t gm_samr3e6eqsavu[27] = "a place assigned to itself";
const uint8_t gm_sannl4idt68d4[42] = "`: it is immutable (declare it with `:=`)";
const uint8_t gm_saqgvkkehjsjb[33] = " (in an instantiation used here)";
const uint8_t gm_saqhrs072atsq[10] = "({} / {})";
const uint8_t gm_sar0vrndv51ep[14] = "gm_rt_acquire";
const uint8_t gm_sar7ovt8nasdo[8] = "Riscv64";
const uint8_t gm_sau1b8ivpr0us[8] = "GM_F16(";
const uint8_t gm_saunlif71pbir[16] = "gm_scratch_grow";
const uint8_t gm_sb5it581p5nll[5] = "Shl(";
const uint8_t gm_sb7lq2a8rdpq3[10] = "atomic_or";
const uint8_t gm_sb8kl54a499bf[7] = "(&F.h)";
const uint8_t gm_sb9dglh7ekrt2[7] = ", b);\012";
const uint8_t gm_sb9k4ip91d7oa[6] = ": \", ";
const uint8_t gm_sb9k60eb8i01r[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\200\003\000\000\031\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sbel6qookv0gl[54] = "internal error: `abs` of a value that is not a number";
const uint8_t gm_sbf8eactntf1g[21] = "{} cannot be ordered";
const uint8_t gm_sbgj0biroecr0[13] = "fs_close_dir";
const uint8_t gm_sbl3pbs9195ho[4] = "abs";
const uint8_t gm_sblao71d2av8h[22] = "unterminated {} quote";
const uint8_t gm_sbngfgv31nmur[61] = "`{}` has no arithmetic: only many-pointers `[^]T` do (\302\2475.5)";
const uint8_t gm_sbo52miqad506[4] = "uri";
const uint8_t gm_sbrifoc7ea21m[7] = "Wasm32";
const uint8_t gm_sbrtnmpf78kqq[10] = "GM_RETURN";
const uint8_t gm_sbs1lj6q8rfiu[44] = "variadic arguments are written positionally";
const uint8_t gm_sc103nruhmor7[3] = "\\x";
const uint8_t gm_sc24a25pi012e[8] = "))) \077 -";
const uint8_t gm_scb1vtgm37aqf[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\232\001\000\000\"\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_scedkbh0j6d78[6] = " r = ";
const uint8_t gm_sck2rq9li012q[23] = "    gm_init_consts();\012";
const uint8_t gm_sckeklr60tlq3[34] = "integer overflow in `pow` ({}^{})";
const uint8_t gm_scn1hel0olv2p[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\215\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_scnb11g2ghrd0[35] = "after the pattern of the match arm";
const uint8_t gm_scohuf4dfj32r[37] = ".ascii \"gdb.inlined-script.gamma\\n\"\012";
const uint8_t gm_scp8gj1kjauec[29] = "(uint8_t)(__builtin_signbit(";
const uint8_t gm_scte4ush0iqha[7] = "] = 1;";
const uint8_t gm_sd7guj32ltqh8[47] = "expected a variant, constant or struct pattern";
const uint8_t gm_sd7vrpqmmkbm0[111] = "an effect is declared at top level; inside a function only aliases, structs and unions can be declared (\302\2473.5)";
const uint8_t gm_sdcigk5ajbcfi[68] = "the literal {} does not fit in `{}` (in an instantiation used here)";
const uint8_t gm_sdh44op4kc2is[15] = "gm_overflowing";
const uint8_t gm_sdib10lkj65nm[45] = "` is not a C-compatible result type (\302\24719.1)";
const uint8_t gm_sdib2k2ql9mgl[9] = "op_scale";
const uint8_t gm_sdjba9ailb8m1[4] = "lsp";
const uint8_t gm_sdk5n6a42f7nl[3] = "0x";
const uint8_t gm_sdlbqh39uq50t[13] = "(uint8_t *)(";
const uint8_t gm_sdllavet8v9kb[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000G\001\000\000H\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_sdugj3qbhl84q[5] = " : (";
const uint8_t gm_sdujhgrk0oln8[5] = "only";
const uint8_t gm_sdvpeftn9n4aa[7] = "{}_s{}";
const uint8_t gm_se3smlbkt8d02[37] = "if this is deliberate, name it `_{}`";
const uint8_t gm_se3u415f6s7p1[13] = "gm_cont_take";
const uint8_t gm_se7h6nse0tc33[26] = "to begin the `when` block";
const uint8_t gm_se7oqjo2sonp9[9] = "\"><code>";
const uint8_t gm_se8cpd8s09lkc[13] = "the module `";
const uint8_t gm_sedk3p1dc2it3[17] = "\000\000\000\000\000\000\000\000\"\000\000\000\000\000\000\000";
const uint8_t gm_sedq8b22f9agn[91] = "`cast` converts between pointer types, not from `{}` to `{}`; use `.addr()` for an address";
const uint8_t gm_sefm4vafv1a3l[3] = "\012/";
const uint8_t gm_seh4hl8ei3cha[20] = "dependency cycle: `";
const uint8_t gm_sehgil1ui47md[7] = ", a + ";
const uint8_t gm_seiuio4eghuvu[19] = "too many arguments";
const uint8_t gm_sel2c30qvm74f[7] = "BitXor";
const uint8_t gm_senjrqtakqrde[16] = "the declaration";
const uint8_t gm_set3pi1jhetbi[7] = "` ends";
const uint8_t gm_sf3fmcr9mrflm[15] = "evok \077 {} : {}";
const uint8_t gm_sf5llhuhdlajp[9] = "FsRemove";
const uint8_t gm_sf8e06i5juo6a[29] = "static void {}(gm_fiber *fb)";
const uint8_t gm_sfaeuqh39av6i[28] = "unwrapping this error value";
const uint8_t gm_sfeuhas7a05k6[6] = "{}\012{}";
const uint8_t gm_sfir6p8a22i6s[31] = " parameters, but this one has ";
const uint8_t gm_sfknvqdnjah7l[18] = "; gm_unw.label = ";
const uint8_t gm_sfm5elvivaoue[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\005\001\000\000E\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sfr3tv65k63d4[16] = "double-negation";
const uint8_t gm_sfsnsm5njae7n[19] = "static const char ";
const uint8_t gm_sg1uf2uigo01q[27] = "this compile-time optional";
const uint8_t gm_sg398rgmck67b[4] = "};\012";
const uint8_t gm_sg3ba4vpic0jb[19] = "__atomic_fetch_xor";
const uint8_t gm_sg46oe1nh64ub[9] = "closure ";
const uint8_t gm_sg7n1u5l63uaa[9] = "soa_free";
const uint8_t gm_sgbcvt6rqctvd[7] = "->fw, ";
const uint8_t gm_sgge76tb2bmvi[32] = "workspace/didChangeWatchedFiles";
const uint8_t gm_sgip9ekbho6mc[11] = "` scales `";
const uint8_t gm_sgm565ktbj9ua[12] = "ProcWriteIn";
const uint8_t gm_sgn9uq6nipp5t[33] = "`ctl` operations at compile time";
const uint8_t gm_sgrpmaenv85i1[73] = "`#soa` takes no arguments and is written once: `#soa [N]S` or `#soa []S`";
const uint8_t gm_sgsd94uuisj8t[73] = ": instantiations with different arguments are different effects (\302\24712.3)";
const uint8_t gm_sh1v5m99cfmp8[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000%\001\000\000\036\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sh50kc23mi3vj[36] = "{}({}.code, {}.env, {}, {}, {}, {})";
const uint8_t gm_sh7mmr8b17j77[29] = "__attribute__((aligned(4))) ";
const uint8_t gm_sh9g09icusqpu[14] = ": its field `";
const uint8_t gm_shc0s7vbfquc0[20] = "extern uint8_t gm_g";
const uint8_t gm_shdh4dd2ejebj[158] = "`, which a generic function does not know; a `#soa` of a type parameter has whole elements (`a[i]`), `len`, slicing, `copy_from`, `make` and `free` (\302\2475.6.4)";
const uint8_t gm_sheqs83o4tgp8[45] = "fail if gamma.lock is missing or out of date";
const uint8_t gm_shh36vc8p1t4e[7] = " error";
const uint8_t gm_shhak6l2rrn43[15] = "({} - {} % {})";
const uint8_t gm_shheebrtu0dtr[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000*\002\000\000\014\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_shj3jonnfj09m[8] = "when {}";
const uint8_t gm_shja5bmmh8ag2[30] = "the import `{}` is never used";
const uint8_t gm_shkhe2ptevja3[18] = "</code></a></li>\012";
const uint8_t gm_shki2pqrgmpd1[19] = "/opt/fil/bin/filcc";
const uint8_t gm_shlilv0hq3bqu[33] = "invalid boolean, expected `true`";
const uint8_t gm_shmdne5cle16i[8] = "write `";
const uint8_t gm_shnig5tqtdedi[4] = "f16";
const uint8_t gm_shqseesnqrpjf[22] = "expected comma after ";
const uint8_t gm_shrlc4g1vtjsu[47] = "` needs an element for every variant: missing ";
const uint8_t gm_sht5g388qmj1f[15] = "already exists";
const uint8_t gm_shv4snn6f8hcr[29] = ")` required by this function";
const uint8_t gm_shvgs2jq7ohhl[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\025\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_si2v1nj23o22k[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\271\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_si4k9tiofr8t2[7] = "LaneEq";
const uint8_t gm_si5jb1hd2qqp1[12] = "slice{}{}{}";
const uint8_t gm_si6sr5gqkcui7[6] = "); }\012";
const uint8_t gm_si8s9ids0otre[44] = "` is a module, which is not a value (\302\2473.2)";
const uint8_t gm_siamquc19uikr[2] = "%";
const uint8_t gm_sicgmpca63jln[2] = "0";
const uint8_t gm_sigl9de72ni6d[16] = "gm_thread_yield";
const uint8_t gm_siiln09e6475q[5] = "i128";
const uint8_t gm_silt1tnb0phmp[3] = "-S";
const uint8_t gm_sj0qpc1n9qui9[7] = "it was";
const uint8_t gm_sj1m58fkg956m[8] = "FromTag";
const uint8_t gm_sj3ehailc2pgo[52] = "`in` compares with `is`, which is not defined for `";
const uint8_t gm_sj7s3tnnvob4e[9] = "lanes_eq";
const uint8_t gm_sjchrspn764h6[30] = "gm_buf mb = {0}; size_t ml = ";
const uint8_t gm_sje5p1r4d76uk[57] = "a range cannot be an operand here; put it in parentheses";
const uint8_t gm_sje7od1nuputq[921] = "\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000=\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\0002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000.\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000M\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000e\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000:\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000F\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000(\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000f\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000c\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000p\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000I\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000C\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\077\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000E\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000)\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000<\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000M\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000a\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sjerq54hvns79[11] = "builtin {}";
const uint8_t gm_sjfqm9gvu1om9[8] = "` lanes";
const uint8_t gm_sjhnkp7qigia6[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000S\001\000\000\033\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sjid61i849qic[3] = "+\077";
const uint8_t gm_sjm2vqdj9hddi[51] = "unexpected character after wildcard in version req";
const uint8_t gm_sjmmigkb19htg[4] = "Big";
const uint8_t gm_sjojf6rrbl49u[6] = "case ";
const uint8_t gm_sjqmjo9ak50j8[5] = "rp{}";
const uint8_t gm_sjrg55dooppac[6] = "after";
const uint8_t gm_sk31jlojfl0rr[8] = "element";
const uint8_t gm_sk754mj3p1el6[2] = "[";
const uint8_t gm_sk88tp2337lf6[13] = "VolatileLoad";
const uint8_t gm_ska87i9f7924f[8] = "int32_t";
const uint8_t gm_skefd3dps40es[40] = "an inclusive range needs an upper bound";
const uint8_t gm_skeo8nnnc4o7f[40] = "`sha256` of dependency `{}` needs `url`";
const uint8_t gm_skgcaidqnnhjr[11] = "bitreverse";
const uint8_t gm_skj7cpsc7j6g9[5] = "rotl";
const uint8_t gm_skmo18eln23n6[16] = "parse_stmt_kind";
const uint8_t gm_skn7gps778eo2[3] = "-E";
const uint8_t gm_sko48juemga53[34] = "struct __attribute__((packed)) {}";
const uint8_t gm_sktopm8j9jc57[802] = "usage: gamma doc <module directory | file | package root> [options]\012\012Writes the documentation of the public declarations of a module (the directory of\012.gm files that `import` names, or one file), from their `///` comments, and the `//!`\012comments at the top of its files.\012\012options:\012  --format=markdown|html   the output format (default: markdown)\012  -o <path>                write to <path>: a directory gets one file per module and an\012                           index (index.md or index.html); a single module may also go\012                           to a file. Without -o one module is printed to stdout\012  --all                    document every module below the directory (needs -o)\012  --missing                warn about public declarations and modules without\012                           documentation\012";
const uint8_t gm_skvch52visg8i[13] = "this handler";
const uint8_t gm_sl0vvvamhv0to[12] = "gm_site_str";
const uint8_t gm_sl1a3p7pocs82[15] = ", (uintptr_t)(";
const uint8_t gm_sl2oiu9s0celr[49] = "\000\000\000\000\000\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_sl2sr5fl46c73[48] = "fmt: buffer too small for the formatted integer";
const uint8_t gm_sl63bnit79nfo[41] = "\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000C\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sl66hhhp1j2a6[14] = " has no tag `";
const uint8_t gm_slahfmk66v1v4[15] = "--max-filesize";
const uint8_t gm_slc0d2s6km3s5[5] = "-L{}";
const uint8_t gm_slc58j6a4e39a[56] = "` binds an optional or a result as it is, but this is `";
const uint8_t gm_slgmqgkioa4u6[3] = ": ";
const uint8_t gm_slj7i6lishtfg[47] = "a boolean constant cannot be converted to `{}`";
const uint8_t gm_sll7gknpe999d[4] = "-%=";
const uint8_t gm_slr574u01mn8m[17] = "gm_wasi_main_ctx";
const uint8_t gm_slshl1j266n3l[7] = "--warn";
const uint8_t gm_slsmq9u0kk3l3[6] = "gm_f_";
const uint8_t gm_sm1kt2tr7m23b[4] = "clz";
const uint8_t gm_sm3gke457vdm3[5] = "<<%=";
const uint8_t gm_sm4ujt9jgj9au[8] = "` has `";
const uint8_t gm_sm5balq7hen2r[3] = "po";
const uint8_t gm_sm8049sr7hsf6[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\366\001\000\000\014\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sm94b3smoinpd[60] = "internal error: a reduction of a value that is not a vector";
const uint8_t gm_smcnje2cdb03e[3] = "mn";
const uint8_t gm_smdj5floh97je[10] = "{}int{}_t";
const uint8_t gm_smggdkb5phvp0[56] = "the slice method `.{}` is missing from the core library";
const uint8_t gm_smo0niiqr9onn[11] = "deprecated";
const uint8_t gm_smrahl8ppkcu9[34] = "invalid integer width in suffix `";
const uint8_t gm_smrc1u3r4i5hd[7] = "note: ";
const uint8_t gm_smsf0v36m37b1[66] = "a `when` condition may not depend on a generic parameter (\302\24716.7)";
const uint8_t gm_smu6g4sneo92j[6] = "freed";
const uint8_t gm_smu8iti9fc903[85] = "`=>` takes an expression; a block body is written without it: `fn(\342\200\246) -> R { \342\200\246 }`";
const uint8_t gm_sn0ac8j2fho01[22] = "`{}` is not an effect";
const uint8_t gm_sn0d6pm4psq1h[21] = "` ends with the call";
const uint8_t gm_sn1puqmdafub0[14] = "unclosed `{}`";
const uint8_t gm_sn6gnsehoh5ru[7] = "to_ptr";
const uint8_t gm_snaenr23tbgmb[23] = "_Static_assert(sizeof(";
const uint8_t gm_snhskkd5kfgfa[31] = "`{}` has no meaning on vectors";
const uint8_t gm_snk6krn32vlub[10] = "<a href=\"";
const uint8_t gm_snls9i3gr3n3f[41] = "gamma.lock does not satisfy dependency `";
const uint8_t gm_snqmbfl2fr85m[17] = "cannot convert `";
const uint8_t gm_snqmeaue6lcfo[9] = "gm_atags";
const uint8_t gm_snrrubbi4s05g[16] = "` has a field `";
const uint8_t gm_snud2jia3fe8d[18] = "a handler literal";
const uint8_t gm_so1gbme93p3pe[5] = "sort";
const uint8_t gm_so25i3en68imc[27] = "(cx, a, file, line, col);\012";
const uint8_t gm_so2dhrd52m05k[49] = "` of a slice is a field, not a method: write `s.";
const uint8_t gm_so3ukp67pds22[190] = "a name in the position of an optional or a result below the top of a pattern matches only a present or success value (\302\2479.3); add an arm for the other case (such as `.V(null)`), or write `\077";
const uint8_t gm_so48b6sdoerku[18] = "u+rwX,u-s,g-s,a-t";
const uint8_t gm_sodgjk6pvm3b4[21] = "memory allocated at ";
const uint8_t gm_sodivosk5r8rb[2] = "\002";
const uint8_t gm_sojb2hhp0ln7r[31] = "    print(\"{}\\n\", __repl_type(";
const uint8_t gm_solsl3u2gfc4f[7] = "a test";
const uint8_t gm_sp0ce5j43v7u2[26] = "` has a payload; write `.";
const uint8_t gm_sp0rb1clvmpp5[5] = "ie{}";
const uint8_t gm_sp1srk1sv0crk[52] = "`for` iterates over ranges of integers, not of `{}`";
const uint8_t gm_sp24uj950qpkl[16] = "variants = {}_v";
const uint8_t gm_sp46pm3sna4bl[86] = "`{}` instantiates itself with ever-larger type arguments (more than {} deep) (\302\24716.7)";
const uint8_t gm_sp796mgrgp4mj[45] = "`=` or `:` and an initializer after the type";
const uint8_t gm_sp8i1c1kf79ks[3] = "lc";
const uint8_t gm_spaa4sseck2fg[45] = "`; use only one of version, tag, branch, rev";
const uint8_t gm_spaflckd9bou5[29] = "`never` has no values, so `@";
const uint8_t gm_spasf3abnfbjh[8] = "general";
const uint8_t gm_spbdk1kpko0k2[141] = "static void gm_start(void) {{\012    static int started;\012    if (started) return;\012    started = 1;\012    gm_debug = {};\012    gm_init_consts();\012}}\012";
const uint8_t gm_spducfcn5c083[69] = "`.{}` of a slice is a field, not a method: write `s.{}` without `()`";
const uint8_t gm_spervk999ikim[8] = "Aarch64";
const uint8_t gm_spf5cnp0l9k4n[449] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_spi47lg4tr80j[3] = "or";
const uint8_t gm_spl4qnmq2arsc[13] = "binding-stmt";
const uint8_t gm_spnr89edksdqp[4] = "), ";
const uint8_t gm_spt7rbjl7ucc9[8] = "prepare";
const uint8_t gm_sq0bes3qstkub[15] = ".exe\012/.gamma/\012";
const uint8_t gm_sq2es9bij87b7[14] = "negating `{}`";
const uint8_t gm_sq32stff79ol1[22] = "`, but its value is `";
const uint8_t gm_sq6e9d3107t3s[11] = "debug-info";
const uint8_t gm_sq8839lqmgrd7[11] = ",\"offset\":";
const uint8_t gm_sq8aqd2uj6ppq[12] = "diagnostics";
const uint8_t gm_sq9qefps1uqei[10] = "{}.{}: {}";
const uint8_t gm_sqa29afvhov50[6] = "gm_fr";
const uint8_t gm_sqbbhnhgkc31j[14] = "repo_dir_name";
const uint8_t gm_sqflguscq1uc5[6] = "guard";
const uint8_t gm_sqg70d5uem21e[35] = "const uint8_t *a, const uint8_t *b";
const uint8_t gm_sqit0ai5v3joc[23] = "remove the parentheses";
const uint8_t gm_sqk1q73sjq658[24] = "operator `{}` on floats";
const uint8_t gm_sqm6u7rqmi1il[19] = "a `for` expression";
const uint8_t gm_sqn7kgtu67hrg[42] = "internal error: not a file-system service";
const uint8_t gm_sqqvi1jr97u13[10] = "gm_cancel";
const uint8_t gm_sqrsljl0ejj01[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\025\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sqt8gk1nsci25[5] = ")) {";
const uint8_t gm_squ4ohvqdqaap[79] = "a vector's lanes are `i8`\342\200\223`i64`, `u8`\342\200\223`u64`, `f32`, `f64` or `bool`, not `";
const uint8_t gm_sqvn1en6c2n9e[32] = "` is stored where it outlives `";
const uint8_t gm_sqvvajmjm3fif[11] = ".tmp-{}-{}";
const uint8_t gm_sr0hr1ep2j2vv[5] = "root";
const uint8_t gm_sr5mkle2k9poq[28] = "rename it, for example to `";
const uint8_t gm_sr899jbhl9noj[3] = "-q";
const uint8_t gm_sremdpsmf4ala[50] = "a union's tag type must be an integer type, not `";
const uint8_t gm_srg9h7ti0cg3a[25] = "(field: \342\200\246)` (\302\2475.10.5)";
const uint8_t gm_sri1d6adh4kkk[11] = "(void *){}";
const uint8_t gm_srj7urnaf6a1u[9] = "handlers";
const uint8_t gm_srjcgu24i74p9[48] = "a compile-time parameter cannot have the type `";
const uint8_t gm_srk1d80eamfhd[15] = "a variant name";
const uint8_t gm_srq25fqvivo2q[73] = "(code, env, cx, d, n);\012    if (b.data) gm_buf_free(&b);\012    return u;\012}\012";
const uint8_t gm_srrm1sevfig2o[3] = "_`";
const uint8_t gm_srs4r9h62u04l[13] = "add_siblings";
const uint8_t gm_sru4dh297ccek[19] = "` for its target:\012";
const uint8_t gm_ss03naco6s5l7[5] = "gm_r";
const uint8_t gm_ss3i577tvjetf[8] = "-fwrapv";
const uint8_t gm_ss4337v5p4tj3[5] = ".len";
const uint8_t gm_ss54u8us11c4v[42] = "the clause for `{}` takes {} parameter(s)";
const uint8_t gm_ssfs2ebod65fl[38] = "` does not apply to complex constants";
const uint8_t gm_ssgn9mppmu3vl[16] = "index unchecked";
const uint8_t gm_sshhi6fn3vfaa[45] = "`cache` needs a subcommand: `dir` or `clean`";
const uint8_t gm_sshi9ffc6nkc4[10] = "is unused";
const uint8_t gm_ssi4rrma2q00g[7] = "static";
const uint8_t gm_ssi95ri2h4d02[28] = "integer overflow in `pow` (";
const uint8_t gm_ssici0eo813ko[17] = "timed out after ";
const uint8_t gm_ssmr7jgj9e42t[25] = "Free Software Foundation";
const uint8_t gm_ssr91slt3ah1c[11] = "(uint8_t)(";
const uint8_t gm_ssrd6bs7gcs4g[29] = "expected `{}`, found {} {}{}";
const uint8_t gm_sstppps49t867[11] = "lane count";
const uint8_t gm_sstqe1228ic6k[51] = "`{}` is not a C-compatible parameter type (\302\24719.1)";
const uint8_t gm_ssvci9220kuoj[45] = "internal error: a shuffle index out of range";
const uint8_t gm_ssvcneuafnfds[8] = "changes";
const uint8_t gm_st0jtdoe1b7a6[15] = "unknown name `";
const uint8_t gm_st28rv7m1lueb[9] = "invalid ";
const uint8_t gm_st3m5iduoqh8b[5] = "deps";
const uint8_t gm_st3rnlu97af0r[63] = "`@{}` writes through the pointer, so it needs a `^T`, not `{}`";
const uint8_t gm_st4r3sao0kja5[6] = "bswap";
const uint8_t gm_st53sho4849cg[18] = "kind = GM_K_FLOAT";
const uint8_t gm_st62vtcrpam9i[17] = "the handler of `";
const uint8_t gm_st6ksn77egld4[13] = "variant_name";
const uint8_t gm_stadskj9k22h7[30] = ": an element cannot be `null`";
const uint8_t gm_stcd71a4feia0[10] = "cell_addr";
const uint8_t gm_std877jr2ed05[29] = "unclosed table, expected `]`";
const uint8_t gm_stddp43pjd9r1[51] = " a, const char *file, uint32_t line, uint32_t col)";
const uint8_t gm_sterslo87taeu[39] = "never use the network for dependencies";
const uint8_t gm_stfp83525k3nr[7] = "little";
const uint8_t gm_stj45lnsahel3[30] = " __attribute__((aligned({})))";
const uint8_t gm_stlojc09k1lf1[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\330\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_stmgel74lrtrn[33] = "global variables at compile time";
const uint8_t gm_stn52kuiq0jk2[12] = "Unreachable";
const uint8_t gm_stoidbi3vfa99[30] = "cannot convert `{}` to `{}`{}";
const uint8_t gm_stqlepc346jgk[85] = " deep; does a generic function call itself with ever-larger type arguments\077 (\302\24716.7)";
const uint8_t gm_stum5f38vfqa2[15] = "content-length";
const uint8_t gm_stvja3baka61k[8] = "gm_sink";
const uint8_t gm_su20i5iji4ao3[4] = "a{}";
const uint8_t gm_su38t05utsk53[67] = "explicit tag values need an explicit tag type: `union(u8) { \342\200\246 }`";
const uint8_t gm_su3ahsl9p584f[9] = "IsFinite";
const uint8_t gm_su41v0g030vmk[95] = "`{}` is a symbol of this implementation's runtime, so it cannot name a foreign member (\302\24723.5)";
const uint8_t gm_su6m1tso8ssol[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\251\001\000\0004\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sufp89bevlq87[5] = "libs";
const uint8_t gm_suhlp72acbph4[43] = "a math vector has 2, 3 or 4 elements, not ";
const uint8_t gm_suia0naoru7mp[7] = "AcqRel";
const uint8_t gm_sukj4damah3dd[85] = "parameter {} is `{}` in one and `{}` in the other; only pointers convert to pointers";
const uint8_t gm_sulvvqvd5gcql[15] = "a handler of `";
const uint8_t gm_sumvvvaqn43sl[10] = ", which `";
const uint8_t gm_sut7mhrkhqp77[56] = "a pointer offset must be a `usize` or an `isize`, not `";
const uint8_t gm_suup20cg9lo2d[8] = " = *f; ";
const uint8_t gm_suvsl6ie8luas[15] = " in the cache\012";
const uint8_t gm_sv0odl408mcff[24] = "` leaves the repository";
const uint8_t gm_sv1bu4l4kq188[8] = "fs_seek";
const uint8_t gm_sv52j7o6hr9j8[10] = "Constants";
const uint8_t gm_sv56kn0notafb[17] = "complex constant";
const uint8_t gm_sv6dkmq38siqt[27] = "{}((double){}, (double){})";
const uint8_t gm_sv7tbc2gvmpa1[100] = "    long which = gm_test_index(argc, argv);\012    if (which < 0) return 2;\012    gm_set_args(1, argv);\012";
const uint8_t gm_sv8pauo1bhltl[9] = "lanes_le";
const uint8_t gm_sv96uhaheg6dq[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\031\001\000\000D\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_svdcmt7tps0ma[57] = "`--timeout` needs a positive number of seconds, not `{}`";
const uint8_t gm_sve2u8a7eapjo[18] = "typedef struct { ";
const uint8_t gm_svirf1u8t89n0[47] = "internal error: formatting would change token ";
const uint8_t gm_svl0gta90bsdr[49] = "`in` needs a range, array, slice or tuple, not `";
const uint8_t gm_svmt0iljnadus[4] = "t{}";
const uint8_t gm_svpr02j6er8vd[15] = "__builtin_fmaf";
const uint8_t gm_svsf0pu3t8u6m[7] = "write ";
uint8_t gm_c24kimhrphq[11] __attribute__((aligned(1)));
uint8_t gm_c26gi09a298[63] __attribute__((aligned(1)));
uint8_t gm_c2elhq7l2vf[4] __attribute__((aligned(1)));
uint8_t gm_c3a039uv4qb[16] __attribute__((aligned(1)));
uint8_t gm_c3nhitb2vug[10] __attribute__((aligned(1)));
uint8_t gm_c3qosqjl81k[8] __attribute__((aligned(8)));
uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
uint8_t gm_c4tkno4ihu4[3] __attribute__((aligned(1)));
uint8_t gm_c653lc0efgo[3] __attribute__((aligned(1)));
uint8_t gm_c6d5506jf7q[8] __attribute__((aligned(8)));
uint8_t gm_c6ef9harlnq[59] __attribute__((aligned(1)));
uint8_t gm_c6ojdgkpe75[3] __attribute__((aligned(1)));
uint8_t gm_c714gka9l4t[1] __attribute__((aligned(1)));
uint8_t gm_c7ivta2baoi[6] __attribute__((aligned(1)));
uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
uint8_t gm_c7p3n1736t1[4] __attribute__((aligned(4)));
uint8_t gm_c9b5effiaau[60] __attribute__((aligned(1)));
uint8_t gm_c9gbtic0o97[5] __attribute__((aligned(1)));
uint8_t gm_caafgbc6m0v[18] __attribute__((aligned(1)));
uint8_t gm_cai164r033u[16] __attribute__((aligned(8)));
uint8_t gm_cckdrlo8348[24] __attribute__((aligned(1)));
uint8_t gm_ce5dkit4sfq[12] __attribute__((aligned(1)));
uint8_t gm_cecimmkvaop[17] __attribute__((aligned(1)));
uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
uint8_t gm_cf51emcsnfi[16] __attribute__((aligned(1)));
uint8_t gm_cf5loitcbc7[13] __attribute__((aligned(1)));
uint8_t gm_cfalqv4rsam[7] __attribute__((aligned(1)));
uint8_t gm_cfclddbf5rj[9] __attribute__((aligned(1)));
uint8_t gm_cfv7g68ca5r[8] __attribute__((aligned(1)));
uint8_t gm_chkob26flgg[9] __attribute__((aligned(1)));
uint8_t gm_chrcl04adpj[25] __attribute__((aligned(1)));
uint8_t gm_chthounu1pg[8] __attribute__((aligned(1)));
uint8_t gm_ci5i3g6ghat[6] __attribute__((aligned(1)));
uint8_t gm_cigdn81ldc9[16] __attribute__((aligned(8)));
uint8_t gm_civobsgahf6[15] __attribute__((aligned(1)));
uint8_t gm_cjnmu2s2207[16] __attribute__((aligned(1)));
uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
uint8_t gm_ckpkn5umvr0[1] __attribute__((aligned(1)));
uint8_t gm_clps76qnh98[3] __attribute__((aligned(1)));
uint8_t gm_cm2dkubhtop[8] __attribute__((aligned(8)));
uint8_t gm_cm9r8d61pap[21] __attribute__((aligned(1)));
uint8_t gm_cmumimn4u17[6] __attribute__((aligned(1)));
uint8_t gm_cnc12norsln[7] __attribute__((aligned(1)));
uint8_t gm_cpk359ca82u[26] __attribute__((aligned(1)));
uint8_t gm_cplg36ag8b5[15] __attribute__((aligned(1)));
uint8_t gm_cpvard348ed[4] __attribute__((aligned(4)));
uint8_t gm_cq22cjd7d4i[13] __attribute__((aligned(1)));
uint8_t gm_cq3tl31s253[2] __attribute__((aligned(1)));
uint8_t gm_cqr2aac1us2[7] __attribute__((aligned(1)));
uint8_t gm_credir2fcqk[9] __attribute__((aligned(1)));
uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
uint8_t gm_cskujdbhaf5[12] __attribute__((aligned(1)));
uint8_t gm_css0is294pa[5] __attribute__((aligned(1)));
uint8_t gm_ctmke2ostj8[12] __attribute__((aligned(1)));
uint8_t gm_ctngbflld19[16] __attribute__((aligned(8)));
uint8_t gm_ctsgq16sumf[3] __attribute__((aligned(1)));
uint8_t gm_cu7epimqni2[3] __attribute__((aligned(1)));
uint8_t gm_curvqf1hf03[77] __attribute__((aligned(1)));
uint8_t gm_cuvdtuneoi6[10] __attribute__((aligned(1)));
uint8_t gm_cvo51u5rfub[14] __attribute__((aligned(1)));
uint8_t gm_k34avs9rqsl[40] __attribute__((aligned(8)));
uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
uint8_t gm_k8o4k1d1for[40] __attribute__((aligned(8)));
uint8_t gm_kbri4nir86d[40] __attribute__((aligned(8)));
uint8_t gm_keokac8bchm[40] __attribute__((aligned(8)));
uint8_t gm_kjh6c1ct3mq[40] __attribute__((aligned(8)));
uint8_t gm_kl5pit792cd[40] __attribute__((aligned(8)));
uint8_t gm_kpbp8co2u50[40] __attribute__((aligned(8)));
uint8_t gm_ksci05qti98[40] __attribute__((aligned(8)));
uint8_t gm_ku0iiu0o88g[40] __attribute__((aligned(8)));
uint8_t gm_kutnfkdjl3d[40] __attribute__((aligned(8)));
uint8_t gm_kv6tql7n7ah[40] __attribute__((aligned(8)));
void *const gm_hd3ip07hju0a[] = {(void *)gm_dt3ip07hju0a_0, (void *)gm_dt3ip07hju0a_1, (void *)gm_dt3ip07hju0a_2, (void *)gm_dt3ip07hju0a_3, (void *)gm_dt3ip07hju0a_4, (void *)gm_dt3ip07hju0a_5};
void *const gm_hdaqemfrshlp[] = {(void *)gm_dtaqemfrshlp_0, (void *)gm_dtaqemfrshlp_1, (void *)gm_dtaqemfrshlp_2, (void *)gm_dtaqemfrshlp_3, (void *)gm_dtaqemfrshlp_4, (void *)gm_dtaqemfrshlp_5, (void *)gm_dtaqemfrshlp_6};
void *const gm_hl5p278go8bp[] = {(void *)gm_f_for_body_6ji90d8};
void *const gm_hle6hel8vh71[] = {(void *)gm_f_for_body_fnpff4s};
void *const gm_hlh176qr0nti[] = {(void *)gm_f_for_body_nnvr1n3};
void *const gm_hlqklhf369l0[] = {(void *)gm_f_for_body_ghum27l};
