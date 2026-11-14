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
typedef struct gm_csfd6et11lk21p { _Bool f0; uint32_t f1; } gm_csfd6et11lk21p;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csfd6et11lk21p) == 8 && _Alignof(gm_csfd6et11lk21p) == 4 && offsetof(gm_csfd6et11lk21p, f0) == 0 && offsetof(gm_csfd6et11lk21p, f1) == 4, "layout of `IntW`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint32_t v1; } f1; } gm_tcula66ipo514;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t0pc2c7dafsjt) == 40 && _Alignof(gm_t0pc2c7dafsjt) == 8 && offsetof(gm_t0pc2c7dafsjt, f0) == 0 && offsetof(gm_t0pc2c7dafsjt, f1) == 4 && offsetof(gm_t0pc2c7dafsjt, f2) == 12 && offsetof(gm_t0pc2c7dafsjt, f3) == 24 && offsetof(gm_t0pc2c7dafsjt, f4) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tcula66ipo514) == 32 && _Alignof(gm_tcula66ipo514) == 8 && offsetof(gm_tcula66ipo514, f0) == 0 && offsetof(gm_tcula66ipo514, f1) == 8, "layout of `Flow!u32`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
struct gm_fr_bit_word_7ae821g {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_div_zero_trap_ap1saqd {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_float_op_osevuqr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_hi64_vi0hvs0 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_int_conv_rqdvurn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_int_done_am7kd5g {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_int_op_5g0lv3e {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_int_un_sm4tv7d {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_int_word_ll0f7kf {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_lo64_91giin8 {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_mask64_vvoc1f6 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_overflow_trap_j2f8u18 {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_sext_g6f00aa {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_shift_word_svovdvk {
    gm_frame h;
    gm_t0pc2c7dafsjt l7; /* x */
    gm_tcula66ipo514 ret;
};
struct gm_fr_word_eq_8jc0uot {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_word_less_l13biee {
    gm_frame h;
    uint8_t ret;
};
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_hi64_vi0hvs0(uint32_t a0);
__attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_int_done_am7kd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint8_t a5, uint8_t a6, gm_b16_8 a7);
__attribute__((aligned(4))) void gm_f_int_op_5g0lv3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, gm_ttovhfee8mf28 a3, gm_ttovhfee8mf28 a4, void * a5, void * a6, void * a7);
__attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) void gm_f_int_word_ll0f7kf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, gm_ttovhfee8mf28 a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint64_t a5, void * a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_lo64_91giin8(uint32_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_sym_6olbepb(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_overflow_result_vqlhh7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint8_t a3, uint32_t a4, unsigned __int128 a5, uint8_t a6, gm_b16_8 a7);
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_shift_check_a7uaho4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, uint8_t a3, unsigned __int128 a4, uint32_t a5);
__attribute__((aligned(4))) void gm_f_shift_word_svovdvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ttovhfee8mf28 a2, uint64_t a3, uint32_t a4);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s9idfla83gb22[59];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcqod65l394oc8[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_div_zero_trap_ap1saqd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_div_zero_trap_ap1saqd F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slf2kl43ogurh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_hi64_vi0hvs0(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hi64_vi0hvs0 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)64ULL));
    if (!t4) goto ie2;
    int64_t t6 = ((int64_t)9223372036854775807LL);
    t1 = t6;
    goto ix2;
ie2:;
    uint32_t t8 = gm_l0;
    uint32_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint32_t)1ULL), &r9);
    uint32_t t11 = r9;
    uint64_t t12 = (uint64_t)t11;
    unsigned __int128 n13 = (unsigned __int128)t12;
    if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_srch12rfrebvus5, 23, 76);
    uint64_t r14 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n13);
    bool o15 = (uint64_t)(r14 >> (int)n13) != ((uint64_t)1ULL);
    uint64_t t16 = r14;
    uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
    uint64_t t19 = r17;
    uint64_t t20 = t19;
    int64_t t21;
    memset(&t21, 0, sizeof t21); memcpy(&t21, ((uint8_t *)&t20), sizeof t21 < 8 ? sizeof t21 : 8);
    int64_t t22 = t21;
    t1 = t22;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_lo64_91giin8(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo64_91giin8 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int64_t t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)64ULL));
    if (!t4) goto ie2;
    int64_t t6 = ((int64_t)(-9223372036854775807LL - 1));
    t1 = t6;
    goto ix2;
ie2:;
    uint64_t t8 = (uint64_t)~((uint64_t)0ULL);
    uint32_t t9 = gm_l0;
    uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
    uint32_t t12 = r10;
    uint64_t t13 = (uint64_t)t12;
    unsigned __int128 n14 = (unsigned __int128)t13;
    if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srch12rfrebvus5, 20, 75);
    uint64_t r15 = (uint64_t)((uint64_t)t8 << (int)n14);
    bool o16 = (uint64_t)(r15 >> (int)n14) != t8;
    uint64_t t17 = r15;
    uint64_t t18 = t17;
    int64_t t19;
    memset(&t19, 0, sizeof t19); memcpy(&t19, ((uint8_t *)&t18), sizeof t19 < 8 ? sizeof t19 : 8);
    int64_t t20 = t19;
    t1 = t20;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask64_vvoc1f6 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)64ULL));
    if (!t4) goto ie2;
    uint64_t t6 = (uint64_t)~((uint64_t)0ULL);
    uint64_t t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    uint64_t t10 = (uint64_t)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 64) gm_trap_shift((uint64_t)n11, "u64", gm_srch12rfrebvus5, 7, 69);
    uint64_t r12 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n11);
    bool o13 = (uint64_t)(r12 >> (int)n11) != ((uint64_t)1ULL);
    uint64_t t14 = r12;
    uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
    uint64_t t17 = r15;
    uint64_t t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_bits_onl28vp F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 t1;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (!t3) goto ie2;
    unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 r6; bool o7 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t5, &r6);
    unsigned __int128 t8 = r6;
    unsigned __int128 t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
    unsigned __int128 t12 = t11;
    t1 = t12;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overflow_trap_j2f8u18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_overflow_trap_j2f8u18 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* sym */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_eruojoe((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcqod65l394oc8, 23, 108);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t9);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sext_g6f00aa F;
    uint64_t gm_l0; /* v */
    uint32_t gm_l1; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint32_t t2 = t1;
    int64_t t3;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint32_t)8ULL))) goto mn5;
    uint64_t t7 = gm_l0;
    uint8_t t8 = (uint8_t)t7;
    uint8_t t9 = t8;
    int8_t t10;
    memset(&t10, 0, sizeof t10); memcpy(&t10, ((uint8_t *)&t9), sizeof t10 < 1 ? sizeof t10 : 1);
    int64_t t11 = (int64_t)t10;
    t3 = t11;
    goto me4;
mn5:;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t13 == ((uint32_t)16ULL))) goto mn12;
    uint64_t t14 = gm_l0;
    uint16_t t15 = (uint16_t)t14;
    uint16_t t16 = t15;
    int16_t t17;
    memset(&t17, 0, sizeof t17); memcpy(&t17, ((uint8_t *)&t16), sizeof t17 < 2 ? sizeof t17 : 2);
    int64_t t18 = (int64_t)t17;
    t3 = t18;
    goto me4;
mn12:;
    uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t20 == ((uint32_t)32ULL))) goto mn19;
    uint64_t t21 = gm_l0;
    uint32_t t22 = (uint32_t)t21;
    uint32_t t23 = t22;
    int32_t t24;
    memset(&t24, 0, sizeof t24); memcpy(&t24, ((uint8_t *)&t23), sizeof t24 < 4 ? sizeof t24 : 4);
    int64_t t25 = (int64_t)t24;
    t3 = t25;
    goto me4;
mn19:;
    uint64_t t27 = gm_l0;
    uint64_t t28 = t27;
    int64_t t29;
    memset(&t29, 0, sizeof t29); memcpy(&t29, ((uint8_t *)&t28), sizeof t29 < 8 ? sizeof t29 : 8);
    t3 = t29;
    goto me4;
mn26:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 11, 41);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_eq_8jc0uot F;
    gm_tmn7n0n9raou5 gm_l0; /* how */
    uint64_t gm_l1; /* p */
    uint64_t gm_l2; /* q */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma3; case ((uint8_t)4ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)3ULL)) goto mn9;
    uint64_t t11 = gm_l1;
    uint64_t t12 = t11;
    double t13;
    memset(&t13, 0, sizeof t13); memcpy(&t13, ((uint8_t *)&t12), sizeof t13 < 8 ? sizeof t13 : 8);
    uint64_t t14 = gm_l2;
    uint64_t t15 = t14;
    double t16;
    memset(&t16, 0, sizeof t16); memcpy(&t16, ((uint8_t *)&t15), sizeof t16 < 8 ? sizeof t16 : 8);
    uint8_t t17 = ((t13 == t16));
    t1 = t17;
    goto me2;
mn9:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)2ULL)) goto mn18;
    uint64_t t20 = gm_l1;
    uint32_t t21 = (uint32_t)t20;
    uint32_t t22 = t21;
    float t23;
    memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 4 ? sizeof t23 : 4);
    uint64_t t24 = gm_l2;
    uint32_t t25 = (uint32_t)t24;
    uint32_t t26 = t25;
    float t27;
    memset(&t27, 0, sizeof t27); memcpy(&t27, ((uint8_t *)&t26), sizeof t27 < 4 ? sizeof t27 : 4);
    uint8_t t28 = ((t23 == t27));
    t1 = t28;
    goto me2;
mn18:;
ma5:;
    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t30 != ((uint8_t)4ULL)) goto mn29;
    uint64_t t31 = gm_l1;
    uint8_t t32 = (!(t31 == ((uint64_t)0ULL)));
    uint64_t t33 = gm_l2;
    uint8_t t34 = (!(t33 == ((uint64_t)0ULL)));
    uint8_t t35 = ((t32 == t34));
    t1 = t35;
    goto me2;
mn29:;
ma6:;
    uint64_t t37 = gm_l1;
    uint64_t t38 = gm_l2;
    uint8_t t39 = ((t37 == t38));
    t1 = t39;
    goto me2;
mn36:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 70, 51);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_word_less_l13biee F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_tmn7n0n9raou5 gm_l1; /* how */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    int64_t gm_l5; /* x */
    int64_t gm_l6; /* y */
    double gm_l7; /* x */
    double gm_l8; /* y */
    float gm_l9; /* x */
    float gm_l10; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 472u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma3; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t10 != ((uint8_t)1ULL)) goto mn9;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l4 = t11;
        {
            uint64_t t13 = gm_l2;
            uint32_t t14 = gm_l4;
            int64_t t15 = gm_f_sext_g6f00aa(t13, t14);
            gm_l5 = t15;
        }
        {
            uint64_t t16 = gm_l3;
            uint32_t t17 = gm_l4;
            int64_t t18 = gm_f_sext_g6f00aa(t16, t17);
            gm_l6 = t18;
        }
        {
            uint8_t t19;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma22; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma24; default: goto ma24; }
ma21:;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t28 != ((uint8_t)10ULL)) goto mn27;
            int64_t t29 = gm_l5;
            int64_t t30 = gm_l6;
            uint8_t t31 = (t29 < t30);
            t19 = t31;
            goto me20;
mn27:;
ma22:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t33 != ((uint8_t)11ULL)) goto mn32;
            int64_t t34 = gm_l5;
            int64_t t35 = gm_l6;
            uint8_t t36 = (t34 <= t35);
            t19 = t36;
            goto me20;
mn32:;
ma23:;
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t38 != ((uint8_t)12ULL)) goto mn37;
            int64_t t39 = gm_l5;
            int64_t t40 = gm_l6;
            uint8_t t41 = (t39 > t40);
            t19 = t41;
            goto me20;
mn37:;
ma24:;
            int64_t t43 = gm_l5;
            int64_t t44 = gm_l6;
            uint8_t t45 = (t43 >= t44);
            t19 = t45;
            goto me20;
mn42:;
ma25:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 32, 20);
me20:;
            uint8_t t46 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto me2;
mn9:;
ma4:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t48 != ((uint8_t)3ULL)) goto mn47;
        {
            uint64_t t50 = gm_l2;
            uint64_t t51 = t50;
            double t52;
            memset(&t52, 0, sizeof t52); memcpy(&t52, ((uint8_t *)&t51), sizeof t52 < 8 ? sizeof t52 : 8);
            gm_l7 = t52;
        }
        {
            uint64_t t53 = gm_l3;
            uint64_t t54 = t53;
            double t55;
            memset(&t55, 0, sizeof t55); memcpy(&t55, ((uint8_t *)&t54), sizeof t55 < 8 ? sizeof t55 : 8);
            gm_l8 = t55;
        }
        {
            uint8_t t56;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t63) { case ((uint8_t)0ULL): goto ma61; case ((uint8_t)1ULL): goto ma61; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma61; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma61; case ((uint8_t)6ULL): goto ma61; case ((uint8_t)7ULL): goto ma61; case ((uint8_t)8ULL): goto ma61; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma58; case ((uint8_t)11ULL): goto ma59; case ((uint8_t)12ULL): goto ma60; case ((uint8_t)13ULL): goto ma61; default: goto ma61; }
ma58:;
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t65 != ((uint8_t)10ULL)) goto mn64;
            double t66 = gm_l7;
            double t67 = gm_l8;
            uint8_t t68 = (t66 < t67);
            t56 = t68;
            goto me57;
mn64:;
ma59:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t70 != ((uint8_t)11ULL)) goto mn69;
            double t71 = gm_l7;
            double t72 = gm_l8;
            uint8_t t73 = (t71 <= t72);
            t56 = t73;
            goto me57;
mn69:;
ma60:;
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t75 != ((uint8_t)12ULL)) goto mn74;
            double t76 = gm_l7;
            double t77 = gm_l8;
            uint8_t t78 = (t76 > t77);
            t56 = t78;
            goto me57;
mn74:;
ma61:;
            double t80 = gm_l7;
            double t81 = gm_l8;
            uint8_t t82 = (t80 >= t81);
            t56 = t82;
            goto me57;
mn79:;
ma62:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 42, 20);
me57:;
            uint8_t t83 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto me2;
mn47:;
ma5:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t85 != ((uint8_t)2ULL)) goto mn84;
        {
            uint64_t t87 = gm_l2;
            uint32_t t88 = (uint32_t)t87;
            uint32_t t89 = t88;
            float t90;
            memset(&t90, 0, sizeof t90); memcpy(&t90, ((uint8_t *)&t89), sizeof t90 < 4 ? sizeof t90 : 4);
            gm_l9 = t90;
        }
        {
            uint64_t t91 = gm_l3;
            uint32_t t92 = (uint32_t)t91;
            uint32_t t93 = t92;
            float t94;
            memset(&t94, 0, sizeof t94); memcpy(&t94, ((uint8_t *)&t93), sizeof t94 < 4 ? sizeof t94 : 4);
            gm_l10 = t94;
        }
        {
            uint8_t t95;
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t102) { case ((uint8_t)0ULL): goto ma100; case ((uint8_t)1ULL): goto ma100; case ((uint8_t)2ULL): goto ma100; case ((uint8_t)3ULL): goto ma100; case ((uint8_t)4ULL): goto ma100; case ((uint8_t)5ULL): goto ma100; case ((uint8_t)6ULL): goto ma100; case ((uint8_t)7ULL): goto ma100; case ((uint8_t)8ULL): goto ma100; case ((uint8_t)9ULL): goto ma100; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma98; case ((uint8_t)12ULL): goto ma99; case ((uint8_t)13ULL): goto ma100; default: goto ma100; }
ma97:;
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t104 != ((uint8_t)10ULL)) goto mn103;
            float t105 = gm_l9;
            float t106 = gm_l10;
            uint8_t t107 = (t105 < t106);
            t95 = t107;
            goto me96;
mn103:;
ma98:;
            uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t109 != ((uint8_t)11ULL)) goto mn108;
            float t110 = gm_l9;
            float t111 = gm_l10;
            uint8_t t112 = (t110 <= t111);
            t95 = t112;
            goto me96;
mn108:;
ma99:;
            uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t114 != ((uint8_t)12ULL)) goto mn113;
            float t115 = gm_l9;
            float t116 = gm_l10;
            uint8_t t117 = (t115 > t116);
            t95 = t117;
            goto me96;
mn113:;
ma100:;
            float t119 = gm_l9;
            float t120 = gm_l10;
            uint8_t t121 = (t119 >= t120);
            t95 = t121;
            goto me96;
mn118:;
ma101:;
            gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 52, 20);
me96:;
            uint8_t t122 = 0;
            F.ret = t95;
            goto gm_fret;
        }
        goto me2;
mn84:;
ma6:;
        uint8_t t124;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t131) { case ((uint8_t)0ULL): goto ma129; case ((uint8_t)1ULL): goto ma129; case ((uint8_t)2ULL): goto ma129; case ((uint8_t)3ULL): goto ma129; case ((uint8_t)4ULL): goto ma129; case ((uint8_t)5ULL): goto ma129; case ((uint8_t)6ULL): goto ma129; case ((uint8_t)7ULL): goto ma129; case ((uint8_t)8ULL): goto ma129; case ((uint8_t)9ULL): goto ma129; case ((uint8_t)10ULL): goto ma126; case ((uint8_t)11ULL): goto ma127; case ((uint8_t)12ULL): goto ma128; case ((uint8_t)13ULL): goto ma129; default: goto ma129; }
ma126:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t133 != ((uint8_t)10ULL)) goto mn132;
        uint64_t t134 = gm_l2;
        uint64_t t135 = gm_l3;
        uint8_t t136 = (t134 < t135);
        t124 = t136;
        goto me125;
mn132:;
ma127:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t138 != ((uint8_t)11ULL)) goto mn137;
        uint64_t t139 = gm_l2;
        uint64_t t140 = gm_l3;
        uint8_t t141 = (t139 <= t140);
        t124 = t141;
        goto me125;
mn137:;
ma128:;
        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t143 != ((uint8_t)12ULL)) goto mn142;
        uint64_t t144 = gm_l2;
        uint64_t t145 = gm_l3;
        uint8_t t146 = (t144 > t145);
        t124 = t146;
        goto me125;
mn142:;
ma129:;
        uint64_t t148 = gm_l2;
        uint64_t t149 = gm_l3;
        uint8_t t150 = (t148 >= t149);
        t124 = t150;
        goto me125;
mn147:;
ma130:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 59, 21);
me125:;
        uint8_t t151 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me2;
mn123:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 28, 5);
me2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((noinline)) uint64_t gm_f_bit_word_7ae821g(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_word_7ae821g F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_ttovhfee8mf28 gm_l1; /* t */
    uint64_t gm_l2; /* v */
    uint64_t gm_l3; /* k */
    uint32_t gm_l4; /* n */
    uint64_t gm_l5; /* x */
    uint32_t gm_l6; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 497u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttovhfee8mf28, a1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l4 = t2;
    }
    {
        uint64_t t3 = gm_l2;
        uint32_t t4 = gm_l4;
        uint64_t t5 = gm_f_mask64_vvoc1f6(t4);
        uint64_t t6 = (uint64_t)(t3 & t5);
        gm_l5 = t6;
    }
    {
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma9; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma13; case ((uint8_t)35ULL): goto ma13; case ((uint8_t)36ULL): goto ma13; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; case ((uint8_t)51ULL): goto ma13; case ((uint8_t)52ULL): goto ma13; case ((uint8_t)53ULL): goto ma13; case ((uint8_t)54ULL): goto ma13; case ((uint8_t)55ULL): goto ma13; case ((uint8_t)56ULL): goto ma13; case ((uint8_t)57ULL): goto ma13; case ((uint8_t)58ULL): goto ma13; case ((uint8_t)59ULL): goto ma13; case ((uint8_t)60ULL): goto ma13; case ((uint8_t)61ULL): goto ma13; case ((uint8_t)62ULL): goto ma13; case ((uint8_t)63ULL): goto ma13; case ((uint8_t)64ULL): goto ma13; case ((uint8_t)65ULL): goto ma13; case ((uint8_t)66ULL): goto ma13; case ((uint8_t)67ULL): goto ma13; case ((uint8_t)68ULL): goto ma13; case ((uint8_t)69ULL): goto ma13; case ((uint8_t)70ULL): goto ma13; case ((uint8_t)71ULL): goto ma13; case ((uint8_t)72ULL): goto ma13; case ((uint8_t)73ULL): goto ma13; case ((uint8_t)74ULL): goto ma13; case ((uint8_t)75ULL): goto ma13; case ((uint8_t)76ULL): goto ma13; case ((uint8_t)77ULL): goto ma13; case ((uint8_t)78ULL): goto ma13; case ((uint8_t)79ULL): goto ma13; case ((uint8_t)80ULL): goto ma13; case ((uint8_t)81ULL): goto ma13; default: goto ma13; }
ma8:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t19 != ((uint8_t)32ULL)) goto pa18;
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t21 != ((uint8_t)33ULL)) goto pa20;
        goto po17;
pa20:;
        goto mn16;
po17:;
        {
            uint64_t t23 = gm_l3;
            uint32_t t24 = gm_l4;
            uint64_t t25 = (uint64_t)t24;
            if (t25 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 108, 19);
            uint64_t t26 = (uint64_t)(t23 % t25);
            uint32_t t27 = (uint32_t)t26;
            gm_l6 = t27;
        }
        {
            uint8_t t29 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)33ULL)));
            if (!t29) goto ie28;
            {
                uint32_t t31 = gm_l4;
                uint32_t t32 = gm_l6;
                uint32_t r33; bool o34 = __builtin_sub_overflow(t31, t32, &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 109, 35);
                uint32_t t35 = r33;
                uint32_t t36 = gm_l4;
                if (t36 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 109, 34);
                uint32_t t37 = (uint32_t)(t35 % t36);
                gm_l6 = t37;
            }
            goto ix28;
ie28:;
ix28:;
        }
        {
            uint32_t t39 = gm_l6;
            uint8_t t40 = ((t39 == ((uint32_t)0ULL)));
            if (!t40) goto ie38;
            {
                uint64_t t42 = gm_l5;
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            uint64_t t44 = gm_l5;
            uint32_t t45 = gm_l6;
            uint64_t t46 = (uint64_t)t45;
            unsigned __int128 n47 = (unsigned __int128)t46;
            if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srch12rfrebvus5, 111, 22);
            uint64_t r48 = (uint64_t)((uint64_t)t44 << (int)n47);
            bool o49 = (uint64_t)(r48 >> (int)n47) != t44;
            uint64_t t50 = r48;
            uint64_t t51 = gm_l5;
            uint32_t t52 = gm_l4;
            uint32_t t53 = gm_l6;
            uint32_t r54; bool o55 = __builtin_sub_overflow(t52, t53, &r54);
            if (o55) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 111, 48);
            uint32_t t56 = r54;
            uint64_t t57 = (uint64_t)t56;
            unsigned __int128 n58 = (unsigned __int128)t57;
            if (n58 >= 64) gm_trap_shift((uint64_t)n58, "u64", gm_srch12rfrebvus5, 111, 39);
            uint64_t t59 = (uint64_t)(t51 >> (int)n58);
            uint64_t t60 = (uint64_t)(t50 | t59);
            uint32_t t61 = gm_l4;
            uint64_t t62 = gm_f_mask64_vvoc1f6(t61);
            uint64_t t63 = (uint64_t)(t60 & t62);
            uint8_t t64 = 0;
            F.ret = t63;
            goto gm_fret;
        }
        goto me7;
mn16:;
ma9:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t66 != ((uint8_t)28ULL)) goto mn65;
        uint64_t t67 = gm_l5;
        uint32_t t68 = (uint32_t)gm_clz(((unsigned __int128)(uint64_t)(t67) & (((unsigned __int128)1 << 64) - 1)), 64);
        uint64_t t69 = (uint64_t)t68;
        uint32_t t70 = gm_l4;
        uint32_t r71; bool o72 = __builtin_sub_overflow(((uint32_t)64ULL), t70, &r71);
        if (o72) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 113, 43);
        uint32_t t73 = r71;
        uint64_t t74 = (uint64_t)t73;
        uint64_t r75; bool o76 = __builtin_sub_overflow(t69, t74, &r75);
        if (o76) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 113, 24);
        uint64_t t77 = r75;
        uint8_t t78 = 0;
        F.ret = t77;
        goto gm_fret;
        goto me7;
mn65:;
ma10:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t80 != ((uint8_t)29ULL)) goto mn79;
        uint64_t t81;
        uint64_t t83 = gm_l5;
        uint8_t t84 = ((t83 == ((uint64_t)0ULL)));
        if (!t84) goto ie82;
        uint32_t t86 = gm_l4;
        uint64_t t87 = (uint64_t)t86;
        uint64_t t88 = t87;
        t81 = t88;
        goto ix82;
ie82:;
        uint64_t t90 = gm_l5;
        uint32_t t91 = (uint32_t)gm_ctz(((unsigned __int128)(uint64_t)(t90) & (((unsigned __int128)1 << 64) - 1)), 64);
        uint64_t t92 = (uint64_t)t91;
        uint64_t t93 = t92;
        t81 = t93;
ix82:;
        uint8_t t94 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me7;
mn79:;
ma11:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t96 != ((uint8_t)30ULL)) goto mn95;
        uint64_t t97 = gm_l5;
        uint32_t t98 = (uint32_t)gm_popcount(((unsigned __int128)(uint64_t)(t97) & (((unsigned __int128)1 << 64) - 1)));
        uint64_t t99 = (uint64_t)t98;
        uint8_t t100 = 0;
        F.ret = t99;
        goto gm_fret;
        goto me7;
mn95:;
ma12:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t102 != ((uint8_t)31ULL)) goto mn101;
        uint64_t t103 = gm_l5;
        uint64_t t104 = (uint64_t)gm_bswap(((unsigned __int128)(uint64_t)(t103) & (((unsigned __int128)1 << 64) - 1)), 64);
        uint64_t t105 = t104;
        uint32_t t106 = gm_l4;
        uint32_t r107; bool o108 = __builtin_sub_overflow(((uint32_t)64ULL), t106, &r107);
        if (o108) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 116, 43);
        uint32_t t109 = r107;
        uint64_t t110 = (uint64_t)t109;
        unsigned __int128 n111 = (unsigned __int128)t110;
        if (n111 >= 64) gm_trap_shift((uint64_t)n111, "u64", gm_srch12rfrebvus5, 116, 26);
        uint64_t t112 = (uint64_t)(t105 >> (int)n111);
        uint8_t t113 = 0;
        F.ret = t112;
        goto gm_fret;
        goto me7;
mn101:;
ma13:;
        uint64_t t115 = gm_l5;
        uint64_t t116 = (uint64_t)gm_bitreverse(((unsigned __int128)(uint64_t)(t115) & (((unsigned __int128)1 << 64) - 1)), 64);
        uint64_t t117 = t116;
        uint32_t t118 = gm_l4;
        uint32_t r119; bool o120 = __builtin_sub_overflow(((uint32_t)64ULL), t118, &r119);
        if (o120) gm_trap("integer overflow in `-`", gm_srch12rfrebvus5, 117, 43);
        uint32_t t121 = r119;
        uint64_t t122 = (uint64_t)t121;
        unsigned __int128 n123 = (unsigned __int128)t122;
        if (n123 >= 64) gm_trap_shift((uint64_t)n123, "u64", gm_srch12rfrebvus5, 117, 21);
        uint64_t t124 = (uint64_t)(t117 >> (int)n123);
        uint8_t t125 = 0;
        F.ret = t124;
        goto gm_fret;
        goto me7;
mn114:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 106, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_float_op_osevuqr(gm_ts6oqppjh0ba3 a0, uint8_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_op_osevuqr F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    uint8_t gm_l1; /* wide */
    uint64_t gm_l2; /* p */
    uint64_t gm_l3; /* q */
    double gm_l4; /* x */
    double gm_l5; /* y */
    double gm_l6; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 399u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        double t2;
        uint8_t t4 = gm_l1;
        if (!t4) goto ie3;
        uint64_t t6 = gm_l2;
        uint64_t t7 = t6;
        double t8;
        memset(&t8, 0, sizeof t8); memcpy(&t8, ((uint8_t *)&t7), sizeof t8 < 8 ? sizeof t8 : 8);
        double t9 = t8;
        t2 = t9;
        goto ix3;
ie3:;
        uint64_t t11 = gm_l2;
        uint32_t t12 = (uint32_t)t11;
        uint32_t t13 = t12;
        float t14;
        memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 4 ? sizeof t14 : 4);
        double t15 = (double)t14;
        double t16 = t15;
        t2 = t16;
ix3:;
        gm_l4 = t2;
    }
    {
        double t17;
        uint8_t t19 = gm_l1;
        if (!t19) goto ie18;
        uint64_t t21 = gm_l3;
        uint64_t t22 = t21;
        double t23;
        memset(&t23, 0, sizeof t23); memcpy(&t23, ((uint8_t *)&t22), sizeof t23 < 8 ? sizeof t23 : 8);
        double t24 = t23;
        t17 = t24;
        goto ix18;
ie18:;
        uint64_t t26 = gm_l3;
        uint32_t t27 = (uint32_t)t26;
        uint32_t t28 = t27;
        float t29;
        memset(&t29, 0, sizeof t29); memcpy(&t29, ((uint8_t *)&t28), sizeof t29 < 4 ? sizeof t29 : 4);
        double t30 = (double)t29;
        double t31 = t30;
        t17 = t31;
ix18:;
        gm_l5 = t17;
    }
    {
        double t32;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t39) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma36; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma37; case ((uint8_t)6ULL): goto ma37; case ((uint8_t)7ULL): goto ma37; case ((uint8_t)8ULL): goto ma37; case ((uint8_t)9ULL): goto ma37; case ((uint8_t)10ULL): goto ma37; case ((uint8_t)11ULL): goto ma37; case ((uint8_t)12ULL): goto ma37; case ((uint8_t)13ULL): goto ma37; default: goto ma37; }
ma34:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t41 != ((uint8_t)0ULL)) goto mn40;
        double t42 = gm_l4;
        double t43 = gm_l5;
        double t44 = (t42 + t43);
        t32 = t44;
        goto me33;
mn40:;
ma35:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t46 != ((uint8_t)1ULL)) goto mn45;
        double t47 = gm_l4;
        double t48 = gm_l5;
        double t49 = (t47 - t48);
        t32 = t49;
        goto me33;
mn45:;
ma36:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t51 != ((uint8_t)2ULL)) goto mn50;
        double t52 = gm_l4;
        double t53 = gm_l5;
        double t54 = (t52 * t53);
        t32 = t54;
        goto me33;
mn50:;
ma37:;
        double t56 = gm_l4;
        double t57 = gm_l5;
        double t58 = (t56 / t57);
        t32 = t58;
        goto me33;
mn55:;
ma38:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 81, 10);
me33:;
        gm_l6 = t32;
    }
    {
        uint64_t t59;
        uint8_t t61 = gm_l1;
        if (!t61) goto ie60;
        double t63 = gm_l6;
        double t64 = t63;
        uint64_t t65;
        memset(&t65, 0, sizeof t65); memcpy(&t65, ((uint8_t *)&t64), sizeof t65 < 8 ? sizeof t65 : 8);
        uint64_t t66 = t65;
        t59 = t66;
        goto ix60;
ie60:;
        double t68 = gm_l6;
        float t69 = (float)((double)t68);
        float t70 = t69;
        uint32_t t71;
        memset(&t71, 0, sizeof t71); memcpy(&t71, ((uint8_t *)&t70), sizeof t71 < 4 ? sizeof t71 : 4);
        uint64_t t72 = (uint64_t)t71;
        uint64_t t73 = t72;
        t59 = t73;
ix60:;
        uint8_t t74 = 0;
        F.ret = t59;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) int64_t gm_f_hi64_vi0hvs0(uint32_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_int_conv_rqdvurn(gm_tvdg94bk7aa22 a0, gm_csfd6et11lk21p a1, gm_csfd6et11lk21p a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_conv_rqdvurn F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_ttovhfee8mf28 gm_l1; /* from */
    gm_ttovhfee8mf28 gm_l2; /* to */
    uint64_t gm_l3; /* v */
    unsigned __int128 gm_l4; /* x */
    uint64_t gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 468u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_ttovhfee8mf28, a1);
    gm_l2 = GM_CONV(gm_ttovhfee8mf28, a2);
    gm_l3 = a3;
    {
        uint8_t t3 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)6ULL)));
        if (!t3) goto ie2;
        {
            unsigned __int128 t5;
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (!t7) goto ie6;
            uint64_t t9 = gm_l3;
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            int64_t t11 = gm_f_sext_g6f00aa(t9, t10);
            __int128 t12 = (__int128)t11;
            __int128 t13 = t12;
            unsigned __int128 t14;
            memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 16 ? sizeof t14 : 16);
            unsigned __int128 t15 = t14;
            t5 = t15;
            goto ix6;
ie6:;
            uint64_t t17 = gm_l3;
            unsigned __int128 t18 = (unsigned __int128)t17;
            unsigned __int128 t19 = t18;
            t5 = t19;
ix6:;
            gm_l4 = t5;
        }
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            unsigned __int128 t23 = gm_l4;
            gm_t86h6hhm6d76n t24;
            gm_f_num_val_i3upeur((void *)&t24, t22, t23);
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_t86h6hhm6d76n t27;
            gm_f_num_sat_66dqcpl((void *)&t27, &t24, t25, t26);
            unsigned __int128 t28 = gm_f_num_bits_onl28vp(&t27);
            unsigned __int128 t29 = gm_f_num_ext_348kmbu(t20, t21, t28);
            uint64_t t30 = (uint64_t)t29;
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint64_t t32 = gm_f_mask64_vvoc1f6(t31);
            uint64_t t33 = (uint64_t)(t30 & t32);
            uint8_t t34 = 0;
            F.ret = t33;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t35;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (!t37) goto ie36;
        uint64_t t39 = gm_l3;
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        int64_t t41 = gm_f_sext_g6f00aa(t39, t40);
        int64_t t42 = t41;
        uint64_t t43;
        memset(&t43, 0, sizeof t43); memcpy(&t43, ((uint8_t *)&t42), sizeof t43 < 8 ? sizeof t43 : 8);
        uint64_t t44 = t43;
        t35 = t44;
        goto ix36;
ie36:;
        uint64_t t46 = gm_l3;
        uint64_t t47 = t46;
        t35 = t47;
ix36:;
        gm_l5 = t35;
    }
    {
        uint64_t t48 = gm_l5;
        uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t50 = gm_f_mask64_vvoc1f6(t49);
        uint64_t t51 = (uint64_t)(t48 & t50);
        uint8_t t52 = 0;
        F.ret = t51;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_int_done_am7kd5g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint8_t a5, uint8_t a6, gm_b16_8 a7) {
    struct gm_fr_int_done_am7kd5g F;
    void * gm_l0; /* m */
    void * gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* fl */
    gm_ttovhfee8mf28 gm_l3; /* t */
    uint64_t gm_l4; /* low */
    uint8_t gm_l5; /* over */
    uint8_t gm_l6; /* below */
    gm_b16_8 gm_l7; /* sym */
    uint64_t gm_l8; /* mask */
    GM_STACK_CHECK(&F, 494u);
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
    gm_l7 = a7;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint64_t t3 = gm_f_mask64_vvoc1f6(t2);
        gm_l8 = t3;
    }
    {
        uint8_t t5 = gm_l5;
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie4;
        {
            uint64_t t8 = gm_l4;
            uint64_t t9 = gm_l8;
            uint64_t t10 = (uint64_t)(t8 & t9);
            gm_t833u93mr4iob t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t11 + 8), t10);
            gm_t833u93mr4iob t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t15) + 112));
        gm_t9qkh0rsooc8d t17 = gm_l2;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint64_t t20 = gm_l4;
        unsigned __int128 t21 = (unsigned __int128)t20;
        uint8_t t22 = gm_l6;
        gm_b16_8 t23 = gm_l7;
        gm_tdlro53ljvkl0 t24;
        gm_f_overflow_result_vqlhh7t((void *)&t24, (void *)NULL, cx, NULL, t14, t16, t17, t18, t19, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdlro53ljvkl0 t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_t76123gfet73g t26 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t25) + 16));
            gm_t833u93mr4iob t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t27 + 8), t26);
            F.ret = t27;
            goto gm_fret;
        }
        unsigned __int128 t28 = GM_LD(unsigned __int128, (((uint8_t *)&t25) + 16));
        uint64_t t29 = (uint64_t)t28;
        uint64_t t30 = gm_l8;
        uint64_t t31 = (uint64_t)(t29 & t30);
        gm_t833u93mr4iob t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t32 + 24, 0);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t32 + 8), t31);
        gm_t833u93mr4iob t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
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
inline __attribute__((aligned(4))) void gm_f_int_op_5g0lv3e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_ts6oqppjh0ba3 a2, gm_ttovhfee8mf28 a3, gm_ttovhfee8mf28 a4, void * a5, void * a6, void * a7) {
    struct gm_fr_int_op_5g0lv3e F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_ts6oqppjh0ba3 gm_l2; /* op */
    gm_ttovhfee8mf28 gm_l3; /* t */
    gm_ttovhfee8mf28 gm_l4; /* shift */
    void * gm_l5; /* l */
    void * gm_l6; /* r */
    void * gm_l7; /* n */
    uint64_t gm_l8; /* p */
    uint64_t gm_l9; /* q */
    GM_STACK_CHECK(&F, 452u);
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
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        gm_tbl8il92l97ut t3 = gm_l1;
        void * t4 = gm_l5;
        gm_t833u93mr4iob t5;
        gm_f_ew_c5kil07((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_t76123gfet73g t7 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t6) + 8));
            gm_t833u93mr4iob t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&t6) + 8));
        gm_l8 = t9;
    }
    {
        void * t10 = gm_l0;
        gm_tbl8il92l97ut t11 = gm_l1;
        void * t12 = gm_l6;
        gm_t833u93mr4iob t13;
        gm_f_ew_c5kil07((void *)&t13, (void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) {
            gm_t76123gfet73g t15 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t14) + 8));
            gm_t833u93mr4iob t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t16 + 8), t15);
            F.ret = t16;
            goto gm_fret;
        }
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&t14) + 8));
        gm_l9 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_ts6oqppjh0ba3 t19 = gm_l2;
        gm_ttovhfee8mf28 t20 = gm_l3;
        gm_ttovhfee8mf28 t21 = gm_l4;
        uint64_t t22 = gm_l8;
        uint64_t t23 = gm_l9;
        void * t24 = gm_l7;
        gm_f_int_word_ll0f7kf((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t18, t19, t20, t21, t22, t23, t24);
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
__attribute__((aligned(4))) void gm_f_int_un_sm4tv7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_ttovhfee8mf28 a2, uint64_t a3, void * a4) {
    struct gm_fr_int_un_sm4tv7d F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_ttovhfee8mf28 gm_l2; /* t */
    uint64_t gm_l3; /* v */
    void * gm_l4; /* n */
    uint64_t gm_l5; /* mask */
    int64_t gm_l6; /* x */
    GM_STACK_CHECK(&F, 886u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t3 = gm_f_mask64_vvoc1f6(t2);
        gm_l5 = t3;
    }
    {
        uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
        if (!t5) goto ie4;
        {
            uint64_t t7 = gm_l3;
            uint64_t t8 = (uint64_t)~t7;
            uint64_t t9 = gm_l5;
            uint64_t t10 = (uint64_t)(t8 & t9);
            gm_t833u93mr4iob t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t11 + 8), t10);
            gm_t833u93mr4iob t12 = t11;
            uint8_t t13 = ((((uint8_t *)&t12))[0] != 0);
            F.ret = t11;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (!t15) goto ie14;
        {
            uint64_t t17 = gm_l3;
            uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            int64_t t19 = gm_f_sext_g6f00aa(t17, t18);
            gm_l6 = t19;
        }
        {
            int64_t t21 = gm_l6;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            int64_t t23 = gm_f_lo64_91giin8(t22);
            uint8_t t24 = ((t21 == t23));
            if (!t24) goto ie20;
            {
                void * t26 = gm_l0;
                void * t27 = gm_l4;
                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 112));
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
                gm_tcfa6l7reoosn t30;
                gm_f_checked_overflow_ma1bm1g((void *)&t30, (void *)NULL, cx, NULL, t26, t28, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tcfa6l7reoosn t31 = t30;
                if (((((uint8_t *)&t31))[0] != 0)) {
                    gm_t76123gfet73g t32 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t31) + 8));
                    gm_t833u93mr4iob t33;
                    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                    ((uint8_t *)&t33)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t33 + 8), t32);
                    F.ret = t33;
                    goto gm_fret;
                }
                gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
            }
            {
                uint64_t t35 = gm_l3;
                gm_t833u93mr4iob t36;
                GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
                ((uint8_t *)&t36)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t36 + 8), t35);
                gm_t833u93mr4iob t37 = t36;
                uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
                F.ret = t36;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            int64_t t39 = gm_l6;
            int64_t r40; bool o41 = __builtin_sub_overflow(((int64_t)0LL), t39, &r40);
            int64_t t42 = r40;
            int64_t t43 = t42;
            uint64_t t44;
            memset(&t44, 0, sizeof t44); memcpy(&t44, ((uint8_t *)&t43), sizeof t44 < 8 ? sizeof t44 : 8);
            uint64_t t45 = gm_l5;
            uint64_t t46 = (uint64_t)(t44 & t45);
            gm_t833u93mr4iob t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
            ((uint8_t *)&t47)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t47 + 8), t46);
            gm_t833u93mr4iob t48 = t47;
            uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
            F.ret = t47;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint64_t t51 = gm_l3;
        uint8_t t52 = ((t51 == ((uint64_t)0ULL)));
        if (!t52) goto ie50;
        {
            gm_t833u93mr4iob t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t54 + 8), ((uint64_t)0ULL));
            gm_t833u93mr4iob t55 = t54;
            uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
            F.ret = t54;
            goto gm_fret;
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        void * t57 = gm_l0;
        void * t58 = gm_l4;
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 112));
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
        gm_tcfa6l7reoosn t61;
        gm_f_checked_overflow_ma1bm1g((void *)&t61, (void *)NULL, cx, NULL, t57, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tcfa6l7reoosn t62 = t61;
        if (((((uint8_t *)&t62))[0] != 0)) {
            gm_t76123gfet73g t63 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t62) + 8));
            gm_t833u93mr4iob t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t64 + 8), t63);
            F.ret = t64;
            goto gm_fret;
        }
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 8));
    }
    {
        uint64_t t66 = gm_l3;
        uint64_t r67; bool o68 = __builtin_sub_overflow(((uint64_t)0ULL), t66, &r67);
        uint64_t t69 = r67;
        uint64_t t70 = gm_l5;
        uint64_t t71 = (uint64_t)(t69 & t70);
        gm_t833u93mr4iob t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t72 + 24, 0);
        ((uint8_t *)&t72)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t72 + 8), t71);
        gm_t833u93mr4iob t73 = t72;
        uint8_t t74 = ((((uint8_t *)&t73))[0] != 0);
        F.ret = t72;
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
__attribute__((aligned(4))) void gm_f_int_word_ll0f7kf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, gm_ttovhfee8mf28 a2, gm_ttovhfee8mf28 a3, uint64_t a4, uint64_t a5, void * a6) {
    struct gm_fr_int_word_ll0f7kf F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_ttovhfee8mf28 gm_l2; /* t */
    gm_ttovhfee8mf28 gm_l3; /* shift */
    uint64_t gm_l4; /* p */
    uint64_t gm_l5; /* q */
    void * gm_l6; /* n */
    uint64_t gm_l7; /* mask */
    uint32_t gm_l8; /* bits */
    gm_t9qkh0rsooc8d gm_l9; /* fl */
    int64_t gm_l10; /* x */
    int64_t gm_l11; /* y */
    int64_t gm_l12; /* s */
    uint8_t gm_l13; /* over */
    uint64_t gm_l14; /* s */
    uint8_t gm_l15; /* over */
    gm_t9qkh0rsooc8d gm_l16; /* fl */
    int64_t gm_l17; /* x */
    int64_t gm_l18; /* y */
    int64_t gm_l19; /* s */
    uint8_t gm_l20; /* over */
    gm_t9qkh0rsooc8d gm_l21; /* fl */
    int64_t gm_l22; /* x */
    int64_t gm_l23; /* y */
    int64_t gm_l24; /* s */
    __int128 gm_l25; /* s */
    uint64_t gm_l26; /* s */
    unsigned __int128 gm_l27; /* s */
    gm_t9qkh0rsooc8d gm_l28; /* fl */
    uint32_t gm_l29; /* k */
    uint64_t gm_l30; /* low */
    int64_t gm_l31; /* v */
    int64_t gm_l32; /* w */
    uint32_t gm_l33; /* k */
    uint8_t gm_l34; /* is_div */
    int64_t gm_l35; /* x */
    int64_t gm_l36; /* y */
    GM_STACK_CHECK(&F, 3830u);
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
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t3 = gm_f_mask64_vvoc1f6(t2);
        gm_l7 = t3;
    }
    {
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_l8 = t4;
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t17) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma15; default: goto ma15; }
ma6:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l9 = t20;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t23) goto ie22;
            {
                uint64_t t25 = gm_l4;
                uint32_t t26 = gm_l8;
                int64_t t27 = gm_f_sext_g6f00aa(t25, t26);
                gm_l10 = t27;
            }
            {
                uint64_t t28 = gm_l5;
                uint32_t t29 = gm_l8;
                int64_t t30 = gm_f_sext_g6f00aa(t28, t29);
                gm_l11 = t30;
            }
            {
                int64_t t31 = gm_l10;
                int64_t t32 = gm_l11;
                int64_t r33; bool o34 = __builtin_add_overflow(t31, t32, &r33);
                int64_t t35 = r33;
                gm_l12 = t35;
            }
            {
                uint8_t t36;
                uint32_t t38 = gm_l8;
                uint8_t t39 = (t38 >= ((uint32_t)64ULL));
                if (!t39) goto ie37;
                int64_t t41 = gm_l10;
                int64_t t42 = gm_l12;
                int64_t t43 = (int64_t)(t41 ^ t42);
                int64_t t44 = gm_l11;
                int64_t t45 = gm_l12;
                int64_t t46 = (int64_t)(t44 ^ t45);
                int64_t t47 = (int64_t)(t43 & t46);
                uint8_t t48 = (t47 < ((int64_t)0LL));
                uint8_t t49 = t48;
                t36 = t49;
                goto ix37;
ie37:;
                uint8_t t51 = 0;
                int64_t t52 = gm_l12;
                uint32_t t53 = gm_l8;
                int64_t t54 = gm_f_lo64_91giin8(t53);
                uint8_t t55 = (t52 < t54);
                if (!t55) {
                    int64_t t56 = gm_l12;
                    uint32_t t57 = gm_l8;
                    int64_t t58 = gm_f_hi64_vi0hvs0(t57);
                    uint8_t t59 = (t56 > t58);
                    t51 = t59;
                } else t51 = 1;
                uint8_t t60 = t51;
                t36 = t60;
ix37:;
                gm_l13 = t36;
            }
            {
                void * t61 = gm_l0;
                void * t62 = gm_l6;
                gm_t9qkh0rsooc8d t63 = gm_l9;
                gm_ttovhfee8mf28 t64 = gm_l2;
                int64_t t65 = gm_l12;
                int64_t t66 = t65;
                uint64_t t67;
                memset(&t67, 0, sizeof t67); memcpy(&t67, ((uint8_t *)&t66), sizeof t67 < 8 ? sizeof t67 : 8);
                uint8_t t68 = gm_l13;
                int64_t t69 = gm_l11;
                uint8_t t70 = (t69 < ((int64_t)0LL));
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sajhum9b4nr96));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(1));
                gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t61, t62, t63, t64, t67, t68, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            uint64_t t72 = gm_l4;
            uint64_t t73 = gm_l5;
            uint64_t r74; bool o75 = __builtin_add_overflow(t72, t73, &r74);
            uint64_t t76 = r74;
            gm_l14 = t76;
        }
        {
            uint8_t t77;
            uint32_t t79 = gm_l8;
            uint8_t t80 = (t79 >= ((uint32_t)64ULL));
            if (!t80) goto ie78;
            uint64_t t82 = gm_l14;
            uint64_t t83 = gm_l4;
            uint8_t t84 = (t82 < t83);
            uint8_t t85 = t84;
            t77 = t85;
            goto ix78;
ie78:;
            uint64_t t87 = gm_l14;
            uint64_t t88 = gm_l7;
            uint8_t t89 = (t87 > t88);
            uint8_t t90 = t89;
            t77 = t90;
ix78:;
            gm_l15 = t77;
        }
        {
            void * t91 = gm_l0;
            void * t92 = gm_l6;
            gm_t9qkh0rsooc8d t93 = gm_l9;
            gm_ttovhfee8mf28 t94 = gm_l2;
            uint64_t t95 = gm_l14;
            uint8_t t96 = gm_l15;
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sajhum9b4nr96));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(1));
            gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t91, t92, t93, t94, t95, t96, ((uint8_t)0), t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn18:;
ma7:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t99 != ((uint8_t)1ULL)) goto mn98;
        gm_t9qkh0rsooc8d t100 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l16 = t100;
        {
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t103) goto ie102;
            {
                uint64_t t105 = gm_l4;
                uint32_t t106 = gm_l8;
                int64_t t107 = gm_f_sext_g6f00aa(t105, t106);
                gm_l17 = t107;
            }
            {
                uint64_t t108 = gm_l5;
                uint32_t t109 = gm_l8;
                int64_t t110 = gm_f_sext_g6f00aa(t108, t109);
                gm_l18 = t110;
            }
            {
                int64_t t111 = gm_l17;
                int64_t t112 = gm_l18;
                int64_t r113; bool o114 = __builtin_sub_overflow(t111, t112, &r113);
                int64_t t115 = r113;
                gm_l19 = t115;
            }
            {
                uint8_t t116;
                uint32_t t118 = gm_l8;
                uint8_t t119 = (t118 >= ((uint32_t)64ULL));
                if (!t119) goto ie117;
                int64_t t121 = gm_l17;
                int64_t t122 = gm_l18;
                int64_t t123 = (int64_t)(t121 ^ t122);
                int64_t t124 = gm_l17;
                int64_t t125 = gm_l19;
                int64_t t126 = (int64_t)(t124 ^ t125);
                int64_t t127 = (int64_t)(t123 & t126);
                uint8_t t128 = (t127 < ((int64_t)0LL));
                uint8_t t129 = t128;
                t116 = t129;
                goto ix117;
ie117:;
                uint8_t t131 = 0;
                int64_t t132 = gm_l19;
                uint32_t t133 = gm_l8;
                int64_t t134 = gm_f_lo64_91giin8(t133);
                uint8_t t135 = (t132 < t134);
                if (!t135) {
                    int64_t t136 = gm_l19;
                    uint32_t t137 = gm_l8;
                    int64_t t138 = gm_f_hi64_vi0hvs0(t137);
                    uint8_t t139 = (t136 > t138);
                    t131 = t139;
                } else t131 = 1;
                uint8_t t140 = t131;
                t116 = t140;
ix117:;
                gm_l20 = t116;
            }
            {
                void * t141 = gm_l0;
                void * t142 = gm_l6;
                gm_t9qkh0rsooc8d t143 = gm_l16;
                gm_ttovhfee8mf28 t144 = gm_l2;
                int64_t t145 = gm_l19;
                int64_t t146 = t145;
                uint64_t t147;
                memset(&t147, 0, sizeof t147); memcpy(&t147, ((uint8_t *)&t146), sizeof t147 < 8 ? sizeof t147 : 8);
                uint8_t t148 = gm_l20;
                int64_t t149 = gm_l18;
                uint8_t t150 = (t149 > ((int64_t)0LL));
                gm_b16_8 t151;
                GM_ST(void *, (uint8_t *)&t151, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(1));
                gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t141, t142, t143, t144, t147, t148, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            void * t152 = gm_l0;
            void * t153 = gm_l6;
            gm_t9qkh0rsooc8d t154 = gm_l16;
            gm_ttovhfee8mf28 t155 = gm_l2;
            uint64_t t156 = gm_l4;
            uint64_t t157 = gm_l5;
            uint64_t r158; bool o159 = __builtin_sub_overflow(t156, t157, &r158);
            uint64_t t160 = r158;
            uint64_t t161 = gm_l4;
            uint64_t t162 = gm_l5;
            uint8_t t163 = (t161 < t162);
            gm_b16_8 t164;
            GM_ST(void *, (uint8_t *)&t164, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(1));
            gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t152, t153, t154, t155, t160, t163, ((uint8_t)1), t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn98:;
ma8:;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t166 != ((uint8_t)2ULL)) goto mn165;
        gm_t9qkh0rsooc8d t167 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t167;
        {
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t170) goto ie169;
            {
                uint64_t t172 = gm_l4;
                uint32_t t173 = gm_l8;
                int64_t t174 = gm_f_sext_g6f00aa(t172, t173);
                gm_l22 = t174;
            }
            {
                uint64_t t175 = gm_l5;
                uint32_t t176 = gm_l8;
                int64_t t177 = gm_f_sext_g6f00aa(t175, t176);
                gm_l23 = t177;
            }
            {
                uint32_t t179 = gm_l8;
                uint8_t t180 = (t179 <= ((uint32_t)32ULL));
                if (!t180) goto ie178;
                {
                    int64_t t182 = gm_l22;
                    int64_t t183 = gm_l23;
                    int64_t r184; bool o185 = __builtin_mul_overflow(t182, t183, &r184);
                    if (o185) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 179, 26);
                    int64_t t186 = r184;
                    gm_l24 = t186;
                }
                {
                    void * t187 = gm_l0;
                    void * t188 = gm_l6;
                    gm_t9qkh0rsooc8d t189 = gm_l21;
                    gm_ttovhfee8mf28 t190 = gm_l2;
                    int64_t t191 = gm_l24;
                    int64_t t192 = t191;
                    uint64_t t193;
                    memset(&t193, 0, sizeof t193); memcpy(&t193, ((uint8_t *)&t192), sizeof t193 < 8 ? sizeof t193 : 8);
                    uint8_t t194 = 0;
                    int64_t t195 = gm_l24;
                    uint32_t t196 = gm_l8;
                    int64_t t197 = gm_f_lo64_91giin8(t196);
                    uint8_t t198 = (t195 < t197);
                    if (!t198) {
                        int64_t t199 = gm_l24;
                        uint32_t t200 = gm_l8;
                        int64_t t201 = gm_f_hi64_vi0hvs0(t200);
                        uint8_t t202 = (t199 > t201);
                        t194 = t202;
                    } else t194 = 1;
                    int64_t t203 = gm_l24;
                    uint8_t t204 = (t203 < ((int64_t)0LL));
                    gm_b16_8 t205;
                    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s6s96daqmhcs9));
                    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(1));
                    gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t187, t188, t189, t190, t193, t194, t204, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix178;
ie178:;
ix178:;
            }
            {
                int64_t t206 = gm_l22;
                __int128 t207 = (__int128)t206;
                int64_t t208 = gm_l23;
                __int128 t209 = (__int128)t208;
                __int128 r210; bool o211 = __builtin_mul_overflow(t207, t209, &r210);
                if (o211) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 182, 22);
                __int128 t212 = r210;
                gm_l25 = t212;
            }
            {
                void * t213 = gm_l0;
                void * t214 = gm_l6;
                gm_t9qkh0rsooc8d t215 = gm_l21;
                gm_ttovhfee8mf28 t216 = gm_l2;
                __int128 t217 = gm_l25;
                int64_t t218 = (int64_t)t217;
                int64_t t219 = t218;
                uint64_t t220;
                memset(&t220, 0, sizeof t220); memcpy(&t220, ((uint8_t *)&t219), sizeof t220 < 8 ? sizeof t220 : 8);
                uint8_t t221 = 0;
                __int128 t222 = gm_l25;
                uint32_t t223 = gm_l8;
                int64_t t224 = gm_f_lo64_91giin8(t223);
                __int128 t225 = (__int128)t224;
                uint8_t t226 = (t222 < t225);
                if (!t226) {
                    __int128 t227 = gm_l25;
                    uint32_t t228 = gm_l8;
                    int64_t t229 = gm_f_hi64_vi0hvs0(t228);
                    __int128 t230 = (__int128)t229;
                    uint8_t t231 = (t227 > t230);
                    t221 = t231;
                } else t221 = 1;
                __int128 t232 = gm_l25;
                uint8_t t233 = (t232 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                gm_b16_8 t234;
                GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s6s96daqmhcs9));
                GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(1));
                gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t213, t214, t215, t216, t220, t221, t233, t234);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix169;
ie169:;
ix169:;
        }
        {
            uint32_t t236 = gm_l8;
            uint8_t t237 = (t236 <= ((uint32_t)32ULL));
            if (!t237) goto ie235;
            {
                uint64_t t239 = gm_l4;
                uint64_t t240 = gm_l5;
                uint64_t r241; bool o242 = __builtin_mul_overflow(t239, t240, &r241);
                if (o242) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 186, 22);
                uint64_t t243 = r241;
                gm_l26 = t243;
            }
            {
                void * t244 = gm_l0;
                void * t245 = gm_l6;
                gm_t9qkh0rsooc8d t246 = gm_l21;
                gm_ttovhfee8mf28 t247 = gm_l2;
                uint64_t t248 = gm_l26;
                uint64_t t249 = gm_l26;
                uint64_t t250 = gm_l7;
                uint8_t t251 = (t249 > t250);
                gm_b16_8 t252;
                GM_ST(void *, (uint8_t *)&t252, (void *)(gm_s6s96daqmhcs9));
                GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(1));
                gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t244, t245, t246, t247, t248, t251, ((uint8_t)0), t252);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix235;
ie235:;
ix235:;
        }
        {
            uint64_t t253 = gm_l4;
            unsigned __int128 t254 = (unsigned __int128)t253;
            uint64_t t255 = gm_l5;
            unsigned __int128 t256 = (unsigned __int128)t255;
            unsigned __int128 r257; bool o258 = __builtin_mul_overflow(t254, t256, &r257);
            if (o258) gm_trap("integer overflow in `*`", gm_srch12rfrebvus5, 189, 18);
            unsigned __int128 t259 = r257;
            gm_l27 = t259;
        }
        {
            void * t260 = gm_l0;
            void * t261 = gm_l6;
            gm_t9qkh0rsooc8d t262 = gm_l21;
            gm_ttovhfee8mf28 t263 = gm_l2;
            unsigned __int128 t264 = gm_l27;
            uint64_t t265 = (uint64_t)t264;
            unsigned __int128 t266 = gm_l27;
            uint64_t t267 = gm_l7;
            unsigned __int128 t268 = (unsigned __int128)t267;
            uint8_t t269 = (t266 > t268);
            gm_b16_8 t270;
            GM_ST(void *, (uint8_t *)&t270, (void *)(gm_s6s96daqmhcs9));
            GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(1));
            gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t260, t261, t262, t263, t265, t269, ((uint8_t)0), t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn165:;
ma9:;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t272 != ((uint8_t)3ULL)) goto mn271;
        gm_t9qkh0rsooc8d t273 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l28 = t273;
        {
            void * t275 = gm_l0;
            void * t276 = gm_l6;
            gm_ttovhfee8mf28 t277 = gm_l3;
            uint64_t t278 = gm_l5;
            uint32_t t279 = gm_l8;
            gm_tcula66ipo514 t280;
            gm_f_shift_word_svovdvk((void *)&t280, (void *)NULL, cx, NULL, t275, t276, t277, t278, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcula66ipo514 t281 = t280;
            if (((((uint8_t *)&t281))[0] != 0)) {
                gm_t76123gfet73g t282 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t281) + 8));
                gm_t833u93mr4iob t283;
                GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                ((uint8_t *)&t283)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t283 + 8), t282);
                F.ret = t283;
                goto gm_fret;
            }
            uint32_t t284 = GM_LD(uint32_t, (((uint8_t *)&t281) + 8));
            gm_l29 = t284;
        }
        {
            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t287 = (uint8_t)!t286;
            if (!t287) goto ie285;
            {
                uint64_t t289 = gm_l4;
                uint32_t t290 = gm_l29;
                uint64_t t291 = (uint64_t)t290;
                unsigned __int128 n292 = (unsigned __int128)t291;
                if (n292 >= 64) gm_trap_shift((uint64_t)n292, "u64", gm_srch12rfrebvus5, 195, 25);
                uint64_t r293 = (uint64_t)((uint64_t)t289 << (int)n292);
                bool o294 = (uint64_t)(r293 >> (int)n292) != t289;
                uint64_t t295 = r293;
                uint64_t t296 = gm_l7;
                uint64_t t297 = (uint64_t)(t295 & t296);
                gm_l30 = t297;
            }
            {
                void * t298 = gm_l0;
                void * t299 = gm_l6;
                gm_t9qkh0rsooc8d t300 = gm_l28;
                gm_ttovhfee8mf28 t301 = gm_l2;
                uint64_t t302 = gm_l30;
                uint64_t t303 = gm_l30;
                uint32_t t304 = gm_l29;
                uint64_t t305 = (uint64_t)t304;
                unsigned __int128 n306 = (unsigned __int128)t305;
                if (n306 >= 64) gm_trap_shift((uint64_t)n306, "u64", gm_srch12rfrebvus5, 196, 51);
                uint64_t t307 = (uint64_t)(t303 >> (int)n306);
                uint64_t t308 = gm_l4;
                uint8_t t309 = (!(t307 == t308));
                gm_b16_8 t310;
                GM_ST(void *, (uint8_t *)&t310, (void *)(gm_si62ejtp0vbct));
                GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(2));
                gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t298, t299, t300, t301, t302, t309, ((uint8_t)0), t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix285;
ie285:;
ix285:;
        }
        {
            uint64_t t311 = gm_l4;
            uint32_t t312 = gm_l8;
            int64_t t313 = gm_f_sext_g6f00aa(t311, t312);
            gm_l31 = t313;
        }
        {
            int64_t t314 = gm_l31;
            int64_t t315 = t314;
            uint64_t t316;
            memset(&t316, 0, sizeof t316); memcpy(&t316, ((uint8_t *)&t315), sizeof t316 < 8 ? sizeof t316 : 8);
            uint32_t t317 = gm_l29;
            uint64_t t318 = (uint64_t)t317;
            unsigned __int128 n319 = (unsigned __int128)t318;
            if (n319 >= 64) gm_trap_shift((uint64_t)n319, "u64", gm_srch12rfrebvus5, 199, 24);
            uint64_t r320 = (uint64_t)((uint64_t)t316 << (int)n319);
            bool o321 = (uint64_t)(r320 >> (int)n319) != t316;
            uint64_t t322 = r320;
            uint64_t t323 = gm_l7;
            uint64_t t324 = (uint64_t)(t322 & t323);
            uint32_t t325 = gm_l8;
            int64_t t326 = gm_f_sext_g6f00aa(t324, t325);
            gm_l32 = t326;
        }
        {
            void * t327 = gm_l0;
            void * t328 = gm_l6;
            gm_t9qkh0rsooc8d t329 = gm_l28;
            gm_ttovhfee8mf28 t330 = gm_l2;
            int64_t t331 = gm_l32;
            int64_t t332 = t331;
            uint64_t t333;
            memset(&t333, 0, sizeof t333); memcpy(&t333, ((uint8_t *)&t332), sizeof t333 < 8 ? sizeof t333 : 8);
            int64_t t334 = gm_l32;
            uint32_t t335 = gm_l29;
            unsigned __int128 n336 = (unsigned __int128)t335;
            if (n336 >= 64) gm_trap_shift((uint64_t)n336, "i64", gm_srch12rfrebvus5, 200, 55);
            int64_t t337 = (int64_t)(t334 >> (int)n336);
            int64_t t338 = gm_l31;
            uint8_t t339 = (!(t337 == t338));
            int64_t t340 = gm_l31;
            uint8_t t341 = (t340 < ((int64_t)0LL));
            gm_b16_8 t342;
            GM_ST(void *, (uint8_t *)&t342, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(2));
            gm_f_int_done_am7kd5g((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t327, t328, t329, t330, t333, t339, t341, t342);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn271:;
ma10:;
        uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t344 != ((uint8_t)6ULL)) goto mn343;
        {
            void * t346 = gm_l0;
            void * t347 = gm_l6;
            gm_ttovhfee8mf28 t348 = gm_l3;
            uint64_t t349 = gm_l5;
            uint32_t t350 = gm_l8;
            gm_tcula66ipo514 t351;
            gm_f_shift_word_svovdvk((void *)&t351, (void *)NULL, cx, NULL, t346, t347, t348, t349, t350);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tcula66ipo514 t352 = t351;
            if (((((uint8_t *)&t352))[0] != 0)) {
                gm_t76123gfet73g t353 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&t352) + 8));
                gm_t833u93mr4iob t354;
                GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
                ((uint8_t *)&t354)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t354 + 8), t353);
                F.ret = t354;
                goto gm_fret;
            }
            uint32_t t355 = GM_LD(uint32_t, (((uint8_t *)&t352) + 8));
            gm_l33 = t355;
        }
        {
            uint8_t t357 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t357) goto ie356;
            {
                uint64_t t359 = gm_l4;
                uint32_t t360 = gm_l8;
                int64_t t361 = gm_f_sext_g6f00aa(t359, t360);
                uint32_t t362 = gm_l33;
                unsigned __int128 n363 = (unsigned __int128)t362;
                if (n363 >= 64) gm_trap_shift((uint64_t)n363, "i64", gm_srch12rfrebvus5, 204, 35);
                int64_t t364 = (int64_t)(t361 >> (int)n363);
                int64_t t365 = t364;
                uint64_t t366;
                memset(&t366, 0, sizeof t366); memcpy(&t366, ((uint8_t *)&t365), sizeof t366 < 8 ? sizeof t366 : 8);
                uint64_t t367 = gm_l7;
                uint64_t t368 = (uint64_t)(t366 & t367);
                gm_t833u93mr4iob t369;
                GM_ST(uint8_t, (uint8_t *)&t369 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t369 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t369 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t369 + 24, 0);
                ((uint8_t *)&t369)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t369 + 8), t368);
                gm_t833u93mr4iob t370 = t369;
                uint8_t t371 = ((((uint8_t *)&t370))[0] != 0);
                F.ret = t369;
                goto gm_fret;
            }
            goto ix356;
ie356:;
ix356:;
        }
        {
            uint64_t t372 = gm_l4;
            uint32_t t373 = gm_l33;
            uint64_t t374 = (uint64_t)t373;
            unsigned __int128 n375 = (unsigned __int128)t374;
            if (n375 >= 64) gm_trap_shift((uint64_t)n375, "u64", gm_srch12rfrebvus5, 205, 20);
            uint64_t t376 = (uint64_t)(t372 >> (int)n375);
            gm_t833u93mr4iob t377;
            GM_ST(uint8_t, (uint8_t *)&t377 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t377 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t377 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t377 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t377 + 24, 0);
            ((uint8_t *)&t377)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t377 + 8), t376);
            gm_t833u93mr4iob t378 = t377;
            uint8_t t379 = ((((uint8_t *)&t378))[0] != 0);
            F.ret = t377;
            goto gm_fret;
        }
        goto me5;
mn343:;
ma11:;
        uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t383 != ((uint8_t)4ULL)) goto pa382;
        goto po381;
pa382:;
        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t385 != ((uint8_t)5ULL)) goto pa384;
        goto po381;
pa384:;
        goto mn380;
po381:;
        {
            uint64_t t388 = gm_l5;
            uint8_t t389 = ((t388 == ((uint64_t)0ULL)));
            if (!t389) goto ie387;
            {
                void * t391 = gm_l0;
                void * t392 = gm_l6;
                gm_tl874t2atnlh3 t393 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t392) + 112));
                gm_t76123gfet73g t394;
                gm_f_div_zero_trap_ap1saqd((void *)&t394, (void *)NULL, cx, NULL, t391, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t833u93mr4iob t395;
                GM_ST(uint8_t, (uint8_t *)&t395 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t395 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t395 + 4, 0);
                ((uint8_t *)&t395)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t395 + 8), t394);
                gm_t833u93mr4iob t396 = t395;
                uint8_t t397 = ((((uint8_t *)&t396))[0] != 0);
                F.ret = t395;
                goto gm_fret;
            }
            goto ix387;
ie387:;
ix387:;
        }
        {
            uint8_t t398 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            gm_l34 = t398;
        }
        {
            uint8_t t400 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t401 = (uint8_t)!t400;
            if (!t401) goto ie399;
            {
                gm_t833u93mr4iob t403;
                uint8_t t405 = gm_l34;
                if (!t405) goto ie404;
                uint64_t t407 = gm_l4;
                uint64_t t408 = gm_l5;
                if (t408 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 210, 50);
                uint64_t t409 = (uint64_t)(t407 / t408);
                uint64_t t410 = t409;
                gm_t833u93mr4iob t411;
                GM_ST(uint8_t, (uint8_t *)&t411 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t411 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t411 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t411 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t411 + 24, 0);
                ((uint8_t *)&t411)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t411 + 8), t410);
                t403 = t411;
                goto ix404;
ie404:;
                uint64_t t413 = gm_l4;
                uint64_t t414 = gm_l5;
                if (t414 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 210, 65);
                uint64_t t415 = (uint64_t)(t413 % t414);
                uint64_t t416 = t415;
                gm_t833u93mr4iob t417;
                GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t417 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t417 + 24, 0);
                ((uint8_t *)&t417)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t417 + 8), t416);
                t403 = t417;
ix404:;
                gm_t833u93mr4iob t418 = t403;
                uint8_t t419 = ((((uint8_t *)&t418))[0] != 0);
                F.ret = t403;
                goto gm_fret;
            }
            goto ix399;
ie399:;
ix399:;
        }
        {
            uint64_t t420 = gm_l4;
            uint32_t t421 = gm_l8;
            int64_t t422 = gm_f_sext_g6f00aa(t420, t421);
            gm_l35 = t422;
        }
        {
            uint64_t t423 = gm_l5;
            uint32_t t424 = gm_l8;
            int64_t t425 = gm_f_sext_g6f00aa(t423, t424);
            gm_l36 = t425;
        }
        {
            int64_t t427 = gm_l36;
            uint8_t t428 = ((t427 == ((int64_t)-1LL)));
            if (!t428) goto ie426;
            {
                int64_t t431 = gm_l35;
                uint32_t t432 = gm_l8;
                int64_t t433 = gm_f_lo64_91giin8(t432);
                uint8_t t434 = ((t431 == t433));
                if (!t434) goto ie430;
                {
                    void * t436 = gm_l0;
                    void * t437 = gm_l6;
                    gm_tl874t2atnlh3 t438 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t437) + 112));
                    gm_ts6oqppjh0ba3 t439 = gm_l1;
                    gm_b16_8 t440 = gm_f_op_sym_6olbepb(t439);
                    gm_t76123gfet73g t441;
                    gm_f_overflow_trap_j2f8u18((void *)&t441, (void *)NULL, cx, NULL, t436, t438, t440);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t833u93mr4iob t442;
                    GM_ST(uint8_t, (uint8_t *)&t442 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t442 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t442 + 4, 0);
                    ((uint8_t *)&t442)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t442 + 8), t441);
                    gm_t833u93mr4iob t443 = t442;
                    uint8_t t444 = ((((uint8_t *)&t443))[0] != 0);
                    F.ret = t442;
                    goto gm_fret;
                }
                goto ix430;
ie430:;
ix430:;
            }
            {
                gm_t833u93mr4iob t445;
                uint8_t t447 = gm_l34;
                if (!t447) goto ie446;
                int64_t t449 = gm_l35;
                int64_t r450; bool o451 = __builtin_sub_overflow(((int64_t)0LL), t449, &r450);
                int64_t t452 = r450;
                int64_t t453 = t452;
                uint64_t t454;
                memset(&t454, 0, sizeof t454); memcpy(&t454, ((uint8_t *)&t453), sizeof t454 < 8 ? sizeof t454 : 8);
                uint64_t t455 = gm_l7;
                uint64_t t456 = (uint64_t)(t454 & t455);
                uint64_t t457 = t456;
                gm_t833u93mr4iob t458;
                GM_ST(uint8_t, (uint8_t *)&t458 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t458 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t458 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t458 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t458 + 24, 0);
                ((uint8_t *)&t458)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t458 + 8), t457);
                t445 = t458;
                goto ix446;
ie446:;
                uint64_t t460 = ((uint64_t)0ULL);
                gm_t833u93mr4iob t461;
                GM_ST(uint8_t, (uint8_t *)&t461 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t461 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t461 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t461 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t461 + 24, 0);
                ((uint8_t *)&t461)[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&t461 + 8), t460);
                t445 = t461;
ix446:;
                gm_t833u93mr4iob t462 = t445;
                uint8_t t463 = ((((uint8_t *)&t462))[0] != 0);
                F.ret = t445;
                goto gm_fret;
            }
            goto ix426;
ie426:;
ix426:;
        }
        {
            int64_t t464;
            uint8_t t466 = gm_l34;
            if (!t466) goto ie465;
            int64_t t468 = gm_l35;
            int64_t t469 = gm_l36;
            if (t469 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 217, 33);
            if (t468 == ((int64_t)(-9223372036854775807LL - 1)) && t469 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srch12rfrebvus5, 217, 33);
            int64_t t470 = (int64_t)(t468 / t469);
            int64_t t471 = t470;
            t464 = t471;
            goto ix465;
ie465:;
            int64_t t473 = gm_l35;
            int64_t t474 = gm_l36;
            if (t474 == 0) gm_trap("division by zero", gm_srch12rfrebvus5, 217, 48);
            if (t473 == ((int64_t)(-9223372036854775807LL - 1)) && t474 == (int64_t)-1) gm_trap("integer overflow in `%`", gm_srch12rfrebvus5, 217, 48);
            int64_t t475 = (int64_t)(t473 % t474);
            int64_t t476 = t475;
            t464 = t476;
ix465:;
            int64_t t477 = t464;
            uint64_t t478;
            memset(&t478, 0, sizeof t478); memcpy(&t478, ((uint8_t *)&t477), sizeof t478 < 8 ? sizeof t478 : 8);
            uint64_t t479 = gm_l7;
            uint64_t t480 = (uint64_t)(t478 & t479);
            gm_t833u93mr4iob t481;
            GM_ST(uint8_t, (uint8_t *)&t481 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t481 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t481 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t481 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t481 + 24, 0);
            ((uint8_t *)&t481)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&t481 + 8), t480);
            gm_t833u93mr4iob t482 = t481;
            uint8_t t483 = ((((uint8_t *)&t482))[0] != 0);
            F.ret = t481;
            goto gm_fret;
        }
        goto me5;
mn380:;
ma12:;
        uint8_t t485 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t485 != ((uint8_t)7ULL)) goto mn484;
        uint64_t t486 = gm_l4;
        uint64_t t487 = gm_l5;
        uint64_t t488 = (uint64_t)(t486 & t487);
        gm_t833u93mr4iob t489;
        GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t489 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t489 + 24, 0);
        ((uint8_t *)&t489)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t489 + 8), t488);
        gm_t833u93mr4iob t490 = t489;
        uint8_t t491 = ((((uint8_t *)&t490))[0] != 0);
        F.ret = t489;
        goto gm_fret;
        goto me5;
mn484:;
ma13:;
        uint8_t t493 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t493 != ((uint8_t)8ULL)) goto mn492;
        uint64_t t494 = gm_l4;
        uint64_t t495 = gm_l5;
        uint64_t t496 = (uint64_t)(t494 | t495);
        gm_t833u93mr4iob t497;
        GM_ST(uint8_t, (uint8_t *)&t497 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t497 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t497 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t497 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t497 + 24, 0);
        ((uint8_t *)&t497)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t497 + 8), t496);
        gm_t833u93mr4iob t498 = t497;
        uint8_t t499 = ((((uint8_t *)&t498))[0] != 0);
        F.ret = t497;
        goto gm_fret;
        goto me5;
mn492:;
ma14:;
        uint8_t t501 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t501 != ((uint8_t)9ULL)) goto mn500;
        uint64_t t502 = gm_l4;
        uint64_t t503 = gm_l5;
        uint64_t t504 = (uint64_t)(t502 ^ t503);
        gm_t833u93mr4iob t505;
        GM_ST(uint8_t, (uint8_t *)&t505 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t505 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t505 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t505 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t505 + 24, 0);
        ((uint8_t *)&t505)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t505 + 8), t504);
        gm_t833u93mr4iob t506 = t505;
        uint8_t t507 = ((((uint8_t *)&t506))[0] != 0);
        F.ret = t505;
        goto gm_fret;
        goto me5;
mn500:;
ma15:;
        void * t509 = gm_l0;
        void * t510 = gm_l6;
        gm_tl874t2atnlh3 t511 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t510) + 112));
        gm_b16_8 t512;
        GM_ST(void *, (uint8_t *)&t512, (void *)(gm_s9idfla83gb22));
        GM_ST(uintptr_t, (uint8_t *)&t512 + 8, (uintptr_t)(58));
        gm_t76123gfet73g t513;
        gm_f_trap_qh84l9d((void *)&t513, (void *)NULL, cx, NULL, t509, t511, t512);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t833u93mr4iob t514;
        GM_ST(uint8_t, (uint8_t *)&t514 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t514 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t514 + 4, 0);
        ((uint8_t *)&t514)[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t514 + 8), t513);
        gm_t833u93mr4iob t515 = t514;
        uint8_t t516 = ((((uint8_t *)&t515))[0] != 0);
        F.ret = t514;
        goto gm_fret;
        goto me5;
mn508:;
ma16:;
        gm_trap("no match arm matched the scrutinee", gm_srch12rfrebvus5, 151, 5);
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
extern __attribute__((aligned(4))) int64_t gm_f_lo64_91giin8(uint32_t a0);
extern __attribute__((aligned(4))) uint64_t gm_f_mask64_vvoc1f6(uint32_t a0);
extern __attribute__((aligned(4))) int64_t gm_f_sext_g6f00aa(uint64_t a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_shift_word_svovdvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ttovhfee8mf28 a2, uint64_t a3, uint32_t a4) {
    struct gm_fr_shift_word_svovdvk F;
    void * gm_l0; /* m */
    void * gm_l1; /* n */
    gm_ttovhfee8mf28 gm_l2; /* rw */
    uint64_t gm_l3; /* q */
    uint32_t gm_l4; /* bits */
    uint8_t gm_l5; /* neg */
    gm_t88rcucg1b7uk gm_l6; /* lt */
    GM_STACK_CHECK(&F, 367u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t3) {
            uint64_t t4 = gm_l3;
            uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            int64_t t6 = gm_f_sext_g6f00aa(t4, t5);
            uint8_t t7 = (t6 < ((int64_t)0LL));
            t2 = t7;
        } else t2 = 0;
        gm_l5 = t2;
    }
    {
        uint8_t t9 = gm_l5;
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) goto ie8;
        uint64_t t11 = gm_l3;
        uint32_t t12 = gm_l4;
        uint64_t t13 = (uint64_t)t12;
        uint8_t t14 = (t11 < t13);
        if (!t14) goto ie8;
        {
            uint64_t t16 = gm_l3;
            uint32_t t17 = (uint32_t)t16;
            gm_tcula66ipo514 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 8), t17);
            gm_tcula66ipo514 t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t21 = gm_l1;
        gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 96));
        gm_l6 = t22;
    }
    {
        void * t24 = gm_l1;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 0) + 0));
        if (t25 != ((uint8_t)21ULL)) goto ie23;
        gm_t0pc2c7dafsjt t26 = GM_LD(gm_t0pc2c7dafsjt, ((((uint8_t *)t24) + 0) + 8));
        F.l7 = t26;
        {
            void * t28 = GM_LD(void *, (((uint8_t *)&F.l7) + 24));
            gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 96));
            gm_l6 = t29;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t30 = gm_l0;
        void * t31 = gm_l1;
        gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 112));
        gm_t88rcucg1b7uk t33 = gm_l6;
        uint8_t t34 = gm_l5;
        uint64_t t35;
        uint8_t t37 = gm_l5;
        if (!t37) goto ie36;
        uint64_t t39 = gm_l3;
        uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        int64_t t41 = gm_f_sext_g6f00aa(t39, t40);
        int64_t r42; bool o43 = __builtin_sub_overflow(((int64_t)0LL), t41, &r42);
        int64_t t44 = r42;
        int64_t t45 = t44;
        uint64_t t46;
        memset(&t46, 0, sizeof t46); memcpy(&t46, ((uint8_t *)&t45), sizeof t46 < 8 ? sizeof t46 : 8);
        uint64_t t47 = t46;
        t35 = t47;
        goto ix36;
ie36:;
        uint64_t t49 = gm_l3;
        uint64_t t50 = t49;
        t35 = t50;
ix36:;
        unsigned __int128 t51 = (unsigned __int128)t35;
        uint32_t t52 = gm_l4;
        gm_f_shift_check_a7uaho4((void *)&(*(gm_tcula66ipo514 *)gm_out), (void *)NULL, cx, NULL, t30, t32, t33, t34, t51, t52);
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
extern __attribute__((aligned(4))) uint8_t gm_f_word_eq_8jc0uot(gm_tmn7n0n9raou5 a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_word_less_l13biee(gm_ts6oqppjh0ba3 a0, gm_tmn7n0n9raou5 a1, uint64_t a2, uint64_t a3);
