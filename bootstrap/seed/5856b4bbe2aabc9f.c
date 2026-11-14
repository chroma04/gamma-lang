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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; } f1; } gm_t6h2b31csotun;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_th2lqbo77pvsc v1; } f1; } gm_tccklsmdasd9s;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_tk9n3vlila9qg f1; } gm_toa5kh9gm9738;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_b16_8 v1; } f1; } gm_tptmds653bni0;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_ti0g3bmheuq9o v1; } f1; } gm_t171adjpb0m86;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0g3bmheuq9o f2; } gm_t6b36gvdk984b;
_Static_assert(sizeof(gm_t6h2b31csotun) == 32 && _Alignof(gm_t6h2b31csotun) == 8 && offsetof(gm_t6h2b31csotun, f0) == 0 && offsetof(gm_t6h2b31csotun, f1) == 8, "layout of `ManifestError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_ti0g3bmheuq9o v1; } f1; } gm_tacj1cskem694;
_Static_assert(sizeof(gm_tccklsmdasd9s) == 32 && _Alignof(gm_tccklsmdasd9s) == 8 && offsetof(gm_tccklsmdasd9s, f0) == 0 && offsetof(gm_tccklsmdasd9s, f1) == 8, "layout of `ManifestError!?string`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_toa5kh9gm9738) == 48 && _Alignof(gm_toa5kh9gm9738) == 8 && offsetof(gm_toa5kh9gm9738, f0) == 0 && offsetof(gm_toa5kh9gm9738, f1) == 8, "layout of `(i: usize, x: LintRule)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tptmds653bni0) == 32 && _Alignof(gm_tptmds653bni0) == 8 && offsetof(gm_tptmds653bni0, f0) == 0 && offsetof(gm_tptmds653bni0, f1) == 8, "layout of `ManifestError![]const string`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t171adjpb0m86) == 160 && _Alignof(gm_t171adjpb0m86) == 8 && offsetof(gm_t171adjpb0m86, f0) == 0 && offsetof(gm_t171adjpb0m86, f1) == 8, "layout of `string!BuildSettings`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t6b36gvdk984b) == 176 && _Alignof(gm_t6b36gvdk984b) == 8 && offsetof(gm_t6b36gvdk984b, f0) == 0 && offsetof(gm_t6b36gvdk984b, f1) == 16 && offsetof(gm_t6b36gvdk984b, f2) == 24, "layout of `Profile`");
_Static_assert(sizeof(gm_tacj1cskem694) == 160 && _Alignof(gm_tacj1cskem694) == 8 && offsetof(gm_tacj1cskem694, f0) == 0 && offsetof(gm_tacj1cskem694, f1) == 8, "layout of `ManifestError!BuildSettings`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tnlfbo6r4vlhg v1; } f1; } gm_tmn9qm2bp7f46;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
_Static_assert(sizeof(gm_tmn9qm2bp7f46) == 104 && _Alignof(gm_tmn9qm2bp7f46) == 8 && offsetof(gm_tmn9qm2bp7f46, f0) == 0 && offsetof(gm_tmn9qm2bp7f46, f1) == 8, "layout of `ManifestError!Source`");
struct gm_fr_as_str_982o16v {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_build_list_f41mb8u {
    gm_frame h;
    gm_trc3jdv2g5qre l0; /* e */
    gm_tptmds653bni0 ret;
};
struct gm_fr_build_settings_brkieib {
    gm_frame h;
    gm_t6b36gvdk984b l5; /* p */
    gm_trqfvmjtt4sij l6; /* known */
    gm_t6b36gvdk984b l9; /* p */
    gm_t171adjpb0m86 ret;
};
struct gm_fr_byte_text_9ib4q2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_subdir_2uljb06 {
    gm_frame h;
    gm_t6h2b31csotun ret;
};
struct gm_fr_debug_char_p6i205h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_debug_escape_t8t1h0j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_dep_string_mmgrsgi {
    gm_frame h;
    gm_tccklsmdasd9s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lqrk916 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lrh8b84 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmhr0p3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_vgf3305 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_control_65ubfpo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_hex_digit_q8s6utq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_start_rjefpfm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_identifier_kvlcjjo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_or_control_3uo6i3o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_subdir_sjdvkdc {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lint_rule_o3ccae1 {
    gm_frame h;
    gm_tk9n3vlila9qg l4; /* r */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_list_7393u00 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7mv9vi7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hs66gbq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lower_ascii_uggh3ca {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_mfail_uqq7o7g {
    gm_frame h;
    gm_tf1bjg81u0glv ret;
};
struct gm_fr_name_problem_6r4mp11 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_normalize_url_faghdja {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_overlay_pjf5fej {
    gm_frame h;
    gm_ti0g3bmheuq9o l0; /* base */
    gm_ti0g3bmheuq9o l1; /* over */
    gm_ti0g3bmheuq9o ret;
};
struct gm_fr_parse_build_c2teosa {
    gm_frame h;
    gm_ti0g3bmheuq9o l2; /* b */
    gm_trc3jdv2g5qre l5; /* e */
    gm_tacj1cskem694 ret;
};
struct gm_fr_parse_dependency_8qmcjrm {
    gm_frame h;
    gm_tktr4rmbindgv l1; /* item */
    gm_trc3jdv2g5qre l6; /* e */
    gm_trqfvmjtt4sij l11; /* sources */
    gm_tfeab279c16rr l12; /* <tmp> */
    gm_t9bmh80edou9c l13; /* <tmp> */
    gm_t0uuhaljsf410 l18; /* <tmp> */
    gm_tdmi4fk9nhqa7 l19; /* <tmp> */
    gm_t0uuhaljsf410 l37; /* <tmp> */
    gm_tdmi4fk9nhqa7 l38; /* <tmp> */
    gm_trqfvmjtt4sij l44; /* present */
    gm_t0uuhaljsf410 l45; /* <tmp> */
    gm_tdmi4fk9nhqa7 l46; /* <tmp> */
    gm_toq2f2q4opoa3 l50; /* spec */
    gm_tin97tt4ud0s1 l53; /* req */
    gm_tmn9qm2bp7f46 ret;
};
struct gm_fr_parse_manifest_b61s3ln {
    gm_frame h;
    gm_tm43kqe3e8b4a l3; /* m */
    gm_trc3jdv2g5qre l16; /* e */
    gm_tnlfbo6r4vlhg l18; /* source */
    gm_trc3jdv2g5qre l25; /* e */
    gm_trc3jdv2g5qre l31; /* e */
    gm_ti0g3bmheuq9o l40; /* settings */
    gm_tjgmtdrk88t7m ret;
};
struct gm_fr_spec_equal_foa1umn {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* a */
    gm_toq2f2q4opoa3 l1; /* b */
    gm_tin97tt4ud0s1 l2; /* x */
    gm_tin97tt4ud0s1 l3; /* y */
    uint8_t ret;
};
struct gm_fr_spec_text_9k2gjft {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* s */
    gm_tin97tt4ud0s1 l1; /* r */
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_array_bn13c0i {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_trqfvmjtt4sij l1; /* items */
    gm_trqfvmjtt4sij l2; /* out */
    gm_tktr4rmbindgv l5; /* it */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_to_lower_0fu6efk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_ascii_c9j36d1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_url_problem_38kdh3a {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqrk916(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lrh8b84(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmhr0p3(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_23qvjoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_64tgs30(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6cnv7j9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6ld92gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_850ujed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_86t06at(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_97etovl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9d8u2dd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gs0k8gk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i3a6tth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ivhanlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_mcrh28a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mvlqlcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p73hfi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p8qi5m3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uqgrksh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_usjmuj4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_control_65ubfpo(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_7393u00(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7mv9vi7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hs66gbq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1);
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7hnmra3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_dob2kfb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pdknc9g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_req_equal_7ia8ip6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_req_string_fk059jr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s7d377q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
extern uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
extern uint8_t gm_cb60tlkkq8o[152] __attribute__((aligned(8)));
extern uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
extern uint8_t gm_cfcggcra0bi[128] __attribute__((aligned(8)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern uint8_t gm_ct5id4947nt[144] __attribute__((aligned(8)));
extern void *const gm_hl5aqi4imlms[];
extern uint8_t gm_klkc6gi5fh5[40] __attribute__((aligned(8)));
extern uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2nmq6ak9nl9d[5];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s4b0873bt7c1q[5];
extern const uint8_t gm_s5ojatldfpfs2[8];
extern const uint8_t gm_s5tee1epg2e5a[24];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6v08aos37nfe[8];
extern const uint8_t gm_s7j1ioljmsuka[19];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_sdbqk29a3n8jm[13];
extern const uint8_t gm_setdfq42nhg1q[5];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfag3nu74qn7r[31];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_sh4f06c2ecgj1[5];
extern const uint8_t gm_shl9ceh0q5tpe[6];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slc3n2ku5uff5[55];
extern const uint8_t gm_slfpvqm0f02j6[8];
extern const uint8_t gm_slllafvm0pbeb[6];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_sna94ju8c8em3[48];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_sob5t135sl7q9[34];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_sp6f2rgoi8toe[7];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_sq3r7vbcbs4ll[26];
extern const uint8_t gm_sq6e9d3107t3s[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcc24pnihhgkug[];
extern const uint8_t gm_srcf8l485c5uq[10];
extern const char gm_srcg0ns322676oc[];
extern const char gm_srci5ni72s6oj5f[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_su0gvqh5vvvlj[37];
extern const uint8_t gm_sv1gci5lo7scg[5];
extern const uint8_t gm_svkmjs6p8bhtg[9];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_982o16v F;
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_th2lqbo77pvsc t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t7), t6);
    ((uint8_t *)&t7)[16] = 1;
    t1 = t7;
    goto me2;
mn3:;
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    t1 = t9;
    goto me2;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 50, 51);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t4 != ((uint8_t)4ULL)) goto mn3;
    void * t5 = GM_LD(void *, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    void * t6 = gm_l1;
    t1 = t6;
    goto me2;
mn3:;
    t1 = ((void *)0);
    goto me2;
mn7:;
    gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_byte_text_9ib4q2b F;
    uint8_t gm_l0; /* c */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tabb710d9mq2e t2;
        gm_f_make_2ip78bj((void *)&t2, (void *)NULL, cx, NULL, ((uint64_t)1ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klkc6gi5fh5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t2))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t2) + 8, gm_srcg0ns322676oc, 199, 10);
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 8));
        gm_l1 = t3;
    }
    {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srcg0ns322676oc, 200, 5);
        uint8_t t6 = gm_l0;
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t4 * 1), t6);
    }
    {
        gm_b16_8 t7 = gm_l1;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqrk916(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lqrk916 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lrh8b84(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lrh8b84 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 176)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmhr0p3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmhr0p3 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_control_65ubfpo(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_control_65ubfpo F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint8_t)32ULL));
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)127ULL)));
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_start_rjefpfm F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_or_control_3uo6i3o F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_control_65ubfpo(t4);
        t1 = t5;
    } else t1 = 1;
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
inline __attribute__((aligned(4))) void gm_f_list_7393u00(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7393u00 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lqrk916();
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
inline __attribute__((aligned(4))) void gm_f_list_7mv9vi7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7mv9vi7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_qmhr0p3();
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
inline __attribute__((aligned(4))) void gm_f_list_hs66gbq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hs66gbq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lrh8b84();
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
inline __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mfail_uqq7o7g F;
    uint64_t gm_l0; /* line */
    gm_b16_8 gm_l1; /* msg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l0;
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tf1bjg81u0glv *)gm_out) + 0, t1);
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tf1bjg81u0glv *)gm_out) + 8, t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_with_6vvt01e F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t t12 = (uintptr_t)t11;
        if (t10 > t12 || t12 > (uintptr_t)t8) gm_trap_slice(t10, t12, t8, gm_srcc24pnihhgkug, 416, 12);
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + t10 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12 - t10));
        gm_b16_8 t14 = gm_l1;
        uint8_t t15 = (gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_lower_0fu6efk F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t3 = gm_l0;
    uint8_t t4 = gm_f_is_upper_iekrftp(t3);
    if (!t4) goto ie2;
    uint8_t t6 = gm_l0;
    uint8_t r7; bool o8 = __builtin_add_overflow(t6, ((uint8_t)32ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srci5ni72s6oj5f, 42, 53);
    uint8_t t9 = r7;
    uint8_t t10 = t9;
    t1 = t10;
    goto ix2;
ie2:;
    uint8_t t12 = gm_l0;
    uint8_t t13 = t12;
    t1 = t13;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1) {
    struct gm_fr_build_list_f41mb8u F;
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 305u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        gm_th2lqbo77pvsc t2;
        gm_f_string_array_bn13c0i((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tktr4rmbindgv *)(((uint8_t *)a0) + 16)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tptmds653bni0 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 0));
            gm_tptmds653bni0 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
            ((uint8_t *)&t5)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
            t3 = t5;
        } else {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)a0) + 64));
            gm_t6j6smj7qoctu t7;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
            gm_b16_8 t9 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
            gm_tabb710d9mq2e t10;
            gm_f_format_p73hfi0((void *)&t10, (void *)NULL, cx, NULL, &t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcg0ns322676oc, 471, 54);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tf1bjg81u0glv t13;
            gm_f_mfail_uqq7o7g((void *)&t13, t6, t12);
            gm_tptmds653bni0 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t14 + 8), t13);
            t3 = t14;
        }
        uint8_t t15 = ((((uint8_t *)&t3))[0] != 0);
        F.ret = t3;
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
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_build_settings_brkieib F;
    void * gm_l0; /* m */
    gm_th2lqbo77pvsc gm_l1; /* profile */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1198u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_th2lqbo77pvsc t2 = gm_l1;
        gm_b16_8 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            void * t5 = gm_l0;
            const uint8_t *t6 = (((uint8_t *)t5) + 88);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out))[0] = 0;
            memmove((uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 8, t6, 152);
            goto gm_fdone;
        }
        gm_l2 = t3;
    }
    {
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 240) + 0));
            gm_l3 = t9;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l4;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t6b36gvdk984b t21 = GM_LD(gm_t6b36gvdk984b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 176));
                    F.l5 = t21;
                    {
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t25 = gm_l2;
                        uint8_t t26 = (gm_eq0iqiubfm7e(((uint8_t *)&t24), ((uint8_t *)&t25)));
                        if (!t26) goto ie23;
                        {
                            void * t28 = gm_l0;
                            gm_ti0g3bmheuq9o t29;
                            gm_f_overlay_pjf5fej((void *)&t29, &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)t28) + 88)), &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)&F.l5) + 24)));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out))[0] = 0;
                            GM_ST(gm_ti0g3bmheuq9o, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 8, t29);
                            goto gm_fdone;
                        }
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 388, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t31 = gm_l4;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l4 = t34;
                }
            }
lb10:;
        }
    }
    {
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 240) + 0) + 8);
        uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
        if (!t38) goto ie35;
        {
            gm_b16_8 t40 = gm_l2;
            gm_tnpifv20irnph t41;
            GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
            gm_tabb710d9mq2e t42;
            gm_f_format_jrdns83((void *)&t42, (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcg0ns322676oc, 392, 17);
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
            memset(&(*(gm_t171adjpb0m86 *)gm_out), 0, sizeof (*(gm_t171adjpb0m86 *)gm_out));
            ((uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 8, t44);
            goto gm_fdone;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t46 = gm_l0;
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((((uint8_t *)t46) + 240) + 0));
            gm_l7 = t47;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l8;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l8;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t6b36gvdk984b t59 = GM_LD(gm_t6b36gvdk984b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t55 * 176));
                    F.l9 = t59;
                    {
                        void * t61 = (void *)((uint8_t *)&F.l6);
                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        gm_tftb1odc739t5 t63 = gm_f_push_3ef746b(t61, t62);
                        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcg0ns322676oc, 395, 33);
                        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                    }
                    goto me57;
mn58:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 395, 5);
me57:;
                }
lc48:;
                {
                    uint64_t t66 = gm_l8;
                    uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                    uint64_t t69 = r67;
                    gm_l8 = t69;
                }
            }
lb48:;
        }
    }
    {
        gm_t6j6smj7qoctu t70;
        gm_b16_8 t71 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t70 + 0, t71);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t75;
        gm_f_join_sfknljb((void *)&t75, (void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcg0ns322676oc, 396, 75);
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t70 + 16, t76);
        gm_tabb710d9mq2e t77;
        gm_f_format_7p2ook8((void *)&t77, (void *)NULL, cx, NULL, &t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcg0ns322676oc, 396, 13);
        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
        memset(&(*(gm_t171adjpb0m86 *)gm_out), 0, sizeof (*(gm_t171adjpb0m86 *)gm_out));
        ((uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t171adjpb0m86 *)gm_out) + 8, t79);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_check_subdir_2uljb06 F;
    gm_b16_8 gm_l0; /* name */
    gm_th2lqbo77pvsc gm_l1; /* subdir */
    uint64_t gm_l2; /* line */
    gm_b16_8 gm_l3; /* s */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l3 = t3;
        {
            gm_b16_8 t6 = gm_l3;
            uint8_t t7 = gm_f_is_subdir_sjdvkdc((void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)!t7;
            if (!t8) goto ie5;
            {
                uint64_t t10 = gm_l2;
                gm_b16_8 t11 = gm_l0;
                gm_tnpifv20irnph t12;
                GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
                gm_tabb710d9mq2e t13;
                gm_f_format_86t06at((void *)&t13, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srcg0ns322676oc, 586, 33);
                gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
                gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
                gm_tf1bjg81u0glv t16;
                gm_f_mfail_uqq7o7g((void *)&t16, t10, t15);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t6h2b31csotun *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 8, t16);
                goto gm_fdone;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b0_1 t17;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t6h2b31csotun *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_t6h2b31csotun *)gm_out) + 8, t17);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_debug_char_p6i205h F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint8_t)10ULL))) goto mn4;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s460n81rl2c9c));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(4));
    t2 = t6;
    goto me3;
mn4:;
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t8 == ((uint8_t)9ULL))) goto mn7;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sh4f06c2ecgj1));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(4));
    t2 = t9;
    goto me3;
mn7:;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t11 == ((uint8_t)13ULL))) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_setdfq42nhg1q));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
    t2 = t12;
    goto me3;
mn10:;
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t14 == ((uint8_t)39ULL))) goto mn13;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s2nmq6ak9nl9d));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
    t2 = t15;
    goto me3;
mn13:;
    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t1));
    if (!(t17 == ((uint8_t)92ULL))) goto mn16;
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4b0873bt7c1q));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
    t2 = t18;
    goto me3;
mn16:;
    gm_b16_8 t20;
    uint8_t t22 = 0;
    uint8_t t23 = gm_l0;
    uint8_t t24 = (t23 < ((uint8_t)32ULL));
    if (!t24) {
        uint8_t t25 = gm_l0;
        uint8_t t26 = ((t25 == ((uint8_t)127ULL)));
        t22 = t26;
    } else t22 = 1;
    if (!t22) goto ie21;
    uint8_t t28 = gm_l0;
    gm_t9qkh0rsooc8d t29;
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), t28);
    gm_tabb710d9mq2e t30;
    gm_f_format_llhsr0b((void *)&t30, (void *)NULL, cx, NULL, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcg0ns322676oc, 194, 37);
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
    gm_b16_8 t32 = t31;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    t20 = t33;
    goto ix21;
ie21:;
    uint8_t t35 = gm_l0;
    gm_b16_8 t36 = gm_f_byte_text_9ib4q2b((void *)NULL, cx, NULL, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t37;
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
    gm_tabb710d9mq2e t38;
    gm_f_format_3aass3a((void *)&t38, (void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_srcg0ns322676oc, 194, 78);
    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
    gm_b16_8 t40 = t39;
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
    t20 = t41;
ix21:;
    t2 = t20;
    goto me3;
mn19:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 188, 50);
me3:;
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_debug_escape_t8t1h0j F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)&t13));
                    gm_l4 = t16;
                    {
                        uint8_t t18 = gm_l4;
                        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t21 == ((uint8_t)10ULL))) goto mn20;
                        void * t22 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t23;
                        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcg0ns322676oc, 209, 21);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                        goto me19;
mn20:;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t27 == ((uint8_t)9ULL))) goto mn26;
                        void * t28 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t28, t29);
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcg0ns322676oc, 210, 21);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                        goto me19;
mn26:;
                        uint8_t t33 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t33 == ((uint8_t)13ULL))) goto mn32;
                        void * t34 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t35;
                        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t34, t35);
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcg0ns322676oc, 211, 21);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        goto me19;
mn32:;
                        uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t39 == ((uint8_t)34ULL))) goto mn38;
                        void * t40 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t41;
                        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t40, t41);
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcg0ns322676oc, 212, 20);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                        goto me19;
mn38:;
                        uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t45 == ((uint8_t)39ULL))) goto mn44;
                        void * t46 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t47;
                        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sj2jft9n0jm50));
                        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcg0ns322676oc, 213, 21);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        goto me19;
mn44:;
                        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        if (!(t51 == ((uint8_t)92ULL))) goto mn50;
                        void * t52 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t53;
                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t54 = gm_f_extend_0cs85gs(t52, t53);
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcg0ns322676oc, 214, 21);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        goto me19;
mn50:;
                        uint8_t t58 = 0;
                        uint8_t t59 = gm_l4;
                        uint8_t t60 = (t59 < ((uint8_t)32ULL));
                        if (!t60) {
                            uint8_t t61 = gm_l4;
                            uint8_t t62 = ((t61 == ((uint8_t)127ULL)));
                            t58 = t62;
                        } else t58 = 1;
                        if (!t58) goto ie57;
                        {
                            void * t64 = (void *)((uint8_t *)&F.l1);
                            uint8_t t65 = gm_l4;
                            gm_t9qkh0rsooc8d t66;
                            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), t65);
                            gm_tftb1odc739t5 t67 = gm_f_write_kki8q0t(t64, t66);
                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 1, gm_srcg0ns322676oc, 215, 45);
                            gm_b0_1 t68 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 1));
                        }
                        goto ix57;
ie57:;
                        {
                            void * t70 = (void *)((uint8_t *)&F.l1);
                            uint8_t t71 = gm_l4;
                            gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t70, t71);
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcg0ns322676oc, 215, 83);
                            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
                        }
ix57:;
                        goto me19;
mn56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 208, 9);
me19:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 207, 5);
me14:;
                }
lc4:;
                {
                    uint64_t t75 = gm_l3;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    uint64_t t78 = r76;
                    gm_l3 = t78;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
        uint8_t t81 = 0;
        F.ret = t80;
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
inline __attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_dep_string_mmgrsgi F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* e */
    gm_b16_8 gm_l4; /* s */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_f_get_9rme0gb(t2, t3);
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out))[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 8, t7);
            goto gm_fdone;
        }
        gm_l3 = t5;
    }
    {
        void * t8 = gm_l3;
        gm_th2lqbo77pvsc t9;
        gm_f_as_str_982o16v((void *)&t9, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t8) + 16)));
        gm_b16_8 t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            void * t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 64));
            gm_t6j6smj7qoctu t14;
            gm_b16_8 t15 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
            gm_b16_8 t16 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
            gm_tabb710d9mq2e t17;
            gm_f_format_fqmocq1((void *)&t17, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcg0ns322676oc, 479, 53);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_tf1bjg81u0glv t20;
            gm_f_mfail_uqq7o7g((void *)&t20, t13, t19);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 8, t20);
            goto gm_fdone;
        }
        gm_l4 = t10;
    }
    {
        gm_b16_8 t21 = gm_l4;
        gm_th2lqbo77pvsc t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
        ((uint8_t *)&t22)[16] = 1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out))[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tccklsmdasd9s *)gm_out) + 8, t22);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vgf3305 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = 0;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
        if (!t8) {
            gm_b16_8 t9 = gm_l1;
            gm_tfeab279c16rr t10;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_snenkhfev166s));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sp7ec3ksfdilk));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 32, t13);
            uint8_t t14 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 0, ((uint8_t *)&t9))) t14 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 16, ((uint8_t *)&t9))) t14 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 32, ((uint8_t *)&t9))) t14 = 1;
            uint8_t t15 = (uint8_t)(t14);
            t6 = t15;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            uint8_t t17 = 0;
            ((struct gm_fr_is_subdir_sjdvkdc *)((&F.h)->up))->ret = ((uint8_t)0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t17;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 270, 5);
me1:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_q8s6utq F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    {
        uint8_t t4 = gm_l0;
        gm_l1 = t4;
    }
    uint8_t t5 = 0;
    uint8_t t6 = gm_l1;
    uint8_t t7 = (((uint8_t)48ULL) <= t6);
    if (t7) {
        uint8_t t8 = gm_l1;
        uint8_t t9 = (t8 <= ((uint8_t)57ULL));
        t5 = t9;
    } else t5 = 0;
    uint8_t t10 = t5;
    if (!t10) {
        {
            uint8_t t12 = gm_l0;
            gm_l2 = t12;
        }
        uint8_t t13 = 0;
        uint8_t t14 = gm_l2;
        uint8_t t15 = (((uint8_t)97ULL) <= t14);
        if (t15) {
            uint8_t t16 = gm_l2;
            uint8_t t17 = (t16 <= ((uint8_t)102ULL));
            t13 = t17;
        } else t13 = 0;
        uint8_t t18 = t13;
        t2 = t18;
    } else t2 = 1;
    if (!t2) {
        {
            uint8_t t20 = gm_l0;
            gm_l3 = t20;
        }
        uint8_t t21 = 0;
        uint8_t t22 = gm_l3;
        uint8_t t23 = (((uint8_t)65ULL) <= t22);
        if (t23) {
            uint8_t t24 = gm_l3;
            uint8_t t25 = (t24 <= ((uint8_t)70ULL));
            t21 = t25;
        } else t21 = 0;
        uint8_t t26 = t21;
        t1 = t26;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_identifier_kvlcjjo F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcg0ns322676oc, 153, 34);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
        uint8_t t11 = gm_f_is_ident_start_rjefpfm(t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie7;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        {
            gm_b16_8 t16 = gm_l0;
            gm_l1 = t16;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l2;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l2;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 1));
                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t26));
                    gm_l3 = t29;
                    {
                        uint8_t t32 = gm_l3;
                        uint8_t t33 = gm_f_is_ident_cont_317fpdb(t32);
                        uint8_t t34 = (uint8_t)!t33;
                        if (!t34) goto ie31;
                        {
                            uint8_t t36 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 154, 5);
me27:;
                }
lc17:;
                {
                    uint64_t t38 = gm_l2;
                    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                    uint64_t t41 = r39;
                    gm_l2 = t41;
                }
            }
lb17:;
        }
    }
    {
        uint8_t t42 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_subdir_sjdvkdc F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            gm_b16_8 t6 = gm_l0;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
            uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t12 = gm_l0;
            gm_l1 = t12;
        }
        gm_hval t13 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn14 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl5aqi4imlms, .env = t13.env, .hctx = t13.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t17 = gm_l1;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
        gm_b0_1 t19 = gm_f_split_obtinjv_s7d377q((void *)NULL, (&wn14), NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_ll16;
        }
        F.w0 = t19;
        goto gm_ls20;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_lad;
gm_ls20:;
we15:;
        gm_b0_1 t21 = F.w0;
        gm_b0_1 t22 = t21;
    }
    {
        uint8_t t23 = 0;
        F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lint_rule_o3ccae1 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 300u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = ((uint8_t *)gm_c42ctv8im24);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_srcg0ns322676oc, 378, 27);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
            gm_l1 = t4;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    gm_toa5kh9gm9738 t11;
                    uint64_t t12 = gm_l2;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 40), 40);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t11) + 0));
                    gm_l3 = t18;
                    F.l4 = (*(gm_tk9n3vlila9qg *)(((uint8_t *)&t11) + 8));
                    {
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t22 = gm_l0;
                        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
                        if (!t23) goto ie20;
                        {
                            uint64_t t25 = gm_l3;
                            gm_tf5srh4mp4b3i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[8] = 1;
                            uint8_t t27 = 0;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto ix20;
ie20:;
ix20:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 378, 5);
me16:;
                }
lc5:;
                {
                    uint64_t t29 = gm_l2;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l2 = t32;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t33;
        memset(&t33, 0, sizeof t33);
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lower_ascii_uggh3ca F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_tabb710d9mq2e t3;
        gm_f_make_2ip78bj((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ko53fduln7u)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_srcg0ns322676oc, 258, 12);
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
        gm_l1 = t4;
    }
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_l2 = t6;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l3;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 1));
                    gm_tf5srh4mp4b3i t18;
                    GM_ST(uint8_t, (uint8_t *)&t18 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t18 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t18 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t13);
                    GM_ST(uint8_t, ((uint8_t *)&t18 + 8), t17);
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l4 = t21;
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t18) + 8));
                    gm_l5 = t22;
                    {
                        uint64_t t24 = gm_l4;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcg0ns322676oc, 259, 32);
                        uint8_t *t27 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t25 * 1);
                        uint8_t t28 = gm_l5;
                        uint8_t t29 = gm_f_to_lower_0fu6efk(t28);
                        GM_ST(uint8_t, t27, t29);
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 259, 5);
me19:;
                }
lc7:;
                {
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                }
            }
lb7:;
        }
    }
    {
        gm_b16_8 t35 = gm_l1;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        uint8_t t37 = 0;
        F.ret = t36;
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
extern __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1) { gm_tf1bjg81u0glv gm_r; gm_f_mfail_uqq7o7g(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_name_problem_6r4mp11 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* k */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* k */
    GM_STACK_CHECK(&F, 1318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        uint8_t t4 = gm_f_is_identifier_kvlcjjo(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_t6j6smj7qoctu t7;
            gm_b16_8 t8 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
            gm_b16_8 t9 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
            gm_tabb710d9mq2e t10;
            gm_f_format_rp4le08((void *)&t10, (void *)NULL, cx, NULL, &t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcg0ns322676oc, 168, 16);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t12);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t14 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t14) goto ie13;
        {
            gm_b16_8 t16 = gm_l1;
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_tabb710d9mq2e t18;
            gm_f_format_i3a6tth((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srcg0ns322676oc, 170, 26);
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t20);
            goto gm_fdone;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        {
            void * t22 = ((uint8_t *)gm_cka4oohkshv);
            if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcg0ns322676oc, 171, 5);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t22 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
            gm_l2 = t23;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l3;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t31 * 16));
                    gm_l4 = t35;
                    {
                        gm_b16_8 t38 = gm_l4;
                        gm_b16_8 t39 = gm_l0;
                        uint8_t t40 = (gm_eq0iqiubfm7e(((uint8_t *)&t38), ((uint8_t *)&t39)));
                        if (!t40) goto ie37;
                        {
                            gm_t6j6smj7qoctu t42;
                            gm_b16_8 t43 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
                            gm_b16_8 t44 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t44);
                            gm_tabb710d9mq2e t45;
                            gm_f_format_usjmuj4((void *)&t45, (void *)NULL, cx, NULL, &t42);
                            if (gm_unw.kind) {
                                goto gm_ll28;
                            }
                            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcg0ns322676oc, 172, 28);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t47);
                            goto gm_fdone;
                        }
                        goto ix37;
ie37:;
ix37:;
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 171, 5);
me33:;
                }
lc24:;
                {
                    uint64_t t49 = gm_l3;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l3 = t52;
                }
            }
gm_ll28:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
    }
    {
        {
            void * t54 = ((uint8_t *)gm_cev3sfhfj2p);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcg0ns322676oc, 174, 5);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t54 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_l5 = t55;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t57 = gm_l6;
                uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t59 = (t57 < t58);
                if (!t59) goto lb56;
                {
                    uint64_t t62 = gm_l6;
                    uint64_t t63 = (uintptr_t)t62;
                    uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t63 * 16));
                    gm_l7 = t67;
                    {
                        gm_b16_8 t70 = gm_l7;
                        gm_b16_8 t71 = gm_l0;
                        uint8_t t72 = (gm_eq0iqiubfm7e(((uint8_t *)&t70), ((uint8_t *)&t71)));
                        if (!t72) goto ie69;
                        {
                            gm_t6j6smj7qoctu t74;
                            gm_b16_8 t75 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t75);
                            gm_b16_8 t76 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t76);
                            gm_tabb710d9mq2e t77;
                            gm_f_format_usjmuj4((void *)&t77, (void *)NULL, cx, NULL, &t74);
                            if (gm_unw.kind) {
                                goto gm_ll60;
                            }
                            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcg0ns322676oc, 175, 28);
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t79);
                            goto gm_fdone;
                        }
                        goto ix69;
ie69:;
ix69:;
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 174, 5);
me65:;
                }
lc56:;
                {
                    uint64_t t81 = gm_l6;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l6 = t84;
                }
            }
gm_ll60:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb56; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc56; }
            }
            goto gm_lad;
lb56:;
        }
    }
    {
        uint8_t t86 = 0;
        gm_b16_8 t87 = gm_l0;
        uint8_t t88 = gm_f_is_reserved_type_name_tuq4qsj(t87);
        if (!t88) {
            gm_b16_8 t89 = gm_l0;
            gm_tfeab279c16rr t90;
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t91);
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t92);
            gm_b16_8 t93;
            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t90 + 32, t93);
            uint8_t t94 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t90) + 0, ((uint8_t *)&t89))) t94 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t90) + 16, ((uint8_t *)&t89))) t94 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t90) + 32, ((uint8_t *)&t89))) t94 = 1;
            uint8_t t95 = (uint8_t)(t94);
            t86 = t95;
        } else t86 = 1;
        if (!t86) goto ie85;
        {
            gm_t6j6smj7qoctu t97;
            gm_b16_8 t98 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 0, t98);
            gm_b16_8 t99 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t97 + 16, t99);
            gm_tabb710d9mq2e t100;
            gm_f_format_mvlqlcp((void *)&t100, (void *)NULL, cx, NULL, &t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 8, gm_srcg0ns322676oc, 178, 16);
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&t100) + 8));
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t102);
            goto gm_fdone;
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        gm_b16_8 t104 = gm_l0;
        gm_t6j6smj7qoctu t105;
        gm_b16_8 t106;
        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s8sv6qdepk5ba));
        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t105 + 0, t106);
        gm_b16_8 t107;
        GM_ST(void *, (uint8_t *)&t107, (void *)(gm_st3p5nb1e6cc8));
        GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t105 + 16, t107);
        uint8_t t108 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t105) + 0, ((uint8_t *)&t104))) t108 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t105) + 16, ((uint8_t *)&t104))) t108 = 1;
        uint8_t t109 = (uint8_t)(t108);
        if (!t109) goto ie103;
        {
            gm_t6j6smj7qoctu t111;
            gm_b16_8 t112 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
            gm_b16_8 t113 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t113);
            gm_tabb710d9mq2e t114;
            gm_f_format_gs0k8gk((void *)&t114, (void *)NULL, cx, NULL, &t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcg0ns322676oc, 181, 16);
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t116);
            goto gm_fdone;
        }
        goto ix103;
ie103:;
ix103:;
    }
    {
        gm_th2lqbo77pvsc t117;
        memset(&t117, 0, sizeof t117);
        uint8_t t118 = 0;
        F.ret = t117;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_normalize_url_faghdja F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* u */
    GM_STACK_CHECK_SHALLOW(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        for (;;) {
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
            uint8_t t7 = gm_f_ends_with_biriu5l(t5, t6);
            if (!t7) goto lb4;
            {
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 239, 39);
                uint64_t t16 = r14;
                uint64_t t17 = (uintptr_t)t16;
                if (t12 > t17 || t17 > (uintptr_t)t10) gm_trap_slice(t12, t17, t10, gm_srcg0ns322676oc, 239, 34);
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t11 + t12 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17 - t12));
                gm_l1 = t18;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sp7ec3ksfdilk));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
        uint8_t t22 = gm_f_ends_with_biriu5l(t20, t21);
        if (!t22) goto ie19;
        {
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)4ULL), &r28);
            if (o29) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 241, 18);
            uint64_t t30 = r28;
            uint64_t t31 = (uintptr_t)t30;
            if (t26 > t31 || t31 > (uintptr_t)t24) gm_trap_slice(t26, t31, t24, gm_srcg0ns322676oc, 241, 13);
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
            gm_l1 = t32;
        }
        {
            for (;;) {
                gm_b16_8 t34 = gm_l1;
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(1));
                uint8_t t36 = gm_f_ends_with_biriu5l(t34, t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                    uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 242, 43);
                    uint64_t t45 = r43;
                    uint64_t t46 = (uintptr_t)t45;
                    if (t41 > t46 || t46 > (uintptr_t)t39) gm_trap_slice(t41, t46, t39, gm_srcg0ns322676oc, 242, 38);
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t40 + t41 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t41));
                    gm_l1 = t47;
                }
lc33:;
            }
lb33:;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_b16_8 t48 = gm_l1;
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_overlay_pjf5fej F;
    GM_STACK_CHECK_SHALLOW(&F, 444u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        memset((uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 4, 0, 4);
        gm_taari1s0m51a7 t2 = GM_LD(gm_taari1s0m51a7, (((uint8_t *)a1) + 0));
        gm_taari1s0m51a7 t3;
        if (((((uint8_t *)&t2))[1] != 0)) {
            t3 = t2;
        } else {
            gm_taari1s0m51a7 t4 = GM_LD(gm_taari1s0m51a7, (((uint8_t *)a0) + 0));
            t3 = t4;
        }
        gm_tqs5p4n2tq19v t5 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a1) + 2));
        gm_tqs5p4n2tq19v t6;
        if (((((uint8_t *)&t5))[1] != 0)) {
            t6 = t5;
        } else {
            gm_tqs5p4n2tq19v t7 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a0) + 2));
            t6 = t7;
        }
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 8));
        gm_th2lqbo77pvsc t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            t9 = t8;
        } else {
            gm_th2lqbo77pvsc t10 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 8));
            t9 = t10;
        }
        gm_th2lqbo77pvsc t11 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 32));
        gm_th2lqbo77pvsc t12;
        if (((((uint8_t *)&t11))[16] != 0)) {
            t12 = t11;
        } else {
            gm_th2lqbo77pvsc t13 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 32));
            t12 = t13;
        }
        gm_th2lqbo77pvsc t14 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 56));
        gm_th2lqbo77pvsc t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            t15 = t14;
        } else {
            gm_th2lqbo77pvsc t16 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 56));
            t15 = t16;
        }
        gm_th2lqbo77pvsc t17 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 80));
        gm_th2lqbo77pvsc t18;
        if (((((uint8_t *)&t17))[16] != 0)) {
            t18 = t17;
        } else {
            gm_th2lqbo77pvsc t19 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 80));
            t18 = t19;
        }
        gm_th2lqbo77pvsc t20 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 104));
        gm_th2lqbo77pvsc t21;
        if (((((uint8_t *)&t20))[16] != 0)) {
            t21 = t20;
        } else {
            gm_th2lqbo77pvsc t22 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 104));
            t21 = t22;
        }
        gm_th2lqbo77pvsc t23 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 128));
        gm_th2lqbo77pvsc t24;
        if (((((uint8_t *)&t23))[16] != 0)) {
            t24 = t23;
        } else {
            gm_th2lqbo77pvsc t25 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 128));
            t24 = t25;
        }
        GM_ST(gm_taari1s0m51a7, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 0, t3);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 2, t6);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 8, t9);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 32, t12);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 56, t15);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 80, t18);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 104, t21);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 128, t24);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1) { gm_ti0g3bmheuq9o gm_r; gm_f_overlay_pjf5fej(&gm_r, &a0, &a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_build_c2teosa F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l6; /* line */
    gm_b16_8 gm_l7; /* k */
    uint8_t gm_l8; /* known */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* x */
    gm_b16_8 gm_l12; /* s */
    uint8_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* l */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* p */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* n */
    uint8_t gm_l22; /* bad */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint8_t gm_l25; /* c */
    GM_STACK_CHECK(&F, 3023u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&F.l2 + 4, 0, 4);
        gm_taari1s0m51a7 t2;
        memset(&t2, 0, sizeof t2);
        GM_ST(gm_taari1s0m51a7, (uint8_t *)&F.l2 + 0, t2);
        gm_tqs5p4n2tq19v t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&F.l2 + 2, t3);
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 8, t4);
        gm_th2lqbo77pvsc t5;
        memset(&t5, 0, sizeof t5);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 32, t5);
        gm_th2lqbo77pvsc t6;
        memset(&t6, 0, sizeof t6);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 56, t6);
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 80, t7);
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 104, t8);
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 128, t9);
    }
    {
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 0) + 0));
            gm_l3 = t12;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l4;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l4;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_trc3jdv2g5qre t24 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 72));
                    F.l5 = t24;
                    {
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 64));
                        gm_l6 = t26;
                    }
                    {
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_l7 = t27;
                    }
                    {
                        gm_l8 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t29 = ((uint8_t *)gm_cfcggcra0bi);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcg0ns322676oc, 418, 9);
                            gm_b16_8 t30;
                            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t29 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_l9 = t30;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t32 = gm_l10;
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t34 = (t32 < t33);
                                if (!t34) goto lb31;
                                {
                                    uint64_t t37 = gm_l10;
                                    uint64_t t38 = (uintptr_t)t37;
                                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t38 * 16));
                                    gm_l11 = t42;
                                    {
                                        gm_b16_8 t45 = gm_l11;
                                        gm_b16_8 t46 = gm_l7;
                                        uint8_t t47 = (gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
                                        if (!t47) goto ie44;
                                        {
                                            gm_l8 = ((uint8_t)1);
                                        }
                                        goto ix44;
ie44:;
ix44:;
                                    }
                                    goto me40;
mn41:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 418, 9);
me40:;
                                }
lc31:;
                                {
                                    uint64_t t50 = gm_l10;
                                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                                    uint64_t t53 = r51;
                                    gm_l10 = t53;
                                }
                            }
lb31:;
                        }
                    }
                    {
                        uint8_t t55 = gm_l8;
                        uint8_t t56 = (uint8_t)!t55;
                        if (!t56) goto ie54;
                        {
                            uint64_t t58 = gm_l6;
                            gm_tfeab279c16rr t59;
                            gm_b16_8 t60 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
                            gm_b16_8 t61 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t61);
                            void * t62 = ((uint8_t *)gm_cfcggcra0bi);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcg0ns322676oc, 422, 109);
                            gm_b16_8 t63;
                            GM_ST(void *, (uint8_t *)&t63, (void *)(((uint8_t *)t62 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_b16_8 t64;
                            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t65;
                            gm_f_join_sfknljb((void *)&t65, (void *)NULL, cx, NULL, t63, t64);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcg0ns322676oc, 422, 100);
                            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t59 + 32, t66);
                            gm_tabb710d9mq2e t67;
                            gm_f_format_23qvjoj((void *)&t67, (void *)NULL, cx, NULL, &t59);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcg0ns322676oc, 422, 33);
                            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_tf1bjg81u0glv t70;
                            gm_f_mfail_uqq7o7g((void *)&t70, t58, t69);
                            memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                            ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t70);
                            goto gm_fdone;
                        }
                        goto ix54;
ie54:;
ix54:;
                    }
                    {
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sgdafdv2n5c25, 4) == 0)) goto mn72;
                        {
                            gm_th2lqbo77pvsc t74;
                            gm_f_as_str_982o16v((void *)&t74, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l5) + 16)));
                            gm_b16_8 t75;
                            if (((((uint8_t *)&t74))[16] != 0)) {
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 0));
                                t75 = t76;
                            } else {
                                uint64_t t77 = gm_l6;
                                gm_b16_8 t78 = gm_l1;
                                gm_tnpifv20irnph t79;
                                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
                                gm_tabb710d9mq2e t80;
                                gm_f_format_4eslq57((void *)&t80, (void *)NULL, cx, NULL, t79);
                                if (gm_unw.kind) {
                                    goto gm_ll17;
                                }
                                if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srcg0ns322676oc, 426, 63);
                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                gm_tf1bjg81u0glv t83;
                                gm_f_mfail_uqq7o7g((void *)&t83, t77, t82);
                                memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                                ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t83);
                                goto gm_fdone;
                            }
                            gm_l12 = t75;
                        }
                        {
                            gm_taari1s0m51a7 t84;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_shl9ceh0q5tpe, 5) == 0)) goto mn86;
                            gm_t9qkh0rsooc8d t87;
                            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)0ULL));
                            gm_taari1s0m51a7 t88;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88), t87);
                            ((uint8_t *)&t88)[1] = 1;
                            t84 = t88;
                            goto me85;
mn86:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_s02419ud122hi, 7) == 0)) goto mn89;
                            gm_t9qkh0rsooc8d t90;
                            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)1ULL));
                            gm_taari1s0m51a7 t91;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t91), t90);
                            ((uint8_t *)&t91)[1] = 1;
                            t84 = t91;
                            goto me85;
mn89:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_s6iuue4hdrbt9, 4) == 0)) goto mn92;
                            gm_t9qkh0rsooc8d t93;
                            GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)2ULL));
                            gm_taari1s0m51a7 t94;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t94), t93);
                            ((uint8_t *)&t94)[1] = 1;
                            t84 = t94;
                            goto me85;
mn92:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_sl0oflhpdomi7, 4) == 0)) goto mn95;
                            gm_t9qkh0rsooc8d t96;
                            GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)3ULL));
                            gm_taari1s0m51a7 t97;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t97), t96);
                            ((uint8_t *)&t97)[1] = 1;
                            t84 = t97;
                            goto me85;
mn95:;
                            uint64_t t99 = gm_l6;
                            gm_t6j6smj7qoctu t100;
                            gm_b16_8 t101 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
                            gm_b16_8 t102 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t102);
                            gm_tabb710d9mq2e t103;
                            gm_f_format_mcrh28a((void *)&t103, (void *)NULL, cx, NULL, &t100);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 8, gm_srcg0ns322676oc, 432, 46);
                            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&t103) + 8));
                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                            gm_tf1bjg81u0glv t106;
                            gm_f_mfail_uqq7o7g((void *)&t106, t99, t105);
                            memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                            ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t106);
                            goto gm_fdone;
                            goto me85;
mn98:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 427, 26);
me85:;
                            GM_ST(gm_taari1s0m51a7, (((uint8_t *)&F.l2) + 0), t84);
                        }
                        goto me71;
mn72:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sq6e9d3107t3s, 10) == 0)) goto mn107;
                        {
                            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 0));
                            if (t110 != ((uint8_t)2ULL)) goto ie109;
                            uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 8));
                            gm_l13 = t111;
                            {
                                uint8_t t113 = gm_l13;
                                gm_tqs5p4n2tq19v t114;
                                GM_ST(uint8_t, ((uint8_t *)&t114), t113);
                                ((uint8_t *)&t114)[1] = 1;
                                GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&F.l2) + 2), t114);
                            }
                            goto ix109;
ie109:;
                            {
                                uint64_t t116 = gm_l6;
                                gm_b16_8 t117 = gm_l1;
                                gm_tnpifv20irnph t118;
                                GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t117);
                                gm_tabb710d9mq2e t119;
                                gm_f_format_6cnv7j9((void *)&t119, (void *)NULL, cx, NULL, t118);
                                if (gm_unw.kind) {
                                    goto gm_ll17;
                                }
                                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_srcg0ns322676oc, 436, 62);
                                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                                gm_tf1bjg81u0glv t122;
                                gm_f_mfail_uqq7o7g((void *)&t122, t116, t121);
                                memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                                ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t122);
                                goto gm_fdone;
                            }
ix109:;
                        }
                        goto me71;
mn107:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sp6f2rgoi8toe, 6) == 0)) goto mn123;
                        {
                            gm_b16_8 t125 = gm_l1;
                            gm_tptmds653bni0 t126;
                            gm_f_build_list_f41mb8u((void *)&t126, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t125);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t126))[0] != 0)) {
                                gm_tacj1cskem694 t127;
                                memset(&t127, 0, sizeof t127);
                                ((uint8_t *)&t127)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t127 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t126) + 8)));
                                F.ret = t127;
                                goto gm_fret;
                            }
                            gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 8)) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 8)) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 8)) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 8)))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 8)) + 0, t128);
                        }
                        goto me71;
mn123:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_slfpvqm0f02j6, 7) == 0)) goto mn129;
                        {
                            gm_b16_8 t131 = gm_l1;
                            gm_tptmds653bni0 t132;
                            gm_f_build_list_f41mb8u((void *)&t132, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t131);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t132))[0] != 0)) {
                                gm_tacj1cskem694 t133;
                                memset(&t133, 0, sizeof t133);
                                ((uint8_t *)&t133)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t133 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t132) + 8)));
                                F.ret = t133;
                                goto gm_fret;
                            }
                            gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 32)) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 32)) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 32)) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 32)))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 32)) + 0, t134);
                        }
                        goto me71;
mn129:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_srcf8l485c5uq, 9) == 0)) goto pa137;
                        goto po136;
pa137:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sdbqk29a3n8jm, 12) == 0)) goto pa138;
                        goto po136;
pa138:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_svkmjs6p8bhtg, 8) == 0)) goto pa139;
                        goto po136;
pa139:;
                        goto mn135;
po136:;
                        {
                            gm_b16_8 t141 = gm_l1;
                            gm_tptmds653bni0 t142;
                            gm_f_build_list_f41mb8u((void *)&t142, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t141);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t142))[0] != 0)) {
                                gm_tacj1cskem694 t143;
                                memset(&t143, 0, sizeof t143);
                                ((uint8_t *)&t143)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t143 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t142) + 8)));
                                F.ret = t143;
                                goto gm_fret;
                            }
                            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t142) + 8));
                            gm_l14 = t144;
                        }
                        {
                            {
                                gm_b16_8 t146 = gm_l14;
                                gm_l15 = t146;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t148 = gm_l16;
                                    uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t150 = (t148 < t149);
                                    if (!t150) goto lb147;
                                    {
                                        uint64_t t153 = gm_l16;
                                        uint64_t t154 = (uintptr_t)t153;
                                        uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t154 * 16));
                                        gm_l17 = t158;
                                        {
                                            uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                            uint8_t t162 = ((t161 == ((uint64_t)0ULL)));
                                            if (!t162) goto ie160;
                                            {
                                                uint64_t t164 = gm_l6;
                                                gm_t6j6smj7qoctu t165;
                                                gm_b16_8 t166 = gm_l7;
                                                GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
                                                gm_b16_8 t167 = gm_l1;
                                                GM_ST(gm_b16_8, (uint8_t *)&t165 + 16, t167);
                                                gm_tabb710d9mq2e t168;
                                                gm_f_format_6ld92gp((void *)&t168, (void *)NULL, cx, NULL, &t165);
                                                if (gm_unw.kind) {
                                                    goto gm_ll151;
                                                }
                                                if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 8, gm_srcg0ns322676oc, 444, 57);
                                                gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
                                                gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                                                gm_tf1bjg81u0glv t171;
                                                gm_f_mfail_uqq7o7g((void *)&t171, t164, t170);
                                                memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                                                ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t171);
                                                goto gm_fdone;
                                            }
                                            goto ix160;
ie160:;
ix160:;
                                        }
                                        goto me156;
mn157:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 443, 17);
me156:;
                                    }
lc147:;
                                    {
                                        uint64_t t173 = gm_l16;
                                        uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                                        uint64_t t176 = r174;
                                        gm_l16 = t176;
                                    }
                                }
gm_ll151:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb147; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc147; }
                                }
                                goto gm_ll17;
lb147:;
                            }
                        }
                        {
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_srcf8l485c5uq, 9) == 0)) goto mn178;
                            {
                                gm_b16_8 t180 = gm_l14;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 56)) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 56)) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 56)) + 20, 0);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 56)))[16] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 56)) + 0, t180);
                            }
                            goto me177;
mn178:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sdbqk29a3n8jm, 12) == 0)) goto mn181;
                            {
                                gm_b16_8 t183 = gm_l14;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 80)) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 80)) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 80)) + 20, 0);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 80)))[16] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 80)) + 0, t183);
                            }
                            goto me177;
mn181:;
                            {
                                gm_b16_8 t186 = gm_l14;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)) + 20, 0);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)))[16] = 1;
                                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 128)) + 0, t186);
                            }
                            goto me177;
mn184:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 446, 17);
me177:;
                        }
                        goto me71;
mn135:;
                        {
                            gm_b16_8 t189 = gm_l1;
                            gm_tptmds653bni0 t190;
                            gm_f_build_list_f41mb8u((void *)&t190, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t189);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (((((uint8_t *)&t190))[0] != 0)) {
                                gm_tacj1cskem694 t191;
                                memset(&t191, 0, sizeof t191);
                                ((uint8_t *)&t191)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t191 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t190) + 8)));
                                F.ret = t191;
                                goto gm_fret;
                            }
                            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&t190) + 8));
                            gm_l18 = t192;
                        }
                        {
                            {
                                gm_b16_8 t194 = gm_l18;
                                gm_l19 = t194;
                            }
                            {
                                gm_l20 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t196 = gm_l20;
                                    uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    uint8_t t198 = (t196 < t197);
                                    if (!t198) goto lb195;
                                    {
                                        uint64_t t201 = gm_l20;
                                        uint64_t t202 = (uintptr_t)t201;
                                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                        gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t202 * 16));
                                        gm_l21 = t206;
                                        {
                                            uint8_t t208 = 0;
                                            uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                            uint8_t t210 = ((t209 == ((uint64_t)0ULL)));
                                            if (!t210) {
                                                gm_b16_8 t211 = gm_l21;
                                                gm_b16_8 t212;
                                                GM_ST(void *, (uint8_t *)&t212, (void *)(gm_skolmnfc0ccbd));
                                                GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(1));
                                                uint8_t t213 = gm_f_starts_with_6vvt01e(t211, t212);
                                                t208 = t213;
                                            } else t208 = 1;
                                            gm_l22 = t208;
                                        }
                                        {
                                            {
                                                gm_b16_8 t215 = gm_l21;
                                                gm_l23 = t215;
                                            }
                                            {
                                                gm_l24 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t217 = gm_l24;
                                                    uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                    uint8_t t219 = (t217 < t218);
                                                    if (!t219) goto lb216;
                                                    {
                                                        uint64_t t222 = gm_l24;
                                                        uint64_t t223 = (uintptr_t)t222;
                                                        uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                        uint8_t t225 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t223 * 1));
                                                        uint8_t t228 = GM_LD(uint8_t, ((uint8_t *)&t225));
                                                        gm_l25 = t228;
                                                        {
                                                            uint8_t t231 = 0;
                                                            uint8_t t232 = gm_l25;
                                                            uint8_t t233 = gm_f_is_space_uen38t5(t232);
                                                            if (!t233) {
                                                                uint8_t t234 = gm_l25;
                                                                uint8_t t235 = ((t234 == ((uint8_t)47ULL)));
                                                                t231 = t235;
                                                            } else t231 = 1;
                                                            if (!t231) goto ie230;
                                                            {
                                                                gm_l22 = ((uint8_t)1);
                                                            }
                                                            goto ix230;
ie230:;
ix230:;
                                                        }
                                                        goto me226;
mn227:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 456, 21);
me226:;
                                                    }
lc216:;
                                                    {
                                                        uint64_t t238 = gm_l24;
                                                        uint64_t r239; bool o240 = __builtin_add_overflow(t238, ((uint64_t)1ULL), &r239);
                                                        uint64_t t241 = r239;
                                                        gm_l24 = t241;
                                                    }
                                                }
lb216:;
                                            }
                                        }
                                        {
                                            uint8_t t243 = gm_l22;
                                            if (!t243) goto ie242;
                                            {
                                                uint64_t t245 = gm_l6;
                                                gm_t6j6smj7qoctu t246;
                                                gm_b16_8 t247 = gm_l21;
                                                GM_ST(gm_b16_8, (uint8_t *)&t246 + 0, t247);
                                                gm_b16_8 t248 = gm_l1;
                                                GM_ST(gm_b16_8, (uint8_t *)&t246 + 16, t248);
                                                gm_tabb710d9mq2e t249;
                                                gm_f_format_jv6n7oh((void *)&t249, (void *)NULL, cx, NULL, &t246);
                                                if (gm_unw.kind) {
                                                    goto gm_ll199;
                                                }
                                                if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 8, gm_srcg0ns322676oc, 460, 45);
                                                gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)&t249) + 8));
                                                gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                                                gm_tf1bjg81u0glv t252;
                                                gm_f_mfail_uqq7o7g((void *)&t252, t245, t251);
                                                memset(&(*(gm_tacj1cskem694 *)gm_out), 0, sizeof (*(gm_tacj1cskem694 *)gm_out));
                                                ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 1;
                                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t252);
                                                goto gm_fdone;
                                            }
                                            goto ix242;
ie242:;
ix242:;
                                        }
                                        goto me204;
mn205:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 454, 17);
me204:;
                                    }
lc195:;
                                    {
                                        uint64_t t254 = gm_l20;
                                        uint64_t r255; bool o256 = __builtin_add_overflow(t254, ((uint64_t)1ULL), &r255);
                                        uint64_t t257 = r255;
                                        gm_l20 = t257;
                                    }
                                }
gm_ll199:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb195; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc195; }
                                }
                                goto gm_ll17;
lb195:;
                            }
                        }
                        {
                            gm_b16_8 t258 = gm_l18;
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 104)) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 104)) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 104)) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 104)))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l2) + 104)) + 0, t258);
                        }
                        goto me71;
mn187:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 424, 9);
me71:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 414, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t260 = gm_l4;
                    uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                    uint64_t t263 = r261;
                    gm_l4 = t263;
                }
            }
gm_ll17:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
    }
    {
        const uint8_t *t264 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_tacj1cskem694 *)gm_out) + 8, t264, 152);
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
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_parse_dependency_8qmcjrm F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l2; /* line */
    void * gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l7; /* known */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* k */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* k */
    gm_b16_8 gm_l17; /* url0 */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* k */
    gm_b16_8 gm_l23; /* url */
    gm_b16_8 gm_l24; /* p */
    gm_th2lqbo77pvsc gm_l25; /* sha */
    gm_b16_8 gm_l26; /* h0 */
    gm_b16_8 gm_l27; /* h1 */
    gm_b16_8 gm_l28; /* h */
    uint8_t gm_l29; /* ok */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint8_t gm_l32; /* c */
    gm_th2lqbo77pvsc gm_l33; /* subdir */
    gm_th2lqbo77pvsc gm_l34; /* path */
    gm_th2lqbo77pvsc gm_l35; /* git_url */
    gm_b16_8 gm_l36; /* p */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_b16_8 gm_l41; /* k */
    gm_b16_8 gm_l42; /* url */
    gm_b16_8 gm_l43; /* p */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l49; /* k */
    gm_b16_8 gm_l51; /* v */
    gm_b16_8 gm_l52; /* e */
    gm_b16_8 gm_l54; /* v */
    gm_b16_8 gm_l55; /* v */
    gm_b16_8 gm_l56; /* v */
    uint8_t gm_l57; /* ok */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint8_t gm_l60; /* c */
    gm_th2lqbo77pvsc gm_l61; /* subdir */
    GM_STACK_CHECK(&F, 8256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)((uint8_t *)a1)));
        void * t3;
        if ((GM_LD(void *, ((uint8_t *)&t2)) != 0)) {
            void * t4 = GM_LD(void *, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            uint64_t t6 = gm_l2;
            gm_b16_8 t7 = gm_l0;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_tabb710d9mq2e t9;
            gm_f_format_9d8u2dd((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcg0ns322676oc, 485, 29);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            gm_tf1bjg81u0glv t12;
            gm_f_mfail_uqq7o7g((void *)&t12, t6, t11);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t12);
            goto gm_fdone;
        }
        gm_l3 = t3;
    }
    {
        {
            void * t14 = gm_l3;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 0) + 0));
            gm_l4 = t15;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t17 = gm_l5;
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t19 = (t17 < t18);
                if (!t19) goto lb16;
                {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = (uintptr_t)t22;
                    uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_trc3jdv2g5qre t27 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t23 * 72));
                    F.l6 = t27;
                    {
                        gm_l7 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t30 = ((uint8_t *)gm_ct5id4947nt);
                            if ((uintptr_t)0 > (uintptr_t)9 || (uintptr_t)9 > (uintptr_t)9) gm_trap_slice((uintptr_t)0, (uintptr_t)9, 9, gm_srcg0ns322676oc, 489, 9);
                            gm_b16_8 t31;
                            GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t30 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)9 - (uintptr_t)0));
                            gm_l8 = t31;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t33 = gm_l9;
                                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t35 = (t33 < t34);
                                if (!t35) goto lb32;
                                {
                                    uint64_t t38 = gm_l9;
                                    uint64_t t39 = (uintptr_t)t38;
                                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t39 * 16));
                                    gm_l10 = t43;
                                    {
                                        gm_b16_8 t46 = gm_l10;
                                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                                        uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
                                        if (!t48) goto ie45;
                                        {
                                            gm_l7 = ((uint8_t)1);
                                        }
                                        goto ix45;
ie45:;
ix45:;
                                    }
                                    goto me41;
mn42:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 489, 9);
me41:;
                                }
lc32:;
                                {
                                    uint64_t t51 = gm_l9;
                                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                                    uint64_t t54 = r52;
                                    gm_l9 = t54;
                                }
                            }
lb32:;
                        }
                    }
                    {
                        uint8_t t56 = gm_l7;
                        uint8_t t57 = (uint8_t)!t56;
                        if (!t57) goto ie55;
                        {
                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 64));
                            gm_t6j6smj7qoctu t60;
                            gm_b16_8 t61 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
                            gm_tabb710d9mq2e t63;
                            gm_f_format_je4jsfp((void *)&t63, (void *)NULL, cx, NULL, &t60);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcg0ns322676oc, 492, 46);
                            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                            gm_tf1bjg81u0glv t66;
                            gm_f_mfail_uqq7o7g((void *)&t66, t59, t65);
                            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t66);
                            goto gm_fdone;
                        }
                        goto ix55;
ie55:;
ix55:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 487, 5);
me25:;
                }
lc16:;
                {
                    uint64_t t68 = gm_l5;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l5 = t71;
                }
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb16; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc16; }
            }
            goto gm_lad;
lb16:;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_smimi08sfrg0n));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 0, t73);
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_s0k6ioi39f878));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 16, t74);
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sa2h9vonan0ma));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 32, t75);
        }
        {
            {
                {
                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 0, t78);
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 16, t79);
                    gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 32, t80);
                }
                void * t81 = ((uint8_t *)&F.l13);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcg0ns322676oc, 495, 5);
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t81 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t83 = t82;
                gm_l14 = t83;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t85 = gm_l15;
                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t87 = (t85 < t86);
                    if (!t87) goto lb84;
                    {
                        uint64_t t90 = gm_l15;
                        uint64_t t91 = (uintptr_t)t90;
                        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t91 * 16));
                        gm_l16 = t95;
                        {
                            void * t98 = gm_l3;
                            gm_b16_8 t99 = gm_l16;
                            void * t100 = gm_f_get_9rme0gb(t98, t99);
                            uint8_t t101;
                            if ((GM_LD(void *, ((uint8_t *)&t100)) != 0)) goto mn103;
                            t101 = ((uint8_t)0);
                            goto me102;
mn103:;
                            t101 = ((uint8_t)1);
                            goto me102;
mn104:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 496, 12);
me102:;
                            if (!t101) goto ie97;
                            {
                                void * t106 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t107 = gm_l16;
                                gm_tftb1odc739t5 t108 = gm_f_push_3ef746b(t106, t107);
                                if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcg0ns322676oc, 496, 35);
                                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                            }
                            goto ix97;
ie97:;
ix97:;
                        }
                        goto me93;
mn94:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 495, 5);
me93:;
                    }
lc84:;
                    {
                        uint64_t t111 = gm_l15;
                        uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                        uint64_t t114 = r112;
                        gm_l15 = t114;
                    }
                }
lb84:;
            }
        }
    }
    {
        uint64_t t116 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
        uint8_t t117 = (t116 > ((uint64_t)1ULL));
        if (!t117) goto ie115;
        {
            uint64_t t119 = gm_l2;
            gm_tfeab279c16rr t120;
            gm_b16_8 t121 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
            uint64_t t122 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t123 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t122 >= (uintptr_t)(t123)) gm_trap_index(t122, t123, gm_srcg0ns322676oc, 499, 105);
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t122 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t120 + 16, t124);
            uint64_t t125 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t126 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t125 >= (uintptr_t)(t126)) gm_trap_index(t125, t126, gm_srcg0ns322676oc, 499, 123);
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t125 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t120 + 32, t127);
            gm_tabb710d9mq2e t128;
            gm_f_format_aome4un((void *)&t128, (void *)NULL, cx, NULL, &t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srcg0ns322676oc, 499, 29);
            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
            gm_tf1bjg81u0glv t131;
            gm_f_mfail_uqq7o7g((void *)&t131, t119, t130);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t131);
            goto gm_fdone;
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        void * t133 = gm_l3;
        gm_b16_8 t134;
        GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(3));
        gm_b16_8 t135 = gm_l0;
        gm_tccklsmdasd9s t136;
        gm_f_dep_string_mmgrsgi((void *)&t136, (void *)NULL, cx, NULL, t133, t134, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t136))[0] != 0)) {
            gm_tmn9qm2bp7f46 t137;
            memset(&t137, 0, sizeof t137);
            ((uint8_t *)&t137)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t137 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t136) + 8)));
            F.ret = t137;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t136) + 8))[16] != 0)) goto ie132;
        gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
        gm_l17 = t138;
        {
            {
                gm_b16_8 t141;
                GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 0, t141);
                gm_b16_8 t142;
                GM_ST(void *, (uint8_t *)&t142, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 16, t142);
                gm_b16_8 t143;
                GM_ST(void *, (uint8_t *)&t143, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 32, t143);
                gm_b16_8 t144;
                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 48, t144);
            }
            {
                {
                    {
                        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 0, t147);
                        gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 16, t148);
                        gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 32));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 32, t149);
                        gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 48));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 48, t150);
                    }
                    void * t151 = ((uint8_t *)&F.l19);
                    if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 502, 9);
                    gm_b16_8 t152;
                    GM_ST(void *, (uint8_t *)&t152, (void *)(((uint8_t *)t151 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                    gm_b16_8 t153 = t152;
                    gm_l20 = t153;
                }
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t155 = gm_l21;
                        uint64_t t156 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint8_t t157 = (t155 < t156);
                        if (!t157) goto lb154;
                        {
                            uint64_t t160 = gm_l21;
                            uint64_t t161 = (uintptr_t)t160;
                            uint64_t t162 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t161 * 16));
                            gm_l22 = t165;
                            {
                                void * t168 = gm_l3;
                                gm_b16_8 t169 = gm_l22;
                                void * t170 = gm_f_get_9rme0gb(t168, t169);
                                uint8_t t171;
                                if ((GM_LD(void *, ((uint8_t *)&t170)) != 0)) goto mn173;
                                t171 = ((uint8_t)0);
                                goto me172;
mn173:;
                                t171 = ((uint8_t)1);
                                goto me172;
mn174:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 503, 16);
me172:;
                                if (!t171) goto ie167;
                                {
                                    uint64_t t176 = gm_l2;
                                    gm_t6j6smj7qoctu t177;
                                    gm_b16_8 t178 = gm_l22;
                                    GM_ST(gm_b16_8, (uint8_t *)&t177 + 0, t178);
                                    gm_b16_8 t179 = gm_l0;
                                    GM_ST(gm_b16_8, (uint8_t *)&t177 + 16, t179);
                                    gm_tabb710d9mq2e t180;
                                    gm_f_format_64tgs30((void *)&t180, (void *)NULL, cx, NULL, &t177);
                                    if (gm_unw.kind) {
                                        goto gm_ll158;
                                    }
                                    if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 8, gm_srcg0ns322676oc, 503, 59);
                                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&t180) + 8));
                                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                                    gm_tf1bjg81u0glv t183;
                                    gm_f_mfail_uqq7o7g((void *)&t183, t176, t182);
                                    memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                                    ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t183);
                                    goto gm_fdone;
                                }
                                goto ix167;
ie167:;
ix167:;
                            }
                            goto me163;
mn164:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 502, 9);
me163:;
                        }
lc154:;
                        {
                            uint64_t t185 = gm_l21;
                            uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                            uint64_t t188 = r186;
                            gm_l21 = t188;
                        }
                    }
gm_ll158:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb154; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc154; }
                    }
                    goto gm_lad;
lb154:;
                }
            }
        }
        {
            gm_b16_8 t190 = gm_l17;
            gm_b16_8 t191 = gm_f_trim_ascii_c9j36d1(t190);
            uint64_t t192 = GM_LD(uintptr_t, ((uint8_t *)&t191) + 8);
            uint8_t t193 = ((t192 == ((uint64_t)0ULL)));
            if (!t193) goto ie189;
            {
                uint64_t t195 = gm_l2;
                gm_b16_8 t196 = gm_l0;
                gm_tnpifv20irnph t197;
                GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t196);
                gm_tabb710d9mq2e t198;
                gm_f_format_97etovl((void *)&t198, (void *)NULL, cx, NULL, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srcg0ns322676oc, 505, 60);
                gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                gm_tf1bjg81u0glv t201;
                gm_f_mfail_uqq7o7g((void *)&t201, t195, t200);
                memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t201);
                goto gm_fdone;
            }
            goto ix189;
ie189:;
ix189:;
        }
        {
            gm_b16_8 t202 = gm_l17;
            gm_b16_8 t203 = gm_f_trim_ascii_c9j36d1(t202);
            gm_l23 = t203;
        }
        {
            gm_b16_8 t205 = gm_l23;
            gm_th2lqbo77pvsc t206;
            gm_f_url_problem_38kdh3a((void *)&t206, (void *)NULL, cx, NULL, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t206))[16] != 0)) goto ie204;
            gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&t206));
            gm_l24 = t207;
            {
                uint64_t t209 = gm_l2;
                gm_t6j6smj7qoctu t210;
                gm_b16_8 t211 = gm_l0;
                GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
                gm_b16_8 t212 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t210 + 16, t212);
                gm_tabb710d9mq2e t213;
                gm_f_format_qjfu8g6((void *)&t213, (void *)NULL, cx, NULL, &t210);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 8, gm_srcg0ns322676oc, 508, 33);
                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&t213) + 8));
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                gm_tf1bjg81u0glv t216;
                gm_f_mfail_uqq7o7g((void *)&t216, t209, t215);
                memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t216);
                goto gm_fdone;
            }
            goto ix204;
ie204:;
ix204:;
        }
        {
            gm_th2lqbo77pvsc t217;
            memset(&t217, 0, sizeof t217);
            gm_l25 = t217;
        }
        {
            void * t219 = gm_l3;
            gm_b16_8 t220;
            GM_ST(void *, (uint8_t *)&t220, (void *)(gm_sd1smnnii4cig));
            GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(6));
            gm_b16_8 t221 = gm_l0;
            gm_tccklsmdasd9s t222;
            gm_f_dep_string_mmgrsgi((void *)&t222, (void *)NULL, cx, NULL, t219, t220, t221);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t222))[0] != 0)) {
                gm_tmn9qm2bp7f46 t223;
                memset(&t223, 0, sizeof t223);
                ((uint8_t *)&t223)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t223 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t222) + 8)));
                F.ret = t223;
                goto gm_fret;
            }
            if (!(((((uint8_t *)&t222) + 8))[16] != 0)) goto ie218;
            gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&t222) + 8));
            gm_l26 = t224;
            {
                gm_b16_8 t226 = gm_l26;
                gm_b16_8 t227 = gm_f_trim_ascii_c9j36d1(t226);
                gm_l27 = t227;
            }
            {
                gm_b16_8 t228 = gm_l27;
                gm_b16_8 t229;
                GM_ST(void *, (uint8_t *)&t229, (void *)(gm_son0j0k0t10ro));
                GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(7));
                gm_th2lqbo77pvsc t230;
                gm_f_strip_prefix_b4949s8((void *)&t230, t228, t229);
                gm_b16_8 t231;
                if (((((uint8_t *)&t230))[16] != 0)) {
                    gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 0));
                    t231 = t232;
                } else {
                    gm_b16_8 t233 = gm_l27;
                    t231 = t233;
                }
                gm_l27 = t231;
            }
            {
                gm_b16_8 t234 = gm_l27;
                gm_b16_8 t235 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t234);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t235;
            }
            {
                uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t237 = ((t236 == ((uint64_t)64ULL)));
                gm_l29 = t237;
            }
            {
                {
                    gm_b16_8 t239 = gm_l28;
                    gm_l30 = t239;
                }
                {
                    gm_l31 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t241 = gm_l31;
                        uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint8_t t243 = (t241 < t242);
                        if (!t243) goto lb240;
                        {
                            uint64_t t246 = gm_l31;
                            uint64_t t247 = (uintptr_t)t246;
                            uint64_t t248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                            uint8_t t249 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t247 * 1));
                            uint8_t t252 = GM_LD(uint8_t, ((uint8_t *)&t249));
                            gm_l32 = t252;
                            {
                                uint8_t t255 = gm_l32;
                                uint8_t t256 = gm_f_is_hex_digit_q8s6utq(t255);
                                uint8_t t257 = (uint8_t)!t256;
                                if (!t257) goto ie254;
                                {
                                    gm_l29 = ((uint8_t)0);
                                }
                                goto ix254;
ie254:;
ix254:;
                            }
                            goto me250;
mn251:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 516, 13);
me250:;
                        }
lc240:;
                        {
                            uint64_t t260 = gm_l31;
                            uint64_t r261; bool o262 = __builtin_add_overflow(t260, ((uint64_t)1ULL), &r261);
                            uint64_t t263 = r261;
                            gm_l31 = t263;
                        }
                    }
lb240:;
                }
            }
            {
                uint8_t t265 = gm_l29;
                uint8_t t266 = (uint8_t)!t265;
                if (!t266) goto ie264;
                {
                    void * t268 = gm_l3;
                    gm_b16_8 t269;
                    GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sd1smnnii4cig));
                    GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(6));
                    void * t270 = gm_f_get_9rme0gb(t268, t269);
                    if (!(GM_LD(void *, ((uint8_t *)&t270)) != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 520, 31);
                    void * t271 = GM_LD(void *, ((uint8_t *)&t270));
                    uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)t271) + 64));
                    gm_b16_8 t273 = gm_l0;
                    gm_tnpifv20irnph t274;
                    GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t273);
                    gm_tabb710d9mq2e t275;
                    gm_f_format_nll5o74((void *)&t275, (void *)NULL, cx, NULL, t274);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srcg0ns322676oc, 520, 54);
                    gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
                    gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                    gm_tf1bjg81u0glv t278;
                    gm_f_mfail_uqq7o7g((void *)&t278, t272, t277);
                    memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                    ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t278);
                    goto gm_fdone;
                }
                goto ix264;
ie264:;
ix264:;
            }
            {
                gm_b16_8 t279 = gm_l28;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l25)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l25)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l25)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l25)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l25)) + 0, t279);
            }
            goto ix218;
ie218:;
ix218:;
        }
        {
            void * t280 = gm_l3;
            gm_b16_8 t281;
            GM_ST(void *, (uint8_t *)&t281, (void *)(gm_sfvspsnv23gbt));
            GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(6));
            gm_b16_8 t282 = gm_l0;
            gm_tccklsmdasd9s t283;
            gm_f_dep_string_mmgrsgi((void *)&t283, (void *)NULL, cx, NULL, t280, t281, t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t283))[0] != 0)) {
                gm_tmn9qm2bp7f46 t284;
                memset(&t284, 0, sizeof t284);
                ((uint8_t *)&t284)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t284 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t283) + 8)));
                F.ret = t284;
                goto gm_fret;
            }
            gm_l33 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t283) + 8));
        }
        {
            gm_b16_8 t285 = gm_l0;
            uint64_t t286 = gm_l2;
            gm_t6h2b31csotun t287;
            gm_f_check_subdir_2uljb06((void *)&t287, (void *)NULL, cx, NULL, t285, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l33)), t286);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t287))[0] != 0)) {
                gm_tmn9qm2bp7f46 t288;
                memset(&t288, 0, sizeof t288);
                ((uint8_t *)&t288)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t288 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t287) + 8)));
                F.ret = t288;
                goto gm_fret;
            }
            gm_b0_1 t289 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 8));
        }
        {
            gm_tt008mgut8glb t290;
            gm_b16_8 t291 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t290 + 0, t291);
            memcpy((uint8_t *)&t290 + 16, ((uint8_t *)&gm_l25), 24);
            memcpy((uint8_t *)&t290 + 40, ((uint8_t *)&gm_l33), 24);
            gm_tnlfbo6r4vlhg t292;
            GM_ST(uint8_t, (uint8_t *)&t292 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t292 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t292 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t292 + 72, 0);
            GM_ST(uint64_t, (uint8_t *)&t292 + 80, 0);
            GM_ST(uint64_t, (uint8_t *)&t292 + 88, 0);
            GM_ST(uint8_t, ((uint8_t *)&t292 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tt008mgut8glb, ((uint8_t *)&t292 + 8), t290);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 0;
            GM_ST(gm_tnlfbo6r4vlhg, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t292);
            goto gm_fdone;
        }
        goto ix132;
ie132:;
ix132:;
    }
    {
        void * t294 = gm_l3;
        gm_b16_8 t295;
        GM_ST(void *, (uint8_t *)&t295, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(6));
        void * t296 = gm_f_get_9rme0gb(t294, t295);
        uint8_t t297;
        if ((GM_LD(void *, ((uint8_t *)&t296)) != 0)) goto mn299;
        t297 = ((uint8_t)0);
        goto me298;
mn299:;
        t297 = ((uint8_t)1);
        goto me298;
mn300:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 528, 8);
me298:;
        if (!t297) goto ie293;
        {
            uint64_t t302 = gm_l2;
            gm_b16_8 t303 = gm_l0;
            gm_tnpifv20irnph t304;
            GM_ST(gm_b16_8, ((uint8_t *)&t304 + 0), t303);
            gm_tabb710d9mq2e t305;
            gm_f_format_p8qi5m3((void *)&t305, (void *)NULL, cx, NULL, t304);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t305))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t305) + 8, gm_srcg0ns322676oc, 528, 58);
            gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
            gm_b16_8 t307 = GM_LD(gm_b16_8, ((uint8_t *)&t306));
            gm_tf1bjg81u0glv t308;
            gm_f_mfail_uqq7o7g((void *)&t308, t302, t307);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t308);
            goto gm_fdone;
        }
        goto ix293;
ie293:;
ix293:;
    }
    {
        void * t309 = gm_l3;
        gm_b16_8 t310;
        GM_ST(void *, (uint8_t *)&t310, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(4));
        gm_b16_8 t311 = gm_l0;
        gm_tccklsmdasd9s t312;
        gm_f_dep_string_mmgrsgi((void *)&t312, (void *)NULL, cx, NULL, t309, t310, t311);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t312))[0] != 0)) {
            gm_tmn9qm2bp7f46 t313;
            memset(&t313, 0, sizeof t313);
            ((uint8_t *)&t313)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t313 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t312) + 8)));
            F.ret = t313;
            goto gm_fret;
        }
        gm_l34 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t312) + 8));
    }
    {
        void * t314 = gm_l3;
        gm_b16_8 t315;
        GM_ST(void *, (uint8_t *)&t315, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(3));
        gm_b16_8 t316 = gm_l0;
        gm_tccklsmdasd9s t317;
        gm_f_dep_string_mmgrsgi((void *)&t317, (void *)NULL, cx, NULL, t314, t315, t316);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t317))[0] != 0)) {
            gm_tmn9qm2bp7f46 t318;
            memset(&t318, 0, sizeof t318);
            ((uint8_t *)&t318)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t318 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t317) + 8)));
            F.ret = t318;
            goto gm_fret;
        }
        gm_l35 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t317) + 8));
    }
    {
        uint8_t t320;
        if (((((uint8_t *)&gm_l34))[16] != 0)) goto mn322;
        t320 = ((uint8_t)1);
        goto me321;
mn322:;
        t320 = ((uint8_t)0);
        goto me321;
mn323:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 8);
me321:;
        if (!t320) goto ie319;
        uint8_t t324;
        if (((((uint8_t *)&gm_l35))[16] != 0)) goto mn326;
        t324 = ((uint8_t)1);
        goto me325;
mn326:;
        t324 = ((uint8_t)0);
        goto me325;
mn327:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 25);
me325:;
        if (!t324) goto ie319;
        {
            uint64_t t329 = gm_l2;
            gm_b16_8 t330 = gm_l0;
            gm_tnpifv20irnph t331;
            GM_ST(gm_b16_8, ((uint8_t *)&t331 + 0), t330);
            gm_tabb710d9mq2e t332;
            gm_f_format_5bo63p7((void *)&t332, (void *)NULL, cx, NULL, t331);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t332))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t332) + 8, gm_srcg0ns322676oc, 532, 29);
            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&t332) + 8));
            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
            gm_tf1bjg81u0glv t335;
            gm_f_mfail_uqq7o7g((void *)&t335, t329, t334);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t335);
            goto gm_fdone;
        }
        goto ix319;
ie319:;
ix319:;
    }
    {
        if (!((((uint8_t *)&gm_l34))[16] != 0)) goto ie336;
        gm_b16_8 t337 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
        gm_l36 = t337;
        {
            {
                gm_b16_8 t340;
                GM_ST(void *, (uint8_t *)&t340, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 0, t340);
                gm_b16_8 t341;
                GM_ST(void *, (uint8_t *)&t341, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 16, t341);
                gm_b16_8 t342;
                GM_ST(void *, (uint8_t *)&t342, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t342 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 32, t342);
                gm_b16_8 t343;
                GM_ST(void *, (uint8_t *)&t343, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 48, t343);
            }
            {
                {
                    {
                        gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 0, t346);
                        gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 16, t347);
                        gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 32));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 32, t348);
                        gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 48));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 48, t349);
                    }
                    void * t350 = ((uint8_t *)&F.l38);
                    if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 535, 9);
                    gm_b16_8 t351;
                    GM_ST(void *, (uint8_t *)&t351, (void *)(((uint8_t *)t350 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                    gm_b16_8 t352 = t351;
                    gm_l39 = t352;
                }
                {
                    gm_l40 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t354 = gm_l40;
                        uint64_t t355 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        uint8_t t356 = (t354 < t355);
                        if (!t356) goto lb353;
                        {
                            uint64_t t359 = gm_l40;
                            uint64_t t360 = (uintptr_t)t359;
                            uint64_t t361 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                            gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t360 * 16));
                            gm_l41 = t364;
                            {
                                void * t367 = gm_l3;
                                gm_b16_8 t368 = gm_l41;
                                void * t369 = gm_f_get_9rme0gb(t367, t368);
                                uint8_t t370;
                                if ((GM_LD(void *, ((uint8_t *)&t369)) != 0)) goto mn372;
                                t370 = ((uint8_t)0);
                                goto me371;
mn372:;
                                t370 = ((uint8_t)1);
                                goto me371;
mn373:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 536, 16);
me371:;
                                if (!t370) goto ie366;
                                {
                                    uint64_t t375 = gm_l2;
                                    gm_t6j6smj7qoctu t376;
                                    gm_b16_8 t377 = gm_l41;
                                    GM_ST(gm_b16_8, (uint8_t *)&t376 + 0, t377);
                                    gm_b16_8 t378 = gm_l0;
                                    GM_ST(gm_b16_8, (uint8_t *)&t376 + 16, t378);
                                    gm_tabb710d9mq2e t379;
                                    gm_f_format_64tgs30((void *)&t379, (void *)NULL, cx, NULL, &t376);
                                    if (gm_unw.kind) {
                                        goto gm_ll357;
                                    }
                                    if (((((uint8_t *)&t379))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t379) + 8, gm_srcg0ns322676oc, 536, 59);
                                    gm_b16_8 t380 = GM_LD(gm_b16_8, (((uint8_t *)&t379) + 8));
                                    gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                                    gm_tf1bjg81u0glv t382;
                                    gm_f_mfail_uqq7o7g((void *)&t382, t375, t381);
                                    memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                                    ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t382);
                                    goto gm_fdone;
                                }
                                goto ix366;
ie366:;
ix366:;
                            }
                            goto me362;
mn363:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 535, 9);
me362:;
                        }
lc353:;
                        {
                            uint64_t t384 = gm_l40;
                            uint64_t r385; bool o386 = __builtin_add_overflow(t384, ((uint64_t)1ULL), &r385);
                            uint64_t t387 = r385;
                            gm_l40 = t387;
                        }
                    }
gm_ll357:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb353; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc353; }
                    }
                    goto gm_lad;
lb353:;
                }
            }
        }
        {
            {
                void * t389 = gm_l3;
                gm_b16_8 t390;
                GM_ST(void *, (uint8_t *)&t390, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t390 + 8, (uintptr_t)(7));
                gm_b16_8 t391 = gm_l0;
                gm_tccklsmdasd9s t392;
                gm_f_dep_string_mmgrsgi((void *)&t392, (void *)NULL, cx, NULL, t389, t390, t391);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t392))[0] != 0)) {
                    gm_tmn9qm2bp7f46 t393;
                    memset(&t393, 0, sizeof t393);
                    ((uint8_t *)&t393)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t393 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t392) + 8)));
                    F.ret = t393;
                    goto gm_fret;
                }
            }
        }
        {
            uint64_t t395 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
            uint8_t t396 = ((t395 == ((uint64_t)0ULL)));
            if (!t396) goto ie394;
            {
                uint64_t t398 = gm_l2;
                gm_b16_8 t399 = gm_l0;
                gm_tnpifv20irnph t400;
                GM_ST(gm_b16_8, ((uint8_t *)&t400 + 0), t399);
                gm_tabb710d9mq2e t401;
                gm_f_format_dd3t0vb((void *)&t401, (void *)NULL, cx, NULL, t400);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t401))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t401) + 8, gm_srcg0ns322676oc, 540, 45);
                gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)&t401) + 8));
                gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
                gm_tf1bjg81u0glv t404;
                gm_f_mfail_uqq7o7g((void *)&t404, t398, t403);
                memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t404);
                goto gm_fdone;
            }
            goto ix394;
ie394:;
ix394:;
        }
        {
            gm_b16_8 t405 = gm_l36;
            gm_tnlfbo6r4vlhg t406;
            memset(&t406, 0, sizeof t406);
            GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t406 + 8), t405);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 0;
            GM_ST(gm_tnlfbo6r4vlhg, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t406);
            goto gm_fdone;
        }
        goto ix336;
ie336:;
ix336:;
    }
    {
        gm_th2lqbo77pvsc t407 = gm_l35;
        if (!((((uint8_t *)&t407))[16] != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 543, 12);
        gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&t407));
        gm_l42 = t408;
    }
    {
        gm_b16_8 t410 = gm_l42;
        gm_b16_8 t411 = gm_f_normalize_url_faghdja(t410);
        uint64_t t412 = GM_LD(uintptr_t, ((uint8_t *)&t411) + 8);
        uint8_t t413 = ((t412 == ((uint64_t)0ULL)));
        if (!t413) goto ie409;
        {
            uint64_t t415 = gm_l2;
            gm_b16_8 t416 = gm_l0;
            gm_tnpifv20irnph t417;
            GM_ST(gm_b16_8, ((uint8_t *)&t417 + 0), t416);
            gm_tabb710d9mq2e t418;
            gm_f_format_fn68qf0((void *)&t418, (void *)NULL, cx, NULL, t417);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t418))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t418) + 8, gm_srcg0ns322676oc, 544, 58);
            gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&t418) + 8));
            gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
            gm_tf1bjg81u0glv t421;
            gm_f_mfail_uqq7o7g((void *)&t421, t415, t420);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t421);
            goto gm_fdone;
        }
        goto ix409;
ie409:;
ix409:;
    }
    {
        gm_b16_8 t423 = gm_l42;
        gm_b16_8 t424 = gm_f_trim_ascii_c9j36d1(t423);
        gm_th2lqbo77pvsc t425;
        gm_f_url_problem_38kdh3a((void *)&t425, (void *)NULL, cx, NULL, t424);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t425))[16] != 0)) goto ie422;
        gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)&t425));
        gm_l43 = t426;
        {
            uint64_t t428 = gm_l2;
            gm_t6j6smj7qoctu t429;
            gm_b16_8 t430 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t429 + 0, t430);
            gm_b16_8 t431 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t429 + 16, t431);
            gm_tabb710d9mq2e t432;
            gm_f_format_7sbr40s((void *)&t432, (void *)NULL, cx, NULL, &t429);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 8, gm_srcg0ns322676oc, 546, 29);
            gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)&t432) + 8));
            gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
            gm_tf1bjg81u0glv t435;
            gm_f_mfail_uqq7o7g((void *)&t435, t428, t434);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t435);
            goto gm_fdone;
        }
        goto ix422;
ie422:;
ix422:;
    }
    {
        gm_f_list_epvducg((void *)&F.l44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t437;
            GM_ST(void *, (uint8_t *)&t437, (void *)(gm_sla3alt5340mf));
            GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 0, t437);
            gm_b16_8 t438;
            GM_ST(void *, (uint8_t *)&t438, (void *)(gm_s420tm9ua7ggg));
            GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 16, t438);
            gm_b16_8 t439;
            GM_ST(void *, (uint8_t *)&t439, (void *)(gm_s0jho24omhfui));
            GM_ST(uintptr_t, (uint8_t *)&t439 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 32, t439);
            gm_b16_8 t440;
            GM_ST(void *, (uint8_t *)&t440, (void *)(gm_sf9ilivr4c86m));
            GM_ST(uintptr_t, (uint8_t *)&t440 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 48, t440);
        }
        {
            {
                {
                    gm_b16_8 t443 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 0, t443);
                    gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 16, t444);
                    gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 32, t445);
                    gm_b16_8 t446 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 48, t446);
                }
                void * t447 = ((uint8_t *)&F.l46);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 549, 5);
                gm_b16_8 t448;
                GM_ST(void *, (uint8_t *)&t448, (void *)(((uint8_t *)t447 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t449 = t448;
                gm_l47 = t449;
            }
            {
                gm_l48 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t451 = gm_l48;
                    uint64_t t452 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    uint8_t t453 = (t451 < t452);
                    if (!t453) goto lb450;
                    {
                        uint64_t t456 = gm_l48;
                        uint64_t t457 = (uintptr_t)t456;
                        uint64_t t458 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                        gm_b16_8 t461 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t457 * 16));
                        gm_l49 = t461;
                        {
                            void * t464 = gm_l3;
                            gm_b16_8 t465 = gm_l49;
                            void * t466 = gm_f_get_9rme0gb(t464, t465);
                            uint8_t t467;
                            if ((GM_LD(void *, ((uint8_t *)&t466)) != 0)) goto mn469;
                            t467 = ((uint8_t)0);
                            goto me468;
mn469:;
                            t467 = ((uint8_t)1);
                            goto me468;
mn470:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 550, 12);
me468:;
                            if (!t467) goto ie463;
                            {
                                void * t472 = (void *)((uint8_t *)&F.l44);
                                gm_b16_8 t473 = gm_l49;
                                gm_tftb1odc739t5 t474 = gm_f_push_3ef746b(t472, t473);
                                if (((((uint8_t *)&t474))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t474) + 1, gm_srcg0ns322676oc, 550, 35);
                                gm_b0_1 t475 = GM_LD(gm_b0_1, (((uint8_t *)&t474) + 1));
                            }
                            goto ix463;
ie463:;
ix463:;
                        }
                        goto me459;
mn460:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 549, 5);
me459:;
                    }
lc450:;
                    {
                        uint64_t t477 = gm_l48;
                        uint64_t r478; bool o479 = __builtin_add_overflow(t477, ((uint64_t)1ULL), &r478);
                        uint64_t t480 = r478;
                        gm_l48 = t480;
                    }
                }
lb450:;
            }
        }
    }
    {
        uint64_t t482 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
        uint8_t t483 = (t482 > ((uint64_t)1ULL));
        if (!t483) goto ie481;
        {
            uint64_t t485 = gm_l2;
            gm_tfeab279c16rr t486;
            gm_b16_8 t487 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t486 + 0, t487);
            uint64_t t488 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t489 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
            if (t488 >= (uintptr_t)(t489)) gm_trap_index(t488, t489, gm_srcg0ns322676oc, 556, 13);
            gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l44) + 0)) + t488 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t486 + 16, t490);
            uint64_t t491 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t492 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
            if (t491 >= (uintptr_t)(t492)) gm_trap_index(t491, t492, gm_srcg0ns322676oc, 557, 13);
            gm_b16_8 t493 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l44) + 0)) + t491 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t486 + 32, t493);
            gm_tabb710d9mq2e t494;
            gm_f_format_4af676s((void *)&t494, (void *)NULL, cx, NULL, &t486);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t494))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t494) + 8, gm_srcg0ns322676oc, 553, 29);
            gm_b16_8 t495 = GM_LD(gm_b16_8, (((uint8_t *)&t494) + 8));
            gm_b16_8 t496 = GM_LD(gm_b16_8, ((uint8_t *)&t495));
            gm_tf1bjg81u0glv t497;
            gm_f_mfail_uqq7o7g((void *)&t497, t485, t496);
            memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
            ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t497);
            goto gm_fdone;
        }
        goto ix481;
ie481:;
ix481:;
    }
    {
        memset(&F.l50, 0, sizeof F.l50);
        GM_ST(uint8_t, (uint8_t *)&F.l50 + 0, ((uint8_t)0ULL));
    }
    {
        void * t499 = gm_l3;
        gm_b16_8 t500;
        GM_ST(void *, (uint8_t *)&t500, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t500 + 8, (uintptr_t)(7));
        gm_b16_8 t501 = gm_l0;
        gm_tccklsmdasd9s t502;
        gm_f_dep_string_mmgrsgi((void *)&t502, (void *)NULL, cx, NULL, t499, t500, t501);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t502))[0] != 0)) {
            gm_tmn9qm2bp7f46 t503;
            memset(&t503, 0, sizeof t503);
            ((uint8_t *)&t503)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t503 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t502) + 8)));
            F.ret = t503;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t502) + 8))[16] != 0)) goto ie498;
        gm_b16_8 t504 = GM_LD(gm_b16_8, (((uint8_t *)&t502) + 8));
        gm_l51 = t504;
        {
            gm_b16_8 t506 = gm_l51;
            gm_b16_8 t507 = gm_f_trim_ascii_c9j36d1(t506);
            gm_t62efos6ssciq t508;
            gm_f_parse_version_req_alipvfi((void *)&t508, (void *)NULL, cx, NULL, t507);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tin97tt4ud0s1 t509;
            if (!((((uint8_t *)&t508))[0] != 0)) {
                t509 = (*(gm_tin97tt4ud0s1 *)(((uint8_t *)&t508) + 8));
            } else {
                gm_b16_8 t510 = GM_LD(gm_b16_8, (((uint8_t *)&t508) + 8));
                gm_l52 = t510;
                void * t512 = gm_l3;
                gm_b16_8 t513;
                GM_ST(void *, (uint8_t *)&t513, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t513 + 8, (uintptr_t)(7));
                void * t514 = gm_f_get_9rme0gb(t512, t513);
                if (!(GM_LD(void *, ((uint8_t *)&t514)) != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 563, 27);
                void * t515 = GM_LD(void *, ((uint8_t *)&t514));
                uint64_t t516 = GM_LD(uint64_t, (((uint8_t *)t515) + 64));
                gm_t6j6smj7qoctu t517;
                gm_b16_8 t518 = gm_l51;
                GM_ST(gm_b16_8, (uint8_t *)&t517 + 0, t518);
                gm_b16_8 t519 = gm_l52;
                GM_ST(gm_b16_8, (uint8_t *)&t517 + 16, t519);
                gm_tabb710d9mq2e t520;
                gm_f_format_pah1p62((void *)&t520, (void *)NULL, cx, NULL, &t517);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t520))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t520) + 8, gm_srcg0ns322676oc, 563, 51);
                gm_b16_8 t521 = GM_LD(gm_b16_8, (((uint8_t *)&t520) + 8));
                gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)&t521));
                gm_tf1bjg81u0glv t523;
                gm_f_mfail_uqq7o7g((void *)&t523, t516, t522);
                memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t523);
                goto gm_fdone;
            }
            F.l53 = t509;
        }
        {
            const uint8_t *t524 = ((uint8_t *)&F.l53);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 0, ((uint8_t)1ULL));
            memmove((uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 8, t524, 40);
        }
        goto ix498;
ie498:;
        void * t526 = gm_l3;
        gm_b16_8 t527;
        GM_ST(void *, (uint8_t *)&t527, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t527 + 8, (uintptr_t)(3));
        gm_b16_8 t528 = gm_l0;
        gm_tccklsmdasd9s t529;
        gm_f_dep_string_mmgrsgi((void *)&t529, (void *)NULL, cx, NULL, t526, t527, t528);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t529))[0] != 0)) {
            gm_tmn9qm2bp7f46 t530;
            memset(&t530, 0, sizeof t530);
            ((uint8_t *)&t530)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t530 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t529) + 8)));
            F.ret = t530;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t529) + 8))[16] != 0)) goto ie525;
        gm_b16_8 t531 = GM_LD(gm_b16_8, (((uint8_t *)&t529) + 8));
        gm_l54 = t531;
        {
            gm_b16_8 t533 = gm_l54;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 40, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 0, ((uint8_t)2ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 8, t533);
        }
        goto ix525;
ie525:;
        void * t535 = gm_l3;
        gm_b16_8 t536;
        GM_ST(void *, (uint8_t *)&t536, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t536 + 8, (uintptr_t)(6));
        gm_b16_8 t537 = gm_l0;
        gm_tccklsmdasd9s t538;
        gm_f_dep_string_mmgrsgi((void *)&t538, (void *)NULL, cx, NULL, t535, t536, t537);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t538))[0] != 0)) {
            gm_tmn9qm2bp7f46 t539;
            memset(&t539, 0, sizeof t539);
            ((uint8_t *)&t539)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t539 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t538) + 8)));
            F.ret = t539;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t538) + 8))[16] != 0)) goto ie534;
        gm_b16_8 t540 = GM_LD(gm_b16_8, (((uint8_t *)&t538) + 8));
        gm_l55 = t540;
        {
            gm_b16_8 t542 = gm_l55;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 40, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 0, ((uint8_t)3ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 8, t542);
        }
        goto ix534;
ie534:;
        void * t544 = gm_l3;
        gm_b16_8 t545;
        GM_ST(void *, (uint8_t *)&t545, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t545 + 8, (uintptr_t)(3));
        gm_b16_8 t546 = gm_l0;
        gm_tccklsmdasd9s t547;
        gm_f_dep_string_mmgrsgi((void *)&t547, (void *)NULL, cx, NULL, t544, t545, t546);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t547))[0] != 0)) {
            gm_tmn9qm2bp7f46 t548;
            memset(&t548, 0, sizeof t548);
            ((uint8_t *)&t548)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t548 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t547) + 8)));
            F.ret = t548;
            goto gm_fret;
        }
        if (!(((((uint8_t *)&t547) + 8))[16] != 0)) goto ie543;
        gm_b16_8 t549 = GM_LD(gm_b16_8, (((uint8_t *)&t547) + 8));
        gm_l56 = t549;
        {
            uint64_t t551 = GM_LD(uintptr_t, ((uint8_t *)&gm_l56) + 8);
            uint8_t t552 = (t551 > ((uint64_t)0ULL));
            gm_l57 = t552;
        }
        {
            {
                gm_b16_8 t554 = gm_l56;
                gm_l58 = t554;
            }
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t556 = gm_l59;
                    uint64_t t557 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                    uint8_t t558 = (t556 < t557);
                    if (!t558) goto lb555;
                    {
                        uint64_t t561 = gm_l59;
                        uint64_t t562 = (uintptr_t)t561;
                        uint64_t t563 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                        uint8_t t564 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t562 * 1));
                        uint8_t t567 = GM_LD(uint8_t, ((uint8_t *)&t564));
                        gm_l60 = t567;
                        {
                            uint8_t t570 = gm_l60;
                            uint8_t t571 = gm_f_is_hex_digit_q8s6utq(t570);
                            uint8_t t572 = (uint8_t)!t571;
                            if (!t572) goto ie569;
                            {
                                gm_l57 = ((uint8_t)0);
                            }
                            goto ix569;
ie569:;
ix569:;
                        }
                        goto me565;
mn566:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 572, 9);
me565:;
                    }
lc555:;
                    {
                        uint64_t t575 = gm_l59;
                        uint64_t r576; bool o577 = __builtin_add_overflow(t575, ((uint64_t)1ULL), &r576);
                        uint64_t t578 = r576;
                        gm_l59 = t578;
                    }
                }
lb555:;
            }
        }
        {
            uint8_t t580 = gm_l57;
            uint8_t t581 = (uint8_t)!t580;
            if (!t581) goto ie579;
            {
                uint64_t t583 = gm_l2;
                gm_b16_8 t584 = gm_l0;
                gm_tnpifv20irnph t585;
                GM_ST(gm_b16_8, ((uint8_t *)&t585 + 0), t584);
                gm_tabb710d9mq2e t586;
                gm_f_format_ll295o3((void *)&t586, (void *)NULL, cx, NULL, t585);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t586))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t586) + 8, gm_srcg0ns322676oc, 575, 41);
                gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)&t586) + 8));
                gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&t587));
                gm_tf1bjg81u0glv t589;
                gm_f_mfail_uqq7o7g((void *)&t589, t583, t588);
                memset(&(*(gm_tmn9qm2bp7f46 *)gm_out), 0, sizeof (*(gm_tmn9qm2bp7f46 *)gm_out));
                ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t589);
                goto gm_fdone;
            }
            goto ix579;
ie579:;
ix579:;
        }
        {
            gm_b16_8 t590 = gm_l56;
            gm_b16_8 t591 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t590);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 40, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 0, ((uint8_t)4ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_toq2f2q4opoa3 *)((uint8_t *)&F.l50)) + 8, t591);
        }
        goto ix543;
ie543:;
ix543:;
ix534:;
ix525:;
ix498:;
    }
    {
        void * t592 = gm_l3;
        gm_b16_8 t593;
        GM_ST(void *, (uint8_t *)&t593, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t593 + 8, (uintptr_t)(6));
        gm_b16_8 t594 = gm_l0;
        gm_tccklsmdasd9s t595;
        gm_f_dep_string_mmgrsgi((void *)&t595, (void *)NULL, cx, NULL, t592, t593, t594);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t595))[0] != 0)) {
            gm_tmn9qm2bp7f46 t596;
            memset(&t596, 0, sizeof t596);
            ((uint8_t *)&t596)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t596 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t595) + 8)));
            F.ret = t596;
            goto gm_fret;
        }
        gm_l61 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t595) + 8));
    }
    {
        gm_b16_8 t597 = gm_l0;
        uint64_t t598 = gm_l2;
        gm_t6h2b31csotun t599;
        gm_f_check_subdir_2uljb06((void *)&t599, (void *)NULL, cx, NULL, t597, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l61)), t598);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t599))[0] != 0)) {
            gm_tmn9qm2bp7f46 t600;
            memset(&t600, 0, sizeof t600);
            ((uint8_t *)&t600)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t600 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t599) + 8)));
            F.ret = t600;
            goto gm_fret;
        }
        gm_b0_1 t601 = GM_LD(gm_b0_1, (((uint8_t *)&t599) + 8));
    }
    {
        gm_te5jk64pjl28k t602;
        gm_b16_8 t603 = gm_l42;
        GM_ST(gm_b16_8, (uint8_t *)&t602 + 0, t603);
        memcpy((uint8_t *)&t602 + 16, ((uint8_t *)&F.l50), 48);
        memcpy((uint8_t *)&t602 + 64, ((uint8_t *)&gm_l61), 24);
        gm_tnlfbo6r4vlhg t604;
        GM_ST(uint8_t, (uint8_t *)&t604 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t604 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t604 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t604 + 0), ((uint8_t)1ULL));
        GM_ST(gm_te5jk64pjl28k, ((uint8_t *)&t604 + 8), t602);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out))[0] = 0;
        GM_ST(gm_tnlfbo6r4vlhg, (uint8_t *)&(*(gm_tmn9qm2bp7f46 *)gm_out) + 8, t604);
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
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_manifest_b61s3ln F;
    gm_b16_8 gm_l0; /* text */
    gm_tob1snuii5bme gm_l1; /* e */
    void * gm_l2; /* doc */
    void * gm_l4; /* pkg */
    void * gm_l5; /* t */
    void * gm_l6; /* item */
    gm_b16_8 gm_l7; /* n */
    gm_b16_8 gm_l8; /* problem */
    void * gm_l9; /* item */
    gm_b16_8 gm_l10; /* v */
    gm_b16_8 gm_l11; /* e */
    void * gm_l12; /* deps */
    void * gm_l13; /* t */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* problem */
    void * gm_l19; /* b */
    void * gm_l20; /* t */
    void * gm_l21; /* l */
    void * gm_l22; /* t */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l26; /* level */
    void * gm_l27; /* ps */
    void * gm_l28; /* t */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint8_t gm_l32; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint8_t gm_l35; /* c */
    uint8_t gm_l36; /* <tmp> */
    uint8_t gm_l37; /* <tmp> */
    uint8_t gm_l38; /* <tmp> */
    void * gm_l39; /* pt */
    GM_STACK_CHECK(&F, 5542u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_trssmtuv8jr0v t3;
        gm_f_parse_toml_gjtmf79((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t4;
        if (!((((uint8_t *)&t3))[0] != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 8));
            t4 = t5;
        } else {
            gm_l1 = (*(gm_tob1snuii5bme *)(((uint8_t *)&t3) + 8));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
            gm_tf1bjg81u0glv t9;
            gm_f_mfail_uqq7o7g((void *)&t9, t7, t8);
            memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
            ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        gm_l2 = t4;
    }
    {
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l3 + 0, t10);
        gm_th2lqbo77pvsc t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l3 + 24, t11);
        gm_f_list_7393u00((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 48)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti0g3bmheuq9o t12 = GM_LD(gm_ti0g3bmheuq9o, gm_cb60tlkkq8o);
        GM_ST(gm_ti0g3bmheuq9o, (uint8_t *)&F.l3 + 88, t12);
        gm_f_list_hs66gbq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 240)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7mv9vi7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 280)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l2;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_snqvo5bgc2fch));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
        void * t16 = gm_f_get_9rme0gb(t14, t15);
        if (!(GM_LD(void *, ((uint8_t *)&t16)) != 0)) goto ie13;
        void * t17 = GM_LD(void *, ((uint8_t *)&t16));
        gm_l4 = t17;
        {
            void * t19 = gm_l4;
            void * t20 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t19) + 16)));
            void * t21;
            if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) {
                void * t22 = GM_LD(void *, (((uint8_t *)&t20) + 0));
                t21 = t22;
            } else {
                void * t23 = gm_l4;
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 64));
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sq3r7vbcbs4ll));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(25));
                gm_tf1bjg81u0glv t26;
                gm_f_mfail_uqq7o7g((void *)&t26, t24, t25);
                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t26);
                goto gm_fdone;
            }
            gm_l5 = t21;
        }
        {
            void * t28 = gm_l5;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sius9pki5u4e3));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
            void * t30 = gm_f_get_9rme0gb(t28, t29);
            if (!(GM_LD(void *, ((uint8_t *)&t30)) != 0)) goto ie27;
            void * t31 = GM_LD(void *, ((uint8_t *)&t30));
            gm_l6 = t31;
            {
                void * t33 = gm_l6;
                gm_th2lqbo77pvsc t34;
                gm_f_as_str_982o16v((void *)&t34, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t33) + 16)));
                gm_b16_8 t35;
                if (((((uint8_t *)&t34))[16] != 0)) {
                    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 0));
                    t35 = t36;
                } else {
                    void * t37 = gm_l6;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 64));
                    gm_b16_8 t39;
                    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sob5t135sl7q9));
                    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(33));
                    gm_tf1bjg81u0glv t40;
                    gm_f_mfail_uqq7o7g((void *)&t40, t38, t39);
                    memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                    ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t40);
                    goto gm_fdone;
                }
                gm_l7 = t35;
            }
            {
                gm_b16_8 t42 = gm_l7;
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_snqvo5bgc2fch));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(7));
                gm_th2lqbo77pvsc t44;
                gm_f_name_problem_6r4mp11((void *)&t44, (void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t44))[16] != 0)) goto ie41;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_l8 = t45;
                {
                    void * t47 = gm_l6;
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 64));
                    gm_b16_8 t49 = gm_l8;
                    gm_tf1bjg81u0glv t50;
                    gm_f_mfail_uqq7o7g((void *)&t50, t48, t49);
                    memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                    ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t50);
                    goto gm_fdone;
                }
                goto ix41;
ie41:;
ix41:;
            }
            {
                gm_b16_8 t51 = gm_l7;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 0)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 0)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 0)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 0)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 0)) + 0, t51);
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            void * t53 = gm_l5;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sla3alt5340mf));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(7));
            void * t55 = gm_f_get_9rme0gb(t53, t54);
            if (!(GM_LD(void *, ((uint8_t *)&t55)) != 0)) goto ie52;
            void * t56 = GM_LD(void *, ((uint8_t *)&t55));
            gm_l9 = t56;
            {
                void * t58 = gm_l9;
                gm_th2lqbo77pvsc t59;
                gm_f_as_str_982o16v((void *)&t59, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t58) + 16)));
                gm_b16_8 t60;
                if (((((uint8_t *)&t59))[16] != 0)) {
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 0));
                    t60 = t61;
                } else {
                    void * t62 = gm_l9;
                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 64));
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_su0gvqh5vvvlj));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(36));
                    gm_tf1bjg81u0glv t65;
                    gm_f_mfail_uqq7o7g((void *)&t65, t63, t64);
                    memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                    ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t65);
                    goto gm_fdone;
                }
                gm_l10 = t60;
            }
            {
                {
                    gm_b16_8 t67 = gm_l10;
                    gm_tvtl3n8qta358 t68;
                    gm_f_parse_version_9a6ns50((void *)&t68, (void *)NULL, cx, NULL, t67);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tu1089hhijadf t69;
                    if (!((((uint8_t *)&t68))[0] != 0)) {
                        t69 = (*(gm_tu1089hhijadf *)(((uint8_t *)&t68) + 8));
                    } else {
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                        gm_l11 = t70;
                        void * t72 = gm_l9;
                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t72) + 64));
                        gm_t6j6smj7qoctu t74;
                        gm_b16_8 t75 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t74 + 0, t75);
                        gm_b16_8 t76 = gm_l11;
                        GM_ST(gm_b16_8, (uint8_t *)&t74 + 16, t76);
                        gm_tabb710d9mq2e t77;
                        gm_f_format_7duae7m((void *)&t77, (void *)NULL, cx, NULL, &t74);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcg0ns322676oc, 290, 42);
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                        gm_tf1bjg81u0glv t80;
                        gm_f_mfail_uqq7o7g((void *)&t80, t73, t79);
                        memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                        ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                        GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t80);
                        goto gm_fdone;
                    }
                }
            }
            {
                gm_b16_8 t81 = gm_l10;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 24)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 24)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 24)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 24)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l3) + 24)) + 0, t81);
            }
            goto ix52;
ie52:;
ix52:;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t83 = gm_l2;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(12));
        void * t85 = gm_f_get_9rme0gb(t83, t84);
        if (!(GM_LD(void *, ((uint8_t *)&t85)) != 0)) goto ie82;
        void * t86 = GM_LD(void *, ((uint8_t *)&t85));
        gm_l12 = t86;
        {
            void * t88 = gm_l12;
            void * t89 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t88) + 16)));
            void * t90;
            if ((GM_LD(void *, ((uint8_t *)&t89)) != 0)) {
                void * t91 = GM_LD(void *, (((uint8_t *)&t89) + 0));
                t90 = t91;
            } else {
                void * t92 = gm_l12;
                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 64));
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sfag3nu74qn7r));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(30));
                gm_tf1bjg81u0glv t95;
                gm_f_mfail_uqq7o7g((void *)&t95, t93, t94);
                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t95);
                goto gm_fdone;
            }
            gm_l13 = t90;
        }
        {
            {
                void * t97 = gm_l13;
                gm_b16_8 t98 = GM_LD(gm_b16_8, ((((uint8_t *)t97) + 0) + 0));
                gm_l14 = t98;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l15;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l15;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_trc3jdv2g5qre t110 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t106 * 72));
                        F.l16 = t110;
                        {
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            gm_b16_8 t114;
                            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s8f9rv6hrr024));
                            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(10));
                            gm_th2lqbo77pvsc t115;
                            gm_f_name_problem_6r4mp11((void *)&t115, (void *)NULL, cx, NULL, t113, t114);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            if (!((((uint8_t *)&t115))[16] != 0)) goto ie112;
                            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                            gm_l17 = t116;
                            {
                                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                                gm_b16_8 t119 = gm_l17;
                                gm_tf1bjg81u0glv t120;
                                gm_f_mfail_uqq7o7g((void *)&t120, t118, t119);
                                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t120);
                                goto gm_fdone;
                            }
                            goto ix112;
ie112:;
ix112:;
                        }
                        {
                            gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                            gm_tmn9qm2bp7f46 t123;
                            gm_f_parse_dependency_8qmcjrm((void *)&t123, (void *)NULL, cx, NULL, t121, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l16) + 16)), t122);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            if (((((uint8_t *)&t123))[0] != 0)) {
                                gm_tjgmtdrk88t7m t124;
                                memset(&t124, 0, sizeof t124);
                                ((uint8_t *)&t124)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t124 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t123) + 8)));
                                F.ret = t124;
                                goto gm_fret;
                            }
                            F.l18 = (*(gm_tnlfbo6r4vlhg *)(((uint8_t *)&t123) + 8));
                        }
                        {
                            void * t125 = (void *)(((uint8_t *)&F.l3) + 48);
                            gm_t7tiv6vif86qg t126;
                            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t127);
                            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t126 + 16, t128);
                            memcpy((uint8_t *)&t126 + 24, ((uint8_t *)&F.l18), 96);
                            gm_tftb1odc739t5 t129 = gm_f_push_pdknc9g(t125, &t126);
                            if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srcg0ns322676oc, 300, 13);
                            gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
                        }
                        goto me108;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 297, 9);
me108:;
                    }
lc99:;
                    {
                        uint64_t t132 = gm_l15;
                        uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                        uint64_t t135 = r133;
                        gm_l15 = t135;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        goto ix82;
ie82:;
ix82:;
    }
    {
        void * t137 = gm_l2;
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_slllafvm0pbeb));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(5));
        void * t139 = gm_f_get_9rme0gb(t137, t138);
        if (!(GM_LD(void *, ((uint8_t *)&t139)) != 0)) goto ie136;
        void * t140 = GM_LD(void *, ((uint8_t *)&t139));
        gm_l19 = t140;
        {
            void * t142 = gm_l19;
            void * t143 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t142) + 16)));
            void * t144;
            if ((GM_LD(void *, ((uint8_t *)&t143)) != 0)) {
                void * t145 = GM_LD(void *, (((uint8_t *)&t143) + 0));
                t144 = t145;
            } else {
                void * t146 = gm_l19;
                uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)t146) + 64));
                gm_b16_8 t148;
                GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s5tee1epg2e5a));
                GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(23));
                gm_tf1bjg81u0glv t149;
                gm_f_mfail_uqq7o7g((void *)&t149, t147, t148);
                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t149);
                goto gm_fdone;
            }
            gm_l20 = t144;
        }
        {
            void * t150 = gm_l20;
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_s5ojatldfpfs2));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(7));
            gm_tacj1cskem694 t152;
            gm_f_parse_build_c2teosa((void *)&t152, (void *)NULL, cx, NULL, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t152))[0] != 0)) {
                gm_tjgmtdrk88t7m t153;
                memset(&t153, 0, sizeof t153);
                ((uint8_t *)&t153)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t153 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t152) + 8)));
                F.ret = t153;
                goto gm_fret;
            }
            GM_ST(gm_ti0g3bmheuq9o, (((uint8_t *)&F.l3) + 88), (*(gm_ti0g3bmheuq9o *)(((uint8_t *)&t152) + 8)));
        }
        goto ix136;
ie136:;
ix136:;
    }
    {
        void * t155 = gm_l2;
        gm_b16_8 t156;
        GM_ST(void *, (uint8_t *)&t156, (void *)(gm_sv1gci5lo7scg));
        GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(4));
        void * t157 = gm_f_get_9rme0gb(t155, t156);
        if (!(GM_LD(void *, ((uint8_t *)&t157)) != 0)) goto ie154;
        void * t158 = GM_LD(void *, ((uint8_t *)&t157));
        gm_l21 = t158;
        {
            void * t160 = gm_l21;
            void * t161 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t160) + 16)));
            void * t162;
            if ((GM_LD(void *, ((uint8_t *)&t161)) != 0)) {
                void * t163 = GM_LD(void *, (((uint8_t *)&t161) + 0));
                t162 = t163;
            } else {
                void * t164 = gm_l21;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 64));
                gm_b16_8 t166;
                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sna94ju8c8em3));
                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(47));
                gm_tf1bjg81u0glv t167;
                gm_f_mfail_uqq7o7g((void *)&t167, t165, t166);
                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t167);
                goto gm_fdone;
            }
            gm_l22 = t162;
        }
        {
            {
                void * t169 = gm_l22;
                gm_b16_8 t170 = GM_LD(gm_b16_8, ((((uint8_t *)t169) + 0) + 0));
                gm_l23 = t170;
            }
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t172 = gm_l24;
                    uint64_t t173 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    uint8_t t174 = (t172 < t173);
                    if (!t174) goto lb171;
                    {
                        uint64_t t177 = gm_l24;
                        uint64_t t178 = (uintptr_t)t177;
                        uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        gm_trc3jdv2g5qre t182 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t178 * 72));
                        F.l25 = t182;
                        {
                            uint8_t t185 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)&F.l25) + 0)), gm_s0nf1rg04f3m5, 3) == 0));
                            if (!t185) goto ie184;
                            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            gm_tf5srh4mp4b3i t187 = gm_f_lint_rule_o3ccae1(t186);
                            uint8_t t188;
                            if (((((uint8_t *)&t187))[8] != 0)) goto mn190;
                            t188 = ((uint8_t)1);
                            goto me189;
mn190:;
                            t188 = ((uint8_t)0);
                            goto me189;
mn191:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 310, 39);
me189:;
                            if (!t188) goto ie184;
                            {
                                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                gm_tnpifv20irnph t195;
                                GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
                                gm_tabb710d9mq2e t196;
                                gm_f_format_55igra0((void *)&t196, (void *)NULL, cx, NULL, t195);
                                if (gm_unw.kind) {
                                    goto gm_ll175;
                                }
                                if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 8, gm_srcg0ns322676oc, 311, 39);
                                gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&t196) + 8));
                                gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                                gm_tf1bjg81u0glv t199;
                                gm_f_mfail_uqq7o7g((void *)&t199, t193, t198);
                                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t199);
                                goto gm_fdone;
                            }
                            goto ix184;
ie184:;
ix184:;
                        }
                        {
                            gm_th2lqbo77pvsc t200;
                            gm_f_as_str_982o16v((void *)&t200, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l25) + 16)));
                            gm_b16_8 t201;
                            if (((((uint8_t *)&t200))[16] != 0)) {
                                gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&t200) + 0));
                                t201 = t202;
                            } else {
                                gm_b16_8 t203;
                                GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(0));
                                t201 = t203;
                            }
                            gm_l26 = t201;
                        }
                        {
                            gm_b16_8 t205 = gm_l26;
                            gm_tfeab279c16rr t206;
                            gm_b16_8 t207;
                            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_slb6cj7ndgvdm));
                            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(5));
                            GM_ST(gm_b16_8, (uint8_t *)&t206 + 0, t207);
                            gm_b16_8 t208;
                            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_soofuqhnovv5o));
                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(4));
                            GM_ST(gm_b16_8, (uint8_t *)&t206 + 16, t208);
                            gm_b16_8 t209;
                            GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s920j0georape));
                            GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(4));
                            GM_ST(gm_b16_8, (uint8_t *)&t206 + 32, t209);
                            uint8_t t210 = 0;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t206) + 0, ((uint8_t *)&t205))) t210 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t206) + 16, ((uint8_t *)&t205))) t210 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t206) + 32, ((uint8_t *)&t205))) t210 = 1;
                            uint8_t t211 = (uint8_t)(!t210);
                            if (!t211) goto ie204;
                            {
                                uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                gm_tnpifv20irnph t215;
                                GM_ST(gm_b16_8, ((uint8_t *)&t215 + 0), t214);
                                gm_tabb710d9mq2e t216;
                                gm_f_format_hvnghft((void *)&t216, (void *)NULL, cx, NULL, t215);
                                if (gm_unw.kind) {
                                    goto gm_ll175;
                                }
                                if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 8, gm_srcg0ns322676oc, 315, 39);
                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
                                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                                gm_tf1bjg81u0glv t219;
                                gm_f_mfail_uqq7o7g((void *)&t219, t213, t218);
                                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t219);
                                goto gm_fdone;
                            }
                            goto ix204;
ie204:;
ix204:;
                        }
                        {
                            void * t220 = (void *)(((uint8_t *)&F.l3) + 280);
                            gm_tftksr4op114e t221;
                            gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t221 + 0, t222);
                            gm_b16_8 t223 = gm_l26;
                            GM_ST(gm_b16_8, (uint8_t *)&t221 + 16, t223);
                            uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t221 + 32, t224);
                            gm_tftb1odc739t5 t225 = gm_f_push_dob2kfb(t220, &t221);
                            if (((((uint8_t *)&t225))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t225) + 1, gm_srcg0ns322676oc, 317, 13);
                            gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t225) + 1));
                        }
                        goto me180;
mn181:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 309, 9);
me180:;
                    }
lc171:;
                    {
                        uint64_t t228 = gm_l24;
                        uint64_t r229; bool o230 = __builtin_add_overflow(t228, ((uint64_t)1ULL), &r229);
                        uint64_t t231 = r229;
                        gm_l24 = t231;
                    }
                }
gm_ll175:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb171; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc171; }
                }
                goto gm_lad;
lb171:;
            }
        }
        goto ix154;
ie154:;
ix154:;
    }
    {
        void * t233 = gm_l2;
        gm_b16_8 t234;
        GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s6v08aos37nfe));
        GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(7));
        void * t235 = gm_f_get_9rme0gb(t233, t234);
        if (!(GM_LD(void *, ((uint8_t *)&t235)) != 0)) goto ie232;
        void * t236 = GM_LD(void *, ((uint8_t *)&t235));
        gm_l27 = t236;
        {
            void * t238 = gm_l27;
            void * t239 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t238) + 16)));
            void * t240;
            if ((GM_LD(void *, ((uint8_t *)&t239)) != 0)) {
                void * t241 = GM_LD(void *, (((uint8_t *)&t239) + 0));
                t240 = t241;
            } else {
                void * t242 = gm_l27;
                uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)t242) + 64));
                gm_b16_8 t244;
                GM_ST(void *, (uint8_t *)&t244, (void *)(gm_slc3n2ku5uff5));
                GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(54));
                gm_tf1bjg81u0glv t245;
                gm_f_mfail_uqq7o7g((void *)&t245, t243, t244);
                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t245);
                goto gm_fdone;
            }
            gm_l28 = t240;
        }
        {
            {
                void * t247 = gm_l28;
                gm_b16_8 t248 = GM_LD(gm_b16_8, ((((uint8_t *)t247) + 0) + 0));
                gm_l29 = t248;
            }
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t250 = gm_l30;
                    uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    uint8_t t252 = (t250 < t251);
                    if (!t252) goto lb249;
                    {
                        uint64_t t255 = gm_l30;
                        uint64_t t256 = (uintptr_t)t255;
                        uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        gm_trc3jdv2g5qre t260 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t256 * 72));
                        F.l31 = t260;
                        {
                            uint64_t t262 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                            uint8_t t263 = (t262 > ((uint64_t)0ULL));
                            gm_l32 = t263;
                        }
                        {
                            {
                                gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l33 = t265;
                            }
                            {
                                gm_l34 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t267 = gm_l34;
                                    uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    uint8_t t269 = (t267 < t268);
                                    if (!t269) goto lb266;
                                    {
                                        uint64_t t272 = gm_l34;
                                        uint64_t t273 = (uintptr_t)t272;
                                        uint64_t t274 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        uint8_t t275 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t273 * 1));
                                        uint8_t t278 = GM_LD(uint8_t, ((uint8_t *)&t275));
                                        gm_l35 = t278;
                                        {
                                            uint8_t t281 = 0;
                                            uint8_t t282 = 0;
                                            uint8_t t283 = 0;
                                            {
                                                uint8_t t285 = gm_l35;
                                                gm_l36 = t285;
                                            }
                                            uint8_t t286 = 0;
                                            uint8_t t287 = gm_l36;
                                            uint8_t t288 = (((uint8_t)97ULL) <= t287);
                                            if (t288) {
                                                uint8_t t289 = gm_l36;
                                                uint8_t t290 = (t289 <= ((uint8_t)122ULL));
                                                t286 = t290;
                                            } else t286 = 0;
                                            uint8_t t291 = t286;
                                            if (!t291) {
                                                {
                                                    uint8_t t293 = gm_l35;
                                                    gm_l37 = t293;
                                                }
                                                uint8_t t294 = 0;
                                                uint8_t t295 = gm_l37;
                                                uint8_t t296 = (((uint8_t)65ULL) <= t295);
                                                if (t296) {
                                                    uint8_t t297 = gm_l37;
                                                    uint8_t t298 = (t297 <= ((uint8_t)90ULL));
                                                    t294 = t298;
                                                } else t294 = 0;
                                                uint8_t t299 = t294;
                                                t283 = t299;
                                            } else t283 = 1;
                                            if (!t283) {
                                                {
                                                    uint8_t t301 = gm_l35;
                                                    gm_l38 = t301;
                                                }
                                                uint8_t t302 = 0;
                                                uint8_t t303 = gm_l38;
                                                uint8_t t304 = (((uint8_t)48ULL) <= t303);
                                                if (t304) {
                                                    uint8_t t305 = gm_l38;
                                                    uint8_t t306 = (t305 <= ((uint8_t)57ULL));
                                                    t302 = t306;
                                                } else t302 = 0;
                                                uint8_t t307 = t302;
                                                t282 = t307;
                                            } else t282 = 1;
                                            if (!t282) {
                                                uint8_t t308 = gm_l35;
                                                gm_tqs5p4n2tq19v t309;
                                                GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)95ULL));
                                                GM_ST(uint8_t, ((uint8_t *)&t309 + 1), ((uint8_t)45ULL));
                                                uint8_t t310 = 0;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t309) + 0, ((uint8_t *)&t308))) t310 = 1;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t309) + 1, ((uint8_t *)&t308))) t310 = 1;
                                                uint8_t t311 = (uint8_t)(t310);
                                                t281 = t311;
                                            } else t281 = 1;
                                            uint8_t t312 = (uint8_t)!t281;
                                            if (!t312) goto ie280;
                                            {
                                                gm_l32 = ((uint8_t)0);
                                            }
                                            goto ix280;
ie280:;
ix280:;
                                        }
                                        goto me276;
mn277:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 324, 13);
me276:;
                                    }
lc266:;
                                    {
                                        uint64_t t315 = gm_l34;
                                        uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                                        uint64_t t318 = r316;
                                        gm_l34 = t318;
                                    }
                                }
lb266:;
                            }
                        }
                        {
                            uint8_t t320 = gm_l32;
                            uint8_t t321 = (uint8_t)!t320;
                            if (!t321) goto ie319;
                            {
                                uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_tnpifv20irnph t325;
                                GM_ST(gm_b16_8, ((uint8_t *)&t325 + 0), t324);
                                gm_tabb710d9mq2e t326;
                                gm_f_format_850ujed((void *)&t326, (void *)NULL, cx, NULL, t325);
                                if (gm_unw.kind) {
                                    goto gm_ll253;
                                }
                                if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 8, gm_srcg0ns322676oc, 328, 39);
                                gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&t326) + 8));
                                gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
                                gm_tf1bjg81u0glv t329;
                                gm_f_mfail_uqq7o7g((void *)&t329, t323, t328);
                                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t329);
                                goto gm_fdone;
                            }
                            goto ix319;
ie319:;
ix319:;
                        }
                        {
                            void * t330 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l31) + 16)));
                            void * t331;
                            if ((GM_LD(void *, ((uint8_t *)&t330)) != 0)) {
                                void * t332 = GM_LD(void *, (((uint8_t *)&t330) + 0));
                                t331 = t332;
                            } else {
                                uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_tnpifv20irnph t335;
                                GM_ST(gm_b16_8, ((uint8_t *)&t335 + 0), t334);
                                gm_tabb710d9mq2e t336;
                                gm_f_format_jh4rfvo((void *)&t336, (void *)NULL, cx, NULL, t335);
                                if (gm_unw.kind) {
                                    goto gm_ll253;
                                }
                                if (((((uint8_t *)&t336))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t336) + 8, gm_srcg0ns322676oc, 330, 64);
                                gm_b16_8 t337 = GM_LD(gm_b16_8, (((uint8_t *)&t336) + 8));
                                gm_b16_8 t338 = GM_LD(gm_b16_8, ((uint8_t *)&t337));
                                gm_tf1bjg81u0glv t339;
                                gm_f_mfail_uqq7o7g((void *)&t339, t333, t338);
                                memset(&(*(gm_tjgmtdrk88t7m *)gm_out), 0, sizeof (*(gm_tjgmtdrk88t7m *)gm_out));
                                ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t339);
                                goto gm_fdone;
                            }
                            gm_l39 = t331;
                        }
                        {
                            void * t340 = gm_l39;
                            gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            gm_tnpifv20irnph t342;
                            GM_ST(gm_b16_8, ((uint8_t *)&t342 + 0), t341);
                            gm_tabb710d9mq2e t343;
                            gm_f_format_1ncs69t((void *)&t343, (void *)NULL, cx, NULL, t342);
                            if (gm_unw.kind) {
                                goto gm_ll253;
                            }
                            if (((((uint8_t *)&t343))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t343) + 8, gm_srcg0ns322676oc, 331, 45);
                            gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&t343) + 8));
                            gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                            gm_tacj1cskem694 t346;
                            gm_f_parse_build_c2teosa((void *)&t346, (void *)NULL, cx, NULL, t340, t345);
                            if (gm_unw.kind) {
                                goto gm_ll253;
                            }
                            if (((((uint8_t *)&t346))[0] != 0)) {
                                gm_tjgmtdrk88t7m t347;
                                memset(&t347, 0, sizeof t347);
                                ((uint8_t *)&t347)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t347 + 8), (*(gm_tf1bjg81u0glv *)(((uint8_t *)&t346) + 8)));
                                F.ret = t347;
                                goto gm_fret;
                            }
                            F.l40 = (*(gm_ti0g3bmheuq9o *)(((uint8_t *)&t346) + 8));
                        }
                        {
                            void * t348 = (void *)(((uint8_t *)&F.l3) + 240);
                            gm_t6b36gvdk984b t349;
                            gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t349 + 0, t350);
                            uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t349 + 16, t351);
                            memcpy((uint8_t *)&t349 + 24, ((uint8_t *)&F.l40), 152);
                            gm_tftb1odc739t5 t352 = gm_f_push_7hnmra3(t348, &t349);
                            if (((((uint8_t *)&t352))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t352) + 1, gm_srcg0ns322676oc, 332, 13);
                            gm_b0_1 t353 = GM_LD(gm_b0_1, (((uint8_t *)&t352) + 1));
                        }
                        goto me258;
mn259:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 322, 9);
me258:;
                    }
lc249:;
                    {
                        uint64_t t355 = gm_l30;
                        uint64_t r356; bool o357 = __builtin_add_overflow(t355, ((uint64_t)1ULL), &r356);
                        uint64_t t358 = r356;
                        gm_l30 = t358;
                    }
                }
gm_ll253:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb249; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc249; }
                }
                goto gm_lad;
lb249:;
            }
        }
        goto ix232;
ie232:;
ix232:;
    }
    {
        const uint8_t *t359 = ((uint8_t *)&F.l3);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_tjgmtdrk88t7m *)gm_out) + 8, t359, 320);
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
inline __attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_equal_foa1umn F;
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t2;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        uint8_t t13 = ((GM_LD(uint8_t, ((uint8_t *)a1) + 0) == ((uint8_t)0ULL)));
        t2 = t13;
        goto me3;
mn11:;
ma5:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_tin97tt4ud0s1 t16 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a0) + 8));
        F.l2 = t16;
        uint8_t t17;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        gm_tin97tt4ud0s1 t21 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a1) + 8));
        F.l3 = t21;
        void * t22 = (void *)((uint8_t *)&F.l2);
        void * t23 = (void *)((uint8_t *)&F.l3);
        uint8_t t24 = gm_f_req_equal_7ia8ip6(t22, t23);
        t17 = t24;
        goto me18;
mn19:;
        t17 = ((uint8_t)0);
        goto me18;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 44, 24);
me18:;
        t2 = t17;
        goto me3;
mn14:;
ma6:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l4 = t28;
        uint8_t t29;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t32 != ((uint8_t)2ULL)) goto mn31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l5 = t33;
        gm_b16_8 t34 = gm_l4;
        gm_b16_8 t35 = gm_l5;
        uint8_t t36 = (gm_eq0iqiubfm7e(((uint8_t *)&t34), ((uint8_t *)&t35)));
        t29 = t36;
        goto me30;
mn31:;
        t29 = ((uint8_t)0);
        goto me30;
mn37:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 48, 20);
me30:;
        t2 = t29;
        goto me3;
mn26:;
ma7:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t39 != ((uint8_t)3ULL)) goto mn38;
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l6 = t40;
        uint8_t t41;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t44 != ((uint8_t)3ULL)) goto mn43;
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l7 = t45;
        gm_b16_8 t46 = gm_l6;
        gm_b16_8 t47 = gm_l7;
        uint8_t t48 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t47)));
        t41 = t48;
        goto me42;
mn43:;
        t41 = ((uint8_t)0);
        goto me42;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 52, 23);
me42:;
        t2 = t41;
        goto me3;
mn38:;
ma8:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t51 != ((uint8_t)4ULL)) goto mn50;
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l8 = t52;
        uint8_t t53;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t56 != ((uint8_t)4ULL)) goto mn55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l9 = t57;
        gm_b16_8 t58 = gm_l8;
        gm_b16_8 t59 = gm_l9;
        uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
        t53 = t60;
        goto me54;
mn55:;
        t53 = ((uint8_t)0);
        goto me54;
mn61:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 56, 20);
me54:;
        t2 = t53;
        goto me3;
mn50:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 42, 12);
me3:;
        uint8_t t62 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1) { return gm_f_spec_equal_foa1umn(&a0, &a1); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_spec_text_9k2gjft F;
    gm_b16_8 gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 566u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma8; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s7j1ioljmsuka));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(18));
    t1 = t12;
    goto me2;
mn10:;
ma4:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto mn13;
    gm_tin97tt4ud0s1 t15 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a0) + 8));
    F.l1 = t15;
    void * t16 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t17 = gm_f_req_string_fk059jr((void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
    gm_tabb710d9mq2e t19;
    gm_f_format_rlu0tid((void *)&t19, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcg0ns322676oc, 34, 20);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
    t1 = t21;
    goto me2;
mn13:;
ma5:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t23 != ((uint8_t)2ULL)) goto mn22;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l2 = t24;
    gm_b16_8 t25 = gm_l2;
    gm_tnpifv20irnph t26;
    GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
    gm_tabb710d9mq2e t27;
    gm_f_format_79gt991((void *)&t27, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcg0ns322676oc, 35, 16);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
    t1 = t29;
    goto me2;
mn22:;
ma6:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t31 != ((uint8_t)3ULL)) goto mn30;
    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t32;
    gm_b16_8 t33 = gm_l3;
    gm_tnpifv20irnph t34;
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
    gm_tabb710d9mq2e t35;
    gm_f_format_4pbmdvl((void *)&t35, (void *)NULL, cx, NULL, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcg0ns322676oc, 36, 19);
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    t1 = t37;
    goto me2;
mn30:;
ma7:;
    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t39 != ((uint8_t)4ULL)) goto mn38;
    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l4 = t40;
    gm_b16_8 t41 = gm_l4;
    gm_tnpifv20irnph t42;
    GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
    gm_tabb710d9mq2e t43;
    gm_f_format_h0dnf9n((void *)&t43, (void *)NULL, cx, NULL, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcg0ns322676oc, 37, 16);
    gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
    t1 = t45;
    goto me2;
mn38:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 32, 58);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_string_array_bn13c0i F;
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* s */
    GM_STACK_CHECK(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t3 != ((uint8_t)3ULL)) goto ie2;
        gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a0) + 8));
        F.l1 = t4;
        {
            gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                gm_l3 = t7;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t9 = gm_l4;
                    uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    {
                        uint64_t t14 = gm_l4;
                        uint64_t t15 = (uintptr_t)t14;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_tktr4rmbindgv t19 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 48));
                        F.l5 = t19;
                        {
                            gm_th2lqbo77pvsc t21;
                            gm_f_as_str_982o16v((void *)&t21, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l5)));
                            gm_b16_8 t22;
                            if (((((uint8_t *)&t21))[16] != 0)) {
                                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 0));
                                t22 = t23;
                            } else {
                                gm_th2lqbo77pvsc t24;
                                memset(&t24, 0, sizeof t24);
                                uint8_t t25 = 0;
                                F.ret = t24;
                                goto gm_fret;
                            }
                            gm_l6 = t22;
                        }
                        {
                            void * t26 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t27 = gm_l6;
                            gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t26, t27);
                            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcg0ns322676oc, 407, 9);
                            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                        }
                        goto me17;
mn18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 405, 5);
me17:;
                    }
lc8:;
                    {
                        uint64_t t31 = gm_l4;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        uint64_t t34 = r32;
                        gm_l4 = t34;
                    }
                }
lb8:;
            }
        }
        {
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t36);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t38;
            memset(&t38, 0, sizeof t38);
            uint8_t t39 = 0;
            F.ret = t38;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_ascii_c9j36d1 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l1;
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            uint64_t t7 = gm_l1;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcg0ns322676oc, 251, 38);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            uint8_t t11 = gm_f_is_space_uen38t5(t10);
            if (!t11) goto lb3;
            {
                uint64_t t14 = gm_l1;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcg0ns322676oc, 251, 47);
                uint64_t t17 = r15;
                gm_l1 = t17;
            }
lc3:;
        }
lb3:;
    }
    {
        for (;;) {
            uint64_t t19 = gm_l2;
            uint64_t t20 = gm_l1;
            uint8_t t21 = (t19 > t20);
            if (!t21) goto lb18;
            uint64_t t22 = gm_l2;
            uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
            if (o24) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 40);
            uint64_t t25 = r23;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcg0ns322676oc, 252, 38);
            uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t26 * 1));
            uint8_t t29 = gm_f_is_space_uen38t5(t28);
            if (!t29) goto lb18;
            {
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 51);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
lc18:;
        }
lb18:;
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t37 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t38 = gm_l1;
        uint64_t t39 = (uintptr_t)t38;
        uint64_t t40 = gm_l2;
        uint64_t t41 = (uintptr_t)t40;
        if (t39 > t41 || t41 > (uintptr_t)t36) gm_trap_slice(t39, t41, t36, gm_srcg0ns322676oc, 253, 12);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t37 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41 - t39));
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_url_problem_38kdh3a F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK(&F, 401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        if (!t5) goto ie2;
        {
            gm_b16_8 t7 = gm_l0;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_tabb710d9mq2e t9;
            gm_f_format_ivhanlp((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcg0ns322676oc, 227, 38);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t11);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_l1 = t13;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l2;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l2;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t21 * 1));
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t23));
                    gm_l3 = t26;
                    {
                        uint8_t t29 = gm_l3;
                        uint8_t t30 = gm_f_is_space_or_control_3uo6i3o(t29);
                        if (!t30) goto ie28;
                        {
                            gm_t6j6smj7qoctu t32;
                            gm_b16_8 t33 = gm_l0;
                            gm_b16_8 t34 = gm_f_debug_escape_t8t1h0j((void *)NULL, cx, NULL, t33);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t32 + 0, t34);
                            uint8_t t35 = gm_l3;
                            gm_b16_8 t36 = gm_f_debug_char_p6i205h((void *)NULL, cx, NULL, t35);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t32 + 16, t36);
                            gm_tabb710d9mq2e t37;
                            gm_f_format_uqgrksh((void *)&t37, (void *)NULL, cx, NULL, &t32);
                            if (gm_unw.kind) {
                                goto gm_ll18;
                            }
                            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcg0ns322676oc, 230, 20);
                            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t39);
                            goto gm_fdone;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 228, 5);
me24:;
                }
lc14:;
                {
                    uint64_t t41 = gm_l2;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l2 = t44;
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
        gm_th2lqbo77pvsc t45;
        memset(&t45, 0, sizeof t45);
        uint8_t t46 = 0;
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
