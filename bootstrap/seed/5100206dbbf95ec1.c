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
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; void * f2; } gm_t017ud7s0np4m;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t017ud7s0np4m) == 24 && _Alignof(gm_t017ud7s0np4m) == 8 && offsetof(gm_t017ud7s0np4m, f0) == 0 && offsetof(gm_t017ud7s0np4m, f1) == 8 && offsetof(gm_t017ud7s0np4m, f2) == 16, "layout of `(f: u32, sizes: ^Expr, aligns: ^Expr)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_t88rcucg1b7uk v1; } f1; } gm_t5e6kkuq21nig;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t p3[3]; gm_t88rcucg1b7uk f3; } gm_tcu60n24kv26r;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_b16_8 v1; } f1; } gm_te3magq8drarp;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tj4mbomtvccf0;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_t017ud7s0np4m f0; uint8_t f1; uint8_t pe[7]; } gm_tn6p6lpnve7b0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5e6kkuq21nig) == 16 && _Alignof(gm_t5e6kkuq21nig) == 8 && offsetof(gm_t5e6kkuq21nig, f0) == 0 && offsetof(gm_t5e6kkuq21nig, f1) == 8, "layout of `ArrayLen`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcu60n24kv26r) == 32 && _Alignof(gm_tcu60n24kv26r) == 8 && offsetof(gm_tcu60n24kv26r, f0) == 0 && offsetof(gm_tcu60n24kv26r, f1) == 8 && offsetof(gm_tcu60n24kv26r, f2) == 24 && offsetof(gm_tcu60n24kv26r, f3) == 28, "layout of `SoaMade`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te3magq8drarp) == 24 && _Alignof(gm_te3magq8drarp) == 8 && offsetof(gm_te3magq8drarp, f0) == 0 && offsetof(gm_te3magq8drarp, f1) == 8, "layout of `SoaTy`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4mbomtvccf0) == 32 && _Alignof(gm_tj4mbomtvccf0) == 8 && offsetof(gm_tj4mbomtvccf0, f0) == 0 && offsetof(gm_tj4mbomtvccf0, f1) == 8 && offsetof(gm_tj4mbomtvccf0, f2) == 24, "layout of `SoaTemplate`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tn6p6lpnve7b0) == 32 && _Alignof(gm_tn6p6lpnve7b0) == 8 && offsetof(gm_tn6p6lpnve7b0, f0) == 0 && offsetof(gm_tn6p6lpnve7b0, f1) == 24, "layout of `?(f: u32, sizes: ^Expr, aligns: ^Expr)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_toa2vhe3l4hfr a[1]; } gm_t2ubvi1j1aq22;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2ubvi1j1aq22) == 40 && _Alignof(gm_t2ubvi1j1aq22) == 8 && offsetof(gm_t2ubvi1j1aq22, a) == 0, "layout of `[1]GenericArg`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_compiler_adt_eiq6rf1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_trqfvmjtt4sij l4; /* defaults */
    gm_trqfvmjtt4sij l5; /* default_exprs */
    gm_t6art7bprjiqb l8; /* info */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_core_fn_of_jr7r6ef {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
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
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_one_arg_f1gj3pp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_soa_errors_m1bqopj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* seen */
    gm_trqfvmjtt4sij l3; /* errors */
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_same_len_bnkvi6r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_set_template_o09q96o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_block_ir2b1jv {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_column_7a5htvb {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_copy_columns_s4nhe5h {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_t0mglksrkfa5e l13; /* field */
    gm_trqfvmjtt4sij l16; /* pending */
    gm_trqfvmjtt4sij l19; /* args */
    void * ret;
};
struct gm_fr_soa_core_sd8eq62 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* pending */
    gm_tn6p6lpnve7b0 ret;
};
struct gm_fr_soa_expand_2e8rmqf {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_fields_dg06fgc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_free_columns_n1tvn1j {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* args */
    void * ret;
};
struct gm_fr_soa_gather_of_4timqsa {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* parts */
    gm_t0mglksrkfa5e l10; /* f */
    void * ret;
};
struct gm_fr_soa_generic_kvk21mq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_info_pq4q43l {
    gm_frame h;
    gm_t8cc1mtbrhlpt ret;
};
struct gm_fr_soa_layout_n4ibc7b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* sizes */
    gm_trqfvmjtt4sij l5; /* aligns */
    gm_t0mglksrkfa5e l8; /* f */
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_soa_len_of_4l5vk85 {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_make_columns_ardr0ua {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* args */
    gm_trqfvmjtt4sij l17; /* parts */
    gm_trqfvmjtt4sij l26; /* arms */
    void * ret;
};
struct gm_fr_soa_repeatable_rqn3pcn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_soa_scatter_h5ruqu8 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* stmts */
    gm_t0mglksrkfa5e l11; /* f */
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_755ppqp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_soa_shape_of_ohenvjc {
    gm_frame h;
    gm_tbm0n9ujhrlhf ret;
};
struct gm_fr_soa_slice_column_6ogdk5f {
    gm_frame h;
    void * ret;
};
struct gm_fr_soa_slice_columns_kqq8dfr {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* parts */
    void * ret;
};
struct gm_fr_soa_stand_in_kdtmkn7 {
    gm_frame h;
    gm_t2ubvi1j1aq22 l5; /* <temporary array> */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_temp_2pshn8g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_template_fa784hs {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_soa_to_const_hhiu49k {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* parts */
    void * ret;
};
struct gm_fr_soa_type_42lvsj0 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_expr_go3p0n7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_soa_type_of_q8vtd5j {
    gm_frame h;
    gm_t6art7bprjiqb l8; /* info */
    gm_tpautr4br7h3l l10; /* def */
    gm_t0mglksrkfa5e l13; /* f */
    gm_trqfvmjtt4sij l18; /* columns */
    gm_t0mglksrkfa5e l21; /* f */
    gm_t2ubvi1j1aq22 l26; /* <temporary array> */
    gm_te3magq8drarp ret;
};
struct gm_fr_subst_soa_2chqte3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_lit_a8hojea {
    gm_frame h;
    void * ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t5e6kkuq21nig gm_f_array_len_eielif2(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_16dceus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3dfoht6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4nn4ect(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_agi9f7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fambdq4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j0i128e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_nt3g9ko(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3dc3qci(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_mf0vgpj(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_ftmshk7(void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9);
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_k2ok1rai48t[40] __attribute__((aligned(8)));
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_k6tu981lmau[40] __attribute__((aligned(8)));
extern uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s58rgrgmho9sa[68];
extern const uint8_t gm_sfflovsm8rlpo[51];
extern const uint8_t gm_sg7n1u5l63uaa[9];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sidairr6d8hcq[45];
extern const uint8_t gm_sm2k5tp7dtg07[4];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4nj79mj9jk57[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssnb4or831s5l[78];
extern const uint8_t gm_suh0878eve8t9[10];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tiilmfd16v3fc t2;
    memset((uint8_t *)&t2 + 4, 0, 4);
    uint32_t t3 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
    gm_t53at80pfdus3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t5 + 8), t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tpautr4br7h3l t1;
    memset((uint8_t *)&t1 + 17, 0, 7);
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    GM_ST(uint8_t, (uint8_t *)&t1 + 16, ((uint8_t)0));
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t1 + 24, t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t1 + 40, t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0, ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_89glhjr F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 136);
        gm_b16_8 t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src0ih77f43mhf2, 263, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 176);
        uint32_t t12 = gm_l2;
        gm_tftb1odc739t5 t13 = gm_f_push_im9d4ro(t11, t12);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_src0ih77f43mhf2, 264, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        uint32_t t15 = gm_l2;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)1ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_epvducg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5nn7ph0();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 208u);
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
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 311, 83);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_arg_f1gj3pp F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 75u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tftb1odc739t5 t4 = gm_f_push_d943vgp_2(t2, &(*(gm_t69m56meqefs8 *)((uint8_t *)t3)));
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_src4nj79mj9jk57, 404, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_eardf07 F;
    void * gm_l0; /* ds */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 176) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src0ih77f43mhf2, 272, 5);
        uint32_t t7 = gm_l2;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 176) + 0)) + t4 * 4), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_fields_dg06fgc F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 0));
    gm_th2lqbo77pvsc t3;
    gm_f_struct_fields_chqu3it((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_b16_8 t4;
    if (((((uint8_t *)&t3))[16] != 0)) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
        t4 = t5;
    } else {
        gm_b16_8 t6 = gm_f_empty_slice_ose1ba9();
        t4 = t6;
    }
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_generic_kvk21mq F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 0));
    uint8_t t4 = gm_f_has_param_u9t0efs(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_usize_lit_a8hojea F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* v */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t4 = gm_l1;
    gm_tbci5ospk5nnn t5;
    gm_f_from_u64_ud13q5v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_teti411eujvf4 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t6 + 8), t5);
    gm_tl874t2atnlh3 t7 = gm_l2;
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_compiler_adt_eiq6rf1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, const void * a2, uint64_t a3) {
    struct gm_fr_compiler_adt_eiq6rf1 F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    uint64_t gm_l3; /* fields */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    {
        gm_f_list_s83jn2q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = gm_l3;
            gm_l6 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l7;
                uint64_t t6 = gm_l6;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l7;
                    {
                        void * t14 = (void *)((uint8_t *)&F.l4);
                        gm_tftb1odc739t5 t15 = gm_f_push_vd1290p(t14, ((uint8_t)0));
                        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_src4nj79mj9jk57, 141, 9);
                        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
                    }
                    {
                        void * t17 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t18 = gm_f_push_r2k6rjf_2(t17, ((void *)0));
                        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_src4nj79mj9jk57, 142, 9);
                        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 140, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t21 = gm_l7;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l7 = t24;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t25 = gm_l0;
        uint64_t t26 = gm_l1;
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tf5srh4mp4b3i t28;
        memset(&t28, 0, sizeof t28);
        gm_f_new_adt_info_5pl2srl((void *)&F.l8, t25, t26, &(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)), GM_CONV(gm_cseahvg8v7p0vs, t27), t28);
    }
    {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 80), t29);
    }
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l8) + 96), t30);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l8), 184);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_core_fn_of_jr7r6ef(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_core_fn_of_jr7r6ef F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* mname */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* d */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_agi9f7f((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_src4nj79mj9jk57, 55, 14);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l3 = t5;
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 336) + 0));
            gm_l4 = t9;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l5;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l5;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 8));
                    uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                    gm_l6 = t22;
                    {
                        uint64_t t25 = gm_l6;
                        uint64_t t26 = (uintptr_t)t25;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 200) + 0) + 8);
                        if (t26 >= (uintptr_t)(t29)) gm_trap_index(t26, t29, gm_src4nj79mj9jk57, 57, 12);
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 200) + 0)) + t26 * 184) + 0));
                        gm_b16_8 t31 = gm_l3;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                        uint8_t t33 = (!gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t32)));
                        if (!t33) goto ie24;
                        {
                            goto lc10;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    {
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                        uint64_t t37 = gm_l6;
                        gm_b16_8 t38 = gm_l2;
                        gm_tf5srh4mp4b3i t39 = gm_f_lookup_59juqoo(t36, t37, t38);
                        uint64_t t40;
                        if (((((uint8_t *)&t39))[8] != 0)) {
                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                            t40 = t41;
                        } else {
                            gm_t9805qtonbsag t42;
                            memset(&t42, 0, sizeof t42);
                            uint8_t t43 = 0;
                            F.ret = t42;
                            goto gm_fret;
                        }
                        gm_l7 = t40;
                    }
                    {
                        void * t45 = gm_l0;
                        uint64_t t46 = gm_l7;
                        gm_t6qooom8qa2do t47;
                        gm_f_resolve_decl_pus042v((void *)&t47, (void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
                        if (t48 != ((uint8_t)5ULL)) goto ie44;
                        uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&t47) + 8));
                        gm_l8 = t49;
                        {
                            uint32_t t51 = gm_l8;
                            gm_t9805qtonbsag t52;
                            GM_ST(uint8_t, (uint8_t *)&t52 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t52 + 6, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t52), t51);
                            ((uint8_t *)&t52)[4] = 1;
                            uint8_t t53 = 0;
                            F.ret = t52;
                            goto gm_fret;
                        }
                        goto ix44;
ie44:;
                        {
                            gm_t9805qtonbsag t55;
                            memset(&t55, 0, sizeof t55);
                            uint8_t t56 = 0;
                            F.ret = t55;
                            goto gm_fret;
                        }
ix44:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 56, 5);
me20:;
                }
lc10:;
                {
                    uint64_t t58 = gm_l5;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l5 = t61;
                }
            }
gm_ll14:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
            }
            goto gm_lad;
lb10:;
        }
    }
    {
        gm_t9805qtonbsag t62;
        memset(&t62, 0, sizeof t62);
        uint8_t t63 = 0;
        F.ret = t62;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_one_arg_f1gj3pp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_report_soa_errors_m1bqopj F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 2456));
        F.l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 2456);
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l4 = t7;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l5;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 16));
                    gm_l6 = t19;
                    {
                        gm_b16_8 t22 = gm_l6;
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
                        uint8_t t24 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t23) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t23))) + i * 16, ((uint8_t *)&t22))) { t24 = 1; break; }
                        uint8_t t25 = (uint8_t)(t24);
                        if (!t25) goto ie21;
                        {
                            goto lc8;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    {
                        void * t27 = gm_l0;
                        gm_tl874t2atnlh3 t28 = gm_l1;
                        gm_b16_8 t29 = gm_l6;
                        gm_tnpifv20irnph t30;
                        GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                        gm_tabb710d9mq2e t31;
                        gm_f_format_mlqiori((void *)&t31, (void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_src4nj79mj9jk57, 196, 23);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        gm_b0_1 t34 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t27, t28, t33);
                        if (gm_unw.kind) {
                            goto gm_ll12;
                        }
                    }
                    {
                        void * t35 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t36 = gm_l6;
                        gm_tftb1odc739t5 t37 = gm_f_push_3ef746b(t35, t36);
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src4nj79mj9jk57, 197, 9);
                        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 194, 5);
me17:;
                }
lc8:;
                {
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_ll12:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
            }
            goto gm_lad;
lb8:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_len_bnkvi6r(gm_tf5srh4mp4b3i a0, gm_tf5srh4mp4b3i a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_len_bnkvi6r F;
    gm_tf5srh4mp4b3i gm_l0; /* a */
    gm_tf5srh4mp4b3i gm_l1; /* b */
    uint64_t gm_l2; /* x */
    uint64_t gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
        gm_l2 = t3;
        {
            if (!((((uint8_t *)&gm_l1))[8] != 0)) goto ie5;
            uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
            gm_l3 = t6;
            {
                uint64_t t8 = gm_l2;
                uint64_t t9 = gm_l3;
                uint8_t t10 = ((t8 == t9));
                uint8_t t11 = 0;
                F.ret = t10;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        {
            uint8_t t12 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t13;
        if (((((uint8_t *)&gm_l1))[8] != 0)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 50, 12);
me14:;
        uint8_t t17 = 0;
        F.ret = t13;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_soa_block_ir2b1jv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_block_ir2b1jv F;
    gm_b16_8 gm_l0; /* stmts */
    void * gm_l1; /* tail */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l1;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l1;
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + 0));
        gm_tgabaikr08mjg t10;
        memset((uint8_t *)&t10 + 36, 0, 4);
        gm_b16_8 t11 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        void * t12 = gm_l1;
        GM_ST(void *, (uint8_t *)&t10 + 16, t12);
        gm_tl874t2atnlh3 t13 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t10 + 24, t13);
        gm_tq7s98ihonkju t14 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t10, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khikhlemh0i)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_src4nj79mj9jk57, 238, 35);
        void * t15 = GM_LD(void *, (((uint8_t *)&t14) + 8));
        gm_teti411eujvf4 t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t16 + 8), t15);
        gm_tl874t2atnlh3 t17 = gm_l2;
        void * t18 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t9, &t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = 0;
        F.ret = t18;
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
__attribute__((aligned(4))) void * gm_f_soa_column_7a5htvb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_column_7a5htvb F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_t88rcucg1b7uk gm_l6; /* ft */
    void * gm_l7; /* col */
    uint8_t gm_l8; /* is_const */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 820u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
        gm_th2lqbo77pvsc t5;
        gm_f_struct_fields_chqu3it((void *)&t5, t2, GM_CONV(gm_csguankko4lmhd, t4));
        gm_b16_8 t6;
        if (((((uint8_t *)&t5))[16] != 0)) {
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_f_empty_slice_ose1ba9();
            t6 = t8;
        }
        gm_l5 = t6;
    }
    {
        uint64_t t9 = gm_l3;
        uint64_t t10 = (uintptr_t)t9;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src4nj79mj9jk57, 258, 11);
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t10 * 40) + 16));
        gm_l6 = t12;
    }
    {
        gm_t88rcucg1b7uk t13 = gm_l6;
        void * t14 = gm_l1;
        void * t15 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t16 = gm_l3;
        uint32_t t17 = (uint32_t)t16;
        gm_tbl8il92l97ut t18;
        GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t18 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
        gm_teti411eujvf4 t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t19 + 8), t18);
        gm_tl874t2atnlh3 t20 = gm_l4;
        void * t21 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t21;
    }
    {
        uint8_t t23;
        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn25;
        t23 = ((uint8_t)0);
        goto me24;
mn25:;
        t23 = ((uint8_t)1);
        goto me24;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 260, 8);
me24:;
        if (!t23) goto ie22;
        {
            void * t28 = gm_l7;
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t31 = gm_l0;
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        gm_t88rcucg1b7uk t33 = gm_l6;
        gm_t53at80pfdus3 t34;
        gm_f_kind_jcnt4hm((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
        if (t35 != ((uint8_t)9ULL)) goto ie30;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&t34) + 8) + 0));
        gm_l8 = t36;
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t34) + 8) + 4));
        gm_l9 = t37;
        {
            void * t39 = gm_l0;
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t42 = gm_l1;
            void * t43 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t r45; bool o46 = __builtin_sub_overflow(t44, ((uint64_t)1ULL), &r45);
            if (o46) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 262, 64);
            uint64_t t47 = r45;
            uint32_t t48 = (uint32_t)t47;
            gm_tbl8il92l97ut t49;
            GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t49 + 0), t43);
            GM_ST(uint32_t, ((uint8_t *)&t49 + 8), t48);
            gm_teti411eujvf4 t50;
            memset(&t50, 0, sizeof t50);
            GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t50 + 8), t49);
            gm_tl874t2atnlh3 t51 = gm_l4;
            void * t52 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t41, &t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t52;
        }
        {
            void * t53 = gm_l0;
            void * t54 = (void *)(((uint8_t *)t53) + 8);
            uint8_t t55 = gm_l8;
            gm_t88rcucg1b7uk t56 = gm_l9;
            gm_t88rcucg1b7uk t57 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tao63pk3r18v5 t58;
            memset((uint8_t *)&t58 + 25, 0, 7);
            void * t59 = gm_l7;
            GM_ST(void *, (uint8_t *)&t58 + 0, t59);
            void * t60 = gm_l0;
            gm_tl874t2atnlh3 t61 = gm_l4;
            void * t62 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t60, ((uint64_t)0ULL), t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t58 + 8, t62);
            void * t63 = gm_l10;
            GM_ST(void *, (uint8_t *)&t58 + 16, t63);
            GM_ST(uint8_t, (uint8_t *)&t58 + 24, ((uint8_t)0));
            gm_teti411eujvf4 t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)21ULL));
            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t64 + 8), t58);
            gm_tl874t2atnlh3 t65 = gm_l4;
            void * t66 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t57, &t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix30;
ie30:;
        {
            void * t69 = gm_l7;
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
ix30:;
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
__attribute__((aligned(4))) void * gm_f_soa_copy_columns_s4nhe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_copy_columns_s4nhe5h F;
    void * gm_l0; /* s */
    void * gm_l1; /* dst */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* src */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint32_t gm_l6; /* f */
    gm_tjpqvfd72o62u gm_l7; /* from */
    gm_b16_8 gm_l8; /* fields */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    void * gm_l14; /* d */
    void * gm_l15; /* c */
    gm_tnqk2qt7j42o9 gm_l17; /* <temporary array> */
    uint32_t gm_l18; /* inst */
    GM_STACK_CHECK(&F, 1711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sgvvkov5kvprt));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(9));
        gm_t9805qtonbsag t7 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t8;
        if (((((uint8_t *)&t7))[4] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            {
                void * t11 = gm_l0;
                gm_tl874t2atnlh3 t12 = gm_l4;
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sidairr6d8hcq));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(44));
                gm_b0_1 t14 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t11, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t15 = gm_l5;
            gm_t88rcucg1b7uk t16 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t17 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            gm_tl874t2atnlh3 t19 = gm_l4;
            void * t20 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        gm_l6 = t8;
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l3;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_t8cc1mtbrhlpt t25;
        gm_f_soa_info_pq4q43l((void *)&t25, t22, GM_CONV(gm_csguankko4lmhd, t24));
        gm_tjpqvfd72o62u t26;
        if (((((uint8_t *)&t25))[32] != 0)) {
            t26 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t25) + 0));
        } else {
            void * t27 = gm_l5;
            gm_t88rcucg1b7uk t28 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t29 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t30;
            memset(&t30, 0, sizeof t30);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 8), t29);
            gm_tl874t2atnlh3 t31 = gm_l4;
            void * t32 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t28, &t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = 0;
            F.ret = t32;
            goto gm_fret;
        }
        gm_l7 = t26;
    }
    {
        void * t34 = gm_l0;
        gm_b16_8 t35 = gm_f_soa_fields_dg06fgc(t34, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l8 = t35;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t37 = gm_l8;
            gm_l10 = t37;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t39 = gm_l11;
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t41 = (t39 < t40);
                if (!t41) goto lb38;
                {
                    gm_t8ta482bl6be0 t44;
                    uint64_t t45 = gm_l11;
                    GM_ST(uint64_t, (uint8_t *)&t44 + 0, t45);
                    uint64_t t46 = gm_l11;
                    uint64_t t47 = (uintptr_t)t46;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    memcpy((uint8_t *)&t44 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t47 * 40), 40);
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&t44) + 0));
                    gm_l12 = t51;
                    F.l13 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t44) + 8));
                    {
                        void * t53 = gm_l0;
                        void * t54 = gm_l1;
                        uint64_t t55 = gm_l12;
                        gm_tl874t2atnlh3 t56 = gm_l4;
                        void * t57 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t53, t54, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t55, t56);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        gm_l14 = t57;
                    }
                    {
                        uint8_t t59;
                        if ((((((uint8_t *)a2) + 8))[8] != 0)) goto mn61;
                        t59 = ((uint8_t)0);
                        goto me60;
mn61:;
                        t59 = ((uint8_t)1);
                        goto me60;
mn62:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 438, 12);
me60:;
                        if (!t59) goto ie58;
                        {
                            void * t64 = gm_l5;
                            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_t88rcucg1b7uk t66 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t64, ((uint8_t)0), t65);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                            gm_tao63pk3r18v5 t67;
                            memset((uint8_t *)&t67 + 25, 0, 7);
                            void * t68 = gm_l14;
                            GM_ST(void *, (uint8_t *)&t67 + 0, t68);
                            GM_ST(void *, (uint8_t *)&t67 + 8, ((void *)0));
                            GM_ST(void *, (uint8_t *)&t67 + 16, ((void *)0));
                            GM_ST(uint8_t, (uint8_t *)&t67 + 24, ((uint8_t)0));
                            gm_teti411eujvf4 t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t69 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t69 + 8), t67);
                            gm_tl874t2atnlh3 t70 = gm_l4;
                            void * t71 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t66, &t69, t70);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                            gm_l14 = t71;
                        }
                        goto ix58;
ie58:;
ix58:;
                    }
                    {
                        void * t72 = gm_l0;
                        void * t73 = gm_l3;
                        uint64_t t74 = gm_l12;
                        gm_tl874t2atnlh3 t75 = gm_l4;
                        void * t76 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t72, t73, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l7)), t74, t75);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        gm_l15 = t76;
                    }
                    {
                        void * t77 = gm_l0;
                        gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t77) + 2456));
                        F.l16 = t78;
                    }
                    {
                        void * t79 = gm_l0;
                        uint8_t *t80 = (((uint8_t *)t79) + 2456);
                        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t80), (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                    }
                    {
                    }
                    {
                        void * t81 = gm_l0;
                        uint32_t t82 = gm_l6;
                        {
                            gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                            gm_tnqk2qt7j42o9 t85;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t85 + 0), t84);
                            gm_l17 = t85;
                        }
                        void * t86 = ((uint8_t *)&gm_l17);
                        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 445, 34);
                        gm_b16_8 t87;
                        GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t86 + (uintptr_t)0 * 4)));
                        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                        gm_b16_8 t88 = t87;
                        gm_tl874t2atnlh3 t89 = gm_l4;
                        uint32_t t90 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t81, t82, t88, t89);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        gm_l18 = t90;
                    }
                    {
                        void * t91 = gm_l0;
                        gm_trqfvmjtt4sij t92 = F.l16;
                        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t91) + 2456), t92);
                    }
                    {
                        gm_f_list_vp1djv1_2((void *)&F.l19, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                    }
                    {
                        void * t93 = (void *)((uint8_t *)&F.l19);
                        void * t94 = gm_l14;
                        gm_tftb1odc739t5 t95 = gm_f_push_d943vgp_2(t93, &(*(gm_t69m56meqefs8 *)((uint8_t *)t94)));
                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_src4nj79mj9jk57, 448, 9);
                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                    }
                    {
                        void * t97 = (void *)((uint8_t *)&F.l19);
                        void * t98 = gm_l15;
                        gm_tftb1odc739t5 t99 = gm_f_push_d943vgp_2(t97, &(*(gm_t69m56meqefs8 *)((uint8_t *)t98)));
                        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 1, gm_src4nj79mj9jk57, 449, 9);
                        gm_b0_1 t100 = GM_LD(gm_b0_1, (((uint8_t *)&t99) + 1));
                    }
                    {
                        void * t101 = (void *)((uint8_t *)&F.l9);
                        uint32_t t102 = gm_l18;
                        uint64_t t103 = (uint64_t)t102;
                        uint64_t t104 = (uintptr_t)t103;
                        void * t105 = gm_l0;
                        uint64_t t106 = GM_LD(uintptr_t, ((((uint8_t *)t105) + 344) + 0) + 8);
                        if (t104 >= (uintptr_t)(t106)) gm_trap_index(t104, t106, gm_src4nj79mj9jk57, 450, 33);
                        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t105) + 344) + 0)) + t104 * 136) + 56));
                        gm_to76jli3bqddq t108;
                        uint32_t t109 = gm_l18;
                        gm_tsth9qu1dv0g3 t110;
                        memset(&t110, 0, sizeof t110);
                        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)0ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t110 + 8), t109);
                        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t108 + 0, t110);
                        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t108 + 40, t111);
                        gm_teti411eujvf4 t112;
                        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)34ULL));
                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t112 + 8), t108);
                        gm_tl874t2atnlh3 t113 = gm_l4;
                        void * t114 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t107, &t112, t113);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        gm_trcmmd12cefb1 t115;
                        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t115 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t115 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)4ULL));
                        GM_ST(void *, ((uint8_t *)&t115 + 8), t114);
                        gm_tftb1odc739t5 t116 = gm_f_push_d6c76ud_2(t101, &t115);
                        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_src4nj79mj9jk57, 450, 9);
                        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                    }
                    goto me49;
mn50:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 436, 5);
me49:;
                }
lc38:;
                {
                    uint64_t t119 = gm_l11;
                    uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                    uint64_t t122 = r120;
                    gm_l11 = t122;
                }
            }
gm_ll42:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
            }
            goto gm_lad;
lb38:;
        }
    }
    {
        void * t123 = gm_l5;
        gm_t88rcucg1b7uk t124 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t123);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgabaikr08mjg t125;
        memset((uint8_t *)&t125 + 36, 0, 4);
        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t125 + 0, t126);
        GM_ST(void *, (uint8_t *)&t125 + 16, ((void *)0));
        gm_tl874t2atnlh3 t127 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t125 + 24, t127);
        gm_tq7s98ihonkju t128 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t125, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k6tu981lmau)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_src4nj79mj9jk57, 452, 38);
        void * t129 = GM_LD(void *, (((uint8_t *)&t128) + 8));
        gm_teti411eujvf4 t130;
        memset(&t130, 0, sizeof t130);
        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)46ULL));
        GM_ST(void *, ((uint8_t *)&t130 + 8), t129);
        gm_tl874t2atnlh3 t131 = gm_l4;
        void * t132 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t124, &t130, t131);
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
inline __attribute__((aligned(4))) void gm_f_soa_core_sd8eq62(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_core_sd8eq62 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* f */
    gm_t3mf58gv9kf3n gm_l5; /* layouts */
    gm_t88rcucg1b7uk gm_l6; /* k */
    gm_tnqk2qt7j42o9 gm_l8; /* <temporary array> */
    uint32_t gm_l9; /* inst */
    GM_STACK_CHECK(&F, 717u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss496b65r9omj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = gm_l1;
        gm_t9805qtonbsag t5 = gm_f_core_fn_of_jr7r6ef((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t6;
        if (((((uint8_t *)&t5))[4] != 0)) {
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            {
                void * t9 = gm_l0;
                gm_tl874t2atnlh3 t10 = gm_l3;
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sfflovsm8rlpo));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(50));
                gm_b0_1 t12 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t9, t10, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tn6p6lpnve7b0 t13;
            memset(&t13, 0, sizeof t13);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l4 = t6;
    }
    {
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_f_soa_layout_n4ibc7b((void *)NULL, cx, NULL, t15, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        void * t20 = gm_l0;
        void * t21 = gm_l0;
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t24 = gm_l0;
        gm_b16_8 t25 = gm_f_soa_fields_dg06fgc(t24, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&t25) + 8);
        uint64_t t27 = (uint64_t)t26;
        gm_tbci5ospk5nnn t28;
        gm_f_from_u64_ud13q5v((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t428ue2u3hf5g t29;
        gm_f_int_key_dmcraup((void *)&t29, (void *)NULL, cx, NULL, t20, t23, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)21ULL));
        GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t30 + 8), t29);
        gm_t88rcucg1b7uk t31 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t19, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t31;
    }
    {
        void * t32 = gm_l0;
        gm_trqfvmjtt4sij t33 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t32) + 2456));
        F.l7 = t33;
    }
    {
        void * t34 = gm_l0;
        uint8_t *t35 = (((uint8_t *)t34) + 2456);
        gm_f_list_epvducg((void *)&(*(gm_trqfvmjtt4sij *)t35), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t36 = gm_l0;
        uint32_t t37 = gm_l4;
        {
            gm_t88rcucg1b7uk t39 = gm_l6;
            gm_tnqk2qt7j42o9 t40;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40 + 0), t39);
            gm_l8 = t40;
        }
        void * t41 = ((uint8_t *)&gm_l8);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 350, 30);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t43 = t42;
        gm_tl874t2atnlh3 t44 = gm_l3;
        uint32_t t45 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t36, t37, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t45;
    }
    {
        void * t46 = gm_l0;
        gm_trqfvmjtt4sij t47 = F.l7;
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t46) + 2456), t47);
    }
    {
        gm_t017ud7s0np4m t48;
        memset((uint8_t *)&t48 + 4, 0, 4);
        uint32_t t49 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t48 + 0, t49);
        void * t50 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        GM_ST(void *, (uint8_t *)&t48 + 8, t50);
        void * t51 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
        GM_ST(void *, (uint8_t *)&t48 + 16, t51);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 28, 0);
        ((uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out))[24] = 1;
        GM_ST(gm_t017ud7s0np4m, (uint8_t *)&(*(gm_tn6p6lpnve7b0 *)gm_out) + 0, t48);
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
__attribute__((aligned(4))) void * gm_f_soa_expand_2e8rmqf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_soa_expand_2e8rmqf F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    void * gm_l2; /* e */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tandvbt63uk7n gm_l4; /* x */
    gm_tesaltf0mhcki gm_l5; /* op */
    gm_b16_8 gm_l6; /* args */
    void * gm_l7; /* err */
    gm_t88rcucg1b7uk gm_l8; /* operand */
    gm_t88rcucg1b7uk gm_l9; /* ok */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_b16_8 gm_l11; /* stmts */
    gm_toh0ootodhjue gm_l12; /* b */
    gm_tjpqvfd72o62u gm_l13; /* to */
    uint64_t gm_l14; /* next */
    void * gm_l15; /* lo */
    void * gm_l16; /* hi */
    GM_STACK_CHECK(&F, 1713u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 72));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l2;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)57ULL)) goto ie4;
        gm_tandvbt63uk7n t7 = GM_LD(gm_tandvbt63uk7n, ((((uint8_t *)t5) + 8) + 8));
        gm_l4 = t7;
        {
            gm_tesaltf0mhcki t9 = GM_LD(gm_tesaltf0mhcki, (((uint8_t *)&gm_l4) + 0));
            gm_l5 = t9;
        }
        {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_l6 = t10;
        }
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 8);
            gm_t88rcucg1b7uk t13 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t14 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            gm_tl874t2atnlh3 t16 = gm_l3;
            void * t17 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t17;
        }
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            gm_t88rcucg1b7uk t20 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t20;
        }
        {
            uint8_t t22 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l5) + 0) == ((uint8_t)5ULL)));
            if (!t22) goto ie21;
            {
                void * t25 = gm_l0;
                void * t26 = (void *)(((uint8_t *)t25) + 8);
                void * t27 = gm_l2;
                gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
                gm_t53at80pfdus3 t29;
                gm_f_kind_jcnt4hm((void *)&t29, t26, GM_CONV(gm_csguankko4lmhd, t28));
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
                if (t30 != ((uint8_t)14ULL)) goto ie24;
                gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 4));
                gm_l9 = t31;
                {
                    gm_t88rcucg1b7uk t33 = gm_l9;
                    gm_l8 = t33;
                }
                goto ix24;
ie24:;
ix24:;
            }
            goto ix21;
ie21:;
            {
                uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src4nj79mj9jk57, 468, 19);
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t35 * 88) + 0));
                gm_l8 = t37;
            }
ix21:;
        }
        {
            void * t38 = gm_l0;
            gm_t88rcucg1b7uk t39 = gm_l8;
            gm_t8cc1mtbrhlpt t40;
            gm_f_soa_info_pq4q43l((void *)&t40, t38, GM_CONV(gm_csguankko4lmhd, t39));
            gm_tjpqvfd72o62u t41;
            if (((((uint8_t *)&t40))[32] != 0)) {
                t41 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t40) + 0));
            } else {
                void * t42 = gm_l7;
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            gm_l10 = t41;
        }
        {
            void * t45 = gm_l0;
            uint8_t t46 = gm_f_soa_generic_kvk21mq(t45, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)));
            if (!t46) goto ie44;
            {
                void * t48 = gm_l7;
                uint8_t t49 = 0;
                F.ret = t48;
                goto gm_fret;
            }
            goto ix44;
ie44:;
ix44:;
        }
        {
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            switch (t60) { case ((uint8_t)0ULL): goto ma51; case ((uint8_t)1ULL): goto ma52; case ((uint8_t)2ULL): goto ma53; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma55; case ((uint8_t)5ULL): goto ma56; case ((uint8_t)6ULL): goto ma57; case ((uint8_t)7ULL): goto ma58; default: goto ma59; }
ma51:;
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t62 != ((uint8_t)0ULL)) goto mn61;
            void * t63 = gm_l0;
            uint64_t t64 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_src4nj79mj9jk57, 473, 41);
            void * t66 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t64 * 88);
            uint64_t t67 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_src4nj79mj9jk57, 473, 57);
            void * t69 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t67 * 88);
            gm_tl874t2atnlh3 t70 = gm_l3;
            void * t71 = gm_f_soa_gather_of_4timqsa((void *)NULL, cx, NULL, t63, t66, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
            goto me50;
mn61:;
ma52:;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t74 != ((uint8_t)1ULL)) goto mn73;
            {
                void * t76 = gm_l0;
                uint64_t t77 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_src4nj79mj9jk57, 475, 37);
                void * t79 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t77 * 88);
                uint64_t t80 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_src4nj79mj9jk57, 475, 53);
                void * t82 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t80 * 88);
                uint64_t t83 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_src4nj79mj9jk57, 475, 63);
                void * t85 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t83 * 88);
                gm_tl874t2atnlh3 t86 = gm_l3;
                gm_b16_8 t87 = gm_f_soa_scatter_h5ruqu8((void *)NULL, cx, NULL, t76, t79, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t82, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t87;
            }
            {
                void * t88 = gm_l0;
                void * t89 = (void *)(((uint8_t *)t88) + 8);
                gm_t88rcucg1b7uk t90 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tgabaikr08mjg t91;
                memset((uint8_t *)&t91 + 36, 0, 4);
                gm_b16_8 t92 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t91 + 0, t92);
                GM_ST(void *, (uint8_t *)&t91 + 16, ((void *)0));
                gm_tl874t2atnlh3 t93 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t91 + 24, t93);
                gm_tq7s98ihonkju t94 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t91, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2ok1rai48t)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_src4nj79mj9jk57, 476, 48);
                void * t95 = GM_LD(void *, (((uint8_t *)&t94) + 8));
                gm_teti411eujvf4 t96;
                memset(&t96, 0, sizeof t96);
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)46ULL));
                GM_ST(void *, ((uint8_t *)&t96 + 8), t95);
                gm_tl874t2atnlh3 t97 = gm_l3;
                void * t98 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t90, &t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            goto me50;
mn73:;
ma53:;
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t101 != ((uint8_t)2ULL)) goto mn100;
            void * t102 = gm_l0;
            uint64_t t103 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_src4nj79mj9jk57, 478, 38);
            void * t105 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t103 * 88);
            gm_tl874t2atnlh3 t106 = gm_l3;
            void * t107 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t102, t105, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t108 = 0;
            F.ret = t107;
            goto gm_fret;
            goto me50;
mn100:;
ma54:;
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t110 != ((uint8_t)3ULL)) goto mn109;
            gm_toh0ootodhjue t111 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l5) + 1));
            gm_l12 = t111;
            {
                void * t113 = gm_l0;
                void * t114 = gm_l2;
                gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t114) + 0));
                gm_t8cc1mtbrhlpt t116;
                gm_f_soa_info_pq4q43l((void *)&t116, t113, GM_CONV(gm_csguankko4lmhd, t115));
                gm_tjpqvfd72o62u t117;
                if (((((uint8_t *)&t116))[32] != 0)) {
                    t117 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t116) + 0));
                } else {
                    void * t118 = gm_l7;
                    uint8_t t119 = 0;
                    F.ret = t118;
                    goto gm_fret;
                }
                gm_l13 = t117;
            }
            {
                void * t121 = gm_l0;
                uint8_t t122 = gm_f_soa_generic_kvk21mq(t121, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l13)));
                if (!t122) goto ie120;
                {
                    void * t124 = gm_l7;
                    uint8_t t125 = 0;
                    F.ret = t124;
                    goto gm_fret;
                }
                goto ix120;
ie120:;
ix120:;
            }
            {
                gm_l14 = ((uint64_t)1ULL);
            }
            {
                gm_l15 = ((void *)0);
            }
            {
                gm_l16 = ((void *)0);
            }
            {
                uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                if (!t127) goto ie126;
                {
                    uint64_t t129 = gm_l14;
                    uint64_t t130 = (uintptr_t)t129;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t130 >= (uintptr_t)(t131)) gm_trap_index(t130, t131, gm_src4nj79mj9jk57, 486, 23);
                    void * t132 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t130 * 88);
                    gm_l15 = t132;
                }
                {
                    uint64_t t133 = gm_l14;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                    if (o135) gm_trap("integer overflow in `+`", gm_src4nj79mj9jk57, 487, 17);
                    uint64_t t136 = r134;
                    gm_l14 = t136;
                }
                goto ix126;
ie126:;
ix126:;
            }
            {
                uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 1));
                if (!t138) goto ie137;
                {
                    uint64_t t140 = gm_l14;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_src4nj79mj9jk57, 489, 29);
                    void * t143 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t141 * 88);
                    gm_l16 = t143;
                }
                goto ix137;
ie137:;
ix137:;
            }
            {
                void * t144 = gm_l0;
                uint64_t t145 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_src4nj79mj9jk57, 490, 41);
                void * t147 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t145 * 88);
                uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 24));
                void * t149 = gm_l15;
                void * t150 = gm_l16;
                uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 2));
                gm_tl874t2atnlh3 t152 = gm_l3;
                void * t153 = gm_f_soa_slice_columns_kqq8dfr((void *)NULL, cx, NULL, t144, t147, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t148, t149, t150, t151, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t154 = 0;
                F.ret = t153;
                goto gm_fret;
            }
            goto me50;
mn109:;
ma55:;
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t156 != ((uint8_t)4ULL)) goto mn155;
            void * t157 = gm_l0;
            uint64_t t158 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_src4nj79mj9jk57, 492, 44);
            void * t160 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t158 * 88);
            uint64_t t161 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t161 >= (uintptr_t)(t162)) gm_trap_index(t161, t162, gm_src4nj79mj9jk57, 492, 53);
            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t161 * 88) + 0));
            void * t164 = gm_l2;
            gm_t88rcucg1b7uk t165 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t164) + 0));
            gm_tl874t2atnlh3 t166 = gm_l3;
            void * t167 = gm_f_soa_to_const_hhiu49k((void *)NULL, cx, NULL, t157, t160, t163, t165, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t168 = 0;
            F.ret = t167;
            goto gm_fret;
            goto me50;
mn155:;
ma56:;
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t170 != ((uint8_t)5ULL)) goto mn169;
            void * t171 = gm_l0;
            void * t172 = gm_l1;
            gm_t88rcucg1b7uk t173 = gm_l8;
            uint64_t t174 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_src4nj79mj9jk57, 493, 68);
            void * t176 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t174 * 88);
            uint64_t t177 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t177 >= (uintptr_t)(t178)) gm_trap_index(t177, t178, gm_src4nj79mj9jk57, 493, 78);
            void * t179 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t177 * 88);
            gm_tl874t2atnlh3 t180 = gm_l3;
            void * t181 = gm_f_soa_make_columns_ardr0ua((void *)NULL, cx, NULL, t171, t172, t173, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t176, t179, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t182 = 0;
            F.ret = t181;
            goto gm_fret;
            goto me50;
mn169:;
ma57:;
            uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t184 != ((uint8_t)6ULL)) goto mn183;
            void * t185 = gm_l0;
            uint64_t t186 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t186 >= (uintptr_t)(t187)) gm_trap_index(t186, t187, gm_src4nj79mj9jk57, 494, 45);
            void * t188 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t186 * 88);
            uint64_t t189 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t190 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t189 >= (uintptr_t)(t190)) gm_trap_index(t189, t190, gm_src4nj79mj9jk57, 494, 61);
            void * t191 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t189 * 88);
            gm_tl874t2atnlh3 t192 = gm_l3;
            void * t193 = gm_f_soa_free_columns_n1tvn1j((void *)NULL, cx, NULL, t185, t188, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t191, t192);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t194 = 0;
            F.ret = t193;
            goto gm_fret;
            goto me50;
mn183:;
ma58:;
            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            if (t196 != ((uint8_t)7ULL)) goto mn195;
            void * t197 = gm_l0;
            uint64_t t198 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t199 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t198 >= (uintptr_t)(t199)) gm_trap_index(t198, t199, gm_src4nj79mj9jk57, 495, 49);
            void * t200 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t198 * 88);
            uint64_t t201 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_src4nj79mj9jk57, 495, 65);
            void * t203 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t201 * 88);
            gm_tl874t2atnlh3 t204 = gm_l3;
            void * t205 = gm_f_soa_copy_columns_s4nhe5h((void *)NULL, cx, NULL, t197, t200, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t203, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t206 = 0;
            F.ret = t205;
            goto gm_fret;
            goto me50;
mn195:;
ma59:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 472, 5);
me50:;
        }
        goto ix4;
ie4:;
        {
            void * t208 = gm_l2;
            uint8_t t209 = 0;
            F.ret = t208;
            goto gm_fret;
        }
ix4:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_fields_dg06fgc_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_fields_dg06fgc(a0, &a1); }
__attribute__((aligned(4))) void * gm_f_soa_free_columns_n1tvn1j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_free_columns_n1tvn1j F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* site */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t017ud7s0np4m gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* first */
    void * gm_l8; /* base */
    void * gm_l9; /* raw */
    void * gm_l10; /* len */
    GM_STACK_CHECK(&F, 1148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sg7n1u5l63uaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(8));
        gm_tl874t2atnlh3 t6 = gm_l4;
        gm_tn6p6lpnve7b0 t7;
        gm_f_soa_core_sd8eq62((void *)&t7, (void *)NULL, cx, NULL, t4, t5, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t017ud7s0np4m t8;
        if (((((uint8_t *)&t7))[24] != 0)) {
            t8 = (*(gm_t017ud7s0np4m *)(((uint8_t *)&t7) + 0));
        } else {
            void * t9 = gm_l5;
            gm_t88rcucg1b7uk t10 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t12;
            memset(&t12, 0, sizeof t12);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
            gm_tl874t2atnlh3 t13 = gm_l4;
            void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l6 = t8;
    }
    {
        uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 0));
        gm_th2lqbo77pvsc t20;
        gm_f_struct_fields_chqu3it((void *)&t20, t17, GM_CONV(gm_csguankko4lmhd, t19));
        gm_b16_8 t21;
        if (((((uint8_t *)&t20))[16] != 0)) {
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            gm_b16_8 t23 = gm_f_empty_slice_ose1ba9();
            t21 = t23;
        }
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&t21) + 8);
        if (t16 >= (uintptr_t)(t24)) gm_trap_index(t16, t24, gm_src4nj79mj9jk57, 412, 14);
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t21)) + t16 * 40) + 16));
        gm_l7 = t25;
    }
    {
        gm_t88rcucg1b7uk t26 = gm_l7;
        void * t27 = gm_l1;
        void * t28 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbl8il92l97ut t29;
        GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t29 + 0), t28);
        GM_ST(uint32_t, ((uint8_t *)&t29 + 8), ((uint32_t)0ULL));
        gm_teti411eujvf4 t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t30 + 8), t29);
        gm_tl874t2atnlh3 t31 = gm_l4;
        void * t32 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t26, &t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t32;
    }
    {
        void * t33 = gm_l5;
        gm_t53at80pfdus3 t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t35 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t33, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tmn7n0n9raou5 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)6ULL));
        void * t37 = gm_l8;
        gm_t8jsc4n9vp2ib t38;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t38 + 0), t36);
        GM_ST(void *, ((uint8_t *)&t38 + 8), t37);
        gm_teti411eujvf4 t39;
        memset(&t39, 0, sizeof t39);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)32ULL));
        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t39 + 8), t38);
        gm_tl874t2atnlh3 t40 = gm_l4;
        void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t41;
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l1;
        gm_tl874t2atnlh3 t44 = gm_l4;
        void * t45 = gm_f_soa_len_of_4l5vk85((void *)NULL, cx, NULL, t42, t43, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t45;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l11);
        void * t47 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
        gm_tftb1odc739t5 t48 = gm_f_push_d943vgp_2(t46, &(*(gm_t69m56meqefs8 *)((uint8_t *)t47)));
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src4nj79mj9jk57, 417, 5);
        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l11);
        void * t51 = GM_LD(void *, (((uint8_t *)&gm_l6) + 16));
        gm_tftb1odc739t5 t52 = gm_f_push_d943vgp_2(t50, &(*(gm_t69m56meqefs8 *)((uint8_t *)t51)));
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_src4nj79mj9jk57, 418, 5);
        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
    }
    {
        void * t54 = (void *)((uint8_t *)&F.l11);
        void * t55 = gm_l10;
        gm_tftb1odc739t5 t56 = gm_f_push_d943vgp_2(t54, &(*(gm_t69m56meqefs8 *)((uint8_t *)t55)));
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_src4nj79mj9jk57, 419, 5);
        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
    }
    {
        void * t58 = (void *)((uint8_t *)&F.l11);
        void * t59 = gm_l9;
        gm_tftb1odc739t5 t60 = gm_f_push_d943vgp_2(t58, &(*(gm_t69m56meqefs8 *)((uint8_t *)t59)));
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_src4nj79mj9jk57, 420, 5);
        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
    }
    {
        void * t62 = (void *)((uint8_t *)&F.l11);
        void * t63 = gm_l3;
        gm_tftb1odc739t5 t64 = gm_f_push_d943vgp_2(t62, &(*(gm_t69m56meqefs8 *)((uint8_t *)t63)));
        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_src4nj79mj9jk57, 421, 5);
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
    }
    {
        uint32_t t66 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        uint64_t t67 = (uint64_t)t66;
        uint64_t t68 = (uintptr_t)t67;
        void * t69 = gm_l0;
        uint64_t t70 = GM_LD(uintptr_t, ((((uint8_t *)t69) + 344) + 0) + 8);
        if (t68 >= (uintptr_t)(t70)) gm_trap_index(t68, t70, gm_src4nj79mj9jk57, 422, 19);
        gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t69) + 344) + 0)) + t68 * 136) + 56));
        gm_to76jli3bqddq t72;
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
        gm_tsth9qu1dv0g3 t74;
        memset(&t74, 0, sizeof t74);
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t74 + 8), t73);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t72 + 0, t74);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t72 + 40, t75);
        gm_teti411eujvf4 t76;
        GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t76 + 8), t72);
        gm_tl874t2atnlh3 t77 = gm_l4;
        void * t78 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t71, &t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t79 = 0;
        F.ret = t78;
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
inline __attribute__((aligned(4))) void * gm_f_soa_gather_of_4timqsa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_gather_of_4timqsa F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* k */
    void * gm_l11; /* col */
    GM_STACK_CHECK(&F, 596u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l5 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l5;
            gm_l7 = t5;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l8;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    gm_t8ta482bl6be0 t12;
                    uint64_t t13 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l8;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t15 * 40), 40);
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                    gm_l9 = t19;
                    F.l10 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t12) + 8));
                    {
                        void * t21 = gm_l0;
                        void * t22 = gm_l1;
                        uint64_t t23 = gm_l9;
                        gm_tl874t2atnlh3 t24 = gm_l4;
                        void * t25 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t21, t22, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l11 = t25;
                    }
                    {
                        void * t26 = (void *)((uint8_t *)&F.l6);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_tlcl4u558l6p5 t28;
                        memset((uint8_t *)&t28 + 17, 0, 7);
                        void * t29 = gm_l11;
                        GM_ST(void *, (uint8_t *)&t28 + 0, t29);
                        void * t30 = gm_l3;
                        void * t31 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        GM_ST(void *, (uint8_t *)&t28 + 8, t31);
                        GM_ST(uint8_t, (uint8_t *)&t28 + 16, ((uint8_t)1));
                        gm_teti411eujvf4 t32;
                        memset(&t32, 0, sizeof t32);
                        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t32 + 8), t28);
                        gm_tl874t2atnlh3 t33 = gm_l4;
                        void * t34 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t27, &t32, t33);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t35 = gm_f_push_d943vgp_2(t26, &(*(gm_t69m56meqefs8 *)((uint8_t *)t34)));
                        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_src4nj79mj9jk57, 280, 9);
                        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 278, 5);
me17:;
                }
lc6:;
                {
                    uint64_t t38 = gm_l8;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l8 = t41;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_teti411eujvf4 t44;
        memset(&t44, 0, sizeof t44);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 8), t43);
        gm_tl874t2atnlh3 t45 = gm_l4;
        void * t46 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t42, &t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t47 = 0;
        F.ret = t46;
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
extern __attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_soa_generic_kvk21mq_c(void * a0, gm_tjpqvfd72o62u a1) { return gm_f_soa_generic_kvk21mq(a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_info_pq4q43l F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l6; /* tm */
    gm_t88rcucg1b7uk gm_l7; /* elem */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 8));
        gm_l3 = t9;
        {
            {
                void * t12 = gm_l0;
                gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 2416) + 0));
                gm_l4 = t13;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t17 = (t15 < t16);
                    if (!t17) goto lb14;
                    {
                        uint64_t t20 = gm_l5;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tj4mbomtvccf0 t25 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 32));
                        gm_l6 = t25;
                        {
                            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                            uint32_t t29 = gm_l2;
                            uint8_t t30 = ((t28 == t29));
                            if (!t30) goto ie27;
                            {
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                uint8_t t34 = (t33 > ((uint64_t)0ULL));
                                if (!t34) goto ie32;
                                uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src4nj79mj9jk57, 74, 48);
                                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 40) + 0));
                                if (t37 != ((uint8_t)0ULL)) goto ie32;
                                gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 40) + 8));
                                gm_l7 = t38;
                                {
                                    gm_tjpqvfd72o62u t40;
                                    memset((uint8_t *)&t40 + 4, 0, 4);
                                    memset((uint8_t *)&t40 + 25, 0, 7);
                                    gm_t88rcucg1b7uk t41 = gm_l7;
                                    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t40 + 0, t41);
                                    gm_tf5srh4mp4b3i t42 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l6) + 8));
                                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t40 + 8, t42);
                                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 24));
                                    GM_ST(uint8_t, (uint8_t *)&t40 + 24, t43);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 33, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 34, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 36, 0);
                                    ((uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out))[32] = 1;
                                    GM_ST(gm_tjpqvfd72o62u, (uint8_t *)&(*(gm_t8cc1mtbrhlpt *)gm_out) + 0, t40);
                                    goto gm_fdone;
                                }
                                goto ix32;
ie32:;
ix32:;
                            }
                            {
                                gm_t8cc1mtbrhlpt t44;
                                memset(&t44, 0, sizeof t44);
                                uint8_t t45 = 0;
                                F.ret = t44;
                                goto gm_fret;
                            }
                            goto ix27;
ie27:;
ix27:;
                        }
                        goto me23;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 72, 5);
me23:;
                    }
lc14:;
                    {
                        uint64_t t47 = gm_l5;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l5 = t50;
                    }
                }
lb14:;
            }
        }
        {
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 2320);
            uint32_t t53 = gm_l2;
            gm_f_get_nt3g9ko((void *)&(*(gm_t8cc1mtbrhlpt *)gm_out), t52, t53);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_t8cc1mtbrhlpt t55;
            memset(&t55, 0, sizeof t55);
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t8cc1mtbrhlpt gm_f_soa_info_pq4q43l_c(void * a0, gm_csguankko4lmhd a1) { gm_t8cc1mtbrhlpt gm_r; gm_f_soa_info_pq4q43l(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_soa_layout_n4ibc7b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_layout_n4ibc7b F;
    void * gm_l0; /* s */
    gm_tjpqvfd72o62u gm_l1; /* info */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tbccvikg6b96e gm_l9; /* l */
    uint64_t gm_l10; /* n */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a1)));
        gm_l3 = t3;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l3;
            gm_l6 = t5;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l7;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l7;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t0mglksrkfa5e t17 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 40));
                    F.l8 = t17;
                    {
                        void * t19 = gm_l0;
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                        gm_f_layout_a5ovnrm((void *)&gm_l9, (void *)NULL, cx, NULL, t19, t20);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    {
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        void * t22 = gm_l0;
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
                        gm_tl874t2atnlh3 t24 = gm_l2;
                        void * t25 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t22, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t26 = gm_f_push_d943vgp_2(t21, &(*(gm_t69m56meqefs8 *)((uint8_t *)t25)));
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src4nj79mj9jk57, 362, 9);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    {
                        void * t28 = (void *)((uint8_t *)&F.l5);
                        void * t29 = gm_l0;
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        gm_tl874t2atnlh3 t31 = gm_l2;
                        void * t32 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t29, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t33 = gm_f_push_d943vgp_2(t28, &(*(gm_t69m56meqefs8 *)((uint8_t *)t32)));
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src4nj79mj9jk57, 363, 9);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 360, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t36 = gm_l7;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l7 = t39;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint64_t t41 = (uint64_t)t40;
        gm_l10 = t41;
    }
    {
        void * t42 = gm_l0;
        void * t43 = (void *)(((uint8_t *)t42) + 8);
        uint64_t t44 = gm_l10;
        void * t45 = gm_l0;
        void * t46 = (void *)(((uint8_t *)t45) + 8);
        gm_t88rcucg1b7uk t47 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t48 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t43, t44, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t48;
    }
    {
        gm_t88rcucg1b7uk t49 = gm_l11;
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_teti411eujvf4 t51;
        memset(&t51, 0, sizeof t51);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 8), t50);
        gm_tl874t2atnlh3 t52 = gm_l2;
        void * t53 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t54 = gm_l11;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_teti411eujvf4 t56;
        memset(&t56, 0, sizeof t56);
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)36ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 8), t55);
        gm_tl874t2atnlh3 t57 = gm_l2;
        void * t58 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t59;
        GM_ST(void *, ((uint8_t *)&t59 + 0), t53);
        GM_ST(void *, ((uint8_t *)&t59 + 8), t58);
        uint8_t t60 = 0;
        F.ret = t59;
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
inline __attribute__((aligned(4))) void * gm_f_soa_len_of_4l5vk85(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_len_of_4l5vk85 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* n */
    gm_b16_8 gm_l5; /* fields */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        if (!(((((uint8_t *)a2) + 8))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
        gm_l4 = t3;
        {
            void * t5 = gm_l0;
            uint64_t t6 = gm_l4;
            gm_tl874t2atnlh3 t7 = gm_l3;
            void * t8 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t5, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t11) + 0));
        gm_th2lqbo77pvsc t13;
        gm_f_struct_fields_chqu3it((void *)&t13, t10, GM_CONV(gm_csguankko4lmhd, t12));
        gm_b16_8 t14;
        if (((((uint8_t *)&t13))[16] != 0)) {
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_b16_8 t16 = gm_f_empty_slice_ose1ba9();
            t14 = t16;
        }
        gm_l5 = t14;
    }
    {
        uint64_t t17;
        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t20 = ((t19 == ((uint64_t)0ULL)));
        if (!t20) goto ie18;
        uint64_t t22 = ((uint64_t)0ULL);
        t17 = t22;
        goto ix18;
ie18:;
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 270, 42);
        uint64_t t27 = r25;
        uint64_t t28 = t27;
        t17 = t28;
ix18:;
        gm_l6 = t17;
    }
    {
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 8);
        gm_t88rcucg1b7uk t31 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t32 = gm_l1;
        void * t33 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t34 = gm_l6;
        uint32_t t35 = (uint32_t)t34;
        gm_tbl8il92l97ut t36;
        GM_ST(uint32_t, (uint8_t *)&t36 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t36 + 0), t33);
        GM_ST(uint32_t, ((uint8_t *)&t36 + 8), t35);
        gm_teti411eujvf4 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)19ULL));
        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t37 + 8), t36);
        gm_tl874t2atnlh3 t38 = gm_l3;
        void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t31, &t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t40 = 0;
        F.ret = t39;
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
__attribute__((aligned(4))) void * gm_f_soa_make_columns_ardr0ua(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, const void * a3, void * a4, void * a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_soa_make_columns_ardr0ua F;
    void * gm_l0; /* s */
    void * gm_l1; /* locals */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tjpqvfd72o62u gm_l3; /* info */
    void * gm_l4; /* n */
    void * gm_l5; /* site */
    gm_tl874t2atnlh3 gm_l6; /* span */
    void * gm_l7; /* tys */
    gm_t017ud7s0np4m gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* rt */
    void * gm_l11; /* call */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    uint32_t gm_l14; /* p */
    uint32_t gm_l15; /* e */
    gm_b16_8 gm_l16; /* fields */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* k */
    void * gm_l21; /* raw */
    gm_t88rcucg1b7uk gm_l22; /* result */
    void * gm_l23; /* value */
    void * gm_l24; /* wrapped */
    void * gm_l25; /* failed */
    GM_STACK_CHECK(&F, 2889u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l7 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_suh0878eve8t9));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(9));
        gm_tl874t2atnlh3 t6 = gm_l6;
        gm_tn6p6lpnve7b0 t7;
        gm_f_soa_core_sd8eq62((void *)&t7, (void *)NULL, cx, NULL, t4, t5, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a3)), t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t017ud7s0np4m t8;
        if (((((uint8_t *)&t7))[24] != 0)) {
            t8 = (*(gm_t017ud7s0np4m *)(((uint8_t *)&t7) + 0));
        } else {
            void * t9 = gm_l7;
            gm_t88rcucg1b7uk t10 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t11 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t12;
            memset(&t12, 0, sizeof t12);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 8), t11);
            gm_tl874t2atnlh3 t13 = gm_l6;
            void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t10, &t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l8 = t8;
    }
    {
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        uint64_t t17 = (uint64_t)t16;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 344) + 0) + 8);
        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_src4nj79mj9jk57, 374, 11);
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 344) + 0)) + t18 * 136) + 56));
        gm_l9 = t21;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l10);
        void * t23 = GM_LD(void *, (((uint8_t *)&gm_l8) + 8));
        gm_tftb1odc739t5 t24 = gm_f_push_d943vgp_2(t22, &(*(gm_t69m56meqefs8 *)((uint8_t *)t23)));
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src4nj79mj9jk57, 376, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        void * t26 = (void *)((uint8_t *)&F.l10);
        void * t27 = GM_LD(void *, (((uint8_t *)&gm_l8) + 16));
        gm_tftb1odc739t5 t28 = gm_f_push_d943vgp_2(t26, &(*(gm_t69m56meqefs8 *)((uint8_t *)t27)));
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src4nj79mj9jk57, 377, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        void * t30 = (void *)((uint8_t *)&F.l10);
        void * t31 = gm_l4;
        void * t32 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t33 = gm_f_push_d943vgp_2(t30, &(*(gm_t69m56meqefs8 *)((uint8_t *)t32)));
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_src4nj79mj9jk57, 378, 5);
        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
    }
    {
        void * t35 = (void *)((uint8_t *)&F.l10);
        void * t36 = gm_l5;
        gm_tftb1odc739t5 t37 = gm_f_push_d943vgp_2(t35, &(*(gm_t69m56meqefs8 *)((uint8_t *)t36)));
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src4nj79mj9jk57, 379, 5);
        gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
    }
    {
        gm_t88rcucg1b7uk t39 = gm_l9;
        gm_to76jli3bqddq t40;
        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
        gm_tsth9qu1dv0g3 t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t42 + 8), t41);
        GM_ST(gm_tsth9qu1dv0g3, (uint8_t *)&t40 + 0, t42);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t40 + 40, t43);
        gm_teti411eujvf4 t44;
        GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t44 + 8), t40);
        gm_tl874t2atnlh3 t45 = gm_l6;
        void * t46 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t39, &t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t46;
    }
    {
        void * t48 = gm_l7;
        gm_t88rcucg1b7uk t49 = gm_l9;
        gm_t53at80pfdus3 t50;
        gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t51 != ((uint8_t)14ULL)) goto ie47;
        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t50) + 8) + 0));
        gm_l12 = t52;
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t50) + 8) + 4));
        gm_l13 = t53;
        {
            void * t55 = gm_l1;
            gm_t88rcucg1b7uk t56 = gm_l13;
            gm_tl874t2atnlh3 t57 = gm_l6;
            uint32_t t58 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t55, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t58;
        }
        {
            void * t59 = gm_l1;
            gm_t88rcucg1b7uk t60 = gm_l12;
            gm_tl874t2atnlh3 t61 = gm_l6;
            uint32_t t62 = gm_f_soa_temp_2pshn8g((void *)NULL, cx, NULL, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t62;
        }
        {
            void * t63 = gm_l0;
            gm_t88rcucg1b7uk t64 = gm_l2;
            gm_th2lqbo77pvsc t65;
            gm_f_struct_fields_chqu3it((void *)&t65, t63, GM_CONV(gm_csguankko4lmhd, t64));
            gm_b16_8 t66;
            if (((((uint8_t *)&t65))[16] != 0)) {
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 0));
                t66 = t67;
            } else {
                gm_b16_8 t68 = gm_f_empty_slice_ose1ba9();
                t66 = t68;
            }
            gm_l16 = t66;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint64_t r71; bool o72 = __builtin_sub_overflow(t70, ((uint64_t)1ULL), &r71);
                if (o72) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 387, 17);
                uint64_t t73 = r71;
                gm_l18 = t73;
            }
            {
                for (;;) {
                    uint64_t t75 = gm_l19;
                    uint64_t t76 = gm_l18;
                    uint8_t t77 = (t75 < t76);
                    if (!t77) goto lb74;
                    {
                        uint64_t t80 = gm_l19;
                        uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&t80));
                        gm_l20 = t83;
                        {
                            void * t85 = gm_l7;
                            gm_t53at80pfdus3 t86;
                            memset(&t86, 0, sizeof t86);
                            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)7ULL));
                            gm_t88rcucg1b7uk t87 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t85, &t86);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            gm_tlcl4u558l6p5 t88;
                            memset((uint8_t *)&t88 + 17, 0, 7);
                            gm_t88rcucg1b7uk t89 = gm_l13;
                            uint32_t t90 = gm_l14;
                            gm_teti411eujvf4 t91;
                            memset(&t91, 0, sizeof t91);
                            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t91 + 8), t90);
                            gm_tl874t2atnlh3 t92 = gm_l6;
                            void * t93 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t91, t92);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            GM_ST(void *, (uint8_t *)&t88 + 0, t93);
                            void * t94 = gm_l0;
                            uint64_t t95 = gm_l20;
                            uint64_t t96 = (uint64_t)t95;
                            gm_tl874t2atnlh3 t97 = gm_l6;
                            void * t98 = gm_f_usize_lit_a8hojea((void *)NULL, cx, NULL, t94, t96, t97);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            GM_ST(void *, (uint8_t *)&t88 + 8, t98);
                            GM_ST(uint8_t, (uint8_t *)&t88 + 16, ((uint8_t)0));
                            gm_teti411eujvf4 t99;
                            memset(&t99, 0, sizeof t99);
                            GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t99 + 8), t88);
                            gm_tl874t2atnlh3 t100 = gm_l6;
                            void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t87, &t99, t100);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            gm_l21 = t101;
                        }
                        {
                            void * t102 = (void *)((uint8_t *)&F.l17);
                            uint64_t t103 = gm_l20;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_src4nj79mj9jk57, 389, 27);
                            gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t104 * 40) + 16));
                            gm_tt8o5kebou62r t107;
                            memset((uint8_t *)&t107 + 12, 0, 4);
                            gm_tvdg94bk7aa22 t108;
                            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t108 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)8ULL));
                            GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t107 + 0, t108);
                            void * t109 = gm_l21;
                            gm_b16_8 t110 = gm_f_one_arg_f1gj3pp((void *)NULL, cx, NULL, t109);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t110);
                            gm_teti411eujvf4 t111;
                            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t111 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t111 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t111 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)33ULL));
                            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t111 + 8), t107);
                            gm_tl874t2atnlh3 t112 = gm_l6;
                            void * t113 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t106, &t111, t112);
                            if (gm_unw.kind) {
                                goto gm_ll78;
                            }
                            gm_tftb1odc739t5 t114 = gm_f_push_d943vgp_2(t102, &(*(gm_t69m56meqefs8 *)((uint8_t *)t113)));
                            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_src4nj79mj9jk57, 389, 9);
                            gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                        }
                        goto me81;
mn82:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 387, 5);
me81:;
                    }
lc74:;
                    {
                        uint64_t t117 = gm_l19;
                        uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                        uint64_t t120 = r118;
                        gm_l19 = t120;
                    }
                }
gm_ll78:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb74; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc74; }
                }
                goto gm_lad;
lb74:;
            }
        }
        {
            void * t121 = (void *)((uint8_t *)&F.l17);
            void * t122 = gm_l4;
            gm_tftb1odc739t5 t123 = gm_f_push_d943vgp_2(t121, &(*(gm_t69m56meqefs8 *)((uint8_t *)t122)));
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_src4nj79mj9jk57, 391, 5);
            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
        }
        {
            void * t125 = gm_l7;
            gm_t88rcucg1b7uk t126 = gm_l12;
            gm_t88rcucg1b7uk t127 = gm_l2;
            gm_t88rcucg1b7uk t128 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t125, t126, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t128;
        }
        {
            gm_t88rcucg1b7uk t129 = gm_l2;
            gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_teti411eujvf4 t131;
            memset(&t131, 0, sizeof t131);
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t131 + 8), t130);
            gm_tl874t2atnlh3 t132 = gm_l6;
            void * t133 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t129, &t131, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t133;
        }
        {
            gm_t88rcucg1b7uk t134 = gm_l22;
            gm_tmn7n0n9raou5 t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)3ULL));
            void * t136 = gm_l23;
            gm_t8jsc4n9vp2ib t137;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t137 + 0), t135);
            GM_ST(void *, ((uint8_t *)&t137 + 8), t136);
            gm_teti411eujvf4 t138;
            memset(&t138, 0, sizeof t138);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t138 + 8), t137);
            gm_tl874t2atnlh3 t139 = gm_l6;
            void * t140 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t134, &t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t140;
        }
        {
            gm_t88rcucg1b7uk t141 = gm_l22;
            gm_t88rcucg1b7uk t142 = gm_l12;
            uint32_t t143 = gm_l15;
            gm_teti411eujvf4 t144;
            memset(&t144, 0, sizeof t144);
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)15ULL));
            GM_ST(uint32_t, ((uint8_t *)&t144 + 8), t143);
            gm_tl874t2atnlh3 t145 = gm_l6;
            void * t146 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t142, &t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_teti411eujvf4 t147;
            memset(&t147, 0, sizeof t147);
            GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)39ULL));
            GM_ST(void *, ((uint8_t *)&t147 + 8), t146);
            gm_tl874t2atnlh3 t148 = gm_l6;
            void * t149 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t141, &t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t149;
        }
        {
            gm_f_list_58nr42v_3((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t150 = (void *)((uint8_t *)&F.l26);
            gm_tbbqefuhv2r8h t151;
            gm_t88rcucg1b7uk t152 = gm_l9;
            gm_t88rcucg1b7uk t153 = gm_l13;
            uint32_t t154 = gm_l14;
            gm_tb38v5s9j1r32 t155;
            memset(&t155, 0, sizeof t155);
            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t155 + 8), t154);
            gm_tl874t2atnlh3 t156 = gm_l6;
            void * t157 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t153, &t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t158;
            memset(&t158, 0, sizeof t158);
            GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&t158 + 8), t157);
            gm_tl874t2atnlh3 t159 = gm_l6;
            void * t160 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t152, &t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t151 + 0, t160);
            GM_ST(void *, (uint8_t *)&t151 + 8, ((void *)0));
            void * t161 = gm_l24;
            GM_ST(void *, (uint8_t *)&t151 + 16, t161);
            gm_tftb1odc739t5 t162 = gm_f_push_bpovr2k_3(t150, &t151);
            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_src4nj79mj9jk57, 397, 5);
            gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
        }
        {
            void * t164 = (void *)((uint8_t *)&F.l26);
            gm_tbbqefuhv2r8h t165;
            gm_t88rcucg1b7uk t166 = gm_l9;
            gm_t88rcucg1b7uk t167 = gm_l12;
            uint32_t t168 = gm_l15;
            gm_tb38v5s9j1r32 t169;
            memset(&t169, 0, sizeof t169);
            GM_ST(uint8_t, ((uint8_t *)&t169 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t169 + 8), t168);
            gm_tl874t2atnlh3 t170 = gm_l6;
            void * t171 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t167, &t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tb38v5s9j1r32 t172;
            memset(&t172, 0, sizeof t172);
            GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t172 + 8), t171);
            gm_tl874t2atnlh3 t173 = gm_l6;
            void * t174 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t166, &t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t165 + 0, t174);
            GM_ST(void *, (uint8_t *)&t165 + 8, ((void *)0));
            void * t175 = gm_l25;
            GM_ST(void *, (uint8_t *)&t165 + 16, t175);
            gm_tftb1odc739t5 t176 = gm_f_push_bpovr2k_3(t164, &t165);
            if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_src4nj79mj9jk57, 398, 5);
            gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
        }
        {
            gm_t88rcucg1b7uk t178 = gm_l22;
            gm_tkaapcii86udk t179;
            void * t180 = gm_l11;
            GM_ST(void *, (uint8_t *)&t179 + 0, t180);
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t179 + 8, t181);
            gm_teti411eujvf4 t182;
            memset(&t182, 0, sizeof t182);
            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)44ULL));
            GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t182 + 8), t179);
            gm_tl874t2atnlh3 t183 = gm_l6;
            void * t184 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t178, &t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t185 = 0;
            F.ret = t184;
            goto gm_fret;
        }
        goto ix47;
ie47:;
        {
            void * t187 = gm_l7;
            gm_t88rcucg1b7uk t188 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t189 = gm_f_empty_slice_g3tddrb_2();
            gm_teti411eujvf4 t190;
            memset(&t190, 0, sizeof t190);
            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t190 + 8), t189);
            gm_tl874t2atnlh3 t191 = gm_l6;
            void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t188, &t190, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t193 = 0;
            F.ret = t192;
            goto gm_fret;
        }
ix47:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_soa_repeatable_rqn3pcn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_repeatable_rqn3pcn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t12 != ((uint8_t)15ULL)) goto mn11;
    t2 = ((uint8_t)1);
    goto me3;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t14 != ((uint8_t)16ULL)) goto mn13;
    t2 = ((uint8_t)1);
    goto me3;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t16 != ((uint8_t)14ULL)) goto mn15;
    t2 = ((uint8_t)1);
    goto me3;
mn15:;
ma7:;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t18 != ((uint8_t)17ULL)) goto mn17;
    t2 = ((uint8_t)1);
    goto me3;
mn17:;
ma8:;
    t2 = ((uint8_t)0);
    goto me3;
mn19:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 250, 53);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_scatter_h5ruqu8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, void * a3, void * a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_soa_scatter_h5ruqu8 F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    void * gm_l3; /* i */
    void * gm_l4; /* x */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l6; /* fields */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l12; /* col */
    void * gm_l13; /* place */
    void * gm_l14; /* part */
    GM_STACK_CHECK(&F, 744u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_f_soa_fields_dg06fgc(t2, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l6 = t3;
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t5 = gm_l6;
            gm_l8 = t5;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l9;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    gm_t8ta482bl6be0 t12;
                    uint64_t t13 = gm_l9;
                    GM_ST(uint64_t, (uint8_t *)&t12 + 0, t13);
                    uint64_t t14 = gm_l9;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    memcpy((uint8_t *)&t12 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t15 * 40), 40);
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t12) + 0));
                    gm_l10 = t19;
                    F.l11 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t12) + 8));
                    {
                        void * t21 = gm_l0;
                        void * t22 = gm_l1;
                        uint64_t t23 = gm_l10;
                        gm_tl874t2atnlh3 t24 = gm_l5;
                        void * t25 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t21, t22, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l12 = t25;
                    }
                    {
                        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_tlcl4u558l6p5 t27;
                        memset((uint8_t *)&t27 + 17, 0, 7);
                        void * t28 = gm_l12;
                        GM_ST(void *, (uint8_t *)&t27 + 0, t28);
                        void * t29 = gm_l3;
                        void * t30 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t29);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        GM_ST(void *, (uint8_t *)&t27 + 8, t30);
                        GM_ST(uint8_t, (uint8_t *)&t27 + 16, ((uint8_t)1));
                        gm_teti411eujvf4 t31;
                        memset(&t31, 0, sizeof t31);
                        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)20ULL));
                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t31 + 8), t27);
                        gm_tl874t2atnlh3 t32 = gm_l5;
                        void * t33 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t26, &t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l13 = t33;
                    }
                    {
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        void * t35 = gm_l4;
                        void * t36 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        uint64_t t37 = gm_l10;
                        uint32_t t38 = (uint32_t)t37;
                        gm_tbl8il92l97ut t39;
                        GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t39 + 0), t36);
                        GM_ST(uint32_t, ((uint8_t *)&t39 + 8), t38);
                        gm_teti411eujvf4 t40;
                        memset(&t40, 0, sizeof t40);
                        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t40 + 8), t39);
                        gm_tl874t2atnlh3 t41 = gm_l5;
                        void * t42 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t40, t41);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_l14 = t42;
                    }
                    {
                        void * t43 = (void *)((uint8_t *)&F.l7);
                        void * t44 = gm_l13;
                        void * t45 = gm_l14;
                        gm_t3mf58gv9kf3n t46;
                        GM_ST(void *, ((uint8_t *)&t46 + 0), t44);
                        GM_ST(void *, ((uint8_t *)&t46 + 8), t45);
                        gm_trcmmd12cefb1 t47;
                        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t47 + 8), t46);
                        gm_tftb1odc739t5 t48 = gm_f_push_d6c76ud_2(t43, &t47);
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src4nj79mj9jk57, 293, 9);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 289, 5);
me17:;
                }
lc6:;
                {
                    uint64_t t51 = gm_l9;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l9 = t54;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        uint8_t t56 = 0;
        F.ret = t55;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_shape_755ppqp(void *env, gm_node *cx, gm_node *inst, gm_tf5srh4mp4b3i a0, uint8_t a1) {
    struct gm_fr_soa_shape_755ppqp F;
    gm_tf5srh4mp4b3i gm_l0; /* len */
    uint8_t gm_l1; /* is_const */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        if (!((((uint8_t *)&gm_l0))[8] != 0)) goto ie2;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l0));
        gm_l2 = t3;
        {
            uint64_t t5 = gm_l2;
            gm_t4ce0jd3tdro4 t6;
            GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t5);
            gm_tabb710d9mq2e t7;
            gm_f_format_d04m6io((void *)&t7, (void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_src4nj79mj9jk57, 229, 26);
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t11;
        uint8_t t13 = gm_l1;
        if (!t13) goto ie12;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_shsv7bl83q04d));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(8));
        gm_b16_8 t16 = t15;
        t11 = t16;
        goto ix12;
ie12:;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3ndsddjcvskd));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
        gm_b16_8 t19 = t18;
        t11 = t19;
ix12:;
        uint8_t t20 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_soa_shape_of_ohenvjc F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l4; /* tm */
    GM_STACK_CHECK_SHALLOW(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 2416) + 0));
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
                    gm_tj4mbomtvccf0 t16 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            gm_th3eueiipivq6 t23;
                            memset((uint8_t *)&t23 + 17, 0, 7);
                            gm_tf5srh4mp4b3i t24 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l4) + 8));
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t23 + 0, t24);
                            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 24));
                            GM_ST(uint8_t, (uint8_t *)&t23 + 16, t25);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 28, 0);
                            ((uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out))[24] = 1;
                            GM_ST(gm_th3eueiipivq6, (uint8_t *)&(*(gm_tbm0n9ujhrlhf *)gm_out) + 0, t23);
                            goto gm_fdone;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 170, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_tbm0n9ujhrlhf t31;
        memset(&t31, 0, sizeof t31);
        uint8_t t32 = 0;
        F.ret = t31;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tbm0n9ujhrlhf gm_f_soa_shape_of_ohenvjc_c(void * a0, uint32_t a1) { gm_tbm0n9ujhrlhf gm_r; gm_f_soa_shape_of_ohenvjc(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void * gm_f_soa_slice_column_6ogdk5f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint64_t a3, uint8_t a4, void * a5, void * a6, uint8_t a7, gm_t88rcucg1b7uk a8, gm_tl874t2atnlh3 a9) {
    struct gm_fr_soa_slice_column_6ogdk5f F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint64_t gm_l3; /* k */
    uint8_t gm_l4; /* is_const */
    void * gm_l5; /* lo */
    void * gm_l6; /* hi */
    uint8_t gm_l7; /* inclusive */
    gm_t88rcucg1b7uk gm_l8; /* ft */
    gm_tl874t2atnlh3 gm_l9; /* span */
    void * gm_l10; /* col */
    void * gm_l11; /* l */
    void * gm_l12; /* x */
    void * gm_l13; /* h */
    void * gm_l14; /* x */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    gm_l9 = a9;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        uint64_t t4 = gm_l3;
        gm_tl874t2atnlh3 t5 = gm_l9;
        void * t6 = gm_f_soa_column_7a5htvb((void *)NULL, cx, NULL, t2, t3, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t6;
    }
    {
        gm_l11 = ((void *)0);
    }
    {
        void * t8 = gm_l5;
        if (!(GM_LD(void *, ((uint8_t *)&t8)) != 0)) goto ie7;
        void * t9 = GM_LD(void *, ((uint8_t *)&t8));
        gm_l12 = t9;
        {
            void * t11 = gm_l12;
            void * t12 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t12;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_l13 = ((void *)0);
    }
    {
        void * t14 = gm_l6;
        if (!(GM_LD(void *, ((uint8_t *)&t14)) != 0)) goto ie13;
        void * t15 = GM_LD(void *, ((uint8_t *)&t14));
        gm_l14 = t15;
        {
            void * t17 = gm_l14;
            void * t18 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t18;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        uint8_t t21 = gm_l4;
        gm_t88rcucg1b7uk t22 = gm_l8;
        gm_t88rcucg1b7uk t23 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tao63pk3r18v5 t24;
        memset((uint8_t *)&t24 + 25, 0, 7);
        void * t25 = gm_l10;
        GM_ST(void *, (uint8_t *)&t24 + 0, t25);
        void * t26 = gm_l11;
        GM_ST(void *, (uint8_t *)&t24 + 8, t26);
        void * t27 = gm_l13;
        GM_ST(void *, (uint8_t *)&t24 + 16, t27);
        uint8_t t28 = gm_l7;
        GM_ST(uint8_t, (uint8_t *)&t24 + 24, t28);
        gm_teti411eujvf4 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)21ULL));
        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t29 + 8), t24);
        gm_tl874t2atnlh3 t30 = gm_l9;
        void * t31 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t23, &t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t32 = 0;
        F.ret = t31;
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
__attribute__((aligned(4))) void * gm_f_soa_slice_columns_kqq8dfr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, uint8_t a3, void * a4, void * a5, uint8_t a6, gm_tl874t2atnlh3 a7) {
    struct gm_fr_soa_slice_columns_kqq8dfr F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_tjpqvfd72o62u gm_l2; /* info */
    uint8_t gm_l3; /* is_const */
    void * gm_l4; /* lo */
    void * gm_l5; /* hi */
    uint8_t gm_l6; /* inclusive */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* tys */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* fields */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* k */
    void * gm_l15; /* sl */
    void * gm_l16; /* first */
    GM_STACK_CHECK(&F, 830u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l8 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 0));
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        uint8_t t7 = gm_l3;
        gm_tl874t2atnlh3 t8 = gm_l7;
        gm_t88rcucg1b7uk t9 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t4, t5, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_f_soa_fields_dg06fgc(t10, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)));
        gm_l10 = t11;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            gm_l12 = t13;
        }
        {
            for (;;) {
                uint64_t t15 = gm_l13;
                uint64_t t16 = gm_l12;
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l13;
                    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&t20));
                    gm_l14 = t23;
                    {
                        void * t25 = gm_l0;
                        void * t26 = gm_l1;
                        uint64_t t27 = gm_l14;
                        uint8_t t28 = gm_l3;
                        void * t29 = gm_l4;
                        void * t30 = gm_l5;
                        uint8_t t31 = gm_l6;
                        uint64_t t32 = gm_l14;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_src4nj79mj9jk57, 305, 75);
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t33 * 40) + 16));
                        gm_tl874t2atnlh3 t36 = gm_l7;
                        void * t37 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t25, t26, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), t27, t28, t29, t30, t31, t35, t36);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_l15 = t37;
                    }
                    {
                        void * t38 = (void *)((uint8_t *)&F.l11);
                        void * t39 = gm_l8;
                        uint8_t t40 = gm_l3;
                        uint64_t t41 = gm_l14;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src4nj79mj9jk57, 306, 50);
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t42 * 40) + 16));
                        gm_t88rcucg1b7uk t45 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t39, t40, t44);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        void * t46 = gm_l15;
                        gm_teti411eujvf4 t47;
                        memset(&t47, 0, sizeof t47);
                        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)23ULL));
                        GM_ST(void *, ((uint8_t *)&t47 + 8), t46);
                        gm_tl874t2atnlh3 t48 = gm_l7;
                        void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t45, &t47, t48);
                        if (gm_unw.kind) {
                            goto gm_ll18;
                        }
                        gm_tftb1odc739t5 t50 = gm_f_push_d943vgp_2(t38, &(*(gm_t69m56meqefs8 *)((uint8_t *)t49)));
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src4nj79mj9jk57, 306, 9);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 304, 5);
me21:;
                }
lc14:;
                {
                    uint64_t t53 = gm_l13;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l13 = t56;
                }
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb14; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc14; }
            }
            goto gm_lad;
lb14:;
        }
    }
    {
        void * t57 = gm_l0;
        void * t58 = gm_l1;
        uint8_t t59 = gm_l3;
        void * t60 = gm_l4;
        void * t61 = gm_l5;
        uint8_t t62 = gm_l6;
        uint64_t t63 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
        if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_src4nj79mj9jk57, 308, 74);
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t63 * 40) + 16));
        gm_tl874t2atnlh3 t66 = gm_l7;
        void * t67 = gm_f_soa_slice_column_6ogdk5f((void *)NULL, cx, NULL, t57, t58, &(*(gm_tjpqvfd72o62u *)((uint8_t *)a2)), ((uint64_t)0ULL), t59, t60, t61, t62, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t67;
    }
    {
        void * t68 = (void *)((uint8_t *)&F.l11);
        void * t69 = gm_l8;
        gm_t88rcucg1b7uk t70 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = gm_l16;
        gm_teti411eujvf4 t72;
        memset(&t72, 0, sizeof t72);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t72 + 8), t71);
        gm_tl874t2atnlh3 t73 = gm_l7;
        void * t74 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t70, &t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t75 = gm_f_push_d943vgp_2(t68, &(*(gm_t69m56meqefs8 *)((uint8_t *)t74)));
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_src4nj79mj9jk57, 309, 5);
        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
    }
    {
        gm_t88rcucg1b7uk t77 = gm_l9;
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_teti411eujvf4 t79;
        memset(&t79, 0, sizeof t79);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t79 + 8), t78);
        gm_tl874t2atnlh3 t80 = gm_l7;
        void * t81 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t77, &t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_stand_in_kdtmkn7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_stand_in_kdtmkn7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_tf5srh4mp4b3i t3 = gm_l2;
        uint8_t t4 = gm_l3;
        uint32_t t5 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t5;
    }
    {
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        uint32_t t8 = gm_l4;
        {
            gm_t88rcucg1b7uk t10 = gm_l1;
            gm_toa2vhe3l4hfr t11;
            memset(&t11, 0, sizeof t11);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 8), t10);
            GM_ST(gm_toa2vhe3l4hfr, (uint8_t *)&(*(gm_t2ubvi1j1aq22 *)((uint8_t *)&F.l5)) + 0, t11);
        }
        void * t12 = ((uint8_t *)&F.l5);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 154, 26);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t12 + (uintptr_t)0 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t14 = t13;
        gm_t88rcucg1b7uk t15 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t7, t8, t14);
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
inline __attribute__((aligned(4))) uint32_t gm_f_soa_temp_2pshn8g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_soa_temp_2pshn8g F;
    void * gm_l0; /* locals */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_tsoie1ppgdvcg t3;
        memset((uint8_t *)&t3 + 21, 0, 3);
        memset((uint8_t *)&t3 + 36, 0, 4);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shv1k5m05egbe));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t3 + 0, t4);
        gm_t88rcucg1b7uk t5 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t3 + 16, t5);
        GM_ST(uint8_t, (uint8_t *)&t3 + 20, ((uint8_t)1));
        gm_tl874t2atnlh3 t6 = gm_l2;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t3 + 24, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_am7bo0v(t2, &t3);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_src4nj79mj9jk57, 245, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_src4nj79mj9jk57, 246, 13);
        uint64_t t13 = r11;
        uint32_t t14 = (uint32_t)t13;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_soa_template_fa784hs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tf5srh4mp4b3i a1, uint8_t a2) {
    struct gm_fr_soa_template_fa784hs F;
    void * gm_l0; /* s */
    gm_tf5srh4mp4b3i gm_l1; /* len */
    uint8_t gm_l2; /* is_const */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tj4mbomtvccf0 gm_l5; /* tm */
    gm_b16_8 gm_l6; /* name */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 770u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 2416) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tj4mbomtvccf0 t16 = GM_LD(gm_tj4mbomtvccf0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        gm_tf5srh4mp4b3i t19 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l5) + 8));
                        gm_tf5srh4mp4b3i t20 = gm_l1;
                        uint8_t t21 = gm_f_same_len_bnkvi6r(t19, t20);
                        if (!t21) goto ie18;
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 24));
                        uint8_t t23 = gm_l2;
                        uint8_t t24 = ((t22 == t23));
                        if (!t24) goto ie18;
                        {
                            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 158, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t33 = gm_l1;
        uint8_t t34 = gm_l2;
        gm_b16_8 t35 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tabb710d9mq2e t37;
        gm_f_format_thmq1t9((void *)&t37, (void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_src4nj79mj9jk57, 161, 13);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_l6 = t38;
    }
    {
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_b16_8 t41 = gm_l6;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        uint32_t t43 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t43;
    }
    {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 1088);
        gm_b16_8 t46 = gm_l6;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        gm_ti0kgh3pc3in2 t48;
        gm_f_empty_struct_b5i94nj((void *)&t48);
        gm_t6art7bprjiqb t49;
        gm_f_compiler_adt_eiq6rf1((void *)&t49, (void *)NULL, cx, NULL, t47, ((uint64_t)0ULL), &t48, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t50 = gm_f_push_j84ih6c(t45, &t49);
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src4nj79mj9jk57, 163, 5);
        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
    }
    {
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 2416);
        gm_tj4mbomtvccf0 t54;
        memset((uint8_t *)&t54 + 4, 0, 4);
        memset((uint8_t *)&t54 + 25, 0, 7);
        uint32_t t55 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t54 + 0, t55);
        gm_tf5srh4mp4b3i t56 = gm_l1;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t54 + 8, t56);
        uint8_t t57 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&t54 + 24, t57);
        gm_tftb1odc739t5 t58 = gm_f_push_3dc3qci(t53, &t54);
        if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_src4nj79mj9jk57, 164, 5);
        gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
    }
    {
        uint32_t t60 = gm_l7;
        uint8_t t61 = 0;
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
__attribute__((aligned(4))) void * gm_f_soa_to_const_hhiu49k(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_to_const_hhiu49k F;
    void * gm_l0; /* s */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* src */
    gm_t88rcucg1b7uk gm_l3; /* target */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* target_fields */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    void * gm_l11; /* part */
    GM_STACK_CHECK(&F, 847u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_th2lqbo77pvsc t4;
        gm_f_struct_fields_chqu3it((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_f_empty_slice_ose1ba9();
            t5 = t7;
        }
        gm_l5 = t5;
    }
    {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l3;
        gm_th2lqbo77pvsc t10;
        gm_f_struct_fields_chqu3it((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_b16_8 t11;
        if (((((uint8_t *)&t10))[16] != 0)) {
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            gm_b16_8 t13 = gm_f_empty_slice_ose1ba9();
            t11 = t13;
        }
        gm_l6 = t11;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint64_t t17 = (t15 < t16 ? t15 : t16);
            gm_l8 = t17;
        }
        {
            for (;;) {
                uint64_t t19 = gm_l9;
                uint64_t t20 = gm_l8;
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l9;
                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t24));
                    gm_l10 = t27;
                    {
                        uint64_t t29 = gm_l10;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_src4nj79mj9jk57, 328, 24);
                        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t30 * 40) + 16));
                        void * t33 = gm_l1;
                        void * t34 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t33);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        uint64_t t35 = gm_l10;
                        uint32_t t36 = (uint32_t)t35;
                        gm_tbl8il92l97ut t37;
                        GM_ST(uint32_t, (uint8_t *)&t37 + 12, 0);
                        GM_ST(void *, ((uint8_t *)&t37 + 0), t34);
                        GM_ST(uint32_t, ((uint8_t *)&t37 + 8), t36);
                        gm_teti411eujvf4 t38;
                        memset(&t38, 0, sizeof t38);
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t38 + 8), t37);
                        gm_tl874t2atnlh3 t39 = gm_l4;
                        void * t40 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t32, &t38, t39);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        gm_l11 = t40;
                    }
                    {
                        uint64_t t42 = gm_l10;
                        uint64_t t43 = (uintptr_t)t42;
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src4nj79mj9jk57, 329, 12);
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t43 * 40) + 16));
                        uint64_t t46 = gm_l10;
                        uint64_t t47 = (uintptr_t)t46;
                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_src4nj79mj9jk57, 329, 28);
                        gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t47 * 40) + 16));
                        uint8_t t50 = (gm_eq9m6mvtmgde(((uint8_t *)&t45), ((uint8_t *)&t49)));
                        if (!t50) goto ie41;
                        {
                            void * t52 = (void *)((uint8_t *)&F.l7);
                            void * t53 = gm_l11;
                            gm_tftb1odc739t5 t54 = gm_f_push_d943vgp_2(t52, &(*(gm_t69m56meqefs8 *)((uint8_t *)t53)));
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src4nj79mj9jk57, 330, 13);
                            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        }
                        goto ix41;
ie41:;
                        {
                            void * t57 = (void *)((uint8_t *)&F.l7);
                            uint64_t t58 = gm_l10;
                            uint64_t t59 = (uintptr_t)t58;
                            uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_src4nj79mj9jk57, 332, 31);
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t59 * 40) + 16));
                            gm_tmn7n0n9raou5 t62;
                            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)6ULL));
                            void * t63 = gm_l11;
                            gm_t8jsc4n9vp2ib t64;
                            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t64 + 0), t62);
                            GM_ST(void *, ((uint8_t *)&t64 + 8), t63);
                            gm_teti411eujvf4 t65;
                            memset(&t65, 0, sizeof t65);
                            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)32ULL));
                            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t65 + 8), t64);
                            gm_tl874t2atnlh3 t66 = gm_l4;
                            void * t67 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t61, &t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tftb1odc739t5 t68 = gm_f_push_d943vgp_2(t57, &(*(gm_t69m56meqefs8 *)((uint8_t *)t67)));
                            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_src4nj79mj9jk57, 332, 13);
                            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                        }
ix41:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 327, 5);
me25:;
                }
lc18:;
                {
                    uint64_t t71 = gm_l9;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l9 = t74;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
        }
    }
    {
        gm_t88rcucg1b7uk t75 = gm_l3;
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        gm_teti411eujvf4 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t77 + 8), t76);
        gm_tl874t2atnlh3 t78 = gm_l4;
        void * t79 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t75, &t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_42lvsj0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_soa_type_42lvsj0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* msg */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tf5srh4mp4b3i t4 = gm_l2;
        uint8_t t5 = gm_l3;
        gm_te3magq8drarp t6;
        gm_f_soa_type_of_q8vtd5j((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l5 = t10;
        gm_t88rcucg1b7uk t11 = gm_l5;
        uint8_t t12 = 0;
        F.ret = t11;
        goto gm_fret;
        goto me7;
mn8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)1ULL)) goto mn13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l6 = t15;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l4;
            gm_b16_8 t19 = gm_l6;
            gm_b0_1 t20 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t21 = gm_l0;
            void * t22 = (void *)(((uint8_t *)t21) + 8);
            gm_t88rcucg1b7uk t23 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto me7;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 84, 5);
me7:;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_soa_type_expr_go3p0n7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t3mf58gv9kf3n gm_l4; /* a */
    void * gm_l5; /* len */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* is_const */
    void * gm_l10; /* inner_ty */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    GM_STACK_CHECK(&F, 511u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma5; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma4; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)28ULL)) goto mn9;
        gm_t3mf58gv9kf3n t11 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t11;
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie12;
        void * t14 = GM_LD(void *, ((uint8_t *)&t13));
        gm_l5 = t14;
        {
            void * t16 = gm_l0;
            uint64_t t17 = gm_l1;
            void * t18 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
            gm_t88rcucg1b7uk t19 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t19;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            void * t20 = gm_l0;
            uint64_t t21 = gm_l1;
            void * t22 = gm_l5;
            gm_t5e6kkuq21nig t23 = gm_f_array_len_eielif2((void *)NULL, cx, NULL, t20, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            switch (t29) { case ((uint8_t)0ULL): goto ma25; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma27; default: goto ma28; }
ma25:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t31 != ((uint8_t)0ULL)) goto mn30;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t23) + 8));
            gm_l8 = t32;
            {
                uint64_t t34 = gm_l8;
                gm_l7 = t34;
            }
            goto me24;
mn30:;
ma26:;
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t36 != ((uint8_t)1ULL)) goto mn35;
            {
                void * t38 = gm_l0;
                void * t39 = gm_l5;
                gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                gm_b16_8 t41;
                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s58rgrgmho9sa));
                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(67));
                gm_b0_1 t42 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t38, t40, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t43 = gm_l0;
                void * t44 = (void *)(((uint8_t *)t43) + 8);
                gm_t88rcucg1b7uk t45 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto me24;
mn35:;
ma27:;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            if (t48 != ((uint8_t)2ULL)) goto mn47;
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 8);
            gm_t88rcucg1b7uk t51 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
            goto me24;
mn47:;
ma28:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 207, 13);
me24:;
        }
        {
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l6;
            uint64_t t55 = gm_l7;
            gm_tf5srh4mp4b3i t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t56), t55);
            ((uint8_t *)&t56)[8] = 1;
            gm_tl874t2atnlh3 t57 = gm_l3;
            gm_t88rcucg1b7uk t58 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t53, t54, t56, ((uint8_t)0), t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t61 != ((uint8_t)26ULL)) goto mn60;
        uint8_t t62 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t62;
        void * t63 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l10 = t63;
        {
            void * t65 = gm_l0;
            uint64_t t66 = gm_l1;
            void * t67 = gm_l10;
            gm_t88rcucg1b7uk t68 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t65, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t68;
        }
        {
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l11;
            gm_tf5srh4mp4b3i t71;
            memset(&t71, 0, sizeof t71);
            uint8_t t72 = gm_l9;
            gm_tl874t2atnlh3 t73 = gm_l3;
            gm_t88rcucg1b7uk t74 = gm_f_soa_type_42lvsj0((void *)NULL, cx, NULL, t69, t70, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto me3;
mn60:;
ma6:;
        goto me3;
mn76:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 203, 5);
me3:;
    }
    {
        void * t78 = gm_l0;
        gm_tl874t2atnlh3 t79 = gm_l3;
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_ssnb4or831s5l));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(77));
        gm_b0_1 t81 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t78, t79, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t82 = gm_l0;
        void * t83 = (void *)(((uint8_t *)t82) + 8);
        gm_t88rcucg1b7uk t84 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t85 = 0;
        F.ret = t84;
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
__attribute__((aligned(4))) void gm_f_soa_type_of_q8vtd5j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tf5srh4mp4b3i a2, uint8_t a3) {
    struct gm_fr_soa_type_of_q8vtd5j F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_tf5srh4mp4b3i gm_l2; /* len */
    uint8_t gm_l3; /* is_const0 */
    void * gm_l4; /* tys */
    uint8_t gm_l5; /* is_const */
    gm_b16_8 gm_l6; /* not_struct */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l9; /* fields */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tcu60n24kv26r gm_l16; /* m */
    gm_b16_8 gm_l17; /* name */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l22; /* n */
    gm_t88rcucg1b7uk gm_l23; /* ct */
    uint32_t gm_l24; /* template */
    uint32_t gm_l25; /* soa */
    gm_t88rcucg1b7uk gm_l27; /* t */
    GM_STACK_CHECK(&F, 2739u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l4;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_error_8k27gd1(t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) goto ie4;
        {
            gm_t88rcucg1b7uk t9 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t10 = 0;
        uint8_t t11 = gm_l3;
        if (t11) {
            uint8_t t12;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn14;
            t12 = ((uint8_t)1);
            goto me13;
mn14:;
            t12 = ((uint8_t)0);
            goto me13;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 98, 31);
me13:;
            t10 = t12;
        } else t10 = 0;
        gm_l5 = t10;
    }
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l1;
        uint8_t t19 = gm_f_has_param_u9t0efs(t17, GM_CONV(gm_csguankko4lmhd, t18));
        if (!t19) goto ie16;
        void * t20 = gm_l4;
        gm_t88rcucg1b7uk t21 = gm_l1;
        gm_t53at80pfdus3 t22;
        gm_f_kind_jcnt4hm((void *)&t22, t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t23;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma27; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma27; case ((uint8_t)5ULL): goto ma27; case ((uint8_t)6ULL): goto ma27; case ((uint8_t)7ULL): goto ma27; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma27; case ((uint8_t)11ULL): goto ma27; case ((uint8_t)12ULL): goto ma27; case ((uint8_t)13ULL): goto ma27; case ((uint8_t)14ULL): goto ma27; case ((uint8_t)15ULL): goto ma27; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma27; case ((uint8_t)19ULL): goto ma25; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma27; case ((uint8_t)22ULL): goto ma27; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma27; case ((uint8_t)25ULL): goto ma27; case ((uint8_t)26ULL): goto ma27; default: goto ma27; }
ma25:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t31 != ((uint8_t)19ULL)) goto mn30;
        t23 = ((uint8_t)1);
        goto me24;
mn30:;
ma26:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t22) + 0));
        if (t33 != ((uint8_t)17ULL)) goto mn32;
        t23 = ((uint8_t)1);
        goto me24;
mn32:;
ma27:;
        t23 = ((uint8_t)0);
        goto me24;
mn34:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 99, 32);
me24:;
        if (!t23) goto ie16;
        {
            void * t36 = gm_l0;
            gm_t88rcucg1b7uk t37 = gm_l1;
            gm_tf5srh4mp4b3i t38 = gm_l2;
            uint8_t t39 = gm_l5;
            gm_t88rcucg1b7uk t40 = gm_f_soa_stand_in_kdtmkn7((void *)NULL, cx, NULL, t36, t37, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t40);
            goto gm_fdone;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t41 = gm_l0;
        gm_t88rcucg1b7uk t42 = gm_l1;
        gm_b16_8 t43 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t44;
        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
        gm_tabb710d9mq2e t45;
        gm_f_format_16dceus((void *)&t45, (void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_src4nj79mj9jk57, 100, 19);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_l6 = t46;
    }
    {
        void * t48 = gm_l4;
        gm_t88rcucg1b7uk t49 = gm_l1;
        gm_t53at80pfdus3 t50;
        gm_f_kind_jcnt4hm((void *)&t50, t48, GM_CONV(gm_csguankko4lmhd, t49));
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t50) + 0));
        if (t51 != ((uint8_t)17ULL)) goto ie47;
        uint32_t t52 = GM_LD(uint32_t, ((((uint8_t *)&t50) + 8) + 0));
        gm_l7 = t52;
        {
            uint32_t t54 = gm_l7;
            uint64_t t55 = (uint64_t)t54;
            uint64_t t56 = (uintptr_t)t55;
            void * t57 = gm_l0;
            uint64_t t58 = GM_LD(uintptr_t, ((((uint8_t *)t57) + 1088) + 0) + 8);
            if (t56 >= (uintptr_t)(t58)) gm_trap_index(t56, t58, gm_src4nj79mj9jk57, 102, 13);
            gm_t6art7bprjiqb t59 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t57) + 1088) + 0)) + t56 * 184));
            F.l8 = t59;
        }
        {
            gm_b16_8 t60 = gm_f_empty_slice_ose1ba9();
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_l9 = t61;
        }
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
            if (t64 != ((uint8_t)0ULL)) goto mn63;
            gm_tpautr4br7h3l t65 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)&F.l8) + 24) + 8));
            F.l10 = t65;
            {
                uint8_t t68;
                if ((((((uint8_t *)&F.l10) + 40))[4] != 0)) goto mn70;
                t68 = ((uint8_t)0);
                goto me69;
mn70:;
                t68 = ((uint8_t)1);
                goto me69;
mn71:;
                gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 106, 16);
me69:;
                if (!t68) goto ie67;
                {
                    void * t73 = gm_l0;
                    gm_t88rcucg1b7uk t74 = gm_l1;
                    gm_b16_8 t75 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t76;
                    GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                    gm_tabb710d9mq2e t77;
                    gm_f_format_fambdq4((void *)&t77, (void *)NULL, cx, NULL, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_src4nj79mj9jk57, 106, 51);
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t79);
                    goto gm_fdone;
                }
                goto ix67;
ie67:;
ix67:;
            }
            {
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                gm_l9 = t81;
            }
            goto me62;
mn63:;
            gm_b16_8 t83 = gm_l6;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t84);
            goto gm_fdone;
            goto me62;
mn82:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 104, 5);
me62:;
        }
        {
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            uint8_t t87 = ((t86 == ((uint64_t)0ULL)));
            if (!t87) goto ie85;
            {
                void * t89 = gm_l0;
                gm_t88rcucg1b7uk t90 = gm_l1;
                gm_b16_8 t91 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t92;
                GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
                gm_tabb710d9mq2e t93;
                gm_f_format_4nn4ect((void *)&t93, (void *)NULL, cx, NULL, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 8, gm_src4nj79mj9jk57, 111, 38);
                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)1ULL));
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t95);
                goto gm_fdone;
            }
            goto ix85;
ie85:;
ix85:;
        }
        {
            {
                gm_b16_8 t97 = gm_l9;
                gm_l11 = t97;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t99 = gm_l12;
                    uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t101 = (t99 < t100);
                    if (!t101) goto lb98;
                    {
                        uint64_t t104 = gm_l12;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_t0mglksrkfa5e t109 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t105 * 40));
                        F.l13 = t109;
                        {
                            uint8_t t112 = (((GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)&F.l13) + 0)), gm_sm2k5tp7dtg07, 3) == 0));
                            if (!t112) goto ie111;
                            {
                                void * t114 = gm_l0;
                                gm_t88rcucg1b7uk t115 = gm_l1;
                                gm_b16_8 t116 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t114, t115);
                                if (gm_unw.kind) {
                                    goto gm_ll102;
                                }
                                gm_tnpifv20irnph t117;
                                GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t116);
                                gm_tabb710d9mq2e t118;
                                gm_f_format_3dfoht6((void *)&t118, (void *)NULL, cx, NULL, t117);
                                if (gm_unw.kind) {
                                    goto gm_ll102;
                                }
                                if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_src4nj79mj9jk57, 113, 42);
                                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)1ULL));
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t120);
                                goto gm_fdone;
                            }
                            goto ix111;
ie111:;
ix111:;
                        }
                        goto me107;
mn108:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 112, 5);
me107:;
                    }
lc98:;
                    {
                        uint64_t t122 = gm_l12;
                        uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                        uint64_t t125 = r123;
                        gm_l12 = t125;
                    }
                }
gm_ll102:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb98; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc98; }
                }
                goto gm_lad;
lb98:;
            }
        }
        {
            {
                void * t127 = gm_l0;
                gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)t127) + 2376) + 0));
                gm_l14 = t128;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t130 = gm_l15;
                    uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t132 = (t130 < t131);
                    if (!t132) goto lb129;
                    {
                        uint64_t t135 = gm_l15;
                        uint64_t t136 = (uintptr_t)t135;
                        uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_tcu60n24kv26r t140 = GM_LD(gm_tcu60n24kv26r, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t136 * 32));
                        gm_l16 = t140;
                        {
                            gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 0));
                            gm_t88rcucg1b7uk t144 = gm_l1;
                            uint8_t t145 = (gm_eq9m6mvtmgde(((uint8_t *)&t143), ((uint8_t *)&t144)));
                            if (!t145) goto ie142;
                            gm_tf5srh4mp4b3i t146 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&gm_l16) + 8));
                            gm_tf5srh4mp4b3i t147 = gm_l2;
                            uint8_t t148 = gm_f_same_len_bnkvi6r(t146, t147);
                            if (!t148) goto ie142;
                            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 24));
                            uint8_t t150 = gm_l5;
                            uint8_t t151 = ((t149 == t150));
                            if (!t151) goto ie142;
                            {
                                gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 28));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t153);
                                goto gm_fdone;
                            }
                            goto ix142;
ie142:;
ix142:;
                        }
                        goto me138;
mn139:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 115, 5);
me138:;
                    }
lc129:;
                    {
                        uint64_t t155 = gm_l15;
                        uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                        uint64_t t158 = r156;
                        gm_l15 = t158;
                    }
                }
lb129:;
            }
        }
        {
            gm_t6j6smj7qoctu t159;
            gm_tf5srh4mp4b3i t160 = gm_l2;
            uint8_t t161 = gm_l5;
            gm_b16_8 t162 = gm_f_soa_shape_755ppqp((void *)NULL, cx, NULL, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t159 + 0, t162);
            void * t163 = gm_l0;
            gm_t88rcucg1b7uk t164 = gm_l1;
            gm_b16_8 t165 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t163, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t159 + 16, t165);
            gm_tabb710d9mq2e t166;
            gm_f_format_j0i128e((void *)&t166, (void *)NULL, cx, NULL, &t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t166))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t166) + 8, gm_src4nj79mj9jk57, 118, 13);
            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&t166) + 8));
            gm_l17 = t167;
        }
        {
            gm_f_list_spqn36o((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t169 = gm_l9;
                gm_l19 = t169;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t171 = gm_l20;
                    uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t173 = (t171 < t172);
                    if (!t173) goto lb170;
                    {
                        uint64_t t176 = gm_l20;
                        uint64_t t177 = (uintptr_t)t176;
                        uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        gm_t0mglksrkfa5e t181 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t177 * 40));
                        F.l21 = t181;
                        {
                            gm_t88rcucg1b7uk t183;
                            if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie184;
                            uint64_t t185 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                            gm_l22 = t185;
                            void * t187 = gm_l4;
                            uint64_t t188 = gm_l22;
                            gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                            gm_t88rcucg1b7uk t190 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t187, t188, t189);
                            if (gm_unw.kind) {
                                goto gm_ll174;
                            }
                            gm_t88rcucg1b7uk t191 = t190;
                            t183 = t191;
                            goto ix184;
ie184:;
                            void * t193 = gm_l4;
                            uint8_t t194 = gm_l5;
                            gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                            gm_t88rcucg1b7uk t196 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t193, t194, t195);
                            if (gm_unw.kind) {
                                goto gm_ll174;
                            }
                            gm_t88rcucg1b7uk t197 = t196;
                            t183 = t197;
ix184:;
                            gm_l23 = t183;
                        }
                        {
                            void * t198 = (void *)((uint8_t *)&F.l18);
                            gm_t0mglksrkfa5e t199;
                            memset((uint8_t *)&t199 + 21, 0, 3);
                            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t199 + 0, t200);
                            gm_t88rcucg1b7uk t201 = gm_l23;
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t199 + 16, t201);
                            gm_t9qkh0rsooc8d t202;
                            GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t199 + 20, t202);
                            gm_tf5srh4mp4b3i t203;
                            memset(&t203, 0, sizeof t203);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t199 + 24, t203);
                            gm_tftb1odc739t5 t204 = gm_f_push_vrmo7c5(t198, &t199);
                            if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_src4nj79mj9jk57, 122, 9);
                            gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
                        }
                        goto me179;
mn180:;
                        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 120, 5);
me179:;
                    }
lc170:;
                    {
                        uint64_t t207 = gm_l20;
                        uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                        uint64_t t210 = r208;
                        gm_l20 = t210;
                    }
                }
gm_ll174:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb170; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc170; }
                }
                goto gm_lad;
lb170:;
            }
        }
        {
            uint8_t t212;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn214;
            t212 = ((uint8_t)1);
            goto me213;
mn214:;
            t212 = ((uint8_t)0);
            goto me213;
mn215:;
            gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 124, 8);
me213:;
            if (!t212) goto ie211;
            {
                void * t217 = (void *)((uint8_t *)&F.l18);
                gm_t0mglksrkfa5e t218;
                memset((uint8_t *)&t218 + 21, 0, 3);
                gm_b16_8 t219;
                GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sm2k5tp7dtg07));
                GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&t218 + 0, t219);
                void * t220 = gm_l4;
                gm_t88rcucg1b7uk t221 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t218 + 16, t221);
                gm_t9qkh0rsooc8d t222;
                GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t218 + 20, t222);
                gm_tf5srh4mp4b3i t223;
                memset(&t223, 0, sizeof t223);
                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t218 + 24, t223);
                gm_tftb1odc739t5 t224 = gm_f_push_vrmo7c5(t217, &t218);
                if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 1, gm_src4nj79mj9jk57, 124, 22);
                gm_b0_1 t225 = GM_LD(gm_b0_1, (((uint8_t *)&t224) + 1));
            }
            goto ix211;
ie211:;
ix211:;
        }
        {
            void * t226 = gm_l0;
            gm_tf5srh4mp4b3i t227 = gm_l2;
            uint8_t t228 = gm_l5;
            uint32_t t229 = gm_f_soa_template_fa784hs((void *)NULL, cx, NULL, t226, t227, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t229;
        }
        {
            void * t230 = gm_l4;
            gm_b16_8 t231 = gm_l17;
            gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&t231));
            uint32_t t233 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t230, t232);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t233;
        }
        {
            void * t234 = gm_l4;
            uint32_t t235 = gm_l25;
            uint32_t t236 = gm_l24;
            gm_f_set_template_o09q96o(t234, t235, t236);
        }
        {
            void * t237 = gm_l0;
            void * t238 = (void *)(((uint8_t *)t237) + 1088);
            gm_b16_8 t239 = gm_l17;
            gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
            uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
            gm_tpautr4br7h3l t242;
            memset((uint8_t *)&t242 + 17, 0, 7);
            gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t242 + 0, t243);
            GM_ST(uint8_t, (uint8_t *)&t242 + 16, ((uint8_t)0));
            gm_tf5srh4mp4b3i t244;
            memset(&t244, 0, sizeof t244);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t242 + 24, t244);
            gm_tkn42u7na3t81 t245;
            memset(&t245, 0, sizeof t245);
            GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t242 + 40, t245);
            gm_ti0kgh3pc3in2 t246;
            GM_ST(uint8_t, (uint8_t *)&t246 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t246 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t246 + 8), t242);
            uint64_t t247 = GM_LD(uintptr_t, (((uint8_t *)&F.l18) + 0) + 8);
            gm_t6art7bprjiqb t248;
            gm_f_compiler_adt_eiq6rf1((void *)&t248, (void *)NULL, cx, NULL, t240, t241, &t246, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t249 = gm_f_push_j84ih6c(t238, &t248);
            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 1, gm_src4nj79mj9jk57, 129, 5);
            gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t249) + 1));
        }
        {
            void * t251 = gm_l0;
            void * t252 = (void *)(((uint8_t *)t251) + 2320);
            uint32_t t253 = gm_l25;
            gm_tjpqvfd72o62u t254;
            memset((uint8_t *)&t254 + 4, 0, 4);
            memset((uint8_t *)&t254 + 25, 0, 7);
            gm_t88rcucg1b7uk t255 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t254 + 0, t255);
            gm_tf5srh4mp4b3i t256 = gm_l2;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t254 + 8, t256);
            uint8_t t257 = gm_l5;
            GM_ST(uint8_t, (uint8_t *)&t254 + 24, t257);
            gm_tftb1odc739t5 t258 = gm_f_put_ftmshk7(t252, t253, &t254);
            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_src4nj79mj9jk57, 130, 5);
            gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
        }
        {
        }
        {
            void * t260 = gm_l4;
            uint32_t t261 = gm_l25;
            {
                gm_t88rcucg1b7uk t263 = gm_l1;
                gm_toa2vhe3l4hfr t264;
                memset(&t264, 0, sizeof t264);
                GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t264 + 8), t263);
                GM_ST(gm_toa2vhe3l4hfr, (uint8_t *)&(*(gm_t2ubvi1j1aq22 *)((uint8_t *)&F.l26)) + 0, t264);
            }
            void * t265 = ((uint8_t *)&F.l26);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src4nj79mj9jk57, 131, 23);
            gm_b16_8 t266;
            GM_ST(void *, (uint8_t *)&t266, (void *)(((uint8_t *)t265 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t267 = t266;
            gm_t88rcucg1b7uk t268 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t260, t261, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t268;
        }
        {
            void * t269 = gm_l0;
            void * t270 = (void *)(((uint8_t *)t269) + 2376);
            gm_tcu60n24kv26r t271;
            memset((uint8_t *)&t271 + 4, 0, 4);
            memset((uint8_t *)&t271 + 25, 0, 3);
            gm_t88rcucg1b7uk t272 = gm_l1;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t271 + 0, t272);
            gm_tf5srh4mp4b3i t273 = gm_l2;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t271 + 8, t273);
            uint8_t t274 = gm_l5;
            GM_ST(uint8_t, (uint8_t *)&t271 + 24, t274);
            gm_t88rcucg1b7uk t275 = gm_l27;
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t271 + 28, t275);
            gm_tftb1odc739t5 t276 = gm_f_push_mf0vgpj(t270, &t271);
            if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 1, gm_src4nj79mj9jk57, 132, 5);
            gm_b0_1 t277 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 1));
        }
        {
            gm_t88rcucg1b7uk t278 = gm_l27;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t278);
            goto gm_fdone;
        }
        goto ix47;
ie47:;
        {
            gm_b16_8 t280 = gm_l6;
            gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&t280));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_te3magq8drarp *)gm_out) + 8, t281);
            goto gm_fdone;
        }
ix47:;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_subst_soa_2chqte3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* elem */
    gm_th3eueiipivq6 gm_l2; /* shape */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* msg */
    GM_STACK_CHECK(&F, 199u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tf5srh4mp4b3i t4 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)a2) + 0));
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        gm_te3magq8drarp t6;
        gm_f_soa_type_of_q8vtd5j((void *)&t6, (void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l3 = t10;
        gm_t88rcucg1b7uk t11 = gm_l3;
        uint8_t t12 = 0;
        F.ret = t11;
        goto gm_fret;
        goto me7;
mn8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t14 != ((uint8_t)1ULL)) goto mn13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l4 = t15;
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 2456);
            gm_b16_8 t19 = gm_l4;
            gm_tftb1odc739t5 t20 = gm_f_push_3ef746b(t18, t19);
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src4nj79mj9jk57, 182, 13);
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
        }
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            gm_t88rcucg1b7uk t24 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto me7;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_src4nj79mj9jk57, 179, 5);
me7:;
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
extern __attribute__((aligned(4))) void * gm_f_usize_lit_a8hojea(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
