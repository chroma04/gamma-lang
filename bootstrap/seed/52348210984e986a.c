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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_b16_8 f1; } gm_t6igi6646q59o;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; } gm_t6ok5tvcjc200;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint8_t v0; gm_tnpifv20irnph v1; gm_b16_8 v2; gm_b16_8 v3; gm_b16_8 v4; } f1; } gm_t8vuock3jnv59;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { uint64_t f0; uint64_t f1; gm_th2lqbo77pvsc f2; } gm_tvin65ttqbr1d;
typedef struct { gm_t8vuock3jnv59 f0; uint8_t f1; uint8_t pe[7]; } gm_t3ph3kbiqt1ah;
_Static_assert(sizeof(gm_t6igi6646q59o) == 56 && _Alignof(gm_t6igi6646q59o) == 8 && offsetof(gm_t6igi6646q59o, f0) == 0 && offsetof(gm_t6igi6646q59o, f1) == 40, "layout of `SplitSignature`");
_Static_assert(sizeof(gm_t6ok5tvcjc200) == 40 && _Alignof(gm_t6ok5tvcjc200) == 8 && offsetof(gm_t6ok5tvcjc200, f0) == 0 && offsetof(gm_t6ok5tvcjc200, f1) == 16, "layout of `Key`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_t6ok5tvcjc200 f1; } gm_t8scu1osgcra5;
_Static_assert(sizeof(gm_t8vuock3jnv59) == 24 && _Alignof(gm_t8vuock3jnv59) == 8 && offsetof(gm_t8vuock3jnv59, f0) == 0 && offsetof(gm_t8vuock3jnv59, f1) == 8, "layout of `Value`");
typedef struct { gm_tvin65ttqbr1d f0; uint8_t f1; uint8_t pe[7]; } gm_t9nnki79sf2e1;
typedef struct { gm_t6igi6646q59o f0; uint8_t f1; uint8_t pe[7]; } gm_tec7dnv8fjfj8;
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { gm_t8vuock3jnv59 a[1]; } gm_tq47bkpmd2b9a;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tvin65ttqbr1d) == 40 && _Alignof(gm_tvin65ttqbr1d) == 8 && offsetof(gm_tvin65ttqbr1d, f0) == 0 && offsetof(gm_tvin65ttqbr1d, f1) == 8 && offsetof(gm_tvin65ttqbr1d, f2) == 16, "layout of `CallContext`");
_Static_assert(sizeof(gm_t3ph3kbiqt1ah) == 32 && _Alignof(gm_t3ph3kbiqt1ah) == 8 && offsetof(gm_t3ph3kbiqt1ah, f0) == 0 && offsetof(gm_t3ph3kbiqt1ah, f1) == 24, "layout of `?Value`");
_Static_assert(sizeof(gm_t8scu1osgcra5) == 56 && _Alignof(gm_t8scu1osgcra5) == 8 && offsetof(gm_t8scu1osgcra5, f0) == 0 && offsetof(gm_t8scu1osgcra5, f1) == 16, "layout of `HoverResult`");
_Static_assert(sizeof(gm_t9nnki79sf2e1) == 48 && _Alignof(gm_t9nnki79sf2e1) == 8 && offsetof(gm_t9nnki79sf2e1, f0) == 0 && offsetof(gm_t9nnki79sf2e1, f1) == 40, "layout of `?CallContext`");
_Static_assert(sizeof(gm_tec7dnv8fjfj8) == 64 && _Alignof(gm_tec7dnv8fjfj8) == 8 && offsetof(gm_tec7dnv8fjfj8, f0) == 0 && offsetof(gm_tec7dnv8fjfj8, f1) == 56, "layout of `?SplitSignature`");
typedef struct { gm_t8scu1osgcra5 f0; uint8_t f1; uint8_t pe[7]; } gm_teijtv56moffm;
_Static_assert(sizeof(gm_tq47bkpmd2b9a) == 24 && _Alignof(gm_tq47bkpmd2b9a) == 8 && offsetof(gm_tq47bkpmd2b9a, a) == 0, "layout of `[1]Value`");
_Static_assert(sizeof(gm_teijtv56moffm) == 64 && _Alignof(gm_teijtv56moffm) == 8 && offsetof(gm_teijtv56moffm, f0) == 0 && offsetof(gm_teijtv56moffm, f1) == 56, "layout of `?HoverResult`");
struct gm_fr_array_k32ana3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_call_context_r6lbirq {
    gm_frame h;
    gm_t9nnki79sf2e1 ret;
};
struct gm_fr_completion_kind_flii2l4 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_definition_kaqo3c6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_document_symbols_0nlnk2i {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_oucukrb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_full_edit_82h286k {
    gm_frame h;
    gm_tin97tt4ud0s1 l4; /* e */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hover_ub02g3b {
    gm_frame h;
    gm_t8scu1osgcra5 l3; /* h */
    gm_tin97tt4ud0s1 l4; /* contents */
    gm_tin97tt4ud0s1 l5; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_int_95caidg {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_char_boundary_h62fhaa {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_item_json_ml3fbup {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* o */
    gm_tin97tt4ud0s1 l4; /* m */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_line_index_2necgtf {
    gm_frame h;
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_list_0202sdh_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_location_i7gnofg {
    gm_frame h;
    gm_tin97tt4ud0s1 l3; /* o */
    gm_t3ph3kbiqt1ah ret;
};
struct gm_fr_markdown_posoa59 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_member_context_p58kq5l {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_members_8m2g0sl {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* out */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_members_at_do67lp0 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_names_ms1jmv4 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l4; /* seen */
    gm_trqfvmjtt4sij l5; /* out */
    gm_trqfvmjtt4sij l6; /* locals */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_obj_aoa7fl7 {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_offset_at_73sli4j {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_references_pd7nt1t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* spans */
    gm_trqfvmjtt4sij l6; /* out */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_signature_help_16c2a8f {
    gm_frame h;
    gm_tvin65ttqbr1d l4; /* cc */
    gm_t8scu1osgcra5 l6; /* h */
    gm_t6igi6646q59o l8; /* sp */
    gm_trqfvmjtt4sij l9; /* params */
    gm_trqfvmjtt4sij l13; /* kept */
    gm_trqfvmjtt4sij l24; /* ps */
    gm_tin97tt4ud0s1 l28; /* po */
    gm_tin97tt4ud0s1 l29; /* sig */
    gm_tin97tt4ud0s1 l31; /* m */
    gm_tin97tt4ud0s1 l32; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_source_text_7ru912k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_split_signature_3r31d7h {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* params */
    gm_tec7dnv8fjfj8 ret;
};
struct gm_fr_stable_sort_by_qutfkbd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_symbol_json_b065ihj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* children */
    gm_tin97tt4ud0s1 l6; /* o */
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_symbol_kind_g6njvq1 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_text_ls49dp6 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_uri_of_t6r79gp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_value_pkuqal3 {
    gm_frame h;
    gm_t8vuock3jnv59 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_array_k32ana3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_call_context_r6lbirq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) int64_t gm_f_completion_kind_flii2l4(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_definition_at_9jj9uuu(void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_definition_kaqo3c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_document_symbols_0nlnk2i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_qmfcuqk_2(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fence_hq2pe32(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9ug250p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gej351p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_full_edit_82h286k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_get_7o29gjt(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hover_at_19okc4g(void *gm_out, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hover_ub02g3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_int_95caidg(void *gm_out, void *env, gm_node *cx, gm_node *inst, int64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_item_json_ml3fbup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_line_starts_oja6lfl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_lines_0mpphhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locals_at_2n5h0d6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_location_i7gnofg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_markdown_posoa59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_member_context_p58kq5l(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tc70a9dbbmgkg gm_f_member_context_p58kq5l_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_members_8m2g0sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
inline __attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_merge_sort_qr74lj4(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_names_ms1jmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_obj_aoa7fl7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) uint64_t gm_f_offset_hdhse2v(void * a0, uint32_t a1, uint32_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_open_path_6gmq4nk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_outline_of_01eujq2(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_path_to_uri_as8bikc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_put_91stqpj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_range_demc0bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_range_json_hlsolbo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_remove_qvip58u(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signature_help_16c2a8f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_signature_3r31d7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_qutfkbd(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_symbol_json_b065ihj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) int64_t gm_f_symbol_kind_g6njvq1(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_text_ls49dp6(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_uses_of_nalefva(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_value_pkuqal3(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8qoqt62(void * a0, const void * a1);
extern uint8_t gm_cro0h2panct[24] __attribute__((aligned(8)));
extern uint8_t gm_k7bjnihrifk[40] __attribute__((aligned(8)));
extern uint8_t gm_k8nmk7je8im[40] __attribute__((aligned(8)));
extern uint8_t gm_klfj89orh68[40] __attribute__((aligned(8)));
extern uint8_t gm_kn81j57eee2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1b629p19p3qq[4];
extern const uint8_t gm_s22mk3qhbqci2[11];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s54psrmgl6ti7[9];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s7b8bvi5kqaqt[7];
extern const uint8_t gm_s7j2u3ofu66j8[5];
extern const uint8_t gm_s7unnudd0kkri[14];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_sb6smklmjl60o[7];
extern const uint8_t gm_sbo52miqad506[4];
extern const uint8_t gm_schvua5hkahp9[16];
extern const uint8_t gm_sd6a57g2jmtks[11];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_si1j2hanh22ll[16];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sm06nsg0msdgd[9];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srcjeqrqq5omjoo[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const char gm_srcvksed7nkdegg[];
extern const uint8_t gm_srdnkonbfvqdq[9];
extern const uint8_t gm_srplcc0p49iui[9];
extern const uint8_t gm_sttp5u65omp7t[15];
extern const uint8_t gm_su137hja0ebcd[6];
extern const uint8_t gm_sum9vj65rg4jb[4];
extern const uint8_t gm_svubtddeidu1d[2];
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) goto mn8;
        t6 = ((uint8_t)0);
        goto me7;
mn8:;
        t6 = ((uint8_t)1);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 153, 12);
me7:;
        uint8_t t10 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    gm_tf5srh4mp4b3i t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 82, 50);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_oucukrb F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, ((((uint8_t *)t1) + 64) + 4));
    void * t3 = gm_l1;
    uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 64) + 4));
    uint8_t t5 = (t2 > t4);
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
        gm_node *pn3 = gm_find(cx, 587034433);
        if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
        gm_t3mf58gv9kf3n t4;
        t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0, t2);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32, ((uint64_t)11400714819323198485ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alnum_n81ervr F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alpha_12215ga(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_digit_2hoggnj(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_alpha_12215ga F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_lower_4ebokoh(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_char_boundary_h62fhaa(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_char_boundary_h62fhaa F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 >= t4);
        if (!t5) goto ie2;
        {
            uint64_t t7 = gm_l1;
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t9 = ((t7 == t8));
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t11 = gm_l1;
        uint64_t t12 = (uintptr_t)t11;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_srcjeqrqq5omjoo, 34, 12);
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t12 * 1));
        uint8_t t15 = (uint8_t)(t14 & ((uint8_t)192ULL));
        uint8_t t16 = (!(t15 == ((uint8_t)128ULL)));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_digit_2hoggnj F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)48ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)57ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_lower_4ebokoh F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)97ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)122ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = 0;
    uint8_t t4 = 0;
    uint8_t t5 = 0;
    uint8_t t6 = gm_l0;
    uint8_t t7 = ((t6 == ((uint8_t)32ULL)));
    if (!t7) {
        uint8_t t8 = gm_l0;
        uint8_t t9 = ((t8 == ((uint8_t)9ULL)));
        t5 = t9;
    } else t5 = 1;
    if (!t5) {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)10ULL)));
        t4 = t11;
    } else t4 = 1;
    if (!t4) {
        uint8_t t12 = gm_l0;
        uint8_t t13 = ((t12 == ((uint8_t)13ULL)));
        t3 = t13;
    } else t3 = 1;
    if (!t3) {
        uint8_t t14 = gm_l0;
        uint8_t t15 = ((t14 == ((uint8_t)11ULL)));
        t2 = t15;
    } else t2 = 1;
    if (!t2) {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        t1 = t17;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_iekrftp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 >= ((uint8_t)65ULL));
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = (t4 <= ((uint8_t)90ULL));
        t1 = t5;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_line_index_2necgtf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_line_index_2necgtf F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_f_line_starts_oja6lfl((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 0, t1);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6j6smj7qoctu *)gm_out) + 16, t5);
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
inline __attribute__((aligned(4))) void gm_f_members_at_do67lp0(void *gm_out, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_members_at_do67lp0 F;
    void * gm_l0; /* ix */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 272);
    uint32_t t3 = gm_l1;
    uint32_t t4 = gm_l2;
    uint32_t t5 = gm_l3;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    gm_f_get_7o29gjt((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t2, GM_CONV(gm_cseahvg8v7p0vs, t6));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct gm_fr_offset_at_73sli4j F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* line */
    uint32_t gm_l3; /* character */
    gm_t6j6smj7qoctu gm_l4; /* ix */
    GM_STACK_CHECK(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        gm_f_lines_0mpphhg((void *)&gm_l4, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&gm_l4);
        uint32_t t6 = gm_l2;
        uint32_t t7 = gm_l3;
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t8) + 8));
        uint64_t t10 = gm_f_offset_hdhse2v(t5, t6, t7, t9);
        uint32_t t11 = (uint32_t)t10;
        uint8_t t12 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_source_text_7ru912k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_text_7ru912k F;
    void * gm_l0; /* a */
    uint32_t gm_l1; /* file */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_file_o15fqku(t2, t3);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 16));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_qutfkbd(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stable_sort_by_qutfkbd F;
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
        gm_f_merge_sort_qr74lj4(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_179crak(t1);
    gm_b16_8 t3 = gm_f_trim_end_6sjv6rg(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_uri_of_t6r79gp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_uri_of_t6r79gp F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* u */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 0);
        gm_b16_8 t5 = gm_l1;
        gm_th2lqbo77pvsc t6;
        gm_f_get_e3joqcc((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            gm_b16_8 t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t12 = gm_l1;
        gm_b16_8 t13 = gm_f_path_to_uri_as8bikc((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
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
__attribute__((aligned(4))) void gm_f_call_context_r6lbirq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_call_context_r6lbirq F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* off_in */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* off */
    uint64_t gm_l4; /* depth */
    uint64_t gm_l5; /* commas */
    uint64_t gm_l6; /* i */
    gm_tf5srh4mp4b3i gm_l7; /* last_comma */
    uint64_t gm_l8; /* open */
    uint8_t gm_l9; /* c */
    uint64_t gm_l10; /* j */
    uint64_t gm_l11; /* end */
    uint64_t gm_l12; /* start */
    uint64_t gm_l13; /* c */
    gm_b16_8 gm_l14; /* seg */
    gm_th2lqbo77pvsc gm_l15; /* named */
    gm_t6j6smj7qoctu gm_l16; /* sp */
    gm_b16_8 gm_l17; /* n */
    uint8_t gm_l18; /* ok */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint8_t gm_l21; /* ch */
    GM_STACK_CHECK(&F, 1582u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t5 = (t3 < t4 ? t3 : t4);
        gm_l3 = t5;
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        uint64_t t6 = gm_l3;
        gm_l6 = t6;
    }
    {
        gm_tf5srh4mp4b3i t7;
        memset(&t7, 0, sizeof t7);
        gm_l7 = t7;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb8;
            {
                uint64_t t12 = gm_l6;
                uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
                if (!t13) goto ie11;
                {
                    gm_t9nnki79sf2e1 t15;
                    memset(&t15, 0, sizeof t15);
                    uint8_t t16 = 0;
                    F.ret = t15;
                    goto gm_fret;
                }
                goto ix11;
ie11:;
ix11:;
            }
            {
                uint64_t t17 = gm_l6;
                uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 242, 9);
                uint64_t t20 = r18;
                gm_l6 = t20;
            }
            {
                uint64_t t21 = gm_l6;
                uint64_t t22 = (uintptr_t)t21;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_srcvksed7nkdegg, 243, 14);
                uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t22 * 1));
                gm_l9 = t24;
            }
            {
                uint8_t t26 = 0;
                uint8_t t27 = 0;
                uint8_t t28 = gm_l9;
                uint8_t t29 = ((t28 == ((uint8_t)41ULL)));
                if (!t29) {
                    uint8_t t30 = gm_l9;
                    uint8_t t31 = ((t30 == ((uint8_t)93ULL)));
                    t27 = t31;
                } else t27 = 1;
                if (!t27) {
                    uint8_t t32 = gm_l9;
                    uint8_t t33 = ((t32 == ((uint8_t)125ULL)));
                    t26 = t33;
                } else t26 = 1;
                if (!t26) goto ie25;
                {
                    uint64_t t35 = gm_l4;
                    uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                    if (o37) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 245, 13);
                    uint64_t t38 = r36;
                    gm_l4 = t38;
                }
                goto ix25;
ie25:;
                uint8_t t40 = gm_l9;
                uint8_t t41 = ((t40 == ((uint8_t)40ULL)));
                if (!t41) goto ie39;
                uint64_t t42 = gm_l4;
                uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
                if (!t43) goto ie39;
                {
                    uint64_t t45 = gm_l6;
                    gm_l8 = t45;
                }
                {
                    goto lb8;
                }
                goto ix39;
ie39:;
                uint8_t t47 = gm_l9;
                uint8_t t48 = ((t47 == ((uint8_t)40ULL)));
                if (!t48) goto ie46;
                {
                    uint64_t t50 = gm_l4;
                    uint64_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint64_t)1ULL), &r51);
                    if (o52) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 250, 13);
                    uint64_t t53 = r51;
                    gm_l4 = t53;
                }
                goto ix46;
ie46:;
                uint8_t t55 = 0;
                uint8_t t56 = gm_l9;
                uint8_t t57 = ((t56 == ((uint8_t)91ULL)));
                if (!t57) {
                    uint8_t t58 = gm_l9;
                    uint8_t t59 = ((t58 == ((uint8_t)123ULL)));
                    t55 = t59;
                } else t55 = 1;
                if (!t55) goto ie54;
                uint64_t t60 = gm_l4;
                uint8_t t61 = ((t60 == ((uint64_t)0ULL)));
                if (!t61) goto ie54;
                {
                    gm_t9nnki79sf2e1 t63;
                    memset(&t63, 0, sizeof t63);
                    uint8_t t64 = 0;
                    F.ret = t63;
                    goto gm_fret;
                }
                goto ix54;
ie54:;
                uint8_t t66 = 0;
                uint8_t t67 = gm_l9;
                uint8_t t68 = ((t67 == ((uint8_t)91ULL)));
                if (!t68) {
                    uint8_t t69 = gm_l9;
                    uint8_t t70 = ((t69 == ((uint8_t)123ULL)));
                    t66 = t70;
                } else t66 = 1;
                if (!t66) goto ie65;
                {
                    uint64_t t72 = gm_l4;
                    uint64_t r73; bool o74 = __builtin_sub_overflow(t72, ((uint64_t)1ULL), &r73);
                    if (o74) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 254, 13);
                    uint64_t t75 = r73;
                    gm_l4 = t75;
                }
                goto ix65;
ie65:;
                uint8_t t77 = gm_l9;
                uint8_t t78 = ((t77 == ((uint8_t)44ULL)));
                if (!t78) goto ie76;
                uint64_t t79 = gm_l4;
                uint8_t t80 = ((t79 == ((uint64_t)0ULL)));
                if (!t80) goto ie76;
                {
                    uint64_t t82 = gm_l5;
                    uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                    if (o84) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 256, 13);
                    uint64_t t85 = r83;
                    gm_l5 = t85;
                }
                {
                    uint8_t t87;
                    if (((((uint8_t *)&gm_l7))[8] != 0)) goto mn89;
                    t87 = ((uint8_t)1);
                    goto me88;
mn89:;
                    t87 = ((uint8_t)0);
                    goto me88;
mn90:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 257, 16);
me88:;
                    if (!t87) goto ie86;
                    {
                        uint64_t t92 = gm_l6;
                        gm_tf5srh4mp4b3i t93;
                        GM_ST(uint8_t, (uint8_t *)&t93 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t93 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t93 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t93), t92);
                        ((uint8_t *)&t93)[8] = 1;
                        gm_l7 = t93;
                    }
                    goto ix86;
ie86:;
ix86:;
                }
                goto ix76;
ie76:;
                uint8_t t95 = gm_l9;
                uint8_t t96 = ((t95 == ((uint8_t)34ULL)));
                if (!t96) goto ie94;
                {
                    uint64_t t98 = gm_l6;
                    gm_l10 = t98;
                }
                {
                    for (;;) {
                        uint64_t t100 = gm_l10;
                        uint8_t t101 = (t100 > ((uint64_t)0ULL));
                        if (!t101) goto lb99;
                        uint64_t t102 = gm_l10;
                        uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)1ULL), &r103);
                        if (o104) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 261, 31);
                        uint64_t t105 = r103;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t106 >= (uintptr_t)(t107)) gm_trap_index(t106, t107, gm_srcvksed7nkdegg, 261, 29);
                        uint8_t t108 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t106 * 1));
                        uint8_t t109 = (!(t108 == ((uint8_t)10ULL)));
                        if (!t109) goto lb99;
                        {
                            uint64_t t112 = gm_l10;
                            uint64_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint64_t)1ULL), &r113);
                            if (o114) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 262, 17);
                            uint64_t t115 = r113;
                            gm_l10 = t115;
                        }
                        {
                            uint64_t t117 = gm_l10;
                            uint64_t t118 = (uintptr_t)t117;
                            uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcvksed7nkdegg, 263, 20);
                            uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t118 * 1));
                            uint8_t t121 = ((t120 == ((uint8_t)34ULL)));
                            if (!t121) goto ie116;
                            uint8_t t122 = 0;
                            uint64_t t123 = gm_l10;
                            uint8_t t124 = ((t123 == ((uint64_t)0ULL)));
                            if (!t124) {
                                uint64_t t125 = gm_l10;
                                uint64_t r126; bool o127 = __builtin_sub_overflow(t125, ((uint64_t)1ULL), &r126);
                                if (o127) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 263, 49);
                                uint64_t t128 = r126;
                                uint64_t t129 = (uintptr_t)t128;
                                uint64_t t130 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                if (t129 >= (uintptr_t)(t130)) gm_trap_index(t129, t130, gm_srcvksed7nkdegg, 263, 47);
                                uint8_t t131 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t129 * 1));
                                uint8_t t132 = (!(t131 == ((uint8_t)92ULL)));
                                t122 = t132;
                            } else t122 = 1;
                            if (!t122) goto ie116;
                            {
                                goto lb99;
                            }
                            goto ix116;
ie116:;
ix116:;
                        }
lc99:;
                    }
lb99:;
                }
                {
                    uint64_t t134 = gm_l10;
                    gm_l6 = t134;
                }
                goto ix94;
ie94:;
ix94:;
ix76:;
ix65:;
ix54:;
ix46:;
ix39:;
ix25:;
            }
lc8:;
        }
lb8:;
    }
    {
        uint64_t t135 = gm_l8;
        gm_l11 = t135;
    }
    {
        for (;;) {
            uint64_t t137 = gm_l11;
            uint8_t t138 = (t137 > ((uint64_t)0ULL));
            if (!t138) goto lb136;
            uint64_t t139 = gm_l11;
            uint64_t r140; bool o141 = __builtin_sub_overflow(t139, ((uint64_t)1ULL), &r140);
            if (o141) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 40);
            uint64_t t142 = r140;
            uint64_t t143 = (uintptr_t)t142;
            uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_srcvksed7nkdegg, 269, 38);
            uint8_t t145 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t143 * 1));
            uint8_t t146 = gm_f_is_space_uen38t5(t145);
            if (!t146) goto lb136;
            uint64_t t147 = gm_l11;
            uint64_t r148; bool o149 = __builtin_sub_overflow(t147, ((uint64_t)1ULL), &r148);
            if (o149) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 56);
            uint64_t t150 = r148;
            uint64_t t151 = (uintptr_t)t150;
            uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t151 >= (uintptr_t)(t152)) gm_trap_index(t151, t152, gm_srcvksed7nkdegg, 269, 54);
            uint8_t t153 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t151 * 1));
            uint8_t t154 = (!(t153 == ((uint8_t)10ULL)));
            if (!t154) goto lb136;
            {
                uint64_t t157 = gm_l11;
                uint64_t r158; bool o159 = __builtin_sub_overflow(t157, ((uint64_t)1ULL), &r158);
                if (o159) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 269, 79);
                uint64_t t160 = r158;
                gm_l11 = t160;
            }
lc136:;
        }
lb136:;
    }
    {
        uint8_t t162 = 0;
        uint64_t t163 = gm_l11;
        uint8_t t164 = ((t163 == ((uint64_t)0ULL)));
        if (!t164) {
            uint64_t t165 = gm_l11;
            uint64_t r166; bool o167 = __builtin_sub_overflow(t165, ((uint64_t)1ULL), &r166);
            if (o167) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 270, 47);
            uint64_t t168 = r166;
            uint64_t t169 = (uintptr_t)t168;
            uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t169 >= (uintptr_t)(t170)) gm_trap_index(t169, t170, gm_srcvksed7nkdegg, 270, 45);
            uint8_t t171 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t169 * 1));
            uint8_t t172 = gm_f_is_ident_cont_317fpdb(t171);
            uint8_t t173 = (uint8_t)!t172;
            t162 = t173;
        } else t162 = 1;
        if (!t162) goto ie161;
        {
            gm_t9nnki79sf2e1 t175;
            memset(&t175, 0, sizeof t175);
            uint8_t t176 = 0;
            F.ret = t175;
            goto gm_fret;
        }
        goto ix161;
ie161:;
ix161:;
    }
    {
        uint64_t t177 = gm_l8;
        uint64_t r178; bool o179 = __builtin_add_overflow(t177, ((uint64_t)1ULL), &r178);
        if (o179) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 272, 14);
        uint64_t t180 = r178;
        gm_l12 = t180;
    }
    {
        if (!((((uint8_t *)&gm_l7))[8] != 0)) goto ie181;
        uint64_t t182 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l13 = t182;
        {
            uint64_t t184 = gm_l13;
            uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
            if (o186) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 273, 34);
            uint64_t t187 = r185;
            gm_l12 = t187;
        }
        goto ix181;
ie181:;
ix181:;
    }
    {
        gm_b16_8 t188;
        GM_ST(void *, (uint8_t *)&t188, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(0));
        gm_l14 = t188;
    }
    {
        uint64_t t190 = gm_l12;
        uint64_t t191 = gm_l3;
        uint8_t t192 = (t190 <= t191);
        if (!t192) goto ie189;
        gm_b16_8 t193 = gm_l0;
        uint64_t t194 = gm_l12;
        uint8_t t195 = gm_f_is_char_boundary_h62fhaa(t193, t194);
        if (!t195) goto ie189;
        gm_b16_8 t196 = gm_l0;
        uint64_t t197 = gm_l3;
        uint8_t t198 = gm_f_is_char_boundary_h62fhaa(t196, t197);
        if (!t198) goto ie189;
        {
            uint64_t t200 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t201 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t202 = gm_l12;
            uint64_t t203 = (uintptr_t)t202;
            uint64_t t204 = gm_l3;
            uint64_t t205 = (uintptr_t)t204;
            if (t203 > t205 || t205 > (uintptr_t)t200) gm_trap_slice(t203, t205, t200, gm_srcvksed7nkdegg, 275, 104);
            gm_b16_8 t206;
            GM_ST(void *, (uint8_t *)&t206, (void *)(((uint8_t *)t201 + t203 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(t205 - t203));
            gm_b16_8 t207 = gm_f_trim_start_179crak(t206);
            gm_l14 = t207;
        }
        goto ix189;
ie189:;
ix189:;
    }
    {
        gm_th2lqbo77pvsc t208;
        memset(&t208, 0, sizeof t208);
        gm_l15 = t208;
    }
    {
        gm_b16_8 t210 = gm_l14;
        gm_b16_8 t211;
        GM_ST(void *, (uint8_t *)&t211, (void *)(gm_s39mljh669qgl));
        GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(1));
        gm_ttu09lij36f8c t212;
        gm_f_split_once_mqcu1ce((void *)&t212, t210, t211);
        gm_ttu09lij36f8c t213 = t212;
        if (!((((uint8_t *)&t213))[32] != 0)) goto ie209;
        gm_t6j6smj7qoctu t214 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t213));
        gm_l16 = t214;
        {
            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
            gm_b16_8 t217 = gm_f_trim_fdrfe6i(t216);
            gm_l17 = t217;
        }
        {
            uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t219 = (t218 > ((uint64_t)0ULL));
            gm_l18 = t219;
        }
        {
            {
                gm_b16_8 t221 = gm_l17;
                gm_l19 = t221;
            }
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t223 = gm_l20;
                    uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                    uint8_t t225 = (t223 < t224);
                    if (!t225) goto lb222;
                    {
                        uint64_t t228 = gm_l20;
                        uint64_t t229 = (uintptr_t)t228;
                        uint64_t t230 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                        uint8_t t231 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t229 * 1));
                        uint8_t t232 = t231;
                        uint8_t t235 = GM_LD(uint8_t, ((uint8_t *)&t232));
                        gm_l21 = t235;
                        {
                            uint8_t t238 = gm_l21;
                            uint8_t t239 = gm_f_is_ident_cont_317fpdb(t238);
                            uint8_t t240 = (uint8_t)!t239;
                            if (!t240) goto ie237;
                            {
                                gm_l18 = ((uint8_t)0);
                            }
                            goto ix237;
ie237:;
ix237:;
                        }
                        goto me233;
mn234:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 280, 9);
me233:;
                    }
lc222:;
                    {
                        uint64_t t243 = gm_l20;
                        uint64_t r244; bool o245 = __builtin_add_overflow(t243, ((uint64_t)1ULL), &r244);
                        uint64_t t246 = r244;
                        gm_l20 = t246;
                    }
                }
lb222:;
            }
        }
        {
            uint8_t t248 = gm_l18;
            if (!t248) goto ie247;
            {
                gm_b16_8 t250 = gm_l17;
                gm_th2lqbo77pvsc t251;
                GM_ST(uint8_t, (uint8_t *)&t251 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t251 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t251 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t251), t250);
                ((uint8_t *)&t251)[16] = 1;
                gm_l15 = t251;
            }
            goto ix247;
ie247:;
ix247:;
        }
        goto ix209;
ie209:;
ix209:;
    }
    {
        gm_tvin65ttqbr1d t252;
        uint64_t t253 = gm_l11;
        GM_ST(uint64_t, (uint8_t *)&t252 + 0, t253);
        uint64_t t254 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&t252 + 8, t254);
        memcpy((uint8_t *)&t252 + 16, ((uint8_t *)&gm_l15), 24);
        gm_t9nnki79sf2e1 t255;
        GM_ST(uint8_t, (uint8_t *)&t255 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t255 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t255 + 44, 0);
        GM_ST(gm_tvin65ttqbr1d, ((uint8_t *)&t255), t252);
        ((uint8_t *)&t255)[40] = 1;
        uint8_t t256 = 0;
        F.ret = t255;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) int64_t gm_f_completion_kind_flii2l4(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_completion_kind_flii2l4 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    t1 = ((int64_t)9LL);
    goto me2;
mn19:;
ma4:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)1ULL)) goto pa23;
    goto po22;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)13ULL)) goto pa25;
    goto po22;
pa25:;
    goto mn21;
po22:;
    t1 = ((int64_t)3LL);
    goto me2;
mn21:;
ma5:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)2ULL)) goto pa29;
    goto po28;
pa29:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)12ULL)) goto pa31;
    goto po28;
pa31:;
    goto mn27;
po28:;
    t1 = ((int64_t)2LL);
    goto me2;
mn27:;
ma6:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)3ULL)) goto mn33;
    t1 = ((int64_t)22LL);
    goto me2;
mn33:;
ma7:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)4ULL)) goto mn35;
    t1 = ((int64_t)13LL);
    goto me2;
mn35:;
ma8:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)5ULL)) goto mn37;
    t1 = ((int64_t)8LL);
    goto me2;
mn37:;
ma9:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)6ULL)) goto mn39;
    t1 = ((int64_t)7LL);
    goto me2;
mn39:;
ma10:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)7ULL)) goto mn41;
    t1 = ((int64_t)21LL);
    goto me2;
mn41:;
ma11:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)8ULL)) goto pa45;
    goto po44;
pa45:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t48 != ((uint8_t)9ULL)) goto pa47;
    goto po44;
pa47:;
    goto mn43;
po44:;
    t1 = ((int64_t)6LL);
    goto me2;
mn43:;
ma12:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    t1 = ((int64_t)5LL);
    goto me2;
mn49:;
ma13:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t52 != ((uint8_t)11ULL)) goto mn51;
    t1 = ((int64_t)20LL);
    goto me2;
mn51:;
ma14:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t54 != ((uint8_t)14ULL)) goto mn53;
    t1 = ((int64_t)7LL);
    goto me2;
mn53:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)15ULL)) goto mn55;
    t1 = ((int64_t)25LL);
    goto me2;
mn55:;
ma16:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t58 != ((uint8_t)16ULL)) goto mn57;
    t1 = ((int64_t)14LL);
    goto me2;
mn57:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 80, 46);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_definition_kaqo3c6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_definition_kaqo3c6 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_tl874t2atnlh3 gm_l3; /* s */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_l2;
        gm_toceffb697p0u t7 = gm_f_definition_at_9jj9uuu(t4, t5, t6);
        gm_toceffb697p0u t8 = t7;
        gm_tl874t2atnlh3 t9;
        if (((((uint8_t *)&t8))[12] != 0)) {
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_t8vuock3jnv59 t11 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        void * t13 = gm_l0;
        gm_tl874t2atnlh3 t14 = gm_l3;
        gm_t3ph3kbiqt1ah t15;
        gm_f_location_i7gnofg((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ph3kbiqt1ah t16 = t15;
        gm_t8vuock3jnv59 t17;
        if (((((uint8_t *)&t16))[24] != 0)) {
            gm_t8vuock3jnv59 t18 = GM_LD(gm_t8vuock3jnv59, (((uint8_t *)&t16) + 0));
            t17 = t18;
        } else {
            gm_t8vuock3jnv59 t19 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            t17 = t19;
        }
        uint8_t t20 = 0;
        F.ret = t17;
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
inline __attribute__((aligned(4))) void gm_f_document_symbols_0nlnk2i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_document_symbols_0nlnk2i F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    gm_b16_8 gm_l2; /* syms */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 285u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        uint32_t t5 = gm_l1;
        gm_b16_8 t6 = gm_f_outline_of_01eujq2(t4, t5);
        gm_l2 = t6;
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l4 = t8;
        }
        {
            for (;;) {
                uint64_t t10 = gm_l5;
                uint64_t t11 = gm_l4;
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l5;
                    uint64_t t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l6 = t19;
                    {
                        void * t21 = (void *)((uint8_t *)&F.l3);
                        void * t22 = gm_l0;
                        uint64_t t23 = gm_l6;
                        uint64_t t24 = (uintptr_t)t23;
                        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_srcvksed7nkdegg, 113, 54);
                        void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t24 * 80);
                        gm_t8vuock3jnv59 t27;
                        gm_f_symbol_json_b065ihj((void *)&t27, (void *)NULL, cx, NULL, t22, t26);
                        if (gm_unw.kind) {
                            goto gm_ll13;
                        }
                        gm_tftb1odc739t5 t28 = gm_f_push_0pgacsj_3(t21, &t27);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcvksed7nkdegg, 113, 28);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 113, 5);
me17:;
                }
lc9:;
                {
                    uint64_t t32 = gm_l5;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l5 = t35;
                }
            }
gm_ll13:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb9; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc9; }
            }
            goto gm_lad;
lb9:;
        }
    }
    {
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t38);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_oucukrb(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_full_edit_82h286k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_full_edit_82h286k F;
    gm_b16_8 gm_l0; /* old */
    gm_b16_8 gm_l1; /* new */
    gm_t9qkh0rsooc8d gm_l2; /* enc */
    gm_t6j6smj7qoctu gm_l3; /* ix */
    gm_tq47bkpmd2b9a gm_l5; /* <temporary array> */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_f_line_index_2necgtf((void *)&gm_l3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
        void * t5 = (void *)((uint8_t *)&gm_l3);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_t9qkh0rsooc8d t7 = gm_l2;
        gm_t8vuock3jnv59 t8;
        gm_f_range_json_hlsolbo((void *)&t8, (void *)NULL, cx, NULL, t5, ((uint64_t)0ULL), t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t9 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t3, t4, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sirpcjgkmlehr));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
        gm_b16_8 t12 = gm_l1;
        gm_t8vuock3jnv59 t13;
        gm_f_text_ls49dp6((void *)&t13, t12);
        gm_b0_1 t14 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t10, t11, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        {
            void * t16 = (void *)((uint8_t *)&F.l4);
            gm_t8vuock3jnv59 t17;
            gm_f_value_pkuqal3((void *)&t17, t16);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l5)) + 0, t17);
        }
        void * t18 = ((uint8_t *)&gm_l5);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcvksed7nkdegg, 389, 18);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t18 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t20 = t19;
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t20);
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
inline __attribute__((aligned(4))) void gm_f_hover_ub02g3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_hover_ub02g3b F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    GM_STACK_CHECK(&F, 606u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_l2;
        gm_teijtv56moffm t7;
        gm_f_hover_at_19okc4g((void *)&t7, t4, t5, t6);
        gm_teijtv56moffm t8 = t7;
        gm_t8scu1osgcra5 t9;
        if (((((uint8_t *)&t8))[56] != 0)) {
            gm_t8scu1osgcra5 t10 = GM_LD(gm_t8scu1osgcra5, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_t8vuock3jnv59 t11 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        F.l3 = t9;
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s7j2u3ofu66j8));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sm06nsg0msdgd));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(8));
        gm_t8vuock3jnv59 t16;
        gm_f_text_ls49dp6((void *)&t16, t15);
        gm_b0_1 t17 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t13, t14, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s5qkai093c1q0));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(5));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 16) + 0));
        gm_b16_8 t21 = gm_f_markdown_posoa59((void *)NULL, cx, NULL, t20, &(*(gm_th2lqbo77pvsc *)((((uint8_t *)&F.l3) + 16) + 16)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8vuock3jnv59 t22;
        gm_f_text_ls49dp6((void *)&t22, t21);
        gm_b0_1 t23 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t18, t19, &t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_srplcc0p49iui));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(8));
        void * t26 = (void *)((uint8_t *)&F.l4);
        gm_t8vuock3jnv59 t27;
        gm_f_value_pkuqal3((void *)&t27, t26);
        gm_b0_1 t28 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t24, t25, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 0));
        void * t34 = gm_l0;
        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t34) + 8));
        gm_t8vuock3jnv59 t36;
        gm_f_range_demc0bt((void *)&t36, (void *)NULL, cx, NULL, t32, t33, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t37 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t29, t30, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l5);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t38);
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
__attribute__((aligned(4))) void gm_f_item_json_ml3fbup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_item_json_ml3fbup F;
    void * gm_l0; /* i */
    gm_th2lqbo77pvsc gm_l1; /* sort */
    gm_b16_8 gm_l3; /* d */
    gm_b16_8 gm_l5; /* s */
    GM_STACK_CHECK(&F, 1026u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_f_obj_aoa7fl7((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su137hja0ebcd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
        gm_tabb710d9mq2e t6;
        gm_f_dupe_g9iqpvn((void *)&t6, (void *)NULL, cx, NULL, t5, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klfj89orh68)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcvksed7nkdegg, 119, 25);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_t8vuock3jnv59 t11;
        gm_f_text_ls49dp6((void *)&t11, t10);
        gm_b0_1 t12 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t2, t3, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s7j2u3ofu66j8));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
        void * t15 = gm_l0;
        gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t15) + 16));
        int64_t t17 = gm_f_completion_kind_flii2l4(t16);
        gm_t8vuock3jnv59 t18;
        gm_f_int_95caidg((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t19 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t13, t14, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 24) + 8);
        uint8_t t23 = (t22 > ((uint64_t)0ULL));
        if (!t23) goto ie20;
        {
            void * t25 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sb6smklmjl60o));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
            void * t27 = gm_l0;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 24));
            gm_tabb710d9mq2e t29;
            gm_f_dupe_g9iqpvn((void *)&t29, (void *)NULL, cx, NULL, t28, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8nmk7je8im)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t30 = t29;
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcvksed7nkdegg, 121, 48);
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
            gm_b16_8 t32 = t31;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_t8vuock3jnv59 t34;
            gm_f_text_ls49dp6((void *)&t34, t33);
            gm_b0_1 t35 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t25, t26, &t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t37 = gm_l0;
        if (!(((((uint8_t *)t37) + 40))[16] != 0)) goto ie36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 40));
        gm_l3 = t38;
        {
            gm_f_obj_aoa7fl7((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t40 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s7j2u3ofu66j8));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sm06nsg0msdgd));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(8));
            gm_t8vuock3jnv59 t43;
            gm_f_text_ls49dp6((void *)&t43, t42);
            gm_b0_1 t44 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t40, t41, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t45 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s5qkai093c1q0));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(5));
            gm_b16_8 t47 = gm_l3;
            gm_tabb710d9mq2e t48;
            gm_f_dupe_g9iqpvn((void *)&t48, (void *)NULL, cx, NULL, t47, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kn81j57eee2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t49 = t48;
            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_srcvksed7nkdegg, 125, 29);
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
            gm_b16_8 t51 = t50;
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_t8vuock3jnv59 t53;
            gm_f_text_ls49dp6((void *)&t53, t52);
            gm_b0_1 t54 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t45, t46, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t55 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s7unnudd0kkri));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(13));
            void * t57 = (void *)((uint8_t *)&F.l4);
            gm_t8vuock3jnv59 t58;
            gm_f_value_pkuqal3((void *)&t58, t57);
            gm_b0_1 t59 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t55, t56, &t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie60;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l5 = t61;
        {
            void * t63 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s54psrmgl6ti7));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(8));
            gm_t6j6smj7qoctu t65;
            gm_b16_8 t66 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
            void * t67 = gm_l0;
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)t67) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t68);
            gm_tabb710d9mq2e t69;
            gm_f_format_8n626pi((void *)&t69, (void *)NULL, cx, NULL, &t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t70 = t69;
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_srcvksed7nkdegg, 128, 43);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
            gm_b16_8 t72 = t71;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_t8vuock3jnv59 t74;
            gm_f_text_ls49dp6((void *)&t74, t73);
            gm_b0_1 t75 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t63, t64, &t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        void * t76 = (void *)((uint8_t *)&F.l2);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t76);
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
inline __attribute__((aligned(4))) void gm_f_location_i7gnofg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_location_i7gnofg F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* path */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_th2lqbo77pvsc t5;
        gm_f_open_path_6gmq4nk((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t6 = t5;
        gm_b16_8 t7;
        if (((((uint8_t *)&t6))[16] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 0));
            t7 = t8;
        } else {
            gm_t3ph3kbiqt1ah t9;
            memset(&t9, 0, sizeof t9);
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        gm_l2 = t7;
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sbo52miqad506));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 16));
        gm_b16_8 t15 = gm_l2;
        gm_b16_8 t16 = gm_f_uri_of_t6r79gp((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8vuock3jnv59 t17;
        gm_f_text_ls49dp6((void *)&t17, t16);
        gm_b0_1 t18 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t11, t12, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        gm_tl874t2atnlh3 t23 = gm_l1;
        void * t24 = gm_l0;
        gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t24) + 8));
        gm_t8vuock3jnv59 t26;
        gm_f_range_demc0bt((void *)&t26, (void *)NULL, cx, NULL, t22, t23, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t27 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t19, t20, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l3);
        gm_t8vuock3jnv59 t29;
        gm_f_value_pkuqal3((void *)&t29, t28);
        gm_t3ph3kbiqt1ah t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 28, 0);
        GM_ST(gm_t8vuock3jnv59, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[24] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_markdown_posoa59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_markdown_posoa59 F;
    gm_b16_8 gm_l0; /* code */
    gm_th2lqbo77pvsc gm_l1; /* doc_text */
    gm_b16_8 gm_l2; /* fence */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_fence_hq2pe32((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l3);
        gm_tfeab279c16rr t5;
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 0, t6);
        gm_b16_8 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 16, t7);
        gm_b16_8 t8 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t5 + 32, t8);
        gm_tftb1odc739t5 t9 = gm_f_write_8qoqt62(t4, &t5);
        gm_tftb1odc739t5 t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcvksed7nkdegg, 26, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie12;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l4 = t13;
        {
            void * t15 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s8dbdqj1sqibj));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
            gm_tftb1odc739t5 t18 = t17;
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcvksed7nkdegg, 28, 9);
            gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        }
        {
            void * t20 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t21 = gm_l4;
            gm_tftb1odc739t5 t22 = gm_f_extend_0cs85gs(t20, t21);
            gm_tftb1odc739t5 t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcvksed7nkdegg, 29, 9);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t26 = t25;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        uint8_t t28 = 0;
        F.ret = t27;
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
__attribute__((aligned(4))) void gm_f_member_context_p58kq5l(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_context_p58kq5l F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l1; /* off */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* p */
    uint64_t gm_l4; /* dot */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    uint8_t gm_l7; /* open */
    uint8_t gm_l8; /* close */
    int64_t gm_l9; /* depth */
    uint64_t gm_l10; /* j */
    GM_STACK_CHECK(&F, 725u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t5 = (t3 < t4 ? t3 : t4);
        gm_l3 = t5;
    }
    {
        for (;;) {
            uint64_t t7 = gm_l3;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) goto lb6;
            uint64_t t9 = gm_l3;
            uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 188, 44);
            uint64_t t12 = r10;
            uint64_t t13 = (uintptr_t)t12;
            uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcvksed7nkdegg, 188, 42);
            uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t13 * 1));
            uint8_t t16 = gm_f_is_ident_cont_317fpdb(t15);
            if (!t16) goto lb6;
            {
                uint64_t t19 = gm_l3;
                uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 188, 54);
                uint64_t t22 = r20;
                gm_l3 = t22;
            }
lc6:;
        }
lb6:;
    }
    {
        uint8_t t24 = 0;
        uint8_t t25 = 0;
        uint64_t t26 = gm_l3;
        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
        if (!t27) {
            uint64_t t28 = gm_l3;
            uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)1ULL), &r29);
            if (o30) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 189, 20);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcvksed7nkdegg, 189, 18);
            uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t32 * 1));
            uint8_t t35 = (!(t34 == ((uint8_t)46ULL)));
            t25 = t35;
        } else t25 = 1;
        if (!t25) {
            uint8_t t36 = 0;
            uint64_t t37 = gm_l3;
            uint8_t t38 = (t37 >= ((uint64_t)2ULL));
            if (t38) {
                uint64_t t39 = gm_l3;
                uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)2ULL), &r40);
                if (o41) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 189, 55);
                uint64_t t42 = r40;
                uint64_t t43 = (uintptr_t)t42;
                uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_srcvksed7nkdegg, 189, 53);
                uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t43 * 1));
                uint8_t t46 = ((t45 == ((uint8_t)46ULL)));
                t36 = t46;
            } else t36 = 0;
            t24 = t36;
        } else t24 = 1;
        if (!t24) goto ie23;
        {
            gm_tc70a9dbbmgkg t48;
            memset(&t48, 0, sizeof t48);
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t50 = gm_l3;
        uint64_t r51; bool o52 = __builtin_sub_overflow(t50, ((uint64_t)1ULL), &r51);
        if (o52) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 190, 12);
        uint64_t t53 = r51;
        gm_l4 = t53;
    }
    {
        uint64_t t54 = gm_l4;
        gm_l5 = t54;
    }
    {
        for (;;) {
            uint64_t t56 = gm_l5;
            uint8_t t57 = (t56 > ((uint64_t)0ULL));
            if (!t57) goto lb55;
            {
                uint64_t t60 = gm_l5;
                uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                if (o62) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 193, 16);
                uint64_t t63 = r61;
                uint64_t t64 = (uintptr_t)t63;
                uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_srcvksed7nkdegg, 193, 14);
                uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t64 * 1));
                gm_l6 = t66;
            }
            {
                uint8_t t68 = 0;
                uint8_t t69 = 0;
                uint8_t t70 = gm_l6;
                uint8_t t71 = gm_f_is_ident_cont_317fpdb(t70);
                if (!t71) {
                    uint8_t t72 = gm_l6;
                    uint8_t t73 = ((t72 == ((uint8_t)46ULL)));
                    t69 = t73;
                } else t69 = 1;
                if (!t69) {
                    uint8_t t74 = gm_l6;
                    uint8_t t75 = ((t74 == ((uint8_t)94ULL)));
                    t68 = t75;
                } else t68 = 1;
                if (!t68) goto ie67;
                {
                    uint64_t t77 = gm_l5;
                    uint64_t r78; bool o79 = __builtin_sub_overflow(t77, ((uint64_t)1ULL), &r78);
                    if (o79) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 195, 13);
                    uint64_t t80 = r78;
                    gm_l5 = t80;
                }
                goto ix67;
ie67:;
                uint8_t t82 = 0;
                uint8_t t83 = gm_l6;
                uint8_t t84 = ((t83 == ((uint8_t)41ULL)));
                if (!t84) {
                    uint8_t t85 = gm_l6;
                    uint8_t t86 = ((t85 == ((uint8_t)93ULL)));
                    t82 = t86;
                } else t82 = 1;
                if (!t82) goto ie81;
                {
                    uint8_t t88;
                    uint8_t t90 = gm_l6;
                    uint8_t t91 = ((t90 == ((uint8_t)41ULL)));
                    if (!t91) goto ie89;
                    uint8_t t93 = ((uint8_t)40ULL);
                    t88 = t93;
                    goto ix89;
ie89:;
                    uint8_t t95 = ((uint8_t)91ULL);
                    t88 = t95;
ix89:;
                    gm_l7 = t88;
                }
                {
                    uint8_t t96 = gm_l6;
                    gm_l8 = t96;
                }
                {
                    gm_l9 = ((int64_t)0LL);
                }
                {
                    uint64_t t97 = gm_l5;
                    gm_l10 = t97;
                }
                {
                    for (;;) {
                        uint64_t t99 = gm_l10;
                        uint8_t t100 = (t99 > ((uint64_t)0ULL));
                        if (!t100) goto lb98;
                        {
                            uint64_t t103 = gm_l10;
                            uint64_t r104; bool o105 = __builtin_sub_overflow(t103, ((uint64_t)1ULL), &r104);
                            if (o105) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 202, 17);
                            uint64_t t106 = r104;
                            gm_l10 = t106;
                        }
                        {
                            uint64_t t108 = gm_l10;
                            uint64_t t109 = (uintptr_t)t108;
                            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srcvksed7nkdegg, 203, 20);
                            uint8_t t111 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t109 * 1));
                            uint8_t t112 = gm_l8;
                            uint8_t t113 = ((t111 == t112));
                            if (!t113) goto ie107;
                            {
                                int64_t t115 = gm_l9;
                                int64_t r116; bool o117 = __builtin_add_overflow(t115, ((int64_t)1LL), &r116);
                                if (o117) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 204, 21);
                                int64_t t118 = r116;
                                gm_l9 = t118;
                            }
                            goto ix107;
ie107:;
                            uint64_t t120 = gm_l10;
                            uint64_t t121 = (uintptr_t)t120;
                            uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t121 >= (uintptr_t)(t122)) gm_trap_index(t121, t122, gm_srcvksed7nkdegg, 205, 27);
                            uint8_t t123 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t121 * 1));
                            uint8_t t124 = gm_l7;
                            uint8_t t125 = ((t123 == t124));
                            if (!t125) goto ie119;
                            {
                                int64_t t127 = gm_l9;
                                int64_t r128; bool o129 = __builtin_sub_overflow(t127, ((int64_t)1LL), &r128);
                                if (o129) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 206, 21);
                                int64_t t130 = r128;
                                gm_l9 = t130;
                            }
                            {
                                int64_t t132 = gm_l9;
                                uint8_t t133 = ((t132 == ((int64_t)0LL)));
                                if (!t133) goto ie131;
                                {
                                    goto lb98;
                                }
                                goto ix131;
ie131:;
ix131:;
                            }
                            goto ix119;
ie119:;
ix119:;
ix107:;
                        }
lc98:;
                    }
lb98:;
                }
                {
                    int64_t t136 = gm_l9;
                    uint8_t t137 = (!(t136 == ((int64_t)0LL)));
                    if (!t137) goto ie135;
                    {
                        gm_tc70a9dbbmgkg t139;
                        memset(&t139, 0, sizeof t139);
                        uint8_t t140 = 0;
                        F.ret = t139;
                        goto gm_fret;
                    }
                    goto ix135;
ie135:;
ix135:;
                }
                {
                    uint64_t t141 = gm_l10;
                    gm_l5 = t141;
                }
                goto ix81;
ie81:;
                {
                    goto lb55;
                }
ix81:;
ix67:;
            }
lc55:;
        }
lb55:;
    }
    {
        for (;;) {
            uint64_t t144 = gm_l5;
            uint64_t t145 = gm_l4;
            uint8_t t146 = (t144 < t145);
            if (!t146) goto lb143;
            uint64_t t147 = gm_l5;
            uint64_t t148 = (uintptr_t)t147;
            uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t148 >= (uintptr_t)(t149)) gm_trap_index(t148, t149, gm_srcvksed7nkdegg, 217, 23);
            uint8_t t150 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t148 * 1));
            uint8_t t151 = ((t150 == ((uint8_t)46ULL)));
            if (!t151) goto lb143;
            {
                uint64_t t154 = gm_l5;
                uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                if (o156) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 217, 37);
                uint64_t t157 = r155;
                gm_l5 = t157;
            }
lc143:;
        }
lb143:;
    }
    {
        uint64_t t159 = gm_l5;
        uint64_t t160 = gm_l4;
        uint8_t t161 = (t159 < t160);
        if (!t161) goto ie158;
        {
            uint64_t t163 = gm_l5;
            uint64_t t164 = gm_l4;
            gm_t2lqu8jsj511d t165;
            GM_ST(uint64_t, ((uint8_t *)&t165 + 0), t163);
            GM_ST(uint64_t, ((uint8_t *)&t165 + 8), t164);
            gm_tc70a9dbbmgkg t166;
            GM_ST(uint8_t, (uint8_t *)&t166 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t166 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t166 + 20, 0);
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t166), t165);
            ((uint8_t *)&t166)[16] = 1;
            uint8_t t167 = 0;
            F.ret = t166;
            goto gm_fret;
        }
        goto ix158;
ie158:;
ix158:;
    }
    {
        gm_tc70a9dbbmgkg t168;
        memset(&t168, 0, sizeof t168);
        uint8_t t169 = 0;
        F.ret = t168;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tc70a9dbbmgkg gm_f_member_context_p58kq5l_c(gm_b16_8 a0, uint64_t a1) { gm_tc70a9dbbmgkg gm_r; gm_f_member_context_p58kq5l(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_members_8m2g0sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    struct gm_fr_members_8m2g0sl F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* lo */
    uint32_t gm_l3; /* hi */
    void * gm_l4; /* ix */
    gm_th2lqbo77pvsc gm_l5; /* found */
    gm_b16_8 gm_l6; /* t */
    gm_b16_8 gm_l7; /* inner */
    gm_b16_8 gm_l8; /* items */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l4;
        uint32_t t6 = gm_l1;
        uint32_t t7 = gm_l2;
        uint32_t t8 = gm_l3;
        gm_f_members_at_do67lp0((void *)&gm_l5, t5, t6, t7, t8);
    }
    {
        uint8_t t10;
        if (((((uint8_t *)&gm_l5))[16] != 0)) goto mn12;
        t10 = ((uint8_t)1);
        goto me11;
mn12:;
        t10 = ((uint8_t)0);
        goto me11;
mn13:;
        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 169, 8);
me11:;
        if (!t10) goto ie9;
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            uint32_t t17 = gm_l1;
            gm_b16_8 t18 = gm_f_source_text_7ru912k(t16, t17);
            gm_l6 = t18;
        }
        {
            uint32_t t20 = gm_l2;
            uint64_t t21 = (uint64_t)t20;
            uint32_t t22 = gm_l3;
            uint64_t t23 = (uint64_t)t22;
            uint8_t t24 = (t21 <= t23);
            if (!t24) goto ie19;
            uint32_t t25 = gm_l3;
            uint64_t t26 = (uint64_t)t25;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t28 = (t26 <= t27);
            if (!t28) goto ie19;
            gm_b16_8 t29 = gm_l6;
            uint32_t t30 = gm_l2;
            uint64_t t31 = (uint64_t)t30;
            uint8_t t32 = gm_f_is_char_boundary_h62fhaa(t29, t31);
            if (!t32) goto ie19;
            gm_b16_8 t33 = gm_l6;
            uint32_t t34 = gm_l3;
            uint64_t t35 = (uint64_t)t34;
            uint8_t t36 = gm_f_is_char_boundary_h62fhaa(t33, t35);
            if (!t36) goto ie19;
            {
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                void * t39 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                uint32_t t40 = gm_l2;
                uint64_t t41 = (uint64_t)t40;
                uint64_t t42 = (uintptr_t)t41;
                uint32_t t43 = gm_l3;
                uint64_t t44 = (uint64_t)t43;
                uint64_t t45 = (uintptr_t)t44;
                if (t42 > t45 || t45 > (uintptr_t)t38) gm_trap_slice(t42, t45, t38, gm_srcvksed7nkdegg, 173, 22);
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t39 + t42 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t42));
                gm_l7 = t46;
            }
            {
                gm_b16_8 t48 = gm_l7;
                gm_b16_8 t49;
                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_smooaouqmm8dj));
                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
                uint8_t t50 = gm_f_starts_with_6vvt01e(t48, t49);
                if (!t50) goto ie47;
                gm_b16_8 t51 = gm_l7;
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s1asl1e1tvb0k));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(1));
                uint8_t t53 = gm_f_ends_with_biriu5l(t51, t52);
                if (!t53) goto ie47;
                {
                    void * t55 = gm_l4;
                    uint32_t t56 = gm_l1;
                    uint32_t t57 = gm_l2;
                    uint32_t r58; bool o59 = __builtin_add_overflow(t57, ((uint32_t)1ULL), &r58);
                    if (o59) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 174, 94);
                    uint32_t t60 = r58;
                    uint32_t t61 = gm_l3;
                    uint32_t r62; bool o63 = __builtin_sub_overflow(t61, ((uint32_t)1ULL), &r62);
                    if (o63) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 174, 102);
                    uint32_t t64 = r62;
                    gm_f_members_at_do67lp0((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l5)), t55, t56, t60, t64);
                }
                goto ix47;
ie47:;
ix47:;
            }
            goto ix19;
ie19:;
ix19:;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_th2lqbo77pvsc t65 = gm_l5;
        gm_th2lqbo77pvsc t66 = t65;
        gm_b16_8 t67;
        if (((((uint8_t *)&t66))[16] != 0)) {
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 0));
            t67 = t68;
        } else {
            gm_tqetrgo0q24b5 t69;
            memset(&t69, 0, sizeof t69);
            uint8_t t70 = 0;
            F.ret = t69;
            goto gm_fret;
        }
        gm_l8 = t67;
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            gm_l10 = t72;
        }
        {
            for (;;) {
                uint64_t t74 = gm_l11;
                uint64_t t75 = gm_l10;
                uint8_t t76 = (t74 < t75);
                if (!t76) goto lb73;
                {
                    uint64_t t79 = gm_l11;
                    uint64_t t80 = t79;
                    uint64_t t83 = GM_LD(uint64_t, ((uint8_t *)&t80));
                    gm_l12 = t83;
                    {
                        void * t85 = (void *)((uint8_t *)&F.l9);
                        uint64_t t86 = gm_l12;
                        uint64_t t87 = (uintptr_t)t86;
                        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_srcvksed7nkdegg, 179, 49);
                        void * t89 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t87 * 64);
                        gm_th2lqbo77pvsc t90;
                        memset(&t90, 0, sizeof t90);
                        gm_t8vuock3jnv59 t91;
                        gm_f_item_json_ml3fbup((void *)&t91, (void *)NULL, cx, NULL, t89, &t90);
                        if (gm_unw.kind) {
                            goto gm_ll77;
                        }
                        gm_tftb1odc739t5 t92 = gm_f_push_0pgacsj_3(t85, &t91);
                        gm_tftb1odc739t5 t93 = t92;
                        if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcvksed7nkdegg, 179, 29);
                        gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
                    }
                    goto me81;
mn82:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 179, 5);
me81:;
                }
lc73:;
                {
                    uint64_t t96 = gm_l11;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l11 = t99;
                }
            }
gm_ll77:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb73; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc73; }
            }
            goto gm_lad;
lb73:;
        }
    }
    {
        gm_trqfvmjtt4sij t100 = F.l9;
        gm_tqetrgo0q24b5 t101;
        GM_ST(uint8_t, (uint8_t *)&t101 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t101 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t101 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t101), t100);
        ((uint8_t *)&t101)[40] = 1;
        uint8_t t102 = 0;
        F.ret = t101;
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
__attribute__((aligned(4))) void gm_f_names_ms1jmv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_names_ms1jmv4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    void * gm_l3; /* ix */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* l */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* it */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* it */
    GM_STACK_CHECK(&F, 993u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        gm_l3 = t4;
    }
    {
        gm_f_hash_map_cuh4fs9((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l3;
        uint32_t t6 = gm_l1;
        uint32_t t7 = gm_l2;
        gm_f_locals_at_2n5h0d6((void *)&F.l6, (void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_closure t9 = ((gm_closure){GM_PTR_TAG(gm_f_fn_oucukrb), NULL, cx});
        gm_f_stable_sort_by_qutfkbd(t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
            gm_l7 = t11;
        }
        {
            for (;;) {
                uint64_t t13 = gm_l8;
                uint64_t t14 = gm_l7;
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l8;
                    uint64_t t19 = t18;
                    uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&t19));
                    gm_l9 = t22;
                    {
                        uint64_t t24 = gm_l9;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcvksed7nkdegg, 142, 15);
                        void * t27 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t25 * 88);
                        gm_l10 = t27;
                    }
                    {
                        void * t29 = (void *)((uint8_t *)&F.l4);
                        void * t30 = gm_l10;
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((((uint8_t *)t30) + 0) + 0));
                        uint8_t t32 = gm_f_contains_57n7jqd(t29, t31);
                        uint8_t t33 = (uint8_t)!t32;
                        if (!t33) goto ie28;
                        {
                            void * t35 = (void *)((uint8_t *)&F.l4);
                            void * t36 = gm_l10;
                            gm_b16_8 t37 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 0) + 0));
                            gm_tftb1odc739t5 t38 = gm_f_put_5c6qgui(t35, t37, ((uint8_t)1));
                            gm_tftb1odc739t5 t39 = t38;
                            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcvksed7nkdegg, 144, 13);
                            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                        }
                        {
                            void * t41 = (void *)((uint8_t *)&F.l5);
                            void * t42 = gm_l10;
                            void * t43 = (void *)(((uint8_t *)t42) + 0);
                            gm_b16_8 t44;
                            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sicgmpca63jln));
                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t45;
                            GM_ST(uint8_t, (uint8_t *)&t45 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t45 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t45 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t45), t44);
                            ((uint8_t *)&t45)[16] = 1;
                            gm_t8vuock3jnv59 t46;
                            gm_f_item_json_ml3fbup((void *)&t46, (void *)NULL, cx, NULL, t43, &t45);
                            if (gm_unw.kind) {
                                goto gm_ll16;
                            }
                            gm_tftb1odc739t5 t47 = gm_f_push_0pgacsj_3(t41, &t46);
                            gm_tftb1odc739t5 t48 = t47;
                            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcvksed7nkdegg, 145, 13);
                            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me20;
mn21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 141, 5);
me20:;
                }
lc12:;
                {
                    uint64_t t51 = gm_l8;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l8 = t54;
                }
            }
gm_ll16:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb12; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc12; }
            }
            goto gm_lad;
lb12:;
        }
    }
    {
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            void * t56 = gm_l3;
            uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)t56) + 328) + 8);
            gm_l11 = t57;
        }
        {
            for (;;) {
                uint64_t t59 = gm_l12;
                uint64_t t60 = gm_l11;
                uint8_t t61 = (t59 < t60);
                if (!t61) goto lb58;
                {
                    uint64_t t64 = gm_l12;
                    uint64_t t65 = t64;
                    uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&t65));
                    gm_l13 = t68;
                    {
                        uint64_t t70 = gm_l13;
                        uint64_t t71 = (uintptr_t)t70;
                        void * t72 = gm_l3;
                        uint64_t t73 = GM_LD(uintptr_t, (((uint8_t *)t72) + 328) + 8);
                        if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_srcvksed7nkdegg, 149, 16);
                        void * t74 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t72) + 328)) + t71 * 64);
                        gm_l14 = t74;
                    }
                    {
                        void * t76 = (void *)((uint8_t *)&F.l4);
                        void * t77 = gm_l14;
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)t77) + 0));
                        uint8_t t79 = gm_f_contains_57n7jqd(t76, t78);
                        uint8_t t80 = (uint8_t)!t79;
                        if (!t80) goto ie75;
                        {
                            void * t82 = (void *)((uint8_t *)&F.l4);
                            void * t83 = gm_l14;
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 0));
                            gm_tftb1odc739t5 t85 = gm_f_put_5c6qgui(t82, t84, ((uint8_t)1));
                            gm_tftb1odc739t5 t86 = t85;
                            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcvksed7nkdegg, 151, 13);
                            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                        }
                        {
                            void * t88 = (void *)((uint8_t *)&F.l5);
                            void * t89 = gm_l14;
                            gm_b16_8 t90;
                            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_svubtddeidu1d));
                            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t91;
                            GM_ST(uint8_t, (uint8_t *)&t91 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t91 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t91 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t91), t90);
                            ((uint8_t *)&t91)[16] = 1;
                            gm_t8vuock3jnv59 t92;
                            gm_f_item_json_ml3fbup((void *)&t92, (void *)NULL, cx, NULL, t89, &t91);
                            if (gm_unw.kind) {
                                goto gm_ll62;
                            }
                            gm_tftb1odc739t5 t93 = gm_f_push_0pgacsj_3(t88, &t92);
                            gm_tftb1odc739t5 t94 = t93;
                            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 1, gm_srcvksed7nkdegg, 152, 13);
                            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t94) + 1));
                        }
                        goto ix75;
ie75:;
ix75:;
                    }
                    goto me66;
mn67:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 148, 5);
me66:;
                }
lc58:;
                {
                    uint64_t t97 = gm_l12;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l12 = t100;
                }
            }
gm_ll62:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb58; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc58; }
            }
            goto gm_lad;
lb58:;
        }
    }
    {
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            void * t102 = gm_l3;
            uint64_t t103 = GM_LD(uintptr_t, (((uint8_t *)t102) + 344) + 8);
            gm_l15 = t103;
        }
        {
            for (;;) {
                uint64_t t105 = gm_l16;
                uint64_t t106 = gm_l15;
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l16;
                    uint64_t t111 = t110;
                    uint64_t t114 = GM_LD(uint64_t, ((uint8_t *)&t111));
                    gm_l17 = t114;
                    {
                        uint64_t t116 = gm_l17;
                        uint64_t t117 = (uintptr_t)t116;
                        void * t118 = gm_l3;
                        uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)t118) + 344) + 8);
                        if (t117 >= (uintptr_t)(t119)) gm_trap_index(t117, t119, gm_srcvksed7nkdegg, 156, 16);
                        void * t120 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t118) + 344)) + t117 * 64);
                        gm_l18 = t120;
                    }
                    {
                        void * t122 = (void *)((uint8_t *)&F.l4);
                        void * t123 = gm_l18;
                        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 0));
                        uint8_t t125 = gm_f_contains_57n7jqd(t122, t124);
                        uint8_t t126 = (uint8_t)!t125;
                        if (!t126) goto ie121;
                        {
                            void * t128 = (void *)((uint8_t *)&F.l4);
                            void * t129 = gm_l18;
                            gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)t129) + 0));
                            gm_tftb1odc739t5 t131 = gm_f_put_5c6qgui(t128, t130, ((uint8_t)1));
                            gm_tftb1odc739t5 t132 = t131;
                            if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srcvksed7nkdegg, 158, 13);
                            gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
                        }
                        {
                            void * t134 = (void *)((uint8_t *)&F.l5);
                            void * t135 = gm_l18;
                            gm_b16_8 t136;
                            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s2dssfe3261t2));
                            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(1));
                            gm_th2lqbo77pvsc t137;
                            GM_ST(uint8_t, (uint8_t *)&t137 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t137 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t137 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t137), t136);
                            ((uint8_t *)&t137)[16] = 1;
                            gm_t8vuock3jnv59 t138;
                            gm_f_item_json_ml3fbup((void *)&t138, (void *)NULL, cx, NULL, t135, &t137);
                            if (gm_unw.kind) {
                                goto gm_ll108;
                            }
                            gm_tftb1odc739t5 t139 = gm_f_push_0pgacsj_3(t134, &t138);
                            gm_tftb1odc739t5 t140 = t139;
                            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srcvksed7nkdegg, 159, 13);
                            gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
                        }
                        goto ix121;
ie121:;
ix121:;
                    }
                    goto me112;
mn113:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 155, 5);
me112:;
                }
lc104:;
                {
                    uint64_t t143 = gm_l16;
                    uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                    uint64_t t146 = r144;
                    gm_l16 = t146;
                }
            }
gm_ll108:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb104; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc104; }
            }
            goto gm_lad;
lb104:;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l5), 40);
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
extern __attribute__((aligned(4))) uint32_t gm_f_offset_at_73sli4j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_references_pd7nt1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, uint8_t a3) {
    struct gm_fr_references_pd7nt1t F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    uint8_t gm_l3; /* include_declaration */
    gm_tl874t2atnlh3 gm_l4; /* def */
    gm_t8vuock3jnv59 gm_l7; /* l */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tl874t2atnlh3 gm_l10; /* s */
    gm_t8vuock3jnv59 gm_l11; /* l */
    GM_STACK_CHECK(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 120);
        uint32_t t5 = gm_l1;
        uint32_t t6 = gm_l2;
        gm_toceffb697p0u t7 = gm_f_definition_at_9jj9uuu(t4, t5, t6);
        gm_toceffb697p0u t8 = t7;
        gm_tl874t2atnlh3 t9;
        if (((((uint8_t *)&t8))[12] != 0)) {
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b16_8 t11 = gm_f_empty_slice_dheabhi_3();
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        gm_l4 = t9;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 120);
        gm_tl874t2atnlh3 t17 = gm_l4;
        gm_f_uses_of_nalefva((void *)&F.l5, (void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t19 = gm_l3;
        if (!t19) goto ie18;
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        uint8_t t22 = (t20 > t21);
        if (!t22) goto ie18;
        {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l4;
            gm_t3ph3kbiqt1ah t27;
            gm_f_location_i7gnofg((void *)&t27, (void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3ph3kbiqt1ah t28 = t27;
            if (!((((uint8_t *)&t28))[24] != 0)) goto ie24;
            gm_t8vuock3jnv59 t29 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&t28));
            gm_l7 = t29;
            {
                void * t31 = (void *)((uint8_t *)&F.l6);
                gm_tftb1odc739t5 t32 = gm_f_push_0pgacsj_3(t31, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l7)));
                gm_tftb1odc739t5 t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcvksed7nkdegg, 55, 37);
                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
            }
            goto ix24;
ie24:;
ix24:;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        {
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l8 = t36;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t38 = gm_l9;
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t40 = (t38 < t39);
                if (!t40) goto lb37;
                {
                    uint64_t t43 = gm_l9;
                    uint64_t t44 = (uintptr_t)t43;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t44 * 12));
                    gm_l10 = t48;
                    {
                        void * t51 = gm_l0;
                        gm_tl874t2atnlh3 t52 = gm_l10;
                        gm_t3ph3kbiqt1ah t53;
                        gm_f_location_i7gnofg((void *)&t53, (void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_ll41;
                        }
                        gm_t3ph3kbiqt1ah t54 = t53;
                        if (!((((uint8_t *)&t54))[24] != 0)) goto ie50;
                        gm_t8vuock3jnv59 t55 = GM_LD(gm_t8vuock3jnv59, ((uint8_t *)&t54));
                        gm_l11 = t55;
                        {
                            void * t57 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t58 = gm_f_push_0pgacsj_3(t57, &(*(gm_t8vuock3jnv59 *)((uint8_t *)&gm_l11)));
                            gm_tftb1odc739t5 t59 = t58;
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcvksed7nkdegg, 58, 35);
                            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        }
                        goto ix50;
ie50:;
ix50:;
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 57, 5);
me46:;
                }
lc37:;
                {
                    uint64_t t62 = gm_l9;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l9 = t65;
                }
            }
gm_ll41:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb37; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc37; }
            }
            goto gm_lad;
lb37:;
        }
    }
    {
        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t67 = t66;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_f_array_k32ana3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), (void *)NULL, cx, NULL, t68);
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
__attribute__((aligned(4))) void gm_f_signature_help_16c2a8f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_signature_help_16c2a8f F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* file */
    uint32_t gm_l2; /* off */
    gm_b16_8 gm_l3; /* t */
    uint64_t gm_l5; /* active */
    gm_b16_8 gm_l7; /* code */
    gm_b16_8 gm_l10; /* name */
    uint64_t gm_l11; /* s */
    uint64_t gm_l12; /* before_end */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* p */
    gm_b16_8 gm_l17; /* n */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_b16_8 gm_l21; /* first */
    uint64_t gm_l22; /* k */
    gm_b16_8 gm_l23; /* label */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* p */
    gm_b16_8 gm_l30; /* d */
    gm_tq47bkpmd2b9a gm_l33; /* <temporary array> */
    GM_STACK_CHECK(&F, 3508u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        gm_b16_8 t5 = gm_f_source_text_7ru912k(t3, t4);
        gm_l3 = t5;
    }
    {
        gm_b16_8 t6 = gm_l3;
        uint32_t t7 = gm_l2;
        uint64_t t8 = (uint64_t)t7;
        gm_t9nnki79sf2e1 t9;
        gm_f_call_context_r6lbirq((void *)&t9, (void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9nnki79sf2e1 t10 = t9;
        gm_tvin65ttqbr1d t11;
        if (((((uint8_t *)&t10))[40] != 0)) {
            gm_tvin65ttqbr1d t12 = GM_LD(gm_tvin65ttqbr1d, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            gm_t8vuock3jnv59 t13 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        F.l4 = t11;
    }
    {
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        gm_l5 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        void * t18 = (void *)(((uint8_t *)t17) + 120);
        uint32_t t19 = gm_l1;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
        uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 326, 37);
        uint64_t t23 = r21;
        uint32_t t24 = (uint32_t)t23;
        gm_teijtv56moffm t25;
        gm_f_hover_at_19okc4g((void *)&t25, t18, t19, t24);
        gm_teijtv56moffm t26 = t25;
        gm_t8scu1osgcra5 t27;
        if (((((uint8_t *)&t26))[56] != 0)) {
            gm_t8scu1osgcra5 t28 = GM_LD(gm_t8scu1osgcra5, (((uint8_t *)&t26) + 0));
            t27 = t28;
        } else {
            gm_t8vuock3jnv59 t29 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        F.l6 = t27;
    }
    {
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 16) + 0));
        gm_l7 = t31;
    }
    {
        gm_b16_8 t32 = gm_l7;
        gm_tec7dnv8fjfj8 t33;
        gm_f_split_signature_3r31d7h((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tec7dnv8fjfj8 t34 = t33;
        gm_t6igi6646q59o t35;
        if (((((uint8_t *)&t34))[56] != 0)) {
            gm_t6igi6646q59o t36 = GM_LD(gm_t6igi6646q59o, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            gm_t8vuock3jnv59 t37 = GM_LD(gm_t8vuock3jnv59, gm_cro0h2panct);
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        F.l8 = t35;
    }
    {
        gm_trqfvmjtt4sij t39 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l8) + 0));
        F.l9 = t39;
    }
    {
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
        gm_l10 = t40;
    }
    {
        gm_b16_8 t42 = gm_l7;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s2urait735ofq));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
        uint8_t t44 = gm_f_contains_gkki4kq(t42, t43);
        if (!t44) goto ie41;
        {
            gm_b16_8 t46 = gm_l7;
            gm_b16_8 t47 = t46;
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&t47) + 8);
            void * t49 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t47)));
            uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t51 = gm_l7;
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s1b629p19p3qq));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(3));
            gm_tf5srh4mp4b3i t53 = gm_f_find_nab29co(t51, t52);
            gm_tf5srh4mp4b3i t54 = t53;
            uint64_t t55;
            if (((((uint8_t *)&t54))[8] != 0)) {
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&t54) + 0));
                t55 = t56;
            } else {
                t55 = ((uint64_t)0ULL);
            }
            uint64_t t57 = (uintptr_t)t55;
            if (t50 > t57 || t57 > (uintptr_t)t48) gm_trap_slice(t50, t57, t48, gm_srcvksed7nkdegg, 332, 16);
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t49 + t50 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(t57 - t50));
            gm_l10 = t58;
        }
        goto ix41;
ie41:;
        {
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
            gm_l11 = t60;
        }
        {
            for (;;) {
                uint64_t t62 = gm_l11;
                uint8_t t63 = (t62 > ((uint64_t)0ULL));
                if (!t63) goto lb61;
                uint8_t t64 = 0;
                uint64_t t65 = gm_l11;
                uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
                if (o67) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 28);
                uint64_t t68 = r66;
                uint64_t t69 = (uintptr_t)t68;
                uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_srcvksed7nkdegg, 336, 26);
                uint8_t t71 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t69 * 1));
                uint8_t t72 = (t71 >= ((uint8_t)128ULL));
                if (!t72) {
                    uint64_t t73 = gm_l11;
                    uint64_t r74; bool o75 = __builtin_sub_overflow(t73, ((uint64_t)1ULL), &r74);
                    if (o75) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 69);
                    uint64_t t76 = r74;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_srcvksed7nkdegg, 336, 67);
                    uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t77 * 1));
                    uint8_t t80 = gm_f_is_ident_cont_317fpdb(t79);
                    t64 = t80;
                } else t64 = 1;
                if (!t64) goto lb61;
                {
                    uint64_t t83 = gm_l11;
                    uint64_t r84; bool o85 = __builtin_sub_overflow(t83, ((uint64_t)1ULL), &r84);
                    if (o85) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 336, 80);
                    uint64_t t86 = r84;
                    gm_l11 = t86;
                }
lc61:;
            }
lb61:;
        }
        {
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t88 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t89 = gm_l11;
            uint64_t t90 = (uintptr_t)t89;
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
            uint64_t t92 = (uintptr_t)t91;
            if (t90 > t92 || t92 > (uintptr_t)t87) gm_trap_slice(t90, t92, t87, gm_srcvksed7nkdegg, 337, 16);
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(((uint8_t *)t88 + t90 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(t92 - t90));
            gm_l10 = t93;
        }
ix41:;
    }
    {
        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 0));
        gm_l12 = t94;
    }
    {
        for (;;) {
            uint64_t t96 = gm_l12;
            uint8_t t97 = (t96 > ((uint64_t)0ULL));
            if (!t97) goto lb95;
            uint8_t t98 = 0;
            uint64_t t99 = gm_l12;
            uint64_t r100; bool o101 = __builtin_sub_overflow(t99, ((uint64_t)1ULL), &r100);
            if (o101) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 33);
            uint64_t t102 = r100;
            uint64_t t103 = (uintptr_t)t102;
            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srcvksed7nkdegg, 341, 31);
            uint8_t t105 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t103 * 1));
            uint8_t t106 = (t105 >= ((uint8_t)128ULL));
            if (!t106) {
                uint64_t t107 = gm_l12;
                uint64_t r108; bool o109 = __builtin_sub_overflow(t107, ((uint64_t)1ULL), &r108);
                if (o109) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 83);
                uint64_t t110 = r108;
                uint64_t t111 = (uintptr_t)t110;
                uint64_t t112 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t111 >= (uintptr_t)(t112)) gm_trap_index(t111, t112, gm_srcvksed7nkdegg, 341, 81);
                uint8_t t113 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t111 * 1));
                uint8_t t114 = gm_f_is_ident_cont_317fpdb(t113);
                t98 = t114;
            } else t98 = 1;
            if (!t98) goto lb95;
            {
                uint64_t t117 = gm_l12;
                uint64_t r118; bool o119 = __builtin_sub_overflow(t117, ((uint64_t)1ULL), &r118);
                if (o119) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 341, 103);
                uint64_t t120 = r118;
                gm_l12 = t120;
            }
lc95:;
        }
lb95:;
    }
    {
        uint64_t t122 = gm_l12;
        uint8_t t123 = (t122 > ((uint64_t)0ULL));
        if (!t123) goto ie121;
        uint64_t t124 = gm_l12;
        uint64_t r125; bool o126 = __builtin_sub_overflow(t124, ((uint64_t)1ULL), &r125);
        if (o126) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 342, 29);
        uint64_t t127 = r125;
        uint64_t t128 = (uintptr_t)t127;
        uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srcvksed7nkdegg, 342, 27);
        uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t128 * 1));
        uint8_t t131 = ((t130 == ((uint8_t)46ULL)));
        if (!t131) goto ie121;
        {
            gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_l14 = t134;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t136 = gm_l15;
                    uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t138 = (t136 < t137);
                    if (!t138) goto lb135;
                    {
                        uint64_t t141 = gm_l15;
                        uint64_t t142 = (uintptr_t)t141;
                        uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t142 * 16));
                        gm_l16 = t146;
                        {
                            uint8_t t149 = 0;
                            gm_b16_8 t150 = gm_l16;
                            gm_b16_8 t151;
                            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s7b8bvi5kqaqt));
                            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(6));
                            uint8_t t152 = gm_f_ends_with_biriu5l(t150, t151);
                            if (!t152) {
                                gm_b16_8 t153 = gm_l16;
                                gm_b16_8 t154;
                                GM_ST(void *, (uint8_t *)&t154, (void *)(gm_s2urait735ofq));
                                GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(4));
                                uint8_t t155 = gm_f_contains_gkki4kq(t153, t154);
                                t149 = t155;
                            } else t149 = 1;
                            uint8_t t156 = (uint8_t)!t149;
                            if (!t156) goto ie148;
                            {
                                void * t158 = (void *)((uint8_t *)&F.l13);
                                gm_b16_8 t159 = gm_l16;
                                gm_tftb1odc739t5 t160 = gm_f_push_3ef746b(t158, t159);
                                gm_tftb1odc739t5 t161 = t160;
                                if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcvksed7nkdegg, 345, 73);
                                gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                            }
                            goto ix148;
ie148:;
ix148:;
                        }
                        goto me144;
mn145:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 344, 9);
me144:;
                    }
lc135:;
                    {
                        uint64_t t164 = gm_l15;
                        uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                        uint64_t t167 = r165;
                        gm_l15 = t167;
                    }
                }
lb135:;
            }
        }
        {
            gm_trqfvmjtt4sij t168 = F.l13;
            F.l9 = t168;
        }
        {
            uint64_t t170 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
            uint8_t t171 = (t170 > ((uint64_t)0ULL));
            if (!t171) goto ie169;
            {
                {
                    void * t174 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t175 = gm_f_remove_qvip58u(t174, ((uint64_t)0ULL));
                }
            }
            goto ix169;
ie169:;
ix169:;
        }
        goto ix121;
ie121:;
ix121:;
    }
    {
        if (!(((((uint8_t *)&F.l4) + 16))[16] != 0)) goto ie176;
        gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
        gm_l17 = t177;
        {
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                gm_l18 = t180;
            }
            {
                for (;;) {
                    uint64_t t182 = gm_l19;
                    uint64_t t183 = gm_l18;
                    uint8_t t184 = (t182 < t183);
                    if (!t184) goto lb181;
                    {
                        uint64_t t187 = gm_l19;
                        uint64_t t188 = t187;
                        uint64_t t191 = GM_LD(uint64_t, ((uint8_t *)&t188));
                        gm_l20 = t191;
                        {
                            uint64_t t193 = gm_l20;
                            uint64_t t194 = (uintptr_t)t193;
                            uint64_t t195 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcvksed7nkdegg, 352, 22);
                            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l9) + 0)) + t194 * 16));
                            gm_l21 = t196;
                        }
                        {
                            gm_b16_8 t198 = gm_l21;
                            gm_tf5srh4mp4b3i t199 = gm_f_index_of_hdeg351(t198, ((uint8_t)58ULL));
                            gm_tf5srh4mp4b3i t200 = t199;
                            if (!((((uint8_t *)&t200))[8] != 0)) goto ie197;
                            uint64_t t201 = GM_LD(uint64_t, ((uint8_t *)&t200));
                            gm_l22 = t201;
                            {
                                uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                void * t204 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)));
                                uint64_t t205 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t206 = gm_l22;
                                uint64_t t207 = (uintptr_t)t206;
                                if (t205 > t207 || t207 > (uintptr_t)t203) gm_trap_slice(t205, t207, t203, gm_srcvksed7nkdegg, 353, 51);
                                gm_b16_8 t208;
                                GM_ST(void *, (uint8_t *)&t208, (void *)(((uint8_t *)t204 + t205 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(t207 - t205));
                                gm_l21 = t208;
                            }
                            goto ix197;
ie197:;
ix197:;
                        }
                        {
                            gm_b16_8 t210 = gm_l21;
                            gm_b16_8 t211 = gm_f_trim_fdrfe6i(t210);
                            gm_b16_8 t212 = gm_l17;
                            gm_b16_8 t213 = t211;
                            gm_b16_8 t214 = t212;
                            uint8_t t215 = (gm_eq0iqiubfm7e(((uint8_t *)&t213), ((uint8_t *)&t214)));
                            if (!t215) goto ie209;
                            {
                                uint64_t t217 = gm_l20;
                                gm_l5 = t217;
                            }
                            {
                                goto lb181;
                            }
                            goto ix209;
ie209:;
ix209:;
                        }
                        goto me189;
mn190:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 351, 9);
me189:;
                    }
lc181:;
                    {
                        uint64_t t219 = gm_l19;
                        uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                        uint64_t t222 = r220;
                        gm_l19 = t222;
                    }
                }
lb181:;
            }
        }
        goto ix176;
ie176:;
ix176:;
    }
    {
        gm_tfeab279c16rr t223;
        gm_b16_8 t224 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 0, t224);
        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t226 = t225;
        gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
        gm_b16_8 t228;
        GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t229;
        gm_f_join_sfknljb((void *)&t229, (void *)NULL, cx, NULL, t227, t228);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t230 = t229;
        if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 8, gm_srcvksed7nkdegg, 360, 43);
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 16, t231);
        gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 32, t232);
        gm_tabb710d9mq2e t233;
        gm_f_format_9ug250p((void *)&t233, (void *)NULL, cx, NULL, &t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t234 = t233;
        if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_srcvksed7nkdegg, 360, 14);
        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
        gm_l23 = t235;
    }
    {
        gm_f_list_0202sdh_3((void *)&F.l24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l25 = t237;
        }
        {
            gm_l26 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t239 = gm_l26;
                uint64_t t240 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                uint8_t t241 = (t239 < t240);
                if (!t241) goto lb238;
                {
                    uint64_t t244 = gm_l26;
                    uint64_t t245 = (uintptr_t)t244;
                    uint64_t t246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t245 * 16));
                    gm_l27 = t249;
                    {
                        gm_f_obj_aoa7fl7((void *)&F.l28, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll242;
                        }
                    }
                    {
                        void * t251 = (void *)((uint8_t *)&F.l28);
                        gm_b16_8 t252;
                        GM_ST(void *, (uint8_t *)&t252, (void *)(gm_su137hja0ebcd));
                        GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(5));
                        gm_b16_8 t253 = gm_l27;
                        gm_t8vuock3jnv59 t254;
                        gm_f_text_ls49dp6((void *)&t254, t253);
                        gm_b0_1 t255 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t251, t252, &t254);
                        if (gm_unw.kind) {
                            goto gm_ll242;
                        }
                    }
                    {
                        void * t256 = (void *)((uint8_t *)&F.l24);
                        void * t257 = (void *)((uint8_t *)&F.l28);
                        gm_t8vuock3jnv59 t258;
                        gm_f_value_pkuqal3((void *)&t258, t257);
                        gm_tftb1odc739t5 t259 = gm_f_push_0pgacsj_3(t256, &t258);
                        gm_tftb1odc739t5 t260 = t259;
                        if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 1, gm_srcvksed7nkdegg, 365, 9);
                        gm_b0_1 t261 = GM_LD(gm_b0_1, (((uint8_t *)&t260) + 1));
                    }
                    goto me247;
mn248:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 362, 5);
me247:;
                }
lc238:;
                {
                    uint64_t t263 = gm_l26;
                    uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                    uint64_t t266 = r264;
                    gm_l26 = t266;
                }
            }
gm_ll242:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb238; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc238; }
            }
            goto gm_lad;
lb238:;
        }
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t267 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t268;
        GM_ST(void *, (uint8_t *)&t268, (void *)(gm_su137hja0ebcd));
        GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(5));
        gm_b16_8 t269 = gm_l23;
        gm_b16_8 t270 = t269;
        gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
        gm_t8vuock3jnv59 t272;
        gm_f_text_ls49dp6((void *)&t272, t271);
        gm_b0_1 t273 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t267, t268, &t272);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t274 = (void *)((uint8_t *)&F.l29);
        gm_b16_8 t275;
        GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sd6a57g2jmtks));
        GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(10));
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
        gm_b16_8 t277 = t276;
        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
        gm_t8vuock3jnv59 t279;
        gm_f_array_k32ana3((void *)&t279, (void *)NULL, cx, NULL, t278);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t280 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t274, t275, &t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((((uint8_t *)&F.l6) + 16) + 16))[16] != 0)) goto ie281;
        gm_b16_8 t282 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 16) + 16));
        gm_l30 = t282;
        {
            gm_f_obj_aoa7fl7((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t284 = (void *)((uint8_t *)&F.l31);
            gm_b16_8 t285;
            GM_ST(void *, (uint8_t *)&t285, (void *)(gm_s7j2u3ofu66j8));
            GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(4));
            gm_b16_8 t286;
            GM_ST(void *, (uint8_t *)&t286, (void *)(gm_sm06nsg0msdgd));
            GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(8));
            gm_t8vuock3jnv59 t287;
            gm_f_text_ls49dp6((void *)&t287, t286);
            gm_b0_1 t288 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t284, t285, &t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t289 = (void *)((uint8_t *)&F.l31);
            gm_b16_8 t290;
            GM_ST(void *, (uint8_t *)&t290, (void *)(gm_s5qkai093c1q0));
            GM_ST(uintptr_t, (uint8_t *)&t290 + 8, (uintptr_t)(5));
            gm_b16_8 t291 = gm_l30;
            gm_tabb710d9mq2e t292;
            gm_f_dupe_g9iqpvn((void *)&t292, (void *)NULL, cx, NULL, t291, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7bjnihrifk)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t293 = t292;
            if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 8, gm_srcvksed7nkdegg, 373, 29);
            gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&t293) + 8));
            gm_b16_8 t295 = t294;
            gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
            gm_t8vuock3jnv59 t297;
            gm_f_text_ls49dp6((void *)&t297, t296);
            gm_b0_1 t298 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t289, t290, &t297);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t299 = (void *)((uint8_t *)&F.l29);
            gm_b16_8 t300;
            GM_ST(void *, (uint8_t *)&t300, (void *)(gm_s7unnudd0kkri));
            GM_ST(uintptr_t, (uint8_t *)&t300 + 8, (uintptr_t)(13));
            void * t301 = (void *)((uint8_t *)&F.l31);
            gm_t8vuock3jnv59 t302;
            gm_f_value_pkuqal3((void *)&t302, t301);
            gm_b0_1 t303 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t299, t300, &t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix281;
ie281:;
ix281:;
    }
    {
        gm_f_obj_aoa7fl7((void *)&F.l32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
    }
    {
        void * t304 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t305;
        GM_ST(void *, (uint8_t *)&t305, (void *)(gm_s22mk3qhbqci2));
        GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(10));
        {
            void * t307 = (void *)((uint8_t *)&F.l29);
            gm_t8vuock3jnv59 t308;
            gm_f_value_pkuqal3((void *)&t308, t307);
            GM_ST(gm_t8vuock3jnv59, (uint8_t *)&(*(gm_tq47bkpmd2b9a *)((uint8_t *)&gm_l33)) + 0, t308);
        }
        void * t309 = ((uint8_t *)&gm_l33);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcvksed7nkdegg, 377, 31);
        gm_b16_8 t310;
        GM_ST(void *, (uint8_t *)&t310, (void *)(((uint8_t *)t309 + (uintptr_t)0 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t311 = t310;
        gm_t8vuock3jnv59 t312;
        gm_f_array_k32ana3((void *)&t312, (void *)NULL, cx, NULL, t311);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t313 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t304, t305, &t312);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t314 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t315;
        GM_ST(void *, (uint8_t *)&t315, (void *)(gm_si1j2hanh22ll));
        GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(15));
        gm_t8vuock3jnv59 t316;
        gm_f_int_95caidg((void *)&t316, (void *)NULL, cx, NULL, ((int64_t)0LL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t317 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t314, t315, &t316);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t318 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t319;
        GM_ST(void *, (uint8_t *)&t319, (void *)(gm_schvua5hkahp9));
        GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(15));
        uint64_t t320 = gm_l5;
        int64_t t321 = (int64_t)t320;
        gm_t8vuock3jnv59 t322;
        gm_f_int_95caidg((void *)&t322, (void *)NULL, cx, NULL, t321);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t323 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t318, t319, &t322);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t324 = (void *)((uint8_t *)&F.l32);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t324);
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
__attribute__((aligned(4))) void gm_f_split_signature_3r31d7h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_signature_3r31d7h F;
    gm_b16_8 gm_l0; /* sig */
    uint64_t gm_l1; /* at */
    uint64_t gm_l2; /* start */
    int64_t gm_l3; /* depth */
    uint64_t gm_l4; /* cur_start */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* c */
    gm_b16_8 gm_l10; /* last */
    GM_STACK_CHECK(&F, 891u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sum9vj65rg4jb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_tf5srh4mp4b3i t4 = gm_f_find_nab29co(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tec7dnv8fjfj8 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)3ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 297, 14);
        uint64_t t13 = r11;
        gm_l2 = t13;
    }
    {
        gm_l3 = ((int64_t)0LL);
    }
    {
        uint64_t t14 = gm_l2;
        gm_l4 = t14;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint64_t t16 = gm_l2;
            gm_l7 = t16;
        }
        {
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l6 = t17;
        }
        {
            for (;;) {
                uint64_t t19 = gm_l7;
                uint64_t t20 = gm_l6;
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l7;
                    uint64_t t25 = t24;
                    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t25));
                    gm_l8 = t28;
                    {
                        uint64_t t30 = gm_l8;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_srcvksed7nkdegg, 302, 14);
                        uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t31 * 1));
                        gm_l9 = t33;
                    }
                    {
                        uint8_t t35 = 0;
                        uint8_t t36 = 0;
                        uint8_t t37 = gm_l9;
                        uint8_t t38 = ((t37 == ((uint8_t)40ULL)));
                        if (!t38) {
                            uint8_t t39 = gm_l9;
                            uint8_t t40 = ((t39 == ((uint8_t)91ULL)));
                            t36 = t40;
                        } else t36 = 1;
                        if (!t36) {
                            uint8_t t41 = gm_l9;
                            uint8_t t42 = ((t41 == ((uint8_t)123ULL)));
                            t35 = t42;
                        } else t35 = 1;
                        if (!t35) goto ie34;
                        {
                            int64_t t44 = gm_l3;
                            int64_t r45; bool o46 = __builtin_add_overflow(t44, ((int64_t)1LL), &r45);
                            if (o46) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 304, 13);
                            int64_t t47 = r45;
                            gm_l3 = t47;
                        }
                        goto ix34;
ie34:;
                        uint8_t t49 = 0;
                        uint8_t t50 = 0;
                        uint8_t t51 = gm_l9;
                        uint8_t t52 = ((t51 == ((uint8_t)41ULL)));
                        if (!t52) {
                            uint8_t t53 = gm_l9;
                            uint8_t t54 = ((t53 == ((uint8_t)93ULL)));
                            t50 = t54;
                        } else t50 = 1;
                        if (!t50) {
                            uint8_t t55 = gm_l9;
                            uint8_t t56 = ((t55 == ((uint8_t)125ULL)));
                            t49 = t56;
                        } else t49 = 1;
                        if (!t49) goto ie48;
                        {
                            int64_t t59 = gm_l3;
                            uint8_t t60 = ((t59 == ((int64_t)0LL)));
                            if (!t60) goto ie58;
                            {
                                uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t63 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t64 = gm_l4;
                                uint64_t t65 = (uintptr_t)t64;
                                uint64_t t66 = gm_l8;
                                uint64_t t67 = (uintptr_t)t66;
                                if (t65 > t67 || t67 > (uintptr_t)t62) gm_trap_slice(t65, t67, t62, gm_srcvksed7nkdegg, 307, 34);
                                gm_b16_8 t68;
                                GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t63 + t65 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67 - t65));
                                gm_b16_8 t69 = gm_f_trim_fdrfe6i(t68);
                                gm_l10 = t69;
                            }
                            {
                                uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                uint8_t t72 = (t71 > ((uint64_t)0ULL));
                                if (!t72) goto ie70;
                                {
                                    void * t74 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t75 = gm_l10;
                                    gm_tftb1odc739t5 t76 = gm_f_push_3ef746b(t74, t75);
                                    gm_tftb1odc739t5 t77 = t76;
                                    if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcvksed7nkdegg, 308, 35);
                                    gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                                }
                                goto ix70;
ie70:;
ix70:;
                            }
                            {
                                gm_t6igi6646q59o t79;
                                memcpy((uint8_t *)&t79 + 0, ((uint8_t *)&F.l5), 40);
                                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                                void * t81 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                                uint64_t t82 = gm_l8;
                                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                                if (o84) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 309, 65);
                                uint64_t t85 = r83;
                                uint64_t t86 = (uintptr_t)t85;
                                if (t86 > (uintptr_t)t80 || (uintptr_t)t80 > (uintptr_t)t80) gm_trap_slice(t86, (uintptr_t)t80, t80, gm_srcvksed7nkdegg, 309, 61);
                                gm_b16_8 t87;
                                GM_ST(void *, (uint8_t *)&t87, (void *)(((uint8_t *)t81 + t86 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)((uintptr_t)t80 - t86));
                                GM_ST(gm_b16_8, (uint8_t *)&t79 + 40, t87);
                                gm_tec7dnv8fjfj8 t88;
                                GM_ST(uint8_t, (uint8_t *)&t88 + 57, 0);
                                GM_ST(uint16_t, (uint8_t *)&t88 + 58, 0);
                                GM_ST(uint32_t, (uint8_t *)&t88 + 60, 0);
                                GM_ST(gm_t6igi6646q59o, ((uint8_t *)&t88), t79);
                                ((uint8_t *)&t88)[56] = 1;
                                uint8_t t89 = 0;
                                F.ret = t88;
                                goto gm_fret;
                            }
                            goto ix58;
ie58:;
ix58:;
                        }
                        {
                            int64_t t90 = gm_l3;
                            int64_t r91; bool o92 = __builtin_sub_overflow(t90, ((int64_t)1LL), &r91);
                            if (o92) gm_trap("integer overflow in `-`", gm_srcvksed7nkdegg, 311, 13);
                            int64_t t93 = r91;
                            gm_l3 = t93;
                        }
                        goto ix48;
ie48:;
                        uint8_t t95 = gm_l9;
                        uint8_t t96 = ((t95 == ((uint8_t)44ULL)));
                        if (!t96) goto ie94;
                        int64_t t97 = gm_l3;
                        uint8_t t98 = ((t97 == ((int64_t)0LL)));
                        if (!t98) goto ie94;
                        {
                            void * t100 = (void *)((uint8_t *)&F.l5);
                            uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            void * t102 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                            uint64_t t103 = gm_l4;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = gm_l8;
                            uint64_t t106 = (uintptr_t)t105;
                            if (t104 > t106 || t106 > (uintptr_t)t101) gm_trap_slice(t104, t106, t101, gm_srcvksed7nkdegg, 313, 34);
                            gm_b16_8 t107;
                            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t102 + t104 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106 - t104));
                            gm_b16_8 t108 = gm_f_trim_fdrfe6i(t107);
                            gm_tftb1odc739t5 t109 = gm_f_push_3ef746b(t100, t108);
                            gm_tftb1odc739t5 t110 = t109;
                            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srcvksed7nkdegg, 313, 13);
                            gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
                        }
                        {
                            uint64_t t112 = gm_l8;
                            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                            if (o114) gm_trap("integer overflow in `+`", gm_srcvksed7nkdegg, 314, 25);
                            uint64_t t115 = r113;
                            gm_l4 = t115;
                        }
                        goto ix94;
ie94:;
ix94:;
ix48:;
ix34:;
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 301, 5);
me26:;
                }
lc18:;
                {
                    uint64_t t117 = gm_l7;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                    uint64_t t120 = r118;
                    gm_l7 = t120;
                }
            }
lb18:;
        }
    }
    {
        gm_tec7dnv8fjfj8 t121;
        memset(&t121, 0, sizeof t121);
        uint8_t t122 = 0;
        F.ret = t121;
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
__attribute__((aligned(4))) void gm_f_symbol_json_b065ihj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_symbol_json_b065ihj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* s */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 696u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_0202sdh_3((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 64) + 8);
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
                        void * t17 = (void *)((uint8_t *)&F.l2);
                        void * t18 = gm_l0;
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = (uintptr_t)t19;
                        void * t21 = gm_l1;
                        uint64_t t22 = GM_LD(uintptr_t, (((uint8_t *)t21) + 64) + 8);
                        if (t20 >= (uintptr_t)(t22)) gm_trap_index(t20, t22, gm_srcvksed7nkdegg, 99, 65);
                        void * t23 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t21) + 64)) + t20 * 80);
                        gm_t8vuock3jnv59 t24;
                        gm_f_symbol_json_b065ihj((void *)&t24, (void *)NULL, cx, NULL, t18, t23);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t25 = gm_f_push_0pgacsj_3(t17, &t24);
                        gm_tftb1odc739t5 t26 = t25;
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcvksed7nkdegg, 99, 34);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 99, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l4;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l4 = t32;
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
        gm_f_obj_aoa7fl7((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(4));
        void * t35 = gm_l1;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 0));
        gm_t8vuock3jnv59 t37;
        gm_f_text_ls49dp6((void *)&t37, t36);
        gm_b0_1 t38 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t33, t34, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t39 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sb6smklmjl60o));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(6));
        void * t41 = gm_l1;
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t41) + 24));
        gm_t8vuock3jnv59 t43;
        gm_f_text_ls49dp6((void *)&t43, t42);
        gm_b0_1 t44 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t39, t40, &t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t45 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s7j2u3ofu66j8));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
        void * t47 = gm_l1;
        gm_t9qkh0rsooc8d t48 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t47) + 16));
        int64_t t49 = gm_f_symbol_kind_g6njvq1(t48);
        gm_t8vuock3jnv59 t50;
        gm_f_int_95caidg((void *)&t50, (void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t51 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t45, t46, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t52 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sl4ns6hobicht));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(5));
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
        void * t56 = gm_l1;
        gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 40));
        void * t58 = gm_l0;
        gm_t9qkh0rsooc8d t59 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t58) + 8));
        gm_t8vuock3jnv59 t60;
        gm_f_range_demc0bt((void *)&t60, (void *)NULL, cx, NULL, t55, t57, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t61 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t52, t53, &t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t62 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sttp5u65omp7t));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(14));
        void * t64 = gm_l0;
        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
        void * t66 = gm_l1;
        gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 52));
        void * t68 = gm_l0;
        gm_t9qkh0rsooc8d t69 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t68) + 8));
        gm_t8vuock3jnv59 t70;
        gm_f_range_demc0bt((void *)&t70, (void *)NULL, cx, NULL, t65, t67, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t71 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t62, t63, &t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t72 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(gm_srdnkonbfvqdq));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(8));
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t75 = t74;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        gm_t8vuock3jnv59 t77;
        gm_f_array_k32ana3((void *)&t77, (void *)NULL, cx, NULL, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t78 = gm_f_put_91stqpj((void *)NULL, cx, NULL, t72, t73, &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t79 = (void *)((uint8_t *)&F.l6);
        gm_f_value_pkuqal3((void *)&(*(gm_t8vuock3jnv59 *)gm_out), t79);
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
inline __attribute__((aligned(4))) int64_t gm_f_symbol_kind_g6njvq1(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_symbol_kind_g6njvq1 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t18) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma4; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma15; case ((uint8_t)16ULL): goto ma16; default: goto ma17; }
ma3:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)0ULL)) goto mn19;
    t1 = ((int64_t)2LL);
    goto me2;
mn19:;
ma4:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)1ULL)) goto pa23;
    goto po22;
pa23:;
    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t26 != ((uint8_t)13ULL)) goto pa25;
    goto po22;
pa25:;
    goto mn21;
po22:;
    t1 = ((int64_t)12LL);
    goto me2;
mn21:;
ma5:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)2ULL)) goto pa29;
    goto po28;
pa29:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t32 != ((uint8_t)12ULL)) goto pa31;
    goto po28;
pa31:;
    goto mn27;
po28:;
    t1 = ((int64_t)6LL);
    goto me2;
mn27:;
ma6:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)3ULL)) goto mn33;
    t1 = ((int64_t)23LL);
    goto me2;
mn33:;
ma7:;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t36 != ((uint8_t)4ULL)) goto mn35;
    t1 = ((int64_t)10LL);
    goto me2;
mn35:;
ma8:;
    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t38 != ((uint8_t)5ULL)) goto mn37;
    t1 = ((int64_t)11LL);
    goto me2;
mn37:;
ma9:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)6ULL)) goto mn39;
    t1 = ((int64_t)19LL);
    goto me2;
mn39:;
ma10:;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t42 != ((uint8_t)7ULL)) goto mn41;
    t1 = ((int64_t)14LL);
    goto me2;
mn41:;
ma11:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t46 != ((uint8_t)8ULL)) goto pa45;
    goto po44;
pa45:;
    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t48 != ((uint8_t)9ULL)) goto pa47;
    goto po44;
pa47:;
    goto mn43;
po44:;
    t1 = ((int64_t)13LL);
    goto me2;
mn43:;
ma12:;
    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t50 != ((uint8_t)10ULL)) goto mn49;
    t1 = ((int64_t)8LL);
    goto me2;
mn49:;
ma13:;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t52 != ((uint8_t)11ULL)) goto mn51;
    t1 = ((int64_t)22LL);
    goto me2;
mn51:;
ma14:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t54 != ((uint8_t)14ULL)) goto mn53;
    t1 = ((int64_t)5LL);
    goto me2;
mn53:;
ma15:;
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t56 != ((uint8_t)15ULL)) goto mn55;
    t1 = ((int64_t)26LL);
    goto me2;
mn55:;
ma16:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t58 != ((uint8_t)16ULL)) goto mn57;
    t1 = ((int64_t)13LL);
    goto me2;
mn57:;
ma17:;
    gm_trap("no match arm matched the scrutinee", gm_srcvksed7nkdegg, 63, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
