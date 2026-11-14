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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_apply_builtin_41dffti {
    gm_frame h;
    gm_tckdobunaulee l32; /* d */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_array_ptr_to_slice_7dg3m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coerce_bytes_9kf3u24 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_convert_is6an2k {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_hash_bytes_2qnf0cp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_int_26mf5j0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_val_nr2e99r {
    gm_frame h;
    gm_t2jeg3bg7sfqa l17; /* s */
    gm_tpautr4br7h3l l31; /* def */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_text_vrjnuu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_htv6093 {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_is_nan_b0ma7mo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_mask_do3naah {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_round_2pna7m3 {
    gm_frame h;
    double ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_align_lavuopj {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
__attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
__attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0);
__attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_tvdg94bk7aa22 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src9pvgnpibl2nm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stk678hojgu6q[24];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = ((GM_LD(uint8_t, (((uint8_t *)t1) + 88) + 0) == ((uint8_t)3ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
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
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_int_26mf5j0 F;
    void * gm_l0; /* h */
    gm_t86h6hhm6d76n gm_l1; /* x */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* low */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        unsigned __int128 t2 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)a1)));
        gm_l3 = t2;
    }
    {
        void * t3 = gm_l0;
        unsigned __int128 t4 = gm_l3;
        uint64_t t5 = (uint64_t)t4;
        gm_f_hash_u64_32bripn(t3, t5);
    }
    {
        uint32_t t7 = gm_l2;
        uint8_t t8 = (t7 > ((uint32_t)64ULL));
        if (!t8) goto ie6;
        {
            void * t10 = gm_l0;
            unsigned __int128 t11 = gm_l3;
            unsigned __int128 n12 = (unsigned __int128)((uint64_t)64ULL);
            if (n12 >= 128) gm_trap_shift((uint64_t)n12, "u128", gm_srcl8pn2pjtmj6k, 427, 33);
            unsigned __int128 t13 = (unsigned __int128)(t11 >> (int)n12);
            uint64_t t14 = (uint64_t)t13;
            gm_f_hash_u64_32bripn(t10, t14);
        }
        goto ix6;
ie6:;
ix6:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_32bripn F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    unsigned __int128 gm_l2; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
        uint64_t t4 = gm_l1;
        uint64_t t5 = (uint64_t)(t3 ^ t4);
        unsigned __int128 t6 = (unsigned __int128)t5;
        unsigned __int128 r7; bool o8 = __builtin_mul_overflow(t6, ((unsigned __int128)(((unsigned __int128)0u << 64) | 11400714819323198485u)), &r7);
        unsigned __int128 t9 = r7;
        gm_l2 = t9;
    }
    {
        void * t10 = gm_l0;
        unsigned __int128 t11 = gm_l2;
        uint64_t t12 = (uint64_t)t11;
        unsigned __int128 t13 = gm_l2;
        unsigned __int128 n14 = (unsigned __int128)((uint64_t)64ULL);
        if (n14 >= 128) gm_trap_shift((uint64_t)n14, "u128", gm_srcl8pn2pjtmj6k, 407, 26);
        unsigned __int128 t15 = (unsigned __int128)(t13 >> (int)n14);
        uint64_t t16 = (uint64_t)t15;
        uint64_t t17 = (uint64_t)(t12 ^ t16);
        GM_ST(uint64_t, ((uint8_t *)t10), t17);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)t1) + 360) + 40));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 360) + 48));
    gm_t2lqu8jsj511d t5;
    GM_ST(uint64_t, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 8), t4);
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
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
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_is_nan_b0ma7mo F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t1 = gm_l0;
    double t2 = gm_l0;
    uint8_t t3 = (!(t1 == t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_mask_do3naah F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_src9pvgnpibl2nm, 8, 74);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_src9pvgnpibl2nm, 8, 73);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_round_2pna7m3 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* width */
    GM_STACK_CHECK_SHALLOW(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint16_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint16_t)32ULL)));
        if (!t4) goto ie2;
        {
            double t6 = gm_l0;
            float t7 = (float)((double)t6);
            double t8 = (double)t7;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint16_t t11 = gm_l1;
        uint8_t t12 = ((t11 == ((uint16_t)16ULL)));
        if (!t12) goto ie10;
        {
            double t14 = gm_l0;
            uint16_t t15 = gm_f_num_f64_to_f16_3g33m05(t14);
            double t16 = gm_f_num_f16_to_f64_h70suv4(t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        double t18 = gm_l0;
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l2 = t7;
        {
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_t88rcucg1b7uk t12 = gm_l1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
        F.ret = t5;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_8jbgndp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t1 = gm_l2;
    gm_b16_8 t2 = t1;
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
    if (t3 >= (uintptr_t)(t7)) gm_trap_index(t3, t7, gm_srcnqtpfma6qe4a, 174, 81);
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t6)) + t3 * 8));
    uint64_t t9 = gm_f_us_cuv01fp(t8);
    uint64_t t10 = (uintptr_t)t9;
    uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 174, 76);
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)) + t10 * 1));
    uint8_t t13 = (!(t12 == ((uint8_t)0ULL)));
    F.ret = t13;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_70vvk1e F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 144u);
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
    gm_f_format_46el73v((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcpit8mdn0781q, 437, 107);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t1, t2, t7);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_apply_builtin_41dffti F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* al */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    __int128 gm_l12; /* stride */
    __int128 gm_l13; /* p */
    uint64_t gm_l14; /* pbits */
    uint64_t gm_l15; /* mask */
    int64_t gm_l16; /* d */
    int64_t gm_l17; /* q */
    gm_ttovhfee8mf28 gm_l18; /* info */
    unsigned __int128 gm_l19; /* raw */
    __int128 gm_l20; /* n */
    __int128 gm_l21; /* off */
    __int128 gm_l22; /* r */
    gm_tcmkompbl09il gm_l23; /* u */
    uint64_t gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* def */
    gm_ttovhfee8mf28 gm_l27; /* info */
    gm_t86h6hhm6d76n gm_l28; /* x */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    unsigned __int128 gm_l33; /* tb */
    unsigned __int128 gm_l34; /* mag */
    gm_t2lqu8jsj511d gm_l35; /* mk */
    gm_b16_8 gm_l36; /* vb */
    double gm_l37; /* x */
    double gm_l38; /* y */
    double gm_l39; /* r */
    gm_ttovhfee8mf28 gm_l40; /* info */
    unsigned __int128 gm_l41; /* x */
    unsigned __int128 gm_l42; /* y */
    uint64_t gm_l43; /* pick */
    unsigned __int128 gm_l44; /* hi */
    gm_ttovhfee8mf28 gm_l45; /* info */
    unsigned __int128 gm_l46; /* x */
    unsigned __int128 gm_l47; /* r */
    gm_ttovhfee8mf28 gm_l48; /* info */
    unsigned __int128 gm_l49; /* x */
    unsigned __int128 gm_l50; /* y */
    unsigned __int128 gm_l51; /* d */
    gm_ttovhfee8mf28 gm_l52; /* r */
    gm_t9qkh0rsooc8d gm_l53; /* fl */
    double gm_l54; /* x */
    uint64_t gm_l55; /* raw */
    uint64_t gm_l56; /* exp */
    uint16_t gm_l57; /* w */
    double gm_l58; /* x */
    double gm_l59; /* y */
    double gm_l60; /* z */
    gm_ttovhfee8mf28 gm_l61; /* info */
    gm_ttovhfee8mf28 gm_l62; /* ainfo */
    uint32_t gm_l63; /* n */
    unsigned __int128 gm_l64; /* x */
    unsigned __int128 gm_l65; /* araw */
    uint32_t gm_l66; /* k */
    unsigned __int128 gm_l67; /* r */
    gm_ttovhfee8mf28 gm_l68; /* info */
    uint32_t gm_l69; /* n */
    unsigned __int128 gm_l70; /* x */
    unsigned __int128 gm_l71; /* r */
    uint32_t gm_l72; /* <tmp> */
    uint32_t gm_l73; /* <tmp> */
    uint32_t gm_l74; /* i */
    uint32_t gm_l75; /* <tmp> */
    uint32_t gm_l76; /* <tmp> */
    uint32_t gm_l77; /* i */
    gm_ttovhfee8mf28 gm_l78; /* info */
    gm_t86h6hhm6d76n gm_l79; /* seed */
    uint64_t gm_l80; /* h */
    GM_STACK_CHECK(&F, 9969u);
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
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t23) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma4; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma9; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma15; case ((uint8_t)26ULL): goto ma15; case ((uint8_t)27ULL): goto ma16; case ((uint8_t)28ULL): goto ma18; case ((uint8_t)29ULL): goto ma18; case ((uint8_t)30ULL): goto ma18; case ((uint8_t)31ULL): goto ma18; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma18; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma21; case ((uint8_t)38ULL): goto ma21; case ((uint8_t)39ULL): goto ma21; case ((uint8_t)40ULL): goto ma20; case ((uint8_t)41ULL): goto ma20; case ((uint8_t)42ULL): goto ma20; case ((uint8_t)43ULL): goto ma20; case ((uint8_t)44ULL): goto ma20; case ((uint8_t)45ULL): goto ma20; case ((uint8_t)46ULL): goto ma20; case ((uint8_t)47ULL): goto ma20; case ((uint8_t)48ULL): goto ma20; case ((uint8_t)49ULL): goto ma20; case ((uint8_t)50ULL): goto ma20; case ((uint8_t)51ULL): goto ma20; case ((uint8_t)52ULL): goto ma20; case ((uint8_t)53ULL): goto ma20; case ((uint8_t)54ULL): goto ma20; case ((uint8_t)55ULL): goto ma20; case ((uint8_t)56ULL): goto ma20; case ((uint8_t)57ULL): goto ma20; case ((uint8_t)58ULL): goto ma21; case ((uint8_t)59ULL): goto ma21; case ((uint8_t)60ULL): goto ma21; case ((uint8_t)61ULL): goto ma21; case ((uint8_t)62ULL): goto ma19; case ((uint8_t)63ULL): goto ma21; case ((uint8_t)64ULL): goto ma21; case ((uint8_t)65ULL): goto ma21; case ((uint8_t)66ULL): goto ma21; case ((uint8_t)67ULL): goto ma21; case ((uint8_t)68ULL): goto ma21; case ((uint8_t)69ULL): goto ma21; case ((uint8_t)70ULL): goto ma21; case ((uint8_t)71ULL): goto ma21; case ((uint8_t)72ULL): goto ma21; case ((uint8_t)73ULL): goto ma21; case ((uint8_t)74ULL): goto ma21; case ((uint8_t)75ULL): goto ma21; case ((uint8_t)76ULL): goto ma21; case ((uint8_t)77ULL): goto ma21; case ((uint8_t)78ULL): goto ma21; case ((uint8_t)79ULL): goto ma21; case ((uint8_t)80ULL): goto ma21; case ((uint8_t)81ULL): goto ma21; default: goto ma21; }
ma4:;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t27 != ((uint8_t)0ULL)) goto pa26;
            goto po25;
pa26:;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t29 != ((uint8_t)1ULL)) goto pa28;
            goto po25;
pa28:;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t31 != ((uint8_t)2ULL)) goto pa30;
            goto po25;
pa30:;
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t33 != ((uint8_t)3ULL)) goto pa32;
            goto po25;
pa32:;
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t35 != ((uint8_t)4ULL)) goto pa34;
            goto po25;
pa34:;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t37 != ((uint8_t)5ULL)) goto pa36;
            goto po25;
pa36:;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t39 != ((uint8_t)6ULL)) goto pa38;
            goto po25;
pa38:;
            goto mn24;
po25:;
            void * t40 = gm_l0;
            gm_tl874t2atnlh3 t41 = gm_l1;
            gm_tvdg94bk7aa22 t42 = gm_l2;
            gm_t88rcucg1b7uk t43 = gm_l4;
            gm_t88rcucg1b7uk t44 = gm_l3;
            uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcl8pn2pjtmj6k, 192, 107);
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t45 * 16));
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
            gm_b16_8 t49 = gm_l7;
            gm_f_convert_is6an2k((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t40, t41, t42, t43, t44, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn24:;
ma5:;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t51 != ((uint8_t)7ULL)) goto mn50;
            {
                gm_b16_8 t53 = gm_l7;
                gm_f_fill_abmf5g5(t53, ((uint8_t)0ULL));
            }
            {
                uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint64_t t55 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srcl8pn2pjtmj6k, 195, 30);
                uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t55 * 16) + 8);
                uint64_t t58 = (t54 < t57 ? t54 : t57);
                gm_l8 = t58;
            }
            {
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t62 = gm_l8;
                uint64_t t63 = (uintptr_t)t62;
                if (t61 > t63 || t63 > (uintptr_t)t59) gm_trap_slice(t61, t63, t59, gm_srcl8pn2pjtmj6k, 196, 13);
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t60 + t61 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63 - t61));
                uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srcl8pn2pjtmj6k, 196, 33);
                uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t65 * 16) + 8);
                void * t68 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t65 * 16)));
                uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t70 = gm_l8;
                uint64_t t71 = (uintptr_t)t70;
                if (t69 > t71 || t71 > (uintptr_t)t67) gm_trap_slice(t69, t71, t67, gm_srcl8pn2pjtmj6k, 196, 33);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t68 + t69 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71 - t69));
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_f_copy_from_f7j6p05(t64, t73);
            }
            goto me3;
mn50:;
ma6:;
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t75 != ((uint8_t)8ULL)) goto mn74;
            {
                void * t77 = gm_l0;
                uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t78 >= (uintptr_t)(t79)) gm_trap_index(t78, t79, gm_srcl8pn2pjtmj6k, 199, 30);
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t78 * 16));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                uint64_t t82 = gm_f_ptr_of_b77b4c9(t77, t81);
                gm_l9 = t82;
            }
            {
                void * t83 = gm_l0;
                gm_t88rcucg1b7uk t84 = gm_l3;
                uint64_t t85 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t85;
            }
            {
                uint64_t t87 = gm_l9;
                uint8_t t88 = (!(t87 == ((uint64_t)0ULL)));
                if (!t88) goto ie86;
                uint64_t t89 = gm_l10;
                uint8_t t90 = (t89 > ((uint64_t)1ULL));
                if (!t90) goto ie86;
                uint64_t t91 = gm_l9;
                uint64_t t92 = gm_l10;
                if (t92 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 201, 45);
                uint64_t t93 = (uint64_t)(t91 % t92);
                uint8_t t94 = (!(t93 == ((uint64_t)0ULL)));
                if (!t94) goto ie86;
                void * t95 = gm_l0;
                gm_t88rcucg1b7uk t96 = gm_l4;
                uint64_t t97 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t98 = gm_l10;
                uint8_t t99 = (t97 < t98);
                if (!t99) goto ie86;
                void * t100 = gm_l0;
                uint8_t t101 = gm_f_fast_23em7sv(t100);
                uint8_t t102 = (uint8_t)!t101;
                if (!t102) goto ie86;
                {
                    void * t104 = gm_l0;
                    gm_tl874t2atnlh3 t105 = gm_l1;
                    uint64_t t106 = gm_l9;
                    uint64_t t107 = gm_l10;
                    gm_t2lqu8jsj511d t108;
                    GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t106);
                    GM_ST(uint64_t, ((uint8_t *)&t108 + 8), t107);
                    gm_tabb710d9mq2e t109;
                    gm_f_format_l7c15sv((void *)&t109, (void *)NULL, cx, NULL, t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srcl8pn2pjtmj6k, 202, 38);
                    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                    gm_t76123gfet73g t112;
                    gm_f_trap_qh84l9d((void *)&t112, (void *)NULL, cx, NULL, t104, t105, t111);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t112);
                    goto gm_fdone;
                }
                goto ix86;
ie86:;
ix86:;
            }
            {
                gm_b16_8 t113 = gm_l7;
                uint64_t t114 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t114 >= (uintptr_t)(t115)) gm_trap_index(t114, t115, gm_srcl8pn2pjtmj6k, 204, 27);
                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t114 * 16));
                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                gm_f_copy_from_f7j6p05(t113, t117);
            }
            goto me3;
mn74:;
ma7:;
            uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t121 != ((uint8_t)9ULL)) goto pa120;
            goto po119;
pa120:;
            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t123 != ((uint8_t)13ULL)) goto pa122;
            goto po119;
pa122:;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t125 != ((uint8_t)35ULL)) goto pa124;
            goto po119;
pa124:;
            goto mn118;
po119:;
            gm_b16_8 t126 = gm_l7;
            uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcl8pn2pjtmj6k, 206, 51);
            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t127 * 16));
            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
            gm_f_copy_from_f7j6p05(t126, t130);
            goto me3;
mn118:;
ma8:;
            uint8_t t134 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t134 != ((uint8_t)10ULL)) goto pa133;
            goto po132;
pa133:;
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t136 != ((uint8_t)11ULL)) goto pa135;
            goto po132;
pa135:;
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t138 != ((uint8_t)12ULL)) goto pa137;
            goto po132;
pa137:;
            goto mn131;
po132:;
            {
                void * t141 = gm_l0;
                gm_t88rcucg1b7uk t142 = gm_l4;
                gm_t53at80pfdus3 t143;
                gm_f_kind_htv6093((void *)&t143, t141, GM_CONV(gm_csguankko4lmhd, t142));
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
                if (t144 != ((uint8_t)9ULL)) goto ie140;
                gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t143) + 8) + 4));
                gm_l11 = t145;
                {
                    void * t147 = gm_l0;
                    gm_t88rcucg1b7uk t148 = gm_l11;
                    uint64_t t149 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t147, t148);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    __int128 t150 = (__int128)t149;
                    gm_l12 = t150;
                }
                {
                    void * t151 = gm_l0;
                    uint64_t t152 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t153 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t152 >= (uintptr_t)(t153)) gm_trap_index(t152, t153, gm_srcl8pn2pjtmj6k, 210, 32);
                    gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t152 * 16));
                    gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                    uint64_t t156 = gm_f_ptr_of_b77b4c9(t151, t155);
                    __int128 t157 = (__int128)t156;
                    gm_l13 = t157;
                }
                {
                    void * t158 = gm_l0;
                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)t158) + 176));
                    uint64_t r160; bool o161 = __builtin_mul_overflow(t159, ((uint64_t)8ULL), &r160);
                    if (o161) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 211, 22);
                    uint64_t t162 = r160;
                    gm_l14 = t162;
                }
                {
                    uint64_t t163;
                    uint64_t t165 = gm_l14;
                    uint8_t t166 = (t165 >= ((uint64_t)64ULL));
                    if (!t166) goto ie164;
                    uint64_t t168 = (uint64_t)~((uint64_t)0ULL);
                    uint64_t t169 = t168;
                    t163 = t169;
                    goto ix164;
ie164:;
                    uint64_t t171 = gm_l14;
                    unsigned __int128 n172 = (unsigned __int128)t171;
                    if (n172 >= 64) gm_trap_shift((uint64_t)n172, "u64", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t r173 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n172);
                    bool o174 = (uint64_t)(r173 >> (int)n172) != ((uint64_t)1ULL);
                    if (o174) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 212, 60);
                    uint64_t t175 = r173;
                    uint64_t r176; bool o177 = __builtin_sub_overflow(t175, ((uint64_t)1ULL), &r176);
                    if (o177) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 212, 59);
                    uint64_t t178 = r176;
                    uint64_t t179 = t178;
                    t163 = t179;
ix164:;
                    gm_l15 = t163;
                }
                {
                    uint8_t t181 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)12ULL)));
                    if (!t181) goto ie180;
                    {
                        __int128 t183 = gm_l13;
                        void * t184 = gm_l0;
                        uint64_t t185 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_srcl8pn2pjtmj6k, 214, 41);
                        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t185 * 16));
                        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                        uint64_t t189 = gm_f_ptr_of_b77b4c9(t184, t188);
                        __int128 t190 = (__int128)t189;
                        __int128 r191; bool o192 = __builtin_sub_overflow(t183, t190, &r191);
                        if (o192) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 214, 23);
                        __int128 t193 = r191;
                        int64_t t194 = (int64_t)t193;
                        gm_l16 = t194;
                    }
                    {
                        int64_t t195;
                        __int128 t197 = gm_l12;
                        uint8_t t198 = ((t197 == ((__int128)(((unsigned __int128)0u << 64) | 0u))));
                        if (!t198) goto ie196;
                        int64_t t200 = ((int64_t)0LL);
                        t195 = t200;
                        goto ix196;
ie196:;
                        int64_t t202 = gm_l16;
                        __int128 t203 = gm_l12;
                        int64_t t204 = (int64_t)t203;
                        if (t204 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 215, 54);
                        if (t202 == ((int64_t)(-9223372036854775807LL - 1)) && t204 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srcl8pn2pjtmj6k, 215, 54);
                        int64_t t205 = (int64_t)(t202 / t204);
                        int64_t t206 = t205;
                        t195 = t206;
ix196:;
                        gm_l17 = t195;
                    }
                    {
                        void * t207 = gm_l0;
                        gm_b16_8 t208 = gm_l7;
                        int64_t t209 = gm_l17;
                        unsigned __int128 t210 = (unsigned __int128)t209;
                        gm_f_put_uint_cddin99(t207, t208, t210);
                    }
                    {
                        gm_b0_1 t211;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t211);
                        goto gm_fdone;
                    }
                    goto ix180;
ie180:;
ix180:;
                }
                {
                    void * t212 = gm_l0;
                    gm_t88rcucg1b7uk t213 = gm_l5;
                    gm_tg5lql6itufk7 t214 = gm_f_int_info_ulcevdv(t212, GM_CONV(gm_csguankko4lmhd, t213));
                    gm_ttovhfee8mf28 t215;
                    if (((((uint8_t *)&t214))[8] != 0)) {
                        gm_ttovhfee8mf28 t216 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t214) + 0));
                        t215 = t216;
                    } else {
                        void * t217 = gm_l0;
                        gm_tl874t2atnlh3 t218 = gm_l1;
                        gm_b16_8 t219;
                        GM_ST(void *, (uint8_t *)&t219, (void *)(gm_s064htvgs8b59));
                        GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(68));
                        gm_t76123gfet73g t220;
                        gm_f_trap_qh84l9d((void *)&t220, (void *)NULL, cx, NULL, t217, t218, t219);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t220);
                        goto gm_fdone;
                    }
                    gm_l18 = t215;
                }
                {
                    void * t221 = gm_l0;
                    gm_t88rcucg1b7uk t222 = gm_l5;
                    uint64_t t223 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t223 >= (uintptr_t)(t224)) gm_trap_index(t223, t224, gm_srcl8pn2pjtmj6k, 220, 34);
                    gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t223 * 16));
                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                    unsigned __int128 t227 = gm_f_get_int_pg7p4gc(t221, GM_CONV(gm_csguankko4lmhd, t222), t226);
                    gm_l19 = t227;
                }
                {
                    gm_l20 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                }
                {
                    uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    if (!t229) goto ie228;
                    {
                        unsigned __int128 t231 = gm_l19;
                        __int128 t232;
                        memset(&t232, 0, sizeof t232); memcpy(&t232, ((uint8_t *)&t231), sizeof t232 < 16 ? sizeof t232 : 16);
                        gm_l20 = t232;
                    }
                    goto ix228;
ie228:;
                    unsigned __int128 t234 = gm_l19;
                    unsigned __int128 t235 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                    unsigned __int128 n236 = (unsigned __int128)((uint64_t)1ULL);
                    if (n236 >= 128) gm_trap_shift((uint64_t)n236, "u128", gm_srcl8pn2pjtmj6k, 224, 31);
                    unsigned __int128 t237 = (unsigned __int128)(t235 >> (int)n236);
                    uint8_t t238 = (t234 <= t237);
                    if (!t238) goto ie233;
                    {
                        unsigned __int128 t240 = gm_l19;
                        __int128 t241;
                        memset(&t241, 0, sizeof t241); memcpy(&t241, ((uint8_t *)&t240), sizeof t241 < 16 ? sizeof t241 : 16);
                        gm_l20 = t241;
                    }
                    goto ix233;
ie233:;
ix233:;
ix228:;
                }
                {
                    __int128 t242 = gm_l20;
                    __int128 t243 = gm_l12;
                    __int128 r244; bool o245 = __builtin_mul_overflow(t242, t243, &r244);
                    __int128 t246 = r244;
                    gm_l21 = t246;
                }
                {
                    __int128 t247;
                    uint8_t t249 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)10ULL)));
                    if (!t249) goto ie248;
                    __int128 t251 = gm_l13;
                    __int128 t252 = gm_l21;
                    __int128 r253; bool o254 = __builtin_add_overflow(t251, t252, &r253);
                    __int128 t255 = r253;
                    __int128 t256 = t255;
                    t247 = t256;
                    goto ix248;
ie248:;
                    __int128 t258 = gm_l13;
                    __int128 t259 = gm_l21;
                    __int128 r260; bool o261 = __builtin_sub_overflow(t258, t259, &r260);
                    __int128 t262 = r260;
                    __int128 t263 = t262;
                    t247 = t263;
ix248:;
                    gm_l22 = t247;
                }
                {
                    void * t264 = gm_l0;
                    gm_b16_8 t265 = gm_l7;
                    __int128 t266 = gm_l22;
                    uint64_t t267 = (uint64_t)t266;
                    uint64_t t268 = gm_l15;
                    uint64_t t269 = (uint64_t)(t267 & t268);
                    unsigned __int128 t270 = (unsigned __int128)t269;
                    gm_f_put_uint_cddin99(t264, t265, t270);
                }
                goto ix140;
ie140:;
                {
                    void * t272 = gm_l0;
                    gm_tl874t2atnlh3 t273 = gm_l1;
                    gm_b16_8 t274;
                    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_sge745dj3cnrb));
                    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(72));
                    gm_t76123gfet73g t275;
                    gm_f_trap_qh84l9d((void *)&t275, (void *)NULL, cx, NULL, t272, t273, t274);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t275);
                    goto gm_fdone;
                }
ix140:;
            }
            goto me3;
mn131:;
ma9:;
            uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t277 != ((uint8_t)14ULL)) goto mn276;
            {
                void * t279 = gm_l0;
                gm_t88rcucg1b7uk t280 = gm_l4;
                gm_t57euh612ligr t281;
                gm_f_union_def_h8mvf4t((void *)&t281, t279, GM_CONV(gm_csguankko4lmhd, t280));
                gm_tcmkompbl09il t282;
                if (((((uint8_t *)&t281))[24] != 0)) {
                    t282 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t281) + 0));
                } else {
                    void * t283 = gm_l0;
                    gm_tl874t2atnlh3 t284 = gm_l1;
                    gm_b16_8 t285;
                    GM_ST(void *, (uint8_t *)&t285, (void *)(gm_sb9352gc5i144));
                    GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(20));
                    gm_t76123gfet73g t286;
                    gm_f_trap_qh84l9d((void *)&t286, (void *)NULL, cx, NULL, t283, t284, t285);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t286);
                    goto gm_fdone;
                }
                gm_l23 = t282;
            }
            {
                void * t287 = gm_l0;
                gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l23) + 0));
                uint64_t t289 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t287, t288);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t290 = gm_f_us_cuv01fp(t289);
                gm_l24 = t290;
            }
            {
                gm_b16_8 t291 = gm_l7;
                gm_f_fill_abmf5g5(t291, ((uint8_t)0ULL));
            }
            {
                uint64_t t292 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t293 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t294 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t295 = gm_l24;
                uint64_t t296 = (uintptr_t)t295;
                if (t294 > t296 || t296 > (uintptr_t)t292) gm_trap_slice(t294, t296, t292, gm_srcl8pn2pjtmj6k, 235, 13);
                gm_b16_8 t297;
                GM_ST(void *, (uint8_t *)&t297, (void *)(((uint8_t *)t293 + t294 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t297 + 8, (uintptr_t)(t296 - t294));
                uint64_t t298 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t299 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t298 >= (uintptr_t)(t299)) gm_trap_index(t298, t299, gm_srcl8pn2pjtmj6k, 235, 33);
                uint64_t t300 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t298 * 16) + 8);
                void * t301 = ((uint8_t *)GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t298 * 16)));
                uint64_t t302 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t303 = gm_l24;
                uint64_t t304 = (uintptr_t)t303;
                if (t302 > t304 || t304 > (uintptr_t)t300) gm_trap_slice(t302, t304, t300, gm_srcl8pn2pjtmj6k, 235, 33);
                gm_b16_8 t305;
                GM_ST(void *, (uint8_t *)&t305, (void *)(((uint8_t *)t301 + t302 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(t304 - t302));
                gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&t305));
                gm_f_copy_from_f7j6p05(t297, t306);
            }
            goto me3;
mn276:;
ma10:;
            uint8_t t308 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t308 != ((uint8_t)15ULL)) goto mn307;
            {
                void * t310 = gm_l0;
                gm_t88rcucg1b7uk t311 = gm_l3;
                gm_t88rcucg1b7uk t312 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t310, GM_CONV(gm_csguankko4lmhd, t311)));
                gm_l25 = t312;
            }
            {
                void * t313 = gm_l0;
                gm_t88rcucg1b7uk t314 = gm_l25;
                gm_t57euh612ligr t315;
                gm_f_union_def_h8mvf4t((void *)&t315, t313, GM_CONV(gm_csguankko4lmhd, t314));
                gm_tcmkompbl09il t316;
                if (((((uint8_t *)&t315))[24] != 0)) {
                    t316 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t315) + 0));
                } else {
                    void * t317 = gm_l0;
                    gm_tl874t2atnlh3 t318 = gm_l1;
                    gm_b16_8 t319;
                    GM_ST(void *, (uint8_t *)&t319, (void *)(gm_stk678hojgu6q));
                    GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(23));
                    gm_t76123gfet73g t320;
                    gm_f_trap_qh84l9d((void *)&t320, (void *)NULL, cx, NULL, t317, t318, t319);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t320);
                    goto gm_fdone;
                }
                gm_l26 = t316;
            }
            {
                void * t321 = gm_l0;
                gm_t88rcucg1b7uk t322 = gm_l4;
                gm_tg5lql6itufk7 t323 = gm_f_int_info_ulcevdv(t321, GM_CONV(gm_csguankko4lmhd, t322));
                gm_ttovhfee8mf28 t324;
                if (((((uint8_t *)&t323))[8] != 0)) {
                    gm_ttovhfee8mf28 t325 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t323) + 0));
                    t324 = t325;
                } else {
                    void * t326 = gm_l0;
                    gm_tl874t2atnlh3 t327 = gm_l1;
                    gm_b16_8 t328;
                    GM_ST(void *, (uint8_t *)&t328, (void *)(gm_s3ergh2il1tjf));
                    GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(42));
                    gm_t76123gfet73g t329;
                    gm_f_trap_qh84l9d((void *)&t329, (void *)NULL, cx, NULL, t326, t327, t328);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t329);
                    goto gm_fdone;
                }
                gm_l27 = t324;
            }
            {
                uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                void * t331 = gm_l0;
                gm_t88rcucg1b7uk t332 = gm_l4;
                uint64_t t333 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t333 >= (uintptr_t)(t334)) gm_trap_index(t333, t334, gm_srcl8pn2pjtmj6k, 241, 53);
                gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t333 * 16));
                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                unsigned __int128 t337 = gm_f_get_int_pg7p4gc(t331, GM_CONV(gm_csguankko4lmhd, t332), t336);
                gm_f_num_val_i3upeur((void *)&gm_l28, t330, t337);
            }
            {
                {
                    gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                    gm_l29 = t340;
                }
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t342 = gm_l30;
                        uint64_t t343 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        uint8_t t344 = (t342 < t343);
                        if (!t344) goto lb341;
                        {
                            gm_t3l8idb2m6c1r t347;
                            uint64_t t348 = gm_l30;
                            GM_ST(uint64_t, (uint8_t *)&t347 + 0, t348);
                            uint64_t t349 = gm_l30;
                            uint64_t t350 = (uintptr_t)t349;
                            uint64_t t351 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            memcpy((uint8_t *)&t347 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t350 * 72), 72);
                            uint64_t t354 = GM_LD(uint64_t, (((uint8_t *)&t347) + 0));
                            gm_l31 = t354;
                            F.l32 = (*(gm_tckdobunaulee *)(((uint8_t *)&t347) + 8));
                            {
                                uint8_t t357;
                                if ((((((uint8_t *)&F.l32) + 16))[4] != 0)) goto mn359;
                                t357 = ((uint8_t)1);
                                goto me358;
mn359:;
                                t357 = ((uint8_t)0);
                                goto me358;
mn360:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 243, 20);
me358:;
                                if (!t357) goto ie356;
                                uint8_t t361 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l28) + 0));
                                uint8_t t363 = ((t361 == t362));
                                if (!t363) goto ie356;
                                {
                                    unsigned __int128 t365 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l32) + 24)));
                                    gm_l33 = t365;
                                }
                                {
                                    unsigned __int128 t366;
                                    uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)&F.l32) + 24) + 0));
                                    if (!t368) goto ie367;
                                    unsigned __int128 t370 = gm_l33;
                                    unsigned __int128 r371; bool o372 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t370, &r371);
                                    unsigned __int128 t373 = r371;
                                    unsigned __int128 t374 = t373;
                                    t366 = t374;
                                    goto ix367;
ie367:;
                                    unsigned __int128 t376 = gm_l33;
                                    unsigned __int128 t377 = t376;
                                    t366 = t377;
ix367:;
                                    gm_l34 = t366;
                                }
                                {
                                    unsigned __int128 t379 = gm_l34;
                                    unsigned __int128 t380 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l28) + 16));
                                    uint8_t t381 = ((t379 == t380));
                                    if (!t381) goto ie378;
                                    {
                                        void * t383 = gm_l0;
                                        gm_t2lqu8jsj511d t384 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t383));
                                        gm_l35 = t384;
                                    }
                                    {
                                        void * t385 = gm_l0;
                                        void * t386 = gm_l0;
                                        gm_t88rcucg1b7uk t387 = gm_l25;
                                        uint64_t t388 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t386, t387);
                                        if (gm_unw.kind) {
                                            goto gm_ll345;
                                        }
                                        gm_b16_8 t389 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t385, t388);
                                        if (gm_unw.kind) {
                                            goto gm_ll345;
                                        }
                                        gm_l36 = t389;
                                    }
                                    {
                                        void * t390 = gm_l0;
                                        gm_t88rcucg1b7uk t391 = gm_l25;
                                        gm_b16_8 t392 = gm_l36;
                                        uint64_t t393 = gm_l31;
                                        uint32_t t394 = (uint32_t)t393;
                                        gm_b16_8 t395 = gm_f_empty_slice_ijtp9lo();
                                        gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&t395));
                                        gm_b0_1 t397 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t390, t391, t392, t394, t396);
                                        if (gm_unw.kind) {
                                            goto gm_ll345;
                                        }
                                    }
                                    {
                                        void * t398 = gm_l0;
                                        gm_t88rcucg1b7uk t399 = gm_l3;
                                        gm_b16_8 t400 = gm_l7;
                                        gm_b16_8 t401 = gm_l36;
                                        gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
                                        gm_b0_1 t403 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t398, t399, t400, t402);
                                        if (gm_unw.kind) {
                                            goto gm_ll345;
                                        }
                                    }
                                    {
                                        void * t404 = gm_l0;
                                        gm_t2lqu8jsj511d t405 = gm_l35;
                                        gm_f_release_jn5kqdi(t404, GM_CONV(gm_csc7lc6jlkqkuq, t405));
                                    }
                                    {
                                        gm_b0_1 t406;
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t406);
                                        goto gm_fdone;
                                    }
                                    goto ix378;
ie378:;
ix378:;
                                }
                                goto ix356;
ie356:;
ix356:;
                            }
                            goto me352;
mn353:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 242, 13);
me352:;
                        }
lc341:;
                        {
                            uint64_t t408 = gm_l30;
                            uint64_t r409; bool o410 = __builtin_add_overflow(t408, ((uint64_t)1ULL), &r409);
                            uint64_t t411 = r409;
                            gm_l30 = t411;
                        }
                    }
gm_ll345:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb341; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc341; }
                    }
                    goto gm_lad;
lb341:;
                }
            }
            {
                gm_b16_8 t412 = gm_l7;
                gm_f_fill_abmf5g5(t412, ((uint8_t)0ULL));
            }
            goto me3;
mn307:;
ma11:;
            uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t416 != ((uint8_t)16ULL)) goto pa415;
            goto po414;
pa415:;
            uint8_t t418 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t418 != ((uint8_t)17ULL)) goto pa417;
            goto po414;
pa417:;
            uint8_t t420 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t420 != ((uint8_t)18ULL)) goto pa419;
            goto po414;
pa419:;
            goto mn413;
po414:;
            {
                void * t423 = gm_l0;
                gm_t88rcucg1b7uk t424 = gm_l4;
                gm_t53at80pfdus3 t425;
                gm_f_kind_htv6093((void *)&t425, t423, GM_CONV(gm_csguankko4lmhd, t424));
                uint8_t t426 = GM_LD(uint8_t, (((uint8_t *)&t425) + 0));
                if (t426 != ((uint8_t)3ULL)) goto ie422;
                {
                    void * t428 = gm_l0;
                    gm_t88rcucg1b7uk t429 = gm_l4;
                    uint64_t t430 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t431 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t430 >= (uintptr_t)(t431)) gm_trap_index(t430, t431, gm_srcl8pn2pjtmj6k, 260, 38);
                    gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t430 * 16));
                    gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
                    double t434 = gm_f_get_float_grmhlm4(t428, GM_CONV(gm_csguankko4lmhd, t429), t433);
                    gm_l37 = t434;
                }
                {
                    void * t435 = gm_l0;
                    gm_t88rcucg1b7uk t436 = gm_l4;
                    uint64_t t437 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t438 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t437 >= (uintptr_t)(t438)) gm_trap_index(t437, t438, gm_srcl8pn2pjtmj6k, 261, 38);
                    gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t437 * 16));
                    gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
                    double t441 = gm_f_get_float_grmhlm4(t435, GM_CONV(gm_csguankko4lmhd, t436), t440);
                    gm_l38 = t441;
                }
                {
                    double t442;
                    uint8_t t448 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    switch (t448) { case ((uint8_t)0ULL): goto ma446; case ((uint8_t)1ULL): goto ma446; case ((uint8_t)2ULL): goto ma446; case ((uint8_t)3ULL): goto ma446; case ((uint8_t)4ULL): goto ma446; case ((uint8_t)5ULL): goto ma446; case ((uint8_t)6ULL): goto ma446; case ((uint8_t)7ULL): goto ma446; case ((uint8_t)8ULL): goto ma446; case ((uint8_t)9ULL): goto ma446; case ((uint8_t)10ULL): goto ma446; case ((uint8_t)11ULL): goto ma446; case ((uint8_t)12ULL): goto ma446; case ((uint8_t)13ULL): goto ma446; case ((uint8_t)14ULL): goto ma446; case ((uint8_t)15ULL): goto ma446; case ((uint8_t)16ULL): goto ma444; case ((uint8_t)17ULL): goto ma445; case ((uint8_t)18ULL): goto ma446; case ((uint8_t)19ULL): goto ma446; case ((uint8_t)20ULL): goto ma446; case ((uint8_t)21ULL): goto ma446; case ((uint8_t)22ULL): goto ma446; case ((uint8_t)23ULL): goto ma446; case ((uint8_t)24ULL): goto ma446; case ((uint8_t)25ULL): goto ma446; case ((uint8_t)26ULL): goto ma446; case ((uint8_t)27ULL): goto ma446; case ((uint8_t)28ULL): goto ma446; case ((uint8_t)29ULL): goto ma446; case ((uint8_t)30ULL): goto ma446; case ((uint8_t)31ULL): goto ma446; case ((uint8_t)32ULL): goto ma446; case ((uint8_t)33ULL): goto ma446; case ((uint8_t)34ULL): goto ma446; case ((uint8_t)35ULL): goto ma446; case ((uint8_t)36ULL): goto ma446; case ((uint8_t)37ULL): goto ma446; case ((uint8_t)38ULL): goto ma446; case ((uint8_t)39ULL): goto ma446; case ((uint8_t)40ULL): goto ma446; case ((uint8_t)41ULL): goto ma446; case ((uint8_t)42ULL): goto ma446; case ((uint8_t)43ULL): goto ma446; case ((uint8_t)44ULL): goto ma446; case ((uint8_t)45ULL): goto ma446; case ((uint8_t)46ULL): goto ma446; case ((uint8_t)47ULL): goto ma446; case ((uint8_t)48ULL): goto ma446; case ((uint8_t)49ULL): goto ma446; case ((uint8_t)50ULL): goto ma446; case ((uint8_t)51ULL): goto ma446; case ((uint8_t)52ULL): goto ma446; case ((uint8_t)53ULL): goto ma446; case ((uint8_t)54ULL): goto ma446; case ((uint8_t)55ULL): goto ma446; case ((uint8_t)56ULL): goto ma446; case ((uint8_t)57ULL): goto ma446; case ((uint8_t)58ULL): goto ma446; case ((uint8_t)59ULL): goto ma446; case ((uint8_t)60ULL): goto ma446; case ((uint8_t)61ULL): goto ma446; case ((uint8_t)62ULL): goto ma446; case ((uint8_t)63ULL): goto ma446; case ((uint8_t)64ULL): goto ma446; case ((uint8_t)65ULL): goto ma446; case ((uint8_t)66ULL): goto ma446; case ((uint8_t)67ULL): goto ma446; case ((uint8_t)68ULL): goto ma446; case ((uint8_t)69ULL): goto ma446; case ((uint8_t)70ULL): goto ma446; case ((uint8_t)71ULL): goto ma446; case ((uint8_t)72ULL): goto ma446; case ((uint8_t)73ULL): goto ma446; case ((uint8_t)74ULL): goto ma446; case ((uint8_t)75ULL): goto ma446; case ((uint8_t)76ULL): goto ma446; case ((uint8_t)77ULL): goto ma446; case ((uint8_t)78ULL): goto ma446; case ((uint8_t)79ULL): goto ma446; case ((uint8_t)80ULL): goto ma446; case ((uint8_t)81ULL): goto ma446; default: goto ma446; }
ma444:;
                    uint8_t t450 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t450 != ((uint8_t)16ULL)) goto mn449;
                    double t451 = gm_l37;
                    double t452 = gm_l38;
                    double t453 = gm_f_num_fmin_1k5uqp0(t451, t452);
                    t442 = t453;
                    goto me443;
mn449:;
ma445:;
                    uint8_t t455 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    if (t455 != ((uint8_t)17ULL)) goto mn454;
                    double t456 = gm_l37;
                    double t457 = gm_l38;
                    double t458 = gm_f_num_fmax_0qau2jp(t456, t457);
                    t442 = t458;
                    goto me443;
mn454:;
ma446:;
                    double t460 = gm_l37;
                    double t461 = gm_l38;
                    double t462 = gm_f_num_fmax_0qau2jp(t460, t461);
                    void * t463 = gm_l0;
                    gm_t88rcucg1b7uk t464 = gm_l4;
                    uint64_t t465 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t466 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t465 >= (uintptr_t)(t466)) gm_trap_index(t465, t466, gm_srcl8pn2pjtmj6k, 265, 67);
                    gm_b16_8 t467 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t465 * 16));
                    gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
                    double t469 = gm_f_get_float_grmhlm4(t463, GM_CONV(gm_csguankko4lmhd, t464), t468);
                    double t470 = gm_f_num_fmin_1k5uqp0(t462, t469);
                    t442 = t470;
                    goto me443;
mn459:;
ma447:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 262, 22);
me443:;
                    gm_l39 = t442;
                }
                {
                    void * t471 = gm_l0;
                    gm_t88rcucg1b7uk t472 = gm_l4;
                    double t473 = gm_l39;
                    gm_b16_8 t474 = gm_l7;
                    gm_f_put_float_fiad72d(t471, GM_CONV(gm_csguankko4lmhd, t472), t473, t474);
                }
                {
                    gm_b0_1 t475;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t475);
                    goto gm_fdone;
                }
                goto ix422;
ie422:;
ix422:;
            }
            {
                void * t476 = gm_l0;
                gm_t88rcucg1b7uk t477 = gm_l4;
                gm_tg5lql6itufk7 t478 = gm_f_int_info_ulcevdv(t476, GM_CONV(gm_csguankko4lmhd, t477));
                gm_ttovhfee8mf28 t479;
                if (((((uint8_t *)&t478))[8] != 0)) {
                    gm_ttovhfee8mf28 t480 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t478) + 0));
                    t479 = t480;
                } else {
                    void * t481 = gm_l0;
                    gm_tl874t2atnlh3 t482 = gm_l1;
                    void * t483 = gm_l0;
                    void * t484 = GM_LD(void *, (((uint8_t *)t483) + 8));
                    gm_t88rcucg1b7uk t485 = gm_l4;
                    gm_b16_8 t486 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t484, t485);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t487;
                    GM_ST(gm_b16_8, ((uint8_t *)&t487 + 0), t486);
                    gm_tabb710d9mq2e t488;
                    gm_f_format_kj15nqp((void *)&t488, (void *)NULL, cx, NULL, t487);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t488))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t488) + 8, gm_srcl8pn2pjtmj6k, 270, 62);
                    gm_b16_8 t489 = GM_LD(gm_b16_8, (((uint8_t *)&t488) + 8));
                    gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)&t489));
                    gm_t76123gfet73g t491;
                    gm_f_trap_qh84l9d((void *)&t491, (void *)NULL, cx, NULL, t481, t482, t490);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t491);
                    goto gm_fdone;
                }
                gm_l40 = t479;
            }
            {
                void * t492 = gm_l0;
                gm_t88rcucg1b7uk t493 = gm_l4;
                uint64_t t494 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t495 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t494 >= (uintptr_t)(t495)) gm_trap_index(t494, t495, gm_srcl8pn2pjtmj6k, 271, 32);
                gm_b16_8 t496 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t494 * 16));
                gm_b16_8 t497 = GM_LD(gm_b16_8, ((uint8_t *)&t496));
                unsigned __int128 t498 = gm_f_get_int_pg7p4gc(t492, GM_CONV(gm_csguankko4lmhd, t493), t497);
                gm_l41 = t498;
            }
            {
                void * t499 = gm_l0;
                gm_t88rcucg1b7uk t500 = gm_l4;
                uint64_t t501 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t502 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t501 >= (uintptr_t)(t502)) gm_trap_index(t501, t502, gm_srcl8pn2pjtmj6k, 272, 32);
                gm_b16_8 t503 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t501 * 16));
                gm_b16_8 t504 = GM_LD(gm_b16_8, ((uint8_t *)&t503));
                unsigned __int128 t505 = gm_f_get_int_pg7p4gc(t499, GM_CONV(gm_csguankko4lmhd, t500), t504);
                gm_l42 = t505;
            }
            {
                gm_l43 = ((uint64_t)0ULL);
            }
            {
                uint8_t t511 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t511) { case ((uint8_t)0ULL): goto ma509; case ((uint8_t)1ULL): goto ma509; case ((uint8_t)2ULL): goto ma509; case ((uint8_t)3ULL): goto ma509; case ((uint8_t)4ULL): goto ma509; case ((uint8_t)5ULL): goto ma509; case ((uint8_t)6ULL): goto ma509; case ((uint8_t)7ULL): goto ma509; case ((uint8_t)8ULL): goto ma509; case ((uint8_t)9ULL): goto ma509; case ((uint8_t)10ULL): goto ma509; case ((uint8_t)11ULL): goto ma509; case ((uint8_t)12ULL): goto ma509; case ((uint8_t)13ULL): goto ma509; case ((uint8_t)14ULL): goto ma509; case ((uint8_t)15ULL): goto ma509; case ((uint8_t)16ULL): goto ma507; case ((uint8_t)17ULL): goto ma508; case ((uint8_t)18ULL): goto ma509; case ((uint8_t)19ULL): goto ma509; case ((uint8_t)20ULL): goto ma509; case ((uint8_t)21ULL): goto ma509; case ((uint8_t)22ULL): goto ma509; case ((uint8_t)23ULL): goto ma509; case ((uint8_t)24ULL): goto ma509; case ((uint8_t)25ULL): goto ma509; case ((uint8_t)26ULL): goto ma509; case ((uint8_t)27ULL): goto ma509; case ((uint8_t)28ULL): goto ma509; case ((uint8_t)29ULL): goto ma509; case ((uint8_t)30ULL): goto ma509; case ((uint8_t)31ULL): goto ma509; case ((uint8_t)32ULL): goto ma509; case ((uint8_t)33ULL): goto ma509; case ((uint8_t)34ULL): goto ma509; case ((uint8_t)35ULL): goto ma509; case ((uint8_t)36ULL): goto ma509; case ((uint8_t)37ULL): goto ma509; case ((uint8_t)38ULL): goto ma509; case ((uint8_t)39ULL): goto ma509; case ((uint8_t)40ULL): goto ma509; case ((uint8_t)41ULL): goto ma509; case ((uint8_t)42ULL): goto ma509; case ((uint8_t)43ULL): goto ma509; case ((uint8_t)44ULL): goto ma509; case ((uint8_t)45ULL): goto ma509; case ((uint8_t)46ULL): goto ma509; case ((uint8_t)47ULL): goto ma509; case ((uint8_t)48ULL): goto ma509; case ((uint8_t)49ULL): goto ma509; case ((uint8_t)50ULL): goto ma509; case ((uint8_t)51ULL): goto ma509; case ((uint8_t)52ULL): goto ma509; case ((uint8_t)53ULL): goto ma509; case ((uint8_t)54ULL): goto ma509; case ((uint8_t)55ULL): goto ma509; case ((uint8_t)56ULL): goto ma509; case ((uint8_t)57ULL): goto ma509; case ((uint8_t)58ULL): goto ma509; case ((uint8_t)59ULL): goto ma509; case ((uint8_t)60ULL): goto ma509; case ((uint8_t)61ULL): goto ma509; case ((uint8_t)62ULL): goto ma509; case ((uint8_t)63ULL): goto ma509; case ((uint8_t)64ULL): goto ma509; case ((uint8_t)65ULL): goto ma509; case ((uint8_t)66ULL): goto ma509; case ((uint8_t)67ULL): goto ma509; case ((uint8_t)68ULL): goto ma509; case ((uint8_t)69ULL): goto ma509; case ((uint8_t)70ULL): goto ma509; case ((uint8_t)71ULL): goto ma509; case ((uint8_t)72ULL): goto ma509; case ((uint8_t)73ULL): goto ma509; case ((uint8_t)74ULL): goto ma509; case ((uint8_t)75ULL): goto ma509; case ((uint8_t)76ULL): goto ma509; case ((uint8_t)77ULL): goto ma509; case ((uint8_t)78ULL): goto ma509; case ((uint8_t)79ULL): goto ma509; case ((uint8_t)80ULL): goto ma509; case ((uint8_t)81ULL): goto ma509; default: goto ma509; }
ma507:;
                uint8_t t513 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t513 != ((uint8_t)16ULL)) goto mn512;
                {
                    uint64_t t515;
                    uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t518 = gm_l41;
                    unsigned __int128 t519 = gm_l42;
                    int32_t t520 = gm_f_int_cmp_kb3sm6u(t517, t518, t519);
                    uint8_t t521 = (t520 <= ((int32_t)0LL));
                    if (!t521) goto ie516;
                    uint64_t t523 = ((uint64_t)0ULL);
                    t515 = t523;
                    goto ix516;
ie516:;
                    uint64_t t525 = ((uint64_t)1ULL);
                    t515 = t525;
ix516:;
                    gm_l43 = t515;
                }
                goto me506;
mn512:;
ma508:;
                uint8_t t527 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t527 != ((uint8_t)17ULL)) goto mn526;
                {
                    uint64_t t529;
                    uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t532 = gm_l41;
                    unsigned __int128 t533 = gm_l42;
                    int32_t t534 = gm_f_int_cmp_kb3sm6u(t531, t532, t533);
                    uint8_t t535 = (t534 >= ((int32_t)0LL));
                    if (!t535) goto ie530;
                    uint64_t t537 = ((uint64_t)0ULL);
                    t529 = t537;
                    goto ix530;
ie530:;
                    uint64_t t539 = ((uint64_t)1ULL);
                    t529 = t539;
ix530:;
                    gm_l43 = t529;
                }
                goto me506;
mn526:;
ma509:;
                {
                    void * t542 = gm_l0;
                    gm_t88rcucg1b7uk t543 = gm_l4;
                    uint64_t t544 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t545 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t544 >= (uintptr_t)(t545)) gm_trap_index(t544, t545, gm_srcl8pn2pjtmj6k, 278, 41);
                    gm_b16_8 t546 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t544 * 16));
                    gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
                    unsigned __int128 t548 = gm_f_get_int_pg7p4gc(t542, GM_CONV(gm_csguankko4lmhd, t543), t547);
                    gm_l44 = t548;
                }
                {
                    uint8_t t550 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t551 = gm_l41;
                    unsigned __int128 t552 = gm_l42;
                    int32_t t553 = gm_f_int_cmp_kb3sm6u(t550, t551, t552);
                    uint8_t t554 = (t553 < ((int32_t)0LL));
                    if (!t554) goto ie549;
                    {
                        gm_l43 = ((uint64_t)1ULL);
                    }
                    goto ix549;
ie549:;
                    uint8_t t557 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                    unsigned __int128 t558 = gm_l41;
                    unsigned __int128 t559 = gm_l44;
                    int32_t t560 = gm_f_int_cmp_kb3sm6u(t557, t558, t559);
                    uint8_t t561 = (t560 > ((int32_t)0LL));
                    if (!t561) goto ie556;
                    {
                        gm_l43 = ((uint64_t)2ULL);
                    }
                    goto ix556;
ie556:;
ix556:;
ix549:;
                }
                goto me506;
mn540:;
ma510:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 274, 13);
me506:;
            }
            {
                gm_b16_8 t563 = gm_l7;
                uint64_t t564 = gm_l43;
                uint64_t t565 = (uintptr_t)t564;
                uint64_t t566 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t565 >= (uintptr_t)(t566)) gm_trap_index(t565, t566, gm_srcl8pn2pjtmj6k, 286, 27);
                gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t565 * 16));
                gm_b16_8 t568 = GM_LD(gm_b16_8, ((uint8_t *)&t567));
                gm_f_copy_from_f7j6p05(t563, t568);
            }
            goto me3;
mn413:;
ma12:;
            uint8_t t570 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t570 != ((uint8_t)19ULL)) goto mn569;
            {
                void * t573 = gm_l0;
                gm_t88rcucg1b7uk t574 = gm_l4;
                gm_t53at80pfdus3 t575;
                gm_f_kind_htv6093((void *)&t575, t573, GM_CONV(gm_csguankko4lmhd, t574));
                uint8_t t576 = GM_LD(uint8_t, (((uint8_t *)&t575) + 0));
                if (t576 != ((uint8_t)3ULL)) goto ie572;
                {
                    void * t578 = gm_l0;
                    gm_t88rcucg1b7uk t579 = gm_l4;
                    void * t580 = gm_l0;
                    gm_t88rcucg1b7uk t581 = gm_l4;
                    uint64_t t582 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t583 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t582 >= (uintptr_t)(t583)) gm_trap_index(t582, t583, gm_srcl8pn2pjtmj6k, 290, 50);
                    gm_b16_8 t584 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t582 * 16));
                    gm_b16_8 t585 = GM_LD(gm_b16_8, ((uint8_t *)&t584));
                    double t586 = gm_f_get_float_grmhlm4(t580, GM_CONV(gm_csguankko4lmhd, t581), t585);
                    uint64_t t587;
                    memset(&t587, 0, sizeof t587); memcpy(&t587, ((uint8_t *)&t586), sizeof t587 < 8 ? sizeof t587 : 8);
                    uint64_t t588 = (uint64_t)(t587 & ((uint64_t)9223372036854775807ULL));
                    double t589;
                    memset(&t589, 0, sizeof t589); memcpy(&t589, ((uint8_t *)&t588), sizeof t589 < 8 ? sizeof t589 : 8);
                    gm_b16_8 t590 = gm_l7;
                    gm_f_put_float_fiad72d(t578, GM_CONV(gm_csguankko4lmhd, t579), t589, t590);
                }
                {
                    gm_b0_1 t591;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t591);
                    goto gm_fdone;
                }
                goto ix572;
ie572:;
ix572:;
            }
            {
                void * t592 = gm_l0;
                gm_t88rcucg1b7uk t593 = gm_l4;
                gm_tg5lql6itufk7 t594 = gm_f_int_info_ulcevdv(t592, GM_CONV(gm_csguankko4lmhd, t593));
                gm_ttovhfee8mf28 t595;
                if (((((uint8_t *)&t594))[8] != 0)) {
                    gm_ttovhfee8mf28 t596 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t594) + 0));
                    t595 = t596;
                } else {
                    void * t597 = gm_l0;
                    gm_tl874t2atnlh3 t598 = gm_l1;
                    gm_b16_8 t599;
                    GM_ST(void *, (uint8_t *)&t599, (void *)(gm_sbel6qookv0gl));
                    GM_ST(uintptr_t, (uint8_t *)&t599 + 8, (uintptr_t)(53));
                    gm_t76123gfet73g t600;
                    gm_f_trap_qh84l9d((void *)&t600, (void *)NULL, cx, NULL, t597, t598, t599);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t600);
                    goto gm_fdone;
                }
                gm_l45 = t595;
            }
            {
                void * t601 = gm_l0;
                gm_t88rcucg1b7uk t602 = gm_l4;
                uint64_t t603 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t604 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t603 >= (uintptr_t)(t604)) gm_trap_index(t603, t604, gm_srcl8pn2pjtmj6k, 294, 32);
                gm_b16_8 t605 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t603 * 16));
                gm_b16_8 t606 = GM_LD(gm_b16_8, ((uint8_t *)&t605));
                unsigned __int128 t607 = gm_f_get_int_pg7p4gc(t601, GM_CONV(gm_csguankko4lmhd, t602), t606);
                gm_l46 = t607;
            }
            {
                uint8_t t609 = 0;
                uint8_t t610 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                uint8_t t611 = (uint8_t)!t610;
                if (!t611) {
                    unsigned __int128 t612 = gm_l46;
                    unsigned __int128 n613 = (unsigned __int128)((uint64_t)127ULL);
                    if (n613 >= 128) gm_trap_shift((uint64_t)n613, "u128", gm_srcl8pn2pjtmj6k, 295, 36);
                    unsigned __int128 t614 = (unsigned __int128)(t612 >> (int)n613);
                    uint8_t t615 = ((t614 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    t609 = t615;
                } else t609 = 1;
                if (!t609) goto ie608;
                {
                    void * t617 = gm_l0;
                    gm_b16_8 t618 = gm_l7;
                    unsigned __int128 t619 = gm_l46;
                    gm_f_put_uint_cddin99(t617, t618, t619);
                }
                {
                    gm_b0_1 t620;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t620);
                    goto gm_fdone;
                }
                goto ix608;
ie608:;
ix608:;
            }
            {
                unsigned __int128 t621 = gm_l46;
                unsigned __int128 r622; bool o623 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), t621, &r622);
                unsigned __int128 t624 = r622;
                gm_l47 = t624;
            }
            {
                unsigned __int128 t626 = gm_l47;
                uint32_t t627 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 4));
                uint32_t r628; bool o629 = __builtin_sub_overflow(t627, ((uint32_t)1ULL), &r628);
                if (o629) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 300, 34);
                uint32_t t630 = r628;
                unsigned __int128 n631 = (unsigned __int128)t630;
                if (n631 >= 128) gm_trap_shift((uint64_t)n631, "u128", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 r632 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n631);
                bool o633 = (unsigned __int128)(r632 >> (int)n631) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                if (o633) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 300, 22);
                unsigned __int128 t634 = r632;
                uint8_t t635 = ((t626 == t634));
                if (!t635) goto ie625;
                {
                    void * t637 = gm_l0;
                    gm_tl874t2atnlh3 t638 = gm_l1;
                    gm_b16_8 t639;
                    GM_ST(void *, (uint8_t *)&t639, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t639 + 8, (uintptr_t)(3));
                    gm_tcfa6l7reoosn t640;
                    gm_f_checked_overflow_ma1bm1g((void *)&t640, (void *)NULL, cx, NULL, t637, t638, t639);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t640))[0] != 0)) {
                        gm_tcfa6l7reoosn t641;
                        GM_ST(uint8_t, (uint8_t *)&t641 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t641 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t641 + 4, 0);
                        ((uint8_t *)&t641)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t641 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t640) + 8)));
                        F.ret = t641;
                        goto gm_fret;
                    }
                    gm_b0_1 t642 = GM_LD(gm_b0_1, (((uint8_t *)&t640) + 8));
                }
                {
                    void * t643 = gm_l0;
                    gm_b16_8 t644 = gm_l7;
                    unsigned __int128 t645 = gm_l46;
                    gm_f_put_uint_cddin99(t643, t644, t645);
                }
                {
                    gm_b0_1 t646;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t646);
                    goto gm_fdone;
                }
                goto ix625;
ie625:;
ix625:;
            }
            {
                void * t647 = gm_l0;
                gm_b16_8 t648 = gm_l7;
                unsigned __int128 t649 = gm_l47;
                gm_f_put_uint_cddin99(t647, t648, t649);
            }
            goto me3;
mn569:;
ma13:;
            uint8_t t651 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t651 != ((uint8_t)20ULL)) goto mn650;
            {
                void * t653 = gm_l0;
                gm_t88rcucg1b7uk t654 = gm_l4;
                gm_tg5lql6itufk7 t655 = gm_f_int_info_ulcevdv(t653, GM_CONV(gm_csguankko4lmhd, t654));
                gm_ttovhfee8mf28 t656;
                if (((((uint8_t *)&t655))[8] != 0)) {
                    gm_ttovhfee8mf28 t657 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t655) + 0));
                    t656 = t657;
                } else {
                    void * t658 = gm_l0;
                    gm_tl874t2atnlh3 t659 = gm_l1;
                    gm_b16_8 t660;
                    GM_ST(void *, (uint8_t *)&t660, (void *)(gm_s3gnqv0up712m));
                    GM_ST(uintptr_t, (uint8_t *)&t660 + 8, (uintptr_t)(60));
                    gm_t76123gfet73g t661;
                    gm_f_trap_qh84l9d((void *)&t661, (void *)NULL, cx, NULL, t658, t659, t660);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t661);
                    goto gm_fdone;
                }
                gm_l48 = t656;
            }
            {
                void * t662 = gm_l0;
                gm_t88rcucg1b7uk t663 = gm_l4;
                uint64_t t664 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t665 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t664 >= (uintptr_t)(t665)) gm_trap_index(t664, t665, gm_srcl8pn2pjtmj6k, 309, 32);
                gm_b16_8 t666 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t664 * 16));
                gm_b16_8 t667 = GM_LD(gm_b16_8, ((uint8_t *)&t666));
                unsigned __int128 t668 = gm_f_get_int_pg7p4gc(t662, GM_CONV(gm_csguankko4lmhd, t663), t667);
                gm_l49 = t668;
            }
            {
                void * t669 = gm_l0;
                gm_t88rcucg1b7uk t670 = gm_l5;
                uint64_t t671 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t672 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t671 >= (uintptr_t)(t672)) gm_trap_index(t671, t672, gm_srcl8pn2pjtmj6k, 310, 32);
                gm_b16_8 t673 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t671 * 16));
                gm_b16_8 t674 = GM_LD(gm_b16_8, ((uint8_t *)&t673));
                unsigned __int128 t675 = gm_f_get_int_pg7p4gc(t669, GM_CONV(gm_csguankko4lmhd, t670), t674);
                gm_l50 = t675;
            }
            {
                gm_l51 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t677 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 0));
                unsigned __int128 t678 = gm_l49;
                unsigned __int128 t679 = gm_l50;
                int32_t t680 = gm_f_int_cmp_kb3sm6u(t677, t678, t679);
                uint8_t t681 = (t680 >= ((int32_t)0LL));
                if (!t681) goto ie676;
                {
                    unsigned __int128 t683 = gm_l49;
                    unsigned __int128 t684 = gm_l50;
                    unsigned __int128 r685; bool o686 = __builtin_sub_overflow(t683, t684, &r685);
                    unsigned __int128 t687 = r685;
                    gm_l51 = t687;
                }
                goto ix676;
ie676:;
                {
                    unsigned __int128 t689 = gm_l50;
                    unsigned __int128 t690 = gm_l49;
                    unsigned __int128 r691; bool o692 = __builtin_sub_overflow(t689, t690, &r691);
                    unsigned __int128 t693 = r691;
                    gm_l51 = t693;
                }
ix676:;
            }
            {
                void * t695 = gm_l0;
                gm_t88rcucg1b7uk t696 = gm_l3;
                gm_tg5lql6itufk7 t697 = gm_f_int_info_ulcevdv(t695, GM_CONV(gm_csguankko4lmhd, t696));
                if (!((((uint8_t *)&t697))[8] != 0)) goto ie694;
                gm_ttovhfee8mf28 t698 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t697));
                gm_l52 = t698;
                {
                    uint8_t t700 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                    uint32_t t701 = GM_LD(uint32_t, (((uint8_t *)&gm_l52) + 4));
                    unsigned __int128 t702 = gm_l51;
                    unsigned __int128 t703 = gm_f_num_ext_348kmbu(t700, t701, t702);
                    gm_l51 = t703;
                }
                goto ix694;
ie694:;
ix694:;
            }
            {
                void * t704 = gm_l0;
                gm_b16_8 t705 = gm_l7;
                unsigned __int128 t706 = gm_l51;
                gm_f_put_uint_cddin99(t704, t705, t706);
            }
            goto me3;
mn650:;
ma14:;
            uint8_t t708 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t708 != ((uint8_t)21ULL)) goto mn707;
            gm_t9qkh0rsooc8d t709 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
            gm_l53 = t709;
            void * t710 = gm_l0;
            gm_tl874t2atnlh3 t711 = gm_l1;
            gm_t9qkh0rsooc8d t712 = gm_l53;
            gm_t88rcucg1b7uk t713 = gm_l4;
            gm_t88rcucg1b7uk t714 = gm_l3;
            gm_t88rcucg1b7uk t715 = gm_l5;
            uint64_t t716 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t717 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t716 >= (uintptr_t)(t717)) gm_trap_index(t716, t717, gm_srcl8pn2pjtmj6k, 320, 60);
            gm_b16_8 t718 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t716 * 16));
            gm_b16_8 t719 = GM_LD(gm_b16_8, ((uint8_t *)&t718));
            uint64_t t720 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t721 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t720 >= (uintptr_t)(t721)) gm_trap_index(t720, t721, gm_srcl8pn2pjtmj6k, 320, 66);
            gm_b16_8 t722 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t720 * 16));
            gm_b16_8 t723 = GM_LD(gm_b16_8, ((uint8_t *)&t722));
            gm_b16_8 t724 = gm_l7;
            gm_f_pow_7je2uvi((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t710, t711, t712, t713, t714, t715, t719, t723, t724);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn707:;
ma15:;
            uint8_t t728 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t728 != ((uint8_t)22ULL)) goto pa727;
            goto po726;
pa727:;
            uint8_t t730 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t730 != ((uint8_t)23ULL)) goto pa729;
            goto po726;
pa729:;
            uint8_t t732 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t732 != ((uint8_t)24ULL)) goto pa731;
            goto po726;
pa731:;
            uint8_t t734 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t734 != ((uint8_t)25ULL)) goto pa733;
            goto po726;
pa733:;
            uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t736 != ((uint8_t)26ULL)) goto pa735;
            goto po726;
pa735:;
            goto mn725;
po726:;
            {
                void * t739 = gm_l0;
                gm_t88rcucg1b7uk t740 = gm_l4;
                gm_t53at80pfdus3 t741;
                gm_f_kind_htv6093((void *)&t741, t739, GM_CONV(gm_csguankko4lmhd, t740));
                uint8_t t742;
                uint8_t t745 = GM_LD(uint8_t, (((uint8_t *)&t741) + 0));
                if (t745 != ((uint8_t)3ULL)) goto mn744;
                t742 = ((uint8_t)1);
                goto me743;
mn744:;
                t742 = ((uint8_t)0);
                goto me743;
mn746:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 322, 21);
me743:;
                uint8_t t747 = (uint8_t)!t742;
                if (!t747) goto ie738;
                {
                    void * t749 = gm_l0;
                    gm_tl874t2atnlh3 t750 = gm_l1;
                    gm_b16_8 t751;
                    GM_ST(void *, (uint8_t *)&t751, (void *)(gm_s9h8lp3gkedth));
                    GM_ST(uintptr_t, (uint8_t *)&t751 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t752;
                    gm_f_trap_qh84l9d((void *)&t752, (void *)NULL, cx, NULL, t749, t750, t751);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t752);
                    goto gm_fdone;
                }
                goto ix738;
ie738:;
ix738:;
            }
            {
                void * t753 = gm_l0;
                gm_t88rcucg1b7uk t754 = gm_l4;
                uint64_t t755 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t756 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t755 >= (uintptr_t)(t756)) gm_trap_index(t755, t756, gm_srcl8pn2pjtmj6k, 323, 34);
                gm_b16_8 t757 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t755 * 16));
                gm_b16_8 t758 = GM_LD(gm_b16_8, ((uint8_t *)&t757));
                double t759 = gm_f_get_float_grmhlm4(t753, GM_CONV(gm_csguankko4lmhd, t754), t758);
                gm_l54 = t759;
            }
            {
                double t760 = gm_l54;
                uint64_t t761;
                memset(&t761, 0, sizeof t761); memcpy(&t761, ((uint8_t *)&t760), sizeof t761 < 8 ? sizeof t761 : 8);
                gm_l55 = t761;
            }
            {
                uint64_t t762 = gm_l55;
                unsigned __int128 n763 = (unsigned __int128)((uint64_t)52ULL);
                if (n763 >= 64) gm_trap_shift((uint64_t)n763, "u64", gm_srcl8pn2pjtmj6k, 325, 21);
                uint64_t t764 = (uint64_t)(t762 >> (int)n763);
                uint64_t t765 = (uint64_t)(t764 & ((uint64_t)2047ULL));
                gm_l56 = t765;
            }
            {
                uint8_t t773 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t773) { case ((uint8_t)0ULL): goto ma771; case ((uint8_t)1ULL): goto ma771; case ((uint8_t)2ULL): goto ma771; case ((uint8_t)3ULL): goto ma771; case ((uint8_t)4ULL): goto ma771; case ((uint8_t)5ULL): goto ma771; case ((uint8_t)6ULL): goto ma771; case ((uint8_t)7ULL): goto ma771; case ((uint8_t)8ULL): goto ma771; case ((uint8_t)9ULL): goto ma771; case ((uint8_t)10ULL): goto ma771; case ((uint8_t)11ULL): goto ma771; case ((uint8_t)12ULL): goto ma771; case ((uint8_t)13ULL): goto ma771; case ((uint8_t)14ULL): goto ma771; case ((uint8_t)15ULL): goto ma771; case ((uint8_t)16ULL): goto ma771; case ((uint8_t)17ULL): goto ma771; case ((uint8_t)18ULL): goto ma771; case ((uint8_t)19ULL): goto ma771; case ((uint8_t)20ULL): goto ma771; case ((uint8_t)21ULL): goto ma771; case ((uint8_t)22ULL): goto ma767; case ((uint8_t)23ULL): goto ma768; case ((uint8_t)24ULL): goto ma769; case ((uint8_t)25ULL): goto ma770; case ((uint8_t)26ULL): goto ma771; case ((uint8_t)27ULL): goto ma771; case ((uint8_t)28ULL): goto ma771; case ((uint8_t)29ULL): goto ma771; case ((uint8_t)30ULL): goto ma771; case ((uint8_t)31ULL): goto ma771; case ((uint8_t)32ULL): goto ma771; case ((uint8_t)33ULL): goto ma771; case ((uint8_t)34ULL): goto ma771; case ((uint8_t)35ULL): goto ma771; case ((uint8_t)36ULL): goto ma771; case ((uint8_t)37ULL): goto ma771; case ((uint8_t)38ULL): goto ma771; case ((uint8_t)39ULL): goto ma771; case ((uint8_t)40ULL): goto ma771; case ((uint8_t)41ULL): goto ma771; case ((uint8_t)42ULL): goto ma771; case ((uint8_t)43ULL): goto ma771; case ((uint8_t)44ULL): goto ma771; case ((uint8_t)45ULL): goto ma771; case ((uint8_t)46ULL): goto ma771; case ((uint8_t)47ULL): goto ma771; case ((uint8_t)48ULL): goto ma771; case ((uint8_t)49ULL): goto ma771; case ((uint8_t)50ULL): goto ma771; case ((uint8_t)51ULL): goto ma771; case ((uint8_t)52ULL): goto ma771; case ((uint8_t)53ULL): goto ma771; case ((uint8_t)54ULL): goto ma771; case ((uint8_t)55ULL): goto ma771; case ((uint8_t)56ULL): goto ma771; case ((uint8_t)57ULL): goto ma771; case ((uint8_t)58ULL): goto ma771; case ((uint8_t)59ULL): goto ma771; case ((uint8_t)60ULL): goto ma771; case ((uint8_t)61ULL): goto ma771; case ((uint8_t)62ULL): goto ma771; case ((uint8_t)63ULL): goto ma771; case ((uint8_t)64ULL): goto ma771; case ((uint8_t)65ULL): goto ma771; case ((uint8_t)66ULL): goto ma771; case ((uint8_t)67ULL): goto ma771; case ((uint8_t)68ULL): goto ma771; case ((uint8_t)69ULL): goto ma771; case ((uint8_t)70ULL): goto ma771; case ((uint8_t)71ULL): goto ma771; case ((uint8_t)72ULL): goto ma771; case ((uint8_t)73ULL): goto ma771; case ((uint8_t)74ULL): goto ma771; case ((uint8_t)75ULL): goto ma771; case ((uint8_t)76ULL): goto ma771; case ((uint8_t)77ULL): goto ma771; case ((uint8_t)78ULL): goto ma771; case ((uint8_t)79ULL): goto ma771; case ((uint8_t)80ULL): goto ma771; case ((uint8_t)81ULL): goto ma771; default: goto ma771; }
ma767:;
                uint8_t t775 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t775 != ((uint8_t)22ULL)) goto mn774;
                {
                    uint64_t t777 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t778 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t777 >= (uintptr_t)(t778)) gm_trap_index(t777, t778, gm_srcl8pn2pjtmj6k, 327, 29);
                    uint8_t *t779 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t777 * 1);
                    uint8_t t780;
                    uint64_t t782 = gm_l56;
                    uint8_t t783 = ((t782 == ((uint64_t)2047ULL)));
                    if (!t783) goto ie781;
                    uint64_t t784 = gm_l55;
                    uint64_t t785 = (uint64_t)(t784 & ((uint64_t)4503599627370495ULL));
                    uint8_t t786 = (!(t785 == ((uint64_t)0ULL)));
                    if (!t786) goto ie781;
                    uint8_t t788 = ((uint8_t)1ULL);
                    t780 = t788;
                    goto ix781;
ie781:;
                    uint8_t t790 = ((uint8_t)0ULL);
                    t780 = t790;
ix781:;
                    GM_ST(uint8_t, t779, t780);
                }
                goto me766;
mn774:;
ma768:;
                uint8_t t792 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t792 != ((uint8_t)23ULL)) goto mn791;
                {
                    uint64_t t794 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t795 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t794 >= (uintptr_t)(t795)) gm_trap_index(t794, t795, gm_srcl8pn2pjtmj6k, 328, 29);
                    uint8_t *t796 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t794 * 1);
                    uint8_t t797;
                    uint64_t t799 = gm_l56;
                    uint8_t t800 = ((t799 == ((uint64_t)2047ULL)));
                    if (!t800) goto ie798;
                    uint64_t t801 = gm_l55;
                    uint64_t t802 = (uint64_t)(t801 & ((uint64_t)4503599627370495ULL));
                    uint8_t t803 = ((t802 == ((uint64_t)0ULL)));
                    if (!t803) goto ie798;
                    uint8_t t805 = ((uint8_t)1ULL);
                    t797 = t805;
                    goto ix798;
ie798:;
                    uint8_t t807 = ((uint8_t)0ULL);
                    t797 = t807;
ix798:;
                    GM_ST(uint8_t, t796, t797);
                }
                goto me766;
mn791:;
ma769:;
                uint8_t t809 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t809 != ((uint8_t)24ULL)) goto mn808;
                {
                    uint64_t t811 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t812 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t811 >= (uintptr_t)(t812)) gm_trap_index(t811, t812, gm_srcl8pn2pjtmj6k, 329, 32);
                    uint8_t *t813 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t811 * 1);
                    uint8_t t814;
                    uint64_t t816 = gm_l56;
                    uint8_t t817 = ((t816 == ((uint64_t)2047ULL)));
                    if (!t817) goto ie815;
                    uint8_t t819 = ((uint8_t)0ULL);
                    t814 = t819;
                    goto ix815;
ie815:;
                    uint8_t t821 = ((uint8_t)1ULL);
                    t814 = t821;
ix815:;
                    GM_ST(uint8_t, t813, t814);
                }
                goto me766;
mn808:;
ma770:;
                uint8_t t823 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t823 != ((uint8_t)25ULL)) goto mn822;
                {
                    uint64_t t825 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t826 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t825 >= (uintptr_t)(t826)) gm_trap_index(t825, t826, gm_srcl8pn2pjtmj6k, 330, 31);
                    uint8_t *t827 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t825 * 1);
                    uint8_t t828;
                    uint64_t t830 = gm_l55;
                    unsigned __int128 n831 = (unsigned __int128)((uint64_t)63ULL);
                    if (n831 >= 64) gm_trap_shift((uint64_t)n831, "u64", gm_srcl8pn2pjtmj6k, 330, 44);
                    uint64_t t832 = (uint64_t)(t830 >> (int)n831);
                    uint8_t t833 = ((t832 == ((uint64_t)1ULL)));
                    if (!t833) goto ie829;
                    uint8_t t835 = ((uint8_t)1ULL);
                    t828 = t835;
                    goto ix829;
ie829:;
                    uint8_t t837 = ((uint8_t)0ULL);
                    t828 = t837;
ix829:;
                    GM_ST(uint8_t, t827, t828);
                }
                goto me766;
mn822:;
ma771:;
                void * t839 = gm_l0;
                gm_t88rcucg1b7uk t840 = gm_l4;
                double t841 = gm_l54;
                double t842 = (double)(__builtin_sqrt(t841));
                gm_b16_8 t843 = gm_l7;
                gm_f_put_float_fiad72d(t839, GM_CONV(gm_csguankko4lmhd, t840), t842, t843);
                goto me766;
mn838:;
ma772:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 326, 13);
me766:;
            }
            goto me3;
mn725:;
ma16:;
            uint8_t t845 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t845 != ((uint8_t)27ULL)) goto mn844;
            {
                void * t848 = gm_l0;
                gm_t88rcucg1b7uk t849 = gm_l4;
                gm_t53at80pfdus3 t850;
                gm_f_kind_htv6093((void *)&t850, t848, GM_CONV(gm_csguankko4lmhd, t849));
                uint8_t t851 = GM_LD(uint8_t, (((uint8_t *)&t850) + 0));
                if (t851 != ((uint8_t)3ULL)) goto ie847;
                uint16_t t852 = GM_LD(uint16_t, (((uint8_t *)&t850) + 8));
                gm_l57 = t852;
                {
                    void * t854 = gm_l0;
                    gm_t88rcucg1b7uk t855 = gm_l4;
                    uint64_t t856 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t857 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t856 >= (uintptr_t)(t857)) gm_trap_index(t856, t857, gm_srcl8pn2pjtmj6k, 336, 34);
                    gm_b16_8 t858 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t856 * 16));
                    gm_b16_8 t859 = GM_LD(gm_b16_8, ((uint8_t *)&t858));
                    double t860 = gm_f_get_float_grmhlm4(t854, GM_CONV(gm_csguankko4lmhd, t855), t859);
                    gm_l58 = t860;
                }
                {
                    void * t861 = gm_l0;
                    gm_t88rcucg1b7uk t862 = gm_l4;
                    uint64_t t863 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t864 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t863 >= (uintptr_t)(t864)) gm_trap_index(t863, t864, gm_srcl8pn2pjtmj6k, 337, 34);
                    gm_b16_8 t865 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t863 * 16));
                    gm_b16_8 t866 = GM_LD(gm_b16_8, ((uint8_t *)&t865));
                    double t867 = gm_f_get_float_grmhlm4(t861, GM_CONV(gm_csguankko4lmhd, t862), t866);
                    gm_l59 = t867;
                }
                {
                    void * t868 = gm_l0;
                    gm_t88rcucg1b7uk t869 = gm_l4;
                    uint64_t t870 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t871 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    if (t870 >= (uintptr_t)(t871)) gm_trap_index(t870, t871, gm_srcl8pn2pjtmj6k, 338, 34);
                    gm_b16_8 t872 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t870 * 16));
                    gm_b16_8 t873 = GM_LD(gm_b16_8, ((uint8_t *)&t872));
                    double t874 = gm_f_get_float_grmhlm4(t868, GM_CONV(gm_csguankko4lmhd, t869), t873);
                    gm_l60 = t874;
                }
                {
                    uint16_t t876 = gm_l57;
                    uint8_t t877 = ((t876 == ((uint16_t)32ULL)));
                    if (!t877) goto ie875;
                    {
                        void * t879 = gm_l0;
                        gm_t88rcucg1b7uk t880 = gm_l4;
                        double t881 = gm_l58;
                        float t882 = (float)((double)t881);
                        double t883 = gm_l59;
                        float t884 = (float)((double)t883);
                        double t885 = gm_l60;
                        float t886 = (float)((double)t885);
                        float t887 = (float)(__builtin_fmaf(t882, t884, t886));
                        double t888 = (double)t887;
                        gm_b16_8 t889 = gm_l7;
                        gm_f_put_float_fiad72d(t879, GM_CONV(gm_csguankko4lmhd, t880), t888, t889);
                    }
                    goto ix875;
ie875:;
                    {
                        void * t891 = gm_l0;
                        gm_t88rcucg1b7uk t892 = gm_l4;
                        double t893 = gm_l58;
                        double t894 = gm_l59;
                        double t895 = gm_l60;
                        double t896 = (double)(__builtin_fma(t893, t894, t895));
                        gm_b16_8 t897 = gm_l7;
                        gm_f_put_float_fiad72d(t891, GM_CONV(gm_csguankko4lmhd, t892), t896, t897);
                    }
ix875:;
                }
                goto ix847;
ie847:;
                {
                    void * t899 = gm_l0;
                    gm_tl874t2atnlh3 t900 = gm_l1;
                    gm_b16_8 t901;
                    GM_ST(void *, (uint8_t *)&t901, (void *)(gm_sh01rb1u2jpfs));
                    GM_ST(uintptr_t, (uint8_t *)&t901 + 8, (uintptr_t)(18));
                    gm_t76123gfet73g t902;
                    gm_f_trap_qh84l9d((void *)&t902, (void *)NULL, cx, NULL, t899, t900, t901);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t902);
                    goto gm_fdone;
                }
ix847:;
            }
            goto me3;
mn844:;
ma17:;
            uint8_t t906 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t906 != ((uint8_t)32ULL)) goto pa905;
            goto po904;
pa905:;
            uint8_t t908 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t908 != ((uint8_t)33ULL)) goto pa907;
            goto po904;
pa907:;
            goto mn903;
po904:;
            {
                void * t910 = gm_l0;
                gm_t88rcucg1b7uk t911 = gm_l4;
                gm_tg5lql6itufk7 t912 = gm_f_int_info_ulcevdv(t910, GM_CONV(gm_csguankko4lmhd, t911));
                gm_ttovhfee8mf28 t913;
                if (((((uint8_t *)&t912))[8] != 0)) {
                    gm_ttovhfee8mf28 t914 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t912) + 0));
                    t913 = t914;
                } else {
                    void * t915 = gm_l0;
                    gm_tl874t2atnlh3 t916 = gm_l1;
                    gm_b16_8 t917;
                    GM_ST(void *, (uint8_t *)&t917, (void *)(gm_s26c80h2pm6a7));
                    GM_ST(uintptr_t, (uint8_t *)&t917 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t918;
                    gm_f_trap_qh84l9d((void *)&t918, (void *)NULL, cx, NULL, t915, t916, t917);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t918);
                    goto gm_fdone;
                }
                gm_l61 = t913;
            }
            {
                void * t919 = gm_l0;
                gm_t88rcucg1b7uk t920 = gm_l5;
                gm_tg5lql6itufk7 t921 = gm_f_int_info_ulcevdv(t919, GM_CONV(gm_csguankko4lmhd, t920));
                gm_ttovhfee8mf28 t922;
                if (((((uint8_t *)&t921))[8] != 0)) {
                    gm_ttovhfee8mf28 t923 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t921) + 0));
                    t922 = t923;
                } else {
                    void * t924 = gm_l0;
                    gm_tl874t2atnlh3 t925 = gm_l1;
                    gm_b16_8 t926;
                    GM_ST(void *, (uint8_t *)&t926, (void *)(gm_sti4tkgslrkgo));
                    GM_ST(uintptr_t, (uint8_t *)&t926 + 8, (uintptr_t)(25));
                    gm_t76123gfet73g t927;
                    gm_f_trap_qh84l9d((void *)&t927, (void *)NULL, cx, NULL, t924, t925, t926);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t927);
                    goto gm_fdone;
                }
                gm_l62 = t922;
            }
            {
                uint32_t t928 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 4));
                gm_l63 = t928;
            }
            {
                void * t929 = gm_l0;
                uint64_t t930 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t931 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t930 >= (uintptr_t)(t931)) gm_trap_index(t930, t931, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t932 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t930 * 16));
                uint64_t t933 = GM_LD(uintptr_t, ((uint8_t *)&t932) + 8);
                void * t934 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t932)));
                uint64_t t935 = (uintptr_t)((uint64_t)0ULL);
                void * t936 = gm_l0;
                gm_t88rcucg1b7uk t937 = gm_l4;
                uint64_t t938 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t936, t937);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t939 = gm_f_us_cuv01fp(t938);
                uint64_t t940 = (uintptr_t)t939;
                if (t935 > t940 || t940 > (uintptr_t)t933) gm_trap_slice(t935, t940, t933, gm_srcl8pn2pjtmj6k, 349, 29);
                gm_b16_8 t941;
                GM_ST(void *, (uint8_t *)&t941, (void *)(((uint8_t *)t934 + t935 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t941 + 8, (uintptr_t)(t940 - t935));
                gm_b16_8 t942 = GM_LD(gm_b16_8, ((uint8_t *)&t941));
                unsigned __int128 t943 = gm_f_get_uint_bq8c4b7(t929, t942);
                uint32_t t944 = gm_l63;
                unsigned __int128 t945 = gm_f_num_mask_do3naah(t944);
                unsigned __int128 t946 = (unsigned __int128)(t943 & t945);
                gm_l64 = t946;
            }
            {
                void * t947 = gm_l0;
                gm_t88rcucg1b7uk t948 = gm_l5;
                uint64_t t949 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t950 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t949 >= (uintptr_t)(t950)) gm_trap_index(t949, t950, gm_srcl8pn2pjtmj6k, 350, 35);
                gm_b16_8 t951 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t949 * 16));
                gm_b16_8 t952 = GM_LD(gm_b16_8, ((uint8_t *)&t951));
                unsigned __int128 t953 = gm_f_get_int_pg7p4gc(t947, GM_CONV(gm_csguankko4lmhd, t948), t952);
                gm_l65 = t953;
            }
            {
                gm_l66 = ((uint32_t)0ULL);
            }
            {
                uint8_t t955 = 0;
                uint8_t t956 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                if (t956) {
                    unsigned __int128 t957 = gm_l65;
                    unsigned __int128 n958 = (unsigned __int128)((uint64_t)127ULL);
                    if (n958 >= 128) gm_trap_shift((uint64_t)n958, "u128", gm_srcl8pn2pjtmj6k, 352, 39);
                    unsigned __int128 t959 = (unsigned __int128)(t957 >> (int)n958);
                    uint8_t t960 = ((t959 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                    t955 = t960;
                } else t955 = 0;
                uint8_t t961 = (uint8_t)!t955;
                if (!t961) goto ie954;
                {
                    unsigned __int128 t963 = gm_l65;
                    uint32_t t964 = gm_l63;
                    unsigned __int128 t965 = (unsigned __int128)t964;
                    if (t965 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 352, 65);
                    unsigned __int128 t966 = (unsigned __int128)(t963 % t965);
                    uint32_t t967 = (uint32_t)t966;
                    gm_l66 = t967;
                }
                goto ix954;
ie954:;
ix954:;
            }
            {
                uint8_t t969 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)33ULL)));
                if (!t969) goto ie968;
                {
                    uint32_t t971 = gm_l63;
                    uint32_t t972 = gm_l66;
                    uint32_t r973; bool o974 = __builtin_sub_overflow(t971, t972, &r973);
                    if (o974) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 353, 35);
                    uint32_t t975 = r973;
                    uint32_t t976 = gm_l63;
                    if (t976 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 353, 34);
                    uint32_t t977 = (uint32_t)(t975 % t976);
                    gm_l66 = t977;
                }
                goto ix968;
ie968:;
ix968:;
            }
            {
                unsigned __int128 t978;
                uint32_t t980 = gm_l66;
                uint8_t t981 = ((t980 == ((uint32_t)0ULL)));
                if (!t981) goto ie979;
                unsigned __int128 t983 = gm_l64;
                unsigned __int128 t984 = t983;
                t978 = t984;
                goto ix979;
ie979:;
                unsigned __int128 t986 = gm_l64;
                uint32_t t987 = gm_l66;
                unsigned __int128 t988 = (unsigned __int128)t987;
                unsigned __int128 n989 = (unsigned __int128)t988;
                if (n989 >= 128) gm_trap_shift((uint64_t)n989, "u128", gm_srcl8pn2pjtmj6k, 354, 43);
                unsigned __int128 r990 = (unsigned __int128)((unsigned __int128)t986 << (int)n989);
                bool o991 = (unsigned __int128)(r990 >> (int)n989) != t986;
                unsigned __int128 t992 = r990;
                unsigned __int128 t993 = gm_l64;
                uint32_t t994 = gm_l63;
                uint32_t t995 = gm_l66;
                uint32_t r996; bool o997 = __builtin_sub_overflow(t994, t995, &r996);
                if (o997) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 354, 71);
                uint32_t t998 = r996;
                unsigned __int128 t999 = (unsigned __int128)t998;
                unsigned __int128 n1000 = (unsigned __int128)t999;
                if (n1000 >= 128) gm_trap_shift((uint64_t)n1000, "u128", gm_srcl8pn2pjtmj6k, 354, 61);
                unsigned __int128 t1001 = (unsigned __int128)(t993 >> (int)n1000);
                unsigned __int128 t1002 = (unsigned __int128)(t992 | t1001);
                uint32_t t1003 = gm_l63;
                unsigned __int128 t1004 = gm_f_num_mask_do3naah(t1003);
                unsigned __int128 t1005 = (unsigned __int128)(t1002 & t1004);
                unsigned __int128 t1006 = t1005;
                t978 = t1006;
ix979:;
                gm_l67 = t978;
            }
            {
                void * t1007 = gm_l0;
                gm_b16_8 t1008 = gm_l7;
                uint8_t t1009 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                uint32_t t1010 = gm_l63;
                unsigned __int128 t1011 = gm_l67;
                unsigned __int128 t1012 = gm_f_num_ext_348kmbu(t1009, t1010, t1011);
                gm_f_put_uint_cddin99(t1007, t1008, t1012);
            }
            goto me3;
mn903:;
ma18:;
            uint8_t t1016 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1016 != ((uint8_t)28ULL)) goto pa1015;
            goto po1014;
pa1015:;
            uint8_t t1018 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1018 != ((uint8_t)29ULL)) goto pa1017;
            goto po1014;
pa1017:;
            uint8_t t1020 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1020 != ((uint8_t)30ULL)) goto pa1019;
            goto po1014;
pa1019:;
            uint8_t t1022 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1022 != ((uint8_t)31ULL)) goto pa1021;
            goto po1014;
pa1021:;
            uint8_t t1024 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1024 != ((uint8_t)34ULL)) goto pa1023;
            goto po1014;
pa1023:;
            goto mn1013;
po1014:;
            {
                void * t1026 = gm_l0;
                gm_t88rcucg1b7uk t1027 = gm_l4;
                gm_tg5lql6itufk7 t1028 = gm_f_int_info_ulcevdv(t1026, GM_CONV(gm_csguankko4lmhd, t1027));
                gm_ttovhfee8mf28 t1029;
                if (((((uint8_t *)&t1028))[8] != 0)) {
                    gm_ttovhfee8mf28 t1030 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1028) + 0));
                    t1029 = t1030;
                } else {
                    void * t1031 = gm_l0;
                    gm_tl874t2atnlh3 t1032 = gm_l1;
                    gm_b16_8 t1033;
                    GM_ST(void *, (uint8_t *)&t1033, (void *)(gm_spqf88ftq0gqk));
                    GM_ST(uintptr_t, (uint8_t *)&t1033 + 8, (uintptr_t)(28));
                    gm_t76123gfet73g t1034;
                    gm_f_trap_qh84l9d((void *)&t1034, (void *)NULL, cx, NULL, t1031, t1032, t1033);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1034);
                    goto gm_fdone;
                }
                gm_l68 = t1029;
            }
            {
                uint32_t t1035 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 4));
                gm_l69 = t1035;
            }
            {
                void * t1036 = gm_l0;
                uint64_t t1037 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1038 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1037 >= (uintptr_t)(t1038)) gm_trap_index(t1037, t1038, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1039 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1037 * 16));
                uint64_t t1040 = GM_LD(uintptr_t, ((uint8_t *)&t1039) + 8);
                void * t1041 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1039)));
                uint64_t t1042 = (uintptr_t)((uint64_t)0ULL);
                void * t1043 = gm_l0;
                gm_t88rcucg1b7uk t1044 = gm_l4;
                uint64_t t1045 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t1043, t1044);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t1046 = gm_f_us_cuv01fp(t1045);
                uint64_t t1047 = (uintptr_t)t1046;
                if (t1042 > t1047 || t1047 > (uintptr_t)t1040) gm_trap_slice(t1042, t1047, t1040, gm_srcl8pn2pjtmj6k, 360, 29);
                gm_b16_8 t1048;
                GM_ST(void *, (uint8_t *)&t1048, (void *)(((uint8_t *)t1041 + t1042 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t1048 + 8, (uintptr_t)(t1047 - t1042));
                gm_b16_8 t1049 = GM_LD(gm_b16_8, ((uint8_t *)&t1048));
                unsigned __int128 t1050 = gm_f_get_uint_bq8c4b7(t1036, t1049);
                uint32_t t1051 = gm_l69;
                unsigned __int128 t1052 = gm_f_num_mask_do3naah(t1051);
                unsigned __int128 t1053 = (unsigned __int128)(t1050 & t1052);
                gm_l70 = t1053;
            }
            {
                gm_l71 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t1061 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                switch (t1061) { case ((uint8_t)0ULL): goto ma1059; case ((uint8_t)1ULL): goto ma1059; case ((uint8_t)2ULL): goto ma1059; case ((uint8_t)3ULL): goto ma1059; case ((uint8_t)4ULL): goto ma1059; case ((uint8_t)5ULL): goto ma1059; case ((uint8_t)6ULL): goto ma1059; case ((uint8_t)7ULL): goto ma1059; case ((uint8_t)8ULL): goto ma1059; case ((uint8_t)9ULL): goto ma1059; case ((uint8_t)10ULL): goto ma1059; case ((uint8_t)11ULL): goto ma1059; case ((uint8_t)12ULL): goto ma1059; case ((uint8_t)13ULL): goto ma1059; case ((uint8_t)14ULL): goto ma1059; case ((uint8_t)15ULL): goto ma1059; case ((uint8_t)16ULL): goto ma1059; case ((uint8_t)17ULL): goto ma1059; case ((uint8_t)18ULL): goto ma1059; case ((uint8_t)19ULL): goto ma1059; case ((uint8_t)20ULL): goto ma1059; case ((uint8_t)21ULL): goto ma1059; case ((uint8_t)22ULL): goto ma1059; case ((uint8_t)23ULL): goto ma1059; case ((uint8_t)24ULL): goto ma1059; case ((uint8_t)25ULL): goto ma1059; case ((uint8_t)26ULL): goto ma1059; case ((uint8_t)27ULL): goto ma1059; case ((uint8_t)28ULL): goto ma1055; case ((uint8_t)29ULL): goto ma1056; case ((uint8_t)30ULL): goto ma1057; case ((uint8_t)31ULL): goto ma1058; case ((uint8_t)32ULL): goto ma1059; case ((uint8_t)33ULL): goto ma1059; case ((uint8_t)34ULL): goto ma1059; case ((uint8_t)35ULL): goto ma1059; case ((uint8_t)36ULL): goto ma1059; case ((uint8_t)37ULL): goto ma1059; case ((uint8_t)38ULL): goto ma1059; case ((uint8_t)39ULL): goto ma1059; case ((uint8_t)40ULL): goto ma1059; case ((uint8_t)41ULL): goto ma1059; case ((uint8_t)42ULL): goto ma1059; case ((uint8_t)43ULL): goto ma1059; case ((uint8_t)44ULL): goto ma1059; case ((uint8_t)45ULL): goto ma1059; case ((uint8_t)46ULL): goto ma1059; case ((uint8_t)47ULL): goto ma1059; case ((uint8_t)48ULL): goto ma1059; case ((uint8_t)49ULL): goto ma1059; case ((uint8_t)50ULL): goto ma1059; case ((uint8_t)51ULL): goto ma1059; case ((uint8_t)52ULL): goto ma1059; case ((uint8_t)53ULL): goto ma1059; case ((uint8_t)54ULL): goto ma1059; case ((uint8_t)55ULL): goto ma1059; case ((uint8_t)56ULL): goto ma1059; case ((uint8_t)57ULL): goto ma1059; case ((uint8_t)58ULL): goto ma1059; case ((uint8_t)59ULL): goto ma1059; case ((uint8_t)60ULL): goto ma1059; case ((uint8_t)61ULL): goto ma1059; case ((uint8_t)62ULL): goto ma1059; case ((uint8_t)63ULL): goto ma1059; case ((uint8_t)64ULL): goto ma1059; case ((uint8_t)65ULL): goto ma1059; case ((uint8_t)66ULL): goto ma1059; case ((uint8_t)67ULL): goto ma1059; case ((uint8_t)68ULL): goto ma1059; case ((uint8_t)69ULL): goto ma1059; case ((uint8_t)70ULL): goto ma1059; case ((uint8_t)71ULL): goto ma1059; case ((uint8_t)72ULL): goto ma1059; case ((uint8_t)73ULL): goto ma1059; case ((uint8_t)74ULL): goto ma1059; case ((uint8_t)75ULL): goto ma1059; case ((uint8_t)76ULL): goto ma1059; case ((uint8_t)77ULL): goto ma1059; case ((uint8_t)78ULL): goto ma1059; case ((uint8_t)79ULL): goto ma1059; case ((uint8_t)80ULL): goto ma1059; case ((uint8_t)81ULL): goto ma1059; default: goto ma1059; }
ma1055:;
                uint8_t t1063 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1063 != ((uint8_t)28ULL)) goto mn1062;
                {
                    void * t1065 = gm_l0;
                    gm_b16_8 t1066 = gm_l7;
                    unsigned __int128 t1067 = gm_l70;
                    uint32_t t1068 = (uint32_t)gm_clz((unsigned __int128)(unsigned __int128)(t1067), 128);
                    uint32_t t1069 = gm_l69;
                    uint32_t r1070; bool o1071 = __builtin_sub_overflow(((uint32_t)128ULL), t1069, &r1070);
                    if (o1071) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 53);
                    uint32_t t1072 = r1070;
                    uint32_t r1073; bool o1074 = __builtin_sub_overflow(t1068, t1072, &r1073);
                    if (o1074) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 42);
                    uint32_t t1075 = r1073;
                    unsigned __int128 t1076 = (unsigned __int128)t1075;
                    gm_f_put_uint_cddin99(t1065, t1066, t1076);
                }
                {
                    gm_b0_1 t1077;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1077);
                    goto gm_fdone;
                }
                goto me1054;
mn1062:;
ma1056:;
                uint8_t t1079 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1079 != ((uint8_t)29ULL)) goto mn1078;
                {
                    void * t1081 = gm_l0;
                    gm_b16_8 t1082 = gm_l7;
                    unsigned __int128 t1083;
                    unsigned __int128 t1085 = gm_l70;
                    uint8_t t1086 = ((t1085 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                    if (!t1086) goto ie1084;
                    uint32_t t1088 = gm_l69;
                    unsigned __int128 t1089 = (unsigned __int128)t1088;
                    unsigned __int128 t1090 = t1089;
                    t1083 = t1090;
                    goto ix1084;
ie1084:;
                    unsigned __int128 t1092 = gm_l70;
                    uint32_t t1093 = (uint32_t)gm_ctz((unsigned __int128)(unsigned __int128)(t1092), 128);
                    unsigned __int128 t1094 = (unsigned __int128)t1093;
                    unsigned __int128 t1095 = t1094;
                    t1083 = t1095;
ix1084:;
                    gm_f_put_uint_cddin99(t1081, t1082, t1083);
                }
                {
                    gm_b0_1 t1096;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1096);
                    goto gm_fdone;
                }
                goto me1054;
mn1078:;
ma1057:;
                uint8_t t1098 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1098 != ((uint8_t)30ULL)) goto mn1097;
                {
                    void * t1100 = gm_l0;
                    gm_b16_8 t1101 = gm_l7;
                    unsigned __int128 t1102 = gm_l70;
                    uint32_t t1103 = (uint32_t)gm_popcount((unsigned __int128)(unsigned __int128)(t1102));
                    unsigned __int128 t1104 = (unsigned __int128)t1103;
                    gm_f_put_uint_cddin99(t1100, t1101, t1104);
                }
                {
                    gm_b0_1 t1105;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1105);
                    goto gm_fdone;
                }
                goto me1054;
mn1097:;
ma1058:;
                uint8_t t1107 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                if (t1107 != ((uint8_t)31ULL)) goto mn1106;
                {
                    uint32_t t1110 = gm_l69;
                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 376, 24);
                    uint32_t t1111 = (uint32_t)(t1110 % ((uint32_t)8ULL));
                    uint8_t t1112 = (!(t1111 == ((uint32_t)0ULL)));
                    if (!t1112) goto ie1109;
                    {
                        void * t1114 = gm_l0;
                        gm_tl874t2atnlh3 t1115 = gm_l1;
                        uint32_t t1116 = gm_l69;
                        gm_t88rcucg1b7uk t1117;
                        GM_ST(uint32_t, ((uint8_t *)&t1117 + 0), t1116);
                        gm_tabb710d9mq2e t1118;
                        gm_f_format_ven7kt5((void *)&t1118, (void *)NULL, cx, NULL, t1117);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t1118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1118) + 8, gm_srcl8pn2pjtmj6k, 376, 62);
                        gm_b16_8 t1119 = GM_LD(gm_b16_8, (((uint8_t *)&t1118) + 8));
                        gm_b16_8 t1120 = GM_LD(gm_b16_8, ((uint8_t *)&t1119));
                        gm_t76123gfet73g t1121;
                        gm_f_trap_qh84l9d((void *)&t1121, (void *)NULL, cx, NULL, t1114, t1115, t1120);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1121);
                        goto gm_fdone;
                    }
                    goto ix1109;
ie1109:;
ix1109:;
                }
                {
                    {
                        gm_l73 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1123 = gm_l69;
                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 33);
                        uint32_t t1124 = (uint32_t)(t1123 / ((uint32_t)8ULL));
                        gm_l72 = t1124;
                    }
                    {
                        for (;;) {
                            uint32_t t1126 = gm_l73;
                            uint32_t t1127 = gm_l72;
                            uint8_t t1128 = (t1126 < t1127);
                            if (!t1128) goto lb1125;
                            {
                                uint32_t t1131 = gm_l73;
                                uint32_t t1134 = GM_LD(uint32_t, ((uint8_t *)&t1131));
                                gm_l74 = t1134;
                                {
                                    unsigned __int128 t1136 = gm_l71;
                                    unsigned __int128 t1137 = gm_l70;
                                    uint32_t t1138 = gm_l74;
                                    uint32_t r1139; bool o1140 = __builtin_mul_overflow(t1138, ((uint32_t)8ULL), &r1139);
                                    if (o1140) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 58);
                                    uint32_t t1141 = r1139;
                                    unsigned __int128 t1142 = (unsigned __int128)t1141;
                                    unsigned __int128 n1143 = (unsigned __int128)t1142;
                                    if (n1143 >= 128) gm_trap_shift((uint64_t)n1143, "u128", gm_srcl8pn2pjtmj6k, 377, 48);
                                    unsigned __int128 t1144 = (unsigned __int128)(t1137 >> (int)n1143);
                                    unsigned __int128 t1145 = (unsigned __int128)(t1144 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 255u)));
                                    uint32_t t1146 = gm_l69;
                                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1147 = (uint32_t)(t1146 / ((uint32_t)8ULL));
                                    uint32_t r1148; bool o1149 = __builtin_sub_overflow(t1147, ((uint32_t)1ULL), &r1148);
                                    if (o1149) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1150 = r1148;
                                    uint32_t t1151 = gm_l74;
                                    uint32_t r1152; bool o1153 = __builtin_sub_overflow(t1150, t1151, &r1152);
                                    if (o1153) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                    uint32_t t1154 = r1152;
                                    uint32_t r1155; bool o1156 = __builtin_mul_overflow(t1154, ((uint32_t)8ULL), &r1155);
                                    if (o1156) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 82);
                                    uint32_t t1157 = r1155;
                                    unsigned __int128 t1158 = (unsigned __int128)t1157;
                                    unsigned __int128 n1159 = (unsigned __int128)t1158;
                                    if (n1159 >= 128) gm_trap_shift((uint64_t)n1159, "u128", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 r1160 = (unsigned __int128)((unsigned __int128)t1145 << (int)n1159);
                                    bool o1161 = (unsigned __int128)(r1160 >> (int)n1159) != t1145;
                                    if (o1161) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 377, 46);
                                    unsigned __int128 t1162 = r1160;
                                    unsigned __int128 t1163 = (unsigned __int128)(t1136 | t1162);
                                    gm_l71 = t1163;
                                }
                                goto me1132;
mn1133:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 377, 21);
me1132:;
                            }
lc1125:;
                            {
                                uint32_t t1165 = gm_l73;
                                uint32_t r1166; bool o1167 = __builtin_add_overflow(t1165, ((uint32_t)1ULL), &r1166);
                                uint32_t t1168 = r1166;
                                gm_l73 = t1168;
                            }
                        }
lb1125:;
                    }
                }
                goto me1054;
mn1106:;
ma1059:;
                {
                    {
                        gm_l76 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1172 = gm_l69;
                        gm_l75 = t1172;
                    }
                    {
                        for (;;) {
                            uint32_t t1174 = gm_l76;
                            uint32_t t1175 = gm_l75;
                            uint8_t t1176 = (t1174 < t1175);
                            if (!t1176) goto lb1173;
                            {
                                uint32_t t1179 = gm_l76;
                                uint32_t t1182 = GM_LD(uint32_t, ((uint8_t *)&t1179));
                                gm_l77 = t1182;
                                {
                                    unsigned __int128 t1185 = gm_l70;
                                    uint32_t t1186 = gm_l77;
                                    unsigned __int128 t1187 = (unsigned __int128)t1186;
                                    unsigned __int128 n1188 = (unsigned __int128)t1187;
                                    if (n1188 >= 128) gm_trap_shift((uint64_t)n1188, "u128", gm_srcl8pn2pjtmj6k, 381, 29);
                                    unsigned __int128 t1189 = (unsigned __int128)(t1185 >> (int)n1188);
                                    unsigned __int128 t1190 = (unsigned __int128)(t1189 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
                                    uint8_t t1191 = ((t1190 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
                                    if (!t1191) goto ie1184;
                                    {
                                        unsigned __int128 t1193 = gm_l71;
                                        uint32_t t1194 = gm_l69;
                                        uint32_t r1195; bool o1196 = __builtin_sub_overflow(t1194, ((uint32_t)1ULL), &r1195);
                                        if (o1196) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1197 = r1195;
                                        uint32_t t1198 = gm_l77;
                                        uint32_t r1199; bool o1200 = __builtin_sub_overflow(t1197, t1198, &r1199);
                                        if (o1200) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                        uint32_t t1201 = r1199;
                                        unsigned __int128 t1202 = (unsigned __int128)t1201;
                                        unsigned __int128 n1203 = (unsigned __int128)t1202;
                                        if (n1203 >= 128) gm_trap_shift((uint64_t)n1203, "u128", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 r1204 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1203);
                                        bool o1205 = (unsigned __int128)(r1204 >> (int)n1203) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                        if (o1205) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 381, 59);
                                        unsigned __int128 t1206 = r1204;
                                        unsigned __int128 t1207 = (unsigned __int128)(t1193 | t1206);
                                        gm_l71 = t1207;
                                    }
                                    goto ix1184;
ie1184:;
ix1184:;
                                }
                                goto me1180;
mn1181:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 380, 21);
me1180:;
                            }
lc1173:;
                            {
                                uint32_t t1209 = gm_l76;
                                uint32_t r1210; bool o1211 = __builtin_add_overflow(t1209, ((uint32_t)1ULL), &r1210);
                                uint32_t t1212 = r1210;
                                gm_l76 = t1212;
                            }
                        }
lb1173:;
                    }
                }
                goto me1054;
mn1169:;
ma1060:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 362, 13);
me1054:;
            }
            {
                void * t1213 = gm_l0;
                gm_b16_8 t1214 = gm_l7;
                uint8_t t1215 = GM_LD(uint8_t, (((uint8_t *)&gm_l68) + 0));
                uint32_t t1216 = gm_l69;
                unsigned __int128 t1217 = gm_l71;
                unsigned __int128 t1218 = gm_f_num_ext_348kmbu(t1215, t1216, t1217);
                gm_f_put_uint_cddin99(t1213, t1214, t1218);
            }
            goto me3;
mn1013:;
ma19:;
            uint8_t t1220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1220 != ((uint8_t)62ULL)) goto mn1219;
            {
                void * t1222 = gm_l0;
                gm_t88rcucg1b7uk t1223 = gm_l5;
                gm_tg5lql6itufk7 t1224 = gm_f_int_info_ulcevdv(t1222, GM_CONV(gm_csguankko4lmhd, t1223));
                gm_ttovhfee8mf28 t1225;
                if (((((uint8_t *)&t1224))[8] != 0)) {
                    gm_ttovhfee8mf28 t1226 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t1224) + 0));
                    t1225 = t1226;
                } else {
                    void * t1227 = gm_l0;
                    gm_tl874t2atnlh3 t1228 = gm_l1;
                    gm_b16_8 t1229;
                    GM_ST(void *, (uint8_t *)&t1229, (void *)(gm_sartclcojin4f));
                    GM_ST(uintptr_t, (uint8_t *)&t1229 + 8, (uintptr_t)(51));
                    gm_t76123gfet73g t1230;
                    gm_f_trap_qh84l9d((void *)&t1230, (void *)NULL, cx, NULL, t1227, t1228, t1229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1230);
                    goto gm_fdone;
                }
                gm_l78 = t1225;
            }
            {
                uint8_t t1231 = GM_LD(uint8_t, (((uint8_t *)&gm_l78) + 0));
                void * t1232 = gm_l0;
                gm_t88rcucg1b7uk t1233 = gm_l5;
                uint64_t t1234 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1235 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1234 >= (uintptr_t)(t1235)) gm_trap_index(t1234, t1235, gm_srcl8pn2pjtmj6k, 389, 56);
                gm_b16_8 t1236 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1234 * 16));
                gm_b16_8 t1237 = GM_LD(gm_b16_8, ((uint8_t *)&t1236));
                unsigned __int128 t1238 = gm_f_get_int_pg7p4gc(t1232, GM_CONV(gm_csguankko4lmhd, t1233), t1237);
                gm_f_num_val_i3upeur((void *)&gm_l79, t1231, t1238);
            }
            {
                unsigned __int128 t1239 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l79) + 16));
                uint64_t t1240 = (uint64_t)t1239;
                uint64_t t1241 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1240);
                gm_l80 = t1241;
            }
            {
                void * t1242 = gm_l0;
                gm_tl874t2atnlh3 t1243 = gm_l1;
                gm_t88rcucg1b7uk t1244 = gm_l4;
                uint64_t t1245 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1246 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                if (t1245 >= (uintptr_t)(t1246)) gm_trap_index(t1245, t1246, gm_srcl8pn2pjtmj6k, 391, 38);
                gm_b16_8 t1247 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t1245 * 16));
                void * t1248 = (void *)((uint8_t *)&gm_l80);
                gm_tcfa6l7reoosn t1249;
                gm_f_hash_val_nr2e99r((void *)&t1249, (void *)NULL, cx, NULL, t1242, t1243, t1244, t1247, t1248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t1249))[0] != 0)) {
                    gm_tcfa6l7reoosn t1250;
                    GM_ST(uint8_t, (uint8_t *)&t1250 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1250 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1250 + 4, 0);
                    ((uint8_t *)&t1250)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t1250 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t1249) + 8)));
                    F.ret = t1250;
                    goto gm_fret;
                }
                gm_b0_1 t1251 = GM_LD(gm_b0_1, (((uint8_t *)&t1249) + 8));
            }
            {
                void * t1252 = gm_l0;
                gm_b16_8 t1253 = gm_l7;
                uint64_t t1254 = gm_l80;
                unsigned __int128 t1255 = (unsigned __int128)t1254;
                gm_f_put_uint_cddin99(t1252, t1253, t1255);
            }
            goto me3;
mn1219:;
ma20:;
            uint8_t t1259 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1259 != ((uint8_t)40ULL)) goto pa1258;
            goto po1257;
pa1258:;
            uint8_t t1261 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1261 != ((uint8_t)41ULL)) goto pa1260;
            goto po1257;
pa1260:;
            uint8_t t1263 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1263 != ((uint8_t)42ULL)) goto pa1262;
            goto po1257;
pa1262:;
            uint8_t t1265 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1265 != ((uint8_t)43ULL)) goto pa1264;
            goto po1257;
pa1264:;
            uint8_t t1267 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1267 != ((uint8_t)44ULL)) goto pa1266;
            goto po1257;
pa1266:;
            uint8_t t1269 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1269 != ((uint8_t)45ULL)) goto pa1268;
            goto po1257;
pa1268:;
            uint8_t t1271 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1271 != ((uint8_t)46ULL)) goto pa1270;
            goto po1257;
pa1270:;
            uint8_t t1273 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1273 != ((uint8_t)47ULL)) goto pa1272;
            goto po1257;
pa1272:;
            uint8_t t1275 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1275 != ((uint8_t)48ULL)) goto pa1274;
            goto po1257;
pa1274:;
            uint8_t t1277 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1277 != ((uint8_t)49ULL)) goto pa1276;
            goto po1257;
pa1276:;
            uint8_t t1279 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1279 != ((uint8_t)50ULL)) goto pa1278;
            goto po1257;
pa1278:;
            uint8_t t1281 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1281 != ((uint8_t)51ULL)) goto pa1280;
            goto po1257;
pa1280:;
            uint8_t t1283 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1283 != ((uint8_t)52ULL)) goto pa1282;
            goto po1257;
pa1282:;
            uint8_t t1285 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1285 != ((uint8_t)53ULL)) goto pa1284;
            goto po1257;
pa1284:;
            uint8_t t1287 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1287 != ((uint8_t)54ULL)) goto pa1286;
            goto po1257;
pa1286:;
            uint8_t t1289 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1289 != ((uint8_t)55ULL)) goto pa1288;
            goto po1257;
pa1288:;
            uint8_t t1291 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1291 != ((uint8_t)56ULL)) goto pa1290;
            goto po1257;
pa1290:;
            uint8_t t1293 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t1293 != ((uint8_t)57ULL)) goto pa1292;
            goto po1257;
pa1292:;
            goto mn1256;
po1257:;
            void * t1294 = gm_l0;
            gm_tl874t2atnlh3 t1295 = gm_l1;
            gm_tvdg94bk7aa22 t1296 = gm_l2;
            gm_t88rcucg1b7uk t1297 = gm_l3;
            gm_t88rcucg1b7uk t1298 = gm_l4;
            gm_t88rcucg1b7uk t1299 = gm_l5;
            gm_b16_8 t1300 = gm_l6;
            gm_b16_8 t1301 = gm_l7;
            gm_f_vector_builtin_3fn933h((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t1294, t1295, t1296, t1297, t1298, t1299, t1300, t1301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me3;
mn1256:;
ma21:;
            void * t1303 = gm_l0;
            gm_tl874t2atnlh3 t1304 = gm_l1;
            gm_tvdg94bk7aa22 t1305 = gm_l2;
            gm_th9nl3oso747e t1306;
            GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1306 + 0), t1305);
            gm_tabb710d9mq2e t1307;
            gm_f_format_985clkf((void *)&t1307, (void *)NULL, cx, NULL, t1306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t1307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1307) + 8, gm_srcl8pn2pjtmj6k, 395, 42);
            gm_b16_8 t1308 = GM_LD(gm_b16_8, (((uint8_t *)&t1307) + 8));
            gm_b16_8 t1309 = GM_LD(gm_b16_8, ((uint8_t *)&t1308));
            gm_t76123gfet73g t1310;
            gm_f_unsupported_70vvk1e((void *)&t1310, (void *)NULL, cx, NULL, t1303, t1304, t1309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1310);
            goto gm_fdone;
            goto me3;
mn1302:;
ma22:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 191, 5);
me3:;
        }
    }
    gm_b0_1 t1311;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t1311);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_array_ptr_to_slice_7dg3m1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ptr_ty */
    gm_b16_8 gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    uint64_t gm_l4; /* len */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 286u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)8ULL)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l5 = t7;
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l5;
        gm_t53at80pfdus3 t10;
        gm_f_kind_htv6093((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
        if (t11 != ((uint8_t)11ULL)) goto ie2;
        uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)&t10) + 8) + 0));
        gm_l6 = t12;
        {
            uint64_t t14 = gm_l6;
            gm_l4 = t14;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_b16_8 t16 = gm_l3;
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l2;
        uint64_t t19 = gm_f_ptr_of_b77b4c9(t17, t18);
        uint64_t t20 = gm_l4;
        gm_f_put_slice_ko14loi(t15, t16, t19, t20);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_coerce_bytes_9kf3u24 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* from */
    gm_t88rcucg1b7uk gm_l4; /* to */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_ttovhfee8mf28 gm_l7; /* info */
    uint32_t gm_l8; /* variant */
    gm_t88rcucg1b7uk gm_l9; /* derr */
    gm_b16_8 gm_l10; /* lifted */
    gm_t88rcucg1b7uk gm_l11; /* src_in */
    gm_t88rcucg1b7uk gm_l12; /* dst_in */
    gm_b16_8 gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    gm_b16_8 gm_l15; /* sl */
    GM_STACK_CHECK(&F, 1882u);
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
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma9; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma12; default: goto ma13; }
ma4:;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t16 != ((uint8_t)0ULL)) goto mn15;
            {
                void * t19 = gm_l0;
                gm_t88rcucg1b7uk t20 = gm_l4;
                gm_tg5lql6itufk7 t21 = gm_f_int_info_ulcevdv(t19, GM_CONV(gm_csguankko4lmhd, t20));
                if (!((((uint8_t *)&t21))[8] != 0)) goto ie18;
                gm_ttovhfee8mf28 t22 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t21));
                gm_l7 = t22;
                {
                    void * t24 = gm_l0;
                    gm_b16_8 t25 = gm_l6;
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                    void * t28 = gm_l0;
                    gm_t88rcucg1b7uk t29 = gm_l3;
                    gm_b16_8 t30 = gm_l5;
                    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                    unsigned __int128 t32 = gm_f_get_int_pg7p4gc(t28, GM_CONV(gm_csguankko4lmhd, t29), t31);
                    unsigned __int128 t33 = gm_f_num_ext_348kmbu(t26, t27, t32);
                    gm_f_put_uint_cddin99(t24, t25, t33);
                }
                goto ix18;
ie18:;
                {
                    void * t35 = gm_l0;
                    gm_b16_8 t36 = gm_l6;
                    void * t37 = gm_l0;
                    gm_t88rcucg1b7uk t38 = gm_l3;
                    gm_b16_8 t39 = gm_l5;
                    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                    unsigned __int128 t41 = gm_f_get_int_pg7p4gc(t37, GM_CONV(gm_csguankko4lmhd, t38), t40);
                    gm_f_put_uint_cddin99(t35, t36, t41);
                }
ix18:;
            }
            goto me3;
mn15:;
ma5:;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t43 != ((uint8_t)1ULL)) goto mn42;
            {
                uint8_t t46 = 0;
                void * t47 = gm_l0;
                gm_t88rcucg1b7uk t48 = gm_l3;
                gm_t53at80pfdus3 t49;
                gm_f_kind_htv6093((void *)&t49, t47, GM_CONV(gm_csguankko4lmhd, t48));
                uint8_t t50;
                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t49) + 0));
                if (t53 != ((uint8_t)3ULL)) goto mn52;
                t50 = ((uint8_t)1);
                goto me51;
mn52:;
                t50 = ((uint8_t)0);
                goto me51;
mn54:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 21);
me51:;
                uint8_t t55 = (uint8_t)!t50;
                if (!t55) {
                    void * t56 = gm_l0;
                    gm_t88rcucg1b7uk t57 = gm_l4;
                    gm_t53at80pfdus3 t58;
                    gm_f_kind_htv6093((void *)&t58, t56, GM_CONV(gm_csguankko4lmhd, t57));
                    uint8_t t59;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
                    if (t62 != ((uint8_t)3ULL)) goto mn61;
                    t59 = ((uint8_t)1);
                    goto me60;
mn61:;
                    t59 = ((uint8_t)0);
                    goto me60;
mn63:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 56);
me60:;
                    uint8_t t64 = (uint8_t)!t59;
                    t46 = t64;
                } else t46 = 1;
                if (!t46) goto ie45;
                {
                    void * t66 = gm_l0;
                    gm_tl874t2atnlh3 t67 = gm_l1;
                    gm_b16_8 t68;
                    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sd4ibvv64si9n));
                    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(44));
                    gm_t76123gfet73g t69;
                    gm_f_trap_qh84l9d((void *)&t69, (void *)NULL, cx, NULL, t66, t67, t68);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t69);
                    goto gm_fdone;
                }
                goto ix45;
ie45:;
ix45:;
            }
            {
                void * t70 = gm_l0;
                gm_t88rcucg1b7uk t71 = gm_l4;
                void * t72 = gm_l0;
                gm_t88rcucg1b7uk t73 = gm_l3;
                gm_b16_8 t74 = gm_l5;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                double t76 = gm_f_get_float_grmhlm4(t72, GM_CONV(gm_csguankko4lmhd, t73), t75);
                gm_b16_8 t77 = gm_l6;
                gm_f_put_float_fiad72d(t70, GM_CONV(gm_csguankko4lmhd, t71), t76, t77);
            }
            goto me3;
mn42:;
ma6:;
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t79 != ((uint8_t)2ULL)) goto mn78;
            void * t80 = gm_l0;
            gm_t88rcucg1b7uk t81 = gm_l4;
            gm_b16_8 t82 = gm_l6;
            gm_b16_8 t83 = gm_l5;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_b0_1 t85 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t80, t81, t82, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn78:;
ma7:;
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t87 != ((uint8_t)3ULL)) goto mn86;
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l4;
            gm_b16_8 t90 = gm_l6;
            gm_b16_8 t91 = gm_l5;
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            gm_b0_1 t93 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t88, t89, t90, ((uint8_t)0), t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn86:;
ma8:;
            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t95 != ((uint8_t)4ULL)) goto mn94;
            uint32_t t96 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l8 = t96;
            {
                void * t99 = gm_l0;
                gm_t88rcucg1b7uk t100 = gm_l3;
                gm_b16_8 t101 = gm_l5;
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                uint8_t t103 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t99, t100, t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t103) goto ie98;
                {
                    void * t105 = gm_l0;
                    gm_t88rcucg1b7uk t106 = gm_l4;
                    gm_tgglga8ro3isc t107 = gm_f_res_types_pg7rvck(t105, GM_CONV(gm_csguankko4lmhd, t106));
                    gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t107) + 0));
                    gm_l9 = t108;
                }
                {
                    void * t109 = gm_l0;
                    void * t110 = gm_l0;
                    gm_t88rcucg1b7uk t111 = gm_l9;
                    uint64_t t112 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t110, t111);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t113 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t109, t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t113;
                }
                {
                    void * t114 = gm_l0;
                    gm_t88rcucg1b7uk t115 = gm_l9;
                    gm_b16_8 t116 = gm_l10;
                    uint32_t t117 = gm_l8;
                    void * t118 = gm_l0;
                    gm_t88rcucg1b7uk t119 = gm_l3;
                    gm_b16_8 t120 = gm_l5;
                    gm_b16_8 t121 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t118, t119, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                    gm_b0_1 t123 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t114, t115, t116, t117, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t124 = gm_l0;
                    gm_t88rcucg1b7uk t125 = gm_l4;
                    gm_b16_8 t126 = gm_l6;
                    gm_b16_8 t127 = gm_l10;
                    gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                    gm_b0_1 t129 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t124, t125, t126, ((uint8_t)1), t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix98;
ie98:;
                {
                    void * t131 = gm_l0;
                    gm_t88rcucg1b7uk t132 = gm_l4;
                    gm_b16_8 t133 = gm_l6;
                    void * t134 = gm_l0;
                    gm_t88rcucg1b7uk t135 = gm_l3;
                    gm_b16_8 t136 = gm_l5;
                    gm_b16_8 t137 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t134, t135, t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                    gm_b0_1 t139 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t131, t132, t133, ((uint8_t)0), t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix98:;
            }
            goto me3;
mn94:;
ma9:;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t141 != ((uint8_t)7ULL)) goto mn140;
            void * t142 = gm_l0;
            gm_t88rcucg1b7uk t143 = gm_l3;
            gm_b16_8 t144 = gm_l5;
            gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
            gm_b16_8 t146 = gm_l6;
            gm_b0_1 t147 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t142, t143, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me3;
mn140:;
ma10:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t149 != ((uint8_t)11ULL)) goto mn148;
            {
                void * t152 = gm_l0;
                gm_t88rcucg1b7uk t153 = gm_l3;
                gm_b16_8 t154 = gm_l5;
                gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                uint8_t t156 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t152, t153, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t157 = (uint8_t)!t156;
                if (!t157) goto ie151;
                {
                    gm_b16_8 t159 = gm_l6;
                    gm_f_fill_abmf5g5(t159, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t160;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t160);
                    goto gm_fdone;
                }
                goto ix151;
ie151:;
ix151:;
            }
            {
                void * t161 = gm_l0;
                gm_t88rcucg1b7uk t162 = gm_l3;
                gm_t88rcucg1b7uk t163 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t161, GM_CONV(gm_csguankko4lmhd, t162)));
                gm_l11 = t163;
            }
            {
                void * t164 = gm_l0;
                gm_t88rcucg1b7uk t165 = gm_l4;
                gm_t88rcucg1b7uk t166 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t164, GM_CONV(gm_csguankko4lmhd, t165)));
                gm_l12 = t166;
            }
            {
                void * t167 = gm_l0;
                gm_t88rcucg1b7uk t168 = gm_l3;
                gm_b16_8 t169 = gm_l5;
                gm_b16_8 t170 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t167, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t170;
            }
            {
                void * t172 = gm_l0;
                gm_t88rcucg1b7uk t173 = gm_l11;
                gm_t53at80pfdus3 t174;
                gm_f_kind_htv6093((void *)&t174, t172, GM_CONV(gm_csguankko4lmhd, t173));
                uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&t174) + 0));
                if (t175 != ((uint8_t)8ULL)) goto ie171;
                gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t174) + 8) + 4));
                gm_l14 = t176;
                void * t177 = gm_l0;
                gm_t88rcucg1b7uk t178 = gm_l12;
                gm_t53at80pfdus3 t179;
                gm_f_kind_htv6093((void *)&t179, t177, GM_CONV(gm_csguankko4lmhd, t178));
                uint8_t t180;
                uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&t179) + 0));
                if (t183 != ((uint8_t)10ULL)) goto mn182;
                t180 = ((uint8_t)1);
                goto me181;
mn182:;
                t180 = ((uint8_t)0);
                goto me181;
mn184:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 55);
me181:;
                if (!t180) goto ie171;
                void * t185 = gm_l0;
                gm_t88rcucg1b7uk t186 = gm_l14;
                gm_t53at80pfdus3 t187;
                gm_f_kind_htv6093((void *)&t187, t185, GM_CONV(gm_csguankko4lmhd, t186));
                uint8_t t188;
                uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&t187) + 0));
                if (t191 != ((uint8_t)11ULL)) goto mn190;
                t188 = ((uint8_t)1);
                goto me189;
mn190:;
                t188 = ((uint8_t)0);
                goto me189;
mn192:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 90);
me189:;
                if (!t188) goto ie171;
                {
                    void * t194 = gm_l0;
                    void * t195 = gm_l0;
                    gm_t88rcucg1b7uk t196 = gm_l12;
                    uint64_t t197 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t195, t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t198 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t194, t197);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t198;
                }
                {
                    void * t199 = gm_l0;
                    gm_t88rcucg1b7uk t200 = gm_l11;
                    gm_b16_8 t201 = gm_l13;
                    gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                    gm_b16_8 t203 = gm_l15;
                    gm_b0_1 t204 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t199, t200, t202, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t205 = gm_l0;
                    gm_t88rcucg1b7uk t206 = gm_l4;
                    gm_b16_8 t207 = gm_l6;
                    gm_b16_8 t208 = gm_l15;
                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                    gm_b0_1 t210 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t205, t206, t207, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix171;
ie171:;
                {
                    void * t212 = gm_l0;
                    gm_t88rcucg1b7uk t213 = gm_l4;
                    gm_b16_8 t214 = gm_l6;
                    gm_b16_8 t215 = gm_l13;
                    gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                    gm_b0_1 t217 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t212, t213, t214, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix171:;
            }
            goto me3;
mn148:;
ma11:;
            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t221 != ((uint8_t)6ULL)) goto pa220;
            goto po219;
pa220:;
            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t223 != ((uint8_t)8ULL)) goto pa222;
            goto po219;
pa222:;
            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t225 != ((uint8_t)10ULL)) goto pa224;
            goto po219;
pa224:;
            goto mn218;
po219:;
            gm_b16_8 t226 = gm_l6;
            gm_b16_8 t227 = gm_l5;
            gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
            gm_f_copy_from_f7j6p05(t226, t228);
            goto me3;
mn218:;
ma12:;
            uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t232 != ((uint8_t)5ULL)) goto pa231;
            goto po230;
pa231:;
            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t234 != ((uint8_t)9ULL)) goto pa233;
            goto po230;
pa233:;
            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t236 != ((uint8_t)12ULL)) goto pa235;
            goto po230;
pa235:;
            goto mn229;
po230:;
            void * t237 = gm_l0;
            gm_tl874t2atnlh3 t238 = gm_l1;
            gm_b16_8 t239;
            GM_ST(void *, (uint8_t *)&t239, (void *)(gm_sbjj41cb5lph3));
            GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(59));
            gm_t76123gfet73g t240;
            gm_f_trap_qh84l9d((void *)&t240, (void *)NULL, cx, NULL, t237, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t240);
            goto gm_fdone;
            goto me3;
mn229:;
ma13:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 9, 5);
me3:;
        }
    }
    gm_b0_1 t241;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t241);
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
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_convert_is6an2k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src_ty */
    gm_t88rcucg1b7uk gm_l4; /* dst_ty */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_t88rcucg1b7uk gm_l7; /* target */
    uint8_t gm_l8; /* is_float */
    gm_t86h6hhm6d76n gm_l9; /* sv */
    double gm_l10; /* fv */
    gm_ttovhfee8mf28 gm_l11; /* info */
    gm_tcmkompbl09il gm_l12; /* u */
    gm_ttovhfee8mf28 gm_l13; /* info */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ttovhfee8mf28 gm_l15; /* info */
    gm_ttovhfee8mf28 gm_l16; /* tinfo */
    uint8_t gm_l17; /* signed */
    uint32_t gm_l18; /* bits */
    gm_t51b2en4qp02s gm_l19; /* pb */
    uint64_t gm_l20; /* n */
    uint8_t gm_l21; /* finite */
    gm_t86h6hhm6d76n gm_l22; /* n */
    gm_t51b2en4qp02s gm_l23; /* pb */
    uint64_t gm_l24; /* sz */
    uint8_t gm_l25; /* neg */
    gm_t86h6hhm6d76n gm_l26; /* n */
    gm_t86h6hhm6d76n gm_l27; /* n */
    uint16_t gm_l28; /* w */
    double gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_ttovhfee8mf28 gm_l31; /* info */
    GM_STACK_CHECK(&F, 3106u);
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
        gm_t88rcucg1b7uk t2;
        uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
        if (!t4) goto ie3;
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l4;
        gm_t88rcucg1b7uk t8 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t6, GM_CONV(gm_csguankko4lmhd, t7)));
        gm_t88rcucg1b7uk t9 = t8;
        t2 = t9;
        goto ix3;
ie3:;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_t88rcucg1b7uk t12 = t11;
        t2 = t12;
ix3:;
        gm_l7 = t2;
    }
    {
        gm_l8 = ((uint8_t)0);
    }
    {
        memset((uint8_t *)&gm_l9 + 1, 0, 15);
        GM_ST(uint8_t, (uint8_t *)&gm_l9 + 0, ((uint8_t)0));
        GM_ST(unsigned __int128, (uint8_t *)&gm_l9 + 16, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
    }
    {
        gm_l10 = ((double)0e0);
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l3;
        gm_t53at80pfdus3 t15;
        gm_f_kind_htv6093((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        switch (t23) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma21; case ((uint8_t)6ULL): goto ma21; case ((uint8_t)7ULL): goto ma21; case ((uint8_t)8ULL): goto ma21; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma21; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma21; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma21; case ((uint8_t)21ULL): goto ma21; case ((uint8_t)22ULL): goto ma21; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma21; case ((uint8_t)25ULL): goto ma21; case ((uint8_t)26ULL): goto ma21; default: goto ma21; }
ma17:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t27 != ((uint8_t)0ULL)) goto pa26;
        goto po25;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t29 != ((uint8_t)1ULL)) goto pa28;
        goto po25;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t31 != ((uint8_t)2ULL)) goto pa30;
        goto po25;
pa30:;
        goto mn24;
po25:;
        {
            void * t33 = gm_l0;
            gm_t88rcucg1b7uk t34 = gm_l3;
            gm_tg5lql6itufk7 t35 = gm_f_int_info_ulcevdv(t33, GM_CONV(gm_csguankko4lmhd, t34));
            if (!((((uint8_t *)&t35))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 75, 21);
            gm_ttovhfee8mf28 t36 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t35));
            gm_l11 = t36;
        }
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            void * t38 = gm_l0;
            gm_t88rcucg1b7uk t39 = gm_l3;
            gm_b16_8 t40 = gm_l5;
            unsigned __int128 t41 = gm_f_get_int_pg7p4gc(t38, GM_CONV(gm_csguankko4lmhd, t39), t40);
            gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t37, t41);
        }
        goto me16;
mn24:;
ma18:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t43 != ((uint8_t)3ULL)) goto mn42;
        {
            gm_l8 = ((uint8_t)1);
        }
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l3;
            gm_b16_8 t47 = gm_l5;
            double t48 = gm_f_get_float_grmhlm4(t45, GM_CONV(gm_csguankko4lmhd, t46), t47);
            gm_l10 = t48;
        }
        goto me16;
mn42:;
ma19:;
        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t50 != ((uint8_t)4ULL)) goto mn49;
        {
            unsigned __int128 t52;
            uint64_t t54 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            if (t54 >= (uintptr_t)(t55)) gm_trap_index(t54, t55, gm_srcl8pn2pjtmj6k, 82, 32);
            uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t54 * 1));
            uint8_t t57 = ((t56 == ((uint8_t)0ULL)));
            if (!t57) goto ie53;
            unsigned __int128 t59 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            t52 = t59;
            goto ix53;
ie53:;
            unsigned __int128 t61 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
            t52 = t61;
ix53:;
            GM_ST(unsigned __int128, (((uint8_t *)&gm_l9) + 16), t52);
        }
        goto me16;
mn49:;
ma20:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t15) + 0));
        if (t63 != ((uint8_t)17ULL)) goto mn62;
        {
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l3;
            gm_t57euh612ligr t68;
            gm_f_union_def_h8mvf4t((void *)&t68, t66, GM_CONV(gm_csguankko4lmhd, t67));
            if (!((((uint8_t *)&t68))[24] != 0)) goto ie65;
            gm_l12 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t68) + 0));
            {
                void * t70 = gm_l0;
                gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_tg5lql6itufk7 t72 = gm_f_int_info_ulcevdv(t70, GM_CONV(gm_csguankko4lmhd, t71));
                if (!((((uint8_t *)&t72))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 85, 25);
                gm_ttovhfee8mf28 t73 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t72));
                gm_l13 = t73;
            }
            {
                uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                void * t75 = gm_l0;
                gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                gm_b16_8 t77 = gm_l5;
                uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&t77) + 8);
                void * t79 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t77)));
                uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
                void * t81 = gm_l0;
                gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l12) + 0));
                uint64_t t83 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t84 = gm_f_us_cuv01fp(t83);
                uint64_t t85 = (uintptr_t)t84;
                if (t80 > t85 || t85 > (uintptr_t)t78) gm_trap_slice(t80, t85, t78, gm_srcl8pn2pjtmj6k, 86, 60);
                gm_b16_8 t86;
                GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t79 + t80 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(t85 - t80));
                unsigned __int128 t87 = gm_f_get_int_pg7p4gc(t75, GM_CONV(gm_csguankko4lmhd, t76), t86);
                gm_f_num_val_i3upeur((void *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t74, t87);
            }
            goto ix65;
ie65:;
            void * t89 = gm_l0;
            gm_t88rcucg1b7uk t90 = gm_l3;
            gm_tkn42u7na3t81 t91 = gm_f_bits_struct_ty_22ue20s(t89, GM_CONV(gm_csguankko4lmhd, t90));
            if (!((((uint8_t *)&t91))[4] != 0)) goto ie88;
            gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t91));
            gm_l14 = t92;
            {
                void * t94 = gm_l0;
                gm_t88rcucg1b7uk t95 = gm_l14;
                gm_tg5lql6itufk7 t96 = gm_f_int_info_ulcevdv(t94, GM_CONV(gm_csguankko4lmhd, t95));
                if (!((((uint8_t *)&t96))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 88, 25);
                gm_ttovhfee8mf28 t97 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t96));
                gm_l15 = t97;
            }
            {
                memset((uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 1, 0, 15);
                void * t98 = gm_l0;
                gm_b16_8 t99 = gm_l5;
                uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&t99) + 8);
                void * t101 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t99)));
                uint64_t t102 = (uintptr_t)((uint64_t)0ULL);
                void * t103 = gm_l0;
                gm_t88rcucg1b7uk t104 = gm_l14;
                uint64_t t105 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t103, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t106 = gm_f_us_cuv01fp(t105);
                uint64_t t107 = (uintptr_t)t106;
                if (t102 > t107 || t107 > (uintptr_t)t100) gm_trap_slice(t102, t107, t100, gm_srcl8pn2pjtmj6k, 89, 57);
                gm_b16_8 t108;
                GM_ST(void *, (uint8_t *)&t108, (void *)(((uint8_t *)t101 + t102 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(t107 - t102));
                unsigned __int128 t109 = gm_f_get_uint_bq8c4b7(t98, t108);
                uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 4));
                unsigned __int128 t111 = gm_f_num_mask_do3naah(t110);
                unsigned __int128 t112 = (unsigned __int128)(t109 & t111);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 0, ((uint8_t)0));
                GM_ST(unsigned __int128, (uint8_t *)&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)) + 16, t112);
            }
            goto ix88;
ie88:;
            {
                void * t114 = gm_l0;
                gm_tl874t2atnlh3 t115 = gm_l1;
                void * t116 = gm_l0;
                void * t117 = GM_LD(void *, (((uint8_t *)t116) + 8));
                gm_t88rcucg1b7uk t118 = gm_l3;
                gm_b16_8 t119 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t120;
                GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
                gm_tabb710d9mq2e t121;
                gm_f_format_pe0ujmq((void *)&t121, (void *)NULL, cx, NULL, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_srcl8pn2pjtmj6k, 91, 38);
                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                gm_t76123gfet73g t124;
                gm_f_trap_qh84l9d((void *)&t124, (void *)NULL, cx, NULL, t114, t115, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t124);
                goto gm_fdone;
            }
ix88:;
ix65:;
        }
        goto me16;
mn62:;
ma21:;
        void * t126 = gm_l0;
        gm_tl874t2atnlh3 t127 = gm_l1;
        void * t128 = gm_l0;
        void * t129 = GM_LD(void *, (((uint8_t *)t128) + 8));
        gm_t88rcucg1b7uk t130 = gm_l3;
        gm_b16_8 t131 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t132;
        GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t131);
        gm_tabb710d9mq2e t133;
        gm_f_format_pe0ujmq((void *)&t133, (void *)NULL, cx, NULL, t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 8, gm_srcl8pn2pjtmj6k, 94, 35);
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_t76123gfet73g t136;
        gm_f_trap_qh84l9d((void *)&t136, (void *)NULL, cx, NULL, t126, t127, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t136);
        goto gm_fdone;
        goto me16;
mn125:;
ma22:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 73, 5);
me16:;
    }
    {
        void * t138 = gm_l0;
        gm_t88rcucg1b7uk t139 = gm_l7;
        gm_tg5lql6itufk7 t140 = gm_f_int_info_ulcevdv(t138, GM_CONV(gm_csguankko4lmhd, t139));
        if (!((((uint8_t *)&t140))[8] != 0)) goto ie137;
        gm_ttovhfee8mf28 t141 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t140));
        gm_l16 = t141;
        {
            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            gm_l17 = t143;
        }
        {
            uint32_t t144 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
            gm_l18 = t144;
        }
        {
            uint8_t t146 = gm_l8;
            uint8_t t147 = (uint8_t)!t146;
            if (!t147) goto ie145;
            {
                uint8_t t150 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
                if (!t150) goto ie149;
                {
                    uint8_t t153 = gm_l17;
                    uint32_t t154 = gm_l18;
                    uint8_t t155 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t153, t154);
                    if (!t155) goto ie152;
                    {
                    }
                    {
                        void * t157 = gm_l0;
                        gm_t88rcucg1b7uk t158 = gm_l7;
                        uint64_t t159 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t157, t158);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t160 = gm_f_us_cuv01fp(t159);
                        gm_l20 = t160;
                    }
                    {
                        void * t161 = gm_l0;
                        void * t162 = ((uint8_t *)&gm_l19);
                        uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t164 = gm_l20;
                        uint64_t t165 = (uintptr_t)t164;
                        if (t163 > t165 || t165 > (uintptr_t)16) gm_trap_slice(t163, t165, 16, gm_srcl8pn2pjtmj6k, 104, 32);
                        gm_b16_8 t166;
                        GM_ST(void *, (uint8_t *)&t166, (void *)(((uint8_t *)t162 + t163 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(t165 - t163));
                        uint8_t t167 = gm_l17;
                        uint32_t t168 = gm_l18;
                        unsigned __int128 t169 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                        unsigned __int128 t170 = gm_f_num_ext_348kmbu(t167, t168, t169);
                        gm_f_put_uint_cddin99(t161, t166, t170);
                    }
                    {
                        void * t171 = gm_l0;
                        gm_t88rcucg1b7uk t172 = gm_l4;
                        gm_b16_8 t173 = gm_l6;
                        void * t174 = ((uint8_t *)&gm_l19);
                        uint64_t t175 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t176 = gm_l20;
                        uint64_t t177 = (uintptr_t)t176;
                        if (t175 > t177 || t177 > (uintptr_t)16) gm_trap_slice(t175, t177, 16, gm_srcl8pn2pjtmj6k, 105, 46);
                        gm_b16_8 t178;
                        GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t174 + t175 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(t177 - t175));
                        gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                        gm_b0_1 t180 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t171, t172, t173, t179);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix152;
ie152:;
                    {
                        gm_b16_8 t182 = gm_l6;
                        gm_f_fill_abmf5g5(t182, ((uint8_t)0ULL));
                    }
ix152:;
                }
                goto ix149;
ie149:;
                uint8_t t184 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
                if (!t184) goto ie183;
                {
                    void * t186 = gm_l0;
                    gm_b16_8 t187 = gm_l6;
                    uint8_t t188 = gm_l17;
                    uint32_t t189 = gm_l18;
                    uint8_t t190 = gm_l17;
                    uint32_t t191 = gm_l18;
                    gm_t86h6hhm6d76n t192;
                    gm_f_num_sat_66dqcpl((void *)&t192, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t190, t191);
                    unsigned __int128 t193 = gm_f_num_bits_onl28vp(&t192);
                    unsigned __int128 t194 = gm_f_num_ext_348kmbu(t188, t189, t193);
                    gm_f_put_uint_cddin99(t186, t187, t194);
                }
                goto ix183;
ie183:;
                {
                    void * t196 = gm_l0;
                    gm_b16_8 t197 = gm_l6;
                    uint8_t t198 = gm_l17;
                    uint32_t t199 = gm_l18;
                    unsigned __int128 t200 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                    unsigned __int128 t201 = gm_f_num_ext_348kmbu(t198, t199, t200);
                    gm_f_put_uint_cddin99(t196, t197, t201);
                }
ix183:;
ix149:;
            }
            {
                gm_b0_1 t202;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t202);
                goto gm_fdone;
            }
            goto ix145;
ie145:;
ix145:;
        }
        {
            double t203 = gm_l10;
            double t204 = gm_l10;
            double t205 = (t203 - t204);
            uint8_t t206 = ((t205 == ((double)0e0)));
            gm_l21 = t206;
        }
        {
            uint8_t t208 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)5ULL)));
            if (!t208) goto ie207;
            {
                gm_b16_8 t210 = gm_l6;
                gm_f_fill_abmf5g5(t210, ((uint8_t)0ULL));
            }
            {
                uint8_t t212 = 0;
                uint8_t t213 = gm_l21;
                uint8_t t214 = (uint8_t)!t213;
                if (!t214) {
                    double t215 = gm_l10;
                    uint8_t t216 = gm_f_num_integral_qbts4a6(t215);
                    uint8_t t217 = (uint8_t)!t216;
                    t212 = t217;
                } else t212 = 1;
                if (!t212) goto ie211;
                {
                    gm_b0_1 t219;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t219);
                    goto gm_fdone;
                }
                goto ix211;
ie211:;
ix211:;
            }
            {
                double t220 = gm_l10;
                gm_tvdg94bk7aa22 t221;
                GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t221 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)1ULL));
                gm_tsp9ul70rnkg6 t222;
                gm_f_num_float_int_ba5kc1t((void *)&t222, t220, t221);
                gm_t86h6hhm6d76n t223;
                if (((((uint8_t *)&t222))[32] != 0)) {
                    t223 = (*(gm_t86h6hhm6d76n *)(((uint8_t *)&t222) + 0));
                } else {
                    gm_b0_1 t224;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t224);
                    goto gm_fdone;
                }
                gm_l22 = t223;
            }
            {
                uint8_t t226 = gm_l17;
                uint32_t t227 = gm_l18;
                uint8_t t228 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)), t226, t227);
                uint8_t t229 = (uint8_t)!t228;
                if (!t229) goto ie225;
                {
                    gm_b0_1 t231;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t231);
                    goto gm_fdone;
                }
                goto ix225;
ie225:;
ix225:;
            }
            {
            }
            {
                void * t232 = gm_l0;
                gm_t88rcucg1b7uk t233 = gm_l7;
                uint64_t t234 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t232, t233);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t235 = gm_f_us_cuv01fp(t234);
                gm_l24 = t235;
            }
            {
                void * t236 = gm_l0;
                void * t237 = ((uint8_t *)&gm_l23);
                uint64_t t238 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t239 = gm_l24;
                uint64_t t240 = (uintptr_t)t239;
                if (t238 > t240 || t240 > (uintptr_t)16) gm_trap_slice(t238, t240, 16, gm_srcl8pn2pjtmj6k, 124, 24);
                gm_b16_8 t241;
                GM_ST(void *, (uint8_t *)&t241, (void *)(((uint8_t *)t237 + t238 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(t240 - t238));
                uint8_t t242 = gm_l17;
                uint32_t t243 = gm_l18;
                unsigned __int128 t244 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)));
                unsigned __int128 t245 = gm_f_num_ext_348kmbu(t242, t243, t244);
                gm_f_put_uint_cddin99(t236, t241, t245);
            }
            {
                void * t246 = gm_l0;
                gm_t88rcucg1b7uk t247 = gm_l4;
                gm_b16_8 t248 = gm_l6;
                void * t249 = ((uint8_t *)&gm_l23);
                uint64_t t250 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t251 = gm_l24;
                uint64_t t252 = (uintptr_t)t251;
                if (t250 > t252 || t252 > (uintptr_t)16) gm_trap_slice(t250, t252, 16, gm_srcl8pn2pjtmj6k, 125, 38);
                gm_b16_8 t253;
                GM_ST(void *, (uint8_t *)&t253, (void *)(((uint8_t *)t249 + t250 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(t252 - t250));
                gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                gm_b0_1 t255 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t246, t247, t248, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t256;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t256);
                goto gm_fdone;
            }
            goto ix207;
ie207:;
ix207:;
        }
        {
            uint8_t t258 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)6ULL)));
            if (!t258) goto ie257;
            {
                double t261 = gm_l10;
                uint8_t t262 = gm_f_num_is_nan_b0ma7mo(t261);
                if (!t262) goto ie260;
                {
                    gm_b16_8 t264 = gm_l6;
                    gm_f_fill_abmf5g5(t264, ((uint8_t)0ULL));
                }
                {
                    gm_b0_1 t265;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t265);
                    goto gm_fdone;
                }
                goto ix260;
ie260:;
ix260:;
            }
            {
                double t266 = gm_l10;
                uint64_t t267;
                memset(&t267, 0, sizeof t267); memcpy(&t267, ((uint8_t *)&t266), sizeof t267 < 8 ? sizeof t267 : 8);
                unsigned __int128 n268 = (unsigned __int128)((uint64_t)63ULL);
                if (n268 >= 64) gm_trap_shift((uint64_t)n268, "u64", gm_srcl8pn2pjtmj6k, 133, 21);
                uint64_t t269 = (uint64_t)(t267 >> (int)n268);
                uint8_t t270 = ((t269 == ((uint64_t)1ULL)));
                gm_l25 = t270;
            }
            {
                memset((uint8_t *)&gm_l26 + 1, 0, 15);
                uint8_t t271 = gm_l25;
                GM_ST(uint8_t, (uint8_t *)&gm_l26 + 0, t271);
                unsigned __int128 t272 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
                GM_ST(unsigned __int128, (uint8_t *)&gm_l26 + 16, t272);
            }
            {
                uint8_t t274 = gm_l21;
                if (!t274) goto ie273;
                {
                    double t276 = gm_l10;
                    gm_tvdg94bk7aa22 t277;
                    GM_ST(uint8_t, (uint8_t *)&t277 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t277 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t277 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)1ULL));
                    gm_tsp9ul70rnkg6 t278;
                    gm_f_num_float_int_ba5kc1t((void *)&t278, t276, t277);
                    gm_t86h6hhm6d76n t279;
                    if (((((uint8_t *)&t278))[32] != 0)) {
                        t279 = (*(gm_t86h6hhm6d76n *)(((uint8_t *)&t278) + 0));
                    } else {
                        gm_t86h6hhm6d76n t280 = gm_l26;
                        t279 = t280;
                    }
                    gm_l26 = t279;
                }
                goto ix273;
ie273:;
ix273:;
            }
            {
                void * t281 = gm_l0;
                gm_b16_8 t282 = gm_l6;
                uint8_t t283 = gm_l17;
                uint32_t t284 = gm_l18;
                uint8_t t285 = gm_l17;
                uint32_t t286 = gm_l18;
                gm_t86h6hhm6d76n t287;
                gm_f_num_sat_66dqcpl((void *)&t287, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l26)), t285, t286);
                unsigned __int128 t288 = gm_f_num_bits_onl28vp(&t287);
                unsigned __int128 t289 = gm_f_num_ext_348kmbu(t283, t284, t288);
                gm_f_put_uint_cddin99(t281, t282, t289);
            }
            {
                gm_b0_1 t290;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t290);
                goto gm_fdone;
            }
            goto ix257;
ie257:;
ix257:;
        }
        {
            uint8_t t292 = gm_l21;
            if (!t292) goto ie291;
            {
                double t295 = gm_l10;
                gm_tvdg94bk7aa22 t296 = gm_l2;
                gm_tsp9ul70rnkg6 t297;
                gm_f_num_float_int_ba5kc1t((void *)&t297, t295, t296);
                if (!((((uint8_t *)&t297))[32] != 0)) goto ie294;
                gm_l27 = (*(gm_t86h6hhm6d76n *)(((uint8_t *)&t297) + 0));
                {
                    uint8_t t300 = gm_l17;
                    uint32_t t301 = gm_l18;
                    uint8_t t302 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)), t300, t301);
                    if (!t302) goto ie299;
                    {
                        void * t304 = gm_l0;
                        gm_b16_8 t305 = gm_l6;
                        uint8_t t306 = gm_l17;
                        uint32_t t307 = gm_l18;
                        unsigned __int128 t308 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)));
                        unsigned __int128 t309 = gm_f_num_ext_348kmbu(t306, t307, t308);
                        gm_f_put_uint_cddin99(t304, t305, t309);
                    }
                    {
                        gm_b0_1 t310;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t310);
                        goto gm_fdone;
                    }
                    goto ix299;
ie299:;
ix299:;
                }
                goto ix294;
ie294:;
ix294:;
            }
            goto ix291;
ie291:;
ix291:;
        }
        {
            void * t312 = gm_l0;
            uint8_t t313 = gm_f_fast_23em7sv(t312);
            if (!t313) goto ie311;
            {
                gm_b16_8 t315 = gm_l6;
                gm_f_fill_abmf5g5(t315, ((uint8_t)0ULL));
            }
            {
                gm_b0_1 t316;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t316);
                goto gm_fdone;
            }
            goto ix311;
ie311:;
ix311:;
        }
        {
            void * t317 = gm_l0;
            gm_tl874t2atnlh3 t318 = gm_l1;
            gm_b16_8 t319;
            GM_ST(void *, (uint8_t *)&t319, (void *)(gm_s9o2c36nosv54));
            GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(40));
            gm_t76123gfet73g t320;
            gm_f_trap_qh84l9d((void *)&t320, (void *)NULL, cx, NULL, t317, t318, t319);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t320);
            goto gm_fdone;
        }
        goto ix137;
ie137:;
ix137:;
    }
    {
        void * t322 = gm_l0;
        gm_t88rcucg1b7uk t323 = gm_l7;
        gm_t53at80pfdus3 t324;
        gm_f_kind_htv6093((void *)&t324, t322, GM_CONV(gm_csguankko4lmhd, t323));
        uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&t324) + 0));
        if (t325 != ((uint8_t)3ULL)) goto ie321;
        uint16_t t326 = GM_LD(uint16_t, (((uint8_t *)&t324) + 8));
        gm_l28 = t326;
        {
            double t328;
            uint8_t t330 = gm_l8;
            if (!t330) goto ie329;
            double t332 = gm_l10;
            uint16_t t333 = gm_l28;
            double t334 = gm_f_num_round_2pna7m3(t332, t333);
            double t335 = t334;
            t328 = t335;
            goto ix329;
ie329:;
            uint16_t t337 = gm_l28;
            double t338 = gm_f_num_int_to_float_4hrvis6(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t337);
            double t339 = t338;
            t328 = t339;
ix329:;
            gm_l29 = t328;
        }
        {
            void * t340 = gm_l0;
            gm_t88rcucg1b7uk t341 = gm_l7;
            double t342 = gm_l29;
            gm_b16_8 t343 = gm_l6;
            gm_f_put_float_fiad72d(t340, GM_CONV(gm_csguankko4lmhd, t341), t342, t343);
        }
        {
            gm_b0_1 t344;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t344);
            goto gm_fdone;
        }
        goto ix321;
ie321:;
ix321:;
    }
    {
        void * t346 = gm_l0;
        gm_t88rcucg1b7uk t347 = gm_l7;
        gm_tkn42u7na3t81 t348 = gm_f_bits_struct_ty_22ue20s(t346, GM_CONV(gm_csguankko4lmhd, t347));
        if (!((((uint8_t *)&t348))[4] != 0)) goto ie345;
        gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t348));
        gm_l30 = t349;
        {
            uint8_t t352 = gm_l8;
            uint8_t t353 = (uint8_t)!t352;
            if (!t353) goto ie351;
            {
                void * t355 = gm_l0;
                gm_t88rcucg1b7uk t356 = gm_l30;
                gm_tg5lql6itufk7 t357 = gm_f_int_info_ulcevdv(t355, GM_CONV(gm_csguankko4lmhd, t356));
                if (!((((uint8_t *)&t357))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 160, 21);
                gm_ttovhfee8mf28 t358 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t357));
                gm_l31 = t358;
            }
            {
                void * t359 = gm_l0;
                gm_b16_8 t360 = gm_l6;
                uint32_t t361 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                unsigned __int128 t362 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                unsigned __int128 t363 = gm_f_num_ext_348kmbu(((uint8_t)0), t361, t362);
                gm_f_put_uint_cddin99(t359, t360, t363);
            }
            {
                gm_b0_1 t364;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t364);
                goto gm_fdone;
            }
            goto ix351;
ie351:;
ix351:;
        }
        goto ix345;
ie345:;
ix345:;
    }
    {
        void * t365 = gm_l0;
        gm_tl874t2atnlh3 t366 = gm_l1;
        void * t367 = gm_l0;
        void * t368 = GM_LD(void *, (((uint8_t *)t367) + 8));
        gm_t88rcucg1b7uk t369 = gm_l7;
        gm_b16_8 t370 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t368, t369);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t371;
        GM_ST(gm_b16_8, ((uint8_t *)&t371 + 0), t370);
        gm_tabb710d9mq2e t372;
        gm_f_format_fv5tfj6((void *)&t372, (void *)NULL, cx, NULL, t371);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t372))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t372) + 8, gm_srcl8pn2pjtmj6k, 165, 26);
        gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&t372) + 8));
        gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
        gm_t76123gfet73g t375;
        gm_f_trap_qh84l9d((void *)&t375, (void *)NULL, cx, NULL, t365, t366, t374);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t375);
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
inline __attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_bytes_2qnf0cp F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l2;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                gm_l3 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t t10 = gm_l2;
                    uint64_t r11; bool o12 = __builtin_sub_overflow(t9, t10, &r11);
                    if (o12) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 416, 22);
                    uint64_t t13 = r11;
                    uint64_t t14 = (t13 < ((uint64_t)8ULL) ? t13 : ((uint64_t)8ULL));
                    gm_l4 = t14;
                }
                {
                    for (;;) {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = gm_l4;
                        uint8_t t18 = (t16 < t17);
                        if (!t18) goto lb15;
                        {
                            uint64_t t21 = gm_l5;
                            uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)&t21));
                            gm_l6 = t24;
                            {
                                uint64_t t26 = gm_l3;
                                uint64_t t27 = gm_l2;
                                uint64_t t28 = gm_l6;
                                uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
                                if (o30) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 416, 61);
                                uint64_t t31 = r29;
                                uint64_t t32 = (uintptr_t)t31;
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcl8pn2pjtmj6k, 416, 55);
                                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                                uint64_t t35 = (uint64_t)t34;
                                uint64_t t36 = gm_l6;
                                uint64_t r37; bool o38 = __builtin_mul_overflow(t36, ((uint64_t)8ULL), &r37);
                                if (o38) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 416, 76);
                                uint64_t t39 = r37;
                                uint64_t t40 = (uint64_t)t39;
                                unsigned __int128 n41 = (unsigned __int128)t40;
                                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcl8pn2pjtmj6k, 416, 51);
                                uint64_t r42 = (uint64_t)((uint64_t)t35 << (int)n41);
                                bool o43 = (uint64_t)(r42 >> (int)n41) != t35;
                                if (o43) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 416, 51);
                                uint64_t t44 = r42;
                                uint64_t t45 = (uint64_t)(t26 | t44);
                                gm_l3 = t45;
                            }
                            goto me22;
mn23:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 416, 9);
me22:;
                        }
lc15:;
                        {
                            uint64_t t47 = gm_l5;
                            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                            uint64_t t50 = r48;
                            gm_l5 = t50;
                        }
                    }
lb15:;
                }
            }
            {
                void * t51 = gm_l0;
                uint64_t t52 = gm_l3;
                gm_f_hash_u64_32bripn(t51, t52);
            }
            {
                uint64_t t53 = gm_l2;
                uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)8ULL), &r54);
                if (o55) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 418, 9);
                uint64_t t56 = r54;
                gm_l2 = t56;
            }
lc2:;
        }
lb2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2) { gm_f_hash_int_26mf5j0(a0, &a1, a2); }
extern __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_hash_val_nr2e99r F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* h */
    gm_ttovhfee8mf28 gm_l5; /* info */
    double gm_l6; /* x */
    gm_b16_8 gm_l7; /* elems */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint64_t gm_l13; /* esz */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* inner */
    gm_t88rcucg1b7uk gm_l22; /* err */
    gm_t88rcucg1b7uk gm_l23; /* ok */
    uint8_t gm_l24; /* is_err */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* u */
    uint32_t gm_l27; /* vi */
    gm_t88rcucg1b7uk gm_l28; /* pt */
    uint64_t gm_l29; /* off */
    void * gm_l30; /* a */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    gm_tfbnciphf3261 gm_l35; /* fl */
    uint64_t gm_l36; /* off */
    GM_STACK_CHECK(&F, 4042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            gm_t88rcucg1b7uk t4 = gm_l2;
            gm_t53at80pfdus3 t5;
            gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            switch (t20) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma15; case ((uint8_t)14ULL): goto ma16; case ((uint8_t)15ULL): goto ma18; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma18; case ((uint8_t)20ULL): goto ma18; case ((uint8_t)21ULL): goto ma18; case ((uint8_t)22ULL): goto ma18; case ((uint8_t)23ULL): goto ma18; case ((uint8_t)24ULL): goto ma18; case ((uint8_t)25ULL): goto ma18; case ((uint8_t)26ULL): goto ma18; default: goto ma18; }
ma7:;
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t24 != ((uint8_t)0ULL)) goto pa23;
            goto po22;
pa23:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t26 != ((uint8_t)1ULL)) goto pa25;
            goto po22;
pa25:;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t28 != ((uint8_t)2ULL)) goto pa27;
            goto po22;
pa27:;
            goto mn21;
po22:;
            {
                void * t30 = gm_l0;
                gm_t88rcucg1b7uk t31 = gm_l2;
                gm_tg5lql6itufk7 t32 = gm_f_int_info_ulcevdv(t30, GM_CONV(gm_csguankko4lmhd, t31));
                if (!((((uint8_t *)&t32))[8] != 0)) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 434, 21);
                gm_ttovhfee8mf28 t33 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t32));
                gm_l5 = t33;
            }
            {
                void * t34 = gm_l4;
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                void * t36 = gm_l0;
                gm_t88rcucg1b7uk t37 = gm_l2;
                gm_b16_8 t38 = gm_l3;
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                unsigned __int128 t40 = gm_f_get_int_pg7p4gc(t36, GM_CONV(gm_csguankko4lmhd, t37), t39);
                gm_t86h6hhm6d76n t41;
                gm_f_num_val_i3upeur((void *)&t41, t35, t40);
                uint32_t t42 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                gm_f_hash_int_26mf5j0(t34, &t41, t42);
            }
            goto me6;
mn21:;
ma8:;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t44 != ((uint8_t)3ULL)) goto mn43;
            {
                void * t46 = gm_l0;
                gm_t88rcucg1b7uk t47 = gm_l2;
                gm_b16_8 t48 = gm_l3;
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                double t50 = gm_f_get_float_grmhlm4(t46, GM_CONV(gm_csguankko4lmhd, t47), t49);
                gm_l6 = t50;
            }
            {
                double t52 = gm_l6;
                uint8_t t53 = ((t52 == ((double)0e0)));
                if (!t53) goto ie51;
                {
                    gm_l6 = ((double)0e0);
                }
                goto ix51;
ie51:;
ix51:;
            }
            {
                void * t55 = gm_l4;
                double t56 = gm_l6;
                uint64_t t57;
                memset(&t57, 0, sizeof t57); memcpy(&t57, ((uint8_t *)&t56), sizeof t57 < 8 ? sizeof t57 : 8);
                gm_f_hash_u64_32bripn(t55, t57);
            }
            goto me6;
mn43:;
ma9:;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t59 != ((uint8_t)4ULL)) goto mn58;
            void * t60 = gm_l4;
            uint64_t t61;
            uint64_t t63 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_srcl8pn2pjtmj6k, 442, 33);
            uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t63 * 1));
            uint8_t t66 = ((t65 == ((uint8_t)0ULL)));
            if (!t66) goto ie62;
            uint64_t t68 = ((uint64_t)0ULL);
            t61 = t68;
            goto ix62;
ie62:;
            uint64_t t70 = ((uint64_t)1ULL);
            t61 = t70;
ix62:;
            gm_f_hash_u64_32bripn(t60, t61);
            goto me6;
mn58:;
ma10:;
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t74 != ((uint8_t)8ULL)) goto pa73;
            goto po72;
pa73:;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t76 != ((uint8_t)9ULL)) goto pa75;
            goto po72;
pa75:;
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t78 != ((uint8_t)7ULL)) goto pa77;
            goto po72;
pa77:;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t80 != ((uint8_t)16ULL)) goto pa79;
            goto po72;
pa79:;
            goto mn71;
po72:;
            void * t81 = gm_l4;
            void * t82 = gm_l0;
            gm_b16_8 t83 = gm_l3;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            uint64_t t85 = gm_f_ptr_of_b77b4c9(t82, t84);
            gm_f_hash_u64_32bripn(t81, t85);
            goto me6;
mn71:;
ma11:;
            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t87 != ((uint8_t)5ULL)) goto mn86;
            goto me6;
mn86:;
ma12:;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t90 != ((uint8_t)12ULL)) goto mn89;
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
            gm_l7 = t91;
            {
                {
                    gm_l9 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l8 = t94;
                }
                {
                    for (;;) {
                        uint64_t t96 = gm_l9;
                        uint64_t t97 = gm_l8;
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb95;
                        {
                            uint64_t t101 = gm_l9;
                            uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&t101));
                            gm_l10 = t104;
                            {
                                void * t106 = gm_l0;
                                gm_tl874t2atnlh3 t107 = gm_l1;
                                uint64_t t108 = gm_l10;
                                uint64_t t109 = (uintptr_t)t108;
                                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srcl8pn2pjtmj6k, 446, 58);
                                gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t109 * 4));
                                void * t112 = gm_l0;
                                gm_t88rcucg1b7uk t113 = gm_l2;
                                gm_b16_8 t114 = gm_l3;
                                uint64_t t115 = gm_l10;
                                uint32_t t116 = (uint32_t)t115;
                                gm_b16_8 t117 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t112, t113, t114, t116);
                                if (gm_unw.kind) {
                                    goto gm_ll99;
                                }
                                void * t118 = gm_l4;
                                gm_tcfa6l7reoosn t119;
                                gm_f_hash_val_nr2e99r((void *)&t119, (void *)NULL, cx, NULL, t106, t107, t111, t117, t118);
                                if (gm_unw.kind) {
                                    goto gm_ll99;
                                }
                                if (((((uint8_t *)&t119))[0] != 0)) {
                                    gm_tcfa6l7reoosn t120;
                                    GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                                    ((uint8_t *)&t120)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t120 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t119) + 8)));
                                    F.ret = t120;
                                    goto gm_fret;
                                }
                                gm_b0_1 t121 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 8));
                            }
                            goto me102;
mn103:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 446, 13);
me102:;
                        }
lc95:;
                        {
                            uint64_t t123 = gm_l9;
                            uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                            uint64_t t126 = r124;
                            gm_l9 = t126;
                        }
                    }
gm_ll99:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
                    }
                    goto gm_lad;
lb95:;
                }
            }
            goto me6;
mn89:;
ma13:;
            uint8_t t128 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t128 != ((uint8_t)11ULL)) goto mn127;
            uint64_t t129 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 0));
            gm_l11 = t129;
            gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
            gm_l12 = t130;
            {
                void * t132 = gm_l0;
                gm_t88rcucg1b7uk t133 = gm_l12;
                uint64_t t134 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t132, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t135 = gm_f_us_cuv01fp(t134);
                gm_l13 = t135;
            }
            {
                void * t137 = gm_l0;
                gm_t88rcucg1b7uk t138 = gm_l12;
                gm_t53at80pfdus3 t139;
                gm_f_kind_htv6093((void *)&t139, t137, GM_CONV(gm_csguankko4lmhd, t138));
                gm_tjcj4j2gbh6c8 t140;
                GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t140 + 2), ((uint16_t)8ULL));
                uint8_t t141 = (((GM_LD(uint8_t, ((uint8_t *)&t139) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t139) + 8), ((uint8_t *)&t140))));
                if (!t141) goto ie136;
                {
                    void * t143 = gm_l4;
                    gm_b16_8 t144 = gm_l3;
                    uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&t144) + 8);
                    void * t146 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t144)));
                    uint64_t t147 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t148 = gm_l11;
                    uint64_t t149 = gm_f_us_cuv01fp(t148);
                    uint64_t t150 = (uintptr_t)t149;
                    if (t147 > t150 || t150 > (uintptr_t)t145) gm_trap_slice(t147, t150, t145, gm_srcl8pn2pjtmj6k, 451, 31);
                    gm_b16_8 t151;
                    GM_ST(void *, (uint8_t *)&t151, (void *)(((uint8_t *)t146 + t147 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(t150 - t147));
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                    gm_f_hash_bytes_2qnf0cp(t143, t152);
                }
                {
                    gm_b0_1 t153;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t153);
                    goto gm_fdone;
                }
                goto ix136;
ie136:;
ix136:;
            }
            {
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t155 = gm_l11;
                    uint64_t t156 = gm_f_us_cuv01fp(t155);
                    gm_l14 = t156;
                }
                {
                    for (;;) {
                        uint64_t t158 = gm_l15;
                        uint64_t t159 = gm_l14;
                        uint8_t t160 = (t158 < t159);
                        if (!t160) goto lb157;
                        {
                            uint64_t t163 = gm_l15;
                            uint64_t t166 = GM_LD(uint64_t, ((uint8_t *)&t163));
                            gm_l16 = t166;
                            {
                                void * t168 = gm_l0;
                                gm_tl874t2atnlh3 t169 = gm_l1;
                                gm_t88rcucg1b7uk t170 = gm_l12;
                                uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                void * t172 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
                                uint64_t t173 = gm_l16;
                                uint64_t t174 = gm_l13;
                                uint64_t r175; bool o176 = __builtin_mul_overflow(t173, t174, &r175);
                                if (o176) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 62);
                                uint64_t t177 = r175;
                                uint64_t t178 = (uintptr_t)t177;
                                uint64_t t179 = gm_l16;
                                uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                                if (o181) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 454, 72);
                                uint64_t t182 = r180;
                                uint64_t t183 = gm_l13;
                                uint64_t r184; bool o185 = __builtin_mul_overflow(t182, t183, &r184);
                                if (o185) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 71);
                                uint64_t t186 = r184;
                                uint64_t t187 = (uintptr_t)t186;
                                if (t178 > t187 || t187 > (uintptr_t)t171) gm_trap_slice(t178, t187, t171, gm_srcl8pn2pjtmj6k, 454, 60);
                                gm_b16_8 t188;
                                GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t172 + t178 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(t187 - t178));
                                void * t189 = gm_l4;
                                gm_tcfa6l7reoosn t190;
                                gm_f_hash_val_nr2e99r((void *)&t190, (void *)NULL, cx, NULL, t168, t169, t170, t188, t189);
                                if (gm_unw.kind) {
                                    goto gm_ll161;
                                }
                                if (((((uint8_t *)&t190))[0] != 0)) {
                                    gm_tcfa6l7reoosn t191;
                                    GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                                    ((uint8_t *)&t191)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t191 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t190) + 8)));
                                    F.ret = t191;
                                    goto gm_fret;
                                }
                                gm_b0_1 t192 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 8));
                            }
                            goto me164;
mn165:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 454, 13);
me164:;
                        }
lc157:;
                        {
                            uint64_t t194 = gm_l15;
                            uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                            uint64_t t197 = r195;
                            gm_l15 = t197;
                        }
                    }
gm_ll161:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb157; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc157; }
                    }
                    goto gm_lad;
lb157:;
                }
            }
            goto me6;
mn127:;
ma14:;
            uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t199 != ((uint8_t)10ULL)) goto mn198;
            {
                void * t201 = gm_l0;
                gm_tl874t2atnlh3 t202 = gm_l1;
                gm_t88rcucg1b7uk t203 = gm_l2;
                gm_b16_8 t204 = gm_l3;
                gm_tmc91gv7n9c0i t205;
                gm_f_seq_of_ifahtjs((void *)&t205, (void *)NULL, cx, NULL, t201, t202, t203, t204);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t205))[0] != 0)) {
                    gm_tcfa6l7reoosn t206;
                    GM_ST(uint8_t, (uint8_t *)&t206 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t206 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t206 + 4, 0);
                    ((uint8_t *)&t206)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t206 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t205) + 8)));
                    F.ret = t206;
                    goto gm_fret;
                }
                F.l17 = (*(gm_t2jeg3bg7sfqa *)(((uint8_t *)&t205) + 8));
            }
            {
                void * t207 = gm_l4;
                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                gm_f_hash_u64_32bripn(t207, t208);
            }
            {
                void * t210 = gm_l0;
                gm_t88rcucg1b7uk t211 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 0));
                gm_t53at80pfdus3 t212;
                gm_f_kind_htv6093((void *)&t212, t210, GM_CONV(gm_csguankko4lmhd, t211));
                gm_tjcj4j2gbh6c8 t213;
                GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t213 + 2), ((uint16_t)8ULL));
                uint8_t t214 = (((GM_LD(uint8_t, ((uint8_t *)&t212) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t212) + 8), ((uint8_t *)&t213))));
                if (!t214) goto ie209;
                {
                    void * t216 = gm_l4;
                    gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 24));
                    uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&t217) + 8);
                    void * t219 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t217)));
                    uint64_t t220 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t222 = gm_f_us_cuv01fp(t221);
                    uint64_t t223 = (uintptr_t)t222;
                    if (t220 > t223 || t223 > (uintptr_t)t218) gm_trap_slice(t220, t223, t218, gm_srcl8pn2pjtmj6k, 460, 31);
                    gm_b16_8 t224;
                    GM_ST(void *, (uint8_t *)&t224, (void *)(((uint8_t *)t219 + t220 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(t223 - t220));
                    gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                    gm_f_hash_bytes_2qnf0cp(t216, t225);
                }
                {
                    gm_b0_1 t226;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t226);
                    goto gm_fdone;
                }
                goto ix209;
ie209:;
ix209:;
            }
            {
                {
                    gm_l19 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t229 = gm_f_us_cuv01fp(t228);
                    gm_l18 = t229;
                }
                {
                    for (;;) {
                        uint64_t t231 = gm_l19;
                        uint64_t t232 = gm_l18;
                        uint8_t t233 = (t231 < t232);
                        if (!t233) goto lb230;
                        {
                            uint64_t t236 = gm_l19;
                            uint64_t t239 = GM_LD(uint64_t, ((uint8_t *)&t236));
                            gm_l20 = t239;
                            {
                                void * t241 = gm_l0;
                                gm_tl874t2atnlh3 t242 = gm_l1;
                                gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 0));
                                uint64_t t244 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 24) + 8);
                                void * t245 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l17) + 24)));
                                uint64_t t246 = gm_l20;
                                uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r248; bool o249 = __builtin_mul_overflow(t246, t247, &r248);
                                if (o249) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 71);
                                uint64_t t250 = r248;
                                uint64_t t251 = (uintptr_t)t250;
                                uint64_t t252 = gm_l20;
                                uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                                if (o254) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 463, 83);
                                uint64_t t255 = r253;
                                uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                uint64_t r257; bool o258 = __builtin_mul_overflow(t255, t256, &r257);
                                if (o258) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 82);
                                uint64_t t259 = r257;
                                uint64_t t260 = (uintptr_t)t259;
                                if (t251 > t260 || t260 > (uintptr_t)t244) gm_trap_slice(t251, t260, t244, gm_srcl8pn2pjtmj6k, 463, 64);
                                gm_b16_8 t261;
                                GM_ST(void *, (uint8_t *)&t261, (void *)(((uint8_t *)t245 + t251 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(t260 - t251));
                                void * t262 = gm_l4;
                                gm_tcfa6l7reoosn t263;
                                gm_f_hash_val_nr2e99r((void *)&t263, (void *)NULL, cx, NULL, t241, t242, t243, t261, t262);
                                if (gm_unw.kind) {
                                    goto gm_ll234;
                                }
                                if (((((uint8_t *)&t263))[0] != 0)) {
                                    gm_tcfa6l7reoosn t264;
                                    GM_ST(uint8_t, (uint8_t *)&t264 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t264 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t264 + 4, 0);
                                    ((uint8_t *)&t264)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t264 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t263) + 8)));
                                    F.ret = t264;
                                    goto gm_fret;
                                }
                                gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 8));
                            }
                            goto me237;
mn238:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 463, 13);
me237:;
                        }
lc230:;
                        {
                            uint64_t t267 = gm_l19;
                            uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                            uint64_t t270 = r268;
                            gm_l19 = t270;
                        }
                    }
gm_ll234:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb230; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc230; }
                    }
                    goto gm_lad;
lb230:;
                }
            }
            goto me6;
mn198:;
ma15:;
            uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t272 != ((uint8_t)13ULL)) goto mn271;
            gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
            gm_l21 = t273;
            {
                void * t276 = gm_l0;
                void * t277 = GM_LD(void *, (((uint8_t *)t276) + 8));
                gm_t88rcucg1b7uk t278 = gm_l21;
                uint8_t t279 = gm_f_nullable_pointer_7qfmn6o(t277, GM_CONV(gm_csguankko4lmhd, t278));
                if (!t279) goto ie275;
                {
                    void * t281 = gm_l4;
                    void * t282 = gm_l0;
                    gm_b16_8 t283 = gm_l3;
                    gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
                    uint64_t t285 = gm_f_ptr_of_b77b4c9(t282, t284);
                    gm_f_hash_u64_32bripn(t281, t285);
                }
                goto ix275;
ie275:;
                void * t287 = gm_l0;
                gm_t88rcucg1b7uk t288 = gm_l2;
                gm_b16_8 t289 = gm_l3;
                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
                uint8_t t291 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t287, t288, t290);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t291) goto ie286;
                {
                    void * t293 = gm_l4;
                    gm_f_hash_u64_32bripn(t293, ((uint64_t)1ULL));
                }
                {
                    void * t294 = gm_l0;
                    gm_tl874t2atnlh3 t295 = gm_l1;
                    gm_t88rcucg1b7uk t296 = gm_l21;
                    void * t297 = gm_l0;
                    gm_t88rcucg1b7uk t298 = gm_l2;
                    gm_b16_8 t299 = gm_l3;
                    gm_b16_8 t300 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t297, t298, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t301 = gm_l4;
                    gm_tcfa6l7reoosn t302;
                    gm_f_hash_val_nr2e99r((void *)&t302, (void *)NULL, cx, NULL, t294, t295, t296, t300, t301);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t302))[0] != 0)) {
                        gm_tcfa6l7reoosn t303;
                        GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
                        ((uint8_t *)&t303)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t303 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t302) + 8)));
                        F.ret = t303;
                        goto gm_fret;
                    }
                    gm_b0_1 t304 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 8));
                }
                goto ix286;
ie286:;
                {
                    void * t306 = gm_l4;
                    gm_f_hash_u64_32bripn(t306, ((uint64_t)0ULL));
                }
ix286:;
ix275:;
            }
            goto me6;
mn271:;
ma16:;
            uint8_t t308 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t308 != ((uint8_t)14ULL)) goto mn307;
            gm_t88rcucg1b7uk t309 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
            gm_l22 = t309;
            gm_t88rcucg1b7uk t310 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
            gm_l23 = t310;
            {
                void * t312 = gm_l0;
                gm_t88rcucg1b7uk t313 = gm_l2;
                gm_b16_8 t314 = gm_l3;
                gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                uint8_t t316 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t312, t313, t315);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t316;
            }
            {
                void * t317 = gm_l4;
                uint64_t t318;
                uint8_t t320 = gm_l24;
                if (!t320) goto ie319;
                uint64_t t322 = ((uint64_t)1ULL);
                t318 = t322;
                goto ix319;
ie319:;
                uint64_t t324 = ((uint64_t)0ULL);
                t318 = t324;
ix319:;
                gm_f_hash_u64_32bripn(t317, t318);
            }
            {
                void * t325 = gm_l0;
                gm_tl874t2atnlh3 t326 = gm_l1;
                gm_t88rcucg1b7uk t327;
                uint8_t t329 = gm_l24;
                if (!t329) goto ie328;
                gm_t88rcucg1b7uk t331 = gm_l22;
                gm_t88rcucg1b7uk t332 = t331;
                t327 = t332;
                goto ix328;
ie328:;
                gm_t88rcucg1b7uk t334 = gm_l23;
                gm_t88rcucg1b7uk t335 = t334;
                t327 = t335;
ix328:;
                void * t336 = gm_l0;
                gm_t88rcucg1b7uk t337 = gm_l2;
                gm_b16_8 t338 = gm_l3;
                gm_b16_8 t339 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t336, t337, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t340 = gm_l4;
                gm_tcfa6l7reoosn t341;
                gm_f_hash_val_nr2e99r((void *)&t341, (void *)NULL, cx, NULL, t325, t326, t327, t339, t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t341))[0] != 0)) {
                    gm_tcfa6l7reoosn t342;
                    GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                    ((uint8_t *)&t342)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t342 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t341) + 8)));
                    F.ret = t342;
                    goto gm_fret;
                }
                gm_b0_1 t343 = GM_LD(gm_b0_1, (((uint8_t *)&t341) + 8));
            }
            goto me6;
mn307:;
ma17:;
            uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            if (t345 != ((uint8_t)17ULL)) goto mn344;
            {
                void * t348 = gm_l0;
                gm_t88rcucg1b7uk t349 = gm_l2;
                gm_tkn42u7na3t81 t350 = gm_f_bits_struct_ty_22ue20s(t348, GM_CONV(gm_csguankko4lmhd, t349));
                if (!((((uint8_t *)&t350))[4] != 0)) goto ie347;
                gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&t350));
                gm_l25 = t351;
                {
                    void * t353 = gm_l0;
                    gm_tl874t2atnlh3 t354 = gm_l1;
                    gm_t88rcucg1b7uk t355 = gm_l25;
                    gm_b16_8 t356 = gm_l3;
                    void * t357 = gm_l4;
                    gm_tcfa6l7reoosn t358;
                    gm_f_hash_val_nr2e99r((void *)&t358, (void *)NULL, cx, NULL, t353, t354, t355, t356, t357);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t358))[0] != 0)) {
                        gm_tcfa6l7reoosn t359;
                        GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
                        ((uint8_t *)&t359)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t359 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t358) + 8)));
                        F.ret = t359;
                        goto gm_fret;
                    }
                    gm_b0_1 t360 = GM_LD(gm_b0_1, (((uint8_t *)&t358) + 8));
                }
                {
                    gm_b0_1 t361;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t361);
                    goto gm_fdone;
                }
                goto ix347;
ie347:;
ix347:;
            }
            {
                void * t363 = gm_l0;
                gm_t88rcucg1b7uk t364 = gm_l2;
                gm_t57euh612ligr t365;
                gm_f_union_def_h8mvf4t((void *)&t365, t363, GM_CONV(gm_csguankko4lmhd, t364));
                if (!((((uint8_t *)&t365))[24] != 0)) goto ie362;
                gm_l26 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t365) + 0));
                {
                    void * t367 = gm_l0;
                    gm_t88rcucg1b7uk t368 = gm_l2;
                    gm_b16_8 t369 = gm_l3;
                    gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&t369));
                    gm_t9805qtonbsag t371 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t367, t368, t370);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t372;
                    if (((((uint8_t *)&t371))[4] != 0)) {
                        uint32_t t373 = GM_LD(uint32_t, (((uint8_t *)&t371) + 0));
                        t372 = t373;
                    } else {
                        void * t374 = gm_l0;
                        gm_tl874t2atnlh3 t375 = gm_l1;
                        gm_t88rcucg1b7uk t376 = gm_l2;
                        gm_b16_8 t377 = gm_l3;
                        gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
                        gm_t76123gfet73g t379;
                        gm_f_invalid_tag_1gif354((void *)&t379, (void *)NULL, cx, NULL, t374, t375, t376, t378);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t379);
                        goto gm_fdone;
                    }
                    gm_l27 = t372;
                }
                {
                    void * t380 = gm_l4;
                    uint32_t t381 = gm_l27;
                    uint64_t t382 = (uint64_t)t381;
                    gm_f_hash_u64_32bripn(t380, t382);
                }
                {
                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t385 = t384;
                    uint32_t t386 = gm_l27;
                    uint64_t t387 = (uint64_t)t386;
                    uint64_t t388 = gm_f_us_cuv01fp(t387);
                    uint64_t t389 = (uintptr_t)t388;
                    uint64_t t390 = GM_LD(uintptr_t, ((uint8_t *)&t385) + 8);
                    if (t389 >= (uintptr_t)(t390)) gm_trap_index(t389, t390, gm_srcl8pn2pjtmj6k, 488, 26);
                    if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t385)) + t389 * 72) + 16))[4] != 0)) goto ie383;
                    gm_t88rcucg1b7uk t391 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t385)) + t389 * 72) + 16));
                    gm_l28 = t391;
                    {
                        void * t393 = gm_l0;
                        gm_t88rcucg1b7uk t394 = gm_l2;
                        uint64_t t395 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t393, t394);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t396 = gm_f_us_cuv01fp(t395);
                        gm_l29 = t396;
                    }
                    {
                        void * t397 = gm_l0;
                        gm_tl874t2atnlh3 t398 = gm_l1;
                        gm_t88rcucg1b7uk t399 = gm_l28;
                        gm_b16_8 t400 = gm_l3;
                        uint64_t t401 = GM_LD(uintptr_t, ((uint8_t *)&t400) + 8);
                        void * t402 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t400)));
                        uint64_t t403 = gm_l29;
                        uint64_t t404 = (uintptr_t)t403;
                        uint64_t t405 = gm_l29;
                        void * t406 = gm_l0;
                        gm_t88rcucg1b7uk t407 = gm_l28;
                        uint64_t t408 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t406, t407);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t409 = gm_f_us_cuv01fp(t408);
                        uint64_t r410; bool o411 = __builtin_add_overflow(t405, t409, &r410);
                        if (o411) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 490, 53);
                        uint64_t t412 = r410;
                        uint64_t t413 = (uintptr_t)t412;
                        if (t404 > t413 || t413 > (uintptr_t)t401) gm_trap_slice(t404, t413, t401, gm_srcl8pn2pjtmj6k, 490, 46);
                        gm_b16_8 t414;
                        GM_ST(void *, (uint8_t *)&t414, (void *)(((uint8_t *)t402 + t404 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t414 + 8, (uintptr_t)(t413 - t404));
                        void * t415 = gm_l4;
                        gm_tcfa6l7reoosn t416;
                        gm_f_hash_val_nr2e99r((void *)&t416, (void *)NULL, cx, NULL, t397, t398, t399, t414, t415);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t416))[0] != 0)) {
                            gm_tcfa6l7reoosn t417;
                            GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
                            ((uint8_t *)&t417)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t417 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t416) + 8)));
                            F.ret = t417;
                            goto gm_fret;
                        }
                        gm_b0_1 t418 = GM_LD(gm_b0_1, (((uint8_t *)&t416) + 8));
                    }
                    goto ix383;
ie383:;
ix383:;
                }
                {
                    gm_b0_1 t419;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t419);
                    goto gm_fdone;
                }
                goto ix362;
ie362:;
ix362:;
            }
            {
                void * t420 = gm_l0;
                gm_t88rcucg1b7uk t421 = gm_l2;
                void * t422 = gm_f_adt_of_8k5vqud(t420, GM_CONV(gm_csguankko4lmhd, t421));
                void * t423;
                if ((GM_LD(void *, ((uint8_t *)&t422)) != 0)) {
                    void * t424 = GM_LD(void *, (((uint8_t *)&t422) + 0));
                    t423 = t424;
                } else {
                    void * t425 = gm_l0;
                    gm_tl874t2atnlh3 t426 = gm_l1;
                    void * t427 = gm_l0;
                    void * t428 = GM_LD(void *, (((uint8_t *)t427) + 8));
                    gm_t88rcucg1b7uk t429 = gm_l2;
                    gm_b16_8 t430 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t428, t429);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t431;
                    GM_ST(gm_b16_8, ((uint8_t *)&t431 + 0), t430);
                    gm_tabb710d9mq2e t432;
                    gm_f_format_h8e7ps1((void *)&t432, (void *)NULL, cx, NULL, t431);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 8, gm_srcl8pn2pjtmj6k, 494, 56);
                    gm_b16_8 t433 = GM_LD(gm_b16_8, (((uint8_t *)&t432) + 8));
                    gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
                    gm_t76123gfet73g t435;
                    gm_f_trap_qh84l9d((void *)&t435, (void *)NULL, cx, NULL, t425, t426, t434);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t435);
                    goto gm_fdone;
                }
                gm_l30 = t423;
            }
            {
                void * t437 = gm_l30;
                uint8_t t438 = GM_LD(uint8_t, ((((uint8_t *)t437) + 24) + 0));
                if (t438 != ((uint8_t)0ULL)) goto ie436;
                gm_tpautr4br7h3l t439 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t437) + 24) + 8));
                F.l31 = t439;
                {
                    {
                        gm_l33 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t442 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                        gm_l32 = t442;
                    }
                    {
                        for (;;) {
                            uint64_t t444 = gm_l33;
                            uint64_t t445 = gm_l32;
                            uint8_t t446 = (t444 < t445);
                            if (!t446) goto lb443;
                            {
                                uint64_t t449 = gm_l33;
                                uint64_t t452 = GM_LD(uint64_t, ((uint8_t *)&t449));
                                gm_l34 = t452;
                                {
                                    void * t454 = gm_l0;
                                    gm_t88rcucg1b7uk t455 = gm_l2;
                                    uint64_t t456 = gm_l34;
                                    uint32_t t457 = (uint32_t)t456;
                                    gm_tfbnciphf3261 t458 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t454, t455, t457);
                                    if (gm_unw.kind) {
                                        goto gm_ll447;
                                    }
                                    gm_l35 = t458;
                                }
                                {
                                    uint64_t t459 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                    uint64_t t460 = gm_f_us_cuv01fp(t459);
                                    gm_l36 = t460;
                                }
                                {
                                    void * t461 = gm_l0;
                                    gm_tl874t2atnlh3 t462 = gm_l1;
                                    gm_t88rcucg1b7uk t463 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    gm_b16_8 t464 = gm_l3;
                                    uint64_t t465 = GM_LD(uintptr_t, ((uint8_t *)&t464) + 8);
                                    void * t466 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t464)));
                                    uint64_t t467 = gm_l36;
                                    uint64_t t468 = (uintptr_t)t467;
                                    uint64_t t469 = gm_l36;
                                    void * t470 = gm_l0;
                                    gm_t88rcucg1b7uk t471 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 0));
                                    uint64_t t472 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t470, t471);
                                    if (gm_unw.kind) {
                                        goto gm_ll447;
                                    }
                                    uint64_t t473 = gm_f_us_cuv01fp(t472);
                                    uint64_t r474; bool o475 = __builtin_add_overflow(t469, t473, &r474);
                                    if (o475) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 499, 52);
                                    uint64_t t476 = r474;
                                    uint64_t t477 = (uintptr_t)t476;
                                    if (t468 > t477 || t477 > (uintptr_t)t465) gm_trap_slice(t468, t477, t465, gm_srcl8pn2pjtmj6k, 499, 45);
                                    gm_b16_8 t478;
                                    GM_ST(void *, (uint8_t *)&t478, (void *)(((uint8_t *)t466 + t468 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t478 + 8, (uintptr_t)(t477 - t468));
                                    void * t479 = gm_l4;
                                    gm_tcfa6l7reoosn t480;
                                    gm_f_hash_val_nr2e99r((void *)&t480, (void *)NULL, cx, NULL, t461, t462, t463, t478, t479);
                                    if (gm_unw.kind) {
                                        goto gm_ll447;
                                    }
                                    if (((((uint8_t *)&t480))[0] != 0)) {
                                        gm_tcfa6l7reoosn t481;
                                        GM_ST(uint8_t, (uint8_t *)&t481 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t481 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t481 + 4, 0);
                                        ((uint8_t *)&t481)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t481 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t480) + 8)));
                                        F.ret = t481;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t482 = GM_LD(gm_b0_1, (((uint8_t *)&t480) + 8));
                                }
                                goto me450;
mn451:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 496, 13);
me450:;
                            }
lc443:;
                            {
                                uint64_t t484 = gm_l33;
                                uint64_t r485; bool o486 = __builtin_add_overflow(t484, ((uint64_t)1ULL), &r485);
                                uint64_t t487 = r485;
                                gm_l33 = t487;
                            }
                        }
gm_ll447:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb443; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc443; }
                        }
                        goto gm_lad;
lb443:;
                    }
                }
                goto ix436;
ie436:;
                {
                    void * t489 = gm_l0;
                    gm_tl874t2atnlh3 t490 = gm_l1;
                    void * t491 = gm_l0;
                    void * t492 = GM_LD(void *, (((uint8_t *)t491) + 8));
                    gm_t88rcucg1b7uk t493 = gm_l2;
                    gm_b16_8 t494 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t492, t493);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t495;
                    GM_ST(gm_b16_8, ((uint8_t *)&t495 + 0), t494);
                    gm_tabb710d9mq2e t496;
                    gm_f_format_8k5eh95((void *)&t496, (void *)NULL, cx, NULL, t495);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t496))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t496) + 8, gm_srcl8pn2pjtmj6k, 495, 62);
                    gm_b16_8 t497 = GM_LD(gm_b16_8, (((uint8_t *)&t496) + 8));
                    gm_b16_8 t498 = GM_LD(gm_b16_8, ((uint8_t *)&t497));
                    gm_t76123gfet73g t499;
                    gm_f_trap_qh84l9d((void *)&t499, (void *)NULL, cx, NULL, t489, t490, t498);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t499);
                    goto gm_fdone;
                }
ix436:;
            }
            goto me6;
mn344:;
ma18:;
            void * t501 = gm_l0;
            gm_tl874t2atnlh3 t502 = gm_l1;
            void * t503 = gm_l0;
            void * t504 = GM_LD(void *, (((uint8_t *)t503) + 8));
            gm_t88rcucg1b7uk t505 = gm_l2;
            gm_b16_8 t506 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t504, t505);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t507;
            GM_ST(gm_b16_8, ((uint8_t *)&t507 + 0), t506);
            gm_tabb710d9mq2e t508;
            gm_f_format_8k5eh95((void *)&t508, (void *)NULL, cx, NULL, t507);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t508))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t508) + 8, gm_srcl8pn2pjtmj6k, 502, 35);
            gm_b16_8 t509 = GM_LD(gm_b16_8, (((uint8_t *)&t508) + 8));
            gm_b16_8 t510 = GM_LD(gm_b16_8, ((uint8_t *)&t509));
            gm_t76123gfet73g t511;
            gm_f_trap_qh84l9d((void *)&t511, (void *)NULL, cx, NULL, t501, t502, t510);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t511);
            goto gm_fdone;
            goto me6;
mn500:;
ma19:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 432, 5);
me6:;
        }
    }
    gm_b0_1 t512;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t512);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_int_text_vrjnuu2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        gm_tg5lql6itufk7 t8 = gm_f_int_info_ulcevdv(t6, GM_CONV(gm_csguankko4lmhd, t7));
        gm_ttovhfee8mf28 t9;
        if (((((uint8_t *)&t8))[8] != 0)) {
            gm_ttovhfee8mf28 t10 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        gm_l4 = t9;
    }
    {
        gm_b16_8 t13;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (!t15) goto ie14;
        unsigned __int128 t17 = gm_l3;
        __int128 t18;
        memset(&t18, 0, sizeof t18); memcpy(&t18, ((uint8_t *)&t17), sizeof t18 < 16 ? sizeof t18 : 16);
        gm_tpl3q6e8s9qo1 t19;
        GM_ST(__int128, ((uint8_t *)&t19 + 0), t18);
        gm_tabb710d9mq2e t20;
        gm_f_format_9qvncot((void *)&t20, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcl8pn2pjtmj6k, 185, 29);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        gm_b16_8 t22 = t21;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        t13 = t23;
        goto ix14;
ie14:;
        unsigned __int128 t25 = gm_l3;
        gm_tsn2omssg6cpf t26;
        GM_ST(unsigned __int128, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_ra7pont((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcl8pn2pjtmj6k, 185, 70);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
        t13 = t30;
ix14:;
        uint8_t t31 = 0;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_align_lavuopj F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)8ULL)) goto pa13;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l2 = t15;
        goto po12;
pa13:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t17 != ((uint8_t)9ULL)) goto pa16;
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l2 = t18;
        goto po12;
pa16:;
        goto mn11;
po12:;
        uint64_t t19;
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l2;
        uint64_t t23 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
        if (!t24) goto ie20;
        uint64_t t26 = ((uint64_t)1ULL);
        t19 = t26;
        goto ix20;
ie20:;
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l2;
        uint64_t t30 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t31 = t30;
        t19 = t31;
ix20:;
        uint8_t t32 = 0;
        F.ret = t19;
        goto gm_fret;
        goto me5;
mn11:;
ma7:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t34 != ((uint8_t)13ULL)) goto mn33;
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t4) + 8));
        gm_l4 = t35;
        void * t36 = gm_l0;
        gm_t88rcucg1b7uk t37 = gm_l4;
        uint64_t t38 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me5;
mn33:;
ma8:;
        uint8_t t41 = 0;
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
        goto me5;
mn40:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 174, 5);
me5:;
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
