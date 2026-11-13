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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_t4ce0jd3tdro4 f0; uint8_t f1; uint8_t pe[7]; } gm_t6ie8iajlnpt1;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; uint16_t v1; } f1; } gm_tvcqs9u9iqpct;
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6ie8iajlnpt1) == 16 && _Alignof(gm_t6ie8iajlnpt1) == 8 && offsetof(gm_t6ie8iajlnpt1, f0) == 0 && offsetof(gm_t6ie8iajlnpt1, f1) == 8, "layout of `?Duration`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_tvcqs9u9iqpct) == 12 && _Alignof(gm_tvcqs9u9iqpct) == 4 && offsetof(gm_tvcqs9u9iqpct, f0) == 0 && offsetof(gm_tvcqs9u9iqpct, f1) == 4, "layout of `IoError!u16`");
gm_b0_1 gm_dt365sua5c99_0(void *env, gm_node *cx, gm_node *inst, uint32_t a1, gm_b16_8 a2);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1, uint16_t a2);
gm_thf5kr0is9u9i gm_dtffkeesmui4_10(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_tvcqs9u9iqpct gm_dtffkeesmui4_2(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_t62ebbi2i5rkd gm_dtffkeesmui4_3(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_thf5kr0is9u9i gm_dtffkeesmui4_4(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_thf5kr0is9u9i gm_dtffkeesmui4_5(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_b16_8 a2);
gm_b0_1 gm_dtffkeesmui4_6(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_b0_1 gm_dtffkeesmui4_7(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1);
gm_ttln0d0hd70en gm_dtffkeesmui4_8(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t9qkh0rsooc8d a2);
gm_ttln0d0hd70en gm_dtffkeesmui4_9(void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a1, gm_t6ie8iajlnpt1 a2, gm_t6ie8iajlnpt1 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_86gje82(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ukps8ob(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c12218cktbl[102] __attribute__((aligned(1)));
extern uint8_t gm_c1k7p3505ef[13] __attribute__((aligned(1)));
extern uint8_t gm_c21nulmhcfa[12] __attribute__((aligned(1)));
extern uint8_t gm_c3c901bgg39[96] __attribute__((aligned(8)));
extern uint8_t gm_c3dq62rrli9[16] __attribute__((aligned(8)));
extern uint8_t gm_c3t8vp50qki[13] __attribute__((aligned(1)));
extern uint8_t gm_c42qjm5ks8a[14] __attribute__((aligned(1)));
extern uint8_t gm_c4v5g7vo02e[9] __attribute__((aligned(1)));
extern uint8_t gm_c6d151di7ae[23] __attribute__((aligned(1)));
extern uint8_t gm_c82til0j71d[13] __attribute__((aligned(1)));
extern uint8_t gm_c8fdn45dfbi[16] __attribute__((aligned(1)));
extern uint8_t gm_c8o6bldrs15[43] __attribute__((aligned(1)));
extern uint8_t gm_capnegefe86[16] __attribute__((aligned(8)));
extern uint8_t gm_cb2tp7m3jgm[9] __attribute__((aligned(1)));
extern uint8_t gm_cb3qqtegs3b[10] __attribute__((aligned(1)));
extern uint8_t gm_cbata63g83q[3] __attribute__((aligned(1)));
extern uint8_t gm_cbci9h62d87[8] __attribute__((aligned(1)));
extern uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
extern uint8_t gm_cbr6nfilhfs[8] __attribute__((aligned(1)));
extern uint8_t gm_cbu54rqt96g[4832] __attribute__((aligned(8)));
extern uint8_t gm_cc763fsv023[15] __attribute__((aligned(1)));
extern uint8_t gm_cd231ebi1kg[16] __attribute__((aligned(8)));
extern uint8_t gm_cek1be2qo54[15] __attribute__((aligned(1)));
extern uint8_t gm_cel0duaak22[61] __attribute__((aligned(1)));
extern uint8_t gm_cf00a8vdcu4[13] __attribute__((aligned(1)));
extern uint8_t gm_cf4gomohuig[12] __attribute__((aligned(1)));
extern uint8_t gm_cfc3i2mbui9[16] __attribute__((aligned(8)));
extern uint8_t gm_cgfibmbf962[3] __attribute__((aligned(1)));
extern uint8_t gm_ch92utta9bv[1] __attribute__((aligned(1)));
extern uint8_t gm_chpv6gcip6t[6] __attribute__((aligned(1)));
extern uint8_t gm_ciadv3fllt2[14] __attribute__((aligned(1)));
extern uint8_t gm_cj54d91346t[2] __attribute__((aligned(1)));
extern uint8_t gm_cko3dv3bfgu[15] __attribute__((aligned(1)));
extern uint8_t gm_cm8j41tujfe[67] __attribute__((aligned(1)));
extern uint8_t gm_cmcv9v31fv5[10] __attribute__((aligned(1)));
extern uint8_t gm_cogdbob2e97[5] __attribute__((aligned(1)));
extern uint8_t gm_couns2p4unr[69] __attribute__((aligned(1)));
extern uint8_t gm_cp0okt01b9q[192] __attribute__((aligned(8)));
extern uint8_t gm_cp3vdr1lhrm[3] __attribute__((aligned(1)));
extern uint8_t gm_cpi9n8ms3ie[4] __attribute__((aligned(1)));
extern uint8_t gm_cq425m2mlf6[10] __attribute__((aligned(1)));
extern uint8_t gm_cqj3uiou0f0[12] __attribute__((aligned(1)));
extern uint8_t gm_cqup1m14s8b[17] __attribute__((aligned(1)));
extern uint8_t gm_crfi87obk4n[16] __attribute__((aligned(1)));
extern uint8_t gm_crr0lcslqi7[7] __attribute__((aligned(1)));
extern uint8_t gm_ct04sva93t7[1006] __attribute__((aligned(1)));
extern uint8_t gm_cu6c7r2uv4n[14] __attribute__((aligned(1)));
extern uint8_t gm_cukp3dhhpod[12] __attribute__((aligned(1)));
extern uint8_t gm_cv0gcjmll71[20] __attribute__((aligned(1)));
extern uint8_t gm_cvde9thke2j[13] __attribute__((aligned(1)));
extern uint8_t gm_cvg0qa54vnl[11] __attribute__((aligned(1)));
extern void *const gm_hd365sua5c99[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hllt7e2ogusq[];
extern void *const gm_hlusqsd00u9k[];
extern uint8_t gm_k1fl2q13fn0[40] __attribute__((aligned(8)));
extern uint8_t gm_k7898pnkupv[40] __attribute__((aligned(8)));
extern uint8_t gm_ka2mtmecqk3[40] __attribute__((aligned(8)));
extern uint8_t gm_kct6b70ce3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kd6k1g0uehn[40] __attribute__((aligned(8)));
extern uint8_t gm_keudd4gct2b[40] __attribute__((aligned(8)));
extern uint8_t gm_kh31phsjqu4[40] __attribute__((aligned(8)));
extern uint8_t gm_ko0rgal5bm2[40] __attribute__((aligned(8)));
extern uint8_t gm_kr1kqehd6fp[40] __attribute__((aligned(8)));
extern uint8_t gm_ksccp6j96fm[40] __attribute__((aligned(8)));
extern uint8_t gm_kshbjn6sg2q[40] __attribute__((aligned(8)));
extern uint8_t gm_ksoekcu07mu[40] __attribute__((aligned(8)));
extern uint8_t gm_kt1fbj7k4jp[40] __attribute__((aligned(8)));
extern uint8_t gm_kukue15f4h3[40] __attribute__((aligned(8)));
extern const uint8_t gm_s020nnomb61pi[11];
extern const uint8_t gm_s03s7smc9pnp4[8];
extern const uint8_t gm_s048hmhpmbi7c[8];
extern const uint8_t gm_s04n2ppe2fa02[9];
extern const uint8_t gm_s070mdv4o3o2t[28];
extern const uint8_t gm_s0fim1krogkdi[18];
extern const uint8_t gm_s0g6c2mg1vd79[39];
extern const uint8_t gm_s0i30h5fom257[41];
extern const uint8_t gm_s0o9ngaj56qhu[12];
extern const uint8_t gm_s0p8s4c02d6fe[151];
extern const uint8_t gm_s0u47ivcj4oar[8];
extern const uint8_t gm_s0vqlasml1r2u[41];
extern const uint8_t gm_s190hapch8qff[16];
extern const uint8_t gm_s1ecd3tdb8cgq[8];
extern const uint8_t gm_s1g69u68g0ra5[3];
extern const uint8_t gm_s1q2nkavemrqf[9];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s1sbi8ubhu50k[51];
extern const uint8_t gm_s1tv0n9hk7497[58];
extern const uint8_t gm_s1ve6cfk7slm1[10];
extern const uint8_t gm_s226st57gplo6[7];
extern const uint8_t gm_s22rffv4gvb2j[193];
extern const uint8_t gm_s2328gjukbu28[16];
extern const uint8_t gm_s23n3pek53qm1[24];
extern const uint8_t gm_s24frr2u6q1at[9];
extern const uint8_t gm_s252dj7ek002d[8];
extern const uint8_t gm_s2568a81aaotm[70];
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s29f0gjhjs59d[5];
extern const uint8_t gm_s2bl27qev8o2g[47];
extern const uint8_t gm_s2e2o3u4kr3q0[5];
extern const uint8_t gm_s2fom02opod7c[11];
extern const uint8_t gm_s2mpgumjqhsl0[15];
extern const uint8_t gm_s2oe2s4851fv5[4];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s2to760maglj8[13];
extern const uint8_t gm_s30jlcv6uaqf6[12];
extern const uint8_t gm_s30rt0lbjn52b[19];
extern const uint8_t gm_s31g9ftlb6lgj[11];
extern const uint8_t gm_s33a8rv0vs9rh[3];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s35gfshei80n5[8];
extern const uint8_t gm_s3bakerohlr2a[3];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s3dj47j0skrv7[60];
extern const uint8_t gm_s3e1pn3bonjpd[9];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3kchfpm4bdoi[6];
extern const uint8_t gm_s3kin7bltuqdc[35];
extern const uint8_t gm_s3l9vh8s8h1c1[54];
extern const uint8_t gm_s3s5u5e6aukj7[48];
extern const uint8_t gm_s4109lou8dsdj[12];
extern const uint8_t gm_s419erk5rpblh[39];
extern const uint8_t gm_s4296alm81si3[14];
extern const uint8_t gm_s454sq1vshj3e[77];
extern const uint8_t gm_s48njnbffa02m[5];
extern const uint8_t gm_s4bcc8cj66rgc[41];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_s4l2s2smm9apo[21];
extern const uint8_t gm_s4mqk7h9f98g3[30];
extern const uint8_t gm_s4s1nl0uacdl1[22];
extern const uint8_t gm_s4ss30tejbl47[32];
extern const uint8_t gm_s5bvhb1j14g3q[9];
extern const uint8_t gm_s5cr5924ev095[65];
extern const uint8_t gm_s5dnt9mgfponp[12];
extern const uint8_t gm_s5e5797b1jurs[20];
extern const uint8_t gm_s5fgj0n2t69gl[8];
extern const uint8_t gm_s5frmv5tebja4[50];
extern const uint8_t gm_s5gaofiqkqtj6[6];
extern const uint8_t gm_s5gha6q48spt5[6];
extern const uint8_t gm_s5hmldklohj31[36];
extern const uint8_t gm_s5hode9408atm[9];
extern const uint8_t gm_s5hom3aej4i0k[10];
extern const uint8_t gm_s5j39opibacm9[12];
extern const uint8_t gm_s5kue8bfcvqfr[27];
extern const uint8_t gm_s5lecs8v1je8f[18];
extern const uint8_t gm_s5q2m7astgscg[10];
extern const uint8_t gm_s5r4hcdoqsklk[53];
extern const uint8_t gm_s5trirh6cc9u8[86];
extern const uint8_t gm_s5vogfeoqmvr7[4];
extern const uint8_t gm_s60eo1gv73iic[5];
extern const uint8_t gm_s655gufo2sud7[15];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_s68nsnvq9a8da[20];
extern const uint8_t gm_s6e8s18mgu3dj[19];
extern const uint8_t gm_s6gil1vdg9ij2[41];
extern const uint8_t gm_s6j4ug98j8dce[39];
extern const uint8_t gm_s6kjraurhulj1[41];
extern const uint8_t gm_s6li878a3ke9r[41];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s6m55kvvtug5k[12];
extern const uint8_t gm_s6nbs6o2qkgvg[16];
extern const uint8_t gm_s6ofccoi3ursa[12];
extern const uint8_t gm_s6tk7vj0njd3g[35];
extern const uint8_t gm_s719fakvceli5[11];
extern const uint8_t gm_s71l0hs29ihls[39];
extern const uint8_t gm_s73ofu7ms28r6[23];
extern const uint8_t gm_s73qv4ju7m615[18];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s7agona8glbka[13];
extern const uint8_t gm_s7kb0m6jm95m7[16];
extern const uint8_t gm_s7moa4u0q6e5d[8];
extern const uint8_t gm_s7q988it6lbn6[15];
extern const uint8_t gm_s7ss5dueeuc9a[5];
extern const uint8_t gm_s7tfst2qj3lk8[24];
extern const uint8_t gm_s7tvcka4r1prv[10];
extern const uint8_t gm_s7u6ij52clodh[7];
extern const uint8_t gm_s7uepsfl7trr3[70];
extern const uint8_t gm_s84ckbhm2d3ul[9];
extern const uint8_t gm_s86g8amsftemj[8];
extern const uint8_t gm_s885vckaljhqv[3];
extern const uint8_t gm_s88makc8ms40g[271];
extern const uint8_t gm_s89fn6dpk70na[17];
extern const uint8_t gm_s8d8bnm2cl1li[7];
extern const uint8_t gm_s8dg516bb3flg[14];
extern const uint8_t gm_s8e077cc2g2c4[41];
extern const uint8_t gm_s8e4i8e286ns3[25];
extern const uint8_t gm_s8elfu9mreigg[24];
extern const uint8_t gm_s8eoogq65pabj[44];
extern const uint8_t gm_s8lvo4deu27ek[16];
extern const uint8_t gm_s8oho6fnunkdm[14];
extern const uint8_t gm_s8opvvbtc63n4[8];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s8s6lb59hkm4u[41];
extern const uint8_t gm_s8s91tiu5r15q[45];
extern const uint8_t gm_s91ab3v9oinn7[11];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s92f6uihka8a6[16];
extern const uint8_t gm_s94bv82atjueb[5];
extern const uint8_t gm_s9819enjtsd7t[10];
extern const uint8_t gm_s99d8nbbgh68j[15];
extern const uint8_t gm_s9aa9jihjgf22[9];
extern const uint8_t gm_s9cg870cl2t07[6];
extern const uint8_t gm_s9d990kf69gae[7];
extern const uint8_t gm_s9h42skj8t388[90];
extern const uint8_t gm_s9habjfdn88uu[31];
extern const uint8_t gm_s9nvmaqikg2bc[8];
extern const uint8_t gm_s9pqiujjjjsta[55];
extern const uint8_t gm_s9uf4t24m7c3t[31];
extern const uint8_t gm_s9vfcoh7jj30b[17];
extern const uint8_t gm_sa05gpeedjved[8];
extern const uint8_t gm_sa0alba1kin3k[8];
extern const uint8_t gm_sa0kbl9vbckel[8];
extern const uint8_t gm_sa1kbs58qog9s[41];
extern const uint8_t gm_sa45sq4bgugc4[50];
extern const uint8_t gm_sa71447djt33j[49];
extern const uint8_t gm_sa7g9ebnm90nd[100];
extern const uint8_t gm_sac45tip59t28[26];
extern const uint8_t gm_sad0udrpgk27o[3];
extern const uint8_t gm_sad95m298up5o[44];
extern const uint8_t gm_saed870gk04of[14];
extern const uint8_t gm_saeqiapbqfqhj[28];
extern const uint8_t gm_sai3vjkob4s2v[10];
extern const uint8_t gm_sait90vqr6gam[33];
extern const uint8_t gm_sakto9q5516h3[50];
extern const uint8_t gm_sanq8fcommvlg[12];
extern const uint8_t gm_saojq2l3thnfg[14];
extern const uint8_t gm_sapag1m4a00vt[16];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_savkl1knf20fd[11];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sb1dc9u3lvvu1[5];
extern const uint8_t gm_sb7dhp0dcdlo6[86];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sba4cjt08ps1i[58];
extern const uint8_t gm_sbbucsktpih4c[44];
extern const uint8_t gm_sbcq15b9em6fm[9];
extern const uint8_t gm_sbiv1tnsmo6hr[35];
extern const uint8_t gm_sbiv3c5qa6ipi[6];
extern const uint8_t gm_sbu7knoujlojo[56];
extern const uint8_t gm_sbvqgqcoh1qhn[56];
extern const uint8_t gm_sc21u906vi2gp[72];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_sc7nhpcejp1mj[21];
extern const uint8_t gm_sca8l83n950sa[8];
extern const uint8_t gm_scj4golhlm008[12];
extern const uint8_t gm_scotq6054g01h[6];
extern const uint8_t gm_scrq475mutg06[4];
extern const uint8_t gm_scs7jonv1denn[64];
extern const uint8_t gm_scu4ehdqrc5fh[19];
extern const uint8_t gm_scvcjjoadl8ss[9];
extern const uint8_t gm_sd1tvohjm34vg[37];
extern const uint8_t gm_sd276t5aajuq8[3];
extern const uint8_t gm_sd2mrmucl6ngt[7];
extern const uint8_t gm_sd6gna8nkqir4[4];
extern const uint8_t gm_sdbekdpro29kh[39];
extern const uint8_t gm_sddbbtt734m9j[9];
extern const uint8_t gm_sddghgsivu6pc[17];
extern const uint8_t gm_sdfmdfp384tf7[11];
extern const uint8_t gm_sdgvv61taqc3d[11];
extern const uint8_t gm_sdq87al6fhs9i[35];
extern const uint8_t gm_sdrhpr2s6028d[6];
extern const uint8_t gm_sdriahok6illd[12];
extern const uint8_t gm_sdscrog2npict[23];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sdsu0imtl17e3[3];
extern const uint8_t gm_sdv3b0kjlrdl2[46];
extern const uint8_t gm_se0cqtnfss5el[9];
extern const uint8_t gm_se1pn0lpfg41k[18];
extern const uint8_t gm_se31bb9e7icn2[11];
extern const uint8_t gm_se6fhe7nkildm[18];
extern const uint8_t gm_segrf6njnsvht[69];
extern const uint8_t gm_sej0mmp3fhvu3[20];
extern const uint8_t gm_sejbaeqb5aort[57];
extern const uint8_t gm_sejtof4qcrkoq[8];
extern const uint8_t gm_sek3nvre3su54[17];
extern const uint8_t gm_seqji52qmi3ea[4];
extern const uint8_t gm_sern92gk4dtsm[13];
extern const uint8_t gm_sett0rbmg4jjf[60];
extern const uint8_t gm_sf1ub570rl3io[41];
extern const uint8_t gm_sf2r2plsgb7n7[18];
extern const uint8_t gm_sf3uq7o37v8ks[13];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sf88t2ve1id1f[9];
extern const uint8_t gm_sfcul3vukc1i9[9];
extern const uint8_t gm_sfg1e0lbt8sf5[3];
extern const uint8_t gm_sfh7t0gvf9bnt[5];
extern const uint8_t gm_sfhcr4tad3e77[85];
extern const uint8_t gm_sfljf7mvjr19m[106];
extern const uint8_t gm_sfn640udk6pjn[19];
extern const uint8_t gm_sfrlts7iu6kt3[23];
extern const uint8_t gm_sftmhd38hos59[31];
extern const uint8_t gm_sfubbk3hjkq2r[41];
extern const uint8_t gm_sg027ftm3ja87[33];
extern const uint8_t gm_sg3a8rmfq3j12[50];
extern const uint8_t gm_sg5e6vqb8nie3[18];
extern const uint8_t gm_sg7m4t90kovch[35];
extern const uint8_t gm_sg7q4ebvc557r[14];
extern const uint8_t gm_sg8f7kt1n4uc5[48];
extern const uint8_t gm_sg8ojjtakanaa[5];
extern const uint8_t gm_sg8s3e7n1hsck[34];
extern const uint8_t gm_sgb7sja94ppju[26];
extern const uint8_t gm_sgc02r26g8j6o[57];
extern const uint8_t gm_sggv9d7r1i8vn[15];
extern const uint8_t gm_sgh9e8ka5t0qj[12];
extern const uint8_t gm_sgigm9413tsmn[11];
extern const uint8_t gm_sgk015tlfp2cu[5];
extern const uint8_t gm_sgn3gq6gfib7b[40];
extern const uint8_t gm_sgpokbp1avpcv[14];
extern const uint8_t gm_sgqep6dpse2vs[64];
extern const uint8_t gm_sgr7vbgb5cqmg[29];
extern const uint8_t gm_sgtpi35uur9ic[7];
extern const uint8_t gm_sgv98i2v4c6ls[93];
extern const uint8_t gm_sh0huebkcvk29[20];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4f06c2ecgj1[5];
extern const uint8_t gm_sh60ig4u675k6[74];
extern const uint8_t gm_sh8j22p8lpudd[15];
extern const uint8_t gm_shciqla3d0quk[14];
extern const uint8_t gm_shcmihp6nmad0[10];
extern const uint8_t gm_shl253arneuel[19];
extern const uint8_t gm_shngka39dfime[31];
extern const uint8_t gm_shoc8pam5lt18[15];
extern const uint8_t gm_shs3nem6t7u8g[4];
extern const uint8_t gm_si7nk951uu78e[16];
extern const uint8_t gm_si7pm1nlmobd4[19];
extern const uint8_t gm_siblunnp4nahm[10];
extern const uint8_t gm_sic1ggm4bq1di[9];
extern const uint8_t gm_sihb07k8fk2nc[35];
extern const uint8_t gm_sihb3eljrjar6[16];
extern const uint8_t gm_sina4qq1cj187[12];
extern const uint8_t gm_siqc5cjnsa99v[38];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sj1d6tstgco64[89];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sj4epgmk695op[75];
extern const uint8_t gm_sj6g27066vk8v[5];
extern const uint8_t gm_sja1nsq0b10to[16];
extern const uint8_t gm_sjbtfuea8qauf[23];
extern const uint8_t gm_sjbtn8nt9amae[3];
extern const uint8_t gm_sjehit11lds9h[34];
extern const uint8_t gm_sjgng334veu6f[8];
extern const uint8_t gm_sjhhuab713r3m[34];
extern const uint8_t gm_sjik6bsmdi5g2[83];
extern const uint8_t gm_sjjb6u8f31rna[15];
extern const uint8_t gm_sjnu6spl5smv6[107];
extern const uint8_t gm_sjourjtj9e3to[9];
extern const uint8_t gm_sjqf7m3o9alo0[9];
extern const uint8_t gm_sjstmj3q83l74[17];
extern const uint8_t gm_sjt41g4f7bvrv[32];
extern const uint8_t gm_sk6k62bfi6ka6[19];
extern const uint8_t gm_sk77bvr488rq3[10];
extern const uint8_t gm_skftljk77e6mu[4];
extern const uint8_t gm_skk6lm5m17t6v[11];
extern const uint8_t gm_skprsag8qovlp[22];
extern const uint8_t gm_sks5ueb3h9e10[24];
extern const uint8_t gm_skvf934c3l76i[41];
extern const uint8_t gm_sl02tfafo85hr[29];
extern const uint8_t gm_sl24u2f29gnqn[25];
extern const uint8_t gm_sl3f0h4bcevcn[5];
extern const uint8_t gm_sl3t4neg1tv2f[41];
extern const uint8_t gm_sl4co37h2cmmb[3];
extern const uint8_t gm_sl4p6vo2bm2sl[15];
extern const uint8_t gm_sl576h51kqj52[24];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sl5rf5cfkcmdk[7];
extern const uint8_t gm_sl775fgtt04iu[15];
extern const uint8_t gm_sl7a8f8pe8le9[21];
extern const uint8_t gm_sl9b2am1i9pmg[52];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_sla6dibovktss[41];
extern const uint8_t gm_slc3n2ku5uff5[55];
extern const uint8_t gm_sld5pmb7t0r25[9];
extern const uint8_t gm_slf85brqp8fiu[125];
extern const uint8_t gm_slgf61fmeeof0[28];
extern const uint8_t gm_slj1a57mmjpm9[56];
extern const uint8_t gm_slj3uno8bsqk4[27];
extern const uint8_t gm_slmas8vbhthkn[20];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_slnlsebkdpqrk[9];
extern const uint8_t gm_slnui26lp9aei[41];
extern const uint8_t gm_slpl0dlmfo4jt[6];
extern const uint8_t gm_slq8b0dsnkd8e[14];
extern const uint8_t gm_slrmmf184fvu8[26];
extern const uint8_t gm_sm7s66sg1itc1[4];
extern const uint8_t gm_sm8tqmj46uffd[14];
extern const uint8_t gm_smd171q6j6al8[20];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smfess31q1bml[17];
extern const uint8_t gm_smi2s23d2qqne[6];
extern const uint8_t gm_smjq2fi49hdnf[11];
extern const uint8_t gm_smktpoks16pgi[6];
extern const uint8_t gm_sml38fgda46cm[56];
extern const uint8_t gm_smloid035k91b[55];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smpa8tl2uhggk[15];
extern const uint8_t gm_smr3su985na96[16];
extern const uint8_t gm_smuhqaa14mehe[16];
extern const uint8_t gm_sn05fk3conlo9[18];
extern const uint8_t gm_sn1sq59pu45vv[39];
extern const uint8_t gm_sn2risld8qhf6[65];
extern const uint8_t gm_sn4ua1mltfudn[13];
extern const uint8_t gm_sn937bfucthvg[4];
extern const uint8_t gm_sn9nkqgu8isqo[3];
extern const uint8_t gm_snes8rgq12tks[18];
extern const uint8_t gm_sngug8co99ok1[26];
extern const uint8_t gm_snod68sv8lqq6[45];
extern const uint8_t gm_snp3hu7kun7ag[23];
extern const uint8_t gm_snqm9b8h9b70a[120];
extern const uint8_t gm_snrqbteh7n9bd[66];
extern const uint8_t gm_so2blaqutst8g[53];
extern const uint8_t gm_so3hs9kj1bue6[21];
extern const uint8_t gm_so3l0v9m39i7o[15];
extern const uint8_t gm_so4k586t2v79t[64];
extern const uint8_t gm_so5215183on4e[58];
extern const uint8_t gm_so6ip6bkb1lgs[9];
extern const uint8_t gm_so7l0chqarptu[13];
extern const uint8_t gm_so91sclkgrupd[33];
extern const uint8_t gm_so9n4mmd960c7[6];
extern const uint8_t gm_soerl6rds8b9v[12];
extern const uint8_t gm_sofs2uc27fjvk[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soiotnern2p8m[19];
extern const uint8_t gm_soqhm2vmt2g9o[21];
extern const uint8_t gm_soqn25gc3d0ls[14];
extern const uint8_t gm_sosrg8fhr95t2[38];
extern const uint8_t gm_soukc8goe066i[6];
extern const uint8_t gm_sovfv4hr4sld4[17];
extern const uint8_t gm_sp1h1hg79ru76[38];
extern const uint8_t gm_sp1nce97af2b9[6];
extern const uint8_t gm_sp33bek143l58[23];
extern const uint8_t gm_sp525daee5j3r[85];
extern const uint8_t gm_sp5b2hbslij9l[5];
extern const uint8_t gm_sp6eh19rmca8b[36];
extern const uint8_t gm_sp6qfs9r7ep2n[75];
extern const uint8_t gm_spdft68ac8308[95];
extern const uint8_t gm_sped63ic8juob[17];
extern const uint8_t gm_spferj7kepn49[115];
extern const uint8_t gm_spffvcegd6os6[6];
extern const uint8_t gm_spl83f693c752[32];
extern const uint8_t gm_splm7v4oko2tv[15];
extern const uint8_t gm_spp1c7975jken[5];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_spvjnpejqaf76[18];
extern const uint8_t gm_sq17986ir1cd2[41];
extern const uint8_t gm_sq1oij2hrovtv[4];
extern const uint8_t gm_sq2g0rh16c4kh[16];
extern const uint8_t gm_sq5sjasaqn114[7];
extern const uint8_t gm_sq6kdq5587s3n[11];
extern const uint8_t gm_sqalimuc6s35k[32];
extern const uint8_t gm_sqcm7se7v2nvs[49];
extern const uint8_t gm_sqe4aodms9tfs[14];
extern const uint8_t gm_sqeqp2v8egtl4[93];
extern const uint8_t gm_sqi9n75mrenn7[5];
extern const uint8_t gm_sqli1lbe2mk02[17];
extern const uint8_t gm_sqnu8fg3ikbeg[35];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const uint8_t gm_sqqh9r6c17rju[7];
extern const uint8_t gm_sqrjr8qjf37v8[17];
extern const uint8_t gm_sr389r8t7d0mf[11];
extern const uint8_t gm_sr4magk9naq4q[23];
extern const uint8_t gm_sr9q8kbf253ia[45];
extern const uint8_t gm_src0qvon2jl38[28];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3s76fqo1a9jk[];
extern const char gm_src4vq75vtnd3bq[];
extern const char gm_src786i8duem00q[];
extern const char gm_src8sgunk80orlb[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcdo1hnokt0mq8[];
extern const char gm_srch470kggqbqqi[];
extern const char gm_srcici0r1ok5poc[];
extern const char gm_srck2hgk32jk2a3[];
extern const char gm_srck6bk8q21r07v[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcr4magk9naq4q[];
extern const char gm_srcs9q7ghsrcgv5[];
extern const char gm_srcvksed7nkdegg[];
extern const uint8_t gm_srgp1m0q2ut8m[12];
extern const uint8_t gm_srio0gkdttkj2[61];
extern const uint8_t gm_srk226j8lb4rq[9];
extern const uint8_t gm_srmistf0k19ab[72];
extern const uint8_t gm_sroje34o1kfta[38];
extern const uint8_t gm_srqr27qbutlf7[78];
extern const uint8_t gm_srrq1irp1jfep[12];
extern const uint8_t gm_srtkupsn5fo0o[123];
extern const uint8_t gm_ss2l6r6gldk9s[23];
extern const uint8_t gm_ss3cao14a6jed[13];
extern const uint8_t gm_ss3f61htk015v[7];
extern const uint8_t gm_ss5q3rrcb010f[27];
extern const uint8_t gm_ss6ubqdvd2q7k[60];
extern const uint8_t gm_ss738pdss75oa[9];
extern const uint8_t gm_ss8v6rf5v54r7[9];
extern const uint8_t gm_ss9sitl8h5uvg[18];
extern const uint8_t gm_ssa5e93p23oqi[90];
extern const uint8_t gm_ssd99k9slj99i[3];
extern const uint8_t gm_ssf8mqtfpipac[45];
extern const uint8_t gm_ssfjmtpagd1pu[6];
extern const uint8_t gm_sshv7blujc475[52];
extern const uint8_t gm_ssi40jo2ghpq5[24];
extern const uint8_t gm_ssm3bl5los9iv[5];
extern const uint8_t gm_ssmb6ukuk62fe[53];
extern const uint8_t gm_ssnku2e1q7u3k[13];
extern const uint8_t gm_sspd8sv58p3ks[72];
extern const uint8_t gm_st02nolqdtiev[9];
extern const uint8_t gm_st49tt11e1avl[8];
extern const uint8_t gm_st4t0ufodvddp[59];
extern const uint8_t gm_st7j9rqo01n3i[17];
extern const uint8_t gm_st87fk7ojs4l8[6];
extern const uint8_t gm_stamom3b25ln8[14];
extern const uint8_t gm_stb0ck7v8hutn[8];
extern const uint8_t gm_std7upetcqgjg[41];
extern const uint8_t gm_stdvq0tcga0vp[8];
extern const uint8_t gm_stfpae614bc1t[35];
extern const uint8_t gm_sti61120eur38[59];
extern const uint8_t gm_stingg155o3k5[8];
extern const uint8_t gm_struubjgj1nj2[15];
extern const uint8_t gm_stuutd7nfq21h[3];
extern const uint8_t gm_stvspp709ordv[3];
extern const uint8_t gm_su0bfk11j19aa[41];
extern const uint8_t gm_su22guvkav5b5[21];
extern const uint8_t gm_su2flp9mbv542[38];
extern const uint8_t gm_su8bu9ae1ds2f[19];
extern const uint8_t gm_sua08g8f9l2h5[12];
extern const uint8_t gm_sudnegb3umvac[12];
extern const uint8_t gm_suekibjsm2ees[34];
extern const uint8_t gm_sugv53k5dm8d7[24];
extern const uint8_t gm_sukjjfkvr8401[40];
extern const uint8_t gm_sun80ccb875r1[13];
extern const uint8_t gm_suntqnp3vfhar[16];
extern const uint8_t gm_suornq817q1pp[40];
extern const uint8_t gm_supnh5d0f56vu[8];
extern const uint8_t gm_suq3bgh3anb87[58];
extern const uint8_t gm_svfdfjqemp6o4[19];
extern const uint8_t gm_svkl3ccbc9hel[11];
extern const uint8_t gm_svmpuinrbii3k[41];
extern const uint8_t gm_svokmu75u5n0c[14];
extern const uint8_t gm_svr1ohtrtj17p[64];
extern const uint8_t gm_svr6jc92gv992[16];
extern const uint8_t gm_svrv9jmovu8v4[20];
extern const uint8_t gm_svsm5u96riots[18];
extern const uint8_t gm_svt8aldhoetro[3];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_tysqt22rph28;
const char gm_src3d0v42kavhv7[] = "lib/std/str/str.gm";
const char gm_src3s76fqo1a9jk[] = "compiler/pkg/archive.gm";
const char gm_src4vq75vtnd3bq[] = "compiler/check/coerce.gm";
const char gm_src786i8duem00q[] = "compiler/check/matching.gm";
const char gm_src8sgunk80orlb[] = "compiler/interp/consts.gm";
const char gm_srcbsc23is03ttm[] = "<core>/map.gm";
const char gm_srcdo1hnokt0mq8[] = "lib/std/fs/fs.gm";
const char gm_srch470kggqbqqi[] = "compiler/check/tests.gm";
const char gm_srcici0r1ok5poc[] = "compiler/check/jumps.gm";
const char gm_srck2hgk32jk2a3[] = "lib/std/thread/thread.gm";
const char gm_srck6bk8q21r07v[] = "compiler/ast/dump.gm";
const char gm_srcnqtpfma6qe4a[] = "compiler/interp/values.gm";
const char gm_srcr4magk9naq4q[] = "compiler/repl/input.gm";
const char gm_srcs9q7ghsrcgv5[] = "compiler/cgen/views.gm";
const char gm_srcvksed7nkdegg[] = "compiler/lsp/features.gm";
const gm_type gm_ty6groq37ram = {.size = 8, .align = 8, .kind = GM_K_INT, .bits = 64, .is_signed = false};
const gm_type gm_ty6groq37ram;
const gm_type gm_tysqt22rph28 = {.size = 8, .align = 8, .kind = GM_K_INT, .bits = 64, .is_signed = true};
const gm_type gm_tysqt22rph28;
const uint8_t gm_s020nnomb61pi[11] = "` (\302\24711.6)";
const uint8_t gm_s03s7smc9pnp4[8] = "numeric";
const uint8_t gm_s048hmhpmbi7c[8] = "Windows";
const uint8_t gm_s04n2ppe2fa02[9] = "\006\000\000\000\000\000\000\000";
const uint8_t gm_s070mdv4o3o2t[28] = "a numeric literal after `-`";
const uint8_t gm_s0fim1krogkdi[18] = "</title>\012<style>\012";
const uint8_t gm_s0g6c2mg1vd79[39] = "unexpected end of input while parsing ";
const uint8_t gm_s0i30h5fom257[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000y\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s0o9ngaj56qhu[12] = "initialized";
const uint8_t gm_s0p8s4c02d6fe[151] = "add debug information that maps the executable back to the\012Gamma source (build also writes the lldb formatters, gamma_lldb.py,\012next to the executable)";
const uint8_t gm_s0u47ivcj4oar[8] = "#simd [";
const uint8_t gm_s0vqlasml1r2u[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000/\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_s190hapch8qff[16] = "parse_union_lit";
const uint8_t gm_s1ecd3tdb8cgq[8] = "prelude";
const uint8_t gm_s1g69u68g0ra5[3] = "`#";
const uint8_t gm_s1q2nkavemrqf[9] = "BlackBox";
const uint8_t gm_s1s57v200fem0[6] = "{} {}";
const uint8_t gm_s1sbi8ubhu50k[51] = "an integer pattern needs an integer, but this is `";
const uint8_t gm_s1tv0n9hk7497[58] = "internal error: a shuffle of a value that is not a vector";
const uint8_t gm_s1ve6cfk7slm1[10] = "cranelift";
const uint8_t gm_s226st57gplo6[7] = " bits)";
const uint8_t gm_s22rffv4gvb2j[193] = "\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s2328gjukbu28[16] = "expected a type";
const uint8_t gm_s23n3pek53qm1[24] = "the type of parameter `";
const uint8_t gm_s24frr2u6q1at[9] = "mips64el";
const uint8_t gm_s252dj7ek002d[8] = "fn-type";
const uint8_t gm_s2568a81aaotm[70] = "unknown profile `{}`: gamma.toml defines no `[profile.<name>]` tables";
const uint8_t gm_s277jq8ulrihr[7] = "return";
const uint8_t gm_s29f0gjhjs59d[5] = "func";
const uint8_t gm_s2bl27qev8o2g[47] = "`{}` is a keyword and cannot be used as a name";
const uint8_t gm_s2e2o3u4kr3q0[5] = "code";
const uint8_t gm_s2fom02opod7c[11] = "gm_fs_seek";
const uint8_t gm_s2mpgumjqhsl0[15] = "a profile name";
const uint8_t gm_s2oe2s4851fv5[4] = "Net";
const uint8_t gm_s2ph9f020upd2[7] = "packed";
const uint8_t gm_s2to760maglj8[13] = "{}/bin/clang";
const uint8_t gm_s30jlcv6uaqf6[12] = "(uintptr_t)";
const uint8_t gm_s30rt0lbjn52b[19] = "reached through {}";
const uint8_t gm_s31g9ftlb6lgj[11] = ", .next = ";
const uint8_t gm_s33a8rv0vs9rh[3] = "-x";
const uint8_t gm_s33h2lce46bnm[9] = "` takes ";
const uint8_t gm_s35gfshei80n5[8] = "ToConst";
const uint8_t gm_s3bakerohlr2a[3] = "lo";
const uint8_t gm_s3cv29r83pgn4[6] = "never";
const uint8_t gm_s3dj47j0skrv7[60] = "invalid profile name `{}`: use letters, digits, `-` and `_`";
const uint8_t gm_s3e1pn3bonjpd[9] = "infinity";
const uint8_t gm_s3e9133b3pasn[3] = " `";
const uint8_t gm_s3kchfpm4bdoi[6] = "minus";
const uint8_t gm_s3kin7bltuqdc[35] = "`{}` is the name of a builtin type";
const uint8_t gm_s3l9vh8s8h1c1[54] = "`try` needs an optional or a result, but this is `{}`";
const uint8_t gm_s3s5u5e6aukj7[48] = "range patterns match integers, but this is `{}`";
const uint8_t gm_s4109lou8dsdj[12] = "wasm32-wasi";
const uint8_t gm_s419erk5rpblh[39] = "`{}` in {} must be an array of strings";
const uint8_t gm_s4296alm81si3[14] = "unused-import";
const uint8_t gm_s454sq1vshj3e[77] = "`{}` cannot be constructed outside its {}: its field `{}` is private (\302\2473.3)";
const uint8_t gm_s48njnbffa02m[5] = "File";
const uint8_t gm_s4bcc8cj66rgc[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\023\002\000\000 \000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s4fd35uidvgkn[4] = "try";
const uint8_t gm_s4l2s2smm9apo[21] = ", \"debugger view\");\012";
const uint8_t gm_s4mqk7h9f98g3[30] = " elements, but the value has ";
const uint8_t gm_s4s1nl0uacdl1[22] = " arguments, expected ";
const uint8_t gm_s4ss30tejbl47[32] = "there is nothing to rename here";
const uint8_t gm_s5bvhb1j14g3q[9] = "err = {}";
const uint8_t gm_s5cr5924ev095[65] = "this `defer` runs at once: it is the last statement of its block";
const uint8_t gm_s5dnt9mgfponp[12] = "gm_atag_cap";
const uint8_t gm_s5e5797b1jurs[20] = "this vector builtin";
const uint8_t gm_s5fgj0n2t69gl[8] = "offline";
const uint8_t gm_s5frmv5tebja4[50] = "hexadecimal float literals require a `p` exponent";
const uint8_t gm_s5gaofiqkqtj6[6] = " <==\012";
const uint8_t gm_s5gha6q48spt5[6] = "filcc";
const uint8_t gm_s5hmldklohj31[36] = "two handlers for `{}` in one `with`";
const uint8_t gm_s5hode9408atm[9] = "-lws2_32";
const uint8_t gm_s5hom3aej4i0k[10] = "--git-dir";
const uint8_t gm_s5j39opibacm9[12] = " = gm_find(";
const uint8_t gm_s5kue8bfcvqfr[27] = "this performs the effect `";
const uint8_t gm_s5lecs8v1je8f[18] = "-Wl,--stack-first";
const uint8_t gm_s5q2m7astgscg[10] = "gm_rt_str";
const uint8_t gm_s5r4hcdoqsklk[53] = "` is not an integer, so it cannot be an array length";
const uint8_t gm_s5trirh6cc9u8[86] = "an element of a `#soa` is assigned whole with `=`; assign to its fields with `a[i].f`";
const uint8_t gm_s5vogfeoqmvr7[4] = "DA:";
const uint8_t gm_s60eo1gv73iic[5] = "fb{}";
const uint8_t gm_s655gufo2sud7[15] = "gm_thread_main";
const uint8_t gm_s67fd8s6hf84e[24] = "this compile-time value";
const uint8_t gm_s68nsnvq9a8da[20] = "cannot copy into {}";
const uint8_t gm_s6e8s18mgu3dj[19] = "no version tag of ";
const uint8_t gm_s6gil1vdg9ij2[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\261\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s6j4ug98j8dce[39] = "postfix operators after a construction";
const uint8_t gm_s6kjraurhulj1[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000j\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s6li878a3ke9r[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\222\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s6lnp3h2ojl1f[77] = "top-level bindings are immutable; use `::` (mutable storage needs `#global`)";
const uint8_t gm_s6m55kvvtug5k[12] = "); memcpy(&";
const uint8_t gm_s6nbs6o2qkgvg[16] = "type-is{} {} {}";
const uint8_t gm_s6ofccoi3ursa[12] = "FsCanonical";
const uint8_t gm_s6tk7vj0njd3g[35] = "add `{}` to the function's effects";
const uint8_t gm_s719fakvceli5[11] = "atomic_add";
const uint8_t gm_s71l0hs29ihls[39] = "((void (*)({}))GM_PTR_UNTAG(code))({})";
const uint8_t gm_s73ofu7ms28r6[23] = "shift amount too large";
const uint8_t gm_s73qv4ju7m615[18] = "`in` on this type";
const uint8_t gm_s76ud0alkjfse[4] = "arm";
const uint8_t gm_s7agona8glbka[13] = ")n->clauses[";
const uint8_t gm_s7kb0m6jm95m7[16] = "gm_filc_running";
const uint8_t gm_s7moa4u0q6e5d[8] = "pow_sat";
const uint8_t gm_s7q988it6lbn6[15] = "gm_fiber_entry";
const uint8_t gm_s7ss5dueeuc9a[5] = "<h1>";
const uint8_t gm_s7tfst2qj3lk8[24] = "the complex operation `";
const uint8_t gm_s7tvcka4r1prv[10] = "system_io";
const uint8_t gm_s7u6ij52clodh[7] = "PtrAdd";
const uint8_t gm_s7uepsfl7trr3[70] = "`{}` takes `bool` values; on the lanes of a mask use `&`, `|` and `~`";
const uint8_t gm_s84ckbhm2d3ul[9] = "FileInfo";
const uint8_t gm_s86g8amsftemj[8] = "Shl({})";
const uint8_t gm_s885vckaljhqv[3] = "(&";
const uint8_t gm_s88makc8ms40g[271] = "` cannot run a program for a freestanding target (the C compiler targets no operating system); build it with `gamma build`, which writes an object file to link into the firmware or kernel. Tests and benchmarks run on the host: leave out --target (and unset CC) for them\012";
const uint8_t gm_s89fn6dpk70na[17] = "unknown effect `";
const uint8_t gm_s8d8bnm2cl1li[7] = "</h2>\012";
const uint8_t gm_s8dg516bb3flg[14] = "gm_sink_float";
const uint8_t gm_s8e077cc2g2c4[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\025\000\000\000\"\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s8e4i8e286ns3[25] = "({})(({}){{0}} - ({}){})";
const uint8_t gm_s8elfu9mreigg[24] = "header line is too long";
const uint8_t gm_s8eoogq65pabj[44] = " of an array type with a constant length: `";
const uint8_t gm_s8lvo4deu27ek[16] = "tag_signed = {}";
const uint8_t gm_s8oho6fnunkdm[14] = "_Alignas({}) ";
const uint8_t gm_s8opvvbtc63n4[8] = "alignas";
const uint8_t gm_s8p032eorpf80[3] = ".o";
const uint8_t gm_s8p1h96ji9kh0[6] = "union";
const uint8_t gm_s8s6lb59hkm4u[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000C\001\000\000N\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s8s91tiu5r15q[45] = "`@variants` takes a tagged union type, not `";
const uint8_t gm_s91ab3v9oinn7[11] = "refs/gamma";
const uint8_t gm_s920dpvvv6cej[4] = "and";
const uint8_t gm_s92f6uihka8a6[16] = "invalid digit `";
const uint8_t gm_s94bv82atjueb[5] = "nf{}";
const uint8_t gm_s9819enjtsd7t[10] = "fs_remove";
const uint8_t gm_s99d8nbbgh68j[15] = "` without `()`";
const uint8_t gm_s9aa9jihjgf22[9] = "arm64_be";
const uint8_t gm_s9cg870cl2t07[6] = "items";
const uint8_t gm_s9d990kf69gae[7] = "#endif";
const uint8_t gm_s9h42skj8t388[90] = "{}; a type parameter that the arguments determine is written by name, if at all: `{}: {}`";
const uint8_t gm_s9habjfdn88uu[31] = "__attribute__((aligned(4))) {}";
const uint8_t gm_s9nvmaqikg2bc[8] = "GM_EXIT";
const uint8_t gm_s9pqiujjjjsta[55] = "internal error: not supported by the interpreter yet: ";
const uint8_t gm_s9uf4t24m7c3t[31] = "gamma.toml has no dependency `";
const uint8_t gm_s9vfcoh7jj30b[17] = " has no commit `";
const uint8_t gm_sa05gpeedjved[8] = "Imports";
const uint8_t gm_sa0alba1kin3k[8] = "VecLoad";
const uint8_t gm_sa0kbl9vbckel[8] = "`, not ";
const uint8_t gm_sa1kbs58qog9s[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000.\002\000\000\034\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sa45sq4bgugc4[50] = "{} an integer, float, `bool` or pointer, not `{}`";
const uint8_t gm_sa71447djt33j[49] = "` is an effect set, not a single effect (\302\24712.3)";
const uint8_t gm_sa7g9ebnm90nd[100] = "(x)` would be a conversion to a type parameter, which is not lossless for every type; use `x.trunc(";
const uint8_t gm_sac45tip59t28[26] = "to begin the handler body";
const uint8_t gm_sad0udrpgk27o[3] = "*|";
const uint8_t gm_sad95m298up5o[44] = "the generated C cannot be split into units\012";
const uint8_t gm_saed870gk04of[14] = "slice bounds ";
const uint8_t gm_saeqiapbqfqhj[28] = "; a scalar is not added to ";
const uint8_t gm_sai3vjkob4s2v[10] = "FsSymlink";
const uint8_t gm_sait90vqr6gam[33] = "`.bits` needs equal sizes, but `";
const uint8_t gm_sakto9q5516h3[50] = "`^` dereferences a pointer `^T`, but this is `{}`";
const uint8_t gm_sanq8fcommvlg[12] = "Broken pipe";
const uint8_t gm_saojq2l3thnfg[14] = "no input file";
const uint8_t gm_sapag1m4a00vt[16] = "a union literal";
const uint8_t gm_sauf6ibu7mnba[5] = "the ";
const uint8_t gm_savkl1knf20fd[11] = "# Module `";
const uint8_t gm_savtbnmf9bjnr[17] = "<{}-bit integer>";
const uint8_t gm_sb1dc9u3lvvu1[5] = "lc{}";
const uint8_t gm_sb7dhp0dcdlo6[86] = "Safe builds compile with the Fil-C compiler (https://fil-c.org), but the C compiler `";
const uint8_t gm_sb9352gc5i144[21] = "tag() of a non-union";
const uint8_t gm_sba4cjt08ps1i[58] = "variadic parameters are allowed only in foreign functions";
const uint8_t gm_sbbucsktpih4c[44] = "` is a keyword and cannot be used as a name";
const uint8_t gm_sbcq15b9em6fm[9] = "SysWrite";
const uint8_t gm_sbiv1tnsmo6hr[35] = "no instantiation is available here";
const uint8_t gm_sbiv3c5qa6ipi[6] = "unary";
const uint8_t gm_sbu7knoujlojo[56] = "a slice bound must have an unsigned integer type, not `";
const uint8_t gm_sbvqgqcoh1qhn[56] = "the field `{}` of `{}` is private to its module (\302\2473.3)";
const uint8_t gm_sc21u906vi2gp[72] = "a spread needs a tuple, an array or a struct with visible fields, not `";
const uint8_t gm_sc6st5mdtp49u[16] = "unknown type {}";
const uint8_t gm_sc7nhpcejp1mj[21] = "(__builtin_isfinite(";
const uint8_t gm_sca8l83n950sa[8] = "{:.1}ms";
const uint8_t gm_scj4golhlm008[12] = "gm_net_recv";
const uint8_t gm_scotq6054g01h[6] = "lane ";
const uint8_t gm_scrq475mutg06[4] = " F;";
const uint8_t gm_scs7jonv1denn[64] = "invalid library name `{}` in {}: write `z`, not `-lz` or a path";
const uint8_t gm_scu4ehdqrc5fh[19] = "__builtin_ceil({})";
const uint8_t gm_scvcjjoadl8ss[9] = "\000\001\000\000\000\000\000\000";
const uint8_t gm_sd1tvohjm34vg[37] = "invalid `url` of dependency `{}`: {}";
const uint8_t gm_sd276t5aajuq8[3] = "[=";
const uint8_t gm_sd2mrmucl6ngt[7] = ", env)";
const uint8_t gm_sd6gna8nkqir4[4] = " ^ ";
const uint8_t gm_sdbekdpro29kh[39] = "for (uintptr_t i = 0; i < (uintptr_t)(";
const uint8_t gm_sddbbtt734m9j[9] = "read_all";
const uint8_t gm_sddghgsivu6pc[17] = "(gm_node *cx) {\012";
const uint8_t gm_sdfmdfp384tf7[11] = "opt-unwrap";
const uint8_t gm_sdgvv61taqc3d[11] = "variants `";
const uint8_t gm_sdq87al6fhs9i[35] = "unclosed array table, expected `]`";
const uint8_t gm_sdrhpr2s6028d[6] = "\"));\012";
const uint8_t gm_sdriahok6illd[12] = "the field `";
const uint8_t gm_sdscrog2npict[23] = "stack size needed here";
const uint8_t gm_sdsd71bjtv81c[98] = "a second-class function cannot be returned; return a function pointer `^fn(\342\200\246)` instead (\302\24711.6)";
const uint8_t gm_sdsu0imtl17e3[3] = "./";
const uint8_t gm_sdv3b0kjlrdl2[46] = "allocation alignment {} is not a power of two";
const uint8_t gm_se0cqtnfss5el[9] = " bytes; ";
const uint8_t gm_se1pn0lpfg41k[18] = "parse_handler_lit";
const uint8_t gm_se31bb9e7icn2[11] = "foreign-fn";
const uint8_t gm_se6fhe7nkildm[18] = "__SIZEOF_INT128__";
const uint8_t gm_segrf6njnsvht[69] = "`{}` has several variants carrying `{}`; handle the error explicitly";
const uint8_t gm_sej0mmp3fhvu3[20] = " bytes does not fit";
const uint8_t gm_sejbaeqb5aort[57] = "a vector holds at most {} bytes; {} lanes of `{}` are {}";
const uint8_t gm_sejtof4qcrkoq[8] = "-endian";
const uint8_t gm_sek3nvre3su54[17] = "boolean constant";
const uint8_t gm_seqji52qmi3ea[4] = "F.w";
const uint8_t gm_sern92gk4dtsm[13] = "gm_l{}_{}_{}";
const uint8_t gm_sett0rbmg4jjf[60] = "`{}` is inferred as both `{}` and `{}`; write it explicitly";
const uint8_t gm_sf1ub570rl3io[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\355\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sf2r2plsgb7n7[18] = "gm_rt_fiber_tramp";
const uint8_t gm_sf3uq7o37v8ks[13] = "nfields = {}";
const uint8_t gm_sf50oc24o4dhk[11] = "write `{}`";
const uint8_t gm_sf88t2ve1id1f[9] = "\020\000\000\000\000\000\000\000";
const uint8_t gm_sfcul3vukc1i9[9] = "gm_fiber";
const uint8_t gm_sfg1e0lbt8sf5[3] = "-|";
const uint8_t gm_sfh7t0gvf9bnt[5] = "copy";
const uint8_t gm_sfhcr4tad3e77[85] = "` is generic, so it needs its compile-time arguments to be a function value: write `";
const uint8_t gm_sfljf7mvjr19m[106] = "a `ctl` clause must not finish normally: end every path with `resume`, an exit or a `never` call (\302\24714.3)";
const uint8_t gm_sfn640udk6pjn[19] = "` must be a string";
const uint8_t gm_sfrlts7iu6kt3[23] = "an unresolved coercion";
const uint8_t gm_sftmhd38hos59[31] = "Unknown error {} (os error {})";
const uint8_t gm_sfubbk3hjkq2r[41] = "` matches a union variant, but this is `";
const uint8_t gm_sg027ftm3ja87[33] = "`, which does not see the change";
const uint8_t gm_sg3a8rmfq3j12[50] = "`main` must return `()`, `u8` or `E!()`, not `{}`";
const uint8_t gm_sg5e6vqb8nie3[18] = ") __asm__(GM_SYM(";
const uint8_t gm_sg7m4t90kovch[35] = "the receiver and argument of `.{}`";
const uint8_t gm_sg7q4ebvc557r[14] = "array length ";
const uint8_t gm_sg8f7kt1n4uc5[48] = "` is not supported on this target (x86-64 only)";
const uint8_t gm_sg8ojjtakanaa[5] = "obj-";
const uint8_t gm_sg8s3e7n1hsck[34] = "the element and the range of `in`";
const uint8_t gm_sgb7sja94ppju[26] = "out of memory allocating ";
const uint8_t gm_sgc02r26g8j6o[57] = "`{}` is not available on {}: an element cannot be `null`";
const uint8_t gm_sggv9d7r1i8vn[15] = "gm_main_thread";
const uint8_t gm_sgh9e8ka5t0qj[12] = "<pre><code>";
const uint8_t gm_sgigm9413tsmn[11] = "gm_wsocket";
const uint8_t gm_sgk015tlfp2cu[5] = "body";
const uint8_t gm_sgn3gq6gfib7b[40] = "    return (a[0] != 0) == (b[0] != 0);\012";
const uint8_t gm_sgpokbp1avpcv[14] = "hoverProvider";
const uint8_t gm_sgqep6dpse2vs[64] = "the type of parameter `{}` cannot be inferred here; annotate it";
const uint8_t gm_sgr7vbgb5cqmg[29] = "compiler/testing/coverage.gm";
const uint8_t gm_sgtpi35uur9ic[7] = "upvar ";
const uint8_t gm_sgv98i2v4c6ls[93] = "a function literal used as a value cannot be generic; bind it with `name :: fn \342\200\246` (\302\24711.8)";
const uint8_t gm_sh0huebkcvk29[20] = "the exported name `";
const uint8_t gm_sh0j2ggs1vvoa[2] = "=";
const uint8_t gm_sh4e4kqha06b3[7] = "not-in";
const uint8_t gm_sh4f06c2ecgj1[5] = "'\\t'";
const uint8_t gm_sh60ig4u675k6[74] = ", so the type it is passed at cannot be inferred; write the type argument";
const uint8_t gm_sh8j22p8lpudd[15] = "gm_ctz({}, {})";
const uint8_t gm_shciqla3d0quk[14] = " points into ";
const uint8_t gm_shcmihp6nmad0[10] = "offset_of";
const uint8_t gm_shl253arneuel[19] = "gamma: cannot put ";
const uint8_t gm_shngka39dfime[31] = "(*({} *)((uint8_t *)&{} + {}))";
const uint8_t gm_shoc8pam5lt18[15] = "gm_file_exists";
const uint8_t gm_shs3nem6t7u8g[4] = "lit";
const uint8_t gm_si7nk951uu78e[16] = "collect_entries";
const uint8_t gm_si7pm1nlmobd4[19] = "\012pub mode :: Mode.";
const uint8_t gm_siblunnp4nahm[10] = "uintptr_t";
const uint8_t gm_sic1ggm4bq1di[9] = "\000\000\000\000\000\000\000\200";
const uint8_t gm_sihb07k8fk2nc[35] = "expected a value, found the type `";
const uint8_t gm_sihb3eljrjar6[16] = "converting to `";
const uint8_t gm_sina4qq1cj187[12] = "gm_wasi_arg";
const uint8_t gm_siqc5cjnsa99v[38] = "`@prefetch` takes a pointer, not `{}`";
const uint8_t gm_sitlbs33jqqal[2] = "i";
const uint8_t gm_sj1d6tstgco64[89] = "`: after the first segment it may not contain empty, `..` or absolute components (\302\2473.2)";
const uint8_t gm_sj1gcl0qo4i9p[7] = " bytes";
const uint8_t gm_sj4epgmk695op[75] = "`\077{}` binds an optional or a result as it is, but this is `{}`; write `{}`";
const uint8_t gm_sj6g27066vk8v[5] = "<em>";
const uint8_t gm_sja1nsq0b10to[16] = "(\342\200\246)` (\302\24712.1)";
const uint8_t gm_sjbtfuea8qauf[23] = "` is not a struct type";
const uint8_t gm_sjbtn8nt9amae[3] = " f";
const uint8_t gm_sjehit11lds9h[34] = "        default: return 2;\012    }\012";
const uint8_t gm_sjgng334veu6f[8] = "dry-run";
const uint8_t gm_sjhhuab713r3m[34] = "` must be a hexadecimal commit id";
const uint8_t gm_sjik6bsmdi5g2[83] = "an array indexed by a type needs a concrete enum type, not the type parameter `{}`";
const uint8_t gm_sjjb6u8f31rna[15] = "GM_LD({}, loc)";
const uint8_t gm_sjnu6spl5smv6[107] = "`@fiber` needs a function pointer `^fn(a: A) -> never needs \342\200\246` with the C calling convention, found `{}`";
const uint8_t gm_sjourjtj9e3to[9] = "pat-some";
const uint8_t gm_sjqf7m3o9alo0[9] = "tag `{}`";
const uint8_t gm_sjstmj3q83l74[17] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000";
const uint8_t gm_sjt41g4f7bvrv[32] = "__atomic_exchange_n({}, {}, {})";
const uint8_t gm_sk6k62bfi6ka6[19] = ", (uint8_t *)&r + ";
const uint8_t gm_sk77bvr488rq3[10] = "net_close";
const uint8_t gm_skftljk77e6mu[4] = "any";
const uint8_t gm_skk6lm5m17t6v[11] = "futex_wait";
const uint8_t gm_skprsag8qovlp[22] = "expected {}, found {}";
const uint8_t gm_sks5ueb3h9e10[24] = "{} does not fit in `{}`";
const uint8_t gm_skvf934c3l76i[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000P\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sl02tfafo85hr[29] = ") gm_trap(\"unwrap of null\", ";
const uint8_t gm_sl24u2f29gnqn[25] = "</code></h3>\012<pre><code>";
const uint8_t gm_sl3f0h4bcevcn[5] = "\000\001\000\000";
const uint8_t gm_sl3t4neg1tv2f[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000x\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sl4co37h2cmmb[3] = "~=";
const uint8_t gm_sl4p6vo2bm2sl[15] = ": dependency `";
const uint8_t gm_sl576h51kqj52[24] = "fields of #bits structs";
const uint8_t gm_sl5h9isb3bijl[5] = "{}{}";
const uint8_t gm_sl5rf5cfkcmdk[7] = "_WIN32";
const uint8_t gm_sl775fgtt04iu[15] = "trailing comma";
const uint8_t gm_sl7a8f8pe8le9[21] = "a `match` expression";
const uint8_t gm_sl9b2am1i9pmg[52] = "internal error: `try` without an enclosing function";
const uint8_t gm_sla3alt5340mf[8] = "version";
const uint8_t gm_sla6dibovktss[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000[\002\000\000 \000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_slc3n2ku5uff5[55] = "`profile` must be a table of `[profile.<name>]` tables";
const uint8_t gm_sld5pmb7t0r25[9] = "){0} - (";
const uint8_t gm_slf85brqp8fiu[125] = "generic instantiations nest more than {} deep; does a generic function call itself with ever-larger type arguments\077 (\302\24716.7)";
const uint8_t gm_slgf61fmeeof0[28] = "to begin the parameter list";
const uint8_t gm_slj1a57mmjpm9[56] = "the build mode (default: debug); safe builds\012with Fil-C";
const uint8_t gm_slj3uno8bsqk4[27] = "unsupported conversion to ";
const uint8_t gm_slmas8vbhthkn[20] = "`, which contains `";
const uint8_t gm_slmii6hjigrcr[7] = "<core>";
const uint8_t gm_slnlsebkdpqrk[9] = "not ({})";
const uint8_t gm_slnui26lp9aei[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\324\000\000\0007\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_slpl0dlmfo4jt[6] = "]) {\012";
const uint8_t gm_slq8b0dsnkd8e[14] = "core module `";
const uint8_t gm_slrmmf184fvu8[26] = "LiftErr {{ variant: {} }}";
const uint8_t gm_sm7s66sg1itc1[4] = " {\012";
const uint8_t gm_sm8tqmj46uffd[14] = " && offsetof(";
const uint8_t gm_smd171q6j6al8[20] = "LiftErr { variant: ";
const uint8_t gm_smee1janbl2t9[3] = "\\\"";
const uint8_t gm_smfess31q1bml[17] = "gm_proc_try_wait";
const uint8_t gm_smi2s23d2qqne[6] = "gm_rt";
const uint8_t gm_smjq2fi49hdnf[11] = "bind-whole";
const uint8_t gm_smktpoks16pgi[6] = " ({})";
const uint8_t gm_sml38fgda46cm[56] = "`.{}` needs a union type, but the expected type is `{}`";
const uint8_t gm_smloid035k91b[55] = "cannot rename `{}`: it is part of the standard library";
const uint8_t gm_smooaouqmm8dj[2] = "(";
const uint8_t gm_smpa8tl2uhggk[15] = "gm_wasi_set_sp";
const uint8_t gm_smr3su985na96[16] = "this conversion";
const uint8_t gm_smuhqaa14mehe[16] = "this array fill";
const uint8_t gm_sn05fk3conlo9[18] = "function literals";
const uint8_t gm_sn1sq59pu45vv[39] = "error: `main` is reserved by the REPL\012";
const uint8_t gm_sn2risld8qhf6[65] = "the package in {} has no dependency `{}`; it has no dependencies";
const uint8_t gm_sn4ua1mltfudn[13] = "iterator_arg";
const uint8_t gm_sn937bfucthvg[4] = "Out";
const uint8_t gm_sn9nkqgu8isqo[3] = ":t";
const uint8_t gm_snes8rgq12tks[18] = "gm_lexically_same";
const uint8_t gm_sngug8co99ok1[26] = "compiler/check/methods.gm";
const uint8_t gm_snod68sv8lqq6[45] = "negative shift amount in constant expression";
const uint8_t gm_snp3hu7kun7ag[23] = "static const gm_field ";
const uint8_t gm_snqm9b8h9b70a[120] = "arithmetic does not apply to arrays; use a `#vec [N]T` math vector (\302\2475.6.3), a `#simd [N]T` vector (\302\2475.6.2) or a loop";
const uint8_t gm_snrqbteh7n9bd[66] = "fail a test that runs longer than <seconds> (no\012limit by default)";
const uint8_t gm_so2blaqutst8g[53] = " was modified (`gamma cache clean --all` removes it)";
const uint8_t gm_so3hs9kj1bue6[21] = "unknown command `{}`";
const uint8_t gm_so3l0v9m39i7o[15] = "gm_fs_truncate";
const uint8_t gm_so4k586t2v79t[64] = "a `for` pattern must match every value (it must be irrefutable)";
const uint8_t gm_so5215183on4e[58] = "expected a union type or a module before `.` in a pattern";
const uint8_t gm_so6ip6bkb1lgs[9] = "break {}";
const uint8_t gm_so7l0chqarptu[13] = "{:.3} ms/run";
const uint8_t gm_so91sclkgrupd[33] = "((__int128)(((unsigned __int128)";
const uint8_t gm_so9n4mmd960c7[6] = "cache";
const uint8_t gm_soerl6rds8b9v[12] = "NetShutdown";
const uint8_t gm_sofs2uc27fjvk[3] = "%\012";
const uint8_t gm_sogehh8t1m7tn[6] = "false";
const uint8_t gm_soiotnern2p8m[19] = "converting to `{}`";
const uint8_t gm_soqhm2vmt2g9o[21] = "__atomic_exchange_n(";
const uint8_t gm_soqn25gc3d0ls[14] = "gm_wasi_drive";
const uint8_t gm_sosrg8fhr95t2[38] = ") gm_trap(\"integer overflow in `-`\", ";
const uint8_t gm_soukc8goe066i[6] = "-fPIC";
const uint8_t gm_sovfv4hr4sld4[17] = "gm_proc_write_in";
const uint8_t gm_sp1h1hg79ru76[38] = "`{}` lives until the end of its scope";
const uint8_t gm_sp1nce97af2b9[6] = "] || ";
const uint8_t gm_sp33bek143l58[23] = "compiler/parse/stmt.gm";
const uint8_t gm_sp525daee5j3r[85] = "unknown constraint `{}`; the constraints are Eq, Ord, Num, Int, Neg, Float and Plain";
const uint8_t gm_sp5b2hbslij9l[5] = "argp";
const uint8_t gm_sp6eh19rmca8b[36] = "`.norm` needs float elements, not `";
const uint8_t gm_sp6qfs9r7ep2n[75] = "construction of `{}` is not supported yet; a conversion takes one argument";
const uint8_t gm_spdft68ac8308[95] = "if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }";
const uint8_t gm_sped63ic8juob[17] = "\000\000\000\000\000\000\000\000<\000\000\000\000\000\000\000";
const uint8_t gm_spferj7kepn49[115] = "`[n]T` is an array type: an array value is written with its type, `[_]i32(1, 2)` (\302\2477.11), and a tuple as `(a, b)`";
const uint8_t gm_spffvcegd6os6[6] = "if (!";
const uint8_t gm_spl83f693c752[32] = "key with no value, expected `=`";
const uint8_t gm_splm7v4oko2tv[15] = "float constant";
const uint8_t gm_spp1c7975jken[5] = "| [`";
const uint8_t gm_spqf88ftq0gqk[29] = "bit builtin on a non-integer";
const uint8_t gm_spvjnpejqaf76[18] = "an intrinsic call";
const uint8_t gm_sq17986ir1cd2[41] = "    return n == 0 || memcmp(pa, pb, n * ";
const uint8_t gm_sq1oij2hrovtv[4] = "*%=";
const uint8_t gm_sq2g0rh16c4kh[16] = "\")\012    eprint(\"";
const uint8_t gm_sq5sjasaqn114[7] = "op_div";
const uint8_t gm_sq6kdq5587s3n[11] = " { struct ";
const uint8_t gm_sqalimuc6s35k[32] = "`; use `.addr()` for an address";
const uint8_t gm_sqcm7se7v2nvs[49] = ")` needs a union without payloads, but variant `";
const uint8_t gm_sqe4aodms9tfs[14] = "this operator";
const uint8_t gm_sqeqp2v8egtl4[93] = "count the lines of the package that the tests run; print\012a table, and with <file> write LCOV";
const uint8_t gm_sqi9n75mrenn7[5] = "\377\377\377\377";
const uint8_t gm_sqli1lbe2mk02[17] = "\000\000\000\000\000\000\000\000=\000\000\000\000\000\000\000";
const uint8_t gm_sqnu8fg3ikbeg[35] = "no match arm matched the scrutinee";
const uint8_t gm_sqp26euqk4rkd[16] = "slice too large";
const uint8_t gm_sqqh9r6c17rju[7] = "argn{}";
const uint8_t gm_sqrjr8qjf37v8[17] = " != UINTPTR_MAX;";
const uint8_t gm_sr389r8t7d0mf[11] = "__wasm32__";
const uint8_t gm_sr4magk9naq4q[23] = "compiler/repl/input.gm";
const uint8_t gm_sr9q8kbf253ia[45] = "; use a function pointer `^fn(\342\200\246)` (\302\24711.6)";
const uint8_t gm_src0qvon2jl38[28] = "cannot unpack the archive: ";
const uint8_t gm_srgp1m0q2ut8m[12] = "\012actual:   ";
const uint8_t gm_srio0gkdttkj2[61] = "a constraint applies to a type parameter of this declaration";
const uint8_t gm_srk226j8lb4rq[9] = "popcount";
const uint8_t gm_srmistf0k19ab[72] = "this continuation is no longer valid: its `with` has completed (\302\24714.2)";
const uint8_t gm_sroje34o1kfta[38] = "this `resume` ends the handler clause";
const uint8_t gm_srqr27qbutlf7[78] = "a compile-time argument must be a constant expression, but `{}` is a variable";
const uint8_t gm_srrq1irp1jfep[12] = "a hard link";
const uint8_t gm_srtkupsn5fo0o[123] = "a temporary array used as a slice is held until the end of its block (\302\24715.1); put it in a variable that lives long enough";
const uint8_t gm_ss2l6r6gldk9s[23] = "replace `{}` with `{}`";
const uint8_t gm_ss3cao14a6jed[13] = "process_file";
const uint8_t gm_ss3f61htk015v[7] = "is_nan";
const uint8_t gm_ss5q3rrcb010f[27] = "    GM_ST(uint32_t, loc + ";
const uint8_t gm_ss6ubqdvd2q7k[60] = "the complex operation `{}` is missing from the core library";
const uint8_t gm_ss738pdss75oa[9] = ", NULL, ";
const uint8_t gm_ss8v6rf5v54r7[9] = " column ";
const uint8_t gm_ss9sitl8h5uvg[18] = "` starts with `-`";
const uint8_t gm_ssa5e93p23oqi[90] = "this performs {}, so the type it is passed at cannot be inferred; write the type argument";
const uint8_t gm_ssd99k9slj99i[3] = "||";
const uint8_t gm_ssf8mqtfpipac[45] = "shifts cannot be chained without parentheses";
const uint8_t gm_ssfjmtpagd1pu[6] = ":list";
const uint8_t gm_sshv7blujc475[52] = "a function type cannot have compile-time parameters";
const uint8_t gm_ssi40jo2ghpq5[24] = "gm_unw.code = (uint8_t)";
const uint8_t gm_ssm3bl5los9iv[5] = "{}.c";
const uint8_t gm_ssmb6ukuk62fe[53] = "`#soa` needs a struct with fields, and `{}` has none";
const uint8_t gm_ssnku2e1q7u3k[13] = " samples of ";
const uint8_t gm_sspd8sv58p3ks[72] = "the result of this constant expression is too large (more than {} bits)";
const uint8_t gm_st02nolqdtiev[9] = "local {}";
const uint8_t gm_st49tt11e1avl[8] = "Add({})";
const uint8_t gm_st4t0ufodvddp[59] = "`<<\077` is not available on vectors: a lane cannot be `null`";
const uint8_t gm_st7j9rqo01n3i[17] = " operands, not `";
const uint8_t gm_st87fk7ojs4l8[6] = "array";
const uint8_t gm_stamom3b25ln8[14] = "gm_test_index";
const uint8_t gm_stb0ck7v8hutn[8] = "gm_fr{}";
const uint8_t gm_std7upetcqgjg[41] = "no enclosing loop or `with` is labeled `";
const uint8_t gm_stdvq0tcga0vp[8] = " after ";
const uint8_t gm_stfpae614bc1t[35] = "(({})(-9223372036854775807LL - 1))";
const uint8_t gm_sti61120eur38[59] = "` must be a table: `{ path = \"\342\200\246\" }` or `{ git = \"\342\200\246\" }`";
const uint8_t gm_stingg155o3k5[8] = "std/net";
const uint8_t gm_struubjgj1nj2[15] = "remove the `,`";
const uint8_t gm_stuutd7nfq21h[3] = "; ";
const uint8_t gm_stvspp709ordv[3] = "</";
const uint8_t gm_su0bfk11j19aa[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000O\000\000\000)\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_su22guvkav5b5[21] = "the operands of `in`";
const uint8_t gm_su2flp9mbv542[38] = "invalid unicode escape `\\u{{{}}}`: {}";
const uint8_t gm_su8bu9ae1ds2f[19] = "method not found: ";
const uint8_t gm_sua08g8f9l2h5[12] = " parameters";
const uint8_t gm_sudnegb3umvac[12] = "gm_format(&";
const uint8_t gm_suekibjsm2ees[34] = "this `match` is not exhaustive: `";
const uint8_t gm_sugv53k5dm8d7[24] = "(~(unsigned __int128)0)";
const uint8_t gm_sukjjfkvr8401[40] = "dependency `{}` has an unknown key `{}`";
const uint8_t gm_sun80ccb875r1[13] = ",\"severity\":";
const uint8_t gm_suntqnp3vfhar[16] = "pat-fields : {}";
const uint8_t gm_suornq817q1pp[40] = "invalid value `{}` for {} (expected {})";
const uint8_t gm_supnh5d0f56vu[8] = "bool {}";
const uint8_t gm_suq3bgh3anb87[58] = "`--tag`, `--branch`, `--rev` and `--version` need `--git`";
const uint8_t gm_svfdfjqemp6o4[19] = "gamma cannot check";
const uint8_t gm_svkl3ccbc9hel[11] = "'''{}{}'''";
const uint8_t gm_svmpuinrbii3k[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\024\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_svokmu75u5n0c[14] = " && _Alignof(";
const uint8_t gm_svr1ohtrtj17p[64] = "invalid memory access: address {:#x} is not aligned to {} bytes";
const uint8_t gm_svr6jc92gv992[16] = "gm_wasi_running";
const uint8_t gm_svrv9jmovu8v4[20] = "spread_generic_args";
const uint8_t gm_svsm5u96riots[18] = "this address of `";
const uint8_t gm_svt8aldhoetro[3] = " a";
const uint8_t gm_svubtddeidu1d[2] = "1";
uint8_t gm_c12218cktbl[102] __attribute__((aligned(1)));
uint8_t gm_c1k7p3505ef[13] __attribute__((aligned(1)));
uint8_t gm_c21nulmhcfa[12] __attribute__((aligned(1)));
uint8_t gm_c3c901bgg39[96] __attribute__((aligned(8)));
uint8_t gm_c3dq62rrli9[16] __attribute__((aligned(8)));
uint8_t gm_c3t8vp50qki[13] __attribute__((aligned(1)));
uint8_t gm_c42qjm5ks8a[14] __attribute__((aligned(1)));
uint8_t gm_c4v5g7vo02e[9] __attribute__((aligned(1)));
uint8_t gm_c6d151di7ae[23] __attribute__((aligned(1)));
uint8_t gm_c82til0j71d[13] __attribute__((aligned(1)));
uint8_t gm_c8fdn45dfbi[16] __attribute__((aligned(1)));
uint8_t gm_c8o6bldrs15[43] __attribute__((aligned(1)));
uint8_t gm_capnegefe86[16] __attribute__((aligned(8)));
uint8_t gm_cb2tp7m3jgm[9] __attribute__((aligned(1)));
uint8_t gm_cb3qqtegs3b[10] __attribute__((aligned(1)));
uint8_t gm_cbata63g83q[3] __attribute__((aligned(1)));
uint8_t gm_cbci9h62d87[8] __attribute__((aligned(1)));
uint8_t gm_cbcngu2pqs7[8] __attribute__((aligned(8)));
uint8_t gm_cbr6nfilhfs[8] __attribute__((aligned(1)));
uint8_t gm_cbu54rqt96g[4832] __attribute__((aligned(8)));
uint8_t gm_cc763fsv023[15] __attribute__((aligned(1)));
uint8_t gm_cd231ebi1kg[16] __attribute__((aligned(8)));
uint8_t gm_cek1be2qo54[15] __attribute__((aligned(1)));
uint8_t gm_cel0duaak22[61] __attribute__((aligned(1)));
uint8_t gm_cf00a8vdcu4[13] __attribute__((aligned(1)));
uint8_t gm_cf4gomohuig[12] __attribute__((aligned(1)));
uint8_t gm_cfc3i2mbui9[16] __attribute__((aligned(8)));
uint8_t gm_cgfibmbf962[3] __attribute__((aligned(1)));
uint8_t gm_ch92utta9bv[1] __attribute__((aligned(1)));
uint8_t gm_chpv6gcip6t[6] __attribute__((aligned(1)));
uint8_t gm_ciadv3fllt2[14] __attribute__((aligned(1)));
uint8_t gm_cj54d91346t[2] __attribute__((aligned(1)));
uint8_t gm_cko3dv3bfgu[15] __attribute__((aligned(1)));
uint8_t gm_cm8j41tujfe[67] __attribute__((aligned(1)));
uint8_t gm_cmcv9v31fv5[10] __attribute__((aligned(1)));
uint8_t gm_cogdbob2e97[5] __attribute__((aligned(1)));
uint8_t gm_couns2p4unr[69] __attribute__((aligned(1)));
uint8_t gm_cp0okt01b9q[192] __attribute__((aligned(8)));
uint8_t gm_cp3vdr1lhrm[3] __attribute__((aligned(1)));
uint8_t gm_cpi9n8ms3ie[4] __attribute__((aligned(1)));
uint8_t gm_cq425m2mlf6[10] __attribute__((aligned(1)));
uint8_t gm_cqj3uiou0f0[12] __attribute__((aligned(1)));
uint8_t gm_cqup1m14s8b[17] __attribute__((aligned(1)));
uint8_t gm_crfi87obk4n[16] __attribute__((aligned(1)));
uint8_t gm_crr0lcslqi7[7] __attribute__((aligned(1)));
uint8_t gm_ct04sva93t7[1006] __attribute__((aligned(1)));
uint8_t gm_cu6c7r2uv4n[14] __attribute__((aligned(1)));
uint8_t gm_cukp3dhhpod[12] __attribute__((aligned(1)));
uint8_t gm_cv0gcjmll71[20] __attribute__((aligned(1)));
uint8_t gm_cvde9thke2j[13] __attribute__((aligned(1)));
uint8_t gm_cvg0qa54vnl[11] __attribute__((aligned(1)));
uint8_t gm_k1fl2q13fn0[40] __attribute__((aligned(8)));
uint8_t gm_k7898pnkupv[40] __attribute__((aligned(8)));
uint8_t gm_ka2mtmecqk3[40] __attribute__((aligned(8)));
uint8_t gm_kct6b70ce3d[40] __attribute__((aligned(8)));
uint8_t gm_kd6k1g0uehn[40] __attribute__((aligned(8)));
uint8_t gm_keudd4gct2b[40] __attribute__((aligned(8)));
uint8_t gm_kh31phsjqu4[40] __attribute__((aligned(8)));
uint8_t gm_ko0rgal5bm2[40] __attribute__((aligned(8)));
uint8_t gm_kr1kqehd6fp[40] __attribute__((aligned(8)));
uint8_t gm_ksccp6j96fm[40] __attribute__((aligned(8)));
uint8_t gm_kshbjn6sg2q[40] __attribute__((aligned(8)));
uint8_t gm_ksoekcu07mu[40] __attribute__((aligned(8)));
uint8_t gm_kt1fbj7k4jp[40] __attribute__((aligned(8)));
uint8_t gm_kukue15f4h3[40] __attribute__((aligned(8)));
void *const gm_hd365sua5c99[] = {(void *)gm_dt365sua5c99_0};
void *const gm_hdffkeesmui4[] = {(void *)gm_dtffkeesmui4_0, (void *)gm_dtffkeesmui4_1, (void *)gm_dtffkeesmui4_2, (void *)gm_dtffkeesmui4_3, (void *)gm_dtffkeesmui4_4, (void *)gm_dtffkeesmui4_5, (void *)gm_dtffkeesmui4_6, (void *)gm_dtffkeesmui4_7, (void *)gm_dtffkeesmui4_8, (void *)gm_dtffkeesmui4_9, (void *)gm_dtffkeesmui4_10};
void *const gm_hllt7e2ogusq[] = {(void *)gm_f_for_body_86gje82};
void *const gm_hlusqsd00u9k[] = {(void *)gm_f_for_body_ukps8ob};
