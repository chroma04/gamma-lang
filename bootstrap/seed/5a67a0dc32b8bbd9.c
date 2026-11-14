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
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_tbfdi36rjh0el;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_tf5srh4mp4b3i f3; uint64_t f4; } gm_tml3m6miq9l2k;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_tgbgti82mf879 f0; uint8_t f1; uint8_t pe[7]; } gm_t8ldkt09h6492;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbfdi36rjh0el) == 40 && _Alignof(gm_tbfdi36rjh0el) == 8 && offsetof(gm_tbfdi36rjh0el, f0) == 0 && offsetof(gm_tbfdi36rjh0el, f1) == 8 && offsetof(gm_tbfdi36rjh0el, f2) == 16 && offsetof(gm_tbfdi36rjh0el, f3) == 24, "layout of `(u64, u64, u64, string)`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tml3m6miq9l2k) == 56 && _Alignof(gm_tml3m6miq9l2k) == 8 && offsetof(gm_tml3m6miq9l2k, f0) == 0 && offsetof(gm_tml3m6miq9l2k, f1) == 16 && offsetof(gm_tml3m6miq9l2k, f2) == 24 && offsetof(gm_tml3m6miq9l2k, f3) == 32 && offsetof(gm_tml3m6miq9l2k, f4) == 48, "layout of `Member`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
typedef struct { uint64_t f0; gm_tml3m6miq9l2k f1; } gm_ttos9gukqrqer;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint64_t f0; uint64_t f1; gm_tl572d7579hns f2; gm_b16_8 f3; } gm_tulpvg6gm4fqa;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8ldkt09h6492) == 40 && _Alignof(gm_t8ldkt09h6492) == 8 && offsetof(gm_t8ldkt09h6492, f0) == 0 && offsetof(gm_t8ldkt09h6492, f1) == 32, "layout of `?PayloadUnion`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { void * f0; gm_t4grvdhp6dkf1 f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_trqfvmjtt4sij f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_t4c7ipjj9s3fp f14; gm_trqfvmjtt4sij f15; gm_t4c7ipjj9s3fp f16; uint8_t f17; uint8_t p18[7]; gm_t4c7ipjj9s3fp f18; gm_tbpdf5ej15iof f19; } gm_trg1s7erfs461;
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_ttos9gukqrqer) == 64 && _Alignof(gm_ttos9gukqrqer) == 8 && offsetof(gm_ttos9gukqrqer, f0) == 0 && offsetof(gm_ttos9gukqrqer, f1) == 8, "layout of `(i: usize, x: Member)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tulpvg6gm4fqa) == 56 && _Alignof(gm_tulpvg6gm4fqa) == 8 && offsetof(gm_tulpvg6gm4fqa, f0) == 0 && offsetof(gm_tulpvg6gm4fqa, f1) == 8 && offsetof(gm_tulpvg6gm4fqa, f2) == 16 && offsetof(gm_tulpvg6gm4fqa, f3) == 40, "layout of `BlobDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_trg1s7erfs461) == 1128 && _Alignof(gm_trg1s7erfs461) == 8 && offsetof(gm_trg1s7erfs461, f0) == 0 && offsetof(gm_trg1s7erfs461, f1) == 8 && offsetof(gm_trg1s7erfs461, f2) == 80 && offsetof(gm_trg1s7erfs461, f3) == 120 && offsetof(gm_trg1s7erfs461, f4) == 176 && offsetof(gm_trg1s7erfs461, f5) == 232 && offsetof(gm_trg1s7erfs461, f6) == 288 && offsetof(gm_trg1s7erfs461, f7) == 328 && offsetof(gm_trg1s7erfs461, f8) == 368 && offsetof(gm_trg1s7erfs461, f9) == 408 && offsetof(gm_trg1s7erfs461, f10) == 464 && offsetof(gm_trg1s7erfs461, f11) == 504 && offsetof(gm_trg1s7erfs461, f12) == 560 && offsetof(gm_trg1s7erfs461, f13) == 600 && offsetof(gm_trg1s7erfs461, f14) == 656 && offsetof(gm_trg1s7erfs461, f15) == 712 && offsetof(gm_trg1s7erfs461, f16) == 752 && offsetof(gm_trg1s7erfs461, f17) == 808 && offsetof(gm_trg1s7erfs461, f18) == 816 && offsetof(gm_trg1s7erfs461, f19) == 872, "layout of `CTypes`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_of_gej99vn {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_assert_message_6bd9l6u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_blob_before_9p1c704 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_blob_kind_kp13j9k {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_blob_name_sjpa1ub {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* lanes */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedef_8ta3rbi {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* members */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedefs_4nfn4l8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij l2; /* sorted */
    gm_tulpvg6gm4fqa l5; /* b */
    gm_b16_8 ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_string_n7h27b3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_content_name_83mn5uf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cty_66ee0kf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ctypes_j6au0st {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_trg1s7erfs461 ret;
};
struct gm_fr_define_typed_m3mqpm3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_desc_0cag2kq {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* f */
    gm_trqfvmjtt4sij l16; /* parts */
    gm_tpautr4br7h3l l32; /* sd */
    gm_trqfvmjtt4sij l34; /* parts */
    gm_t0mglksrkfa5e l38; /* fd */
    gm_trqfvmjtt4sij l41; /* parts */
    gm_t0mglksrkfa5e l45; /* fd */
    gm_trqfvmjtt4sij l51; /* parts */
    gm_tckdobunaulee l54; /* v */
    gm_trqfvmjtt4sij l57; /* fields */
    gm_b16_8 ret;
};
struct gm_fr_descriptors_fnnm3h1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2g727l2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hm6i8si {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_glhj3bs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_cty_nfj8rcv {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_fields_array_tikg33t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_0h3rgr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_cty_s62af0i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ty_7nma1b7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_float_gthejtb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_signed_integer_ige9b74 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_fm7q9br {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_kind_rank_g1kdlst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_layouts_au8nfbt {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_t4grvdhp6dkf1 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_cfvjofe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nvrrj0t {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_cty_3kvgdd5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_mirror_rudvl6p {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l11; /* field_names */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t4c7ipjj9s3fp l15; /* taken */
    gm_trqfvmjtt4sij l23; /* members */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l34; /* attrs */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_make_typed_2bh0dpj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_trqfvmjtt4sij l7; /* named */
    gm_tpautr4br7h3l l22; /* def */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l31; /* payloads */
    gm_tckdobunaulee l34; /* v */
    gm_trqfvmjtt4sij l37; /* members */
    gm_trqfvmjtt4sij l38; /* named */
    gm_tml3m6miq9l2k l44; /* m */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_mirror_lcjegsf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_mirror_text_cr5muc9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_union_feshns2 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* members */
    gm_t8ldkt09h6492 ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d0271rf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_m55rf90 {
    gm_frame h;
    gm_tml3m6miq9l2k l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pmmlu39 {
    gm_frame h;
    gm_tulpvg6gm4fqa l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_q9fsb22 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2m6a5d4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e5frfkp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_p9v2h8s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_repr_53quqki {
    gm_frame h;
    gm_t5v8n6qrn4rh4 ret;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sort_by_55adsi9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_typed_stkcgvr {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_typedef_vpgf6u8 {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_glhj3bs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9117rt8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ef193cn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ef6t3dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gkjg5ld(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_io38sjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_irg2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_mfspujt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ms4f9aj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
__attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_onn3qvb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_p144a0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_udu99n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_shnrbt7(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_may_hold_pointers_iuorcah(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pointer_words_ljph5no(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pmmlu39(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_q9fsb22(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2m6a5d4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p9v2h8s(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1h8ac6i(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2h6ucjl(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2hik1mg(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3f2sd55(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3mmgun5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_712qp7n(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cscq5c8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1h65c6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fle5ipv(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pecnce5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_trmu0m5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern void *const gm_hlak2jvvbqg4[];
extern uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0f7epcithn0r[18];
extern const uint8_t gm_s1a1r1qe3ssot[9];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s58f7dj21gtf9[18];
extern const uint8_t gm_s6g2mpli2mu72[11];
extern const uint8_t gm_s6r94a2k1goos[9];
extern const uint8_t gm_s76d71nockpip[17];
extern const uint8_t gm_s7qhcf46f682d[21];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_sa9ktdbn49iui[16];
extern const uint8_t gm_sbec11r2rv4ti[23];
extern const uint8_t gm_sc0vbtqan26pc[18];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scttf7jtr4fn9[6];
extern const uint8_t gm_sdam23bqvafqn[18];
extern const uint8_t gm_sf1p9u1vovjso[5];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_shrq42dntcbrd[19];
extern const uint8_t gm_siblunnp4nahm[10];
extern const uint8_t gm_ska87i9f7924f[8];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slad86501rp5c[19];
extern const uint8_t gm_sm6ioruobq064[6];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smiavg3q7ucd9[8];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_so3qrsvnd4euf[12];
extern const uint8_t gm_so4g53vn8c2d4[8];
extern const uint8_t gm_spi406027g4jo[19];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sropo1941pvlt[16];
extern const uint8_t gm_st53sho4849cg[18];
extern const uint8_t gm_st9g0aublffth[18];
extern const uint8_t gm_std3g89n1j9ln[8];
extern const uint8_t gm_suggn3kp9dfmg[9];
extern const uint8_t gm_suuq26jjqhul4[7];
extern const uint8_t gm_suvf0cqih9u7i[17];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 816);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srckfibkpt681aa, 450, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_u86m8ir F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint32_t t4 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)6ULL)));
    if (t5) {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint64_t t8 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (t8 > ((uint64_t)16ULL));
        t1 = t9;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_content_name_83mn5uf F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* content */
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t6j6smj7qoctu t1;
    gm_b16_8 t2 = gm_l0;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t3, ((uint64_t)12ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_8n626pi((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcht195tfrfuqo, 86, 82);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    F.ret = t7;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_descriptors_fnnm3h1 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 216u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tfeab279c16rr t1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 288) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t3);
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 328) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t5);
    void * t6 = gm_l0;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t6) + 368) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 32, t7);
    gm_tabb710d9mq2e t8;
    gm_f_format_o1q76rb((void *)&t8, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srckfibkpt681aa, 627, 63);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2g727l2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hm6i8si F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_glhj3bs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_glhj3bs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_2m6a5d4(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 56)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 56);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)t1));
    void * t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)t3));
    uint8_t t5 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_0h3rgr6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_member_names_17el54k *)((&F.h)->up))->l1);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srckfibkpt681aa, 456, 37);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 456, 5);
me1:;
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_p217beu F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 32) + 24));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ty_7nma1b7 F;
    void * gm_l0; /* ty */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    uint8_t t4 = gm_l1;
    uint16_t t5 = gm_l2;
    gm_t88rcucg1b7uk t6 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_gthejtb F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t9 != ((uint8_t)3ULL)) goto mn8;
    t6 = ((uint8_t)1);
    goto me7;
mn8:;
    t6 = ((uint8_t)0);
    goto me7;
mn10:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 112, 53);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_eo6jkc5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_integer_8d4f9ek(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)5ULL)));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_ige9b74 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_signed_integer_s66djm7(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
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
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 82, 91);
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
inline __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_rank_g1kdlst F;
    gm_tl572d7579hns gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    t1 = ((uint8_t)0ULL);
    goto me2;
mn9:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    t1 = ((uint8_t)1ULL);
    goto me2;
mn11:;
ma5:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)2ULL)) goto mn13;
    t1 = ((uint8_t)2ULL);
    goto me2;
mn13:;
ma6:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t16 != ((uint8_t)3ULL)) goto mn15;
    t1 = ((uint8_t)3ULL);
    goto me2;
mn15:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 691, 39);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_layouts_au8nfbt F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy((uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 0, ((uint8_t *)a1), 64);
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_t4grvdhp6dkf1 *)gm_out) + 64, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt();
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
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_cfvjofe F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_2g727l2();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nvrrj0t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hm6i8si();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mirror_text_cr5muc9 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)t1) + 712) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_mirror_lcjegsf((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            gm_b16_8 t8 = gm_l2;
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
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d0271rf F;
    void * gm_l0; /* l */
    gm_th2lqbo77pvsc gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_ppl3r45();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_e5frfkp(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_th2lqbo77pvsc t21 = gm_l1;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t20 + t18 * 24), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_m55rf90 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_ab79bmt();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_ocvi9r0(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tml3m6miq9l2k t21 = GM_LD(gm_tml3m6miq9l2k, ((uint8_t *)a1));
        GM_ST(gm_tml3m6miq9l2k, ((uint8_t *)t20 + t18 * 56), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pmmlu39(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pmmlu39 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_hm6i8si();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_2m6a5d4(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tulpvg6gm4fqa t21 = GM_LD(gm_tulpvg6gm4fqa, ((uint8_t *)a1));
        GM_ST(gm_tulpvg6gm4fqa, ((uint8_t *)t20 + t18 * 56), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_q9fsb22(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_q9fsb22 F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_2g727l2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_p9v2h8s(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_t3877pk7lv748 t21 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a1));
        GM_ST(gm_t3877pk7lv748, ((uint8_t *)t20 + t18 * 24), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2m6a5d4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2m6a5d4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3cp9o8d F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_e5frfkp F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p9v2h8s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_p9v2h8s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_55adsi9 F;
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
        gm_f_introsort_shnrbt7(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
        gm_f_introsort_r6p6ckl(t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_members_mrojmjf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t1, t2, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_of_gej99vn F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l2 = t9;
        {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 1088) + 0) + 8);
            if (t13 >= (uintptr_t)(t16)) gm_trap_index(t13, t16, gm_srckfibkpt681aa, 130, 13);
            void * t17 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 1088) + 0)) + t13 * 184);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_srckfibkpt681aa, 129, 42);
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_assert_message_6bd9l6u F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 237u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srckfibkpt681aa, 734, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
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
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 1));
                    uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t16));
                    gm_l4 = t19;
                    {
                        uint8_t t21 = gm_l4;
                        uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t26 == ((uint8_t)34ULL))) goto pa25;
                        goto po24;
pa25:;
                        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if (!(t28 == ((uint8_t)92ULL))) goto pa27;
                        goto po24;
pa27:;
                        goto mn23;
po24:;
                        {
                            void * t30 = (void *)((uint8_t *)&F.l1);
                            gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t30, ((uint8_t)92ULL));
                            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srckfibkpt681aa, 738, 17);
                            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        }
                        {
                            void * t33 = (void *)((uint8_t *)&F.l1);
                            uint8_t t34 = gm_l4;
                            gm_tftb1odc739t5 t35 = gm_f_push_t83rg2m(t33, t34);
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srckfibkpt681aa, 739, 17);
                            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
                        }
                        goto me22;
mn23:;
                        uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t21));
                        if ((t38 < ((uint8_t)32ULL)) || (t38 > ((uint8_t)126ULL))) goto mn37;
                        void * t39 = (void *)((uint8_t *)&F.l1);
                        uint8_t t40 = gm_l4;
                        gm_tftb1odc739t5 t41 = gm_f_push_t83rg2m(t39, t40);
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srckfibkpt681aa, 741, 26);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        goto me22;
mn37:;
                        goto me22;
mn43:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 736, 9);
me22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 735, 5);
me17:;
                }
lc7:;
                {
                    uint64_t t46 = gm_l3;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l3 = t49;
                }
            }
lb7:;
        }
    }
    {
        void * t50 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t51 = gm_f_push_t83rg2m(t50, ((uint8_t)34ULL));
        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srckfibkpt681aa, 745, 5);
        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
    }
    {
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        uint8_t t55 = 0;
        F.ret = t54;
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
extern __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_blob_before_9p1c704 F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    uint8_t gm_l2; /* rx */
    uint8_t gm_l3; /* ry */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 0));
        uint8_t t7 = (!(t4 == t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 0));
            void * t11 = gm_l1;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 0));
            uint8_t t13 = (t10 < t12);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
        void * t18 = gm_l1;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
        uint8_t t20 = (!(t17 == t19));
        if (!t20) goto ie15;
        {
            void * t22 = gm_l0;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 8));
            void * t24 = gm_l1;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (t23 < t25);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        void * t28 = gm_l0;
        uint8_t t29 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t28) + 16)));
        gm_l2 = t29;
    }
    {
        void * t30 = gm_l1;
        uint8_t t31 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t30) + 16)));
        gm_l3 = t31;
    }
    {
        uint8_t t33 = gm_l2;
        uint8_t t34 = gm_l3;
        uint8_t t35 = (!(t33 == t34));
        if (!t35) goto ie32;
        {
            uint8_t t37 = gm_l2;
            uint8_t t38 = gm_l3;
            uint8_t t39 = (t37 < t38);
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t42 = gm_l0;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 16) + 0));
        if (t43 != ((uint8_t)3ULL)) goto ie41;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((((uint8_t *)t42) + 16) + 8));
        gm_l4 = t44;
        void * t45 = gm_l1;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 16) + 0));
        if (t46 != ((uint8_t)3ULL)) goto ie41;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + 16) + 8));
        gm_l5 = t47;
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint64_t t52 = (t50 < t51 ? t50 : t51);
                gm_l6 = t52;
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l7;
                    uint64_t t55 = gm_l6;
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l7;
                        uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)&t59));
                        gm_l8 = t62;
                        {
                            uint64_t t65 = gm_l8;
                            uint64_t t66 = (uintptr_t)t65;
                            uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srckfibkpt681aa, 684, 16);
                            uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t66 * 1));
                            uint64_t t69 = gm_l8;
                            uint64_t t70 = (uintptr_t)t69;
                            uint64_t t71 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srckfibkpt681aa, 684, 28);
                            uint8_t t72 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t70 * 1));
                            uint8_t t73 = (!(t68 == t72));
                            if (!t73) goto ie64;
                            {
                                uint64_t t75 = gm_l8;
                                uint64_t t76 = (uintptr_t)t75;
                                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srckfibkpt681aa, 684, 42);
                                uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t76 * 1));
                                uint8_t t79 = 0;
                                F.ret = t78;
                                goto gm_fret;
                            }
                            goto ix64;
ie64:;
ix64:;
                        }
                        goto me60;
mn61:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 683, 9);
me60:;
                    }
lc53:;
                    {
                        uint64_t t81 = gm_l7;
                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                        uint64_t t84 = r82;
                        gm_l7 = t84;
                    }
                }
lb53:;
            }
        }
        {
            uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
            uint8_t t87 = (t85 < t86);
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        uint8_t t89 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_blob_kind_kp13j9k F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* s */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* pb */
    gm_b16_8 gm_l5; /* lanes */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 0) + 0));
        gm_l4 = t3;
    }
    {
        uint8_t t5 = 0;
        uint8_t t6 = 0;
        uint8_t t7 = 0;
        void * t8 = gm_l0;
        uint8_t t9 = GM_LD(uint8_t, (((((uint8_t *)t8) + 8) + 0) + 56));
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = gm_l4;
            uint8_t t13 = (t11 < t12);
            t7 = t13;
        } else t7 = 1;
        if (!t7) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = gm_l4;
            uint8_t t16 = (t14 < t15);
            t6 = t16;
        } else t6 = 1;
        if (!t6) {
            uint64_t t17 = gm_l2;
            uint64_t t18 = gm_l4;
            if (t18 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 701, 54);
            uint64_t t19 = (uint64_t)(t17 % t18);
            uint8_t t20 = (!(t19 == ((uint64_t)0ULL)));
            t5 = t20;
        } else t5 = 1;
        if (!t5) goto ie4;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)0ULL));
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 8);
        gm_t88rcucg1b7uk t25 = gm_l1;
        uint8_t t26 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t27 = (uint8_t)!t26;
        if (!t27) goto ie22;
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)2ULL));
            goto gm_fdone;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 8);
        gm_t88rcucg1b7uk t32 = gm_l1;
        gm_th2lqbo77pvsc t33;
        gm_f_pointer_words_ljph5no((void *)&t33, (void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t33))[16] != 0)) goto ie29;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_l5 = t34;
        {
            gm_b16_8 t36 = gm_l5;
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)3ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, t37);
            goto gm_fdone;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 0, ((uint8_t)1ULL));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2) {
    struct gm_fr_blob_name_sjpa1ub F;
    uint64_t gm_l0; /* size */
    uint64_t gm_l1; /* align */
    gm_tl572d7579hns gm_l2; /* kind */
    gm_b16_8 gm_l3; /* ptrs */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma3; case ((uint8_t)3ULL): goto ma4; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    uint64_t t10 = gm_l0;
    uint64_t t11 = gm_l1;
    gm_t2lqu8jsj511d t12;
    GM_ST(uint64_t, ((uint8_t *)&t12 + 0), t10);
    GM_ST(uint64_t, ((uint8_t *)&t12 + 8), t11);
    gm_tabb710d9mq2e t13;
    gm_f_format_ef6t3dr((void *)&t13, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_srckfibkpt681aa, 638, 15);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
    t1 = t15;
    goto me2;
mn8:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
    if (t17 != ((uint8_t)3ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_l3 = t18;
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t21 = gm_l3;
            gm_l5 = t21;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l6;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t29 * 1));
                    uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t31));
                    gm_l7 = t34;
                    {
                        void * t36 = (void *)((uint8_t *)&F.l4);
                        uint8_t t37;
                        uint8_t t39 = gm_l7;
                        if (!t39) goto ie38;
                        uint8_t t41 = ((uint8_t)112ULL);
                        t37 = t41;
                        goto ix38;
ie38:;
                        uint8_t t43 = ((uint8_t)119ULL);
                        t37 = t43;
ix38:;
                        gm_tftb1odc739t5 t44 = gm_f_push_t83rg2m(t36, t37);
                        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srckfibkpt681aa, 641, 25);
                        gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 641, 9);
me32:;
                }
lc22:;
                {
                    uint64_t t47 = gm_l6;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l6 = t50;
                }
            }
lb22:;
        }
    }
    gm_tbccvikg6b96e t51;
    uint64_t t52 = gm_l0;
    GM_ST(uint64_t, (uint8_t *)&t51 + 0, t52);
    uint64_t t53 = gm_l1;
    GM_ST(uint64_t, (uint8_t *)&t51 + 8, t53);
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
    GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t54);
    gm_tabb710d9mq2e t55;
    gm_f_format_udu99n6((void *)&t55, (void *)NULL, cx, NULL, &t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_srckfibkpt681aa, 642, 9);
    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
    gm_b16_8 t57 = t56;
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
    t1 = t58;
    goto me2;
mn16:;
ma5:;
    uint64_t t60 = gm_l0;
    uint64_t t61 = gm_l1;
    gm_t2lqu8jsj511d t62;
    GM_ST(uint64_t, ((uint8_t *)&t62 + 0), t60);
    GM_ST(uint64_t, ((uint8_t *)&t62 + 8), t61);
    gm_tabb710d9mq2e t63;
    gm_f_format_f6fn313((void *)&t63, (void *)NULL, cx, NULL, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srckfibkpt681aa, 644, 10);
    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
    t1 = t65;
    goto me2;
mn59:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 637, 81);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4) {
    struct gm_fr_blob_typedef_8ta3rbi F;
    void * gm_l0; /* ty */
    uint64_t gm_l1; /* s */
    uint64_t gm_l2; /* a */
    gm_tl572d7579hns gm_l3; /* kind */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l5; /* pb */
    gm_b16_8 gm_l6; /* ptrs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l11; /* p */
    GM_STACK_CHECK(&F, 1006u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 0) + 0));
        gm_l5 = t3;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma9; }
ma5:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        gm_tbccvikg6b96e t13;
        uint64_t t14 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t13 + 0, t14);
        uint64_t t15 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t13 + 8, t15);
        gm_b16_8 t16 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 16, t16);
        gm_tabb710d9mq2e t17;
        gm_f_format_a4r7k06((void *)&t17, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srckfibkpt681aa, 714, 26);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        uint8_t t20 = 0;
        F.ret = t19;
        goto gm_fret;
        goto me4;
mn11:;
ma6:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        gm_tbfdi36rjh0el t23;
        uint64_t t24 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t23 + 0, t24);
        uint64_t t25 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&t23 + 8, t25);
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l5;
        if (t27 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 715, 123);
        uint64_t t28 = (uint64_t)(t26 / t27);
        GM_ST(uint64_t, (uint8_t *)&t23 + 16, t28);
        gm_b16_8 t29 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 24, t29);
        gm_tabb710d9mq2e t30;
        gm_f_format_c19sabv((void *)&t30, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srckfibkpt681aa, 715, 29);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me4;
mn21:;
ma7:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t35 != ((uint8_t)2ULL)) goto mn34;
        gm_tbccvikg6b96e t36;
        uint64_t t37 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t36 + 0, t37);
        uint64_t t38 = gm_l1;
        uint64_t t39 = gm_l5;
        if (t39 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 716, 103);
        uint64_t t40 = (uint64_t)(t38 / t39);
        GM_ST(uint64_t, (uint8_t *)&t36 + 8, t40);
        gm_b16_8 t41 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t41);
        gm_tabb710d9mq2e t42;
        gm_f_format_s9gvae7((void *)&t42, (void *)NULL, cx, NULL, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srckfibkpt681aa, 716, 26);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        uint8_t t45 = 0;
        F.ret = t44;
        goto gm_fret;
        goto me4;
mn34:;
ma8:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
        if (t47 != ((uint8_t)3ULL)) goto mn46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 8));
        gm_l6 = t48;
        {
            gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t51 = gm_l6;
                gm_l8 = t51;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t53 = gm_l9;
                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t55 = (t53 < t54);
                    if (!t55) goto lb52;
                    {
                        uint64_t t58 = gm_l9;
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t60 * 1));
                        gm_tf5srh4mp4b3i t63;
                        GM_ST(uint8_t, (uint8_t *)&t63 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t63 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t63 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t58);
                        GM_ST(uint8_t, ((uint8_t *)&t63 + 8), t62);
                        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&t63) + 0));
                        gm_l10 = t66;
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t63) + 8));
                        gm_l11 = t67;
                        {
                            uint64_t t70 = gm_l10;
                            uint8_t t71 = (t70 > ((uint64_t)0ULL));
                            if (!t71) goto ie69;
                            {
                                void * t73 = (void *)((uint8_t *)&F.l7);
                                gm_tftb1odc739t5 t74 = gm_f_push_t83rg2m(t73, ((uint8_t)32ULL));
                                if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srckfibkpt681aa, 720, 28);
                                gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
                            }
                            goto ix69;
ie69:;
ix69:;
                        }
                        {
                            uint64_t t77 = gm_l10;
                            uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
                            if (!t78) goto ie76;
                            {
                                void * t80 = (void *)((uint8_t *)&F.l7);
                                uint64_t t81 = gm_l2;
                                gm_t4ce0jd3tdro4 t82;
                                GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t81);
                                gm_tftb1odc739t5 t83 = gm_f_write_2h6ucjl(t80, t82);
                                if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srckfibkpt681aa, 721, 29);
                                gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
                            }
                            goto ix76;
ie76:;
ix76:;
                        }
                        {
                            void * t85 = (void *)((uint8_t *)&F.l7);
                            gm_t3877pk7lv748 t86;
                            gm_b16_8 t87;
                            uint8_t t89 = gm_l11;
                            if (!t89) goto ie88;
                            gm_b16_8 t91;
                            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s44ek1gecbajs));
                            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(6));
                            gm_b16_8 t92 = t91;
                            t87 = t92;
                            goto ix88;
ie88:;
                            gm_b16_8 t94;
                            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_siblunnp4nahm));
                            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(9));
                            gm_b16_8 t95 = t94;
                            t87 = t95;
ix88:;
                            GM_ST(gm_b16_8, (uint8_t *)&t86 + 0, t87);
                            uint64_t t96 = gm_l10;
                            GM_ST(uint64_t, (uint8_t *)&t86 + 16, t96);
                            gm_tftb1odc739t5 t97 = gm_f_write_2hik1mg(t85, &t86);
                            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srckfibkpt681aa, 722, 17);
                            gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                        }
                        goto me64;
mn65:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 719, 13);
me64:;
                    }
lc52:;
                    {
                        uint64_t t100 = gm_l9;
                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                        uint64_t t103 = r101;
                        gm_l9 = t103;
                    }
                }
lb52:;
            }
        }
        {
            gm_t6j6smj7qoctu t104;
            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
            gm_b16_8 t106 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t104 + 16, t106);
            gm_tabb710d9mq2e t107;
            gm_f_format_tjkla39((void *)&t107, (void *)NULL, cx, NULL, &t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srckfibkpt681aa, 724, 20);
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            uint8_t t110 = 0;
            F.ret = t109;
            goto gm_fret;
        }
        goto me4;
mn46:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 713, 5);
me4:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_blob_typedefs_4nfn4l8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 392u);
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
        gm_f_list_nvrrj0t((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 80) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_glhj3bs(t2, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srckfibkpt681aa, 469, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_closure t9 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_blob_before_9p1c704)), NULL, cx});
        gm_f_sort_by_55adsi9(t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_l3 = t11;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t13 = gm_l4;
                uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t15 = (t13 < t14);
                if (!t15) goto lb12;
                {
                    uint64_t t18 = gm_l4;
                    uint64_t t19 = (uintptr_t)t18;
                    uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tulpvg6gm4fqa t23 = GM_LD(gm_tulpvg6gm4fqa, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 56));
                    F.l5 = t23;
                    {
                        void * t25 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_tftb1odc739t5 t27 = gm_f_extend_0cs85gs(t25, t26);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srckfibkpt681aa, 471, 29);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 471, 5);
me21:;
                }
lc12:;
                {
                    uint64_t t30 = gm_l4;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l4 = t33;
                }
            }
lb12:;
        }
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        void * t35 = gm_l0;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((((uint8_t *)t35) + 560) + 0));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_tftb1odc739t5 t38 = gm_f_extend_0cs85gs(t34, t37);
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srckfibkpt681aa, 472, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        uint8_t t42 = 0;
        F.ret = t41;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_c_string_n7h27b3 F;
    gm_b16_8 gm_l0; /* bytes */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 308u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)2ULL), &r4);
        if (o5) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 752, 17);
        uint64_t t6 = r4;
        gm_tftb1odc739t5 t7 = gm_f_reserve_d1qo8mf(t2, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srckfibkpt681aa, 752, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t10 = gm_f_push_t83rg2m(t9, ((uint8_t)34ULL));
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srckfibkpt681aa, 753, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        {
            gm_b16_8 t13 = gm_l0;
            gm_l2 = t13;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l3;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t21 * 1));
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)&t23));
                    gm_l4 = t26;
                    {
                        uint8_t t28 = gm_l4;
                        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t28));
                        if (!(t31 == ((uint8_t)34ULL))) goto mn30;
                        void * t32 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t33;
                        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t34 = gm_f_extend_0cs85gs(t32, t33);
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srckfibkpt681aa, 756, 20);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                        goto me29;
mn30:;
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t28));
                        if (!(t37 == ((uint8_t)92ULL))) goto mn36;
                        void * t38 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t38, t39);
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srckfibkpt681aa, 757, 21);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        goto me29;
mn36:;
                        uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&t28));
                        if ((t43 < ((uint8_t)32ULL)) || (t43 > ((uint8_t)126ULL))) goto mn42;
                        uint8_t t44 = gm_l4;
                        uint8_t t45 = (!(t44 == ((uint8_t)63ULL)));
                        if (!t45) goto mn42;
                        void * t46 = (void *)((uint8_t *)&F.l1);
                        uint8_t t47 = gm_l4;
                        gm_tftb1odc739t5 t48 = gm_f_push_t83rg2m(t46, t47);
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srckfibkpt681aa, 758, 44);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        goto me29;
mn42:;
                        void * t51 = (void *)((uint8_t *)&F.l1);
                        uint8_t t52 = gm_l4;
                        gm_t9qkh0rsooc8d t53;
                        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), t52);
                        gm_tftb1odc739t5 t54 = gm_f_write_fle5ipv(t51, t53);
                        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srckfibkpt681aa, 759, 18);
                        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
                        goto me29;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 755, 9);
me29:;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 754, 5);
me24:;
                }
lc14:;
                {
                    uint64_t t57 = gm_l3;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l3 = t60;
                }
            }
lb14:;
        }
    }
    {
        void * t61 = (void *)((uint8_t *)&F.l1);
        gm_tftb1odc739t5 t62 = gm_f_push_t83rg2m(t61, ((uint8_t)34ULL));
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srckfibkpt681aa, 762, 5);
        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
    }
    {
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        uint8_t t66 = 0;
        F.ret = t65;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_cty_66ee0kf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* c */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        uint64_t t5 = gm_l2;
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 464) + 0) + 8);
        uint8_t t8 = (t5 < t7);
        if (!t8) goto ie4;
        {
            uint64_t t11 = gm_l2;
            uint64_t t12 = (uintptr_t)t11;
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 464) + 0) + 8);
            if (t12 >= (uintptr_t)(t14)) gm_trap_index(t12, t14, gm_srckfibkpt681aa, 160, 17);
            if (!((((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 464) + 0)) + t12 * 24))[16] != 0)) goto ie10;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t13) + 464) + 0)) + t12 * 24));
            gm_l3 = t15;
            {
                gm_b16_8 t17 = gm_l3;
                uint8_t t18 = 0;
                F.ret = t17;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_f_make_cty_3kvgdd5((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t21;
    }
    {
        for (;;) {
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 464) + 0) + 8);
            uint64_t t25 = gm_l2;
            uint8_t t26 = (t24 <= t25);
            if (!t26) goto lb22;
            {
                void * t29 = gm_l0;
                void * t30 = (void *)(((uint8_t *)t29) + 464);
                gm_th2lqbo77pvsc t31;
                memset(&t31, 0, sizeof t31);
                gm_tftb1odc739t5 t32 = gm_f_push_d0271rf(t30, &t31);
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srckfibkpt681aa, 163, 41);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
lc22:;
        }
lb22:;
    }
    {
        uint64_t t34 = gm_l2;
        uint64_t t35 = (uintptr_t)t34;
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, ((((uint8_t *)t36) + 464) + 0) + 8);
        if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srckfibkpt681aa, 164, 5);
        gm_b16_8 t38 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 464) + 0)) + t35 * 24)) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 464) + 0)) + t35 * 24)) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 464) + 0)) + t35 * 24)) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 464) + 0)) + t35 * 24)))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t36) + 464) + 0)) + t35 * 24)) + 0, t38);
    }
    {
        gm_b16_8 t39 = gm_l4;
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
inline __attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_ctypes_j6au0st F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 1136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memset((uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 809, 0, 7);
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t4grvdhp6dkf1 t3;
    gm_f_layouts_au8nfbt((void *)&t3, t2, &(*(gm_t0rfe1ou14h21 *)((uint8_t *)&F.l1)));
    gm_trqfvmjtt4sij t4;
    gm_f_list_nvrrj0t((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_cuh4fs9((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_tl7apid((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t7;
    gm_f_new_namer_5q88o9g((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t8;
    gm_f_list_uvfu1k1((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t9;
    gm_f_list_uvfu1k1((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t10;
    gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t11;
    gm_f_hash_map_1s9eugt((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t12;
    gm_f_list_0c2v515((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t13;
    gm_f_hash_map_p3t5tj8((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t14;
    gm_f_list_uvfu1k1((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t15;
    gm_f_hash_map_1s9eugt((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t16;
    gm_f_hash_map_p3t5tj8((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t17;
    gm_f_list_uvfu1k1((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t18;
    gm_f_hash_map_cuh4fs9((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t19;
    gm_f_hash_map_cuh4fs9((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbpdf5ej15iof t20;
    gm_f_views_bc1b3r4((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(void *, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 0, t1);
    GM_ST(gm_t4grvdhp6dkf1, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 8, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 80, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 120, t5);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 176, t6);
    GM_ST(gm_ttiitul0263ld, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 232, t7);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 288, t8);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 328, t9);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 368, t10);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 408, t11);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 464, t12);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 504, t13);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 560, t14);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 600, t15);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 656, t16);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 712, t17);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 752, t18);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 808, ((uint8_t)0));
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 816, t19);
    GM_ST(gm_tbpdf5ej15iof, (uint8_t *)&(*(gm_trg1s7erfs461 *)gm_out) + 872, t20);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_define_typed_m3mqpm3 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* members */
    gm_b16_8 gm_l3; /* named */
    uint64_t gm_l4; /* natural */
    uint64_t gm_l5; /* size */
    uint64_t gm_l6; /* al */
    gm_b16_8 gm_l7; /* attr */
    gm_b16_8 gm_l8; /* shape */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* name */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t3877pk7lv748 gm_l13; /* m */
    GM_STACK_CHECK(&F, 1334u);
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
        uint64_t t4 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint64_t t7 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t7;
    }
    {
        gm_b16_8 t8;
        uint64_t t10 = gm_l6;
        uint64_t t11 = gm_l4;
        uint8_t t12 = (t10 > t11);
        if (!t12) goto ie9;
        uint64_t t14 = gm_l6;
        gm_t4ce0jd3tdro4 t15;
        GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t14);
        gm_tabb710d9mq2e t16;
        gm_f_format_qc4s0m5((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srckfibkpt681aa, 313, 38);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        t8 = t19;
        goto ix9;
ie9:;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(0));
        gm_b16_8 t22 = t21;
        t8 = t22;
ix9:;
        gm_l7 = t8;
    }
    {
        gm_t6j6smj7qoctu t23;
        gm_b16_8 t24 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
        gm_b16_8 t25 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_8n626pi((void *)&t26, (void *)NULL, cx, NULL, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srckfibkpt681aa, 314, 14);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_l8 = t27;
    }
    {
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 504);
        gm_b16_8 t31 = gm_l8;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_th2lqbo77pvsc t33;
        gm_f_get_e3joqcc((void *)&t33, t30, t32);
        if (!((((uint8_t *)&t33))[16] != 0)) goto ie28;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_l9 = t34;
        {
            gm_b16_8 t36 = gm_l9;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sf1p9u1vovjso));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
        gm_b16_8 t39 = gm_l8;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
        gm_b16_8 t41 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t38, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t41;
    }
    {
        void * t42 = gm_l0;
        void * t43 = (void *)(((uint8_t *)t42) + 560);
        gm_tfeab279c16rr t44;
        gm_b16_8 t45 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
        gm_b16_8 t46 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 16, t46);
        gm_b16_8 t47 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 32, t47);
        gm_tftb1odc739t5 t48 = gm_f_write_f1h65c6(t43, &t44);
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srckfibkpt681aa, 317, 5);
        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
    }
    {
        void * t50 = gm_l0;
        void * t51 = (void *)(((uint8_t *)t50) + 560);
        gm_thddbqb6snabm t52;
        gm_b16_8 t53 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
        uint64_t t54 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&t52 + 16, t54);
        gm_b16_8 t55 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 24, t55);
        uint64_t t56 = gm_l6;
        GM_ST(uint64_t, (uint8_t *)&t52 + 40, t56);
        gm_tftb1odc739t5 t57 = gm_f_write_cscq5c8(t51, &t52);
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srckfibkpt681aa, 318, 5);
        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
    }
    {
        {
            gm_b16_8 t60 = gm_l3;
            gm_l11 = t60;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t62 = gm_l12;
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t64 = (t62 < t63);
                if (!t64) goto lb61;
                {
                    uint64_t t67 = gm_l12;
                    uint64_t t68 = (uintptr_t)t67;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t3877pk7lv748 t72 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t68 * 24));
                    gm_l13 = t72;
                    {
                        void * t74 = gm_l0;
                        void * t75 = (void *)(((uint8_t *)t74) + 560);
                        gm_tftksr4op114e t76;
                        gm_b16_8 t77 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t77);
                        gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t76 + 16, t78);
                        uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 16));
                        GM_ST(uint64_t, (uint8_t *)&t76 + 32, t79);
                        gm_tftb1odc739t5 t80 = gm_f_write_3f2sd55(t75, &t76);
                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srckfibkpt681aa, 319, 22);
                        gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
                    }
                    goto me70;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 319, 5);
me70:;
                }
lc61:;
                {
                    uint64_t t83 = gm_l12;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l12 = t86;
                }
            }
lb61:;
        }
    }
    {
        void * t87 = gm_l0;
        void * t88 = (void *)(((uint8_t *)t87) + 560);
        void * t89 = gm_l0;
        gm_t88rcucg1b7uk t90 = gm_l1;
        gm_b16_8 t91 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t92;
        GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
        gm_tabb710d9mq2e t93;
        gm_f_format_1cgbgnn((void *)&t93, (void *)NULL, cx, NULL, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 8, gm_srckfibkpt681aa, 320, 52);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
        gm_b16_8 t96 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t97;
        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
        gm_tftb1odc739t5 t98 = gm_f_write_pecnce5(t88, t97);
        if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srckfibkpt681aa, 320, 5);
        gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
    }
    {
        void * t100 = gm_l0;
        void * t101 = (void *)(((uint8_t *)t100) + 504);
        gm_b16_8 t102 = gm_l8;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        gm_b16_8 t104 = gm_l10;
        gm_tftb1odc739t5 t105 = gm_f_put_7shfu9u(t101, t103, t104);
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srckfibkpt681aa, 321, 5);
        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
    }
    {
        gm_b16_8 t107 = gm_l10;
        uint8_t t108 = 0;
        F.ret = t107;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_desc_0cag2kq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* i */
    gm_b16_8 gm_l3; /* i */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* bits */
    uint16_t gm_l7; /* bits */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* e */
    uint64_t gm_l10; /* len */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* names */
    gm_b16_8 gm_l15; /* offs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_t88rcucg1b7uk gm_l20; /* et */
    gm_b16_8 gm_l21; /* d */
    gm_b16_8 gm_l22; /* name */
    gm_t88rcucg1b7uk gm_l23; /* inner */
    gm_b16_8 gm_l24; /* e */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* e */
    gm_b16_8 gm_l28; /* o */
    gm_b16_8 gm_l29; /* offs */
    uint32_t gm_l30; /* id */
    void * gm_l31; /* def */
    gm_b16_8 gm_l33; /* offs */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* d */
    gm_tcmkompbl09il gm_l40; /* bl */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* k */
    gm_tov4mtrfp5e7v gm_l46; /* bf */
    gm_b16_8 gm_l47; /* d */
    gm_t88rcucg1b7uk gm_l48; /* tag */
    gm_b16_8 gm_l49; /* variants */
    gm_b16_8 gm_l50; /* offs */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l55; /* pt */
    gm_b16_8 gm_l56; /* p */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    gm_b16_8 gm_l60; /* x */
    GM_STACK_CHECK(&F, 7268u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 176);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tabb710d9mq2e t11;
            gm_f_format_h6b2ofo((void *)&t11, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srckfibkpt681aa, 482, 44);
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
        void * t15 = gm_l0;
        void * t16 = (void *)(((uint8_t *)t15) + 232);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sm6ioruobq064));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l1;
        gm_b16_8 t20 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t21 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t16, t17, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = (void *)(((uint8_t *)t22) + 176);
        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_b16_8 t25 = gm_l3;
        gm_tftb1odc739t5 t26 = gm_f_put_po09vud(t23, t24, t25);
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srckfibkpt681aa, 484, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 288);
        gm_b16_8 t30 = gm_l3;
        gm_tnpifv20irnph t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
        gm_tftb1odc739t5 t32 = gm_f_write_trmu0m5(t29, t31);
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srckfibkpt681aa, 485, 5);
        gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l4);
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l1;
        uint64_t t37 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t38;
        GM_ST(uint64_t, ((uint8_t *)&t38 + 0), t37);
        gm_tabb710d9mq2e t39;
        gm_f_format_jpvhgp3((void *)&t39, (void *)NULL, cx, NULL, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srckfibkpt681aa, 487, 12);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_tftb1odc739t5 t42 = gm_f_push_3ef746b(t34, t41);
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srckfibkpt681aa, 487, 5);
        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
    }
    {
        void * t44 = (void *)((uint8_t *)&F.l4);
        void * t45 = gm_l0;
        gm_t88rcucg1b7uk t46 = gm_l1;
        uint64_t t47 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t48;
        GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t47);
        gm_tabb710d9mq2e t49;
        gm_f_format_ef193cn((void *)&t49, (void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_srckfibkpt681aa, 488, 12);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
        gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t44, t51);
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srckfibkpt681aa, 488, 5);
        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
    }
    {
        void * t54 = gm_l0;
        gm_t88rcucg1b7uk t55 = gm_l1;
        gm_t53at80pfdus3 t56;
        gm_f_kind_ghvav22((void *)&t56, t54, GM_CONV(gm_csguankko4lmhd, t55));
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        switch (t71) { case ((uint8_t)0ULL): goto ma58; case ((uint8_t)1ULL): goto ma59; case ((uint8_t)2ULL): goto ma59; case ((uint8_t)3ULL): goto ma60; case ((uint8_t)4ULL): goto ma61; case ((uint8_t)5ULL): goto ma69; case ((uint8_t)6ULL): goto ma69; case ((uint8_t)7ULL): goto ma62; case ((uint8_t)8ULL): goto ma62; case ((uint8_t)9ULL): goto ma62; case ((uint8_t)10ULL): goto ma63; case ((uint8_t)11ULL): goto ma64; case ((uint8_t)12ULL): goto ma65; case ((uint8_t)13ULL): goto ma66; case ((uint8_t)14ULL): goto ma67; case ((uint8_t)15ULL): goto ma69; case ((uint8_t)16ULL): goto ma62; case ((uint8_t)17ULL): goto ma68; case ((uint8_t)18ULL): goto ma69; case ((uint8_t)19ULL): goto ma69; case ((uint8_t)20ULL): goto ma69; case ((uint8_t)21ULL): goto ma69; case ((uint8_t)22ULL): goto ma69; case ((uint8_t)23ULL): goto ma69; case ((uint8_t)24ULL): goto ma69; case ((uint8_t)25ULL): goto ma69; case ((uint8_t)26ULL): goto ma69; default: goto ma69; }
ma58:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t73 != ((uint8_t)0ULL)) goto mn72;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&t56) + 8) + 0));
        gm_l5 = t74;
        uint16_t t75 = GM_LD(uint16_t, ((((uint8_t *)&t56) + 8) + 2));
        gm_l6 = t75;
        {
            void * t77 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sropo1941pvlt));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(15));
            gm_tftb1odc739t5 t79 = gm_f_push_3ef746b(t77, t78);
            if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srckfibkpt681aa, 491, 13);
            gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        }
        {
            void * t81 = (void *)((uint8_t *)&F.l4);
            uint16_t t82 = gm_l6;
            gm_tu0pangbbaeec t83;
            GM_ST(uint16_t, ((uint8_t *)&t83 + 0), t82);
            gm_tabb710d9mq2e t84;
            gm_f_format_ms4f9aj((void *)&t84, (void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srckfibkpt681aa, 492, 20);
            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t84) + 8));
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&t85));
            gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t81, t86);
            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srckfibkpt681aa, 492, 13);
            gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
        }
        {
            void * t89 = (void *)((uint8_t *)&F.l4);
            uint8_t t90 = gm_l5;
            gm_t9qkh0rsooc8d t91;
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), t90);
            gm_tabb710d9mq2e t92;
            gm_f_format_lkeo172((void *)&t92, (void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srckfibkpt681aa, 493, 20);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_tftb1odc739t5 t95 = gm_f_push_3ef746b(t89, t94);
            if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srckfibkpt681aa, 493, 13);
            gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
        }
        goto me57;
mn72:;
ma59:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t100 != ((uint8_t)1ULL)) goto pa99;
        goto po98;
pa99:;
        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t102 != ((uint8_t)2ULL)) goto pa101;
        goto po98;
pa101:;
        goto mn97;
po98:;
        {
            void * t104 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sropo1941pvlt));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(15));
            gm_tftb1odc739t5 t106 = gm_f_push_3ef746b(t104, t105);
            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srckfibkpt681aa, 496, 13);
            gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
        }
        {
            void * t108 = (void *)((uint8_t *)&F.l4);
            void * t109 = gm_l0;
            void * t110 = (void *)((((uint8_t *)t109) + 8) + 0);
            uint32_t t111 = gm_f_ptr_bits_glps5i7(t110);
            gm_t88rcucg1b7uk t112;
            GM_ST(uint32_t, ((uint8_t *)&t112 + 0), t111);
            gm_tabb710d9mq2e t113;
            gm_f_format_ch41dev((void *)&t113, (void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_srckfibkpt681aa, 497, 20);
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
            gm_tftb1odc739t5 t116 = gm_f_push_3ef746b(t108, t115);
            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srckfibkpt681aa, 497, 13);
            gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
        }
        {
            void * t118 = (void *)((uint8_t *)&F.l4);
            void * t119 = gm_l0;
            gm_t88rcucg1b7uk t120 = gm_l1;
            gm_t53at80pfdus3 t121;
            gm_f_kind_ghvav22((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
            uint8_t t122 = ((GM_LD(uint8_t, ((uint8_t *)&t121) + 0) == ((uint8_t)2ULL)));
            gm_t9qkh0rsooc8d t123;
            GM_ST(uint8_t, ((uint8_t *)&t123 + 0), t122);
            gm_tabb710d9mq2e t124;
            gm_f_format_lkeo172((void *)&t124, (void *)NULL, cx, NULL, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_srckfibkpt681aa, 498, 20);
            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            gm_tftb1odc739t5 t127 = gm_f_push_3ef746b(t118, t126);
            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srckfibkpt681aa, 498, 13);
            gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
        }
        goto me57;
mn97:;
ma60:;
        uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t130 != ((uint8_t)3ULL)) goto mn129;
        uint16_t t131 = GM_LD(uint16_t, (((uint8_t *)&t56) + 8));
        gm_l7 = t131;
        {
            void * t133 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_st53sho4849cg));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t135 = gm_f_push_3ef746b(t133, t134);
            if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srckfibkpt681aa, 501, 13);
            gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
        }
        {
            void * t137 = (void *)((uint8_t *)&F.l4);
            uint16_t t138 = gm_l7;
            gm_tu0pangbbaeec t139;
            GM_ST(uint16_t, ((uint8_t *)&t139 + 0), t138);
            gm_tabb710d9mq2e t140;
            gm_f_format_ms4f9aj((void *)&t140, (void *)NULL, cx, NULL, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_srckfibkpt681aa, 502, 20);
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
            gm_tftb1odc739t5 t143 = gm_f_push_3ef746b(t137, t142);
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srckfibkpt681aa, 502, 13);
            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
        }
        goto me57;
mn129:;
ma61:;
        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t146 != ((uint8_t)4ULL)) goto mn145;
        void * t147 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t148;
        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_suvf0cqih9u7i));
        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(16));
        gm_tftb1odc739t5 t149 = gm_f_push_3ef746b(t147, t148);
        if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_srckfibkpt681aa, 504, 18);
        gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
        goto me57;
mn145:;
ma62:;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t154 != ((uint8_t)8ULL)) goto pa153;
        goto po152;
pa153:;
        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t156 != ((uint8_t)9ULL)) goto pa155;
        goto po152;
pa155:;
        uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t158 != ((uint8_t)7ULL)) goto pa157;
        goto po152;
pa157:;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t160 != ((uint8_t)16ULL)) goto pa159;
        goto po152;
pa159:;
        goto mn151;
po152:;
        void * t161 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sa9ktdbn49iui));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(15));
        gm_tftb1odc739t5 t163 = gm_f_push_3ef746b(t161, t162);
        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srckfibkpt681aa, 505, 53);
        gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
        goto me57;
mn151:;
ma63:;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t166 != ((uint8_t)10ULL)) goto mn165;
        gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 4));
        gm_l8 = t167;
        {
            void * t169 = gm_l0;
            gm_t88rcucg1b7uk t170 = gm_l8;
            gm_b16_8 t171 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t171;
        }
        {
            void * t172 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t173;
            GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sdam23bqvafqn));
            GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t174 = gm_f_push_3ef746b(t172, t173);
            if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srckfibkpt681aa, 508, 13);
            gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
        }
        {
            void * t176 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t177 = gm_l9;
            gm_tnpifv20irnph t178;
            GM_ST(gm_b16_8, ((uint8_t *)&t178 + 0), t177);
            gm_tabb710d9mq2e t179;
            gm_f_format_vsbf5l9((void *)&t179, (void *)NULL, cx, NULL, t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 8, gm_srckfibkpt681aa, 509, 20);
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
            gm_tftb1odc739t5 t182 = gm_f_push_3ef746b(t176, t181);
            if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srckfibkpt681aa, 509, 13);
            gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
        }
        goto me57;
mn165:;
ma64:;
        uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t185 != ((uint8_t)11ULL)) goto mn184;
        uint64_t t186 = GM_LD(uint64_t, ((((uint8_t *)&t56) + 8) + 0));
        gm_l10 = t186;
        gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 8));
        gm_l11 = t187;
        {
            void * t189 = gm_l0;
            gm_t88rcucg1b7uk t190 = gm_l11;
            gm_b16_8 t191 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t189, t190);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t191;
        }
        {
            void * t192 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t193;
            GM_ST(void *, (uint8_t *)&t193, (void *)(gm_s0f7epcithn0r));
            GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t194 = gm_f_push_3ef746b(t192, t193);
            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_srckfibkpt681aa, 513, 13);
            gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
        }
        {
            void * t196 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t197 = gm_l12;
            gm_tnpifv20irnph t198;
            GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
            gm_tabb710d9mq2e t199;
            gm_f_format_vsbf5l9((void *)&t199, (void *)NULL, cx, NULL, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 8, gm_srckfibkpt681aa, 514, 20);
            gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&t199) + 8));
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_tftb1odc739t5 t202 = gm_f_push_3ef746b(t196, t201);
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 1, gm_srckfibkpt681aa, 514, 13);
            gm_b0_1 t203 = GM_LD(gm_b0_1, (((uint8_t *)&t202) + 1));
        }
        {
            void * t204 = (void *)((uint8_t *)&F.l4);
            uint64_t t205 = gm_l10;
            gm_t4ce0jd3tdro4 t206;
            GM_ST(uint64_t, ((uint8_t *)&t206 + 0), t205);
            gm_tabb710d9mq2e t207;
            gm_f_format_km55tit((void *)&t207, (void *)NULL, cx, NULL, t206);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_srckfibkpt681aa, 515, 20);
            gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
            gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
            gm_tftb1odc739t5 t210 = gm_f_push_3ef746b(t204, t209);
            if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srckfibkpt681aa, 515, 13);
            gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
        }
        goto me57;
mn184:;
ma65:;
        uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t213 != ((uint8_t)12ULL)) goto mn212;
        gm_b16_8 t214 = GM_LD(gm_b16_8, ((((uint8_t *)&t56) + 8) + 0));
        gm_l13 = t214;
        gm_b16_8 t215 = GM_LD(gm_b16_8, ((((uint8_t *)&t56) + 8) + 16));
        gm_l14 = t215;
        {
            void * t217 = gm_l0;
            gm_t88rcucg1b7uk t218 = gm_l1;
            gm_b16_8 t219 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t217, t218);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t219;
        }
        {
            gm_f_list_epvducg((void *)&F.l16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t221 = gm_l13;
                gm_l17 = t221;
            }
            {
                gm_l18 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t223 = gm_l18;
                    uint64_t t224 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                    uint8_t t225 = (t223 < t224);
                    if (!t225) goto lb222;
                    {
                        uint64_t t228 = gm_l18;
                        uint64_t t229 = gm_l18;
                        uint64_t t230 = (uintptr_t)t229;
                        uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t230 * 4));
                        gm_tlcm4lvu3g1a8 t233;
                        GM_ST(uint32_t, (uint8_t *)&t233 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t233 + 0), t228);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t233 + 8), t232);
                        uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&t233) + 0));
                        gm_l19 = t236;
                        gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t233) + 8));
                        gm_l20 = t237;
                        {
                            void * t239 = gm_l0;
                            gm_t88rcucg1b7uk t240 = gm_l20;
                            gm_b16_8 t241 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t239, t240);
                            if (gm_unw.kind) {
                                goto gm_ll226;
                            }
                            gm_l21 = t241;
                        }
                        {
                            gm_b16_8 t242;
                            uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            uint8_t t245 = (t244 > ((uint64_t)0ULL));
                            if (!t245) goto ie243;
                            uint64_t t247 = gm_l19;
                            uint64_t t248 = (uintptr_t)t247;
                            uint64_t t249 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            if (t248 >= (uintptr_t)(t249)) gm_trap_index(t248, t249, gm_srckfibkpt681aa, 522, 60);
                            gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t248 * 16));
                            gm_b16_8 t251 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t250);
                            if (gm_unw.kind) {
                                goto gm_ll226;
                            }
                            gm_b16_8 t252 = t251;
                            t242 = t252;
                            goto ix243;
ie243:;
                            gm_b16_8 t254;
                            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_s04qus0luoga5));
                            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(4));
                            gm_b16_8 t255 = t254;
                            t242 = t255;
ix243:;
                            gm_l22 = t242;
                        }
                        {
                            void * t256 = (void *)((uint8_t *)&F.l16);
                            gm_tq3m5qgvkpje8 t257;
                            gm_b16_8 t258 = gm_l22;
                            GM_ST(gm_b16_8, (uint8_t *)&t257 + 0, t258);
                            uint64_t t259 = gm_l19;
                            uint64_t t260 = (uintptr_t)t259;
                            uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            if (t260 >= (uintptr_t)(t261)) gm_trap_index(t260, t261, gm_srckfibkpt681aa, 523, 63);
                            uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t260 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t257 + 16, t262);
                            gm_b16_8 t263 = gm_l21;
                            GM_ST(gm_b16_8, (uint8_t *)&t257 + 24, t263);
                            gm_tabb710d9mq2e t264;
                            gm_f_format_1il5da4((void *)&t264, (void *)NULL, cx, NULL, &t257);
                            if (gm_unw.kind) {
                                goto gm_ll226;
                            }
                            if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 8, gm_srckfibkpt681aa, 523, 28);
                            gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&t264) + 8));
                            gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                            gm_tftb1odc739t5 t267 = gm_f_push_3ef746b(t256, t266);
                            if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 1, gm_srckfibkpt681aa, 523, 17);
                            gm_b0_1 t268 = GM_LD(gm_b0_1, (((uint8_t *)&t267) + 1));
                        }
                        goto me234;
mn235:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 520, 13);
me234:;
                    }
lc222:;
                    {
                        uint64_t t270 = gm_l18;
                        uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                        uint64_t t273 = r271;
                        gm_l18 = t273;
                    }
                }
gm_ll226:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb222; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc222; }
                }
                goto gm_lad;
lb222:;
            }
        }
        {
            void * t274 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t275;
            GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sc0vbtqan26pc));
            GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t276 = gm_f_push_3ef746b(t274, t275);
            if (((((uint8_t *)&t276))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t276) + 1, gm_srckfibkpt681aa, 525, 13);
            gm_b0_1 t277 = GM_LD(gm_b0_1, (((uint8_t *)&t276) + 1));
        }
        {
            void * t278 = (void *)((uint8_t *)&F.l4);
            uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
            uint8_t t280 = (t279 > ((uint64_t)0ULL));
            gm_t9qkh0rsooc8d t281;
            GM_ST(uint8_t, ((uint8_t *)&t281 + 0), t280);
            gm_tabb710d9mq2e t282;
            gm_f_format_df9v4jq((void *)&t282, (void *)NULL, cx, NULL, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 8, gm_srckfibkpt681aa, 526, 20);
            gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&t282) + 8));
            gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
            gm_tftb1odc739t5 t285 = gm_f_push_3ef746b(t278, t284);
            if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 1, gm_srckfibkpt681aa, 526, 13);
            gm_b0_1 t286 = GM_LD(gm_b0_1, (((uint8_t *)&t285) + 1));
        }
        {
            void * t287 = gm_l0;
            gm_b16_8 t288 = gm_l3;
            gm_b16_8 t289 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
            gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)&t289));
            void * t291 = (void *)((uint8_t *)&F.l4);
            gm_b0_1 t292 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t287, t288, t290, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me57;
mn212:;
ma66:;
        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t294 != ((uint8_t)13ULL)) goto mn293;
        gm_t88rcucg1b7uk t295 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t56) + 8));
        gm_l23 = t295;
        {
            void * t297 = gm_l0;
            gm_t88rcucg1b7uk t298 = gm_l23;
            gm_b16_8 t299 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t297, t298);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t299;
        }
        {
            void * t300 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t301;
            GM_ST(void *, (uint8_t *)&t301, (void *)(gm_s7qhcf46f682d));
            GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)(20));
            gm_tftb1odc739t5 t302 = gm_f_push_3ef746b(t300, t301);
            if (((((uint8_t *)&t302))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t302) + 1, gm_srckfibkpt681aa, 531, 13);
            gm_b0_1 t303 = GM_LD(gm_b0_1, (((uint8_t *)&t302) + 1));
        }
        {
            void * t304 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t305 = gm_l24;
            gm_tnpifv20irnph t306;
            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t305);
            gm_tabb710d9mq2e t307;
            gm_f_format_vsbf5l9((void *)&t307, (void *)NULL, cx, NULL, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_srckfibkpt681aa, 532, 20);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
            gm_tftb1odc739t5 t310 = gm_f_push_3ef746b(t304, t309);
            if (((((uint8_t *)&t310))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t310) + 1, gm_srckfibkpt681aa, 532, 13);
            gm_b0_1 t311 = GM_LD(gm_b0_1, (((uint8_t *)&t310) + 1));
        }
        {
            void * t313 = gm_l0;
            void * t314 = GM_LD(void *, (((uint8_t *)t313) + 0));
            gm_t88rcucg1b7uk t315 = gm_l23;
            uint8_t t316 = gm_f_nullable_pointer_7qfmn6o(t314, GM_CONV(gm_csguankko4lmhd, t315));
            if (!t316) goto ie312;
            {
                void * t318 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t319;
                GM_ST(void *, (uint8_t *)&t319, (void *)(gm_sbec11r2rv4ti));
                GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(22));
                gm_tftb1odc739t5 t320 = gm_f_push_3ef746b(t318, t319);
                if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 1, gm_srckfibkpt681aa, 534, 17);
                gm_b0_1 t321 = GM_LD(gm_b0_1, (((uint8_t *)&t320) + 1));
            }
            goto ix312;
ie312:;
            {
                void * t323 = (void *)((uint8_t *)&F.l4);
                uint64_t t324 = (uintptr_t)((uint64_t)1ULL);
                void * t325 = gm_l0;
                gm_t88rcucg1b7uk t326 = gm_l1;
                gm_b16_8 t327 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t325, t326);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&t327) + 8);
                if (t324 >= (uintptr_t)(t328)) gm_trap_index(t324, t328, gm_srckfibkpt681aa, 536, 55);
                uint64_t t329 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t327)) + t324 * 8));
                gm_t4ce0jd3tdro4 t330;
                GM_ST(uint64_t, ((uint8_t *)&t330 + 0), t329);
                gm_tabb710d9mq2e t331;
                gm_f_format_9117rt8((void *)&t331, (void *)NULL, cx, NULL, t330);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t331))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t331) + 8, gm_srckfibkpt681aa, 536, 24);
                gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&t331) + 8));
                gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
                gm_tftb1odc739t5 t334 = gm_f_push_3ef746b(t323, t333);
                if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_srckfibkpt681aa, 536, 17);
                gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
            }
ix312:;
        }
        goto me57;
mn293:;
ma67:;
        uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t337 != ((uint8_t)14ULL)) goto mn336;
        gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 0));
        gm_l25 = t338;
        gm_t88rcucg1b7uk t339 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t56) + 8) + 4));
        gm_l26 = t339;
        {
            void * t341 = gm_l0;
            gm_t88rcucg1b7uk t342 = gm_l25;
            gm_b16_8 t343 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t341, t342);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t343;
        }
        {
            void * t344 = gm_l0;
            gm_t88rcucg1b7uk t345 = gm_l26;
            gm_b16_8 t346 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t344, t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t346;
        }
        {
            void * t347 = gm_l0;
            gm_t88rcucg1b7uk t348 = gm_l1;
            gm_b16_8 t349 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t347, t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t349;
        }
        {
            void * t350 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t351;
            GM_ST(void *, (uint8_t *)&t351, (void *)(gm_shrq42dntcbrd));
            GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t352 = gm_f_push_3ef746b(t350, t351);
            if (((((uint8_t *)&t352))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t352) + 1, gm_srckfibkpt681aa, 543, 13);
            gm_b0_1 t353 = GM_LD(gm_b0_1, (((uint8_t *)&t352) + 1));
        }
        {
            void * t354 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t355 = gm_l28;
            gm_tnpifv20irnph t356;
            GM_ST(gm_b16_8, ((uint8_t *)&t356 + 0), t355);
            gm_tabb710d9mq2e t357;
            gm_f_format_vsbf5l9((void *)&t357, (void *)NULL, cx, NULL, t356);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t357))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t357) + 8, gm_srckfibkpt681aa, 544, 20);
            gm_b16_8 t358 = GM_LD(gm_b16_8, (((uint8_t *)&t357) + 8));
            gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&t358));
            gm_tftb1odc739t5 t360 = gm_f_push_3ef746b(t354, t359);
            if (((((uint8_t *)&t360))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t360) + 1, gm_srckfibkpt681aa, 544, 13);
            gm_b0_1 t361 = GM_LD(gm_b0_1, (((uint8_t *)&t360) + 1));
        }
        {
            void * t362 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t363 = gm_l27;
            gm_tnpifv20irnph t364;
            GM_ST(gm_b16_8, ((uint8_t *)&t364 + 0), t363);
            gm_tabb710d9mq2e t365;
            gm_f_format_s44fp1q((void *)&t365, (void *)NULL, cx, NULL, t364);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t365))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t365) + 8, gm_srckfibkpt681aa, 545, 20);
            gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&t365) + 8));
            gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&t366));
            gm_tftb1odc739t5 t368 = gm_f_push_3ef746b(t362, t367);
            if (((((uint8_t *)&t368))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t368) + 1, gm_srckfibkpt681aa, 545, 13);
            gm_b0_1 t369 = GM_LD(gm_b0_1, (((uint8_t *)&t368) + 1));
        }
        {
            void * t370 = (void *)((uint8_t *)&F.l4);
            uint64_t t371 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t372 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t371 >= (uintptr_t)(t372)) gm_trap_index(t371, t372, gm_srckfibkpt681aa, 546, 50);
            uint64_t t373 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t371 * 8));
            gm_t4ce0jd3tdro4 t374;
            GM_ST(uint64_t, ((uint8_t *)&t374 + 0), t373);
            gm_tabb710d9mq2e t375;
            gm_f_format_4v9fh5b((void *)&t375, (void *)NULL, cx, NULL, t374);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t375))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t375) + 8, gm_srckfibkpt681aa, 546, 20);
            gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&t375) + 8));
            gm_b16_8 t377 = GM_LD(gm_b16_8, ((uint8_t *)&t376));
            gm_tftb1odc739t5 t378 = gm_f_push_3ef746b(t370, t377);
            if (((((uint8_t *)&t378))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t378) + 1, gm_srckfibkpt681aa, 546, 13);
            gm_b0_1 t379 = GM_LD(gm_b0_1, (((uint8_t *)&t378) + 1));
        }
        {
            void * t380 = (void *)((uint8_t *)&F.l4);
            uint64_t t381 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t381 >= (uintptr_t)(t382)) gm_trap_index(t381, t382, gm_srckfibkpt681aa, 547, 54);
            uint64_t t383 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t381 * 8));
            gm_t4ce0jd3tdro4 t384;
            GM_ST(uint64_t, ((uint8_t *)&t384 + 0), t383);
            gm_tabb710d9mq2e t385;
            gm_f_format_io38sjt((void *)&t385, (void *)NULL, cx, NULL, t384);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 8, gm_srckfibkpt681aa, 547, 20);
            gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&t385) + 8));
            gm_b16_8 t387 = GM_LD(gm_b16_8, ((uint8_t *)&t386));
            gm_tftb1odc739t5 t388 = gm_f_push_3ef746b(t380, t387);
            if (((((uint8_t *)&t388))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t388) + 1, gm_srckfibkpt681aa, 547, 13);
            gm_b0_1 t389 = GM_LD(gm_b0_1, (((uint8_t *)&t388) + 1));
        }
        goto me57;
mn336:;
ma68:;
        uint8_t t391 = GM_LD(uint8_t, (((uint8_t *)&t56) + 0));
        if (t391 != ((uint8_t)17ULL)) goto mn390;
        uint32_t t392 = GM_LD(uint32_t, ((((uint8_t *)&t56) + 8) + 0));
        gm_l30 = t392;
        {
            uint32_t t394 = gm_l30;
            uint64_t t395 = (uint64_t)t394;
            uint64_t t396 = (uintptr_t)t395;
            void * t397 = gm_l0;
            void * t398 = GM_LD(void *, (((uint8_t *)t397) + 0));
            uint64_t t399 = GM_LD(uintptr_t, ((((uint8_t *)t398) + 1088) + 0) + 8);
            if (t396 >= (uintptr_t)(t399)) gm_trap_index(t396, t399, gm_srckfibkpt681aa, 550, 21);
            void * t400 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t398) + 1088) + 0)) + t396 * 184);
            gm_l31 = t400;
        }
        {
            void * t401 = (void *)((uint8_t *)&F.l4);
            void * t402 = gm_l31;
            gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)t402) + 0));
            gm_b16_8 t404 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t403);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t405;
            GM_ST(gm_b16_8, ((uint8_t *)&t405 + 0), t404);
            gm_tabb710d9mq2e t406;
            gm_f_format_nbg44fl((void *)&t406, (void *)NULL, cx, NULL, t405);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 8, gm_srckfibkpt681aa, 551, 20);
            gm_b16_8 t407 = GM_LD(gm_b16_8, (((uint8_t *)&t406) + 8));
            gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&t407));
            gm_tftb1odc739t5 t409 = gm_f_push_3ef746b(t401, t408);
            if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 1, gm_srckfibkpt681aa, 551, 13);
            gm_b0_1 t410 = GM_LD(gm_b0_1, (((uint8_t *)&t409) + 1));
        }
        {
            void * t411 = gm_l31;
            uint8_t t417 = GM_LD(uint8_t, ((((uint8_t *)t411) + 24) + 0));
            switch (t417) { case ((uint8_t)0ULL): goto ma413; case ((uint8_t)1ULL): goto ma414; case ((uint8_t)2ULL): goto ma415; default: goto ma416; }
ma413:;
            uint8_t t419 = GM_LD(uint8_t, ((((uint8_t *)t411) + 24) + 0));
            if (t419 != ((uint8_t)0ULL)) goto mn418;
            gm_tpautr4br7h3l t420 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t411) + 24) + 8));
            F.l32 = t420;
            {
                uint8_t t423;
                if ((((((uint8_t *)&F.l32) + 40))[4] != 0)) goto mn425;
                t423 = ((uint8_t)1);
                goto me424;
mn425:;
                t423 = ((uint8_t)0);
                goto me424;
mn426:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 554, 24);
me424:;
                if (!t423) goto ie422;
                {
                    void * t428 = gm_l0;
                    gm_t88rcucg1b7uk t429 = gm_l1;
                    gm_b16_8 t430 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t428, t429);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t430;
                }
                {
                    gm_f_list_epvducg((void *)&F.l34, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                        gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
                        gm_l35 = t433;
                    }
                    {
                        gm_l36 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t435 = gm_l36;
                            uint64_t t436 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                            uint8_t t437 = (t435 < t436);
                            if (!t437) goto lb434;
                            {
                                gm_t8ta482bl6be0 t440;
                                uint64_t t441 = gm_l36;
                                GM_ST(uint64_t, (uint8_t *)&t440 + 0, t441);
                                uint64_t t442 = gm_l36;
                                uint64_t t443 = (uintptr_t)t442;
                                uint64_t t444 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                memcpy((uint8_t *)&t440 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t443 * 40), 40);
                                uint64_t t447 = GM_LD(uint64_t, (((uint8_t *)&t440) + 0));
                                gm_l37 = t447;
                                F.l38 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t440) + 8));
                                {
                                    void * t449 = gm_l0;
                                    gm_t88rcucg1b7uk t450 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l38) + 16));
                                    gm_b16_8 t451 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t449, t450);
                                    if (gm_unw.kind) {
                                        goto gm_ll438;
                                    }
                                    gm_l39 = t451;
                                }
                                {
                                    void * t452 = (void *)((uint8_t *)&F.l34);
                                    gm_tq3m5qgvkpje8 t453;
                                    gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&F.l38) + 0));
                                    gm_b16_8 t455 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t454);
                                    if (gm_unw.kind) {
                                        goto gm_ll438;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t453 + 0, t455);
                                    uint64_t t456 = gm_l37;
                                    uint64_t t457 = (uintptr_t)t456;
                                    uint64_t t458 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    if (t457 >= (uintptr_t)(t458)) gm_trap_index(t457, t458, gm_srckfibkpt681aa, 559, 88);
                                    uint64_t t459 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t457 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t453 + 16, t459);
                                    gm_b16_8 t460 = gm_l39;
                                    GM_ST(gm_b16_8, (uint8_t *)&t453 + 24, t460);
                                    gm_tabb710d9mq2e t461;
                                    gm_f_format_1il5da4((void *)&t461, (void *)NULL, cx, NULL, &t453);
                                    if (gm_unw.kind) {
                                        goto gm_ll438;
                                    }
                                    if (((((uint8_t *)&t461))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t461) + 8, gm_srckfibkpt681aa, 559, 40);
                                    gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&t461) + 8));
                                    gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)&t462));
                                    gm_tftb1odc739t5 t464 = gm_f_push_3ef746b(t452, t463);
                                    if (((((uint8_t *)&t464))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t464) + 1, gm_srckfibkpt681aa, 559, 29);
                                    gm_b0_1 t465 = GM_LD(gm_b0_1, (((uint8_t *)&t464) + 1));
                                }
                                goto me445;
mn446:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 557, 25);
me445:;
                            }
lc434:;
                            {
                                uint64_t t467 = gm_l36;
                                uint64_t r468; bool o469 = __builtin_add_overflow(t467, ((uint64_t)1ULL), &r468);
                                uint64_t t470 = r468;
                                gm_l36 = t470;
                            }
                        }
gm_ll438:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb434; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc434; }
                        }
                        goto gm_lad;
lb434:;
                    }
                }
                {
                    void * t471 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t472;
                    GM_ST(void *, (uint8_t *)&t472, (void *)(gm_slad86501rp5c));
                    GM_ST(uintptr_t, (uint8_t *)&t472 + 8, (uintptr_t)(18));
                    gm_tftb1odc739t5 t473 = gm_f_push_3ef746b(t471, t472);
                    if (((((uint8_t *)&t473))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t473) + 1, gm_srckfibkpt681aa, 561, 25);
                    gm_b0_1 t474 = GM_LD(gm_b0_1, (((uint8_t *)&t473) + 1));
                }
                {
                    void * t475 = gm_l0;
                    gm_b16_8 t476 = gm_l3;
                    gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                    gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)&t477));
                    void * t479 = (void *)((uint8_t *)&F.l4);
                    gm_b0_1 t480 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t475, t476, t478, t479);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix422;
ie422:;
                {
                    void * t482 = gm_l0;
                    gm_t88rcucg1b7uk t483 = gm_l1;
                    gm_t57euh612ligr t484;
                    gm_f_bits_layout_23rect9((void *)&t484, (void *)NULL, cx, NULL, t482, t483);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!((((uint8_t *)&t484))[24] != 0)) gm_trap("unwrap of null", gm_srckfibkpt681aa, 564, 31);
                    gm_l40 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t484) + 0));
                }
                {
                    gm_f_list_epvducg((void *)&F.l41, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_b16_8 t486 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                        gm_b16_8 t487 = GM_LD(gm_b16_8, ((uint8_t *)&t486));
                        gm_l42 = t487;
                    }
                    {
                        gm_l43 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t489 = gm_l43;
                            uint64_t t490 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                            uint8_t t491 = (t489 < t490);
                            if (!t491) goto lb488;
                            {
                                gm_t8ta482bl6be0 t494;
                                uint64_t t495 = gm_l43;
                                GM_ST(uint64_t, (uint8_t *)&t494 + 0, t495);
                                uint64_t t496 = gm_l43;
                                uint64_t t497 = (uintptr_t)t496;
                                uint64_t t498 = GM_LD(uintptr_t, ((uint8_t *)&gm_l42) + 8);
                                memcpy((uint8_t *)&t494 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l42)) + t497 * 40), 40);
                                uint64_t t501 = GM_LD(uint64_t, (((uint8_t *)&t494) + 0));
                                gm_l44 = t501;
                                F.l45 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t494) + 8));
                                {
                                    uint64_t t503 = gm_l44;
                                    uint64_t t504 = (uintptr_t)t503;
                                    uint64_t t505 = GM_LD(uintptr_t, (((uint8_t *)&gm_l40) + 8) + 8);
                                    if (t504 >= (uintptr_t)(t505)) gm_trap_index(t504, t505, gm_srckfibkpt681aa, 567, 35);
                                    gm_tov4mtrfp5e7v t506 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l40) + 8)) + t504 * 12));
                                    gm_l46 = t506;
                                }
                                {
                                    void * t507 = gm_l0;
                                    gm_t88rcucg1b7uk t508 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                                    gm_b16_8 t509 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t507, t508);
                                    if (gm_unw.kind) {
                                        goto gm_ll492;
                                    }
                                    gm_l47 = t509;
                                }
                                {
                                    void * t510 = (void *)((uint8_t *)&F.l41);
                                    gm_temv198mna2jp t511;
                                    memset((uint8_t *)&t511 + 20, 0, 4);
                                    gm_b16_8 t512 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                                    gm_b16_8 t513 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t512);
                                    if (gm_unw.kind) {
                                        goto gm_ll492;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t511 + 0, t513);
                                    uint32_t t514 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 0));
                                    GM_ST(uint32_t, (uint8_t *)&t511 + 16, t514);
                                    gm_b16_8 t515 = gm_l47;
                                    GM_ST(gm_b16_8, (uint8_t *)&t511 + 24, t515);
                                    gm_tabb710d9mq2e t516;
                                    gm_f_format_n3gtmrf((void *)&t516, (void *)NULL, cx, NULL, &t511);
                                    if (gm_unw.kind) {
                                        goto gm_ll492;
                                    }
                                    if (((((uint8_t *)&t516))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t516) + 8, gm_srckfibkpt681aa, 569, 40);
                                    gm_b16_8 t517 = GM_LD(gm_b16_8, (((uint8_t *)&t516) + 8));
                                    gm_b16_8 t518 = GM_LD(gm_b16_8, ((uint8_t *)&t517));
                                    gm_tftb1odc739t5 t519 = gm_f_push_3ef746b(t510, t518);
                                    if (((((uint8_t *)&t519))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t519) + 1, gm_srckfibkpt681aa, 569, 29);
                                    gm_b0_1 t520 = GM_LD(gm_b0_1, (((uint8_t *)&t519) + 1));
                                }
                                goto me499;
mn500:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 566, 25);
me499:;
                            }
lc488:;
                            {
                                uint64_t t522 = gm_l43;
                                uint64_t r523; bool o524 = __builtin_add_overflow(t522, ((uint64_t)1ULL), &r523);
                                uint64_t t525 = r523;
                                gm_l43 = t525;
                            }
                        }
gm_ll492:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb488; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc488; }
                        }
                        goto gm_lad;
lb488:;
                    }
                }
                {
                    void * t526 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t527;
                    GM_ST(void *, (uint8_t *)&t527, (void *)(gm_s76d71nockpip));
                    GM_ST(uintptr_t, (uint8_t *)&t527 + 8, (uintptr_t)(16));
                    gm_tftb1odc739t5 t528 = gm_f_push_3ef746b(t526, t527);
                    if (((((uint8_t *)&t528))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t528) + 1, gm_srckfibkpt681aa, 571, 25);
                    gm_b0_1 t529 = GM_LD(gm_b0_1, (((uint8_t *)&t528) + 1));
                }
                {
                    void * t530 = (void *)((uint8_t *)&F.l4);
                    void * t531 = gm_l0;
                    gm_t88rcucg1b7uk t532 = gm_l1;
                    uint32_t t533 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t531, t532);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t534;
                    GM_ST(uint32_t, ((uint8_t *)&t534 + 0), t533);
                    gm_tabb710d9mq2e t535;
                    gm_f_format_ch41dev((void *)&t535, (void *)NULL, cx, NULL, t534);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t535))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t535) + 8, gm_srckfibkpt681aa, 572, 32);
                    gm_b16_8 t536 = GM_LD(gm_b16_8, (((uint8_t *)&t535) + 8));
                    gm_b16_8 t537 = GM_LD(gm_b16_8, ((uint8_t *)&t536));
                    gm_tftb1odc739t5 t538 = gm_f_push_3ef746b(t530, t537);
                    if (((((uint8_t *)&t538))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t538) + 1, gm_srckfibkpt681aa, 572, 25);
                    gm_b0_1 t539 = GM_LD(gm_b0_1, (((uint8_t *)&t538) + 1));
                }
                {
                    void * t540 = gm_l0;
                    gm_b16_8 t541 = gm_l3;
                    gm_b16_8 t542 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
                    gm_b16_8 t543 = GM_LD(gm_b16_8, ((uint8_t *)&t542));
                    void * t544 = (void *)((uint8_t *)&F.l4);
                    gm_b0_1 t545 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t540, t541, t543, t544);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix422:;
            }
            goto me412;
mn418:;
ma414:;
            uint8_t t547 = GM_LD(uint8_t, ((((uint8_t *)t411) + 24) + 0));
            if (t547 != ((uint8_t)1ULL)) goto mn546;
            gm_t88rcucg1b7uk t548 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t411) + 24) + 8) + 0));
            gm_l48 = t548;
            gm_b16_8 t549 = GM_LD(gm_b16_8, (((((uint8_t *)t411) + 24) + 8) + 8));
            gm_l49 = t549;
            {
                void * t551 = gm_l0;
                gm_t88rcucg1b7uk t552 = gm_l1;
                gm_b16_8 t553 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t551, t552);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t553;
            }
            {
                gm_f_list_epvducg((void *)&F.l51, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t555 = gm_l49;
                    gm_l52 = t555;
                }
                {
                    gm_l53 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t557 = gm_l53;
                        uint64_t t558 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                        uint8_t t559 = (t557 < t558);
                        if (!t559) goto lb556;
                        {
                            uint64_t t562 = gm_l53;
                            uint64_t t563 = (uintptr_t)t562;
                            uint64_t t564 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                            gm_tckdobunaulee t567 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t563 * 72));
                            F.l54 = t567;
                            {
                                gm_b16_8 t569;
                                if (!(((((uint8_t *)&F.l54) + 16))[4] != 0)) goto ie570;
                                gm_t88rcucg1b7uk t571 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l54) + 16));
                                gm_l55 = t571;
                                void * t573 = gm_l0;
                                gm_t88rcucg1b7uk t574 = gm_l55;
                                gm_b16_8 t575 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t573, t574);
                                if (gm_unw.kind) {
                                    goto gm_ll560;
                                }
                                gm_b16_8 t576 = t575;
                                t569 = t576;
                                goto ix570;
ie570:;
                                gm_b16_8 t578;
                                GM_ST(void *, (uint8_t *)&t578, (void *)(gm_s04qus0luoga5));
                                GM_ST(uintptr_t, (uint8_t *)&t578 + 8, (uintptr_t)(4));
                                gm_b16_8 t579 = t578;
                                t569 = t579;
ix570:;
                                gm_l56 = t569;
                            }
                            {
                                void * t580 = (void *)((uint8_t *)&F.l51);
                                gm_tfeab279c16rr t581;
                                gm_b16_8 t582 = GM_LD(gm_b16_8, (((uint8_t *)&F.l54) + 0));
                                gm_b16_8 t583 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t582);
                                if (gm_unw.kind) {
                                    goto gm_ll560;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t581 + 0, t583);
                                gm_b16_8 t584 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l54) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll560;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t581 + 16, t584);
                                gm_b16_8 t585 = gm_l56;
                                GM_ST(gm_b16_8, (uint8_t *)&t581 + 32, t585);
                                gm_tabb710d9mq2e t586;
                                gm_f_format_aqb9tiu((void *)&t586, (void *)NULL, cx, NULL, &t581);
                                if (gm_unw.kind) {
                                    goto gm_ll560;
                                }
                                if (((((uint8_t *)&t586))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t586) + 8, gm_srckfibkpt681aa, 581, 36);
                                gm_b16_8 t587 = GM_LD(gm_b16_8, (((uint8_t *)&t586) + 8));
                                gm_b16_8 t588 = GM_LD(gm_b16_8, ((uint8_t *)&t587));
                                gm_tftb1odc739t5 t589 = gm_f_push_3ef746b(t580, t588);
                                if (((((uint8_t *)&t589))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t589) + 1, gm_srckfibkpt681aa, 581, 25);
                                gm_b0_1 t590 = GM_LD(gm_b0_1, (((uint8_t *)&t589) + 1));
                            }
                            goto me565;
mn566:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 579, 21);
me565:;
                        }
lc556:;
                        {
                            uint64_t t592 = gm_l53;
                            uint64_t r593; bool o594 = __builtin_add_overflow(t592, ((uint64_t)1ULL), &r593);
                            uint64_t t595 = r593;
                            gm_l53 = t595;
                        }
                    }
gm_ll560:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb556; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc556; }
                    }
                    goto gm_lad;
lb556:;
                }
            }
            {
                void * t596 = gm_l0;
                void * t597 = (void *)(((uint8_t *)t596) + 328);
                gm_t6j6smj7qoctu t598;
                gm_b16_8 t599 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t598 + 0, t599);
                gm_b16_8 t600 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
                gm_b16_8 t601 = GM_LD(gm_b16_8, ((uint8_t *)&t600));
                gm_b16_8 t602;
                GM_ST(void *, (uint8_t *)&t602, (void *)(gm_sg5gdnrith2i6));
                GM_ST(uintptr_t, (uint8_t *)&t602 + 8, (uintptr_t)(2));
                gm_tabb710d9mq2e t603;
                gm_f_join_sfknljb((void *)&t603, (void *)NULL, cx, NULL, t601, t602);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t603))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t603) + 8, gm_srckfibkpt681aa, 583, 90);
                gm_b16_8 t604 = GM_LD(gm_b16_8, (((uint8_t *)&t603) + 8));
                GM_ST(gm_b16_8, (uint8_t *)&t598 + 16, t604);
                gm_tftb1odc739t5 t605 = gm_f_write_r7au1ks(t597, &t598);
                if (((((uint8_t *)&t605))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t605) + 1, gm_srckfibkpt681aa, 583, 21);
                gm_b0_1 t606 = GM_LD(gm_b0_1, (((uint8_t *)&t605) + 1));
            }
            {
                void * t607 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t608;
                GM_ST(void *, (uint8_t *)&t608, (void *)(gm_s58f7dj21gtf9));
                GM_ST(uintptr_t, (uint8_t *)&t608 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t609 = gm_f_push_3ef746b(t607, t608);
                if (((((uint8_t *)&t609))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t609) + 1, gm_srckfibkpt681aa, 584, 21);
                gm_b0_1 t610 = GM_LD(gm_b0_1, (((uint8_t *)&t609) + 1));
            }
            {
                void * t611 = (void *)((uint8_t *)&F.l4);
                uint64_t t612 = GM_LD(uintptr_t, ((uint8_t *)&gm_l49) + 8);
                gm_t4ce0jd3tdro4 t613;
                GM_ST(uint64_t, ((uint8_t *)&t613 + 0), t612);
                gm_tabb710d9mq2e t614;
                gm_f_format_ktg7ckd((void *)&t614, (void *)NULL, cx, NULL, t613);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t614))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t614) + 8, gm_srckfibkpt681aa, 585, 28);
                gm_b16_8 t615 = GM_LD(gm_b16_8, (((uint8_t *)&t614) + 8));
                gm_b16_8 t616 = GM_LD(gm_b16_8, ((uint8_t *)&t615));
                gm_tftb1odc739t5 t617 = gm_f_push_3ef746b(t611, t616);
                if (((((uint8_t *)&t617))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t617) + 1, gm_srckfibkpt681aa, 585, 21);
                gm_b0_1 t618 = GM_LD(gm_b0_1, (((uint8_t *)&t617) + 1));
            }
            {
                void * t619 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t620 = gm_l3;
                gm_tnpifv20irnph t621;
                GM_ST(gm_b16_8, ((uint8_t *)&t621 + 0), t620);
                gm_tabb710d9mq2e t622;
                gm_f_format_ka5ced8((void *)&t622, (void *)NULL, cx, NULL, t621);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t622))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t622) + 8, gm_srckfibkpt681aa, 586, 28);
                gm_b16_8 t623 = GM_LD(gm_b16_8, (((uint8_t *)&t622) + 8));
                gm_b16_8 t624 = GM_LD(gm_b16_8, ((uint8_t *)&t623));
                gm_tftb1odc739t5 t625 = gm_f_push_3ef746b(t619, t624);
                if (((((uint8_t *)&t625))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t625) + 1, gm_srckfibkpt681aa, 586, 21);
                gm_b0_1 t626 = GM_LD(gm_b0_1, (((uint8_t *)&t625) + 1));
            }
            {
                void * t627 = (void *)((uint8_t *)&F.l4);
                uint64_t t628 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t629 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                if (t628 >= (uintptr_t)(t629)) gm_trap_index(t628, t629, gm_srckfibkpt681aa, 587, 58);
                uint64_t t630 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t628 * 8));
                gm_t4ce0jd3tdro4 t631;
                GM_ST(uint64_t, ((uint8_t *)&t631 + 0), t630);
                gm_tabb710d9mq2e t632;
                gm_f_format_4v9fh5b((void *)&t632, (void *)NULL, cx, NULL, t631);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t632))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t632) + 8, gm_srckfibkpt681aa, 587, 28);
                gm_b16_8 t633 = GM_LD(gm_b16_8, (((uint8_t *)&t632) + 8));
                gm_b16_8 t634 = GM_LD(gm_b16_8, ((uint8_t *)&t633));
                gm_tftb1odc739t5 t635 = gm_f_push_3ef746b(t627, t634);
                if (((((uint8_t *)&t635))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t635) + 1, gm_srckfibkpt681aa, 587, 21);
                gm_b0_1 t636 = GM_LD(gm_b0_1, (((uint8_t *)&t635) + 1));
            }
            {
                void * t637 = (void *)((uint8_t *)&F.l4);
                uint64_t t638;
                uint64_t t640 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                uint8_t t641 = (t640 > ((uint64_t)1ULL));
                if (!t641) goto ie639;
                uint64_t t643 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t644 = GM_LD(uintptr_t, ((uint8_t *)&gm_l50) + 8);
                if (t643 >= (uintptr_t)(t644)) gm_trap_index(t643, t644, gm_srckfibkpt681aa, 588, 80);
                uint64_t t645 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l50)) + t643 * 8));
                uint64_t t646 = t645;
                t638 = t646;
                goto ix639;
ie639:;
                uint64_t t648 = ((uint64_t)0ULL);
                t638 = t648;
ix639:;
                gm_t4ce0jd3tdro4 t649;
                GM_ST(uint64_t, ((uint8_t *)&t649 + 0), t638);
                gm_tabb710d9mq2e t650;
                gm_f_format_io38sjt((void *)&t650, (void *)NULL, cx, NULL, t649);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t650))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t650) + 8, gm_srckfibkpt681aa, 588, 28);
                gm_b16_8 t651 = GM_LD(gm_b16_8, (((uint8_t *)&t650) + 8));
                gm_b16_8 t652 = GM_LD(gm_b16_8, ((uint8_t *)&t651));
                gm_tftb1odc739t5 t653 = gm_f_push_3ef746b(t637, t652);
                if (((((uint8_t *)&t653))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t653) + 1, gm_srckfibkpt681aa, 588, 21);
                gm_b0_1 t654 = GM_LD(gm_b0_1, (((uint8_t *)&t653) + 1));
            }
            {
                void * t655 = (void *)((uint8_t *)&F.l4);
                void * t656 = gm_l0;
                gm_t88rcucg1b7uk t657 = gm_l48;
                uint64_t t658 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t656, t657);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4ce0jd3tdro4 t659;
                GM_ST(uint64_t, ((uint8_t *)&t659 + 0), t658);
                gm_tabb710d9mq2e t660;
                gm_f_format_tbvfjdi((void *)&t660, (void *)NULL, cx, NULL, t659);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t660))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t660) + 8, gm_srckfibkpt681aa, 589, 28);
                gm_b16_8 t661 = GM_LD(gm_b16_8, (((uint8_t *)&t660) + 8));
                gm_b16_8 t662 = GM_LD(gm_b16_8, ((uint8_t *)&t661));
                gm_tftb1odc739t5 t663 = gm_f_push_3ef746b(t655, t662);
                if (((((uint8_t *)&t663))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t663) + 1, gm_srckfibkpt681aa, 589, 21);
                gm_b0_1 t664 = GM_LD(gm_b0_1, (((uint8_t *)&t663) + 1));
            }
            {
                void * t665 = (void *)((uint8_t *)&F.l4);
                void * t666 = gm_l0;
                gm_t88rcucg1b7uk t667 = gm_l48;
                uint32_t t668 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t666, t667);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t669;
                GM_ST(uint32_t, ((uint8_t *)&t669 + 0), t668);
                gm_tabb710d9mq2e t670;
                gm_f_format_ch41dev((void *)&t670, (void *)NULL, cx, NULL, t669);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t670))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t670) + 8, gm_srckfibkpt681aa, 590, 28);
                gm_b16_8 t671 = GM_LD(gm_b16_8, (((uint8_t *)&t670) + 8));
                gm_b16_8 t672 = GM_LD(gm_b16_8, ((uint8_t *)&t671));
                gm_tftb1odc739t5 t673 = gm_f_push_3ef746b(t665, t672);
                if (((((uint8_t *)&t673))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t673) + 1, gm_srckfibkpt681aa, 590, 21);
                gm_b0_1 t674 = GM_LD(gm_b0_1, (((uint8_t *)&t673) + 1));
            }
            {
                void * t675 = (void *)((uint8_t *)&F.l4);
                void * t676 = gm_l0;
                gm_t88rcucg1b7uk t677 = gm_l48;
                uint8_t t678 = gm_f_is_signed_integer_ige9b74(t676, GM_CONV(gm_csguankko4lmhd, t677));
                gm_t9qkh0rsooc8d t679;
                GM_ST(uint8_t, ((uint8_t *)&t679 + 0), t678);
                gm_tabb710d9mq2e t680;
                gm_f_format_onn3qvb((void *)&t680, (void *)NULL, cx, NULL, t679);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t680))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t680) + 8, gm_srckfibkpt681aa, 591, 28);
                gm_b16_8 t681 = GM_LD(gm_b16_8, (((uint8_t *)&t680) + 8));
                gm_b16_8 t682 = GM_LD(gm_b16_8, ((uint8_t *)&t681));
                gm_tftb1odc739t5 t683 = gm_f_push_3ef746b(t675, t682);
                if (((((uint8_t *)&t683))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t683) + 1, gm_srckfibkpt681aa, 591, 21);
                gm_b0_1 t684 = GM_LD(gm_b0_1, (((uint8_t *)&t683) + 1));
            }
            goto me412;
mn546:;
ma415:;
            uint8_t t686 = GM_LD(uint8_t, ((((uint8_t *)t411) + 24) + 0));
            if (t686 != ((uint8_t)2ULL)) goto mn685;
            {
                void * t688 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t689;
                GM_ST(void *, (uint8_t *)&t689, (void *)(gm_spi406027g4jo));
                GM_ST(uintptr_t, (uint8_t *)&t689 + 8, (uintptr_t)(18));
                gm_tftb1odc739t5 t690 = gm_f_push_3ef746b(t688, t689);
                if (((((uint8_t *)&t690))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t690) + 1, gm_srckfibkpt681aa, 594, 21);
                gm_b0_1 t691 = GM_LD(gm_b0_1, (((uint8_t *)&t690) + 1));
            }
            {
                void * t692 = (void *)((uint8_t *)&F.l4);
                void * t693 = gm_l31;
                gm_b16_8 t694 = GM_LD(gm_b16_8, (((uint8_t *)t693) + 0));
                gm_tnpifv20irnph t695;
                GM_ST(gm_b16_8, ((uint8_t *)&t695 + 0), t694);
                gm_tabb710d9mq2e t696;
                gm_f_format_gkjg5ld((void *)&t696, (void *)NULL, cx, NULL, t695);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t696))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t696) + 8, gm_srckfibkpt681aa, 595, 61);
                gm_b16_8 t697 = GM_LD(gm_b16_8, (((uint8_t *)&t696) + 8));
                gm_b16_8 t698 = GM_LD(gm_b16_8, ((uint8_t *)&t697));
                gm_b16_8 t699 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t698);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t700;
                GM_ST(gm_b16_8, ((uint8_t *)&t700 + 0), t699);
                gm_tabb710d9mq2e t701;
                gm_f_format_nbg44fl((void *)&t701, (void *)NULL, cx, NULL, t700);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t701))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t701) + 8, gm_srckfibkpt681aa, 595, 28);
                gm_b16_8 t702 = GM_LD(gm_b16_8, (((uint8_t *)&t701) + 8));
                gm_b16_8 t703 = GM_LD(gm_b16_8, ((uint8_t *)&t702));
                gm_tftb1odc739t5 t704 = gm_f_push_3ef746b(t692, t703);
                if (((((uint8_t *)&t704))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t704) + 1, gm_srckfibkpt681aa, 595, 21);
                gm_b0_1 t705 = GM_LD(gm_b0_1, (((uint8_t *)&t704) + 1));
            }
            goto me412;
mn685:;
ma416:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 552, 13);
me412:;
        }
        goto me57;
mn390:;
ma69:;
        {
            void * t708 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t709;
            GM_ST(void *, (uint8_t *)&t709, (void *)(gm_spi406027g4jo));
            GM_ST(uintptr_t, (uint8_t *)&t709 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t710 = gm_f_push_3ef746b(t708, t709);
            if (((((uint8_t *)&t710))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t710) + 1, gm_srckfibkpt681aa, 600, 13);
            gm_b0_1 t711 = GM_LD(gm_b0_1, (((uint8_t *)&t710) + 1));
        }
        {
            void * t712 = (void *)((uint8_t *)&F.l4);
            void * t713 = gm_l0;
            gm_t88rcucg1b7uk t714 = gm_l1;
            gm_b16_8 t715 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t713, t714);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t716;
            GM_ST(gm_b16_8, ((uint8_t *)&t716 + 0), t715);
            gm_tabb710d9mq2e t717;
            gm_f_format_gkjg5ld((void *)&t717, (void *)NULL, cx, NULL, t716);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t717))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t717) + 8, gm_srckfibkpt681aa, 601, 53);
            gm_b16_8 t718 = GM_LD(gm_b16_8, (((uint8_t *)&t717) + 8));
            gm_b16_8 t719 = GM_LD(gm_b16_8, ((uint8_t *)&t718));
            gm_b16_8 t720 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t719);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t721;
            GM_ST(gm_b16_8, ((uint8_t *)&t721 + 0), t720);
            gm_tabb710d9mq2e t722;
            gm_f_format_nbg44fl((void *)&t722, (void *)NULL, cx, NULL, t721);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t722))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t722) + 8, gm_srckfibkpt681aa, 601, 20);
            gm_b16_8 t723 = GM_LD(gm_b16_8, (((uint8_t *)&t722) + 8));
            gm_b16_8 t724 = GM_LD(gm_b16_8, ((uint8_t *)&t723));
            gm_tftb1odc739t5 t725 = gm_f_push_3ef746b(t712, t724);
            if (((((uint8_t *)&t725))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t725) + 1, gm_srckfibkpt681aa, 601, 13);
            gm_b0_1 t726 = GM_LD(gm_b0_1, (((uint8_t *)&t725) + 1));
        }
        goto me57;
mn706:;
ma70:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 489, 5);
me57:;
    }
    {
        gm_f_list_epvducg((void *)&F.l57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t728 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_l58 = t728;
        }
        {
            gm_l59 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t730 = gm_l59;
                uint64_t t731 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                uint8_t t732 = (t730 < t731);
                if (!t732) goto lb729;
                {
                    uint64_t t735 = gm_l59;
                    uint64_t t736 = (uintptr_t)t735;
                    uint64_t t737 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                    gm_b16_8 t740 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t736 * 16));
                    gm_l60 = t740;
                    {
                        void * t742 = (void *)((uint8_t *)&F.l57);
                        gm_b16_8 t743;
                        gm_b16_8 t745 = gm_l60;
                        gm_b16_8 t746;
                        GM_ST(void *, (uint8_t *)&t746, (void *)(gm_sl3q52ep6aa53));
                        GM_ST(uintptr_t, (uint8_t *)&t746 + 8, (uintptr_t)(1));
                        uint8_t t747 = gm_f_starts_with_6vvt01e(t745, t746);
                        if (!t747) goto ie744;
                        gm_b16_8 t749 = gm_l60;
                        gm_b16_8 t750 = t749;
                        t743 = t750;
                        goto ix744;
ie744:;
                        gm_b16_8 t752 = gm_l60;
                        gm_tnpifv20irnph t753;
                        GM_ST(gm_b16_8, ((uint8_t *)&t753 + 0), t752);
                        gm_tabb710d9mq2e t754;
                        gm_f_format_g0ibdjv((void *)&t754, (void *)NULL, cx, NULL, t753);
                        if (gm_unw.kind) {
                            goto gm_ll733;
                        }
                        if (((((uint8_t *)&t754))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t754) + 8, gm_srckfibkpt681aa, 605, 71);
                        gm_b16_8 t755 = GM_LD(gm_b16_8, (((uint8_t *)&t754) + 8));
                        gm_b16_8 t756 = t755;
                        gm_b16_8 t757 = GM_LD(gm_b16_8, ((uint8_t *)&t756));
                        t743 = t757;
ix744:;
                        gm_tftb1odc739t5 t758 = gm_f_push_3ef746b(t742, t743);
                        if (((((uint8_t *)&t758))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t758) + 1, gm_srckfibkpt681aa, 605, 24);
                        gm_b0_1 t759 = GM_LD(gm_b0_1, (((uint8_t *)&t758) + 1));
                    }
                    goto me738;
mn739:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 605, 5);
me738:;
                }
lc729:;
                {
                    uint64_t t761 = gm_l59;
                    uint64_t r762; bool o763 = __builtin_add_overflow(t761, ((uint64_t)1ULL), &r762);
                    uint64_t t764 = r762;
                    gm_l59 = t764;
                }
            }
gm_ll733:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb729; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc729; }
            }
            goto gm_lad;
lb729:;
        }
    }
    {
        void * t765 = gm_l0;
        void * t766 = (void *)(((uint8_t *)t765) + 368);
        gm_t6j6smj7qoctu t767;
        gm_b16_8 t768 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t767 + 0, t768);
        gm_b16_8 t769 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
        gm_b16_8 t770 = GM_LD(gm_b16_8, ((uint8_t *)&t769));
        gm_b16_8 t771;
        GM_ST(void *, (uint8_t *)&t771, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t771 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t772;
        gm_f_join_sfknljb((void *)&t772, (void *)NULL, cx, NULL, t770, t771);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t772))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t772) + 8, gm_srckfibkpt681aa, 606, 66);
        gm_b16_8 t773 = GM_LD(gm_b16_8, (((uint8_t *)&t772) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t767 + 16, t773);
        gm_tftb1odc739t5 t774 = gm_f_write_uf08lpv(t766, &t767);
        if (((((uint8_t *)&t774))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t774) + 1, gm_srckfibkpt681aa, 606, 5);
        gm_b0_1 t775 = GM_LD(gm_b0_1, (((uint8_t *)&t774) + 1));
    }
    {
        gm_b16_8 t776 = gm_l3;
        gm_tnpifv20irnph t777;
        GM_ST(gm_b16_8, ((uint8_t *)&t777 + 0), t776);
        gm_tabb710d9mq2e t778;
        gm_f_format_h6b2ofo((void *)&t778, (void *)NULL, cx, NULL, t777);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t778))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t778) + 8, gm_srckfibkpt681aa, 607, 12);
        gm_b16_8 t779 = GM_LD(gm_b16_8, (((uint8_t *)&t778) + 8));
        gm_b16_8 t780 = GM_LD(gm_b16_8, ((uint8_t *)&t779));
        uint8_t t781 = 0;
        F.ret = t780;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_cty_nfj8rcv F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t6j6smj7qoctu gm_l4; /* inner */
    gm_b16_8 gm_l5; /* m */
    GM_STACK_CHECK(&F, 921u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t5v8n6qrn4rh4 t4;
        gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t13 != ((uint8_t)2ULL)) goto mn12;
        gm_t6j6smj7qoctu t14;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_snokcvhtidt97));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t14);
        goto gm_fdone;
        goto me5;
mn12:;
ma7:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t20 != ((uint8_t)0ULL)) goto pa19;
        goto po18;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t22 != ((uint8_t)1ULL)) goto pa21;
        goto po18;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t24 != ((uint8_t)3ULL)) goto pa23;
        goto po18;
pa23:;
        goto mn17;
po18:;
        gm_t6j6smj7qoctu t25;
        void * t26 = gm_l0;
        gm_t88rcucg1b7uk t27 = gm_l1;
        gm_b16_8 t28 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t28);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t29);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
        GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t25);
        goto gm_fdone;
        goto me5;
mn17:;
ma8:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t33 != ((uint8_t)4ULL)) goto pa32;
        goto po31;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t35 != ((uint8_t)5ULL)) goto pa34;
        goto po31;
pa34:;
        goto mn30;
po31:;
        gm_ttu09lij36f8c t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me5;
mn30:;
ma9:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t39 != ((uint8_t)6ULL)) goto mn38;
        goto me5;
mn38:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 414, 5);
me5:;
    }
    {
        void * t41 = gm_l0;
        gm_t88rcucg1b7uk t42 = gm_l1;
        gm_t53at80pfdus3 t43;
        gm_f_kind_ghvav22((void *)&t43, t41, GM_CONV(gm_csguankko4lmhd, t42));
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
        switch (t49) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma47; case ((uint8_t)2ULL): goto ma47; case ((uint8_t)3ULL): goto ma47; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma47; case ((uint8_t)6ULL): goto ma47; case ((uint8_t)7ULL): goto ma47; case ((uint8_t)8ULL): goto ma47; case ((uint8_t)9ULL): goto ma47; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma45; case ((uint8_t)12ULL): goto ma47; case ((uint8_t)13ULL): goto ma47; case ((uint8_t)14ULL): goto ma47; case ((uint8_t)15ULL): goto ma47; case ((uint8_t)16ULL): goto ma47; case ((uint8_t)17ULL): goto ma46; case ((uint8_t)18ULL): goto ma47; case ((uint8_t)19ULL): goto ma47; case ((uint8_t)20ULL): goto ma47; case ((uint8_t)21ULL): goto ma47; case ((uint8_t)22ULL): goto ma47; case ((uint8_t)23ULL): goto ma47; case ((uint8_t)24ULL): goto ma47; case ((uint8_t)25ULL): goto ma47; case ((uint8_t)26ULL): goto ma47; default: goto ma47; }
ma45:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
        if (t51 != ((uint8_t)11ULL)) goto mn50;
        uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)&t43) + 8) + 0));
        gm_l2 = t52;
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t43) + 8) + 8));
        gm_l3 = t53;
        {
            uint64_t t56 = gm_l2;
            uint8_t t57 = ((t56 == ((uint64_t)0ULL)));
            if (!t57) goto ie55;
            {
                gm_ttu09lij36f8c t59;
                memset(&t59, 0, sizeof t59);
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            goto ix55;
ie55:;
ix55:;
        }
        {
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l3;
            gm_ttu09lij36f8c t63;
            gm_f_field_cty_nfj8rcv((void *)&t63, (void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t64;
            if (((((uint8_t *)&t63))[32] != 0)) {
                t64 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t63) + 0));
            } else {
                gm_ttu09lij36f8c t65;
                memset(&t65, 0, sizeof t65);
                uint8_t t66 = 0;
                F.ret = t65;
                goto gm_fret;
            }
            gm_l4 = t64;
        }
        {
            gm_t6j6smj7qoctu t67;
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t67 + 0, t68);
            gm_tf1bjg81u0glv t69;
            uint64_t t70 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t69 + 0, t70);
            gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
            GM_ST(gm_b16_8, (uint8_t *)&t69 + 8, t71);
            gm_tabb710d9mq2e t72;
            gm_f_format_6a91mh8((void *)&t72, (void *)NULL, cx, NULL, &t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srckfibkpt681aa, 424, 51);
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            GM_ST(gm_b16_8, (uint8_t *)&t67 + 16, t74);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
            GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t67);
            goto gm_fdone;
        }
        goto me44;
mn50:;
ma46:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t43) + 0));
        if (t76 != ((uint8_t)17ULL)) goto mn75;
        {
            void * t78 = gm_l0;
            gm_t88rcucg1b7uk t79 = gm_l1;
            gm_th2lqbo77pvsc t80;
            gm_f_mirror_lcjegsf((void *)&t80, (void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t81;
            if (((((uint8_t *)&t80))[16] != 0)) {
                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 0));
                t81 = t82;
            } else {
                gm_ttu09lij36f8c t83;
                memset(&t83, 0, sizeof t83);
                uint8_t t84 = 0;
                F.ret = t83;
                goto gm_fret;
            }
            gm_l5 = t81;
        }
        {
            gm_t6j6smj7qoctu t85;
            gm_b16_8 t86 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t86);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(0));
            GM_ST(gm_b16_8, (uint8_t *)&t85 + 16, t87);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 36, 0);
            ((uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out))[32] = 1;
            GM_ST(gm_t6j6smj7qoctu, (uint8_t *)&(*(gm_ttu09lij36f8c *)gm_out) + 0, t85);
            goto gm_fdone;
        }
        goto me44;
mn75:;
ma47:;
        gm_ttu09lij36f8c t89;
        memset(&t89, 0, sizeof t89);
        uint8_t t90 = 0;
        F.ret = t89;
        goto gm_fret;
        goto me44;
mn88:;
ma48:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 420, 5);
me44:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_fields_array_tikg33t F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* i */
    gm_b16_8 gm_l2; /* parts */
    void * gm_l3; /* f */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l3;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_so3qrsvnd4euf));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srckfibkpt681aa, 612, 9);
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
        }
        {
            gm_b0_1 t10;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 328);
        gm_t6j6smj7qoctu t14;
        gm_b16_8 t15 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
        gm_b16_8 t16 = gm_l2;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t18;
        gm_f_join_sfknljb((void *)&t18, (void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 8, gm_srckfibkpt681aa, 615, 72);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t19);
        gm_tftb1odc739t5 t20 = gm_f_write_3mmgun5(t13, &t14);
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srckfibkpt681aa, 615, 5);
        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
    }
    {
        void * t22 = gm_l3;
        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_t4ce0jd3tdro4 t24;
        GM_ST(uint64_t, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_aiomp5b((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srckfibkpt681aa, 616, 12);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t22, t27);
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srckfibkpt681aa, 616, 5);
        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
    }
    {
        void * t30 = gm_l3;
        gm_b16_8 t31 = gm_l1;
        gm_tnpifv20irnph t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
        gm_tabb710d9mq2e t33;
        gm_f_format_s51nisc((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srckfibkpt681aa, 617, 12);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t30, t35);
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srckfibkpt681aa, 617, 5);
        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_cty_s62af0i F;
    uint8_t gm_l0; /* signed */
    uint64_t gm_l1; /* storage */
    GM_STACK_CHECK_SHALLOW(&F, 502u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l1;
    gm_b16_8 t2;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&t1));
    if (!(t5 == ((uint64_t)1ULL))) goto mn4;
    gm_b16_8 t6;
    uint8_t t8 = gm_l0;
    if (!t8) goto ie7;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_suuq26jjqhul4));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(6));
    gm_b16_8 t11 = t10;
    t6 = t11;
    goto ix7;
ie7:;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s9grtrvtmi962));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
    gm_b16_8 t14 = t13;
    t6 = t14;
ix7:;
    t2 = t6;
    goto me3;
mn4:;
    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t1));
    if (!(t16 == ((uint64_t)2ULL))) goto mn15;
    gm_b16_8 t17;
    uint8_t t19 = gm_l0;
    if (!t19) goto ie18;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_std3g89n1j9ln));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
    gm_b16_8 t22 = t21;
    t17 = t22;
    goto ix18;
ie18:;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_suggn3kp9dfmg));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(8));
    gm_b16_8 t25 = t24;
    t17 = t25;
ix18:;
    t2 = t17;
    goto me3;
mn15:;
    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t1));
    if (!(t27 == ((uint64_t)4ULL))) goto mn26;
    gm_b16_8 t28;
    uint8_t t30 = gm_l0;
    if (!t30) goto ie29;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_ska87i9f7924f));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(7));
    gm_b16_8 t33 = t32;
    t28 = t33;
    goto ix29;
ie29:;
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s6r94a2k1goos));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(8));
    gm_b16_8 t36 = t35;
    t28 = t36;
ix29:;
    t2 = t28;
    goto me3;
mn26:;
    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t1));
    if (!(t38 == ((uint64_t)8ULL))) goto mn37;
    gm_b16_8 t39;
    uint8_t t41 = gm_l0;
    if (!t41) goto ie40;
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_so4g53vn8c2d4));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(7));
    gm_b16_8 t44 = t43;
    t39 = t44;
    goto ix40;
ie40:;
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(gm_shn8mjp9kc2n2));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(8));
    gm_b16_8 t47 = t46;
    t39 = t47;
ix40:;
    t2 = t39;
    goto me3;
mn37:;
    gm_b16_8 t49;
    uint8_t t51 = gm_l0;
    if (!t51) goto ie50;
    gm_b16_8 t53;
    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s1a1r1qe3ssot));
    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(8));
    gm_b16_8 t54 = t53;
    t49 = t54;
    goto ix50;
ie50:;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_st9g0aublffth));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(17));
    gm_b16_8 t57 = t56;
    t49 = t57;
ix50:;
    t2 = t49;
    goto me3;
mn48:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 629, 56);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_fm7q9br F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma8:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t14 != ((uint8_t)11ULL)) goto mn13;
    uint8_t t15 = GM_LD(uint8_t, (((((uint8_t *)&t5) + 8) + 12) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn13;
    t6 = ((uint8_t)1);
    goto me7;
mn13:;
ma9:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    if (t17 != ((uint8_t)11ULL)) goto mn16;
    uint8_t t18 = GM_LD(uint8_t, (((((uint8_t *)&t5) + 8) + 12) + 0));
    if (t18 != ((uint8_t)3ULL)) goto mn16;
    t6 = ((uint8_t)1);
    goto me7;
mn16:;
ma10:;
    t6 = ((uint8_t)0);
    goto me7;
mn19:;
ma11:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 115, 54);
me7:;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1) { gm_t53at80pfdus3 gm_r; gm_f_kind_ghvav22(&gm_r, a0, a1); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0) { return gm_f_kind_rank_g1kdlst(&a0); }
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_cty_3kvgdd5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint64_t gm_l3; /* storage */
    gm_b16_8 gm_l4; /* n */
    uint64_t gm_l5; /* s */
    uint64_t gm_l6; /* a */
    gm_tl572d7579hns gm_l7; /* kind */
    gm_b16_8 gm_l8; /* name */
    GM_STACK_CHECK(&F, 738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t5v8n6qrn4rh4 t4;
        gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma14; default: goto ma15; }
ma6:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l2 = t19;
        uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 8));
        gm_l3 = t20;
        uint8_t t21 = gm_l2;
        uint64_t t22 = gm_l3;
        gm_b16_8 t23 = gm_f_int_cty_s62af0i(t21, t22);
        uint8_t t24 = 0;
        F.ret = t23;
        goto gm_fret;
        goto me5;
mn17:;
ma7:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        uint16_t t27 = GM_LD(uint16_t, (((uint8_t *)&t4) + 8));
        if (!(t27 == ((uint16_t)16ULL))) goto mn25;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_scj0sm2kpivk4));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(8));
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me5;
mn25:;
ma8:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t31 != ((uint8_t)1ULL)) goto mn30;
        uint16_t t32 = GM_LD(uint16_t, (((uint8_t *)&t4) + 8));
        if (!(t32 == ((uint16_t)32ULL))) goto mn30;
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s4cess8gum6i7));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(5));
        uint8_t t34 = 0;
        F.ret = t33;
        goto gm_fret;
        goto me5;
mn30:;
ma9:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t36 != ((uint8_t)1ULL)) goto mn35;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s866ofni9sb8u));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(6));
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me5;
mn35:;
ma10:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t40 != ((uint8_t)2ULL)) goto mn39;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9grtrvtmi962));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
        uint8_t t42 = 0;
        F.ret = t41;
        goto gm_fret;
        goto me5;
mn39:;
ma11:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t44 != ((uint8_t)3ULL)) goto mn43;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s44ek1gecbajs));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(6));
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me5;
mn43:;
ma12:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t48 != ((uint8_t)4ULL)) goto mn47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s6g2mpli2mu72));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(10));
        uint8_t t50 = 0;
        F.ret = t49;
        goto gm_fret;
        goto me5;
mn47:;
ma13:;
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t52 != ((uint8_t)5ULL)) goto mn51;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_smiavg3q7ucd9));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(7));
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
        goto me5;
mn51:;
ma14:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t56 != ((uint8_t)6ULL)) goto mn55;
        {
            void * t59 = gm_l0;
            gm_t88rcucg1b7uk t60 = gm_l1;
            gm_th2lqbo77pvsc t61;
            gm_f_typed_stkcgvr((void *)&t61, (void *)NULL, cx, NULL, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t61))[16] != 0)) goto ie58;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            gm_l4 = t62;
            {
                gm_b16_8 t64 = gm_l4;
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            void * t66 = gm_l0;
            gm_t88rcucg1b7uk t67 = gm_l1;
            uint64_t t68 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t68;
        }
        {
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l1;
            uint64_t t71 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t72 = (t71 > ((uint64_t)1ULL) ? t71 : ((uint64_t)1ULL));
            gm_l6 = t72;
        }
        {
            void * t73 = gm_l0;
            gm_t88rcucg1b7uk t74 = gm_l1;
            uint64_t t75 = gm_l5;
            uint64_t t76 = gm_l6;
            gm_f_blob_kind_kp13j9k((void *)&gm_l7, (void *)NULL, cx, NULL, t73, t74, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint64_t t77 = gm_l5;
            uint64_t t78 = gm_l6;
            gm_b16_8 t79 = gm_f_blob_name_sjpa1ub((void *)NULL, cx, NULL, t77, t78, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t79;
        }
        {
            void * t81 = gm_l0;
            void * t82 = (void *)(((uint8_t *)t81) + 120);
            gm_b16_8 t83 = gm_l8;
            gm_tqs5p4n2tq19v t84 = gm_f_get_p217beu(t82, t83);
            uint8_t t85;
            if (((((uint8_t *)&t84))[1] != 0)) goto mn87;
            t85 = ((uint8_t)1);
            goto me86;
mn87:;
            t85 = ((uint8_t)0);
            goto me86;
mn88:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 184, 16);
me86:;
            if (!t85) goto ie80;
            {
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 120);
                gm_b16_8 t92 = gm_l8;
                gm_tftb1odc739t5 t93 = gm_f_put_5c6qgui(t91, t92, ((uint8_t)1));
                if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srckfibkpt681aa, 185, 17);
                gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
            }
            {
                void * t95 = gm_l0;
                void * t96 = (void *)(((uint8_t *)t95) + 80);
                gm_tulpvg6gm4fqa t97;
                uint64_t t98 = gm_l5;
                GM_ST(uint64_t, (uint8_t *)&t97 + 0, t98);
                uint64_t t99 = gm_l6;
                GM_ST(uint64_t, (uint8_t *)&t97 + 8, t99);
                memcpy((uint8_t *)&t97 + 16, ((uint8_t *)&gm_l7), 24);
                void * t100 = gm_l0;
                uint64_t t101 = gm_l5;
                uint64_t t102 = gm_l6;
                gm_b16_8 t103 = gm_l8;
                gm_b16_8 t104 = gm_f_blob_typedef_8ta3rbi((void *)NULL, cx, NULL, t100, t101, t102, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)), t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t97 + 40, t104);
                gm_tftb1odc739t5 t105 = gm_f_push_pmmlu39(t96, &t97);
                if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srckfibkpt681aa, 186, 17);
                gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
            }
            goto ix80;
ie80:;
ix80:;
        }
        {
            gm_b16_8 t107 = gm_l8;
            uint8_t t108 = 0;
            F.ret = t107;
            goto gm_fret;
        }
        goto me5;
mn55:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 169, 5);
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
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_mirror_rudvl6p F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* keyword */
    gm_b16_8 gm_l4; /* fields */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* explicit */
    gm_t88rcucg1b7uk gm_l8; /* u */
    gm_b16_8 gm_l9; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_th2lqbo77pvsc gm_l19; /* n */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t6j6smj7qoctu gm_l28; /* fc */
    uint64_t gm_l29; /* a */
    gm_b16_8 gm_l30; /* al */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_b16_8 gm_l33; /* n */
    uint64_t gm_l35; /* a */
    gm_b16_8 gm_l36; /* attr_text */
    gm_b16_8 gm_l37; /* content */
    gm_b16_8 gm_l38; /* n */
    gm_b16_8 gm_l39; /* name */
    gm_b16_8 gm_l40; /* offs */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    GM_STACK_CHECK(&F, 3880u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_ghvav22((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(6));
            gm_l3 = t9;
        }
        {
            gm_b16_8 t10 = gm_f_empty_slice_ose1ba9();
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
            gm_l4 = t11;
        }
        {
            gm_l5 = ((uint8_t)0);
        }
        {
            gm_tf5srh4mp4b3i t12;
            memset(&t12, 0, sizeof t12);
            gm_l6 = t12;
        }
        {
            uint32_t t13 = gm_l2;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1088) + 0) + 8);
            if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srckfibkpt681aa, 351, 11);
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            switch (t24) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma21; default: goto ma23; }
ma20:;
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t26 != ((uint8_t)0ULL)) goto mn25;
            gm_tpautr4br7h3l t27 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8));
            F.l7 = t27;
            {
                if (!(((((uint8_t *)&F.l7) + 40))[4] != 0)) goto ie29;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 40));
                gm_l8 = t30;
                {
                    void * t32 = gm_l0;
                    gm_t88rcucg1b7uk t33 = gm_l8;
                    gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t32, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t34);
                    goto gm_fdone;
                }
                goto ix29;
ie29:;
ix29:;
            }
            {
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_l4 = t36;
            }
            {
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 16));
                gm_l5 = t37;
            }
            {
                gm_tf5srh4mp4b3i t38 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l7) + 24));
                gm_l6 = t38;
            }
            goto me19;
mn25:;
ma21:;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t40 != ((uint8_t)2ULL)) goto mn39;
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8));
            gm_l9 = t41;
            {
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s8p1h96ji9kh0));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(5));
                gm_l3 = t43;
            }
            {
                gm_b16_8 t44 = gm_l9;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_l4 = t45;
            }
            goto me19;
mn39:;
ma22:;
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t47 != ((uint8_t)1ULL)) goto mn46;
            gm_th2lqbo77pvsc t48;
            memset(&t48, 0, sizeof t48);
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
            goto me19;
mn46:;
ma23:;
            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 351, 5);
me19:;
        }
        {
            uint8_t t51 = 0;
            uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t53 = ((t52 == ((uint64_t)0ULL)));
            if (!t53) {
                void * t54 = gm_l0;
                gm_t88rcucg1b7uk t55 = gm_l1;
                uint64_t t56 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t57 = ((t56 == ((uint64_t)0ULL)));
                t51 = t57;
            } else t51 = 1;
            if (!t51) goto ie50;
            {
                gm_th2lqbo77pvsc t59;
                memset(&t59, 0, sizeof t59);
                uint8_t t60 = 0;
                F.ret = t59;
                goto gm_fret;
            }
            goto ix50;
ie50:;
ix50:;
        }
        {
            gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = gm_l0;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 808));
            if (!t63) goto ie61;
            {
                gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t66 = gm_l4;
                    gm_l12 = t66;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t68 = gm_l13;
                        uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t70 = (t68 < t69);
                        if (!t70) goto lb67;
                        {
                            uint64_t t73 = gm_l13;
                            uint64_t t74 = (uintptr_t)t73;
                            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t0mglksrkfa5e t78 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t74 * 40));
                            F.l14 = t78;
                            {
                                void * t80 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t80, t81);
                                if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srckfibkpt681aa, 369, 27);
                                gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                            }
                            goto me76;
mn77:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 369, 9);
me76:;
                        }
lc67:;
                        {
                            uint64_t t85 = gm_l13;
                            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                            uint64_t t88 = r86;
                            gm_l13 = t88;
                        }
                    }
lb67:;
                }
            }
            {
                gm_f_hash_map_cuh4fs9((void *)&F.l15, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                    void * t92 = (void *)((uint8_t *)&F.l15);
                    gm_b16_8 t93 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t91, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                    gm_l16 = t94;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t96 = gm_l17;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb95;
                        {
                            gm_t13urt52695sh t101;
                            uint64_t t102 = gm_l17;
                            GM_ST(uint64_t, (uint8_t *)&t101 + 0, t102);
                            uint64_t t103 = gm_l17;
                            uint64_t t104 = (uintptr_t)t103;
                            uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            memcpy((uint8_t *)&t101 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t104 * 24), 24);
                            uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&t101) + 0));
                            gm_l18 = t108;
                            gm_l19 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t101) + 8));
                            {
                                void * t110 = (void *)((uint8_t *)&F.l10);
                                gm_th2lqbo77pvsc t111 = gm_l19;
                                gm_b16_8 t112;
                                if (((((uint8_t *)&t111))[16] != 0)) {
                                    gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 0));
                                    t112 = t113;
                                } else {
                                    uint64_t t114 = gm_l18;
                                    gm_t4ce0jd3tdro4 t115;
                                    GM_ST(uint64_t, ((uint8_t *)&t115 + 0), t114);
                                    gm_tabb710d9mq2e t116;
                                    gm_f_format_tc7tn98((void *)&t116, (void *)NULL, cx, NULL, t115);
                                    if (gm_unw.kind) {
                                        goto gm_ll99;
                                    }
                                    if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_srckfibkpt681aa, 371, 94);
                                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                                    t112 = t118;
                                }
                                gm_tftb1odc739t5 t119 = gm_f_push_3ef746b(t110, t112);
                                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srckfibkpt681aa, 371, 76);
                                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
                            }
                            goto me106;
mn107:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 371, 9);
me106:;
                        }
lc95:;
                        {
                            uint64_t t122 = gm_l17;
                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                            uint64_t t125 = r123;
                            gm_l17 = t125;
                        }
                    }
gm_ll99:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb95; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc95; }
                    }
                    goto gm_lad;
lb95:;
                }
            }
            goto ix61;
ie61:;
            {
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_l20 = t128;
                }
                {
                    for (;;) {
                        uint64_t t130 = gm_l21;
                        uint64_t t131 = gm_l20;
                        uint8_t t132 = (t130 < t131);
                        if (!t132) goto lb129;
                        {
                            uint64_t t135 = gm_l21;
                            uint64_t t138 = GM_LD(uint64_t, ((uint8_t *)&t135));
                            gm_l22 = t138;
                            {
                                void * t140 = (void *)((uint8_t *)&F.l10);
                                uint64_t t141 = gm_l22;
                                gm_t4ce0jd3tdro4 t142;
                                GM_ST(uint64_t, ((uint8_t *)&t142 + 0), t141);
                                gm_tabb710d9mq2e t143;
                                gm_f_format_tc7tn98((void *)&t143, (void *)NULL, cx, NULL, t142);
                                if (gm_unw.kind) {
                                    goto gm_ll133;
                                }
                                if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srckfibkpt681aa, 373, 45);
                                gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                                gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                                gm_tftb1odc739t5 t146 = gm_f_push_3ef746b(t140, t145);
                                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srckfibkpt681aa, 373, 34);
                                gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
                            }
                            goto me136;
mn137:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 373, 9);
me136:;
                        }
lc129:;
                        {
                            uint64_t t149 = gm_l21;
                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                            uint64_t t152 = r150;
                            gm_l21 = t152;
                        }
                    }
gm_ll133:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb129; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc129; }
                    }
                    goto gm_lad;
lb129:;
                }
            }
ix61:;
        }
        {
            gm_f_list_epvducg((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t154 = gm_l4;
                gm_l24 = t154;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t156 = gm_l25;
                    uint64_t t157 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t158 = (t156 < t157);
                    if (!t158) goto lb155;
                    {
                        gm_t8ta482bl6be0 t161;
                        uint64_t t162 = gm_l25;
                        GM_ST(uint64_t, (uint8_t *)&t161 + 0, t162);
                        uint64_t t163 = gm_l25;
                        uint64_t t164 = (uintptr_t)t163;
                        uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        memcpy((uint8_t *)&t161 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t164 * 40), 40);
                        uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&t161) + 0));
                        gm_l26 = t168;
                        F.l27 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t161) + 8));
                        {
                            void * t170 = gm_l0;
                            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            gm_ttu09lij36f8c t172;
                            gm_f_field_cty_nfj8rcv((void *)&t172, (void *)NULL, cx, NULL, t170, t171);
                            if (gm_unw.kind) {
                                goto gm_ll159;
                            }
                            gm_t6j6smj7qoctu t173;
                            if (((((uint8_t *)&t172))[32] != 0)) {
                                t173 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t172) + 0));
                            } else {
                                gm_th2lqbo77pvsc t174;
                                memset(&t174, 0, sizeof t174);
                                uint8_t t175 = 0;
                                F.ret = t174;
                                goto gm_fret;
                            }
                            gm_l28 = t173;
                        }
                        {
                            gm_b16_8 t176;
                            if (!(((((uint8_t *)&F.l27) + 24))[8] != 0)) goto ie177;
                            uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&F.l27) + 24));
                            gm_l29 = t178;
                            uint64_t t180 = gm_l29;
                            gm_t4ce0jd3tdro4 t181;
                            GM_ST(uint64_t, ((uint8_t *)&t181 + 0), t180);
                            gm_tabb710d9mq2e t182;
                            gm_f_format_irg2n6e((void *)&t182, (void *)NULL, cx, NULL, t181);
                            if (gm_unw.kind) {
                                goto gm_ll159;
                            }
                            if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 8, gm_srckfibkpt681aa, 378, 40);
                            gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&t182) + 8));
                            gm_b16_8 t184 = t183;
                            gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
                            t176 = t185;
                            goto ix177;
ie177:;
                            gm_b16_8 t187;
                            GM_ST(void *, (uint8_t *)&t187, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(0));
                            gm_b16_8 t188 = t187;
                            t176 = t188;
ix177:;
                            gm_l30 = t176;
                        }
                        {
                            void * t189 = (void *)((uint8_t *)&F.l23);
                            gm_t0uuhaljsf410 t190;
                            gm_b16_8 t191 = gm_l30;
                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t191);
                            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 16, t192);
                            uint64_t t193 = gm_l26;
                            uint64_t t194 = (uintptr_t)t193;
                            uint64_t t195 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                            if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srckfibkpt681aa, 379, 59);
                            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t194 * 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 32, t196);
                            gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 16));
                            GM_ST(gm_b16_8, (uint8_t *)&t190 + 48, t197);
                            gm_tabb710d9mq2e t198;
                            gm_f_format_tra8sop((void *)&t198, (void *)NULL, cx, NULL, &t190);
                            if (gm_unw.kind) {
                                goto gm_ll159;
                            }
                            if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srckfibkpt681aa, 379, 22);
                            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t198) + 8));
                            gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                            gm_tftb1odc739t5 t201 = gm_f_push_3ef746b(t189, t200);
                            if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srckfibkpt681aa, 379, 9);
                            gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                        }
                        goto me166;
mn167:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 376, 5);
me166:;
                    }
lc155:;
                    {
                        uint64_t t204 = gm_l25;
                        uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                        uint64_t t207 = r205;
                        gm_l25 = t207;
                    }
                }
gm_ll159:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb155; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc155; }
                }
                goto gm_lad;
lb155:;
            }
        }
        {
            void * t209 = gm_l0;
            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)t209) + 808));
            if (!t210) goto ie208;
            {
                {
                    gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_l31 = t213;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t215 = gm_l32;
                        uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t217 = (t215 < t216);
                        if (!t217) goto lb214;
                        {
                            uint64_t t220 = gm_l32;
                            uint64_t t221 = (uintptr_t)t220;
                            uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t221 * 16));
                            gm_l33 = t225;
                            {
                                gm_b16_8 t228 = gm_l33;
                                gm_th2lqbo77pvsc t229;
                                gm_f_member_name_mj2bvs3((void *)&t229, (void *)NULL, cx, NULL, t228);
                                if (gm_unw.kind) {
                                    goto gm_ll218;
                                }
                                uint8_t t230;
                                if (((((uint8_t *)&t229))[16] != 0)) goto mn232;
                                t230 = ((uint8_t)0);
                                goto me231;
mn232:;
                                t230 = ((uint8_t)1);
                                goto me231;
mn233:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 383, 16);
me231:;
                                if (!t230) goto ie227;
                                {
                                    void * t235 = gm_l0;
                                    gm_b16_8 t236 = gm_l33;
                                    gm_b0_1 t237 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t235, t236);
                                    if (gm_unw.kind) {
                                        goto gm_ll218;
                                    }
                                }
                                goto ix227;
ie227:;
ix227:;
                            }
                            goto me223;
mn224:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 382, 9);
me223:;
                        }
lc214:;
                        {
                            uint64_t t239 = gm_l32;
                            uint64_t r240; bool o241 = __builtin_add_overflow(t239, ((uint64_t)1ULL), &r240);
                            uint64_t t242 = r240;
                            gm_l32 = t242;
                        }
                    }
gm_ll218:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb214; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc214; }
                    }
                    goto gm_lad;
lb214:;
                }
            }
            goto ix208;
ie208:;
ix208:;
        }
        {
            gm_f_list_epvducg((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t244 = gm_l5;
            if (!t244) goto ie243;
            {
                void * t246 = (void *)((uint8_t *)&F.l34);
                gm_b16_8 t247;
                GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s2ph9f020upd2));
                GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t248 = gm_f_push_3ef746b(t246, t247);
                if (((((uint8_t *)&t248))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t248) + 1, gm_srckfibkpt681aa, 387, 17);
                gm_b0_1 t249 = GM_LD(gm_b0_1, (((uint8_t *)&t248) + 1));
            }
            goto ix243;
ie243:;
ix243:;
        }
        {
            if (!((((uint8_t *)&gm_l6))[8] != 0)) goto ie250;
            uint64_t t251 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
            gm_l35 = t251;
            {
                void * t253 = (void *)((uint8_t *)&F.l34);
                uint64_t t254 = gm_l35;
                gm_t4ce0jd3tdro4 t255;
                GM_ST(uint64_t, ((uint8_t *)&t255 + 0), t254);
                gm_tabb710d9mq2e t256;
                gm_f_format_mfspujt((void *)&t256, (void *)NULL, cx, NULL, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t256))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t256) + 8, gm_srckfibkpt681aa, 388, 35);
                gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&t256) + 8));
                gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                gm_tftb1odc739t5 t259 = gm_f_push_3ef746b(t253, t258);
                if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srckfibkpt681aa, 388, 24);
                gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
            }
            goto ix250;
ie250:;
ix250:;
        }
        {
            gm_b16_8 t261;
            uint64_t t263 = GM_LD(uintptr_t, (((uint8_t *)&F.l34) + 0) + 8);
            uint8_t t264 = ((t263 == ((uint64_t)0ULL)));
            if (!t264) goto ie262;
            gm_b16_8 t266;
            GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(0));
            gm_b16_8 t267 = t266;
            t261 = t267;
            goto ix262;
ie262:;
            gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
            gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
            gm_b16_8 t271;
            GM_ST(void *, (uint8_t *)&t271, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t272;
            gm_f_join_sfknljb((void *)&t272, (void *)NULL, cx, NULL, t270, t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t272))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t272) + 8, gm_srckfibkpt681aa, 389, 98);
            gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&t272) + 8));
            gm_tnpifv20irnph t274;
            GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t273);
            gm_tabb710d9mq2e t275;
            gm_f_format_p144a0o((void *)&t275, (void *)NULL, cx, NULL, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t275))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t275) + 8, gm_srckfibkpt681aa, 389, 63);
            gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&t275) + 8));
            gm_b16_8 t277 = t276;
            gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
            t261 = t278;
ix262:;
            gm_l36 = t261;
        }
        {
            gm_tfeab279c16rr t279;
            gm_b16_8 t280 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t279 + 0, t280);
            gm_b16_8 t281 = gm_l36;
            GM_ST(gm_b16_8, (uint8_t *)&t279 + 16, t281);
            gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
            gm_b16_8 t284;
            GM_ST(void *, (uint8_t *)&t284, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t285;
            gm_f_join_sfknljb((void *)&t285, (void *)NULL, cx, NULL, t283, t284);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 8, gm_srckfibkpt681aa, 391, 58);
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&t285) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t279 + 32, t286);
            gm_tabb710d9mq2e t287;
            gm_f_format_19e6va7((void *)&t287, (void *)NULL, cx, NULL, &t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 8, gm_srckfibkpt681aa, 391, 16);
            gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&t287) + 8));
            gm_l37 = t288;
        }
        {
            void * t290 = gm_l0;
            void * t291 = (void *)(((uint8_t *)t290) + 656);
            gm_b16_8 t292 = gm_l37;
            gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&t292));
            gm_th2lqbo77pvsc t294;
            gm_f_get_e3joqcc((void *)&t294, t291, t293);
            if (!((((uint8_t *)&t294))[16] != 0)) goto ie289;
            gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
            gm_l38 = t295;
            {
                gm_b16_8 t297 = gm_l38;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t297);
                goto gm_fdone;
            }
            goto ix289;
ie289:;
ix289:;
        }
        {
            gm_b16_8 t298;
            GM_ST(void *, (uint8_t *)&t298, (void *)(gm_scttf7jtr4fn9));
            GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(5));
            gm_b16_8 t299 = gm_l37;
            gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
            gm_b16_8 t301 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t298, t300);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l39 = t301;
        }
        {
            void * t302 = gm_l0;
            void * t303 = (void *)(((uint8_t *)t302) + 656);
            gm_b16_8 t304 = gm_l37;
            gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
            gm_b16_8 t306 = gm_l39;
            gm_tftb1odc739t5 t307 = gm_f_put_7shfu9u(t303, t305, t306);
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 1, gm_srckfibkpt681aa, 394, 5);
            gm_b0_1 t308 = GM_LD(gm_b0_1, (((uint8_t *)&t307) + 1));
        }
        {
            void * t309 = gm_l0;
            void * t310 = (void *)(((uint8_t *)t309) + 712);
            gm_tud7oovfvnf43 t311;
            gm_b16_8 t312 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t311 + 0, t312);
            gm_b16_8 t313 = gm_l36;
            GM_ST(gm_b16_8, (uint8_t *)&t311 + 16, t313);
            gm_b16_8 t314 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t311 + 32, t314);
            gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&F.l23) + 0));
            gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
            gm_b16_8 t317;
            GM_ST(void *, (uint8_t *)&t317, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t318;
            gm_f_join_sfknljb((void *)&t318, (void *)NULL, cx, NULL, t316, t317);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_srckfibkpt681aa, 395, 86);
            gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t311 + 48, t319);
            gm_b16_8 t320 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t311 + 64, t320);
            gm_tftb1odc739t5 t321 = gm_f_write_712qp7n(t310, &t311);
            if (((((uint8_t *)&t321))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t321) + 1, gm_srckfibkpt681aa, 395, 5);
            gm_b0_1 t322 = GM_LD(gm_b0_1, (((uint8_t *)&t321) + 1));
        }
        {
            void * t323 = gm_l0;
            void * t324 = (void *)(((uint8_t *)t323) + 712);
            gm_thddbqb6snabm t325;
            gm_b16_8 t326 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t325 + 0, t326);
            void * t327 = gm_l0;
            gm_t88rcucg1b7uk t328 = gm_l1;
            uint64_t t329 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t327, t328);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t325 + 16, t329);
            gm_b16_8 t330 = gm_l39;
            GM_ST(gm_b16_8, (uint8_t *)&t325 + 24, t330);
            void * t331 = gm_l0;
            gm_t88rcucg1b7uk t332 = gm_l1;
            uint64_t t333 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t331, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t325 + 40, t333);
            gm_tftb1odc739t5 t334 = gm_f_write_cscq5c8(t324, &t325);
            if (((((uint8_t *)&t334))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t334) + 1, gm_srckfibkpt681aa, 397, 5);
            gm_b0_1 t335 = GM_LD(gm_b0_1, (((uint8_t *)&t334) + 1));
        }
        {
            void * t336 = gm_l0;
            gm_t88rcucg1b7uk t337 = gm_l1;
            gm_b16_8 t338 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t336, t337);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l40 = t338;
        }
        {
            {
                gm_l42 = ((uint64_t)0ULL);
            }
            {
                uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                gm_l41 = t340;
            }
            {
                for (;;) {
                    uint64_t t342 = gm_l42;
                    uint64_t t343 = gm_l41;
                    uint8_t t344 = (t342 < t343);
                    if (!t344) goto lb341;
                    {
                        uint64_t t347 = gm_l42;
                        uint64_t t350 = GM_LD(uint64_t, ((uint8_t *)&t347));
                        gm_l43 = t350;
                        {
                            uint64_t t353 = gm_l43;
                            uint64_t t354 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                            uint8_t t355 = (t353 < t354);
                            if (!t355) goto ie352;
                            {
                                void * t357 = gm_l0;
                                void * t358 = (void *)(((uint8_t *)t357) + 712);
                                gm_tftksr4op114e t359;
                                gm_b16_8 t360 = gm_l39;
                                GM_ST(gm_b16_8, (uint8_t *)&t359 + 0, t360);
                                uint64_t t361 = gm_l43;
                                uint64_t t362 = (uintptr_t)t361;
                                uint64_t t363 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                if (t362 >= (uintptr_t)(t363)) gm_trap_index(t362, t363, gm_srckfibkpt681aa, 400, 84);
                                gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t362 * 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t359 + 16, t364);
                                uint64_t t365 = gm_l43;
                                uint64_t t366 = (uintptr_t)t365;
                                uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                if (t366 >= (uintptr_t)(t367)) gm_trap_index(t366, t367, gm_srckfibkpt681aa, 400, 100);
                                uint64_t t368 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t366 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t359 + 32, t368);
                                gm_tftb1odc739t5 t369 = gm_f_write_3f2sd55(t358, &t359);
                                if (((((uint8_t *)&t369))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t369) + 1, gm_srckfibkpt681aa, 400, 27);
                                gm_b0_1 t370 = GM_LD(gm_b0_1, (((uint8_t *)&t369) + 1));
                            }
                            goto ix352;
ie352:;
ix352:;
                        }
                        goto me348;
mn349:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 399, 5);
me348:;
                    }
lc341:;
                    {
                        uint64_t t372 = gm_l42;
                        uint64_t r373; bool o374 = __builtin_add_overflow(t372, ((uint64_t)1ULL), &r373);
                        uint64_t t375 = r373;
                        gm_l42 = t375;
                    }
                }
lb341:;
            }
        }
        {
            void * t376 = gm_l0;
            void * t377 = (void *)(((uint8_t *)t376) + 712);
            void * t378 = gm_l0;
            gm_t88rcucg1b7uk t379 = gm_l1;
            gm_b16_8 t380 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t378, t379);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t381;
            GM_ST(gm_b16_8, ((uint8_t *)&t381 + 0), t380);
            gm_tabb710d9mq2e t382;
            gm_f_format_1cgbgnn((void *)&t382, (void *)NULL, cx, NULL, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t382))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t382) + 8, gm_srckfibkpt681aa, 402, 53);
            gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&t382) + 8));
            gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
            gm_b16_8 t385 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t384);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t386;
            GM_ST(gm_b16_8, ((uint8_t *)&t386 + 0), t385);
            gm_tftb1odc739t5 t387 = gm_f_write_pecnce5(t377, t386);
            if (((((uint8_t *)&t387))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t387) + 1, gm_srckfibkpt681aa, 402, 5);
            gm_b0_1 t388 = GM_LD(gm_b0_1, (((uint8_t *)&t387) + 1));
        }
        {
            gm_b16_8 t389 = gm_l39;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t389);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t391;
            memset(&t391, 0, sizeof t391);
            uint8_t t392 = 0;
            F.ret = t391;
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
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_typed_2bh0dpj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l5; /* ec */
    gm_b16_8 gm_l6; /* members */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ft */
    gm_t88rcucg1b7uk gm_l14; /* inner */
    gm_b16_8 gm_l15; /* offs */
    gm_t88rcucg1b7uk gm_l16; /* err */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    gm_tqsk8dt6ib3qq gm_l19; /* pair */
    gm_tgbgti82mf879 gm_l20; /* p */
    uint32_t gm_l21; /* id */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t88rcucg1b7uk gm_l28; /* tag */
    gm_b16_8 gm_l29; /* variants */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    gm_tgbgti82mf879 gm_l36; /* p */
    uint64_t gm_l39; /* cursor */
    uint64_t gm_l40; /* natural */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    uint64_t gm_l45; /* x */
    uint64_t gm_l46; /* x */
    gm_b16_8 gm_l47; /* alignas */
    uint64_t gm_l48; /* a */
    GM_STACK_CHECK(&F, 4318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        void * t5 = gm_l0;
        uint8_t t6 = GM_LD(uint8_t, (((((uint8_t *)t5) + 8) + 0) + 56));
        if (!t6) {
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((((uint8_t *)t7) + 8) + 0) + 0));
            uint8_t t9 = (!(t8 == ((uint64_t)8ULL)));
            t4 = t9;
        } else t4 = 1;
        if (!t4) {
            void * t10 = gm_l0;
            gm_t88rcucg1b7uk t11 = gm_l1;
            uint64_t t12 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_th2lqbo77pvsc t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_792g2go((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_t53at80pfdus3 t19;
        gm_f_kind_ghvav22((void *)&t19, t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma25; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; default: goto ma26; }
ma21:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t30 != ((uint8_t)11ULL)) goto mn29;
        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)&t19) + 8) + 0));
        gm_l3 = t31;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t19) + 8) + 8));
        gm_l4 = t32;
        {
            uint8_t t35 = 0;
            uint64_t t36 = gm_l3;
            uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
            if (!t37) {
                void * t38 = gm_l0;
                gm_t88rcucg1b7uk t39 = gm_l4;
                uint64_t t40 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
                t35 = t41;
            } else t35 = 1;
            if (!t35) goto ie34;
            {
                gm_th2lqbo77pvsc t43;
                memset(&t43, 0, sizeof t43);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l4;
            gm_b16_8 t47 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t47;
        }
        {
            gm_t3877pk7lv748 t48;
            gm_b16_8 t49 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
            uint64_t t50 = gm_l3;
            GM_ST(uint64_t, (uint8_t *)&t48 + 16, t50);
            gm_tabb710d9mq2e t51;
            gm_f_format_19ftgop((void *)&t51, (void *)NULL, cx, NULL, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 8, gm_srckfibkpt681aa, 225, 24);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 8));
            gm_l6 = t52;
        }
        {
            gm_f_list_cfvjofe((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t53 = (void *)((uint8_t *)&F.l7);
            gm_tftb1odc739t5 t54 = gm_f_push_q9fsb22(t53, &(*(gm_t3877pk7lv748 *)((uint8_t *)gm_kr85ffi32os)));
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srckfibkpt681aa, 227, 13);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l1;
            gm_b16_8 t58 = gm_l6;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            void * t62 = gm_l0;
            gm_t88rcucg1b7uk t63 = gm_l4;
            uint64_t t64 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t65 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t56, t57, t59, t61, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t65);
            goto gm_fdone;
        }
        goto me20;
mn29:;
ma22:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t67 != ((uint8_t)12ULL)) goto mn66;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)&t19) + 8) + 0));
        gm_l8 = t68;
        {
            void * t70 = gm_l0;
            gm_t88rcucg1b7uk t71 = gm_l1;
            gm_b16_8 t72 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t72;
        }
        {
            {
                gm_b16_8 t74 = gm_l8;
                gm_l10 = t74;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t76 = gm_l11;
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t78 = (t76 < t77);
                    if (!t78) goto lb75;
                    {
                        uint64_t t81 = gm_l11;
                        uint64_t t82 = gm_l11;
                        uint64_t t83 = (uintptr_t)t82;
                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t83 * 4));
                        gm_tlcm4lvu3g1a8 t86;
                        GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t81);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t86 + 8), t85);
                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&t86) + 0));
                        gm_l12 = t89;
                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t86) + 8));
                        gm_l13 = t90;
                        {
                            void * t92 = (void *)((uint8_t *)&F.l2);
                            gm_tml3m6miq9l2k t93;
                            void * t94 = gm_l0;
                            gm_t88rcucg1b7uk t95 = gm_l13;
                            gm_b16_8 t96 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t96);
                            void * t97 = gm_l0;
                            gm_t88rcucg1b7uk t98 = gm_l13;
                            uint64_t t99 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t97, t98);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t93 + 16, t99);
                            void * t100 = gm_l0;
                            gm_t88rcucg1b7uk t101 = gm_l13;
                            uint64_t t102 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t100, t101);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t93 + 24, t102);
                            gm_tf5srh4mp4b3i t103;
                            memset(&t103, 0, sizeof t103);
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t93 + 32, t103);
                            uint64_t t104 = gm_l12;
                            uint64_t t105 = (uintptr_t)t104;
                            uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srckfibkpt681aa, 232, 141);
                            uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t105 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t93 + 48, t107);
                            gm_tftb1odc739t5 t108 = gm_f_push_m55rf90(t92, &t93);
                            if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srckfibkpt681aa, 232, 45);
                            gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                        }
                        goto me87;
mn88:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 232, 13);
me87:;
                    }
lc75:;
                    {
                        uint64_t t111 = gm_l11;
                        uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                        uint64_t t114 = r112;
                        gm_l11 = t114;
                    }
                }
gm_ll79:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
                }
                goto gm_lad;
lb75:;
            }
        }
        goto me20;
mn66:;
ma23:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t116 != ((uint8_t)13ULL)) goto mn115;
        gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t19) + 8));
        gm_l14 = t117;
        {
            void * t119 = gm_l0;
            gm_t88rcucg1b7uk t120 = gm_l1;
            gm_b16_8 t121 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t121;
        }
        {
            void * t122 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t123;
            void * t124 = gm_l0;
            gm_t88rcucg1b7uk t125 = gm_l14;
            gm_b16_8 t126 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t126);
            void * t127 = gm_l0;
            gm_t88rcucg1b7uk t128 = gm_l14;
            uint64_t t129 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t123 + 16, t129);
            void * t130 = gm_l0;
            gm_t88rcucg1b7uk t131 = gm_l14;
            uint64_t t132 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t123 + 24, t132);
            gm_tf5srh4mp4b3i t133;
            memset(&t133, 0, sizeof t133);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t123 + 32, t133);
            uint64_t t134 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_srckfibkpt681aa, 236, 118);
            uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t134 * 8));
            GM_ST(uint64_t, (uint8_t *)&t123 + 48, t136);
            gm_tftb1odc739t5 t137 = gm_f_push_m55rf90(t122, &t123);
            if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srckfibkpt681aa, 236, 13);
            gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
        }
        {
            void * t139 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t140;
            gm_b16_8 t141;
            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_s9grtrvtmi962));
            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t140 + 0, t141);
            GM_ST(uint64_t, (uint8_t *)&t140 + 16, ((uint64_t)1ULL));
            GM_ST(uint64_t, (uint8_t *)&t140 + 24, ((uint64_t)1ULL));
            gm_tf5srh4mp4b3i t142;
            memset(&t142, 0, sizeof t142);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t140 + 32, t142);
            uint64_t t143 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t143 >= (uintptr_t)(t144)) gm_trap_index(t143, t144, gm_srckfibkpt681aa, 237, 87);
            uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t143 * 8));
            GM_ST(uint64_t, (uint8_t *)&t140 + 48, t145);
            gm_tftb1odc739t5 t146 = gm_f_push_m55rf90(t139, &t140);
            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srckfibkpt681aa, 237, 13);
            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
        }
        goto me20;
mn115:;
ma24:;
        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t149 != ((uint8_t)14ULL)) goto mn148;
        gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t19) + 8) + 0));
        gm_l16 = t150;
        gm_t88rcucg1b7uk t151 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t19) + 8) + 4));
        gm_l17 = t151;
        {
            void * t153 = gm_l0;
            gm_t88rcucg1b7uk t154 = gm_l1;
            gm_b16_8 t155 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t155;
        }
        {
            void * t156 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t157;
            gm_b16_8 t158;
            GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s9grtrvtmi962));
            GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t158);
            GM_ST(uint64_t, (uint8_t *)&t157 + 16, ((uint64_t)1ULL));
            GM_ST(uint64_t, (uint8_t *)&t157 + 24, ((uint64_t)1ULL));
            gm_tf5srh4mp4b3i t159;
            memset(&t159, 0, sizeof t159);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t157 + 32, t159);
            uint64_t t160 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t161 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
            if (t160 >= (uintptr_t)(t161)) gm_trap_index(t160, t161, gm_srckfibkpt681aa, 241, 87);
            uint64_t t162 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t160 * 8));
            GM_ST(uint64_t, (uint8_t *)&t157 + 48, t162);
            gm_tftb1odc739t5 t163 = gm_f_push_m55rf90(t156, &t157);
            if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_srckfibkpt681aa, 241, 13);
            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
        }
        {
            gm_t88rcucg1b7uk t165 = gm_l16;
            gm_t88rcucg1b7uk t166 = gm_l17;
            gm_tqsk8dt6ib3qq t167;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t167 + 0), t165);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t167 + 4), t166);
            gm_l19 = t167;
        }
        {
            void * t169 = gm_l0;
            void * t170 = ((uint8_t *)&gm_l19);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srckfibkpt681aa, 243, 38);
            gm_b16_8 t171;
            GM_ST(void *, (uint8_t *)&t171, (void *)(((uint8_t *)t170 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
            gm_t8ldkt09h6492 t173;
            gm_f_payload_union_feshns2((void *)&t173, (void *)NULL, cx, NULL, t169, t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t173))[32] != 0)) goto ie168;
            gm_l20 = (*(gm_tgbgti82mf879 *)(((uint8_t *)&t173) + 0));
            {
                void * t175 = (void *)((uint8_t *)&F.l2);
                gm_tml3m6miq9l2k t176;
                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t176 + 0, t177);
                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 16));
                GM_ST(uint64_t, (uint8_t *)&t176 + 16, t178);
                uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 24));
                GM_ST(uint64_t, (uint8_t *)&t176 + 24, t179);
                gm_tf5srh4mp4b3i t180;
                memset(&t180, 0, sizeof t180);
                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t176 + 32, t180);
                uint64_t t181 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t182 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                if (t181 >= (uintptr_t)(t182)) gm_trap_index(t181, t182, gm_srckfibkpt681aa, 243, 131);
                uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t181 * 8));
                GM_ST(uint64_t, (uint8_t *)&t176 + 48, t183);
                gm_tftb1odc739t5 t184 = gm_f_push_m55rf90(t175, &t176);
                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srckfibkpt681aa, 243, 50);
                gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
            }
            goto ix168;
ie168:;
ix168:;
        }
        goto me20;
mn148:;
ma25:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
        if (t187 != ((uint8_t)17ULL)) goto mn186;
        uint32_t t188 = GM_LD(uint32_t, ((((uint8_t *)&t19) + 8) + 0));
        gm_l21 = t188;
        uint32_t t189 = gm_l21;
        uint64_t t190 = (uint64_t)t189;
        uint64_t t191 = (uintptr_t)t190;
        void * t192 = gm_l0;
        void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
        uint64_t t194 = GM_LD(uintptr_t, ((((uint8_t *)t193) + 1088) + 0) + 8);
        if (t191 >= (uintptr_t)(t194)) gm_trap_index(t191, t194, gm_srckfibkpt681aa, 245, 30);
        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 0));
        switch (t200) { case ((uint8_t)0ULL): goto ma196; case ((uint8_t)1ULL): goto ma197; case ((uint8_t)2ULL): goto ma198; default: goto ma199; }
ma196:;
        uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 0));
        if (t202 != ((uint8_t)0ULL)) goto mn201;
        gm_tpautr4br7h3l t203 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 8));
        F.l22 = t203;
        {
            uint8_t t206 = 0;
            uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 16));
            if (!t207) {
                uint8_t t208;
                if ((((((uint8_t *)&F.l22) + 40))[4] != 0)) goto mn210;
                t208 = ((uint8_t)0);
                goto me209;
mn210:;
                t208 = ((uint8_t)1);
                goto me209;
mn211:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 247, 34);
me209:;
                t206 = t208;
            } else t206 = 1;
            if (!t206) goto ie205;
            {
                gm_th2lqbo77pvsc t213;
                memset(&t213, 0, sizeof t213);
                uint8_t t214 = 0;
                F.ret = t213;
                goto gm_fret;
            }
            goto ix205;
ie205:;
ix205:;
        }
        {
            void * t215 = gm_l0;
            gm_t88rcucg1b7uk t216 = gm_l1;
            gm_b16_8 t217 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t217;
        }
        {
            {
                gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                gm_l24 = t220;
            }
            {
                gm_l25 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t222 = gm_l25;
                    uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                    uint8_t t224 = (t222 < t223);
                    if (!t224) goto lb221;
                    {
                        gm_t8ta482bl6be0 t227;
                        uint64_t t228 = gm_l25;
                        GM_ST(uint64_t, (uint8_t *)&t227 + 0, t228);
                        uint64_t t229 = gm_l25;
                        uint64_t t230 = (uintptr_t)t229;
                        uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                        memcpy((uint8_t *)&t227 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t230 * 40), 40);
                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&t227) + 0));
                        gm_l26 = t234;
                        F.l27 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t227) + 8));
                        {
                            void * t236 = (void *)((uint8_t *)&F.l2);
                            gm_tml3m6miq9l2k t237;
                            void * t238 = gm_l0;
                            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            gm_b16_8 t240 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t238, t239);
                            if (gm_unw.kind) {
                                goto gm_ll225;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t237 + 0, t240);
                            void * t241 = gm_l0;
                            gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            uint64_t t243 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t241, t242);
                            if (gm_unw.kind) {
                                goto gm_ll225;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t237 + 16, t243);
                            void * t244 = gm_l0;
                            gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                            uint64_t t246 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t244, t245);
                            if (gm_unw.kind) {
                                goto gm_ll225;
                            }
                            GM_ST(uint64_t, (uint8_t *)&t237 + 24, t246);
                            gm_tf5srh4mp4b3i t247 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l27) + 24));
                            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t237 + 32, t247);
                            uint64_t t248 = gm_l26;
                            uint64_t t249 = (uintptr_t)t248;
                            uint64_t t250 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            if (t249 >= (uintptr_t)(t250)) gm_trap_index(t249, t250, gm_srckfibkpt681aa, 249, 158);
                            uint64_t t251 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t249 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t237 + 48, t251);
                            gm_tftb1odc739t5 t252 = gm_f_push_m55rf90(t236, &t237);
                            if (((((uint8_t *)&t252))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t252) + 1, gm_srckfibkpt681aa, 249, 53);
                            gm_b0_1 t253 = GM_LD(gm_b0_1, (((uint8_t *)&t252) + 1));
                        }
                        goto me232;
mn233:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 249, 17);
me232:;
                    }
lc221:;
                    {
                        uint64_t t255 = gm_l25;
                        uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                        uint64_t t258 = r256;
                        gm_l25 = t258;
                    }
                }
gm_ll225:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb221; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc221; }
                }
                goto gm_lad;
lb221:;
            }
        }
        goto me195;
mn201:;
ma197:;
        uint8_t t260 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 0));
        if (t260 != ((uint8_t)1ULL)) goto mn259;
        gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 8) + 0));
        gm_l28 = t261;
        gm_b16_8 t262 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 8) + 8));
        gm_l29 = t262;
        {
            void * t264 = gm_l0;
            gm_t88rcucg1b7uk t265 = gm_l1;
            gm_b16_8 t266 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t264, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t266;
        }
        {
            void * t267 = (void *)((uint8_t *)&F.l2);
            gm_tml3m6miq9l2k t268;
            void * t269 = gm_l0;
            gm_t88rcucg1b7uk t270 = gm_l28;
            gm_b16_8 t271 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t269, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t271);
            void * t272 = gm_l0;
            gm_t88rcucg1b7uk t273 = gm_l28;
            uint64_t t274 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t272, t273);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t268 + 16, t274);
            void * t275 = gm_l0;
            gm_t88rcucg1b7uk t276 = gm_l28;
            uint64_t t277 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t275, t276);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t268 + 24, t277);
            gm_tf5srh4mp4b3i t278;
            memset(&t278, 0, sizeof t278);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t268 + 32, t278);
            uint64_t t279 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            if (t279 >= (uintptr_t)(t280)) gm_trap_index(t279, t280, gm_srckfibkpt681aa, 253, 116);
            uint64_t t281 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t279 * 8));
            GM_ST(uint64_t, (uint8_t *)&t268 + 48, t281);
            gm_tftb1odc739t5 t282 = gm_f_push_m55rf90(t267, &t268);
            if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 1, gm_srckfibkpt681aa, 253, 17);
            gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t282) + 1));
        }
        {
            gm_f_list_nof7vfj((void *)&F.l31, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t285 = gm_l29;
                gm_l32 = t285;
            }
            {
                gm_l33 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t287 = gm_l33;
                    uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    uint8_t t289 = (t287 < t288);
                    if (!t289) goto lb286;
                    {
                        uint64_t t292 = gm_l33;
                        uint64_t t293 = (uintptr_t)t292;
                        uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                        gm_tckdobunaulee t297 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t293 * 72));
                        F.l34 = t297;
                        {
                            if (!(((((uint8_t *)&F.l34) + 16))[4] != 0)) goto ie299;
                            gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 16));
                            gm_l35 = t300;
                            {
                                void * t302 = (void *)((uint8_t *)&F.l31);
                                gm_t88rcucg1b7uk t303 = gm_l35;
                                gm_tftb1odc739t5 t304 = gm_f_push_k3l9nic(t302, GM_CONV(gm_csguankko4lmhd, t303));
                                if (((((uint8_t *)&t304))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t304) + 1, gm_srckfibkpt681aa, 256, 42);
                                gm_b0_1 t305 = GM_LD(gm_b0_1, (((uint8_t *)&t304) + 1));
                            }
                            goto ix299;
ie299:;
ix299:;
                        }
                        goto me295;
mn296:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 255, 17);
me295:;
                    }
lc286:;
                    {
                        uint64_t t307 = gm_l33;
                        uint64_t r308; bool o309 = __builtin_add_overflow(t307, ((uint64_t)1ULL), &r308);
                        uint64_t t310 = r308;
                        gm_l33 = t310;
                    }
                }
lb286:;
            }
        }
        {
            uint64_t t312 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
            uint8_t t313 = (t312 > ((uint64_t)1ULL));
            if (!t313) goto ie311;
            {
                void * t316 = gm_l0;
                gm_b16_8 t317 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)&t317));
                gm_t8ldkt09h6492 t319;
                gm_f_payload_union_feshns2((void *)&t319, (void *)NULL, cx, NULL, t316, t318);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t319))[32] != 0)) goto ie315;
                gm_l36 = (*(gm_tgbgti82mf879 *)(((uint8_t *)&t319) + 0));
                {
                    void * t321 = (void *)((uint8_t *)&F.l2);
                    gm_tml3m6miq9l2k t322;
                    gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t322 + 0, t323);
                    uint64_t t324 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 16));
                    GM_ST(uint64_t, (uint8_t *)&t322 + 16, t324);
                    uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                    GM_ST(uint64_t, (uint8_t *)&t322 + 24, t325);
                    gm_tf5srh4mp4b3i t326;
                    memset(&t326, 0, sizeof t326);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t322 + 32, t326);
                    uint64_t t327 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    if (t327 >= (uintptr_t)(t328)) gm_trap_index(t327, t328, gm_srckfibkpt681aa, 259, 145);
                    uint64_t t329 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t327 * 8));
                    GM_ST(uint64_t, (uint8_t *)&t322 + 48, t329);
                    gm_tftb1odc739t5 t330 = gm_f_push_m55rf90(t321, &t322);
                    if (((((uint8_t *)&t330))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t330) + 1, gm_srckfibkpt681aa, 259, 64);
                    gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t330) + 1));
                }
                goto ix315;
ie315:;
ix315:;
            }
            goto ix311;
ie311:;
ix311:;
        }
        goto me195;
mn259:;
ma198:;
        uint8_t t333 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t193) + 1088) + 0)) + t191 * 184) + 24) + 0));
        if (t333 != ((uint8_t)2ULL)) goto mn332;
        gm_th2lqbo77pvsc t334;
        memset(&t334, 0, sizeof t334);
        uint8_t t335 = 0;
        F.ret = t334;
        goto gm_fret;
        goto me195;
mn332:;
ma199:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 245, 24);
me195:;
        goto me20;
mn186:;
ma26:;
        gm_th2lqbo77pvsc t337;
        memset(&t337, 0, sizeof t337);
        uint8_t t338 = 0;
        F.ret = t337;
        goto gm_fret;
        goto me20;
mn336:;
ma27:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 221, 5);
me20:;
    }
    {
        gm_f_list_epvducg((void *)&F.l37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_cfvjofe((void *)&F.l38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l39 = ((uint64_t)0ULL);
    }
    {
        gm_l40 = ((uint64_t)1ULL);
    }
    {
        {
            gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&t340));
            gm_l41 = t341;
        }
        {
            gm_l42 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t343 = gm_l42;
                uint64_t t344 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t345 = (t343 < t344);
                if (!t345) goto lb342;
                {
                    gm_ttos9gukqrqer t348;
                    uint64_t t349 = gm_l42;
                    GM_ST(uint64_t, (uint8_t *)&t348 + 0, t349);
                    uint64_t t350 = gm_l42;
                    uint64_t t351 = (uintptr_t)t350;
                    uint64_t t352 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                    memcpy((uint8_t *)&t348 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l41)) + t351 * 56), 56);
                    uint64_t t355 = GM_LD(uint64_t, (((uint8_t *)&t348) + 0));
                    gm_l43 = t355;
                    F.l44 = (*(gm_tml3m6miq9l2k *)(((uint8_t *)&t348) + 8));
                    {
                        uint64_t t358 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                        uint8_t t359 = ((t358 == ((uint64_t)0ULL)));
                        if (!t359) goto ie357;
                        {
                            goto lc342;
                        }
                        goto ix357;
ie357:;
ix357:;
                    }
                    {
                        uint64_t t362 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        uint64_t t363 = gm_l39;
                        uint8_t t364 = (t362 > t363);
                        if (!t364) goto ie361;
                        {
                            void * t366 = (void *)((uint8_t *)&F.l37);
                            uint64_t t367 = gm_l43;
                            uint64_t t368 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                            uint64_t t369 = gm_l39;
                            uint64_t r370; bool o371 = __builtin_sub_overflow(t368, t369, &r370);
                            if (o371) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 272, 76);
                            uint64_t t372 = r370;
                            gm_t2lqu8jsj511d t373;
                            GM_ST(uint64_t, ((uint8_t *)&t373 + 0), t367);
                            GM_ST(uint64_t, ((uint8_t *)&t373 + 8), t372);
                            gm_tabb710d9mq2e t374;
                            gm_f_format_38m1q8c((void *)&t374, (void *)NULL, cx, NULL, t373);
                            if (gm_unw.kind) {
                                goto gm_ll346;
                            }
                            if (((((uint8_t *)&t374))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t374) + 8, gm_srckfibkpt681aa, 272, 42);
                            gm_b16_8 t375 = GM_LD(gm_b16_8, (((uint8_t *)&t374) + 8));
                            gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                            gm_tftb1odc739t5 t377 = gm_f_push_3ef746b(t366, t376);
                            if (((((uint8_t *)&t377))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t377) + 1, gm_srckfibkpt681aa, 272, 29);
                            gm_b0_1 t378 = GM_LD(gm_b0_1, (((uint8_t *)&t377) + 1));
                        }
                        goto ix361;
ie361:;
ix361:;
                    }
                    {
                        uint64_t t379 = gm_l40;
                        uint64_t t380;
                        if (!(((((uint8_t *)&F.l44) + 32))[8] != 0)) goto ie381;
                        uint64_t t382 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                        gm_l45 = t382;
                        uint64_t t384 = gm_l45;
                        uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t386 = (t384 > t385 ? t384 : t385);
                        uint64_t t387 = t386;
                        t380 = t387;
                        goto ix381;
ie381:;
                        uint64_t t389 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t390 = t389;
                        t380 = t390;
ix381:;
                        uint64_t t391 = (t379 > t380 ? t379 : t380);
                        gm_l40 = t391;
                    }
                    {
                        gm_b16_8 t392;
                        if (!(((((uint8_t *)&F.l44) + 32))[8] != 0)) goto ie393;
                        uint64_t t394 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                        gm_l46 = t394;
                        uint64_t t396 = gm_l46;
                        gm_t4ce0jd3tdro4 t397;
                        GM_ST(uint64_t, ((uint8_t *)&t397 + 0), t396);
                        gm_tabb710d9mq2e t398;
                        gm_f_format_irg2n6e((void *)&t398, (void *)NULL, cx, NULL, t397);
                        if (gm_unw.kind) {
                            goto gm_ll346;
                        }
                        if (((((uint8_t *)&t398))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t398) + 8, gm_srckfibkpt681aa, 274, 48);
                        gm_b16_8 t399 = GM_LD(gm_b16_8, (((uint8_t *)&t398) + 8));
                        gm_b16_8 t400 = t399;
                        gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                        t392 = t401;
                        goto ix393;
ie393:;
                        gm_b16_8 t403;
                        GM_ST(void *, (uint8_t *)&t403, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(0));
                        gm_b16_8 t404 = t403;
                        t392 = t404;
ix393:;
                        gm_l47 = t392;
                    }
                    {
                        void * t405 = (void *)((uint8_t *)&F.l37);
                        gm_tftksr4op114e t406;
                        gm_b16_8 t407 = gm_l47;
                        GM_ST(gm_b16_8, (uint8_t *)&t406 + 0, t407);
                        gm_b16_8 t408 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t406 + 16, t408);
                        uint64_t t409 = gm_l43;
                        GM_ST(uint64_t, (uint8_t *)&t406 + 32, t409);
                        gm_tabb710d9mq2e t410;
                        gm_f_format_1dfo4pq((void *)&t410, (void *)NULL, cx, NULL, &t406);
                        if (gm_unw.kind) {
                            goto gm_ll346;
                        }
                        if (((((uint8_t *)&t410))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t410) + 8, gm_srckfibkpt681aa, 275, 22);
                        gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)&t410) + 8));
                        gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                        gm_tftb1odc739t5 t413 = gm_f_push_3ef746b(t405, t412);
                        if (((((uint8_t *)&t413))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t413) + 1, gm_srckfibkpt681aa, 275, 9);
                        gm_b0_1 t414 = GM_LD(gm_b0_1, (((uint8_t *)&t413) + 1));
                    }
                    {
                        void * t415 = (void *)((uint8_t *)&F.l38);
                        gm_t3877pk7lv748 t416;
                        uint64_t t417 = gm_l43;
                        gm_t4ce0jd3tdro4 t418;
                        GM_ST(uint64_t, ((uint8_t *)&t418 + 0), t417);
                        gm_tabb710d9mq2e t419;
                        gm_f_format_tc7tn98((void *)&t419, (void *)NULL, cx, NULL, t418);
                        if (gm_unw.kind) {
                            goto gm_ll346;
                        }
                        if (((((uint8_t *)&t419))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t419) + 8, gm_srckfibkpt681aa, 276, 38);
                        gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)&t419) + 8));
                        gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&t420));
                        GM_ST(gm_b16_8, (uint8_t *)&t416 + 0, t421);
                        uint64_t t422 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        GM_ST(uint64_t, (uint8_t *)&t416 + 16, t422);
                        gm_tftb1odc739t5 t423 = gm_f_push_q9fsb22(t415, &t416);
                        if (((((uint8_t *)&t423))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t423) + 1, gm_srckfibkpt681aa, 276, 9);
                        gm_b0_1 t424 = GM_LD(gm_b0_1, (((uint8_t *)&t423) + 1));
                    }
                    {
                        uint64_t t425 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                        uint64_t t426 = (t425 > ((uint64_t)1ULL) ? t425 : ((uint64_t)1ULL));
                        gm_l48 = t426;
                    }
                    {
                        uint64_t t427 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                        uint64_t t428 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                        uint64_t t429 = gm_l48;
                        uint64_t r430; bool o431 = __builtin_add_overflow(t428, t429, &r430);
                        if (o431) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 27);
                        uint64_t t432 = r430;
                        uint64_t r433; bool o434 = __builtin_sub_overflow(t432, ((uint64_t)1ULL), &r433);
                        if (o434) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 279, 27);
                        uint64_t t435 = r433;
                        uint64_t t436 = gm_l48;
                        if (t436 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 279, 26);
                        uint64_t t437 = (uint64_t)(t435 / t436);
                        uint64_t t438 = gm_l48;
                        uint64_t r439; bool o440 = __builtin_mul_overflow(t437, t438, &r439);
                        if (o440) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 279, 26);
                        uint64_t t441 = r439;
                        uint64_t r442; bool o443 = __builtin_add_overflow(t427, t441, &r442);
                        if (o443) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 18);
                        uint64_t t444 = r442;
                        gm_l39 = t444;
                    }
                    goto me353;
mn354:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 270, 5);
me353:;
                }
lc342:;
                {
                    uint64_t t446 = gm_l42;
                    uint64_t r447; bool o448 = __builtin_add_overflow(t446, ((uint64_t)1ULL), &r447);
                    uint64_t t449 = r447;
                    gm_l42 = t449;
                }
            }
gm_ll346:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb342; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc342; }
            }
            goto gm_lad;
lb342:;
        }
    }
    {
        void * t451 = gm_l0;
        gm_t88rcucg1b7uk t452 = gm_l1;
        uint64_t t453 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t451, t452);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t454 = gm_l39;
        uint8_t t455 = (t453 > t454);
        if (!t455) goto ie450;
        {
            void * t457 = (void *)((uint8_t *)&F.l37);
            void * t458 = gm_l0;
            gm_t88rcucg1b7uk t459 = gm_l1;
            uint64_t t460 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t458, t459);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t461 = gm_l39;
            uint64_t r462; bool o463 = __builtin_sub_overflow(t460, t461, &r462);
            if (o463) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 281, 73);
            uint64_t t464 = r462;
            gm_t4ce0jd3tdro4 t465;
            GM_ST(uint64_t, ((uint8_t *)&t465 + 0), t464);
            gm_tabb710d9mq2e t466;
            gm_f_format_kl8i1sq((void *)&t466, (void *)NULL, cx, NULL, t465);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t466))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t466) + 8, gm_srckfibkpt681aa, 281, 43);
            gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&t466) + 8));
            gm_b16_8 t468 = GM_LD(gm_b16_8, ((uint8_t *)&t467));
            gm_tftb1odc739t5 t469 = gm_f_push_3ef746b(t457, t468);
            if (((((uint8_t *)&t469))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t469) + 1, gm_srckfibkpt681aa, 281, 30);
            gm_b0_1 t470 = GM_LD(gm_b0_1, (((uint8_t *)&t469) + 1));
        }
        goto ix450;
ie450:;
ix450:;
    }
    {
        void * t471 = gm_l0;
        gm_t88rcucg1b7uk t472 = gm_l1;
        gm_b16_8 t473 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
        gm_b16_8 t474 = GM_LD(gm_b16_8, ((uint8_t *)&t473));
        gm_b16_8 t475;
        GM_ST(void *, (uint8_t *)&t475, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t475 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t476;
        gm_f_join_sfknljb((void *)&t476, (void *)NULL, cx, NULL, t474, t475);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t476))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t476) + 8, gm_srckfibkpt681aa, 282, 31);
        gm_b16_8 t477 = GM_LD(gm_b16_8, (((uint8_t *)&t476) + 8));
        gm_b16_8 t478 = GM_LD(gm_b16_8, ((uint8_t *)&t477));
        gm_b16_8 t479 = GM_LD(gm_b16_8, (((uint8_t *)&F.l38) + 0));
        gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&t479));
        uint64_t t481 = gm_l40;
        gm_b16_8 t482 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t471, t472, t478, t480, t481);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t482);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_member_names_17el54k F;
    void * gm_l0; /* ty */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 121u);
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
            void * t4 = (void *)(((uint8_t *)t3) + 816);
            gm_l2 = t4;
        }
        gm_hval t5 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn6 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlak2jvvbqg4, .env = t5.env, .hctx = t5.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_keys_qc5bjlr_s31qjug((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
        F.w0 = t10;
        goto gm_ls11;
gm_ll8:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
        goto gm_lad;
gm_ls11:;
we7:;
        gm_b0_1 t12 = F.w0;
        gm_b0_1 t13 = t12;
    }
    {
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_f_sort_skeree7(t14);
    }
    {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
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
inline __attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_mirror_lcjegsf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 600);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_ti5ramhglnvdt t6;
        gm_f_get_9k2rdij((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[24] != 0)) goto ie2;
        gm_l2 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t6) + 0));
        {
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l1;
        gm_f_make_mirror_rudvl6p((void *)&gm_l3, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 600);
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t13 = gm_f_put_dm7u1sg(t11, t12, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srckfibkpt681aa, 341, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_payload_union_feshns2 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* tys */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 666u);
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
        gm_l3 = ((uint64_t)0ULL);
    }
    {
        gm_l4 = ((uint64_t)1ULL);
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t12 * 4));
                    gm_tlcm4lvu3g1a8 t15;
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 8), t14);
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&t15) + 0));
                    gm_l7 = t18;
                    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t15) + 8));
                    gm_l8 = t19;
                    {
                        void * t22 = gm_l0;
                        gm_t88rcucg1b7uk t23 = gm_l8;
                        uint64_t t24 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        uint8_t t25 = (t24 > ((uint64_t)0ULL));
                        if (!t25) goto ie21;
                        {
                            void * t27 = (void *)((uint8_t *)&F.l2);
                            gm_t3877pk7lv748 t28;
                            void * t29 = gm_l0;
                            gm_t88rcucg1b7uk t30 = gm_l8;
                            gm_b16_8 t31 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t29, t30);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t31);
                            uint64_t t32 = gm_l7;
                            GM_ST(uint64_t, (uint8_t *)&t28 + 16, t32);
                            gm_tabb710d9mq2e t33;
                            gm_f_format_3buamil((void *)&t33, (void *)NULL, cx, NULL, &t28);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_srckfibkpt681aa, 299, 26);
                            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
                            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                            gm_tftb1odc739t5 t36 = gm_f_push_3ef746b(t27, t35);
                            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srckfibkpt681aa, 299, 13);
                            gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                        }
                        {
                            uint64_t t38 = gm_l3;
                            void * t39 = gm_l0;
                            gm_t88rcucg1b7uk t40 = gm_l8;
                            uint64_t t41 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint64_t t42 = (t38 > t41 ? t38 : t41);
                            gm_l3 = t42;
                        }
                        {
                            uint64_t t43 = gm_l4;
                            void * t44 = gm_l0;
                            gm_t88rcucg1b7uk t45 = gm_l8;
                            uint64_t t46 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            uint64_t t47 = (t43 > t46 ? t43 : t46);
                            gm_l4 = t47;
                        }
                        goto ix21;
ie21:;
ix21:;
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 297, 5);
me16:;
                }
lc4:;
                {
                    uint64_t t49 = gm_l6;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l6 = t52;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        uint64_t t54 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
        uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
        if (!t55) goto ie53;
        {
            gm_t8ldkt09h6492 t57;
            memset(&t57, 0, sizeof t57);
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix53;
ie53:;
ix53:;
    }
    {
        gm_tgbgti82mf879 t59;
        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t63;
        gm_f_join_sfknljb((void *)&t63, (void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srckfibkpt681aa, 305, 59);
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
        gm_tnpifv20irnph t65;
        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
        gm_tabb710d9mq2e t66;
        gm_f_format_vgl5p2e((void *)&t66, (void *)NULL, cx, NULL, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srckfibkpt681aa, 305, 30);
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t68);
        uint64_t t69 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t59 + 16, t69);
        uint64_t t70 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t59 + 24, t70);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t8ldkt09h6492 *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t8ldkt09h6492 *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t8ldkt09h6492 *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_t8ldkt09h6492 *)gm_out))[32] = 1;
        GM_ST(gm_tgbgti82mf879, (uint8_t *)&(*(gm_t8ldkt09h6492 *)gm_out) + 0, t59);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_repr_53quqki F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    uint16_t gm_l4; /* bits */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    GM_STACK_CHECK(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_ghvav22((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t5v8n6qrn4rh4 t4;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    switch (t17) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma15; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma15; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma15; case ((uint8_t)18ULL): goto ma15; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma15; case ((uint8_t)21ULL): goto ma15; case ((uint8_t)22ULL): goto ma15; case ((uint8_t)23ULL): goto ma15; case ((uint8_t)24ULL): goto ma15; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma15; default: goto ma15; }
ma6:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t19 != ((uint8_t)0ULL)) goto mn18;
    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)&t3) + 8) + 0));
    gm_l2 = t20;
    uint16_t t21 = GM_LD(uint16_t, ((((uint8_t *)&t3) + 8) + 2));
    gm_l3 = t21;
    uint8_t t22 = gm_l2;
    uint16_t t23 = gm_l3;
    uint32_t t24 = (uint32_t)t23;
    void * t25 = gm_l0;
    gm_t88rcucg1b7uk t26 = gm_l1;
    uint64_t t27 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tuglhjjmj2g58 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), t22);
    GM_ST(uint32_t, ((uint8_t *)&t28 + 4), t24);
    GM_ST(uint64_t, ((uint8_t *)&t28 + 8), t27);
    gm_t5v8n6qrn4rh4 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t29 + 8), t28);
    t4 = t29;
    goto me5;
mn18:;
ma7:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t31 != ((uint8_t)1ULL)) goto mn30;
    void * t32 = gm_l0;
    void * t33 = (void *)((((uint8_t *)t32) + 8) + 0);
    uint32_t t34 = gm_f_ptr_bits_glps5i7(t33);
    void * t35 = gm_l0;
    uint64_t t36 = GM_LD(uint64_t, (((((uint8_t *)t35) + 8) + 0) + 0));
    gm_tuglhjjmj2g58 t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t37 + 4), t34);
    GM_ST(uint64_t, ((uint8_t *)&t37 + 8), t36);
    gm_t5v8n6qrn4rh4 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t38 + 8), t37);
    t4 = t38;
    goto me5;
mn30:;
ma8:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t40 != ((uint8_t)2ULL)) goto mn39;
    void * t41 = gm_l0;
    void * t42 = (void *)((((uint8_t *)t41) + 8) + 0);
    uint32_t t43 = gm_f_ptr_bits_glps5i7(t42);
    void * t44 = gm_l0;
    uint64_t t45 = GM_LD(uint64_t, (((((uint8_t *)t44) + 8) + 0) + 0));
    gm_tuglhjjmj2g58 t46;
    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t43);
    GM_ST(uint64_t, ((uint8_t *)&t46 + 8), t45);
    gm_t5v8n6qrn4rh4 t47;
    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t47 + 8), t46);
    t4 = t47;
    goto me5;
mn39:;
ma9:;
    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t49 != ((uint8_t)3ULL)) goto mn48;
    uint16_t t50 = GM_LD(uint16_t, (((uint8_t *)&t3) + 8));
    gm_l4 = t50;
    uint16_t t51 = gm_l4;
    gm_t5v8n6qrn4rh4 t52;
    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
    GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)1ULL));
    GM_ST(uint16_t, ((uint8_t *)&t52 + 8), t51);
    t4 = t52;
    goto me5;
mn48:;
ma10:;
    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t54 != ((uint8_t)4ULL)) goto mn53;
    gm_t5v8n6qrn4rh4 t55;
    GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t55 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t55 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)2ULL));
    t4 = t55;
    goto me5;
mn53:;
ma11:;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t59 != ((uint8_t)8ULL)) goto pa58;
    goto po57;
pa58:;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t61 != ((uint8_t)9ULL)) goto pa60;
    goto po57;
pa60:;
    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t63 != ((uint8_t)7ULL)) goto pa62;
    goto po57;
pa62:;
    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t65 != ((uint8_t)16ULL)) goto pa64;
    goto po57;
pa64:;
    goto mn56;
po57:;
    gm_t5v8n6qrn4rh4 t66;
    GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t66 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)3ULL));
    t4 = t66;
    goto me5;
mn56:;
ma12:;
    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t68 != ((uint8_t)13ULL)) goto mn67;
    gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t3) + 8));
    gm_l5 = t69;
    gm_t5v8n6qrn4rh4 t70;
    void * t72 = gm_l0;
    void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
    gm_t88rcucg1b7uk t74 = gm_l5;
    uint8_t t75 = gm_f_nullable_pointer_7qfmn6o(t73, GM_CONV(gm_csguankko4lmhd, t74));
    if (!t75) goto ie71;
    gm_t5v8n6qrn4rh4 t77;
    GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t77 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t77 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)3ULL));
    gm_t5v8n6qrn4rh4 t78 = t77;
    t70 = t78;
    goto ix71;
ie71:;
    gm_t5v8n6qrn4rh4 t80;
    GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t80 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t80 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)6ULL));
    gm_t5v8n6qrn4rh4 t81 = t80;
    t70 = t81;
ix71:;
    t4 = t70;
    goto me5;
mn67:;
ma13:;
    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t83 != ((uint8_t)15ULL)) goto mn82;
    gm_t5v8n6qrn4rh4 t84;
    GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t84 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t84 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)4ULL));
    t4 = t84;
    goto me5;
mn82:;
ma14:;
    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t86 != ((uint8_t)25ULL)) goto mn85;
    gm_t5v8n6qrn4rh4 t87;
    GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t87 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t87 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)5ULL));
    t4 = t87;
    goto me5;
mn85:;
ma15:;
    gm_t5v8n6qrn4rh4 t89;
    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t89 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t89 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)6ULL));
    t4 = t89;
    goto me5;
mn88:;
ma16:;
    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 143, 55);
me5:;
    F.ret = t4;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_typed_stkcgvr F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 408);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_ti5ramhglnvdt t6;
        gm_f_get_9k2rdij((void *)&t6, t4, t5);
        if (!((((uint8_t *)&t6))[24] != 0)) goto ie2;
        gm_l2 = (*(gm_th2lqbo77pvsc *)(((uint8_t *)&t6) + 0));
        {
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l1;
        gm_f_make_typed_2bh0dpj((void *)&gm_l3, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 408);
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
        gm_tftb1odc739t5 t13 = gm_f_put_dm7u1sg(t11, t12, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srckfibkpt681aa, 200, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_vector_typedef_vpgf6u8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* lane */
    gm_b16_8 gm_l2; /* tag */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* name */
    GM_STACK_CHECK(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_tf1bjg81u0glv t2;
        uint64_t t3 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t2 + 0, t3);
        gm_b16_8 t4 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
        gm_tabb710d9mq2e t5;
        gm_f_format_dtmr1c2((void *)&t5, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srckfibkpt681aa, 328, 13);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l5 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 752);
        gm_b16_8 t10 = gm_l5;
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        gm_tqs5p4n2tq19v t12 = gm_f_get_p217beu(t9, t11);
        uint8_t t13;
        if (((((uint8_t *)&t12))[1] != 0)) goto mn15;
        t13 = ((uint8_t)1);
        goto me14;
mn15:;
        t13 = ((uint8_t)0);
        goto me14;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 329, 8);
me14:;
        if (!t13) goto ie7;
        {
            void * t18 = gm_l0;
            void * t19 = (void *)(((uint8_t *)t18) + 752);
            gm_b16_8 t20 = gm_l5;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_tftb1odc739t5 t22 = gm_f_put_5c6qgui(t19, t21, ((uint8_t)1));
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srckfibkpt681aa, 330, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        {
            void * t24 = gm_l0;
            void * t25 = (void *)(((uint8_t *)t24) + 560);
            gm_tftksr4op114e t26;
            gm_b16_8 t27 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
            gm_b16_8 t28 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
            uint64_t t29 = gm_l3;
            uint64_t t30 = gm_l4;
            uint64_t r31; bool o32 = __builtin_mul_overflow(t29, t30, &r31);
            if (o32) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 331, 94);
            uint64_t t33 = r31;
            GM_ST(uint64_t, (uint8_t *)&t26 + 32, t33);
            gm_tftb1odc739t5 t34 = gm_f_write_1h8ac6i(t25, &t26);
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srckfibkpt681aa, 331, 9);
            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t36 = gm_l5;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        uint8_t t38 = 0;
        F.ret = t37;
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
