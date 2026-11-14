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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v1; } f1; } gm_tt7ek1a8bopcl;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tt7ek1a8bopcl) == 24 && _Alignof(gm_tt7ek1a8bopcl) == 8 && offsetof(gm_tt7ek1a8bopcl, f0) == 0 && offsetof(gm_tt7ek1a8bopcl, f1) == 8, "layout of `Abort![]Attribute`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0d02142k4tu8 v1; } f1; } gm_ttlipffho4ijl;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnrgict8sthcv v1; } f1; } gm_tp1f3h8bodhg6;
_Static_assert(sizeof(gm_ttlipffho4ijl) == 72 && _Alignof(gm_ttlipffho4ijl) == 8 && offsetof(gm_ttlipffho4ijl, f0) == 0 && offsetof(gm_ttlipffho4ijl, f1) == 8, "layout of `Abort!Pattern`");
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
_Static_assert(sizeof(gm_tp1f3h8bodhg6) == 128 && _Alignof(gm_tp1f3h8bodhg6) == 8 && offsetof(gm_tp1f3h8bodhg6, f0) == 0 && offsetof(gm_tp1f3h8bodhg6, f1) == 8, "layout of `Abort!PatArg`");
struct gm_fr_after_parens_is_6ccqs6v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_finish_range_pattern_vl01cru {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* lo */
    gm_tc3oht4ksth9d l5; /* hi */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_mk_pat_o5qvg0u {
    gm_frame h;
    gm_to361lgknsu69 l1; /* k */
    gm_t0d02142k4tu8 ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_literal_bound_ivo05f4 {
    gm_frame h;
    gm_t2gtbh39ptekd l7; /* k */
    gm_tc3oht4ksth9d l8; /* lit */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_literal_pattern_fpgt11g {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* lo */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_parse_pat_arg_2gpdqgv {
    gm_frame h;
    gm_t1s75i08qtptd l2; /* name */
    gm_t0d02142k4tu8 l3; /* pat */
    gm_tp1f3h8bodhg6 ret;
};
struct gm_fr_parse_pat_args_jmv27hk {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_path_pattern_n2mad2q {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* path */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_parse_pattern_mggn9o4 {
    gm_frame h;
    gm_t0d02142k4tu8 l2; /* inner */
    gm_ttlipffho4ijl ret;
};
struct gm_fr_parse_qualified_path_00id0iv {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* path */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_tuple_pattern_0nrt2qh {
    gm_frame h;
    gm_ttlipffho4ijl ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_after_parens_is_6ccqs6v(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_finish_range_pattern_vl01cru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
inline __attribute__((aligned(4))) void gm_f_mk_pat_o5qvg0u(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_t0d02142k4tu8 gm_f_mk_pat_o5qvg0u_c(void * a0, gm_to361lgknsu69 a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_f7qroea(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_literal_bound_ivo05f4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_literal_pattern_fpgt11g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_pat_arg_2gpdqgv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_pat_args_jmv27hk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_path_pattern_n2mad2q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_pattern_mggn9o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_qualified_path_00id0iv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_tuple_pattern_0nrt2qh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_krkq6pebrgk[40] __attribute__((aligned(8)));
extern const uint8_t gm_s070mdv4o3o2t[28];
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s541gi95epov5[25];
extern const uint8_t gm_sbo1c1kpq53tj[31];
extern const uint8_t gm_semskfu4esg5u[17];
extern const uint8_t gm_skv53e6m2c88s[10];
extern const uint8_t gm_sn1sp4f9hibg6[10];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcpmlh93537vbl[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_srqm47b4oe5lm[72];
extern const uint8_t gm_stjafkt8gs4e7[13];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t3fcbuehh3bs2 t2;
    gm_f_kind_k239kpg((void *)&t2, t1);
    gm_t3fcbuehh3bs2 t3 = t2;
    gm_t9qkh0rsooc8d t4 = gm_l1;
    gm_t9qkh0rsooc8d t5 = t4;
    uint8_t t6 = (((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t3) + 8), ((uint8_t *)&t5))));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju t1 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t2 = t1;
    if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t3 = GM_LD(void *, (((uint8_t *)&t2) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = gm_l1;
        uint8_t t5 = gm_f_at_54906rm(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t10 = gm_l0;
            gm_tm4pc0emhrv1g t11;
            gm_f_bump_q2i40re((void *)&t11, t10);
        }
    }
    {
        uint8_t t12 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 120)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_fail_e5kqpus F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_b16_8 t3 = gm_l2;
    gm_t4gut2c9fj1kt t4;
    gm_f_error_s6hn871((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b0_1 t5 = gm_f_fail_diag_u0q4kp3(t1, &t4);
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_t3fcbuehh3bs2 t3 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t2) + 0));
    F.ret = t3;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_rb20u32 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
        uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 434, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 132), t7);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tl874t2atnlh3 t2 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t1));
    gm_tl874t2atnlh3 t3 = t2;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&t3) + 4));
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0, 4);
    void * t1 = gm_l0;
    uint32_t t2 = gm_f_next_id_a7upaf4(t1);
    gm_t2gtbh39ptekd t3 = F.l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0, t2);
    GM_ST(gm_t2gtbh39ptekd, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8, t3);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mk_pat_o5qvg0u(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_pat_o5qvg0u F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)gm_out) + 4, 0, 4);
    memset((uint8_t *)&(*(gm_t0d02142k4tu8 *)gm_out) + 60, 0, 4);
    void * t1 = gm_l0;
    uint32_t t2 = gm_f_next_id_a7upaf4(t1);
    gm_to361lgknsu69 t3 = F.l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t0d02142k4tu8 *)gm_out) + 0, t2);
    GM_ST(gm_to361lgknsu69, (uint8_t *)&(*(gm_t0d02142k4tu8 *)gm_out) + 8, t3);
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t0d02142k4tu8 *)gm_out) + 48, t4);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
        uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
        gm_l1 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 56));
            void * t8 = (void *)((uint8_t *)t7);
            gm_l2 = t8;
        }
        {
            void * t9 = gm_l2;
            void * t10 = gm_l2;
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)t10));
            uint32_t r12; bool o13 = __builtin_add_overflow(t11, ((uint32_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
            uint32_t t14 = r12;
            GM_ST(uint32_t, ((uint8_t *)t9), t14);
        }
    }
    {
        uint32_t t15 = gm_l1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    uint64_t t6 = gm_l1;
    uint64_t r7; bool o8 = __builtin_add_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t9 = r7;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 8) + 8);
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t14 = r12;
    uint64_t t15 = (t9 < t14 ? t9 : t14);
    uint64_t t16 = (uintptr_t)t15;
    gm_b16_8 t17 = t3;
    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&t17) + 8);
    if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcuiab49hlgv7g, 126, 62);
    gm_t3fcbuehh3bs2 t19 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t17)) + t16 * 48) + 0));
    F.ret = t19;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_t3fcbuehh3bs2 t3;
    gm_f_nth_c0r2nvs((void *)&t3, t1, t2);
    gm_t3fcbuehh3bs2 t4 = t3;
    gm_t9qkh0rsooc8d t5 = gm_l2;
    gm_t9qkh0rsooc8d t6 = t5;
    uint8_t t7 = (((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t4) + 8), ((uint8_t *)&t6))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    {
        void * t2 = gm_l0;
        gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_toh0ootodhjue t5 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    }
    {
        gm_toh0ootodhjue t6 = gm_l2;
        uint8_t t7 = 0;
        F.ret = t6;
        goto gm_fret;
    }
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_f_tok_ai6sc12(t1);
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 32));
    F.ret = t3;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_l0;
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 80));
    uint32_t t6 = gm_l1;
    uint32_t t7 = (t5 > t6 ? t5 : t6);
    gm_tl874t2atnlh3 t8;
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 8), t7);
    F.ret = t8;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 72));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
    uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
    if (o9) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t10 = r8;
    uint64_t t11 = (t5 < t10 ? t5 : t10);
    uint64_t t12 = (uintptr_t)t11;
    gm_b16_8 t13 = t3;
    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&t13) + 8);
    if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srcuiab49hlgv7g, 122, 56);
    void * t15 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&t13)) + t12 * 48);
    F.ret = t15;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_after_parens_is_6ccqs6v(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_after_parens_is_6ccqs6v F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    uint64_t gm_l2; /* depth */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb2;
            {
                void * t5 = gm_l0;
                uint64_t t6 = gm_l3;
                gm_t3fcbuehh3bs2 t7;
                gm_f_nth_c0r2nvs((void *)&t7, t5, t6);
                gm_t3fcbuehh3bs2 t8 = t7;
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma12; default: goto ma13; }
ma10:;
                uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t19 != ((uint8_t)8ULL)) goto pa18;
                uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t20 != ((uint8_t)61ULL)) goto pa18;
                goto po17;
pa18:;
                uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t22 != ((uint8_t)8ULL)) goto pa21;
                uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t23 != ((uint8_t)63ULL)) goto pa21;
                goto po17;
pa21:;
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t25 != ((uint8_t)8ULL)) goto pa24;
                uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t26 != ((uint8_t)65ULL)) goto pa24;
                goto po17;
pa24:;
                goto mn16;
po17:;
                {
                    uint64_t t28 = gm_l2;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    if (o30) gm_trap("integer overflow in `+`", gm_srcpmlh93537vbl, 119, 70);
                    uint64_t t31 = r29;
                    gm_l2 = t31;
                }
                goto me9;
mn16:;
ma11:;
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t35 != ((uint8_t)8ULL)) goto pa34;
                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t36 != ((uint8_t)62ULL)) goto pa34;
                goto po33;
pa34:;
                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t38 != ((uint8_t)8ULL)) goto pa37;
                uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t39 != ((uint8_t)64ULL)) goto pa37;
                goto po33;
pa37:;
                uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t41 != ((uint8_t)8ULL)) goto pa40;
                uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
                if (t42 != ((uint8_t)66ULL)) goto pa40;
                goto po33;
pa40:;
                goto mn32;
po33:;
                {
                    uint64_t t44 = gm_l2;
                    uint64_t r45; bool o46 = __builtin_sub_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = o46 ? (0 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r45;
                    gm_l2 = t47;
                }
                {
                    uint64_t t49 = gm_l2;
                    uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
                    if (!t50) goto ie48;
                    {
                        void * t52 = gm_l0;
                        uint64_t t53 = gm_l3;
                        uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                        if (o55) gm_trap("integer overflow in `+`", gm_srcpmlh93537vbl, 122, 49);
                        uint64_t t56 = r54;
                        gm_t9qkh0rsooc8d t57 = gm_l1;
                        uint8_t t58 = gm_f_nth_is_3fgqmcb(t52, t56, t57);
                        uint8_t t59 = 0;
                        F.ret = t58;
                        goto gm_fret;
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                goto me9;
mn32:;
ma12:;
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
                if (t61 != ((uint8_t)10ULL)) goto mn60;
                uint8_t t62 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me9;
mn60:;
ma13:;
                goto me9;
mn63:;
ma14:;
                gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 118, 9);
me9:;
            }
            {
                uint64_t t65 = gm_l3;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                if (o67) gm_trap("integer overflow in `+`", gm_srcpmlh93537vbl, 127, 9);
                uint64_t t68 = r66;
                gm_l3 = t68;
            }
lc2:;
        }
lb2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_finish_range_pattern_vl01cru(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_finish_range_pattern_vl01cru F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    uint8_t gm_l3; /* inclusive */
    uint8_t gm_l4; /* qualified */
    GM_STACK_CHECK(&F, 1414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3;
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)51ULL));
        uint8_t t4 = gm_f_at_54906rm(t2, t3);
        gm_l3 = t4;
    }
    {
        {
            void * t6 = gm_l0;
            gm_tm4pc0emhrv1g t7;
            gm_f_bump_q2i40re((void *)&t7, t6);
        }
    }
    {
        void * t8 = gm_l0;
        gm_t3fcbuehh3bs2 t9;
        gm_f_kind_k239kpg((void *)&t9, t8);
        gm_t3fcbuehh3bs2 t10 = t9;
        uint8_t t11;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        t11 = ((uint8_t)1);
        goto me12;
mn13:;
        t11 = ((uint8_t)0);
        goto me12;
mn15:;
        gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 74, 18);
me12:;
        gm_l4 = t11;
    }
    {
        gm_to1cssm8dfiv5 t16;
        uint8_t t18 = gm_l4;
        if (!t18) goto ie17;
        void * t20 = gm_l0;
        gm_to1cssm8dfiv5 t21;
        gm_f_parse_qualified_path_00id0iv((void *)&t21, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t22 = t21;
        t16 = t22;
        goto ix17;
ie17:;
        void * t24 = gm_l0;
        gm_to1cssm8dfiv5 t25;
        gm_f_parse_literal_bound_ivo05f4((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t26 = t25;
        t16 = t26;
ix17:;
        gm_to1cssm8dfiv5 t27 = t16;
        if (((((uint8_t *)&t27))[0] != 0)) {
            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
            gm_ttlipffho4ijl t29;
            memset(&t29, 0, sizeof t29);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
            F.ret = t29;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t30 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t27) + 8));
        F.l5 = t30;
    }
    {
        uint8_t t32 = gm_l4;
        if (!t32) goto ie31;
        uint8_t t33;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 8) + 0));
        if (t36 != ((uint8_t)22ULL)) goto mn35;
        t33 = ((uint8_t)1);
        goto me34;
mn35:;
        t33 = ((uint8_t)0);
        goto me34;
mn37:;
        gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 76, 27);
me34:;
        uint8_t t38 = (uint8_t)!t33;
        if (!t38) goto ie31;
        {
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 72));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_srqm47b4oe5lm));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(71));
            gm_b0_1 t43 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttlipffho4ijl t44;
            memset(&t44, 0, sizeof t44);
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t44 + 8), t43);
            gm_ttlipffho4ijl t45 = t44;
            uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
            F.ret = t44;
            goto gm_fret;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t47 = gm_l0;
        gm_tlcl4u558l6p5 t48;
        memset((uint8_t *)&t48 + 17, 0, 7);
        void * t49 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t48 + 0, t49);
        void * t50 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t48 + 8, t50);
        uint8_t t51 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&t48 + 16, t51);
        gm_to361lgknsu69 t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t52 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t52 + 8), t48);
        void * t53 = gm_l0;
        uint32_t t54 = gm_l1;
        gm_tl874t2atnlh3 t55 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t53, t54));
        gm_t0d02142k4tu8 t56;
        gm_f_mk_pat_o5qvg0u((void *)&t56, t47, &t52, GM_CONV(gm_cseahvg8v7p0vs, t55));
        gm_ttlipffho4ijl t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        ((uint8_t *)&t57)[0] = 0;
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t57 + 8), t56);
        gm_ttlipffho4ijl t58 = t57;
        uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
        F.ret = t57;
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
extern __attribute__((aligned(4))) void gm_f_mk_pat_o5qvg0u(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_t0d02142k4tu8 gm_f_mk_pat_o5qvg0u_c(void * a0, gm_to361lgknsu69 a1, gm_cseahvg8v7p0vs a2) { gm_t0d02142k4tu8 gm_r; gm_f_mk_pat_o5qvg0u(&gm_r, a0, &a1, a2); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_literal_bound_ivo05f4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_literal_bound_ivo05f4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    uint8_t gm_l2; /* negated */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* l */
    void * gm_l5; /* l */
    gm_tibupt4dmcqai gm_l6; /* l */
    GM_STACK_CHECK(&F, 1700u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
        uint8_t t6 = gm_f_eat_qmock9g(t4, t5);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_tl874t2atnlh3 t8 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t7));
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_t3fcbuehh3bs2 t10;
        gm_f_kind_k239kpg((void *)&t10, t9);
        gm_t3fcbuehh3bs2 t11 = t10;
        gm_t2gtbh39ptekd t12;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma19; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma19; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma19; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma19; default: goto ma19; }
ma14:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t23 != ((uint8_t)3ULL)) goto mn22;
        void * t24 = GM_LD(void *, (((uint8_t *)&t11) + 8));
        gm_l4 = t24;
        void * t25 = gm_l4;
        gm_t2gtbh39ptekd t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
        t12 = t26;
        goto me13;
mn22:;
ma15:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t28 != ((uint8_t)4ULL)) goto mn27;
        void * t29 = GM_LD(void *, (((uint8_t *)&t11) + 8));
        gm_l5 = t29;
        void * t30 = gm_l5;
        gm_t2gtbh39ptekd t31;
        memset(&t31, 0, sizeof t31);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)1ULL));
        GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
        t12 = t31;
        goto me13;
mn27:;
ma16:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t33 != ((uint8_t)5ULL)) goto mn32;
        gm_tibupt4dmcqai t34 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t11) + 8));
        gm_l6 = t34;
        uint8_t t35 = gm_l2;
        uint8_t t36 = (uint8_t)!t35;
        if (!t36) goto mn32;
        gm_tibupt4dmcqai t37 = gm_l6;
        gm_t2gtbh39ptekd t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)2ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t38 + 8), t37);
        t12 = t38;
        goto me13;
mn32:;
ma17:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&t11) + 8) + 0));
        if (t41 != ((uint8_t)25ULL)) goto mn39;
        uint8_t t42 = gm_l2;
        uint8_t t43 = (uint8_t)!t42;
        if (!t43) goto mn39;
        gm_t2gtbh39ptekd t44;
        memset(&t44, 0, sizeof t44);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t44 + 8), ((uint8_t)1));
        t12 = t44;
        goto me13;
mn39:;
ma18:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
        if (t46 != ((uint8_t)2ULL)) goto mn45;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)&t11) + 8) + 0));
        if (t47 != ((uint8_t)10ULL)) goto mn45;
        uint8_t t48 = gm_l2;
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) goto mn45;
        gm_t2gtbh39ptekd t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t50 + 8), ((uint8_t)0));
        t12 = t50;
        goto me13;
mn45:;
ma19:;
        void * t52 = gm_l0;
        gm_b16_8 t53;
        uint8_t t55 = gm_l2;
        if (!t55) goto ie54;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s070mdv4o3o2t));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(27));
        gm_b16_8 t58 = t57;
        t53 = t58;
        goto ix54;
ie54:;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_skv53e6m2c88s));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(9));
        gm_b16_8 t61 = t60;
        t53 = t61;
ix54:;
        gm_b0_1 t62 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t63;
        memset(&t63, 0, sizeof t63);
        ((uint8_t *)&t63)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t63 + 8), t62);
        gm_to1cssm8dfiv5 t64 = t63;
        uint8_t t65 = ((((uint8_t *)&t64))[0] != 0);
        F.ret = t63;
        goto gm_fret;
        goto me13;
mn51:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 56, 23);
me13:;
        F.l7 = t12;
    }
    {
        {
            void * t67 = gm_l0;
            gm_tm4pc0emhrv1g t68;
            gm_f_bump_q2i40re((void *)&t68, t67);
        }
    }
    {
        void * t69 = gm_l0;
        gm_tl874t2atnlh3 t70 = gm_l3;
        gm_f_mk_expr_53qj7nd((void *)&F.l8, t69, &(*(gm_t2gtbh39ptekd *)((uint8_t *)&F.l7)), GM_CONV(gm_cseahvg8v7p0vs, t70));
    }
    {
        uint8_t t72 = gm_l2;
        uint8_t t73 = (uint8_t)!t72;
        if (!t73) goto ie71;
        {
            gm_tc3oht4ksth9d t75 = F.l8;
            gm_to1cssm8dfiv5 t76;
            GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
            ((uint8_t *)&t76)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t76 + 8), t75);
            gm_to1cssm8dfiv5 t77 = t76;
            uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
            F.ret = t76;
            goto gm_fret;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        void * t79 = gm_l0;
        gm_t9qkh0rsooc8d t80;
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
        void * t81 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8ghgl2jj0qha t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t82 + 0), t80);
        GM_ST(void *, ((uint8_t *)&t82 + 8), t81);
        gm_t2gtbh39ptekd t83;
        memset(&t83, 0, sizeof t83);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)12ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t83 + 8), t82);
        void * t84 = gm_l0;
        uint32_t t85 = gm_l1;
        gm_tl874t2atnlh3 t86 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t84, t85));
        gm_tc3oht4ksth9d t87;
        gm_f_mk_expr_53qj7nd((void *)&t87, t79, &t83, GM_CONV(gm_cseahvg8v7p0vs, t86));
        gm_to1cssm8dfiv5 t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        ((uint8_t *)&t88)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t88 + 8), t87);
        gm_to1cssm8dfiv5 t89 = t88;
        uint8_t t90 = ((((uint8_t *)&t89))[0] != 0);
        F.ret = t88;
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
inline __attribute__((aligned(4))) void gm_f_parse_literal_pattern_fpgt11g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_literal_pattern_fpgt11g F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 706u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_to1cssm8dfiv5 t5;
        gm_f_parse_literal_bound_ivo05f4((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_ttlipffho4ijl t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        uint8_t t11 = 0;
        void * t12 = gm_l0;
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)50ULL));
        uint8_t t14 = gm_f_at_54906rm(t12, t13);
        if (!t14) {
            void * t15 = gm_l0;
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)51ULL));
            uint8_t t17 = gm_f_at_54906rm(t15, t16);
            t11 = t17;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            void * t19 = gm_l0;
            uint32_t t20 = gm_l1;
            gm_f_finish_range_pattern_vl01cru((void *)&(*(gm_ttlipffho4ijl *)gm_out), (void *)NULL, cx, NULL, t19, t20, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to361lgknsu69 t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        void * t24 = gm_l0;
        uint32_t t25 = gm_l1;
        gm_tl874t2atnlh3 t26 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t24, t25));
        gm_t0d02142k4tu8 t27;
        gm_f_mk_pat_o5qvg0u((void *)&t27, t21, &t23, GM_CONV(gm_cseahvg8v7p0vs, t26));
        gm_ttlipffho4ijl t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        ((uint8_t *)&t28)[0] = 0;
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t28 + 8), t27);
        gm_ttlipffho4ijl t29 = t28;
        uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
        F.ret = t28;
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
inline __attribute__((aligned(4))) void gm_f_parse_pat_arg_2gpdqgv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_pat_arg_2gpdqgv F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 1188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        gm_t1s75i08qtptd t4;
        memset(&t4, 0, sizeof t4);
        F.l2 = t4;
    }
    {
        void * t6 = gm_l0;
        uint8_t t7 = gm_f_nth_is_name_oqhoobd(t6, ((uint64_t)0ULL));
        if (!t7) goto ie5;
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)56ULL));
        uint8_t t10 = gm_f_nth_is_3fgqmcb(t8, ((uint64_t)1ULL), t9);
        if (!t10) goto ie5;
        {
            void * t12 = gm_l0;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_stjafkt8gs4e7));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(12));
            gm_t4jn8fl3lmk5k t14;
            gm_f_parse_name_figjvbe((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4jn8fl3lmk5k t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) {
                gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 8));
                gm_tp1f3h8bodhg6 t17;
                memset(&t17, 0, sizeof t17);
                ((uint8_t *)&t17)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), t16);
                F.ret = t17;
                goto gm_fret;
            }
            gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t15) + 8));
            gm_t1s75i08qtptd t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[32] = 1;
            F.l2 = t19;
        }
        {
            {
                void * t21 = gm_l0;
                gm_tm4pc0emhrv1g t22;
                gm_f_bump_q2i40re((void *)&t22, t21);
            }
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t23 = gm_l0;
        gm_ttlipffho4ijl t24;
        gm_f_parse_pattern_mggn9o4((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttlipffho4ijl t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 8));
            gm_tp1f3h8bodhg6 t27;
            memset(&t27, 0, sizeof t27);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t27 + 8), t26);
            F.ret = t27;
            goto gm_fret;
        }
        gm_t0d02142k4tu8 t28 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t25) + 8));
        F.l3 = t28;
    }
    {
        gm_tnrgict8sthcv t29;
        memset((uint8_t *)&t29 + 116, 0, 4);
        memcpy((uint8_t *)&t29 + 0, ((uint8_t *)&F.l2), 40);
        memcpy((uint8_t *)&t29 + 40, ((uint8_t *)&F.l3), 64);
        void * t30 = gm_l0;
        uint32_t t31 = gm_l1;
        gm_tl874t2atnlh3 t32 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t30, t31));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t29 + 104, t32);
        gm_tp1f3h8bodhg6 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        ((uint8_t *)&t33)[0] = 0;
        GM_ST(gm_tnrgict8sthcv, ((uint8_t *)&t33 + 8), t29);
        gm_tp1f3h8bodhg6 t34 = t33;
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t33;
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
inline __attribute__((aligned(4))) void gm_f_parse_pat_args_jmv27hk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_pat_args_jmv27hk F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 751u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_t9qkh0rsooc8d t4;
            GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)61ULL));
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
            gm_t0u0j6voh3loe t6 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t3, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0u0j6voh3loe t7 = t6;
            if (((((uint8_t *)&t7))[0] != 0)) {
                gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 4));
                gm_tt7ek1a8bopcl t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), t8);
                F.ret = t9;
                goto gm_fret;
            }
            gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t7) + 4));
        }
    }
    {
        void * t11 = gm_l0;
        gm_toh0ootodhjue t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t12 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t13 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t11, GM_CONV(gm_cs3cuou245tr5f, t12)));
        gm_l1 = t13;
    }
    {
    }
    {
        gm_f_list_ttksl9g((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
    }
    {
        for (;;) {
            void * t18 = gm_l0;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)62ULL));
            uint8_t t20 = gm_f_at_54906rm(t18, t19);
            uint8_t t21 = (uint8_t)!t20;
            if (!t21) goto lb17;
            {
                void * t24 = (void *)((uint8_t *)&F.l2);
                void * t25 = gm_l0;
                gm_tp1f3h8bodhg6 t26;
                gm_f_parse_pat_arg_2gpdqgv((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
                gm_tp1f3h8bodhg6 t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) {
                    gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
                    gm_tt7ek1a8bopcl t29;
                    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
                    ((uint8_t *)&t29)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), t28);
                    F.ret = t29;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ll22;
                }
                gm_tnrgict8sthcv t30 = GM_LD(gm_tnrgict8sthcv, (((uint8_t *)&t27) + 8));
                gm_tftb1odc739t5 t31 = gm_f_push_l74t9rq(t24, &t30);
                gm_tftb1odc739t5 t32 = t31;
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srcpmlh93537vbl, 138, 9);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            {
                void * t35 = gm_l0;
                gm_t9qkh0rsooc8d t36;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)57ULL));
                uint8_t t37 = gm_f_eat_qmock9g(t35, t36);
                uint8_t t38 = (uint8_t)!t37;
                if (!t38) goto ie34;
                {
                    goto lb17;
                }
                goto ix34;
ie34:;
ix34:;
            }
lc17:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
        }
        goto gm_ld1_1;
lb17:;
    }
    {
        {
            void * t41 = gm_l0;
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)62ULL));
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sbo1c1kpq53tj));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(30));
            gm_t0u0j6voh3loe t44 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t0u0j6voh3loe t45 = t44;
            if (((((uint8_t *)&t45))[0] != 0)) {
                gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 4));
                gm_tt7ek1a8bopcl t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t47 + 16, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t47 + 8), t46);
                F.ret = t47;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_tl874t2atnlh3 t48 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&t45) + 4));
        }
    }
    {
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_tt7ek1a8bopcl t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        ((uint8_t *)&t50)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
        gm_tt7ek1a8bopcl t51 = t50;
        uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
        F.ret = t50;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t52;
        goto gm_ld1_1;
    }
    {
        {
            void * t54 = gm_l0;
            gm_toh0ootodhjue t55 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t54) + 128), t55);
        }
    }
    goto gm_ls56;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        {
            void * t58 = gm_l0;
            gm_toh0ootodhjue t59 = gm_l1;
            GM_ST(gm_toh0ootodhjue, (((uint8_t *)t58) + 128), t59);
        }
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls56:;
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
__attribute__((aligned(4))) void gm_f_parse_path_pattern_n2mad2q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_path_pattern_n2mad2q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_th2lqbo77pvsc gm_l3; /* args */
    gm_b16_8 gm_l4; /* args */
    gm_t08olq97rscn3 gm_l5; /* id */
    GM_STACK_CHECK(&F, 2036u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_to1cssm8dfiv5 t5;
        gm_f_parse_qualified_path_00id0iv((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_ttlipffho4ijl t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_tc3oht4ksth9d t9 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)&t6) + 8));
        F.l2 = t9;
    }
    {
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)61ULL));
        uint8_t t13 = gm_f_at_54906rm(t11, t12);
        if (!t13) goto ie10;
        void * t14 = gm_l0;
        gm_t9qkh0rsooc8d t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)61ULL));
        uint8_t t16 = gm_f_after_parens_is_6ccqs6v(t14, t15);
        if (!t16) goto ie10;
        {
            void * t18 = gm_l0;
            gm_thk954m1ol1ig t19;
            gm_f_parse_call_args_32e205h((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thk954m1ol1ig t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) {
                gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 8));
                gm_ttlipffho4ijl t22;
                memset(&t22, 0, sizeof t22);
                ((uint8_t *)&t22)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t22 + 8), t21);
                F.ret = t22;
                goto gm_fret;
            }
            gm_th2lqbo77pvsc t23 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t20) + 8));
            gm_l3 = t23;
        }
        {
            void * t24 = gm_l0;
            gm_tlljg61a2ctgq t25;
            memset((uint8_t *)&t25 + 25, 0, 7);
            void * t26 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t25 + 0, t26);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 8, t27);
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 16));
            GM_ST(uint8_t, (uint8_t *)&t25 + 24, t28);
            gm_t2gtbh39ptekd t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)19ULL));
            GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t29 + 8), t25);
            void * t30 = gm_l0;
            uint32_t t31 = gm_l1;
            gm_tl874t2atnlh3 t32 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t30, t31));
            gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t24, &t29, GM_CONV(gm_cseahvg8v7p0vs, t32));
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t34 = gm_l0;
        gm_t9qkh0rsooc8d t35;
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)61ULL));
        uint8_t t36 = gm_f_at_54906rm(t34, t35);
        if (!t36) goto ie33;
        {
            void * t38 = gm_l0;
            gm_tt7ek1a8bopcl t39;
            gm_f_parse_pat_args_jmv27hk((void *)&t39, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tt7ek1a8bopcl t40 = t39;
            if (((((uint8_t *)&t40))[0] != 0)) {
                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 8));
                gm_ttlipffho4ijl t42;
                memset(&t42, 0, sizeof t42);
                ((uint8_t *)&t42)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t42 + 8), t41);
                F.ret = t42;
                goto gm_fret;
            }
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_l4 = t43;
        }
        {
            void * t44 = gm_l0;
            gm_tagc77imt2chm t45;
            void * t46 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(void *, (uint8_t *)&t45 + 0, t46);
            gm_b16_8 t47 = gm_l4;
            gm_th2lqbo77pvsc t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t48 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t48), t47);
            ((uint8_t *)&t48)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t45 + 8, t48);
            gm_to361lgknsu69 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)7ULL));
            GM_ST(gm_tagc77imt2chm, ((uint8_t *)&t49 + 8), t45);
            void * t50 = gm_l0;
            uint32_t t51 = gm_l1;
            gm_tl874t2atnlh3 t52 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t50, t51));
            gm_t0d02142k4tu8 t53;
            gm_f_mk_pat_o5qvg0u((void *)&t53, t44, &t49, GM_CONV(gm_cseahvg8v7p0vs, t52));
            gm_ttlipffho4ijl t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t54 + 8), t53);
            gm_ttlipffho4ijl t55 = t54;
            uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
            F.ret = t54;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 8) + 0));
        if (t58 != ((uint8_t)6ULL)) goto ie57;
        gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)&F.l2) + 8) + 8));
        gm_l5 = t59;
        {
            void * t61 = gm_l0;
            gm_t08olq97rscn3 t62 = gm_l5;
            gm_to361lgknsu69 t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t63 + 8), t62);
            void * t64 = gm_l0;
            uint32_t t65 = gm_l1;
            gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t64, t65));
            gm_t0d02142k4tu8 t67;
            gm_f_mk_pat_o5qvg0u((void *)&t67, t61, &t63, GM_CONV(gm_cseahvg8v7p0vs, t66));
            gm_ttlipffho4ijl t68;
            GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
            ((uint8_t *)&t68)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t68 + 8), t67);
            gm_ttlipffho4ijl t69 = t68;
            uint8_t t70 = ((((uint8_t *)&t69))[0] != 0);
            F.ret = t68;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
    }
    {
        uint8_t t72 = 0;
        void * t73 = gm_l0;
        gm_t9qkh0rsooc8d t74;
        GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)50ULL));
        uint8_t t75 = gm_f_at_54906rm(t73, t74);
        if (!t75) {
            void * t76 = gm_l0;
            gm_t9qkh0rsooc8d t77;
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)51ULL));
            uint8_t t78 = gm_f_at_54906rm(t76, t77);
            t72 = t78;
        } else t72 = 1;
        if (!t72) goto ie71;
        {
            void * t80 = gm_l0;
            uint32_t t81 = gm_l1;
            gm_f_finish_range_pattern_vl01cru((void *)&(*(gm_ttlipffho4ijl *)gm_out), (void *)NULL, cx, NULL, t80, t81, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix71;
ie71:;
ix71:;
    }
    {
        void * t82 = gm_l0;
        gm_tagc77imt2chm t83;
        void * t84 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t83 + 0, t84);
        gm_th2lqbo77pvsc t85;
        memset(&t85, 0, sizeof t85);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t83 + 8, t85);
        gm_to361lgknsu69 t86;
        GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tagc77imt2chm, ((uint8_t *)&t86 + 8), t83);
        void * t87 = gm_l0;
        uint32_t t88 = gm_l1;
        gm_tl874t2atnlh3 t89 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t87, t88));
        gm_t0d02142k4tu8 t90;
        gm_f_mk_pat_o5qvg0u((void *)&t90, t82, &t86, GM_CONV(gm_cseahvg8v7p0vs, t89));
        gm_ttlipffho4ijl t91;
        GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
        ((uint8_t *)&t91)[0] = 0;
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t91 + 8), t90);
        gm_ttlipffho4ijl t92 = t91;
        uint8_t t93 = ((((uint8_t *)&t92))[0] != 0);
        F.ret = t91;
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
__attribute__((aligned(4))) void gm_f_parse_pattern_mggn9o4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_pattern_mggn9o4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* path */
    gm_th2lqbo77pvsc gm_l5; /* args */
    gm_t08olq97rscn3 gm_l6; /* name */
    GM_STACK_CHECK(&F, 3443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_t9qkh0rsooc8d t3 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9qkh0rsooc8d t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
            gm_ttlipffho4ijl t6;
            memset(&t6, 0, sizeof t6);
            ((uint8_t *)&t6)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), t5);
            F.ret = t6;
            goto gm_fret;
        }
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
    }
    {
        void * t9 = gm_l0;
        uint32_t t10 = gm_f_lo_fp82gt4(t9);
        gm_l1 = t10;
    }
    {
        void * t11 = gm_l0;
        gm_t3fcbuehh3bs2 t12;
        gm_f_kind_k239kpg((void *)&t12, t11);
        gm_t3fcbuehh3bs2 t13 = t12;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        switch (t25) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma15; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma23; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma23; default: goto ma23; }
ma15:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t27 != ((uint8_t)1ULL)) goto mn26;
        {
            {
                void * t30 = gm_l0;
                gm_tm4pc0emhrv1g t31;
                gm_f_bump_q2i40re((void *)&t31, t30);
            }
        }
        {
            void * t32 = gm_l0;
            gm_to361lgknsu69 t33;
            memset(&t33, 0, sizeof t33);
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)0ULL));
            void * t34 = gm_l0;
            uint32_t t35 = gm_l1;
            gm_tl874t2atnlh3 t36 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t34, t35));
            gm_t0d02142k4tu8 t37;
            gm_f_mk_pat_o5qvg0u((void *)&t37, t32, &t33, GM_CONV(gm_cseahvg8v7p0vs, t36));
            gm_ttlipffho4ijl t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            ((uint8_t *)&t38)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t38 + 8), t37);
            gm_ttlipffho4ijl t39 = t38;
            uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
            F.ret = t38;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t40;
            goto gm_ld1_1;
        }
        goto me14;
mn26:;
ma16:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t42 != ((uint8_t)2ULL)) goto mn41;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t43 != ((uint8_t)19ULL)) goto mn41;
        {
            {
                void * t46 = gm_l0;
                gm_tm4pc0emhrv1g t47;
                gm_f_bump_q2i40re((void *)&t47, t46);
            }
        }
        {
            void * t48 = gm_l0;
            gm_to361lgknsu69 t49;
            memset(&t49, 0, sizeof t49);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)4ULL));
            void * t50 = gm_l0;
            uint32_t t51 = gm_l1;
            gm_tl874t2atnlh3 t52 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t50, t51));
            gm_t0d02142k4tu8 t53;
            gm_f_mk_pat_o5qvg0u((void *)&t53, t48, &t49, GM_CONV(gm_cseahvg8v7p0vs, t52));
            gm_ttlipffho4ijl t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t54 + 8), t53);
            gm_ttlipffho4ijl t55 = t54;
            uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
            F.ret = t54;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t56;
            goto gm_ld1_1;
        }
        goto me14;
mn41:;
ma17:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t58 != ((uint8_t)8ULL)) goto mn57;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t59 != ((uint8_t)46ULL)) goto mn57;
        {
            {
                void * t62 = gm_l0;
                gm_tm4pc0emhrv1g t63;
                gm_f_bump_q2i40re((void *)&t63, t62);
            }
        }
        {
            void * t64 = gm_l0;
            gm_ttlipffho4ijl t65;
            gm_f_parse_pattern_mggn9o4((void *)&t65, (void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_ttlipffho4ijl t66 = t65;
            if (((((uint8_t *)&t66))[0] != 0)) {
                gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 8));
                gm_ttlipffho4ijl t68;
                memset(&t68, 0, sizeof t68);
                ((uint8_t *)&t68)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t68 + 8), t67);
                F.ret = t68;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_t0d02142k4tu8 t69 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)&t66) + 8));
            F.l2 = t69;
        }
        {
            void * t70 = gm_l0;
            gm_tq7s98ihonkju t71 = gm_f_new_f7qroea((void *)NULL, cx, NULL, &(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krkq6pebrgk)));
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_tq7s98ihonkju t72 = t71;
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcpmlh93537vbl, 21, 34);
            void * t73 = GM_LD(void *, (((uint8_t *)&t72) + 8));
            gm_to361lgknsu69 t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)6ULL));
            GM_ST(void *, ((uint8_t *)&t74 + 8), t73);
            void * t75 = gm_l0;
            uint32_t t76 = gm_l1;
            gm_tl874t2atnlh3 t77 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t75, t76));
            gm_t0d02142k4tu8 t78;
            gm_f_mk_pat_o5qvg0u((void *)&t78, t70, &t74, GM_CONV(gm_cseahvg8v7p0vs, t77));
            gm_ttlipffho4ijl t79;
            GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
            ((uint8_t *)&t79)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t79 + 8), t78);
            gm_ttlipffho4ijl t80 = t79;
            uint8_t t81 = ((((uint8_t *)&t80))[0] != 0);
            F.ret = t79;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t81;
            goto gm_ld1_1;
        }
        goto me14;
mn57:;
ma18:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t83 != ((uint8_t)8ULL)) goto mn82;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t84 != ((uint8_t)58ULL)) goto mn82;
        {
            {
                void * t87 = gm_l0;
                gm_tm4pc0emhrv1g t88;
                gm_f_bump_q2i40re((void *)&t88, t87);
            }
        }
        {
            void * t89 = gm_l0;
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_s541gi95epov5));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(24));
            gm_t4jn8fl3lmk5k t91;
            gm_f_parse_ident_n2c6b75((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t4jn8fl3lmk5k t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) {
                gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 8));
                gm_ttlipffho4ijl t94;
                memset(&t94, 0, sizeof t94);
                ((uint8_t *)&t94)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t94 + 8), t93);
                F.ret = t94;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_t08olq97rscn3 t95 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t92) + 8));
            gm_l3 = t95;
        }
        {
            void * t96 = gm_l0;
            gm_t08olq97rscn3 t97 = gm_l3;
            gm_t2gtbh39ptekd t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t98 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t98 + 8), t97);
            void * t99 = gm_l0;
            uint32_t t100 = gm_l1;
            gm_tl874t2atnlh3 t101 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t99, t100));
            gm_tc3oht4ksth9d t102;
            gm_f_mk_expr_53qj7nd((void *)&t102, t96, &t98, GM_CONV(gm_cseahvg8v7p0vs, t101));
            void * t103 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t102);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_l4 = t103;
        }
        {
            gm_th2lqbo77pvsc t104;
            memset(&t104, 0, sizeof t104);
            gm_l5 = t104;
        }
        {
            void * t106 = gm_l0;
            gm_t9qkh0rsooc8d t107;
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)61ULL));
            uint8_t t108 = gm_f_at_54906rm(t106, t107);
            if (!t108) goto ie105;
            {
                void * t110 = gm_l0;
                gm_tt7ek1a8bopcl t111;
                gm_f_parse_pat_args_jmv27hk((void *)&t111, (void *)NULL, cx, NULL, t110);
                if (gm_unw.kind) {
                    goto gm_ld1_1;
                }
                gm_tt7ek1a8bopcl t112 = t111;
                if (((((uint8_t *)&t112))[0] != 0)) {
                    gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 8));
                    gm_ttlipffho4ijl t114;
                    memset(&t114, 0, sizeof t114);
                    ((uint8_t *)&t114)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t114 + 8), t113);
                    F.ret = t114;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                    goto gm_ld1_1;
                }
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                gm_th2lqbo77pvsc t116;
                GM_ST(uint8_t, (uint8_t *)&t116 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t116 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t116 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t116), t115);
                ((uint8_t *)&t116)[16] = 1;
                gm_l5 = t116;
            }
            goto ix105;
ie105:;
ix105:;
        }
        {
            void * t117 = gm_l0;
            gm_tagc77imt2chm t118;
            void * t119 = gm_l4;
            GM_ST(void *, (uint8_t *)&t118 + 0, t119);
            memcpy((uint8_t *)&t118 + 8, ((uint8_t *)&gm_l5), 24);
            gm_to361lgknsu69 t120;
            GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)7ULL));
            GM_ST(gm_tagc77imt2chm, ((uint8_t *)&t120 + 8), t118);
            void * t121 = gm_l0;
            uint32_t t122 = gm_l1;
            gm_tl874t2atnlh3 t123 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t121, t122));
            gm_t0d02142k4tu8 t124;
            gm_f_mk_pat_o5qvg0u((void *)&t124, t117, &t120, GM_CONV(gm_cseahvg8v7p0vs, t123));
            gm_ttlipffho4ijl t125;
            GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
            ((uint8_t *)&t125)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t125 + 8), t124);
            gm_ttlipffho4ijl t126 = t125;
            uint8_t t127 = ((((uint8_t *)&t126))[0] != 0);
            F.ret = t125;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t127;
            goto gm_ld1_1;
        }
        goto me14;
mn82:;
ma19:;
        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t129 != ((uint8_t)8ULL)) goto mn128;
        uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t130 != ((uint8_t)45ULL)) goto mn128;
        {
            {
                void * t133 = gm_l0;
                gm_tm4pc0emhrv1g t134;
                gm_f_bump_q2i40re((void *)&t134, t133);
            }
        }
        {
            void * t135 = gm_l0;
            gm_b16_8 t136;
            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_semskfu4esg5u));
            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(16));
            gm_t4jn8fl3lmk5k t137;
            gm_f_parse_ident_n2c6b75((void *)&t137, (void *)NULL, cx, NULL, t135, t136);
            if (gm_unw.kind) {
                goto gm_ld1_1;
            }
            gm_t4jn8fl3lmk5k t138 = t137;
            if (((((uint8_t *)&t138))[0] != 0)) {
                gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 8));
                gm_ttlipffho4ijl t140;
                memset(&t140, 0, sizeof t140);
                ((uint8_t *)&t140)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t140 + 8), t139);
                F.ret = t140;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 1;
                goto gm_ld1_1;
            }
            gm_t08olq97rscn3 t141 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t138) + 8));
            gm_l6 = t141;
        }
        {
            void * t142 = gm_l0;
            gm_t08olq97rscn3 t143 = gm_l6;
            gm_to361lgknsu69 t144;
            GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t144 + 8), t143);
            void * t145 = gm_l0;
            uint32_t t146 = gm_l1;
            gm_tl874t2atnlh3 t147 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t145, t146));
            gm_t0d02142k4tu8 t148;
            gm_f_mk_pat_o5qvg0u((void *)&t148, t142, &t144, GM_CONV(gm_cseahvg8v7p0vs, t147));
            gm_ttlipffho4ijl t149;
            GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
            ((uint8_t *)&t149)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t149 + 8), t148);
            gm_ttlipffho4ijl t150 = t149;
            uint8_t t151 = ((((uint8_t *)&t150))[0] != 0);
            F.ret = t149;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t151;
            goto gm_ld1_1;
        }
        goto me14;
mn128:;
ma20:;
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t153 != ((uint8_t)8ULL)) goto mn152;
        uint8_t t154 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t154 != ((uint8_t)61ULL)) goto mn152;
        void * t155 = gm_l0;
        gm_f_parse_tuple_pattern_0nrt2qh((void *)&F.ret, (void *)NULL, cx, NULL, t155);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t156 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t156;
        goto gm_ld1_1;
        goto me14;
mn152:;
ma21:;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t160 != ((uint8_t)8ULL)) goto pa159;
        uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t161 != ((uint8_t)1ULL)) goto pa159;
        goto po158;
pa159:;
        uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t163 != ((uint8_t)3ULL)) goto pa162;
        goto po158;
pa162:;
        uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t165 != ((uint8_t)4ULL)) goto pa164;
        goto po158;
pa164:;
        uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t167 != ((uint8_t)5ULL)) goto pa166;
        goto po158;
pa166:;
        uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t169 != ((uint8_t)2ULL)) goto pa168;
        uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t170 != ((uint8_t)25ULL)) goto pa168;
        goto po158;
pa168:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t172 != ((uint8_t)2ULL)) goto pa171;
        uint8_t t173 = GM_LD(uint8_t, ((((uint8_t *)&t13) + 8) + 0));
        if (t173 != ((uint8_t)10ULL)) goto pa171;
        goto po158;
pa171:;
        goto mn157;
po158:;
        void * t174 = gm_l0;
        gm_f_parse_literal_pattern_fpgt11g((void *)&F.ret, (void *)NULL, cx, NULL, t174);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t175 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t175;
        goto gm_ld1_1;
        goto me14;
mn157:;
ma22:;
        uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t177 != ((uint8_t)0ULL)) goto mn176;
        void * t178 = gm_l0;
        gm_f_parse_path_pattern_n2mad2q((void *)&F.ret, (void *)NULL, cx, NULL, t178);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        uint8_t t179 = ((((uint8_t *)&F.ret))[0] != 0);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t179;
        goto gm_ld1_1;
        goto me14;
mn176:;
ma23:;
        void * t181 = gm_l0;
        gm_b16_8 t182;
        GM_ST(void *, (uint8_t *)&t182, (void *)(gm_sn1sp4f9hibg6));
        GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(9));
        gm_b0_1 t183 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t181, t182);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_ttlipffho4ijl t184;
        memset(&t184, 0, sizeof t184);
        ((uint8_t *)&t184)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t184 + 8), t183);
        gm_ttlipffho4ijl t185 = t184;
        uint8_t t186 = ((((uint8_t *)&t185))[0] != 0);
        F.ret = t184;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t186;
        goto gm_ld1_1;
        goto me14;
mn180:;
ma24:;
        gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 9, 5);
me14:;
    }
    {
        void * t187 = gm_l0;
        gm_f_leave_rb20u32(t187);
    }
    goto gm_ls188;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t189 = gm_l0;
        gm_f_leave_rb20u32(t189);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls188:;
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
inline __attribute__((aligned(4))) void gm_f_parse_qualified_path_00id0iv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_qualified_path_00id0iv F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l1; /* id */
    gm_t08olq97rscn3 gm_l3; /* name */
    GM_STACK_CHECK(&F, 1217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1pvh8pfrs3ir));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_t4jn8fl3lmk5k t4;
        gm_f_parse_ident_n2c6b75((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4jn8fl3lmk5k t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t08olq97rscn3 t8 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
        gm_l1 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_t08olq97rscn3 t10 = gm_l1;
        gm_t2gtbh39ptekd t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t11 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)6ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t11 + 8), t10);
        gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l1) + 16));
        gm_f_mk_expr_53qj7nd((void *)&F.l2, t9, &t11, GM_CONV(gm_cseahvg8v7p0vs, t12));
    }
    {
        for (;;) {
            void * t14 = gm_l0;
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)58ULL));
            uint8_t t16 = gm_f_at_54906rm(t14, t15);
            if (!t16) goto lb13;
            void * t17 = gm_l0;
            gm_t3fcbuehh3bs2 t18;
            gm_f_nth_c0r2nvs((void *)&t18, t17, ((uint64_t)1ULL));
            gm_t3fcbuehh3bs2 t19 = t18;
            uint8_t t20;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
            if (t23 != ((uint8_t)0ULL)) goto mn22;
            t20 = ((uint8_t)1);
            goto me21;
mn22:;
            t20 = ((uint8_t)0);
            goto me21;
mn24:;
            gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 86, 26);
me21:;
            if (!t20) goto lb13;
            {
                {
                    void * t28 = gm_l0;
                    gm_tm4pc0emhrv1g t29;
                    gm_f_bump_q2i40re((void *)&t29, t28);
                }
            }
            {
                void * t30 = gm_l0;
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s1pvh8pfrs3ir));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(6));
                gm_t4jn8fl3lmk5k t32;
                gm_f_parse_ident_n2c6b75((void *)&t32, (void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_ll25;
                }
                gm_t4jn8fl3lmk5k t33 = t32;
                if (((((uint8_t *)&t33))[0] != 0)) {
                    gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 8));
                    gm_to1cssm8dfiv5 t35;
                    memset(&t35, 0, sizeof t35);
                    ((uint8_t *)&t35)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t35 + 8), t34);
                    F.ret = t35;
                    goto gm_fret;
                }
                gm_t08olq97rscn3 t36 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t33) + 8));
                gm_l3 = t36;
            }
            {
                void * t37 = gm_l0;
                gm_tioie2ju3q4ul t38;
                void * t39 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                if (gm_unw.kind) {
                    goto gm_ll25;
                }
                GM_ST(void *, (uint8_t *)&t38 + 0, t39);
                memcpy((uint8_t *)&t38 + 8, ((uint8_t *)&gm_l3), 32);
                gm_t2gtbh39ptekd t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)22ULL));
                GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t40 + 8), t38);
                void * t41 = gm_l0;
                uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)&gm_l1) + 16) + 4));
                gm_tl874t2atnlh3 t43 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t41, t42));
                gm_f_mk_expr_53qj7nd((void *)&(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)), t37, &t40, GM_CONV(gm_cseahvg8v7p0vs, t43));
            }
lc13:;
        }
gm_ll25:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
        }
        goto gm_lad;
lb13:;
    }
    {
        gm_tc3oht4ksth9d t44 = F.l2;
        gm_to1cssm8dfiv5 t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        ((uint8_t *)&t45)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t45 + 8), t44);
        gm_to1cssm8dfiv5 t46 = t45;
        uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
        F.ret = t45;
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
inline __attribute__((aligned(4))) void gm_f_parse_tuple_pattern_0nrt2qh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_tuple_pattern_0nrt2qh F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l2; /* args */
    uint8_t gm_l3; /* had_comma */
    gm_tl874t2atnlh3 gm_l4; /* whole */
    GM_STACK_CHECK(&F, 892u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_lo_fp82gt4(t2);
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_tt7ek1a8bopcl t5;
        gm_f_parse_pat_args_jmv27hk((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt7ek1a8bopcl t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
            gm_ttlipffho4ijl t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), t7);
            F.ret = t8;
            goto gm_fret;
        }
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l2 = t9;
    }
    {
        uint8_t t10 = 0;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 72));
        uint8_t t13 = (t12 >= ((uint64_t)2ULL));
        if (t13) {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 72));
            uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)2ULL), &r16);
            if (o17) gm_trap("integer overflow in `-`", gm_srcpmlh93537vbl, 161, 40);
            uint64_t t18 = r16;
            uint64_t t19 = (uintptr_t)t18;
            void * t20 = gm_l0;
            uint64_t t21 = GM_LD(uintptr_t, (((uint8_t *)t20) + 8) + 8);
            if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcpmlh93537vbl, 161, 33);
            gm_t3fcbuehh3bs2 t22 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, (((uint8_t *)t20) + 8)) + t19 * 48) + 0));
            gm_t3fcbuehh3bs2 t23 = t22;
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)57ULL));
            gm_t9qkh0rsooc8d t25 = t24;
            uint8_t t26 = (((GM_LD(uint8_t, ((uint8_t *)&t23) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t23) + 8), ((uint8_t *)&t25))));
            t10 = t26;
        } else t10 = 0;
        gm_l3 = t10;
    }
    {
        void * t27 = gm_l0;
        uint32_t t28 = gm_l1;
        gm_tl874t2atnlh3 t29 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t27, t28));
        gm_l4 = t29;
    }
    {
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t32 = ((t31 == ((uint64_t)1ULL)));
        if (!t32) goto ie30;
        uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcpmlh93537vbl, 163, 26);
        uint8_t t35;
        if ((((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t33 * 120) + 0))[32] != 0)) goto mn37;
        t35 = ((uint8_t)1);
        goto me36;
mn37:;
        t35 = ((uint8_t)0);
        goto me36;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_srcpmlh93537vbl, 163, 26);
me36:;
        if (!t35) goto ie30;
        uint8_t t39 = gm_l3;
        uint8_t t40 = (uint8_t)!t39;
        if (!t40) goto ie30;
        {
            uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_srcpmlh93537vbl, 163, 74);
            gm_t0d02142k4tu8 t44 = GM_LD(gm_t0d02142k4tu8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t42 * 120) + 40));
            gm_ttlipffho4ijl t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
            ((uint8_t *)&t45)[0] = 0;
            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t45 + 8), t44);
            gm_ttlipffho4ijl t46 = t45;
            uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
            F.ret = t45;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t48 = gm_l0;
        gm_b16_8 t49 = gm_l2;
        gm_to361lgknsu69 t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t50 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)8ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
        gm_tl874t2atnlh3 t51 = gm_l4;
        gm_t0d02142k4tu8 t52;
        gm_f_mk_pat_o5qvg0u((void *)&t52, t48, &t50, GM_CONV(gm_cseahvg8v7p0vs, t51));
        gm_ttlipffho4ijl t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        ((uint8_t *)&t53)[0] = 0;
        GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t53 + 8), t52);
        gm_ttlipffho4ijl t54 = t53;
        uint8_t t55 = ((((uint8_t *)&t54))[0] != 0);
        F.ret = t53;
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
