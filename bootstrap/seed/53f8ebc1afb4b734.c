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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_t1d8endhr9k72;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; } gm_t1h4kdi7gs4v5;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; gm_b16_8 f4; uint64_t f5; uint64_t f6; } gm_t6s59i0k0msk8;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tr4ummvbupakb;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t1d8endhr9k72) == 40 && _Alignof(gm_t1d8endhr9k72) == 8 && offsetof(gm_t1d8endhr9k72, f0) == 0 && offsetof(gm_t1d8endhr9k72, f1) == 8 && offsetof(gm_t1d8endhr9k72, f2) == 24 && offsetof(gm_t1d8endhr9k72, f3) == 32, "layout of `(u64, string, u64, u64)`");
_Static_assert(sizeof(gm_t1h4kdi7gs4v5) == 64 && _Alignof(gm_t1h4kdi7gs4v5) == 8 && offsetof(gm_t1h4kdi7gs4v5, f0) == 0 && offsetof(gm_t1h4kdi7gs4v5, f1) == 16 && offsetof(gm_t1h4kdi7gs4v5, f2) == 32 && offsetof(gm_t1h4kdi7gs4v5, f3) == 40 && offsetof(gm_t1h4kdi7gs4v5, f4) == 56, "layout of `(string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s59i0k0msk8) == 72 && _Alignof(gm_t6s59i0k0msk8) == 8 && offsetof(gm_t6s59i0k0msk8, f0) == 0 && offsetof(gm_t6s59i0k0msk8, f1) == 8 && offsetof(gm_t6s59i0k0msk8, f2) == 24 && offsetof(gm_t6s59i0k0msk8, f3) == 32 && offsetof(gm_t6s59i0k0msk8, f4) == 40 && offsetof(gm_t6s59i0k0msk8, f5) == 56 && offsetof(gm_t6s59i0k0msk8, f6) == 64, "layout of `(u64, string, u64, u64, string, u64, u64)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tr4ummvbupakb) == 64 && _Alignof(gm_tr4ummvbupakb) == 8 && offsetof(gm_tr4ummvbupakb, f0) == 0 && offsetof(gm_tr4ummvbupakb, f1) == 16 && offsetof(gm_tr4ummvbupakb, f2) == 24 && offsetof(gm_tr4ummvbupakb, f3) == 32 && offsetof(gm_tr4ummvbupakb, f4) == 48 && offsetof(gm_tr4ummvbupakb, f5) == 56, "layout of `(string, u64, u64, string, u64, u64)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; } gm_te68172gglous;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_te68172gglous) == 304 && _Alignof(gm_te68172gglous) == 8 && offsetof(gm_te68172gglous, f0) == 0 && offsetof(gm_te68172gglous, f1) == 40 && offsetof(gm_te68172gglous, f2) == 80 && offsetof(gm_te68172gglous, f3) == 136 && offsetof(gm_te68172gglous, f4) == 192 && offsetof(gm_te68172gglous, f5) == 248, "layout of `Helpers`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_bytewise_eq_e8jgutj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_helper_k8mi0a4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_define_helper_c1utd1n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_fn_5udvcnu {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l32; /* sd */
    gm_t0mglksrkfa5e l39; /* fd */
    gm_tckdobunaulee l47; /* v */
    gm_b16_8 ret;
};
struct gm_fr_hash_elems_7gnr39c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_hash_fn_nlk82uj {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l23; /* sd */
    gm_t0mglksrkfa5e l29; /* fd */
    gm_tckdobunaulee l37; /* var */
    gm_b16_8 ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_scalar_pt0pjre {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_hash_step_ari7fns {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_helper_name_fob48de {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_less_fn_64um0ab {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l29; /* sd */
    gm_b16_8 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_helpers_4q783cn {
    gm_frame h;
    gm_te68172gglous ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4hhihlj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5f2vpdc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8nj2557(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8to6k7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_edav5jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eqjc9ro(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g3tqbla(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_l1ii34d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lovbvbi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_me0dr2l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vd1blml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_elems_7gnr39c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_hash_scalar_pt0pjre(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_step_ari7fns(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0kurvf5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2tci68e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_49bd3in(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4unpkmv(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_551e4in(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ahb23t(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6nhr3v9(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7k4ript(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7sl1fre(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_91uvm3b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_96p75mj(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aakaioo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aaqg4k2(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_avsehn1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bjaiklv(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ho6u1fd(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_idp5df3(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ikd2l7g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jfl63ic(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jgspi6j(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jrf40pg(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k88klp2(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k9lbjqb(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ltl9pvt(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_q1ni37m(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qvj9qj9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sbsltkj(void * a0, const void * a1);
extern const uint8_t gm_s81r46mpa8ra9[29];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9iu5gsqog7uc[5];
extern const uint8_t gm_sciq0a78hirud[25];
extern const uint8_t gm_sel1fj24qk32p[15];
extern const uint8_t gm_sfm0hseeqersg[25];
extern const uint8_t gm_sge6a56sqn6kj[21];
extern const uint8_t gm_sgn3gq6gfib7b[40];
extern const uint8_t gm_shm5oj4temhh6[33];
extern const uint8_t gm_shmmkvp83pr05[18];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sl6pi8la5phb6[20];
extern const uint8_t gm_slq0l63qf1dvs[11];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_sor8flejv9lfq[67];
extern const uint8_t gm_sp2m434o2c1ha[3];
extern const uint8_t gm_sqg70d5uem21e[35];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckg9scnip7e8g[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssdi9g220if8h[2];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_sug64ltbhov0m[19];
extern const uint8_t gm_sup5ecv5jiqbn[119];
extern const uint8_t gm_surrjo1qo53mh[3];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eqb2nno7n67r(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_helper_name_fob48de F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_b16_8 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 248);
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_39snre1((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srckg9scnip7e8g, 28, 105);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    void * t8 = gm_l1;
    gm_t88rcucg1b7uk t9 = gm_l3;
    gm_b16_8 t10 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t11 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t2, t7, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 80, 91);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b16_8 t9 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t1;
    gm_f_hash_map_m04clhi((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_ttiitul0263ld *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bytewise_eq_e8jgutj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* bits */
    uint64_t gm_l3; /* storage */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t5v8n6qrn4rh4 t3;
    gm_f_repr_53quqki((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t7 != ((uint8_t)0ULL)) goto mn6;
    uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t3) + 8) + 4));
    gm_l2 = t8;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)&t3) + 8) + 8));
    gm_l3 = t9;
    uint32_t t10 = gm_l2;
    uint64_t t11 = (uint64_t)t10;
    uint64_t t12 = gm_l3;
    uint64_t r13; bool o14 = __builtin_mul_overflow(t12, ((uint64_t)8ULL), &r13);
    if (o14) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 147, 44);
    uint64_t t15 = r13;
    uint8_t t16 = ((t11 == t15));
    t4 = t16;
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn17:;
    gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 146, 62);
me5:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_declare_helper_k8mi0a4 F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    void * gm_l2; /* memo */
    gm_b16_8 gm_l3; /* kind */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* ret */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l4;
        gm_b16_8 t6 = gm_f_helper_name_fob48de((void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t6;
    }
    {
        void * t7 = gm_l2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t9 = gm_l7;
        gm_tftb1odc739t5 t10 = gm_f_put_po09vud(t7, t8, t9);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srckg9scnip7e8g, 35, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 0);
        gm_tfeab279c16rr t14;
        gm_b16_8 t15 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
        gm_b16_8 t16 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
        gm_b16_8 t17 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 32, t17);
        gm_tftb1odc739t5 t18 = gm_f_write_sbsltkj(t13, &t14);
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srckg9scnip7e8g, 36, 5);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
    }
    {
        gm_b16_8 t20 = gm_l7;
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_define_helper_c1utd1n F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* ret */
    gm_b16_8 gm_l3; /* params */
    gm_b16_8 gm_l4; /* body */
    GM_STACK_CHECK(&F, 254u);
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
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tfeab279c16rr t4;
        gm_b16_8 t5 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_b16_8 t7 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t7);
        gm_tftb1odc739t5 t8 = gm_f_write_jrf40pg(t3, &t4);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srckg9scnip7e8g, 44, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 40);
        gm_b16_8 t12 = gm_l4;
        gm_tftb1odc739t5 t13 = gm_f_extend_0cs85gs(t11, t12);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srckg9scnip7e8g, 45, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 40);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srckg9scnip7e8g, 46, 5);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_eq_fn_5udvcnu F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* f */
    uint64_t gm_l15; /* len */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    gm_b16_8 gm_l18; /* f */
    gm_t88rcucg1b7uk gm_l19; /* elem */
    uint64_t gm_l20; /* esz */
    gm_b16_8 gm_l21; /* f */
    gm_t88rcucg1b7uk gm_l22; /* inner */
    gm_b16_8 gm_l23; /* f */
    uint64_t gm_l24; /* flag */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* fe */
    gm_b16_8 gm_l28; /* fo */
    gm_b16_8 gm_l29; /* offs */
    uint64_t gm_l30; /* tg */
    uint64_t gm_l31; /* pl */
    gm_t88rcucg1b7uk gm_l33; /* u */
    gm_b16_8 gm_l34; /* f */
    gm_b16_8 gm_l35; /* offs */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* f */
    gm_t88rcucg1b7uk gm_l41; /* tag */
    gm_b16_8 gm_l42; /* variants */
    gm_b16_8 gm_l43; /* offs */
    gm_b16_8 gm_l44; /* tc */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_t88rcucg1b7uk gm_l48; /* pt */
    gm_b16_8 gm_l49; /* f */
    GM_STACK_CHECK(&F, 3719u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 80);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l1;
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 80);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sp2m434o2c1ha));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(34));
        gm_b16_8 t19 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t11, t12, t14, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uint64_t, (((((uint8_t *)t20) + 8) + 0) + 0));
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l1;
        gm_t88rcucg1b7uk t23 = gm_l2;
        gm_t5v8n6qrn4rh4 t24;
        gm_f_repr_53quqki((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        switch (t30) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma27; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma28; case ((uint8_t)5ULL): goto ma28; case ((uint8_t)6ULL): goto ma28; default: goto ma28; }
ma26:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t34 != ((uint8_t)0ULL)) goto pa33;
        goto po32;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t36 != ((uint8_t)1ULL)) goto pa35;
        goto po32;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t38 != ((uint8_t)3ULL)) goto pa37;
        goto po32;
pa37:;
        goto mn31;
po32:;
        {
            void * t40 = gm_l1;
            gm_t88rcucg1b7uk t41 = gm_l2;
            gm_b16_8 t42 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t42;
        }
        {
            void * t43 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t44;
            gm_b16_8 t45 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
            gm_b16_8 t46 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t44 + 16, t46);
            gm_tftb1odc739t5 t47 = gm_f_write_avsehn1(t43, &t44);
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srckg9scnip7e8g, 58, 13);
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
        }
        goto me25;
mn31:;
ma27:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t50 != ((uint8_t)2ULL)) goto mn49;
        void * t51 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sgn3gq6gfib7b));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(39));
        gm_tftb1odc739t5 t53 = gm_f_extend_0cs85gs(t51, t52);
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srckg9scnip7e8g, 60, 18);
        gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
        goto me25;
mn49:;
ma28:;
        void * t56 = gm_l1;
        gm_t88rcucg1b7uk t57 = gm_l2;
        gm_t53at80pfdus3 t58;
        gm_f_kind_ghvav22((void *)&t58, t56, GM_CONV(gm_csguankko4lmhd, t57));
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        switch (t68) { case ((uint8_t)0ULL): goto ma66; case ((uint8_t)1ULL): goto ma66; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma66; case ((uint8_t)4ULL): goto ma66; case ((uint8_t)5ULL): goto ma66; case ((uint8_t)6ULL): goto ma66; case ((uint8_t)7ULL): goto ma66; case ((uint8_t)8ULL): goto ma66; case ((uint8_t)9ULL): goto ma66; case ((uint8_t)10ULL): goto ma62; case ((uint8_t)11ULL): goto ma61; case ((uint8_t)12ULL): goto ma60; case ((uint8_t)13ULL): goto ma63; case ((uint8_t)14ULL): goto ma64; case ((uint8_t)15ULL): goto ma66; case ((uint8_t)16ULL): goto ma66; case ((uint8_t)17ULL): goto ma65; case ((uint8_t)18ULL): goto ma66; case ((uint8_t)19ULL): goto ma66; case ((uint8_t)20ULL): goto ma66; case ((uint8_t)21ULL): goto ma66; case ((uint8_t)22ULL): goto ma66; case ((uint8_t)23ULL): goto ma66; case ((uint8_t)24ULL): goto ma66; case ((uint8_t)25ULL): goto ma66; case ((uint8_t)26ULL): goto ma66; default: goto ma66; }
ma60:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t70 != ((uint8_t)12ULL)) goto mn69;
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((((uint8_t *)&t58) + 8) + 0));
        gm_l8 = t71;
        {
            void * t73 = gm_l1;
            gm_t88rcucg1b7uk t74 = gm_l2;
            gm_b16_8 t75 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t75;
        }
        {
            {
                gm_b16_8 t77 = gm_l8;
                gm_l10 = t77;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t79 = gm_l11;
                    uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t81 = (t79 < t80);
                    if (!t81) goto lb78;
                    {
                        uint64_t t84 = gm_l11;
                        uint64_t t85 = gm_l11;
                        uint64_t t86 = (uintptr_t)t85;
                        uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t86 * 4));
                        gm_tlcm4lvu3g1a8 t89;
                        GM_ST(uint32_t, (uint8_t *)&t89 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t89 + 0), t84);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89 + 8), t88);
                        uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&t89) + 0));
                        gm_l12 = t92;
                        gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t89) + 8));
                        gm_l13 = t93;
                        {
                            void * t95 = gm_l0;
                            void * t96 = gm_l1;
                            gm_t88rcucg1b7uk t97 = gm_l13;
                            gm_b16_8 t98 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t95, t96, t97);
                            if (gm_unw.kind) {
                                goto gm_ll82;
                            }
                            gm_l14 = t98;
                        }
                        {
                            void * t99 = (void *)((uint8_t *)&F.l5);
                            gm_tgbgti82mf879 t100;
                            gm_b16_8 t101 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
                            uint64_t t102 = gm_l12;
                            uint64_t t103 = (uintptr_t)t102;
                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_srckg9scnip7e8g, 66, 80);
                            uint64_t t105 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t103 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t100 + 16, t105);
                            uint64_t t106 = gm_l12;
                            uint64_t t107 = (uintptr_t)t106;
                            uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_srckg9scnip7e8g, 66, 89);
                            uint64_t t109 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t107 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t100 + 24, t109);
                            gm_tftb1odc739t5 t110 = gm_f_write_7sl1fre(t99, &t100);
                            if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_srckg9scnip7e8g, 66, 21);
                            gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 64, 17);
me90:;
                    }
lc78:;
                    {
                        uint64_t t113 = gm_l11;
                        uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                        uint64_t t116 = r114;
                        gm_l11 = t116;
                    }
                }
gm_ll82:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb78; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc78; }
                }
                goto gm_lad;
lb78:;
            }
        }
        {
            void * t117 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t118;
            GM_ST(void *, (uint8_t *)&t118, (void *)(gm_shmmkvp83pr05));
            GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t119 = gm_f_extend_0cs85gs(t117, t118);
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srckg9scnip7e8g, 68, 17);
            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
        }
        goto me59;
mn69:;
ma61:;
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t122 != ((uint8_t)11ULL)) goto mn121;
        uint64_t t123 = GM_LD(uint64_t, ((((uint8_t *)&t58) + 8) + 0));
        gm_l15 = t123;
        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t58) + 8) + 8));
        gm_l16 = t124;
        {
            void * t126 = gm_l1;
            gm_t88rcucg1b7uk t127 = gm_l16;
            uint64_t t128 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t126, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t128;
        }
        {
            void * t130 = gm_l1;
            gm_t88rcucg1b7uk t131 = gm_l16;
            uint8_t t132 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t132) goto ie129;
            {
                void * t134 = (void *)((uint8_t *)&F.l5);
                uint64_t t135 = gm_l15;
                uint64_t t136 = gm_l17;
                uint64_t r137; bool o138 = __builtin_mul_overflow(t135, t136, &r137);
                if (o138) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 73, 68);
                uint64_t t139 = r137;
                gm_t4ce0jd3tdro4 t140;
                GM_ST(uint64_t, ((uint8_t *)&t140 + 0), t139);
                gm_tftb1odc739t5 t141 = gm_f_write_idp5df3(t134, t140);
                if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 1, gm_srckg9scnip7e8g, 73, 21);
                gm_b0_1 t142 = GM_LD(gm_b0_1, (((uint8_t *)&t141) + 1));
            }
            goto ix129;
ie129:;
            {
                void * t144 = gm_l0;
                void * t145 = gm_l1;
                gm_t88rcucg1b7uk t146 = gm_l16;
                gm_b16_8 t147 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t144, t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t147;
            }
            {
                void * t148 = (void *)((uint8_t *)&F.l5);
                gm_t1d8endhr9k72 t149;
                uint64_t t150 = gm_l15;
                GM_ST(uint64_t, (uint8_t *)&t149 + 0, t150);
                gm_b16_8 t151 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 8, t151);
                uint64_t t152 = gm_l17;
                GM_ST(uint64_t, (uint8_t *)&t149 + 24, t152);
                uint64_t t153 = gm_l17;
                GM_ST(uint64_t, (uint8_t *)&t149 + 32, t153);
                gm_tftb1odc739t5 t154 = gm_f_write_49bd3in(t148, &t149);
                if (((((uint8_t *)&t154))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t154) + 1, gm_srckg9scnip7e8g, 76, 21);
                gm_b0_1 t155 = GM_LD(gm_b0_1, (((uint8_t *)&t154) + 1));
            }
            {
                void * t156 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t157;
                GM_ST(void *, (uint8_t *)&t157, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t158 = gm_f_extend_0cs85gs(t156, t157);
                if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_srckg9scnip7e8g, 77, 21);
                gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
            }
ix129:;
        }
        goto me59;
mn121:;
ma62:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t161 != ((uint8_t)10ULL)) goto mn160;
        gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t58) + 8) + 4));
        gm_l19 = t162;
        {
            void * t164 = gm_l1;
            gm_t88rcucg1b7uk t165 = gm_l19;
            uint64_t t166 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t166;
        }
        {
            void * t167 = (void *)((uint8_t *)&F.l5);
            uint64_t t168 = gm_l6;
            gm_t4ce0jd3tdro4 t169;
            GM_ST(uint64_t, ((uint8_t *)&t169 + 0), t168);
            gm_tftb1odc739t5 t170 = gm_f_write_baasn56(t167, t169);
            if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 1, gm_srckg9scnip7e8g, 82, 17);
            gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 1));
        }
        {
            void * t172 = (void *)((uint8_t *)&F.l5);
            uint64_t t173 = gm_l6;
            gm_t4ce0jd3tdro4 t174;
            GM_ST(uint64_t, ((uint8_t *)&t174 + 0), t173);
            gm_tftb1odc739t5 t175 = gm_f_write_o50khi1(t172, t174);
            if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srckg9scnip7e8g, 83, 17);
            gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
        }
        {
            void * t177 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t178;
            GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sor8flejv9lfq));
            GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(66));
            gm_tftb1odc739t5 t179 = gm_f_extend_0cs85gs(t177, t178);
            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srckg9scnip7e8g, 84, 17);
            gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
        }
        {
            void * t182 = gm_l1;
            gm_t88rcucg1b7uk t183 = gm_l19;
            uint8_t t184 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t184) goto ie181;
            {
                void * t186 = (void *)((uint8_t *)&F.l5);
                uint64_t t187 = gm_l20;
                gm_t4ce0jd3tdro4 t188;
                GM_ST(uint64_t, ((uint8_t *)&t188 + 0), t187);
                gm_tftb1odc739t5 t189 = gm_f_write_ho6u1fd(t186, t188);
                if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 1, gm_srckg9scnip7e8g, 86, 21);
                gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t189) + 1));
            }
            goto ix181;
ie181:;
            {
                void * t192 = gm_l0;
                void * t193 = gm_l1;
                gm_t88rcucg1b7uk t194 = gm_l19;
                gm_b16_8 t195 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t192, t193, t194);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t195;
            }
            {
                void * t196 = (void *)((uint8_t *)&F.l5);
                gm_tgbgti82mf879 t197;
                gm_b16_8 t198 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
                uint64_t t199 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t197 + 16, t199);
                uint64_t t200 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t197 + 24, t200);
                gm_tftb1odc739t5 t201 = gm_f_write_qvj9qj9(t196, &t197);
                if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srckg9scnip7e8g, 89, 21);
                gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
            }
            {
                void * t203 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t204;
                GM_ST(void *, (uint8_t *)&t204, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t205 = gm_f_extend_0cs85gs(t203, t204);
                if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_srckg9scnip7e8g, 90, 21);
                gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
            }
ix181:;
        }
        goto me59;
mn160:;
ma63:;
        uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t208 != ((uint8_t)13ULL)) goto mn207;
        gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t58) + 8));
        gm_l22 = t209;
        {
            void * t211 = gm_l0;
            void * t212 = gm_l1;
            gm_t88rcucg1b7uk t213 = gm_l22;
            gm_b16_8 t214 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t211, t212, t213);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t214;
        }
        {
            uint64_t t215 = (uintptr_t)((uint64_t)1ULL);
            void * t216 = gm_l1;
            gm_t88rcucg1b7uk t217 = gm_l2;
            gm_b16_8 t218 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t216, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t219 = GM_LD(uintptr_t, ((uint8_t *)&t218) + 8);
            if (t215 >= (uintptr_t)(t219)) gm_trap_index(t215, t219, gm_srckg9scnip7e8g, 95, 25);
            uint64_t t220 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t218)) + t215 * 8));
            gm_l24 = t220;
        }
        {
            void * t221 = (void *)((uint8_t *)&F.l5);
            uint64_t t222 = gm_l24;
            uint64_t t223 = gm_l24;
            gm_t2lqu8jsj511d t224;
            GM_ST(uint64_t, ((uint8_t *)&t224 + 0), t222);
            GM_ST(uint64_t, ((uint8_t *)&t224 + 8), t223);
            gm_tftb1odc739t5 t225 = gm_f_write_jgspi6j(t221, t224);
            if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srckg9scnip7e8g, 96, 17);
            gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
        }
        {
            void * t227 = (void *)((uint8_t *)&F.l5);
            gm_tf1bjg81u0glv t228;
            uint64_t t229 = gm_l24;
            GM_ST(uint64_t, (uint8_t *)&t228 + 0, t229);
            gm_b16_8 t230 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 8, t230);
            gm_tftb1odc739t5 t231 = gm_f_write_ed06dp1(t227, &t228);
            if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_srckg9scnip7e8g, 97, 17);
            gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
        }
        goto me59;
mn207:;
ma64:;
        uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t234 != ((uint8_t)14ULL)) goto mn233;
        gm_t88rcucg1b7uk t235 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t58) + 8) + 0));
        gm_l25 = t235;
        gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t58) + 8) + 4));
        gm_l26 = t236;
        {
            void * t238 = gm_l0;
            void * t239 = gm_l1;
            gm_t88rcucg1b7uk t240 = gm_l25;
            gm_b16_8 t241 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t238, t239, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t241;
        }
        {
            void * t242 = gm_l0;
            void * t243 = gm_l1;
            gm_t88rcucg1b7uk t244 = gm_l26;
            gm_b16_8 t245 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t242, t243, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t245;
        }
        {
            void * t246 = gm_l1;
            gm_t88rcucg1b7uk t247 = gm_l2;
            gm_b16_8 t248 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t246, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t248;
        }
        {
            uint64_t t249 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_srckg9scnip7e8g, 103, 23);
            uint64_t t251 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t249 * 8));
            gm_l30 = t251;
        }
        {
            uint64_t t252 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t253 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t252 >= (uintptr_t)(t253)) gm_trap_index(t252, t253, gm_srckg9scnip7e8g, 104, 23);
            uint64_t t254 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t252 * 8));
            gm_l31 = t254;
        }
        {
            void * t255 = (void *)((uint8_t *)&F.l5);
            uint64_t t256 = gm_l30;
            uint64_t t257 = gm_l30;
            gm_t2lqu8jsj511d t258;
            GM_ST(uint64_t, ((uint8_t *)&t258 + 0), t256);
            GM_ST(uint64_t, ((uint8_t *)&t258 + 8), t257);
            gm_tftb1odc739t5 t259 = gm_f_write_6nhr3v9(t255, t258);
            if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srckg9scnip7e8g, 105, 17);
            gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
        }
        {
            void * t261 = (void *)((uint8_t *)&F.l5);
            gm_t6s59i0k0msk8 t262;
            uint64_t t263 = gm_l30;
            GM_ST(uint64_t, (uint8_t *)&t262 + 0, t263);
            gm_b16_8 t264 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t262 + 8, t264);
            uint64_t t265 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t262 + 24, t265);
            uint64_t t266 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t262 + 32, t266);
            gm_b16_8 t267 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t262 + 40, t267);
            uint64_t t268 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t262 + 56, t268);
            uint64_t t269 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t262 + 64, t269);
            gm_tftb1odc739t5 t270 = gm_f_write_bq1tl8a(t261, &t262);
            if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 1, gm_srckg9scnip7e8g, 106, 17);
            gm_b0_1 t271 = GM_LD(gm_b0_1, (((uint8_t *)&t270) + 1));
        }
        goto me59;
mn233:;
ma65:;
        uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
        if (t273 != ((uint8_t)17ULL)) goto mn272;
        void * t274 = gm_l1;
        gm_t88rcucg1b7uk t275 = gm_l2;
        void * t276 = gm_f_adt_of_gej99vn(t274, GM_CONV(gm_csguankko4lmhd, t275));
        uint8_t t282 = GM_LD(uint8_t, ((((uint8_t *)t276) + 24) + 0));
        switch (t282) { case ((uint8_t)0ULL): goto ma278; case ((uint8_t)1ULL): goto ma279; case ((uint8_t)2ULL): goto ma280; default: goto ma281; }
ma278:;
        uint8_t t284 = GM_LD(uint8_t, ((((uint8_t *)t276) + 24) + 0));
        if (t284 != ((uint8_t)0ULL)) goto mn283;
        gm_tpautr4br7h3l t285 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t276) + 24) + 8));
        F.l32 = t285;
        {
            if (!(((((uint8_t *)&F.l32) + 40))[4] != 0)) goto ie287;
            gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 40));
            gm_l33 = t288;
            {
                void * t290 = gm_l0;
                void * t291 = gm_l1;
                gm_t88rcucg1b7uk t292 = gm_l33;
                gm_b16_8 t293 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t290, t291, t292);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t293;
            }
            {
                void * t294 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t295 = gm_l34;
                gm_tnpifv20irnph t296;
                GM_ST(gm_b16_8, ((uint8_t *)&t296 + 0), t295);
                gm_tftb1odc739t5 t297 = gm_f_write_96p75mj(t294, t296);
                if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 1, gm_srckg9scnip7e8g, 112, 25);
                gm_b0_1 t298 = GM_LD(gm_b0_1, (((uint8_t *)&t297) + 1));
            }
            goto ix287;
ie287:;
            {
                void * t300 = gm_l1;
                gm_t88rcucg1b7uk t301 = gm_l2;
                gm_b16_8 t302 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t300, t301);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t302;
            }
            {
                {
                    gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                    gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                    gm_l36 = t305;
                }
                {
                    gm_l37 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t307 = gm_l37;
                        uint64_t t308 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        uint8_t t309 = (t307 < t308);
                        if (!t309) goto lb306;
                        {
                            gm_t8ta482bl6be0 t312;
                            uint64_t t313 = gm_l37;
                            GM_ST(uint64_t, (uint8_t *)&t312 + 0, t313);
                            uint64_t t314 = gm_l37;
                            uint64_t t315 = (uintptr_t)t314;
                            uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                            memcpy((uint8_t *)&t312 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t315 * 40), 40);
                            uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)&t312) + 0));
                            gm_l38 = t319;
                            F.l39 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t312) + 8));
                            {
                                void * t321 = gm_l0;
                                void * t322 = gm_l1;
                                gm_t88rcucg1b7uk t323 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l39) + 16));
                                gm_b16_8 t324 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t321, t322, t323);
                                if (gm_unw.kind) {
                                    goto gm_ll310;
                                }
                                gm_l40 = t324;
                            }
                            {
                                void * t325 = (void *)((uint8_t *)&F.l5);
                                gm_tgbgti82mf879 t326;
                                gm_b16_8 t327 = gm_l40;
                                GM_ST(gm_b16_8, (uint8_t *)&t326 + 0, t327);
                                uint64_t t328 = gm_l38;
                                uint64_t t329 = (uintptr_t)t328;
                                uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                if (t329 >= (uintptr_t)(t330)) gm_trap_index(t329, t330, gm_srckg9scnip7e8g, 117, 88);
                                uint64_t t331 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t329 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t326 + 16, t331);
                                uint64_t t332 = gm_l38;
                                uint64_t t333 = (uintptr_t)t332;
                                uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                if (t333 >= (uintptr_t)(t334)) gm_trap_index(t333, t334, gm_srckg9scnip7e8g, 117, 97);
                                uint64_t t335 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t333 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t326 + 24, t335);
                                gm_tftb1odc739t5 t336 = gm_f_write_7sl1fre(t325, &t326);
                                if (((((uint8_t *)&t336))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t336) + 1, gm_srckg9scnip7e8g, 117, 29);
                                gm_b0_1 t337 = GM_LD(gm_b0_1, (((uint8_t *)&t336) + 1));
                            }
                            goto me317;
mn318:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 115, 25);
me317:;
                        }
lc306:;
                        {
                            uint64_t t339 = gm_l37;
                            uint64_t r340; bool o341 = __builtin_add_overflow(t339, ((uint64_t)1ULL), &r340);
                            uint64_t t342 = r340;
                            gm_l37 = t342;
                        }
                    }
gm_ll310:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb306; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc306; }
                    }
                    goto gm_lad;
lb306:;
                }
            }
            {
                void * t343 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t344;
                GM_ST(void *, (uint8_t *)&t344, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t345 = gm_f_extend_0cs85gs(t343, t344);
                if (((((uint8_t *)&t345))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t345) + 1, gm_srckg9scnip7e8g, 119, 25);
                gm_b0_1 t346 = GM_LD(gm_b0_1, (((uint8_t *)&t345) + 1));
            }
ix287:;
        }
        goto me277;
mn283:;
ma279:;
        uint8_t t348 = GM_LD(uint8_t, ((((uint8_t *)t276) + 24) + 0));
        if (t348 != ((uint8_t)1ULL)) goto mn347;
        gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t276) + 24) + 8) + 0));
        gm_l41 = t349;
        gm_b16_8 t350 = GM_LD(gm_b16_8, (((((uint8_t *)t276) + 24) + 8) + 8));
        gm_l42 = t350;
        {
            void * t352 = gm_l1;
            gm_t88rcucg1b7uk t353 = gm_l2;
            gm_b16_8 t354 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t352, t353);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t354;
        }
        {
            void * t355 = gm_l1;
            gm_t88rcucg1b7uk t356 = gm_l41;
            gm_b16_8 t357 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t355, t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t357;
        }
        {
            void * t358 = (void *)((uint8_t *)&F.l5);
            gm_t1h4kdi7gs4v5 t359;
            gm_b16_8 t360 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t359 + 0, t360);
            gm_b16_8 t361 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t359 + 16, t361);
            uint64_t t362 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t363 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
            if (t362 >= (uintptr_t)(t363)) gm_trap_index(t362, t363, gm_srckg9scnip7e8g, 125, 97);
            uint64_t t364 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t362 * 8));
            GM_ST(uint64_t, (uint8_t *)&t359 + 32, t364);
            gm_b16_8 t365 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t359 + 40, t365);
            uint64_t t366 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
            if (t366 >= (uintptr_t)(t367)) gm_trap_index(t366, t367, gm_srckg9scnip7e8g, 125, 110);
            uint64_t t368 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t366 * 8));
            GM_ST(uint64_t, (uint8_t *)&t359 + 56, t368);
            gm_tftb1odc739t5 t369 = gm_f_write_0kurvf5(t358, &t359);
            if (((((uint8_t *)&t369))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t369) + 1, gm_srckg9scnip7e8g, 125, 21);
            gm_b0_1 t370 = GM_LD(gm_b0_1, (((uint8_t *)&t369) + 1));
        }
        {
            void * t371 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t372;
            GM_ST(void *, (uint8_t *)&t372, (void *)(gm_shm5oj4temhh6));
            GM_ST(uintptr_t, (uint8_t *)&t372 + 8, (uintptr_t)(32));
            gm_tftb1odc739t5 t373 = gm_f_extend_0cs85gs(t371, t372);
            if (((((uint8_t *)&t373))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t373) + 1, gm_srckg9scnip7e8g, 126, 21);
            gm_b0_1 t374 = GM_LD(gm_b0_1, (((uint8_t *)&t373) + 1));
        }
        {
            {
                gm_b16_8 t376 = gm_l42;
                gm_l45 = t376;
            }
            {
                gm_l46 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t378 = gm_l46;
                    uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                    uint8_t t380 = (t378 < t379);
                    if (!t380) goto lb377;
                    {
                        uint64_t t383 = gm_l46;
                        uint64_t t384 = (uintptr_t)t383;
                        uint64_t t385 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                        gm_tckdobunaulee t388 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t384 * 72));
                        F.l47 = t388;
                        {
                            if (!(((((uint8_t *)&F.l47) + 16))[4] != 0)) goto ie390;
                            gm_t88rcucg1b7uk t391 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l47) + 16));
                            gm_l48 = t391;
                            {
                                void * t393 = gm_l0;
                                void * t394 = gm_l1;
                                gm_t88rcucg1b7uk t395 = gm_l48;
                                gm_b16_8 t396 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t393, t394, t395);
                                if (gm_unw.kind) {
                                    goto gm_ll381;
                                }
                                gm_l49 = t396;
                            }
                            {
                                void * t397 = (void *)((uint8_t *)&F.l5);
                                gm_tg23pq9a29gh1 t398;
                                gm_b16_8 t399 = gm_l44;
                                GM_ST(gm_b16_8, (uint8_t *)&t398 + 0, t399);
                                gm_b16_8 t400 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l47) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll381;
                                }
                                gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                                GM_ST(gm_b16_8, (uint8_t *)&t398 + 16, t401);
                                gm_b16_8 t402 = gm_l49;
                                GM_ST(gm_b16_8, (uint8_t *)&t398 + 32, t402);
                                uint64_t t403 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t404 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                if (t403 >= (uintptr_t)(t404)) gm_trap_index(t403, t404, gm_srckg9scnip7e8g, 130, 126);
                                uint64_t t405 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t403 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t398 + 48, t405);
                                uint64_t t406 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t407 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                if (t406 >= (uintptr_t)(t407)) gm_trap_index(t406, t407, gm_srckg9scnip7e8g, 130, 135);
                                uint64_t t408 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t406 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t398 + 56, t408);
                                gm_tftb1odc739t5 t409 = gm_f_write_8ijr07s(t397, &t398);
                                if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 1, gm_srckg9scnip7e8g, 130, 29);
                                gm_b0_1 t410 = GM_LD(gm_b0_1, (((uint8_t *)&t409) + 1));
                            }
                            goto ix390;
ie390:;
ix390:;
                        }
                        goto me386;
mn387:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 127, 21);
me386:;
                    }
lc377:;
                    {
                        uint64_t t412 = gm_l46;
                        uint64_t r413; bool o414 = __builtin_add_overflow(t412, ((uint64_t)1ULL), &r413);
                        uint64_t t415 = r413;
                        gm_l46 = t415;
                    }
                }
gm_ll381:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb377; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc377; }
                }
                goto gm_lad;
lb377:;
            }
        }
        {
            void * t416 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t417;
            GM_ST(void *, (uint8_t *)&t417, (void *)(gm_shmmkvp83pr05));
            GM_ST(uintptr_t, (uint8_t *)&t417 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t418 = gm_f_extend_0cs85gs(t416, t417);
            if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 1, gm_srckg9scnip7e8g, 133, 21);
            gm_b0_1 t419 = GM_LD(gm_b0_1, (((uint8_t *)&t418) + 1));
        }
        goto me277;
mn347:;
ma280:;
        uint8_t t421 = GM_LD(uint8_t, ((((uint8_t *)t276) + 24) + 0));
        if (t421 != ((uint8_t)2ULL)) goto mn420;
        void * t422 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t423;
        GM_ST(void *, (uint8_t *)&t423, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t424 = gm_f_extend_0cs85gs(t422, t423);
        if (((((uint8_t *)&t424))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t424) + 1, gm_srckg9scnip7e8g, 135, 31);
        gm_b0_1 t425 = GM_LD(gm_b0_1, (((uint8_t *)&t424) + 1));
        goto me277;
mn420:;
ma281:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 108, 27);
me277:;
        goto me59;
mn272:;
ma66:;
        void * t427 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t428;
        GM_ST(void *, (uint8_t *)&t428, (void *)(gm_shmmkvp83pr05));
        GM_ST(uintptr_t, (uint8_t *)&t428 + 8, (uintptr_t)(17));
        gm_tftb1odc739t5 t429 = gm_f_extend_0cs85gs(t427, t428);
        if (((((uint8_t *)&t429))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t429) + 1, gm_srckg9scnip7e8g, 137, 18);
        gm_b0_1 t430 = GM_LD(gm_b0_1, (((uint8_t *)&t429) + 1));
        goto me59;
mn426:;
ma67:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 61, 14);
me59:;
        goto me25;
mn55:;
ma29:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 55, 5);
me25:;
    }
    {
        void * t431 = gm_l0;
        gm_b16_8 t432 = gm_l4;
        gm_b16_8 t433;
        GM_ST(void *, (uint8_t *)&t433, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t433 + 8, (uintptr_t)(4));
        gm_b16_8 t434;
        GM_ST(void *, (uint8_t *)&t434, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t434 + 8, (uintptr_t)(34));
        gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
        gm_b0_1 t437 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t431, t432, t433, t434, t436);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t438 = gm_l4;
        uint8_t t439 = 0;
        F.ret = t438;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hash_elems_7gnr39c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_hash_elems_7gnr39c F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    gm_b16_8 gm_l3; /* data */
    gm_b16_8 gm_l4; /* count */
    uint8_t gm_l5; /* signed */
    uint32_t gm_l6; /* bits */
    gm_b16_8 gm_l7; /* step */
    GM_STACK_CHECK(&F, 548u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t5v8n6qrn4rh4 t5;
        gm_f_repr_53quqki((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie2;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l5 = t7;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 4));
        gm_l6 = t8;
        uint8_t t9 = gm_l5;
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie2;
        uint32_t t11 = gm_l6;
        uint8_t t12 = ((t11 == ((uint32_t)8ULL)));
        if (!t12) goto ie2;
        {
            gm_t6j6smj7qoctu t14;
            gm_b16_8 t15 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
            gm_b16_8 t16 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
            gm_tabb710d9mq2e t17;
            gm_f_format_l1ii34d((void *)&t17, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srckg9scnip7e8g, 255, 16);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_t88rcucg1b7uk t23 = gm_l2;
        gm_t3877pk7lv748 t24;
        gm_b16_8 t25 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
        void * t26 = gm_l1;
        gm_t88rcucg1b7uk t27 = gm_l2;
        uint64_t t28 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t24 + 16, t28);
        gm_tabb710d9mq2e t29;
        gm_f_format_eqjc9ro((void *)&t29, (void *)NULL, cx, NULL, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srckg9scnip7e8g, 257, 35);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_b16_8 t32 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t21, t22, t23, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t32;
    }
    {
        gm_t6j6smj7qoctu t33;
        gm_b16_8 t34 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 0, t34);
        gm_b16_8 t35 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 16, t35);
        gm_tabb710d9mq2e t36;
        gm_f_format_8to6k7q((void *)&t36, (void *)NULL, cx, NULL, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srckg9scnip7e8g, 258, 12);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        uint8_t t39 = 0;
        F.ret = t38;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_hash_fn_nlk82uj F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* e */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    uint64_t gm_l14; /* len */
    gm_t88rcucg1b7uk gm_l15; /* elem */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_t88rcucg1b7uk gm_l17; /* inner */
    uint64_t gm_l18; /* flag */
    gm_t88rcucg1b7uk gm_l19; /* err */
    gm_t88rcucg1b7uk gm_l20; /* ok */
    gm_b16_8 gm_l21; /* offs */
    gm_b16_8 gm_l22; /* at */
    gm_t88rcucg1b7uk gm_l24; /* u */
    gm_b16_8 gm_l25; /* offs */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    gm_t88rcucg1b7uk gm_l30; /* tag */
    gm_b16_8 gm_l31; /* variants */
    gm_b16_8 gm_l32; /* offs */
    gm_b16_8 gm_l33; /* tc */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    gm_t88rcucg1b7uk gm_l38; /* pt */
    GM_STACK_CHECK(&F, 3237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 192);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l1;
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 192);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s9iu5gsqog7uc));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(8));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(28));
        gm_b16_8 t19 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t11, t12, t14, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uint64_t, (((((uint8_t *)t20) + 8) + 0) + 0));
        gm_l6 = t21;
    }
    {
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_ssdi9g220if8h));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
        gm_th2lqbo77pvsc t26;
        gm_f_hash_scalar_pt0pjre((void *)&t26, (void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t26))[16] != 0)) goto ie22;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_l7 = t27;
        {
            void * t29 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t30 = gm_l7;
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_tftb1odc739t5 t32 = gm_f_write_5ahb23t(t29, t31);
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srckg9scnip7e8g, 268, 9);
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        }
        {
            void * t34 = gm_l0;
            gm_b16_8 t35 = gm_l4;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_shn8mjp9kc2n2));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(8));
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s81r46mpa8ra9));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(28));
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_b0_1 t40 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t34, t35, t36, t37, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t41 = gm_l4;
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t43 = gm_l1;
        gm_t88rcucg1b7uk t44 = gm_l2;
        gm_t53at80pfdus3 t45;
        gm_f_kind_ghvav22((void *)&t45, t43, GM_CONV(gm_csguankko4lmhd, t44));
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        switch (t55) { case ((uint8_t)0ULL): goto ma53; case ((uint8_t)1ULL): goto ma53; case ((uint8_t)2ULL): goto ma53; case ((uint8_t)3ULL): goto ma53; case ((uint8_t)4ULL): goto ma53; case ((uint8_t)5ULL): goto ma53; case ((uint8_t)6ULL): goto ma53; case ((uint8_t)7ULL): goto ma53; case ((uint8_t)8ULL): goto ma53; case ((uint8_t)9ULL): goto ma53; case ((uint8_t)10ULL): goto ma49; case ((uint8_t)11ULL): goto ma48; case ((uint8_t)12ULL): goto ma47; case ((uint8_t)13ULL): goto ma50; case ((uint8_t)14ULL): goto ma51; case ((uint8_t)15ULL): goto ma53; case ((uint8_t)16ULL): goto ma53; case ((uint8_t)17ULL): goto ma52; case ((uint8_t)18ULL): goto ma53; case ((uint8_t)19ULL): goto ma53; case ((uint8_t)20ULL): goto ma53; case ((uint8_t)21ULL): goto ma53; case ((uint8_t)22ULL): goto ma53; case ((uint8_t)23ULL): goto ma53; case ((uint8_t)24ULL): goto ma53; case ((uint8_t)25ULL): goto ma53; case ((uint8_t)26ULL): goto ma53; default: goto ma53; }
ma47:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t57 != ((uint8_t)12ULL)) goto mn56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((((uint8_t *)&t45) + 8) + 0));
        gm_l8 = t58;
        {
            void * t60 = gm_l1;
            gm_t88rcucg1b7uk t61 = gm_l2;
            gm_b16_8 t62 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t62;
        }
        {
            {
                gm_b16_8 t64 = gm_l8;
                gm_l10 = t64;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t66 = gm_l11;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t68 = (t66 < t67);
                    if (!t68) goto lb65;
                    {
                        uint64_t t71 = gm_l11;
                        uint64_t t72 = gm_l11;
                        uint64_t t73 = (uintptr_t)t72;
                        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t73 * 4));
                        gm_tlcm4lvu3g1a8 t76;
                        GM_ST(uint32_t, (uint8_t *)&t76 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t76 + 0), t71);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76 + 8), t75);
                        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&t76) + 0));
                        gm_l12 = t79;
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t76) + 8));
                        gm_l13 = t80;
                        {
                            void * t82 = (void *)((uint8_t *)&F.l5);
                            void * t83 = gm_l0;
                            void * t84 = gm_l1;
                            gm_t88rcucg1b7uk t85 = gm_l13;
                            uint64_t t86 = gm_l12;
                            uint64_t t87 = (uintptr_t)t86;
                            uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t87 >= (uintptr_t)(t88)) gm_trap_index(t87, t88, gm_srckg9scnip7e8g, 276, 78);
                            uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t87 * 8));
                            gm_t4ce0jd3tdro4 t90;
                            GM_ST(uint64_t, ((uint8_t *)&t90 + 0), t89);
                            gm_tabb710d9mq2e t91;
                            gm_f_format_g3tqbla((void *)&t91, (void *)NULL, cx, NULL, t90);
                            if (gm_unw.kind) {
                                goto gm_ll69;
                            }
                            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_srckg9scnip7e8g, 276, 57);
                            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                            gm_b16_8 t94 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t83, t84, t85, t93);
                            if (gm_unw.kind) {
                                goto gm_ll69;
                            }
                            gm_tnpifv20irnph t95;
                            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t94);
                            gm_tftb1odc739t5 t96 = gm_f_write_aaqg4k2(t82, t95);
                            if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srckg9scnip7e8g, 276, 17);
                            gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
                        }
                        goto me77;
mn78:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 275, 13);
me77:;
                    }
lc65:;
                    {
                        uint64_t t99 = gm_l11;
                        uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                        uint64_t t102 = r100;
                        gm_l11 = t102;
                    }
                }
gm_ll69:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb65; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc65; }
                }
                goto gm_lad;
lb65:;
            }
        }
        goto me46;
mn56:;
ma48:;
        uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t104 != ((uint8_t)11ULL)) goto mn103;
        uint64_t t105 = GM_LD(uint64_t, ((((uint8_t *)&t45) + 8) + 0));
        gm_l14 = t105;
        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 8));
        gm_l15 = t106;
        void * t107 = (void *)((uint8_t *)&F.l5);
        void * t108 = gm_l0;
        void * t109 = gm_l1;
        gm_t88rcucg1b7uk t110 = gm_l15;
        gm_b16_8 t111;
        GM_ST(void *, (uint8_t *)&t111, (void *)(gm_ssdi9g220if8h));
        GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(1));
        uint64_t t112 = gm_l14;
        gm_t4ce0jd3tdro4 t113;
        GM_ST(uint64_t, ((uint8_t *)&t113 + 0), t112);
        gm_tabb710d9mq2e t114;
        gm_f_format_1amnh72((void *)&t114, (void *)NULL, cx, NULL, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srckg9scnip7e8g, 279, 70);
        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        gm_b16_8 t117 = gm_f_hash_elems_7gnr39c((void *)NULL, cx, NULL, t108, t109, t110, t111, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t118 = gm_f_extend_0cs85gs(t107, t117);
        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srckg9scnip7e8g, 279, 33);
        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
        goto me46;
mn103:;
ma49:;
        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t121 != ((uint8_t)10ULL)) goto mn120;
        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 4));
        gm_l16 = t122;
        {
            void * t124 = (void *)((uint8_t *)&F.l5);
            uint64_t t125 = gm_l6;
            gm_t4ce0jd3tdro4 t126;
            GM_ST(uint64_t, ((uint8_t *)&t126 + 0), t125);
            gm_tftb1odc739t5 t127 = gm_f_write_k9lbjqb(t124, t126);
            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srckg9scnip7e8g, 281, 13);
            gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
        }
        {
            void * t129 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sfm0hseeqersg));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(24));
            gm_tftb1odc739t5 t131 = gm_f_extend_0cs85gs(t129, t130);
            if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 1, gm_srckg9scnip7e8g, 282, 13);
            gm_b0_1 t132 = GM_LD(gm_b0_1, (((uint8_t *)&t131) + 1));
        }
        {
            void * t133 = (void *)((uint8_t *)&F.l5);
            void * t134 = gm_l0;
            void * t135 = gm_l1;
            gm_t88rcucg1b7uk t136 = gm_l16;
            gm_b16_8 t137;
            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_stafskn6bv24f));
            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(1));
            gm_b16_8 t138;
            GM_ST(void *, (uint8_t *)&t138, (void *)(gm_smnfu5n5pum1l));
            GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(1));
            gm_b16_8 t139 = gm_f_hash_elems_7gnr39c((void *)NULL, cx, NULL, t134, t135, t136, t137, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t133, t139);
            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 1, gm_srckg9scnip7e8g, 283, 13);
            gm_b0_1 t141 = GM_LD(gm_b0_1, (((uint8_t *)&t140) + 1));
        }
        goto me46;
mn120:;
ma50:;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t143 != ((uint8_t)13ULL)) goto mn142;
        gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t45) + 8));
        gm_l17 = t144;
        {
            uint64_t t146 = (uintptr_t)((uint64_t)1ULL);
            void * t147 = gm_l1;
            gm_t88rcucg1b7uk t148 = gm_l2;
            gm_b16_8 t149 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&t149) + 8);
            if (t146 >= (uintptr_t)(t150)) gm_trap_index(t146, t150, gm_srckg9scnip7e8g, 286, 21);
            uint64_t t151 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t149)) + t146 * 8));
            gm_l18 = t151;
        }
        {
            void * t152 = (void *)((uint8_t *)&F.l5);
            uint64_t t153 = gm_l18;
            gm_t4ce0jd3tdro4 t154;
            GM_ST(uint64_t, ((uint8_t *)&t154 + 0), t153);
            gm_tftb1odc739t5 t155 = gm_f_write_4unpkmv(t152, t154);
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srckg9scnip7e8g, 287, 13);
            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
        }
        {
            void * t157 = (void *)((uint8_t *)&F.l5);
            void * t158 = gm_l0;
            void * t159 = gm_l1;
            gm_t88rcucg1b7uk t160 = gm_l17;
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_ssdi9g220if8h));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(1));
            gm_b16_8 t162 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t158, t159, t160, t161);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t163;
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t162);
            gm_tftb1odc739t5 t164 = gm_f_write_jfl63ic(t157, t163);
            if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_srckg9scnip7e8g, 288, 13);
            gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
        }
        goto me46;
mn142:;
ma51:;
        uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t167 != ((uint8_t)14ULL)) goto mn166;
        gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 0));
        gm_l19 = t168;
        gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 4));
        gm_l20 = t169;
        {
            void * t171 = gm_l1;
            gm_t88rcucg1b7uk t172 = gm_l2;
            gm_b16_8 t173 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t171, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t173;
        }
        {
            void * t174 = (void *)((uint8_t *)&F.l5);
            uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t176 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
            if (t175 >= (uintptr_t)(t176)) gm_trap_index(t175, t176, gm_srckg9scnip7e8g, 292, 74);
            uint64_t t177 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t175 * 8));
            gm_t4ce0jd3tdro4 t178;
            GM_ST(uint64_t, ((uint8_t *)&t178 + 0), t177);
            gm_tftb1odc739t5 t179 = gm_f_write_7k4ript(t174, t178);
            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srckg9scnip7e8g, 292, 13);
            gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
        }
        {
            uint64_t t181 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
            if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_srckg9scnip7e8g, 293, 40);
            uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t181 * 8));
            gm_t4ce0jd3tdro4 t184;
            GM_ST(uint64_t, ((uint8_t *)&t184 + 0), t183);
            gm_tabb710d9mq2e t185;
            gm_f_format_g3tqbla((void *)&t185, (void *)NULL, cx, NULL, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_srckg9scnip7e8g, 293, 19);
            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
            gm_l22 = t186;
        }
        {
            void * t187 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t188;
            void * t189 = gm_l0;
            void * t190 = gm_l1;
            gm_t88rcucg1b7uk t191 = gm_l19;
            gm_b16_8 t192 = gm_l22;
            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
            gm_b16_8 t194 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t189, t190, t191, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t188 + 0, t194);
            void * t195 = gm_l0;
            void * t196 = gm_l1;
            gm_t88rcucg1b7uk t197 = gm_l20;
            gm_b16_8 t198 = gm_l22;
            gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
            gm_b16_8 t200 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t195, t196, t197, t199);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t188 + 16, t200);
            gm_tftb1odc739t5 t201 = gm_f_write_ltl9pvt(t187, &t188);
            if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srckg9scnip7e8g, 294, 13);
            gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
        }
        goto me46;
mn166:;
ma52:;
        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t204 != ((uint8_t)17ULL)) goto mn203;
        void * t205 = gm_l1;
        gm_t88rcucg1b7uk t206 = gm_l2;
        void * t207 = gm_f_adt_of_gej99vn(t205, GM_CONV(gm_csguankko4lmhd, t206));
        uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        switch (t213) { case ((uint8_t)0ULL): goto ma209; case ((uint8_t)1ULL): goto ma210; case ((uint8_t)2ULL): goto ma211; default: goto ma212; }
ma209:;
        uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        if (t215 != ((uint8_t)0ULL)) goto mn214;
        gm_tpautr4br7h3l t216 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t207) + 24) + 8));
        F.l23 = t216;
        {
            if (!(((((uint8_t *)&F.l23) + 40))[4] != 0)) goto ie218;
            gm_t88rcucg1b7uk t219 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l23) + 40));
            gm_l24 = t219;
            {
                void * t221 = (void *)((uint8_t *)&F.l5);
                void * t222 = gm_l0;
                void * t223 = gm_l1;
                gm_t88rcucg1b7uk t224 = gm_l24;
                gm_b16_8 t225;
                GM_ST(void *, (uint8_t *)&t225, (void *)(gm_ssdi9g220if8h));
                GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(1));
                gm_b16_8 t226 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t222, t223, t224, t225);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t227;
                GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t226);
                gm_tftb1odc739t5 t228 = gm_f_write_aaqg4k2(t221, t227);
                if (((((uint8_t *)&t228))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t228) + 1, gm_srckg9scnip7e8g, 299, 21);
                gm_b0_1 t229 = GM_LD(gm_b0_1, (((uint8_t *)&t228) + 1));
            }
            goto ix218;
ie218:;
            {
                void * t231 = gm_l1;
                gm_t88rcucg1b7uk t232 = gm_l2;
                gm_b16_8 t233 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t231, t232);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t233;
            }
            {
                {
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                    gm_l26 = t236;
                }
                {
                    gm_l27 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t238 = gm_l27;
                        uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                        uint8_t t240 = (t238 < t239);
                        if (!t240) goto lb237;
                        {
                            gm_t8ta482bl6be0 t243;
                            uint64_t t244 = gm_l27;
                            GM_ST(uint64_t, (uint8_t *)&t243 + 0, t244);
                            uint64_t t245 = gm_l27;
                            uint64_t t246 = (uintptr_t)t245;
                            uint64_t t247 = GM_LD(uintptr_t, ((uint8_t *)&gm_l26) + 8);
                            memcpy((uint8_t *)&t243 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l26)) + t246 * 40), 40);
                            uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)&t243) + 0));
                            gm_l28 = t250;
                            F.l29 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t243) + 8));
                            {
                                void * t252 = (void *)((uint8_t *)&F.l5);
                                void * t253 = gm_l0;
                                void * t254 = gm_l1;
                                gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 16));
                                uint64_t t256 = gm_l28;
                                uint64_t t257 = (uintptr_t)t256;
                                uint64_t t258 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                                if (t257 >= (uintptr_t)(t258)) gm_trap_index(t257, t258, gm_srckg9scnip7e8g, 303, 89);
                                uint64_t t259 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t257 * 8));
                                gm_t4ce0jd3tdro4 t260;
                                GM_ST(uint64_t, ((uint8_t *)&t260 + 0), t259);
                                gm_tabb710d9mq2e t261;
                                gm_f_format_g3tqbla((void *)&t261, (void *)NULL, cx, NULL, t260);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                                if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 8, gm_srckg9scnip7e8g, 303, 68);
                                gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&t261) + 8));
                                gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                                gm_b16_8 t264 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t253, t254, t255, t263);
                                if (gm_unw.kind) {
                                    goto gm_ll241;
                                }
                                gm_tnpifv20irnph t265;
                                GM_ST(gm_b16_8, ((uint8_t *)&t265 + 0), t264);
                                gm_tftb1odc739t5 t266 = gm_f_write_aaqg4k2(t252, t265);
                                if (((((uint8_t *)&t266))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t266) + 1, gm_srckg9scnip7e8g, 303, 25);
                                gm_b0_1 t267 = GM_LD(gm_b0_1, (((uint8_t *)&t266) + 1));
                            }
                            goto me248;
mn249:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 302, 21);
me248:;
                        }
lc237:;
                        {
                            uint64_t t269 = gm_l27;
                            uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                            uint64_t t272 = r270;
                            gm_l27 = t272;
                        }
                    }
gm_ll241:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb237; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc237; }
                    }
                    goto gm_lad;
lb237:;
                }
            }
ix218:;
        }
        goto me208;
mn214:;
ma210:;
        uint8_t t274 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        if (t274 != ((uint8_t)1ULL)) goto mn273;
        gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t207) + 24) + 8) + 0));
        gm_l30 = t275;
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((((uint8_t *)t207) + 24) + 8) + 8));
        gm_l31 = t276;
        {
            void * t278 = gm_l1;
            gm_t88rcucg1b7uk t279 = gm_l2;
            gm_b16_8 t280 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t278, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t280;
        }
        {
            void * t281 = gm_l1;
            gm_t88rcucg1b7uk t282 = gm_l30;
            gm_b16_8 t283 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t281, t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t283;
        }
        {
            void * t284 = (void *)((uint8_t *)&F.l5);
            gm_tftksr4op114e t285;
            gm_b16_8 t286 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t285 + 0, t286);
            gm_b16_8 t287 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t285 + 16, t287);
            uint64_t t288 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
            if (t288 >= (uintptr_t)(t289)) gm_trap_index(t288, t289, gm_srckg9scnip7e8g, 310, 69);
            uint64_t t290 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t288 * 8));
            GM_ST(uint64_t, (uint8_t *)&t285 + 32, t290);
            gm_tftb1odc739t5 t291 = gm_f_write_551e4in(t284, &t285);
            if (((((uint8_t *)&t291))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t291) + 1, gm_srckg9scnip7e8g, 310, 17);
            gm_b0_1 t292 = GM_LD(gm_b0_1, (((uint8_t *)&t291) + 1));
        }
        {
            {
                gm_b16_8 t294 = gm_l31;
                gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                gm_l34 = t295;
            }
            {
                gm_l35 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t297 = gm_l35;
                    uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    uint8_t t299 = (t297 < t298);
                    if (!t299) goto lb296;
                    {
                        gm_t3l8idb2m6c1r t302;
                        uint64_t t303 = gm_l35;
                        GM_ST(uint64_t, (uint8_t *)&t302 + 0, t303);
                        uint64_t t304 = gm_l35;
                        uint64_t t305 = (uintptr_t)t304;
                        uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        memcpy((uint8_t *)&t302 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t305 * 72), 72);
                        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&t302) + 0));
                        gm_l36 = t309;
                        F.l37 = (*(gm_tckdobunaulee *)(((uint8_t *)&t302) + 8));
                        {
                            void * t311 = (void *)((uint8_t *)&F.l5);
                            gm_t6j6smj7qoctu t312;
                            gm_b16_8 t313 = gm_l33;
                            GM_ST(gm_b16_8, (uint8_t *)&t312 + 0, t313);
                            gm_b16_8 t314 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l37) + 24)));
                            if (gm_unw.kind) {
                                goto gm_ll300;
                            }
                            gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                            GM_ST(gm_b16_8, (uint8_t *)&t312 + 16, t315);
                            gm_tftb1odc739t5 t316 = gm_f_write_2tci68e(t311, &t312);
                            if (((((uint8_t *)&t316))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t316) + 1, gm_srckg9scnip7e8g, 312, 21);
                            gm_b0_1 t317 = GM_LD(gm_b0_1, (((uint8_t *)&t316) + 1));
                        }
                        {
                            void * t318 = (void *)((uint8_t *)&F.l5);
                            uint64_t t319 = gm_l36;
                            gm_t4ce0jd3tdro4 t320;
                            GM_ST(uint64_t, ((uint8_t *)&t320 + 0), t319);
                            gm_tftb1odc739t5 t321 = gm_f_write_bjaiklv(t318, t320);
                            if (((((uint8_t *)&t321))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t321) + 1, gm_srckg9scnip7e8g, 313, 21);
                            gm_b0_1 t322 = GM_LD(gm_b0_1, (((uint8_t *)&t321) + 1));
                        }
                        {
                            if (!(((((uint8_t *)&F.l37) + 16))[4] != 0)) goto ie323;
                            gm_t88rcucg1b7uk t324 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l37) + 16));
                            gm_l38 = t324;
                            {
                                void * t326 = (void *)((uint8_t *)&F.l5);
                                void * t327 = gm_l0;
                                void * t328 = gm_l1;
                                gm_t88rcucg1b7uk t329 = gm_l38;
                                uint64_t t330 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t331 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                if (t330 >= (uintptr_t)(t331)) gm_trap_index(t330, t331, gm_srckg9scnip7e8g, 315, 90);
                                uint64_t t332 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t330 * 8));
                                gm_t4ce0jd3tdro4 t333;
                                GM_ST(uint64_t, ((uint8_t *)&t333 + 0), t332);
                                gm_tabb710d9mq2e t334;
                                gm_f_format_g3tqbla((void *)&t334, (void *)NULL, cx, NULL, t333);
                                if (gm_unw.kind) {
                                    goto gm_ll300;
                                }
                                if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 8, gm_srckg9scnip7e8g, 315, 69);
                                gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&t334) + 8));
                                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                                gm_b16_8 t337 = gm_f_hash_step_ari7fns((void *)NULL, cx, NULL, t327, t328, t329, t336);
                                if (gm_unw.kind) {
                                    goto gm_ll300;
                                }
                                gm_tnpifv20irnph t338;
                                GM_ST(gm_b16_8, ((uint8_t *)&t338 + 0), t337);
                                gm_tftb1odc739t5 t339 = gm_f_write_q1ni37m(t326, t338);
                                if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 1, gm_srckg9scnip7e8g, 315, 25);
                                gm_b0_1 t340 = GM_LD(gm_b0_1, (((uint8_t *)&t339) + 1));
                            }
                            goto ix323;
ie323:;
ix323:;
                        }
                        {
                            void * t341 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t342;
                            GM_ST(void *, (uint8_t *)&t342, (void *)(gm_sciq0a78hirud));
                            GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(24));
                            gm_tftb1odc739t5 t343 = gm_f_extend_0cs85gs(t341, t342);
                            if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 1, gm_srckg9scnip7e8g, 317, 21);
                            gm_b0_1 t344 = GM_LD(gm_b0_1, (((uint8_t *)&t343) + 1));
                        }
                        goto me307;
mn308:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 311, 17);
me307:;
                    }
lc296:;
                    {
                        uint64_t t346 = gm_l35;
                        uint64_t r347; bool o348 = __builtin_add_overflow(t346, ((uint64_t)1ULL), &r347);
                        uint64_t t349 = r347;
                        gm_l35 = t349;
                    }
                }
gm_ll300:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb296; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc296; }
                }
                goto gm_lad;
lb296:;
            }
        }
        goto me208;
mn273:;
ma211:;
        uint8_t t351 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        if (t351 != ((uint8_t)2ULL)) goto mn350;
        goto me208;
mn350:;
ma212:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 296, 23);
me208:;
        goto me46;
mn203:;
ma53:;
        goto me46;
mn353:;
ma54:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 272, 5);
me46:;
    }
    {
        void * t355 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t356;
        GM_ST(void *, (uint8_t *)&t356, (void *)(gm_sel1fj24qk32p));
        GM_ST(uintptr_t, (uint8_t *)&t356 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t357 = gm_f_extend_0cs85gs(t355, t356);
        if (((((uint8_t *)&t357))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t357) + 1, gm_srckg9scnip7e8g, 324, 5);
        gm_b0_1 t358 = GM_LD(gm_b0_1, (((uint8_t *)&t357) + 1));
    }
    {
        void * t359 = gm_l0;
        gm_b16_8 t360 = gm_l4;
        gm_b16_8 t361;
        GM_ST(void *, (uint8_t *)&t361, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(8));
        gm_b16_8 t362;
        GM_ST(void *, (uint8_t *)&t362, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(28));
        gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&t363));
        gm_b0_1 t365 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t359, t360, t361, t362, t364);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t366 = gm_l4;
        uint8_t t367 = 0;
        F.ret = t366;
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
__attribute__((aligned(4))) void gm_f_hash_scalar_pt0pjre(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_hash_scalar_pt0pjre F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* at */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* storage */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* wide */
    GM_STACK_CHECK(&F, 842u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t5v8n6qrn4rh4 t4;
        gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; default: goto ma10; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l3 = t15;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 8));
        gm_l4 = t16;
        {
            void * t18 = gm_l0;
            gm_t88rcucg1b7uk t19 = gm_l1;
            gm_b16_8 t20 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t20;
        }
        {
            uint64_t t22 = gm_l4;
            uint8_t t23 = (t22 > ((uint64_t)8ULL));
            if (!t23) goto ie21;
            {
                gm_t6j6smj7qoctu t25;
                gm_b16_8 t26 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
                gm_b16_8 t27 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
                gm_tabb710d9mq2e t28;
                gm_f_format_5f2vpdc((void *)&t28, (void *)NULL, cx, NULL, &t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srckg9scnip7e8g, 234, 37);
                gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t30);
                goto gm_fdone;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            gm_b16_8 t31;
            uint8_t t33 = gm_l3;
            if (!t33) goto ie32;
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sl6pi8la5phb6));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(19));
            gm_b16_8 t36 = t35;
            t31 = t36;
            goto ix32;
ie32:;
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_slq0l63qf1dvs));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(10));
            gm_b16_8 t39 = t38;
            t31 = t39;
ix32:;
            gm_l6 = t31;
        }
        {
            gm_tfeab279c16rr t40;
            gm_b16_8 t41 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t40 + 0, t41);
            gm_b16_8 t42 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t40 + 16, t42);
            gm_b16_8 t43 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t40 + 32, t43);
            gm_tabb710d9mq2e t44;
            gm_f_format_lovbvbi((void *)&t44, (void *)NULL, cx, NULL, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srckg9scnip7e8g, 236, 20);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t46);
            goto gm_fdone;
        }
        goto me5;
mn13:;
ma7:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t48 != ((uint8_t)1ULL)) goto mn47;
        gm_t6j6smj7qoctu t49;
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_l1;
        gm_b16_8 t52 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t52);
        gm_b16_8 t53 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t49 + 16, t53);
        gm_tabb710d9mq2e t54;
        gm_f_format_4hhihlj((void *)&t54, (void *)NULL, cx, NULL, &t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srckg9scnip7e8g, 238, 29);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t56);
        goto gm_fdone;
        goto me5;
mn47:;
ma8:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t58 != ((uint8_t)2ULL)) goto mn57;
        gm_b16_8 t59 = gm_l2;
        gm_tnpifv20irnph t60;
        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
        gm_tabb710d9mq2e t61;
        gm_f_format_me0dr2l((void *)&t61, (void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srckg9scnip7e8g, 239, 25);
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t63);
        goto gm_fdone;
        goto me5;
mn57:;
ma9:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t65 != ((uint8_t)3ULL)) goto mn64;
        gm_b16_8 t66 = gm_l2;
        gm_tnpifv20irnph t67;
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
        gm_tabb710d9mq2e t68;
        gm_f_format_vd1blml((void *)&t68, (void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srckg9scnip7e8g, 240, 24);
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t70);
        goto gm_fdone;
        goto me5;
mn64:;
ma10:;
        gm_th2lqbo77pvsc t72;
        memset(&t72, 0, sizeof t72);
        uint8_t t73 = 0;
        F.ret = t72;
        goto gm_fret;
        goto me5;
mn71:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 231, 5);
me5:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hash_step_ari7fns(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_hash_step_ari7fns F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* at */
    gm_b16_8 gm_l4; /* e */
    GM_STACK_CHECK(&F, 382u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_b16_8 t5 = gm_l3;
        gm_th2lqbo77pvsc t6;
        gm_f_hash_scalar_pt0pjre((void *)&t6, (void *)NULL, cx, NULL, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l4 = t7;
        {
            gm_b16_8 t9 = gm_l4;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_8nj2557((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srckg9scnip7e8g, 247, 45);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t15;
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        gm_t88rcucg1b7uk t18 = gm_l2;
        gm_b16_8 t19 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t19);
        gm_b16_8 t20 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t20);
        gm_tabb710d9mq2e t21;
        gm_f_format_edav5jt((void *)&t21, (void *)NULL, cx, NULL, &t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srckg9scnip7e8g, 248, 12);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        uint8_t t24 = 0;
        F.ret = t23;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_less_fn_64um0ab F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* fe */
    gm_b16_8 gm_l15; /* fl */
    uint64_t gm_l16; /* o */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_tf5srh4mp4b3i gm_l18; /* array_len */
    uint64_t gm_l19; /* len */
    gm_t88rcucg1b7uk gm_l20; /* el */
    gm_t88rcucg1b7uk gm_l21; /* el */
    uint64_t gm_l22; /* esz */
    uint64_t gm_l23; /* n */
    gm_b16_8 gm_l24; /* fe */
    gm_b16_8 gm_l25; /* fl */
    gm_t88rcucg1b7uk gm_l26; /* tag */
    gm_b16_8 gm_l27; /* tc */
    uint64_t gm_l28; /* o */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_b16_8 gm_l31; /* f */
    GM_STACK_CHECK(&F, 2299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 136);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l3 = t7;
        {
            gm_b16_8 t9 = gm_l3;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l1;
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 136);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_surrjo1qo53mh));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t16 = gm_l2;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(34));
        gm_b16_8 t19 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t11, t12, t14, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t20 = gm_l1;
        uint64_t t21 = GM_LD(uint64_t, (((((uint8_t *)t20) + 8) + 0) + 0));
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l1;
        gm_t88rcucg1b7uk t23 = gm_l2;
        gm_t5v8n6qrn4rh4 t24;
        gm_f_repr_53quqki((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t29 != ((uint8_t)0ULL)) goto pa28;
        goto po27;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t31 != ((uint8_t)1ULL)) goto pa30;
        goto po27;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t33 != ((uint8_t)3ULL)) goto pa32;
        goto po27;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        if (t35 != ((uint8_t)2ULL)) goto pa34;
        goto po27;
pa34:;
        goto mn26;
po27:;
        {
            void * t37 = gm_l1;
            gm_t88rcucg1b7uk t38 = gm_l2;
            gm_b16_8 t39 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t39;
        }
        {
            void * t40 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t41;
            gm_b16_8 t42 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t42);
            gm_b16_8 t43 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t43);
            gm_tftb1odc739t5 t44 = gm_f_write_ikd2l7g(t40, &t41);
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srckg9scnip7e8g, 160, 13);
            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
        }
        goto me25;
mn26:;
        void * t47 = gm_l1;
        gm_t88rcucg1b7uk t48 = gm_l2;
        gm_t53at80pfdus3 t49;
        gm_f_kind_ghvav22((void *)&t49, t47, GM_CONV(gm_csguankko4lmhd, t48));
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        switch (t56) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma54; case ((uint8_t)7ULL): goto ma54; case ((uint8_t)8ULL): goto ma54; case ((uint8_t)9ULL): goto ma54; case ((uint8_t)10ULL): goto ma52; case ((uint8_t)11ULL): goto ma52; case ((uint8_t)12ULL): goto ma51; case ((uint8_t)13ULL): goto ma54; case ((uint8_t)14ULL): goto ma54; case ((uint8_t)15ULL): goto ma54; case ((uint8_t)16ULL): goto ma54; case ((uint8_t)17ULL): goto ma53; case ((uint8_t)18ULL): goto ma54; case ((uint8_t)19ULL): goto ma54; case ((uint8_t)20ULL): goto ma54; case ((uint8_t)21ULL): goto ma54; case ((uint8_t)22ULL): goto ma54; case ((uint8_t)23ULL): goto ma54; case ((uint8_t)24ULL): goto ma54; case ((uint8_t)25ULL): goto ma54; case ((uint8_t)26ULL): goto ma54; default: goto ma54; }
ma51:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t58 != ((uint8_t)12ULL)) goto mn57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&t49) + 8) + 0));
        gm_l8 = t59;
        {
            void * t61 = gm_l1;
            gm_t88rcucg1b7uk t62 = gm_l2;
            gm_b16_8 t63 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t63;
        }
        {
            {
                gm_b16_8 t65 = gm_l8;
                gm_l10 = t65;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t67 = gm_l11;
                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t69 = (t67 < t68);
                    if (!t69) goto lb66;
                    {
                        uint64_t t72 = gm_l11;
                        uint64_t t73 = gm_l11;
                        uint64_t t74 = (uintptr_t)t73;
                        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t74 * 4));
                        gm_tlcm4lvu3g1a8 t77;
                        GM_ST(uint32_t, (uint8_t *)&t77 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t77 + 0), t72);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t77 + 8), t76);
                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&t77) + 0));
                        gm_l12 = t80;
                        gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t77) + 8));
                        gm_l13 = t81;
                        {
                            void * t83 = gm_l0;
                            void * t84 = gm_l1;
                            gm_t88rcucg1b7uk t85 = gm_l13;
                            gm_b16_8 t86 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t83, t84, t85);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                            gm_l14 = t86;
                        }
                        {
                            void * t87 = gm_l0;
                            void * t88 = gm_l1;
                            gm_t88rcucg1b7uk t89 = gm_l13;
                            gm_b16_8 t90 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t87, t88, t89);
                            if (gm_unw.kind) {
                                goto gm_ll70;
                            }
                            gm_l15 = t90;
                        }
                        {
                            uint64_t t91 = gm_l12;
                            uint64_t t92 = (uintptr_t)t91;
                            uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_srckg9scnip7e8g, 168, 26);
                            uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t92 * 8));
                            gm_l16 = t94;
                        }
                        {
                            void * t95 = (void *)((uint8_t *)&F.l5);
                            gm_tr4ummvbupakb t96;
                            gm_b16_8 t97 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t96 + 0, t97);
                            uint64_t t98 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t96 + 16, t98);
                            uint64_t t99 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t96 + 24, t99);
                            gm_b16_8 t100 = gm_l15;
                            GM_ST(gm_b16_8, (uint8_t *)&t96 + 32, t100);
                            uint64_t t101 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t96 + 48, t101);
                            uint64_t t102 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t96 + 56, t102);
                            gm_tftb1odc739t5 t103 = gm_f_write_e15chue(t95, &t96);
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srckg9scnip7e8g, 169, 21);
                            gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
                        }
                        goto me78;
mn79:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 165, 17);
me78:;
                    }
lc66:;
                    {
                        uint64_t t106 = gm_l11;
                        uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                        uint64_t t109 = r107;
                        gm_l11 = t109;
                    }
                }
gm_ll70:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
                }
                goto gm_lad;
lb66:;
            }
        }
        {
            void * t110 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sug64ltbhov0m));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t112 = gm_f_extend_0cs85gs(t110, t111);
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_srckg9scnip7e8g, 171, 17);
            gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
        }
        goto me50;
mn57:;
ma52:;
        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t117 != ((uint8_t)11ULL)) goto pa116;
        goto po115;
pa116:;
        uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t119 != ((uint8_t)10ULL)) goto pa118;
        goto po115;
pa118:;
        goto mn114;
po115:;
        {
            gm_t88rcucg1b7uk t121 = gm_l2;
            gm_l17 = t121;
        }
        {
            gm_tf5srh4mp4b3i t122;
            memset(&t122, 0, sizeof t122);
            gm_l18 = t122;
        }
        {
            void * t123 = gm_l1;
            gm_t88rcucg1b7uk t124 = gm_l2;
            gm_t53at80pfdus3 t125;
            gm_f_kind_ghvav22((void *)&t125, t123, GM_CONV(gm_csguankko4lmhd, t124));
            uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&t125) + 0));
            switch (t131) { case ((uint8_t)0ULL): goto ma129; case ((uint8_t)1ULL): goto ma129; case ((uint8_t)2ULL): goto ma129; case ((uint8_t)3ULL): goto ma129; case ((uint8_t)4ULL): goto ma129; case ((uint8_t)5ULL): goto ma129; case ((uint8_t)6ULL): goto ma129; case ((uint8_t)7ULL): goto ma129; case ((uint8_t)8ULL): goto ma129; case ((uint8_t)9ULL): goto ma129; case ((uint8_t)10ULL): goto ma128; case ((uint8_t)11ULL): goto ma127; case ((uint8_t)12ULL): goto ma129; case ((uint8_t)13ULL): goto ma129; case ((uint8_t)14ULL): goto ma129; case ((uint8_t)15ULL): goto ma129; case ((uint8_t)16ULL): goto ma129; case ((uint8_t)17ULL): goto ma129; case ((uint8_t)18ULL): goto ma129; case ((uint8_t)19ULL): goto ma129; case ((uint8_t)20ULL): goto ma129; case ((uint8_t)21ULL): goto ma129; case ((uint8_t)22ULL): goto ma129; case ((uint8_t)23ULL): goto ma129; case ((uint8_t)24ULL): goto ma129; case ((uint8_t)25ULL): goto ma129; case ((uint8_t)26ULL): goto ma129; default: goto ma129; }
ma127:;
            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t125) + 0));
            if (t133 != ((uint8_t)11ULL)) goto mn132;
            uint64_t t134 = GM_LD(uint64_t, ((((uint8_t *)&t125) + 8) + 0));
            gm_l19 = t134;
            gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t125) + 8) + 8));
            gm_l20 = t135;
            {
                gm_t88rcucg1b7uk t137 = gm_l20;
                gm_l17 = t137;
            }
            {
                uint64_t t138 = gm_l19;
                gm_tf5srh4mp4b3i t139;
                GM_ST(uint8_t, (uint8_t *)&t139 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t139 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t139 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t139), t138);
                ((uint8_t *)&t139)[8] = 1;
                gm_l18 = t139;
            }
            goto me126;
mn132:;
ma128:;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t125) + 0));
            if (t141 != ((uint8_t)10ULL)) goto mn140;
            gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t125) + 8) + 4));
            gm_l21 = t142;
            {
                gm_t88rcucg1b7uk t144 = gm_l21;
                gm_l17 = t144;
            }
            goto me126;
mn140:;
ma129:;
            gm_trap("reached `@unreachable()`", gm_srckg9scnip7e8g, 182, 26);
            goto me126;
mn145:;
ma130:;
            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 176, 17);
me126:;
        }
        {
            void * t146 = gm_l1;
            gm_t88rcucg1b7uk t147 = gm_l17;
            uint64_t t148 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t148;
        }
        {
            if (!((((uint8_t *)&gm_l18))[8] != 0)) goto ie149;
            uint64_t t150 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
            gm_l23 = t150;
            {
                void * t152 = (void *)((uint8_t *)&F.l5);
                uint64_t t153 = gm_l23;
                uint64_t t154 = gm_l23;
                gm_t2lqu8jsj511d t155;
                GM_ST(uint64_t, ((uint8_t *)&t155 + 0), t153);
                GM_ST(uint64_t, ((uint8_t *)&t155 + 8), t154);
                gm_tftb1odc739t5 t156 = gm_f_write_8tfqc0p(t152, t155);
                if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_srckg9scnip7e8g, 186, 21);
                gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
            }
            goto ix149;
ie149:;
            {
                void * t159 = (void *)((uint8_t *)&F.l5);
                uint64_t t160 = gm_l6;
                uint64_t t161 = gm_l6;
                gm_t2lqu8jsj511d t162;
                GM_ST(uint64_t, ((uint8_t *)&t162 + 0), t160);
                GM_ST(uint64_t, ((uint8_t *)&t162 + 8), t161);
                gm_tftb1odc739t5 t163 = gm_f_write_k88klp2(t159, t162);
                if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srckg9scnip7e8g, 188, 21);
                gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
            }
            {
                void * t165 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t166;
                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sor8flejv9lfq));
                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(66));
                gm_tftb1odc739t5 t167 = gm_f_extend_0cs85gs(t165, t166);
                if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srckg9scnip7e8g, 189, 21);
                gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
            }
ix149:;
        }
        {
            void * t170 = gm_l1;
            gm_t88rcucg1b7uk t171 = gm_l17;
            gm_t5v8n6qrn4rh4 t172;
            gm_f_repr_53quqki((void *)&t172, (void *)NULL, cx, NULL, t170, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tuglhjjmj2g58 t173;
            GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t173 + 4), ((uint32_t)8ULL));
            GM_ST(uint64_t, ((uint8_t *)&t173 + 8), ((uint64_t)1ULL));
            uint8_t t174 = (((GM_LD(uint8_t, ((uint8_t *)&t172) + 0) == ((uint8_t)0ULL)) && gm_eqvs1pd373b1((((uint8_t *)&t172) + 8), ((uint8_t *)&t173))));
            if (!t174) goto ie169;
            {
                void * t176 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t177;
                GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sup5ecv5jiqbn));
                GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(118));
                gm_tftb1odc739t5 t178 = gm_f_extend_0cs85gs(t176, t177);
                if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_srckg9scnip7e8g, 193, 21);
                gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
            }
            goto ix169;
ie169:;
            {
                void * t181 = gm_l0;
                void * t182 = gm_l1;
                gm_t88rcucg1b7uk t183 = gm_l17;
                gm_b16_8 t184 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t181, t182, t183);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t184;
            }
            {
                void * t185 = gm_l0;
                void * t186 = gm_l1;
                gm_t88rcucg1b7uk t187 = gm_l17;
                gm_b16_8 t188 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t185, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t188;
            }
            {
                void * t189 = (void *)((uint8_t *)&F.l5);
                gm_tr4ummvbupakb t190;
                gm_b16_8 t191 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t191);
                uint64_t t192 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t190 + 16, t192);
                uint64_t t193 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t190 + 24, t193);
                gm_b16_8 t194 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t190 + 32, t194);
                uint64_t t195 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t190 + 48, t195);
                uint64_t t196 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t190 + 56, t196);
                gm_tftb1odc739t5 t197 = gm_f_write_aakaioo(t189, &t190);
                if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srckg9scnip7e8g, 197, 21);
                gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
            }
            {
                void * t199 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t200;
                GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sge6a56sqn6kj));
                GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(20));
                gm_tftb1odc739t5 t201 = gm_f_extend_0cs85gs(t199, t200);
                if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srckg9scnip7e8g, 198, 21);
                gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
            }
ix169:;
        }
        goto me50;
mn114:;
ma53:;
        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
        if (t204 != ((uint8_t)17ULL)) goto mn203;
        void * t205 = gm_l1;
        gm_t88rcucg1b7uk t206 = gm_l2;
        void * t207 = gm_f_adt_of_gej99vn(t205, GM_CONV(gm_csguankko4lmhd, t206));
        uint8_t t213 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        switch (t213) { case ((uint8_t)0ULL): goto ma210; case ((uint8_t)1ULL): goto ma209; case ((uint8_t)2ULL): goto ma211; default: goto ma211; }
ma209:;
        uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        if (t215 != ((uint8_t)1ULL)) goto mn214;
        gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t207) + 24) + 8) + 0));
        gm_l26 = t216;
        {
            void * t218 = gm_l1;
            gm_t88rcucg1b7uk t219 = gm_l26;
            gm_b16_8 t220 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t218, t219);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t220;
        }
        {
            uint64_t t221 = (uintptr_t)((uint64_t)0ULL);
            void * t222 = gm_l1;
            gm_t88rcucg1b7uk t223 = gm_l2;
            gm_b16_8 t224 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t225 = GM_LD(uintptr_t, ((uint8_t *)&t224) + 8);
            if (t221 >= (uintptr_t)(t225)) gm_trap_index(t221, t225, gm_srckg9scnip7e8g, 204, 26);
            uint64_t t226 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t224)) + t221 * 8));
            gm_l28 = t226;
        }
        {
            void * t227 = (void *)((uint8_t *)&F.l5);
            gm_thddbqb6snabm t228;
            gm_b16_8 t229 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 0, t229);
            uint64_t t230 = gm_l28;
            GM_ST(uint64_t, (uint8_t *)&t228 + 16, t230);
            gm_b16_8 t231 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t228 + 24, t231);
            uint64_t t232 = gm_l28;
            GM_ST(uint64_t, (uint8_t *)&t228 + 40, t232);
            gm_tftb1odc739t5 t233 = gm_f_write_91uvm3b(t227, &t228);
            if (((((uint8_t *)&t233))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t233) + 1, gm_srckg9scnip7e8g, 205, 21);
            gm_b0_1 t234 = GM_LD(gm_b0_1, (((uint8_t *)&t233) + 1));
        }
        goto me208;
mn214:;
ma210:;
        uint8_t t236 = GM_LD(uint8_t, ((((uint8_t *)t207) + 24) + 0));
        if (t236 != ((uint8_t)0ULL)) goto mn235;
        gm_tpautr4br7h3l t237 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t207) + 24) + 8));
        F.l29 = t237;
        {
            if (!(((((uint8_t *)&F.l29) + 40))[4] != 0)) goto ie239;
            gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 40));
            gm_l30 = t240;
            {
                void * t242 = gm_l0;
                void * t243 = gm_l1;
                gm_t88rcucg1b7uk t244 = gm_l30;
                gm_b16_8 t245 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t242, t243, t244);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l31 = t245;
            }
            {
                void * t246 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t247 = gm_l31;
                gm_tnpifv20irnph t248;
                GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t247);
                gm_tftb1odc739t5 t249 = gm_f_write_96p75mj(t246, t248);
                if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 1, gm_srckg9scnip7e8g, 210, 25);
                gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t249) + 1));
            }
            goto ix239;
ie239:;
            {
                void * t252 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t253;
                GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sug64ltbhov0m));
                GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(18));
                gm_tftb1odc739t5 t254 = gm_f_extend_0cs85gs(t252, t253);
                if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srckg9scnip7e8g, 212, 25);
                gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
            }
ix239:;
        }
        goto me208;
mn235:;
ma211:;
        void * t257 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t258;
        GM_ST(void *, (uint8_t *)&t258, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t258 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t259 = gm_f_extend_0cs85gs(t257, t258);
        if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srckg9scnip7e8g, 215, 22);
        gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
        goto me208;
mn256:;
ma212:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 201, 27);
me208:;
        goto me50;
mn203:;
ma54:;
        void * t262 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t263;
        GM_ST(void *, (uint8_t *)&t263, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t264 = gm_f_extend_0cs85gs(t262, t263);
        if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 1, gm_srckg9scnip7e8g, 217, 18);
        gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 1));
        goto me50;
mn261:;
ma55:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 162, 14);
me50:;
        goto me25;
mn46:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 157, 5);
me25:;
    }
    {
        void * t266 = gm_l0;
        gm_b16_8 t267 = gm_l4;
        gm_b16_8 t268;
        GM_ST(void *, (uint8_t *)&t268, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(4));
        gm_b16_8 t269;
        GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(34));
        gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
        gm_b0_1 t272 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t266, t267, t268, t269, t271);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t273 = gm_l4;
        uint8_t t274 = 0;
        F.ret = t273;
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
inline __attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_helpers_4q783cn F;
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_uvfu1k1((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_tl7apid((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_tl7apid((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_tl7apid((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t6;
    gm_f_new_namer_5q88o9g((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 40, t2);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 80, t3);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 136, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 192, t5);
    GM_ST(gm_ttiitul0263ld, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 248, t6);
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
