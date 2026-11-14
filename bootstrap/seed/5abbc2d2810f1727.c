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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; gm_ts6oqppjh0ba3 f2; gm_ts6oqppjh0ba3 f3; gm_ts6oqppjh0ba3 f4; gm_ts6oqppjh0ba3 f5; } gm_tvqq0vnt1mu0q;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tvqq0vnt1mu0q) == 12 && _Alignof(gm_tvqq0vnt1mu0q) == 1 && offsetof(gm_tvqq0vnt1mu0q, f0) == 0 && offsetof(gm_tvqq0vnt1mu0q, f1) == 2 && offsetof(gm_tvqq0vnt1mu0q, f2) == 4 && offsetof(gm_tvqq0vnt1mu0q, f3) == 6 && offsetof(gm_tvqq0vnt1mu0q, f4) == 8 && offsetof(gm_tvqq0vnt1mu0q, f5) == 10, "layout of `(BinOp, BinOp, BinOp, BinOp, BinOp, BinOp)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { gm_tu15p0hi2qlv6 f0; uint8_t f1; uint8_t pe[7]; } gm_tk3f7nn6jq07c;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_ts78ca2amthgu v1; } f1; } gm_t1hofbajv6dns;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; gm_b16_8 v1; } f1; } gm_t4t568o7qnv02;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tk3f7nn6jq07c) == 200 && _Alignof(gm_tk3f7nn6jq07c) == 8 && offsetof(gm_tk3f7nn6jq07c, f0) == 0 && offsetof(gm_tk3f7nn6jq07c, f1) == 192, "layout of `?(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t1hofbajv6dns) == 208 && _Alignof(gm_t1hofbajv6dns) == 8 && offsetof(gm_t1hofbajv6dns, f0) == 0 && offsetof(gm_t1hofbajv6dns, f1) == 8, "layout of `LocalConstKind`");
_Static_assert(sizeof(gm_t4t568o7qnv02) == 208 && _Alignof(gm_t4t568o7qnv02) == 8 && offsetof(gm_t4t568o7qnv02, f0) == 0 && offsetof(gm_t4t568o7qnv02, f1) == 8, "layout of `UntypedOr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { gm_b16_8 f0; gm_t1hofbajv6dns f1; } gm_tb07pp6njg0bk;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tb07pp6njg0bk) == 224 && _Alignof(gm_tb07pp6njg0bk) == 8 && offsetof(gm_tb07pp6njg0bk, f0) == 0 && offsetof(gm_tb07pp6njg0bk, f1) == 16, "layout of `LocalConst`");
typedef struct { gm_tb07pp6njg0bk f0; uint8_t f1; uint8_t pe[7]; } gm_tk1gen3hgkm51;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tk1gen3hgkm51) == 232 && _Alignof(gm_tk1gen3hgkm51) == 8 && offsetof(gm_tk1gen3hgkm51, f0) == 0 && offsetof(gm_tk1gen3hgkm51, f1) == 224, "layout of `?LocalConst`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_837b4po {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_as_complex_7e8vejf {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* re */
    gm_tngtuiuskn938 l2; /* im */
    gm_tngtuiuskn938 l3; /* r */
    gm_tk3f7nn6jq07c ret;
};
struct gm_fr_as_rational_dj3nve5 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_binary_9sb7899 {
    gm_frame h;
    gm_t4t568o7qnv02 l3; /* r */
    gm_t0ialelq3ql1k l4; /* v */
    gm_tbci5ospk5nnn l5; /* i */
    gm_tngtuiuskn938 l6; /* q */
    gm_tngtuiuskn938 l7; /* re */
    gm_tngtuiuskn938 l8; /* im */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_binary_exact_8smjl0k {
    gm_frame h;
    gm_tngtuiuskn938 l5; /* x */
    gm_tngtuiuskn938 l6; /* y */
    gm_tbci5ospk5nnn l8; /* x */
    gm_tbci5ospk5nnn l9; /* y */
    gm_tngtuiuskn938 l10; /* x */
    gm_tngtuiuskn938 l11; /* y */
    gm_tngtuiuskn938 l12; /* r */
    gm_tbci5ospk5nnn l13; /* x */
    gm_tbci5ospk5nnn l14; /* y */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_bit_and_3tgam8m {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_or_j1tdrhl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_xor_tmvdnad {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_complex_binary_fggi5t8 {
    gm_frame h;
    gm_tu15p0hi2qlv6 l3; /* x */
    gm_tu15p0hi2qlv6 l4; /* y */
    gm_tngtuiuskn938 l5; /* p */
    gm_tngtuiuskn938 l6; /* q */
    gm_tngtuiuskn938 l7; /* r */
    gm_tngtuiuskn938 l8; /* t */
    gm_tngtuiuskn938 l9; /* d */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_describe_bm129vh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_dump_untyped_o8mt4q8 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* q */
    gm_tngtuiuskn938 l4; /* re */
    gm_tngtuiuskn938 l5; /* im */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_eq_ceufbo4 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_result_csnia2t {
    gm_frame h;
    gm_tngtuiuskn938 l3; /* r */
    gm_t0ialelq3ql1k ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_neg_q1tmt2n {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_rb46l9j {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_zero_f3o1s37 {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* r */
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_od1e7nd {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_negate_fohael7 {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    gm_tngtuiuskn938 l2; /* r */
    gm_tngtuiuskn938 l3; /* re */
    gm_tngtuiuskn938 l4; /* im */
    gm_t4t568o7qnv02 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_rational_bits_u60jbai {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* q */
    uint64_t ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_int_ub6npub {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_b16_8 ret;
};
struct gm_fr_show_rational_00mlc2h {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* q */
    gm_b16_8 ret;
};
struct gm_fr_sub_mler6q8 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_try_untyped_fj0h8i5 {
    gm_frame h;
    gm_tngtuiuskn938 l5; /* v */
    gm_tb07pp6njg0bk l8; /* c */
    gm_t0ialelq3ql1k l9; /* v */
    gm_t0ialelq3ql1k l11; /* v */
    gm_t0ialelq3ql1k l16; /* v */
    gm_t5vbkdq81qfs7 l18; /* r */
    gm_t0ialelq3ql1k l19; /* v */
    gm_t0ialelq3ql1k l20; /* x */
    gm_t5vbkdq81qfs7 l29; /* a */
    gm_t5vbkdq81qfs7 l30; /* b */
    gm_t0ialelq3ql1k l31; /* x */
    gm_t0ialelq3ql1k l32; /* y */
    gm_t0ialelq3ql1k l33; /* v */
    gm_tkv1vap90vht2 ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_zero_9r6o7sj {
    gm_frame h;
    gm_tngtuiuskn938 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_as_complex_7e8vejf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_as_rational_dj3nve5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binary_exact_8smjl0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_complex_binary_fggi5t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0);
__attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_untyped_o8mt4q8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_float_result_csnia2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_9453blg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_djdpqv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mmf09ti(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_o65p921(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_obkf19v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_thddtj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_neg_q1tmt2n(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_local_const_idpokjh(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai_c(gm_tngtuiuskn938 a0);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_rational_00mlc2h(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_cii3u4ppm6k[16] __attribute__((aligned(8)));
extern const uint8_t gm_s27qdg2qrqsr9[25];
extern const uint8_t gm_s73ofu7ms28r6[23];
extern const uint8_t gm_s8qs8rpj34te9[28];
extern const uint8_t gm_s9c60h0qendav[9];
extern const uint8_t gm_sc003f3fvnij4[17];
extern const uint8_t gm_scaduriptfv6s[34];
extern const uint8_t gm_sek3nvre3su54[17];
extern const uint8_t gm_snod68sv8lqq6[45];
extern const uint8_t gm_splm7v4oko2tv[15];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stpkldc5dmf71[33];
extern const uint8_t gm_sv56kn0notafb[17];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)1) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)2) return gm_eqehhdtpeti1(a + 1, b + 1);
    if (ta == (uint8_t)3) return gm_eqehhdtpeti1(a + 1, b + 1);
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) void gm_f_add_837b4po(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_837b4po F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_add_420lj6o((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_and_3tgam8m F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_or_j1tdrhl F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)1ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_xor_tmvdnad F;
    GM_STACK_CHECK(&F, 1u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t1;
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)2ULL));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_cmp_9aseneh F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t3 = gm_f_cmp_i1knrgu(&t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 240) + 0) + 8);
        if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_src6lm8esmv16p1, 176, 14);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 240) + 0)) + t3 * 216);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
        void * t11 = gm_l3;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
        uint64_t t13 = gm_l2;
        uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        int32_t t11 = gm_f_mag_cmp_s39veq6(t7, t10);
        uint8_t t12 = ((t11 == ((int32_t)0LL)));
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_ceufbo4(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_ceufbo4 F;
    GM_STACK_CHECK_SHALLOW(&F, 5u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (t2) {
        uint8_t t3 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
        t1 = t3;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t2 = ((t1 == ((uint64_t)1ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_rb46l9j F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t2 = ((t1 == ((uint64_t)0ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_is_zero_f3o1s37 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tvegkb6ouf3m0 t3;
        gm_f_as_rational_dj3nve5((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t4 = t3;
        gm_tngtuiuskn938 t5;
        if (((((uint8_t *)&t4))[96] != 0)) {
            gm_tngtuiuskn938 t6 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        F.l1 = t5;
    }
    {
        uint8_t t8 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    uint8_t t3 = (!(t1 == t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_trqfvmjtt4sij t10;
    gm_f_mag_mul_if5s38k((void *)&t10, (void *)NULL, cx, NULL, t6, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t3, &t10);
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
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_od1e7nd F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1;
    gm_f_neg_agd5c5k((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rational_bits_u60jbai F;
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint64_t t3 = (t1 > t2 ? t1 : t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_rem_ib4l1vl F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_mler6q8 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_sub_pktm5cr((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 113u);
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
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        uint8_t t8 = 0;
        F.ret = t7;
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma3; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t9 != ((uint8_t)2ULL)) goto mn8;
    t1 = ((uint8_t)1);
    goto me2;
mn8:;
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t11 != ((uint8_t)1ULL)) goto mn10;
    void * t12 = gm_l0;
    uint64_t t13 = gm_l2;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_same_package_vmps24r(t12, t13, t14);
    t1 = t15;
    goto me2;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
    if (t17 != ((uint8_t)0ULL)) goto mn16;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l3;
    uint8_t t20 = ((t18 == t19));
    t1 = t20;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 168, 103);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_zero_9r6o7sj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_9r6o7sj F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_zero_s25l990((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_from_int_ofjuoqh((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1);
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
inline __attribute__((aligned(4))) void gm_f_as_complex_7e8vejf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_as_complex_7e8vejf F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 1604u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
        if (t4 != ((uint8_t)4ULL)) goto ie2;
        gm_tngtuiuskn938 t5 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t3) + 8) + 0));
        F.l1 = t5;
        gm_tngtuiuskn938 t6 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t3) + 8) + 96));
        F.l2 = t6;
        {
            gm_tu15p0hi2qlv6 t8;
            memcpy((uint8_t *)&t8 + 0, ((uint8_t *)&F.l1), 96);
            memcpy((uint8_t *)&t8 + 96, ((uint8_t *)&F.l2), 96);
            gm_tk3f7nn6jq07c t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 193, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 194, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 196, 0);
            GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[192] = 1;
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
        gm_tvegkb6ouf3m0 t12;
        gm_f_as_rational_dj3nve5((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t13 = t12;
        gm_tngtuiuskn938 t14;
        if (((((uint8_t *)&t13))[96] != 0)) {
            gm_tngtuiuskn938 t15 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t13) + 0));
            t14 = t15;
        } else {
            gm_tk3f7nn6jq07c t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        F.l3 = t14;
    }
    {
        gm_tu15p0hi2qlv6 t18;
        memcpy((uint8_t *)&t18 + 0, ((uint8_t *)&F.l3), 96);
        gm_f_zero_9r6o7sj((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t18 + 96)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tk3f7nn6jq07c t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 193, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 194, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 196, 0);
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t19), t18);
        ((uint8_t *)&t19)[192] = 1;
        uint8_t t20 = 0;
        F.ret = t19;
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
inline __attribute__((aligned(4))) void gm_f_as_rational_dj3nve5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_as_rational_dj3nve5 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tvegkb6ouf3m0 t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_tbci5ospk5nnn t12 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t12;
    gm_tngtuiuskn938 t13;
    gm_f_from_int_ofjuoqh((void *)&t13, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvegkb6ouf3m0 t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t14 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[96] = 1;
    t2 = t14;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)1ULL)) goto mn15;
    gm_tngtuiuskn938 t17 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t17;
    gm_tngtuiuskn938 t18 = F.l2;
    gm_tvegkb6ouf3m0 t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[96] = 1;
    t2 = t19;
    goto me3;
mn15:;
ma6:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t21 != ((uint8_t)2ULL)) goto mn20;
    gm_tngtuiuskn938 t22;
    gm_f_zero_9r6o7sj((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvegkb6ouf3m0 t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 97, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 98, 0);
    GM_ST(uint32_t, (uint8_t *)&t23 + 100, 0);
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[96] = 1;
    t2 = t23;
    goto me3;
mn20:;
ma7:;
    gm_tvegkb6ouf3m0 t25;
    memset(&t25, 0, sizeof t25);
    t2 = t25;
    goto me3;
mn24:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 37, 61);
me3:;
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_binary_9sb7899(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_binary_9sb7899 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint64_t gm_l9; /* bits */
    GM_STACK_CHECK(&F, 951u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_ts6oqppjh0ba3 t2 = gm_l0;
        void * t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_binary_exact_8smjl0k((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t6 != ((uint8_t)0ULL)) goto ie5;
        gm_t0ialelq3ql1k t7 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l3) + 8));
        F.l4 = t7;
        {
            uint64_t t9;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            switch (t16) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma13; default: goto ma14; }
ma11:;
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t18 != ((uint8_t)0ULL)) goto mn17;
            gm_tbci5ospk5nnn t19 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l4) + 8));
            F.l5 = t19;
            uint64_t t20 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
            t9 = t20;
            goto me10;
mn17:;
ma12:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t22 != ((uint8_t)1ULL)) goto mn21;
            gm_tngtuiuskn938 t23 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 8));
            F.l6 = t23;
            uint64_t t24 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
            t9 = t24;
            goto me10;
mn21:;
ma13:;
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t26 != ((uint8_t)4ULL)) goto mn25;
            gm_tngtuiuskn938 t27 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)&F.l4) + 8) + 0));
            F.l7 = t27;
            gm_tngtuiuskn938 t28 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)&F.l4) + 8) + 96));
            F.l8 = t28;
            uint64_t t29 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            uint64_t t30 = gm_f_rational_bits_u60jbai(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            uint64_t t31 = (t29 > t30 ? t29 : t30);
            t9 = t31;
            goto me10;
mn25:;
ma14:;
            t9 = ((uint64_t)0ULL);
            goto me10;
mn32:;
ma15:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 75, 17);
me10:;
            gm_l9 = t9;
        }
        {
            uint64_t t34 = gm_l9;
            uint8_t t35 = (t34 > ((uint64_t)4194304ULL));
            if (!t35) goto ie33;
            {
                gm_t19enj90vg8bd t37;
                GM_ST(int32_t, ((uint8_t *)&t37 + 0), ((int32_t)4194304LL));
                gm_tabb710d9mq2e t38;
                gm_f_format_mmf09ti((void *)&t38, (void *)NULL, cx, NULL, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t39 = t38;
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_src7h8dni243k2v, 82, 21);
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
                gm_b16_8 t41 = t40;
                gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
                gm_t4t568o7qnv02 t43;
                memset(&t43, 0, sizeof t43);
                GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t43 + 8), t42);
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            memmove(gm_out, ((uint8_t *)&F.l3), 208);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
        {
            memmove(gm_out, ((uint8_t *)&F.l3), 208);
            goto gm_fdone;
        }
ix5:;
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
__attribute__((aligned(4))) void gm_f_binary_exact_8smjl0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_binary_exact_8smjl0k F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint8_t gm_l3; /* x */
    uint8_t gm_l4; /* y */
    int32_t gm_l7; /* c */
    uint64_t gm_l15; /* n */
    GM_STACK_CHECK(&F, 9727u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l1;
        uint8_t t5;
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t8 != ((uint8_t)4ULL)) goto mn7;
        t5 = ((uint8_t)1);
        goto me6;
mn7:;
        t5 = ((uint8_t)0);
        goto me6;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 92, 8);
me6:;
        if (!t5) {
            void * t10 = gm_l2;
            uint8_t t11;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t10) + 0));
            if (t14 != ((uint8_t)4ULL)) goto mn13;
            t11 = ((uint8_t)1);
            goto me12;
mn13:;
            t11 = ((uint8_t)0);
            goto me12;
mn15:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 92, 29);
me12:;
            t3 = t11;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_ts6oqppjh0ba3 t17 = gm_l0;
            void * t18 = gm_l1;
            void * t19 = gm_l2;
            gm_f_complex_binary_fggi5t8((void *)&(*(gm_t4t568o7qnv02 *)gm_out), (void *)NULL, cx, NULL, t17, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma23; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma21; case ((uint8_t)13ULL): goto ma21; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma21; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma21; case ((uint8_t)19ULL): goto ma21; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; default: goto ma24; }
ma21:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t30 != ((uint8_t)16ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t32 != ((uint8_t)17ULL)) goto pa31;
        goto po28;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t34 != ((uint8_t)18ULL)) goto pa33;
        goto po28;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t36 != ((uint8_t)19ULL)) goto pa35;
        goto po28;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t38 != ((uint8_t)12ULL)) goto pa37;
        goto po28;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t40 != ((uint8_t)13ULL)) goto pa39;
        goto po28;
pa39:;
        goto mn27;
po28:;
        {
            void * t43 = gm_l1;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t43) + 0));
            if (t44 != ((uint8_t)3ULL)) goto ie42;
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t43) + 8));
            gm_l3 = t45;
            void * t46 = gm_l2;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t46) + 0));
            if (t47 != ((uint8_t)3ULL)) goto ie42;
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + 8));
            gm_l4 = t48;
            {
                gm_t4t568o7qnv02 t50;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                switch (t56) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma54; case ((uint8_t)7ULL): goto ma54; case ((uint8_t)8ULL): goto ma54; case ((uint8_t)9ULL): goto ma54; case ((uint8_t)10ULL): goto ma54; case ((uint8_t)11ULL): goto ma54; case ((uint8_t)12ULL): goto ma52; case ((uint8_t)13ULL): goto ma53; case ((uint8_t)14ULL): goto ma54; case ((uint8_t)15ULL): goto ma54; case ((uint8_t)16ULL): goto ma54; case ((uint8_t)17ULL): goto ma54; case ((uint8_t)18ULL): goto ma54; case ((uint8_t)19ULL): goto ma54; case ((uint8_t)20ULL): goto ma54; case ((uint8_t)21ULL): goto ma54; default: goto ma54; }
ma52:;
                uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t58 != ((uint8_t)12ULL)) goto mn57;
                uint8_t t59 = gm_l3;
                uint8_t t60 = gm_l4;
                uint8_t t61 = ((t59 == t60));
                gm_t0ialelq3ql1k t62;
                memset(&t62, 0, sizeof t62);
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t62 + 8), t61);
                gm_t4t568o7qnv02 t63;
                GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t63 + 8), t62);
                t50 = t63;
                goto me51;
mn57:;
ma53:;
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t65 != ((uint8_t)13ULL)) goto mn64;
                uint8_t t66 = gm_l3;
                uint8_t t67 = gm_l4;
                uint8_t t68 = (!(t66 == t67));
                gm_t0ialelq3ql1k t69;
                memset(&t69, 0, sizeof t69);
                GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t69 + 8), t68);
                gm_t4t568o7qnv02 t70;
                GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t70 + 8), t69);
                t50 = t70;
                goto me51;
mn64:;
ma54:;
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s27qdg2qrqsr9));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(24));
                gm_t4t568o7qnv02 t73;
                memset(&t73, 0, sizeof t73);
                GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t73 + 8), t72);
                t50 = t73;
                goto me51;
mn71:;
ma55:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 96, 24);
me51:;
                uint8_t t74 = 0;
                F.ret = t50;
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            void * t75 = gm_l1;
            gm_tvegkb6ouf3m0 t76;
            gm_f_as_rational_dj3nve5((void *)&t76, (void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t77 = t76;
            gm_tngtuiuskn938 t78;
            if (((((uint8_t *)&t77))[96] != 0)) {
                gm_tngtuiuskn938 t79 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t77) + 0));
                t78 = t79;
            } else {
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(27));
                gm_t4t568o7qnv02 t81;
                memset(&t81, 0, sizeof t81);
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
                uint8_t t82 = 0;
                F.ret = t81;
                goto gm_fret;
            }
            F.l5 = t78;
        }
        {
            void * t83 = gm_l2;
            gm_tvegkb6ouf3m0 t84;
            gm_f_as_rational_dj3nve5((void *)&t84, (void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t85 = t84;
            gm_tngtuiuskn938 t86;
            if (((((uint8_t *)&t85))[96] != 0)) {
                gm_tngtuiuskn938 t87 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t85) + 0));
                t86 = t87;
            } else {
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(27));
                gm_t4t568o7qnv02 t89;
                memset(&t89, 0, sizeof t89);
                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t89 + 8), t88);
                uint8_t t90 = 0;
                F.ret = t89;
                goto gm_fret;
            }
            F.l6 = t86;
        }
        {
            int32_t t91 = gm_f_cmp_9aseneh((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t91;
        }
        {
            uint8_t t92;
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t101) { case ((uint8_t)0ULL): goto ma99; case ((uint8_t)1ULL): goto ma99; case ((uint8_t)2ULL): goto ma99; case ((uint8_t)3ULL): goto ma99; case ((uint8_t)4ULL): goto ma99; case ((uint8_t)5ULL): goto ma99; case ((uint8_t)6ULL): goto ma99; case ((uint8_t)7ULL): goto ma99; case ((uint8_t)8ULL): goto ma99; case ((uint8_t)9ULL): goto ma99; case ((uint8_t)10ULL): goto ma99; case ((uint8_t)11ULL): goto ma99; case ((uint8_t)12ULL): goto ma98; case ((uint8_t)13ULL): goto ma99; case ((uint8_t)14ULL): goto ma99; case ((uint8_t)15ULL): goto ma99; case ((uint8_t)16ULL): goto ma94; case ((uint8_t)17ULL): goto ma95; case ((uint8_t)18ULL): goto ma96; case ((uint8_t)19ULL): goto ma97; case ((uint8_t)20ULL): goto ma99; case ((uint8_t)21ULL): goto ma99; default: goto ma99; }
ma94:;
            uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t103 != ((uint8_t)16ULL)) goto mn102;
            int32_t t104 = gm_l7;
            uint8_t t105 = (t104 < ((int32_t)0LL));
            t92 = t105;
            goto me93;
mn102:;
ma95:;
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t107 != ((uint8_t)17ULL)) goto mn106;
            int32_t t108 = gm_l7;
            uint8_t t109 = (t108 <= ((int32_t)0LL));
            t92 = t109;
            goto me93;
mn106:;
ma96:;
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t111 != ((uint8_t)18ULL)) goto mn110;
            int32_t t112 = gm_l7;
            uint8_t t113 = (t112 > ((int32_t)0LL));
            t92 = t113;
            goto me93;
mn110:;
ma97:;
            uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t115 != ((uint8_t)19ULL)) goto mn114;
            int32_t t116 = gm_l7;
            uint8_t t117 = (t116 >= ((int32_t)0LL));
            t92 = t117;
            goto me93;
mn114:;
ma98:;
            uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t119 != ((uint8_t)12ULL)) goto mn118;
            int32_t t120 = gm_l7;
            uint8_t t121 = ((t120 == ((int32_t)0LL)));
            t92 = t121;
            goto me93;
mn118:;
ma99:;
            int32_t t123 = gm_l7;
            uint8_t t124 = (!(t123 == ((int32_t)0LL)));
            t92 = t124;
            goto me93;
mn122:;
ma100:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 105, 30);
me93:;
            gm_t0ialelq3ql1k t125;
            memset(&t125, 0, sizeof t125);
            GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)3ULL));
            GM_ST(uint8_t, ((uint8_t *)&t125 + 8), t92);
            gm_t4t568o7qnv02 t126;
            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t126 + 8), t125);
            uint8_t t127 = 0;
            F.ret = t126;
            goto gm_fret;
        }
        goto me20;
mn27:;
ma22:;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t131 != ((uint8_t)0ULL)) goto pa130;
        goto po129;
pa130:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t133 != ((uint8_t)1ULL)) goto pa132;
        goto po129;
pa132:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t135 != ((uint8_t)2ULL)) goto pa134;
        goto po129;
pa134:;
        uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t137 != ((uint8_t)4ULL)) goto pa136;
        goto po129;
pa136:;
        goto mn128;
po129:;
        {
            void * t140 = gm_l1;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)t140) + 0));
            if (t141 != ((uint8_t)0ULL)) goto ie139;
            gm_tbci5ospk5nnn t142 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t140) + 8));
            F.l8 = t142;
            void * t143 = gm_l2;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)t143) + 0));
            if (t144 != ((uint8_t)0ULL)) goto ie139;
            gm_tbci5ospk5nnn t145 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t143) + 8));
            F.l9 = t145;
            {
                gm_tbci5ospk5nnn t147;
                uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                switch (t154) { case ((uint8_t)0ULL): goto ma149; case ((uint8_t)1ULL): goto ma150; case ((uint8_t)2ULL): goto ma151; case ((uint8_t)3ULL): goto ma152; case ((uint8_t)4ULL): goto ma152; case ((uint8_t)5ULL): goto ma152; case ((uint8_t)6ULL): goto ma152; case ((uint8_t)7ULL): goto ma152; case ((uint8_t)8ULL): goto ma152; case ((uint8_t)9ULL): goto ma152; case ((uint8_t)10ULL): goto ma152; case ((uint8_t)11ULL): goto ma152; case ((uint8_t)12ULL): goto ma152; case ((uint8_t)13ULL): goto ma152; case ((uint8_t)14ULL): goto ma152; case ((uint8_t)15ULL): goto ma152; case ((uint8_t)16ULL): goto ma152; case ((uint8_t)17ULL): goto ma152; case ((uint8_t)18ULL): goto ma152; case ((uint8_t)19ULL): goto ma152; case ((uint8_t)20ULL): goto ma152; case ((uint8_t)21ULL): goto ma152; default: goto ma152; }
ma149:;
                uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t156 != ((uint8_t)0ULL)) goto mn155;
                gm_tbci5ospk5nnn t157;
                gm_f_add_420lj6o((void *)&t157, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t147 = t157;
                goto me148;
mn155:;
ma150:;
                uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t159 != ((uint8_t)1ULL)) goto mn158;
                gm_tbci5ospk5nnn t160;
                gm_f_sub_pktm5cr((void *)&t160, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t147 = t160;
                goto me148;
mn158:;
ma151:;
                uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                if (t162 != ((uint8_t)2ULL)) goto mn161;
                gm_tbci5ospk5nnn t163;
                gm_f_mul_nfj5rjv((void *)&t163, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t147 = t163;
                goto me148;
mn161:;
ma152:;
                {
                    uint8_t t167 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                    if (!t167) goto ie166;
                    {
                        gm_b16_8 t169 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                        gm_t4t568o7qnv02 t170;
                        memset(&t170, 0, sizeof t170);
                        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 8), t169);
                        uint8_t t171 = 0;
                        F.ret = t170;
                        goto gm_fret;
                    }
                    goto ix166;
ie166:;
ix166:;
                }
                gm_tbci5ospk5nnn t172;
                gm_f_div_f0qrfbr((void *)&t172, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l8)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t173 = t172;
                t147 = t173;
                goto me148;
mn164:;
ma153:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 116, 33);
me148:;
                gm_t0ialelq3ql1k t174;
                memset(&t174, 0, sizeof t174);
                GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t174 + 8), t147);
                gm_t4t568o7qnv02 t175;
                GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t175 + 8), t174);
                uint8_t t176 = 0;
                F.ret = t175;
                goto gm_fret;
            }
            goto ix139;
ie139:;
ix139:;
        }
        {
            void * t177 = gm_l1;
            gm_tvegkb6ouf3m0 t178;
            gm_f_as_rational_dj3nve5((void *)&t178, (void *)NULL, cx, NULL, t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t179 = t178;
            gm_tngtuiuskn938 t180;
            if (((((uint8_t *)&t179))[96] != 0)) {
                gm_tngtuiuskn938 t181 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t179) + 0));
                t180 = t181;
            } else {
                gm_b16_8 t182;
                GM_ST(void *, (uint8_t *)&t182, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(27));
                gm_t4t568o7qnv02 t183;
                memset(&t183, 0, sizeof t183);
                GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t183 + 8), t182);
                uint8_t t184 = 0;
                F.ret = t183;
                goto gm_fret;
            }
            F.l10 = t180;
        }
        {
            void * t185 = gm_l2;
            gm_tvegkb6ouf3m0 t186;
            gm_f_as_rational_dj3nve5((void *)&t186, (void *)NULL, cx, NULL, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t187 = t186;
            gm_tngtuiuskn938 t188;
            if (((((uint8_t *)&t187))[96] != 0)) {
                gm_tngtuiuskn938 t189 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t187) + 0));
                t188 = t189;
            } else {
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s8qs8rpj34te9));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(27));
                gm_t4t568o7qnv02 t191;
                memset(&t191, 0, sizeof t191);
                GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t191 + 8), t190);
                uint8_t t192 = 0;
                F.ret = t191;
                goto gm_fret;
            }
            F.l11 = t188;
        }
        {
            gm_tngtuiuskn938 t193;
            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            switch (t200) { case ((uint8_t)0ULL): goto ma195; case ((uint8_t)1ULL): goto ma196; case ((uint8_t)2ULL): goto ma197; case ((uint8_t)3ULL): goto ma198; case ((uint8_t)4ULL): goto ma198; case ((uint8_t)5ULL): goto ma198; case ((uint8_t)6ULL): goto ma198; case ((uint8_t)7ULL): goto ma198; case ((uint8_t)8ULL): goto ma198; case ((uint8_t)9ULL): goto ma198; case ((uint8_t)10ULL): goto ma198; case ((uint8_t)11ULL): goto ma198; case ((uint8_t)12ULL): goto ma198; case ((uint8_t)13ULL): goto ma198; case ((uint8_t)14ULL): goto ma198; case ((uint8_t)15ULL): goto ma198; case ((uint8_t)16ULL): goto ma198; case ((uint8_t)17ULL): goto ma198; case ((uint8_t)18ULL): goto ma198; case ((uint8_t)19ULL): goto ma198; case ((uint8_t)20ULL): goto ma198; case ((uint8_t)21ULL): goto ma198; default: goto ma198; }
ma195:;
            uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t202 != ((uint8_t)0ULL)) goto mn201;
            gm_tngtuiuskn938 t203;
            gm_f_add_837b4po((void *)&t203, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t193 = t203;
            goto me194;
mn201:;
ma196:;
            uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t205 != ((uint8_t)1ULL)) goto mn204;
            gm_tngtuiuskn938 t206;
            gm_f_sub_mler6q8((void *)&t206, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t193 = t206;
            goto me194;
mn204:;
ma197:;
            uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            if (t208 != ((uint8_t)2ULL)) goto mn207;
            gm_tngtuiuskn938 t209;
            gm_f_mul_od1e7nd((void *)&t209, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t193 = t209;
            goto me194;
mn207:;
ma198:;
            {
                uint8_t t213 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
                if (!t213) goto ie212;
                {
                    gm_b16_8 t215 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                    gm_t4t568o7qnv02 t216;
                    memset(&t216, 0, sizeof t216);
                    GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t216 + 8), t215);
                    uint8_t t217 = 0;
                    F.ret = t216;
                    goto gm_fret;
                }
                goto ix212;
ie212:;
ix212:;
            }
            gm_tngtuiuskn938 t218;
            gm_f_div_d8sd4qc((void *)&t218, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l10)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t219 = t218;
            t193 = t219;
            goto me194;
mn210:;
ma199:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 128, 18);
me194:;
            F.l12 = t193;
        }
        {
            gm_ts6oqppjh0ba3 t220 = gm_l0;
            void * t221 = gm_l1;
            void * t222 = gm_l2;
            gm_t0ialelq3ql1k t223;
            gm_f_float_result_csnia2t((void *)&t223, (void *)NULL, cx, NULL, t220, t221, t222, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l12)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t4t568o7qnv02 t224;
            GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t224 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t224 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t224 + 8), t223);
            uint8_t t225 = 0;
            F.ret = t224;
            goto gm_fret;
        }
        goto me20;
mn128:;
ma23:;
        uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t229 != ((uint8_t)5ULL)) goto pa228;
        goto po227;
pa228:;
        uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t231 != ((uint8_t)7ULL)) goto pa230;
        goto po227;
pa230:;
        uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t233 != ((uint8_t)8ULL)) goto pa232;
        goto po227;
pa232:;
        uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t235 != ((uint8_t)9ULL)) goto pa234;
        goto po227;
pa234:;
        uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t237 != ((uint8_t)3ULL)) goto pa236;
        goto po227;
pa236:;
        uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t239 != ((uint8_t)6ULL)) goto pa238;
        goto po227;
pa238:;
        goto mn226;
po227:;
        {
            void * t242 = gm_l1;
            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)t242) + 0));
            if (t243 != ((uint8_t)0ULL)) goto ie241;
            gm_tbci5ospk5nnn t244 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t242) + 8));
            F.l13 = t244;
            {
                void * t247 = gm_l2;
                uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)t247) + 0));
                if (t248 != ((uint8_t)0ULL)) goto ie246;
                gm_tbci5ospk5nnn t249 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t247) + 8));
                F.l14 = t249;
                {
                    gm_tbci5ospk5nnn t251;
                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    switch (t259) { case ((uint8_t)0ULL): goto ma257; case ((uint8_t)1ULL): goto ma257; case ((uint8_t)2ULL): goto ma257; case ((uint8_t)3ULL): goto ma257; case ((uint8_t)4ULL): goto ma257; case ((uint8_t)5ULL): goto ma253; case ((uint8_t)6ULL): goto ma257; case ((uint8_t)7ULL): goto ma254; case ((uint8_t)8ULL): goto ma255; case ((uint8_t)9ULL): goto ma256; case ((uint8_t)10ULL): goto ma257; case ((uint8_t)11ULL): goto ma257; case ((uint8_t)12ULL): goto ma257; case ((uint8_t)13ULL): goto ma257; case ((uint8_t)14ULL): goto ma257; case ((uint8_t)15ULL): goto ma257; case ((uint8_t)16ULL): goto ma257; case ((uint8_t)17ULL): goto ma257; case ((uint8_t)18ULL): goto ma257; case ((uint8_t)19ULL): goto ma257; case ((uint8_t)20ULL): goto ma257; case ((uint8_t)21ULL): goto ma257; default: goto ma257; }
ma253:;
                    uint8_t t261 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t261 != ((uint8_t)5ULL)) goto mn260;
                    {
                        uint8_t t264 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        if (!t264) goto ie263;
                        {
                            gm_b16_8 t266 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                            gm_t4t568o7qnv02 t267;
                            memset(&t267, 0, sizeof t267);
                            GM_ST(uint8_t, ((uint8_t *)&t267 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t267 + 8), t266);
                            uint8_t t268 = 0;
                            F.ret = t267;
                            goto gm_fret;
                        }
                        goto ix263;
ie263:;
ix263:;
                    }
                    gm_tbci5ospk5nnn t269;
                    gm_f_rem_ib4l1vl((void *)&t269, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t270 = t269;
                    t251 = t270;
                    goto me252;
mn260:;
ma254:;
                    uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t272 != ((uint8_t)7ULL)) goto mn271;
                    gm_tbci5ospk5nnn t273;
                    gm_f_bit_and_3tgam8m((void *)&t273, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t251 = t273;
                    goto me252;
mn271:;
ma255:;
                    uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t275 != ((uint8_t)8ULL)) goto mn274;
                    gm_tbci5ospk5nnn t276;
                    gm_f_bit_or_j1tdrhl((void *)&t276, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t251 = t276;
                    goto me252;
mn274:;
ma256:;
                    uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    if (t278 != ((uint8_t)9ULL)) goto mn277;
                    gm_tbci5ospk5nnn t279;
                    gm_f_bit_xor_tmvdnad((void *)&t279, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    t251 = t279;
                    goto me252;
mn277:;
ma257:;
                    {
                        uint8_t t283 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        if (!t283) goto ie282;
                        {
                            gm_b16_8 t285;
                            GM_ST(void *, (uint8_t *)&t285, (void *)(gm_snod68sv8lqq6));
                            GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(44));
                            gm_t4t568o7qnv02 t286;
                            memset(&t286, 0, sizeof t286);
                            GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t286 + 8), t285);
                            uint8_t t287 = 0;
                            F.ret = t286;
                            goto gm_fret;
                        }
                        goto ix282;
ie282:;
ix282:;
                    }
                    {
                        gm_tf5srh4mp4b3i t288 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)));
                        gm_tf5srh4mp4b3i t289 = t288;
                        uint64_t t290;
                        if (((((uint8_t *)&t289))[8] != 0)) {
                            uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&t289) + 0));
                            t290 = t291;
                        } else {
                            gm_b16_8 t292;
                            GM_ST(void *, (uint8_t *)&t292, (void *)(gm_s73ofu7ms28r6));
                            GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(22));
                            gm_t4t568o7qnv02 t293;
                            memset(&t293, 0, sizeof t293);
                            GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 8), t292);
                            uint8_t t294 = 0;
                            F.ret = t293;
                            goto gm_fret;
                        }
                        gm_l15 = t290;
                    }
                    {
                        uint64_t t296 = gm_l15;
                        unsigned __int128 n297 = (unsigned __int128)((uint64_t)20ULL);
                        if (n297 >= 64) gm_trap_shift((uint64_t)n297, "u64", gm_src7h8dni243k2v, 153, 28);
                        uint64_t r298 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n297);
                        bool o299 = (uint64_t)(r298 >> (int)n297) != ((uint64_t)1ULL);
                        if (o299) gm_trap("integer overflow in `<<`", gm_src7h8dni243k2v, 153, 28);
                        uint64_t t300 = r298;
                        uint8_t t301 = (t296 > t300);
                        if (!t301) goto ie295;
                        {
                            gm_b16_8 t303;
                            GM_ST(void *, (uint8_t *)&t303, (void *)(gm_s73ofu7ms28r6));
                            GM_ST(uintptr_t, (uint8_t *)&t303 + 8, (uintptr_t)(22));
                            gm_t4t568o7qnv02 t304;
                            memset(&t304, 0, sizeof t304);
                            GM_ST(uint8_t, ((uint8_t *)&t304 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t304 + 8), t303);
                            uint8_t t305 = 0;
                            F.ret = t304;
                            goto gm_fret;
                        }
                        goto ix295;
ie295:;
ix295:;
                    }
                    gm_tbci5ospk5nnn t306;
                    uint8_t t308 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l0) + 0) == ((uint8_t)6ULL)));
                    if (!t308) goto ie307;
                    uint64_t t310 = gm_l15;
                    gm_tbci5ospk5nnn t311;
                    gm_f_shr_4jp99a9((void *)&t311, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), t310);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t312 = t311;
                    t306 = t312;
                    goto ix307;
ie307:;
                    uint64_t t314 = gm_l15;
                    gm_tbci5ospk5nnn t315;
                    gm_f_shl_qqecl81((void *)&t315, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l13)), t314);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t316 = t315;
                    t306 = t316;
ix307:;
                    gm_tbci5ospk5nnn t317 = t306;
                    t251 = t317;
                    goto me252;
mn280:;
ma258:;
                    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 142, 29);
me252:;
                    gm_t0ialelq3ql1k t318;
                    memset(&t318, 0, sizeof t318);
                    GM_ST(uint8_t, ((uint8_t *)&t318 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t318 + 8), t251);
                    gm_t4t568o7qnv02 t319;
                    GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t319 + 8), t318);
                    uint8_t t320 = 0;
                    F.ret = t319;
                    goto gm_fret;
                }
                goto ix246;
ie246:;
                {
                    gm_ts6oqppjh0ba3 t322 = gm_l0;
                    gm_b16_8 t323 = gm_f_binop_text_0ljfc0r(t322);
                    gm_tnpifv20irnph t324;
                    GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
                    gm_tabb710d9mq2e t325;
                    gm_f_format_obkf19v((void *)&t325, (void *)NULL, cx, NULL, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t326 = t325;
                    if (((((uint8_t *)&t326))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t326) + 8, gm_src7h8dni243k2v, 141, 44);
                    gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&t326) + 8));
                    gm_b16_8 t328 = t327;
                    gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                    gm_t4t568o7qnv02 t330;
                    memset(&t330, 0, sizeof t330);
                    GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 8), t329);
                    uint8_t t331 = 0;
                    F.ret = t330;
                    goto gm_fret;
                }
ix246:;
            }
            goto ix241;
ie241:;
            {
                gm_ts6oqppjh0ba3 t333 = gm_l0;
                gm_b16_8 t334 = gm_f_binop_text_0ljfc0r(t333);
                gm_tnpifv20irnph t335;
                GM_ST(gm_b16_8, ((uint8_t *)&t335 + 0), t334);
                gm_tabb710d9mq2e t336;
                gm_f_format_obkf19v((void *)&t336, (void *)NULL, cx, NULL, t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t337 = t336;
                if (((((uint8_t *)&t337))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t337) + 8, gm_src7h8dni243k2v, 140, 44);
                gm_b16_8 t338 = GM_LD(gm_b16_8, (((uint8_t *)&t337) + 8));
                gm_b16_8 t339 = t338;
                gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                gm_t4t568o7qnv02 t341;
                memset(&t341, 0, sizeof t341);
                GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t341 + 8), t340);
                uint8_t t342 = 0;
                F.ret = t341;
                goto gm_fret;
            }
ix241:;
        }
        goto me20;
mn226:;
ma24:;
        gm_ts6oqppjh0ba3 t344 = gm_l0;
        gm_b16_8 t345 = gm_f_binop_text_0ljfc0r(t344);
        gm_tnpifv20irnph t346;
        GM_ST(gm_b16_8, ((uint8_t *)&t346 + 0), t345);
        gm_tabb710d9mq2e t347;
        gm_f_format_9453blg((void *)&t347, (void *)NULL, cx, NULL, t346);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t348 = t347;
        if (((((uint8_t *)&t348))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t348) + 8, gm_src7h8dni243k2v, 158, 26);
        gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&t348) + 8));
        gm_b16_8 t350 = t349;
        gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
        gm_t4t568o7qnv02 t352;
        memset(&t352, 0, sizeof t352);
        GM_ST(uint8_t, ((uint8_t *)&t352 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t352 + 8), t351);
        uint8_t t353 = 0;
        F.ret = t352;
        goto gm_fret;
        goto me20;
mn343:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 93, 5);
me20:;
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
__attribute__((aligned(4))) void gm_f_complex_binary_fggi5t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2) {
    struct gm_fr_complex_binary_fggi5t8 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    GM_STACK_CHECK(&F, 7619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_tk3f7nn6jq07c t3;
        gm_f_as_complex_7e8vejf((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tk3f7nn6jq07c t4 = t3;
        gm_tu15p0hi2qlv6 t5;
        if (((((uint8_t *)&t4))[192] != 0)) {
            gm_tu15p0hi2qlv6 t6 = GM_LD(gm_tu15p0hi2qlv6, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s8qs8rpj34te9));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(27));
            gm_t4t568o7qnv02 t8;
            memset(&t8, 0, sizeof t8);
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        F.l3 = t5;
    }
    {
        void * t10 = gm_l2;
        gm_tk3f7nn6jq07c t11;
        gm_f_as_complex_7e8vejf((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tk3f7nn6jq07c t12 = t11;
        gm_tu15p0hi2qlv6 t13;
        if (((((uint8_t *)&t12))[192] != 0)) {
            gm_tu15p0hi2qlv6 t14 = GM_LD(gm_tu15p0hi2qlv6, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s8qs8rpj34te9));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(27));
            gm_t4t568o7qnv02 t16;
            memset(&t16, 0, sizeof t16);
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        F.l4 = t13;
    }
    {
        gm_tngtuiuskn938 t18 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l3) + 0));
        F.l5 = t18;
    }
    {
        gm_tngtuiuskn938 t19 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l3) + 96));
        F.l6 = t19;
    }
    {
        gm_tngtuiuskn938 t20 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 0));
        F.l7 = t20;
    }
    {
        gm_tngtuiuskn938 t21 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&F.l4) + 96));
        F.l8 = t21;
    }
    {
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma30; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma27; case ((uint8_t)13ULL): goto ma28; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma29; case ((uint8_t)18ULL): goto ma29; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; default: goto ma30; }
ma23:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t34 != ((uint8_t)0ULL)) goto mn33;
        gm_tu15p0hi2qlv6 t35;
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t35 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t35 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t36 + 8), t35);
        gm_t4t568o7qnv02 t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t37 + 8), t36);
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
        goto me22;
mn33:;
ma24:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        gm_tu15p0hi2qlv6 t41;
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t41 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t41 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t42 + 8), t41);
        gm_t4t568o7qnv02 t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t43 + 8), t42);
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me22;
mn39:;
ma25:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t46 != ((uint8_t)2ULL)) goto mn45;
        gm_tu15p0hi2qlv6 t47;
        gm_tngtuiuskn938 t48;
        gm_f_mul_od1e7nd((void *)&t48, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t49;
        gm_f_mul_od1e7nd((void *)&t49, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t47 + 0)), (void *)NULL, cx, NULL, &t48, &t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t50;
        gm_f_mul_od1e7nd((void *)&t50, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t51;
        gm_f_mul_od1e7nd((void *)&t51, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_add_837b4po((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t47 + 96)), (void *)NULL, cx, NULL, &t50, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0ialelq3ql1k t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t52 + 8), t47);
        gm_t4t568o7qnv02 t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t53 + 8), t52);
        uint8_t t54 = 0;
        F.ret = t53;
        goto gm_fret;
        goto me22;
mn45:;
ma26:;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t56 != ((uint8_t)4ULL)) goto mn55;
        {
            gm_tngtuiuskn938 t58;
            gm_f_mul_od1e7nd((void *)&t58, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t59;
            gm_f_mul_od1e7nd((void *)&t59, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_837b4po((void *)&F.l9, (void *)NULL, cx, NULL, &t58, &t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t61 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (!t61) goto ie60;
            {
                gm_b16_8 t63 = GM_LD(gm_b16_8, gm_cii3u4ppm6k);
                gm_t4t568o7qnv02 t64;
                memset(&t64, 0, sizeof t64);
                GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t64 + 8), t63);
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
            goto ix60;
ie60:;
ix60:;
        }
        {
            gm_tu15p0hi2qlv6 t66;
            gm_tngtuiuskn938 t67;
            gm_f_mul_od1e7nd((void *)&t67, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t68;
            gm_f_mul_od1e7nd((void *)&t68, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t69;
            gm_f_add_837b4po((void *)&t69, (void *)NULL, cx, NULL, &t67, &t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_div_d8sd4qc((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t66 + 0)), (void *)NULL, cx, NULL, &t69, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t70;
            gm_f_mul_od1e7nd((void *)&t70, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t71;
            gm_f_mul_od1e7nd((void *)&t71, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t72;
            gm_f_sub_mler6q8((void *)&t72, (void *)NULL, cx, NULL, &t70, &t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_div_d8sd4qc((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t66 + 96)), (void *)NULL, cx, NULL, &t72, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0ialelq3ql1k t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)4ULL));
            GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t73 + 8), t66);
            gm_t4t568o7qnv02 t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t74 + 8), t73);
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto me22;
mn55:;
ma27:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t77 != ((uint8_t)12ULL)) goto mn76;
        uint8_t t78 = 0;
        uint8_t t79 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        if (t79) {
            uint8_t t80 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            t78 = t80;
        } else t78 = 0;
        gm_t0ialelq3ql1k t81;
        memset(&t81, 0, sizeof t81);
        GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t81 + 8), t78);
        gm_t4t568o7qnv02 t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t82 + 8), t81);
        uint8_t t83 = 0;
        F.ret = t82;
        goto gm_fret;
        goto me22;
mn76:;
ma28:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t85 != ((uint8_t)13ULL)) goto mn84;
        uint8_t t86 = 0;
        uint8_t t87 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l7)));
        uint8_t t88 = (uint8_t)!t87;
        if (!t88) {
            uint8_t t89 = gm_f_eq_ceufbo4(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l8)));
            uint8_t t90 = (uint8_t)!t89;
            t86 = t90;
        } else t86 = 1;
        gm_t0ialelq3ql1k t91;
        memset(&t91, 0, sizeof t91);
        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t91 + 8), t86);
        gm_t4t568o7qnv02 t92;
        GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t92 + 8), t91);
        uint8_t t93 = 0;
        F.ret = t92;
        goto gm_fret;
        goto me22;
mn84:;
ma29:;
        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t97 != ((uint8_t)16ULL)) goto pa96;
        goto po95;
pa96:;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t99 != ((uint8_t)17ULL)) goto pa98;
        goto po95;
pa98:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t101 != ((uint8_t)18ULL)) goto pa100;
        goto po95;
pa100:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t103 != ((uint8_t)19ULL)) goto pa102;
        goto po95;
pa102:;
        goto mn94;
po95:;
        gm_b16_8 t104;
        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_scaduriptfv6s));
        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(33));
        gm_t4t568o7qnv02 t105;
        memset(&t105, 0, sizeof t105);
        GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t104);
        uint8_t t106 = 0;
        F.ret = t105;
        goto gm_fret;
        goto me22;
mn94:;
ma30:;
        gm_ts6oqppjh0ba3 t108 = gm_l0;
        gm_b16_8 t109 = gm_f_binop_text_0ljfc0r(t108);
        gm_tnpifv20irnph t110;
        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t109);
        gm_tabb710d9mq2e t111;
        gm_f_format_thddtj7((void *)&t111, (void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t112 = t111;
        if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_src7h8dni243k2v, 183, 26);
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
        gm_b16_8 t114 = t113;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        gm_t4t568o7qnv02 t116;
        memset(&t116, 0, sizeof t116);
        GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t116 + 8), t115);
        uint8_t t117 = 0;
        F.ret = t116;
        goto gm_fret;
        goto me22;
mn107:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 171, 5);
me22:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_bm129vh(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_describe_bm129vh F;
    void * gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma8; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sc003f3fvnij4));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(16));
    t2 = t12;
    goto me3;
mn10:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t16 != ((uint8_t)1ULL)) goto pa15;
    goto po14;
pa15:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t18 != ((uint8_t)2ULL)) goto pa17;
    goto po14;
pa17:;
    goto mn13;
po14:;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_splm7v4oko2tv));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(14));
    t2 = t19;
    goto me3;
mn13:;
ma6:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t21 != ((uint8_t)3ULL)) goto mn20;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sek3nvre3su54));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(16));
    t2 = t22;
    goto me3;
mn20:;
ma7:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t24 != ((uint8_t)4ULL)) goto mn23;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sv56kn0notafb));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(16));
    t2 = t25;
    goto me3;
mn23:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 29, 48);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dump_untyped_o8mt4q8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_untyped_o8mt4q8 F;
    void * gm_l0; /* v */
    uint8_t gm_l3; /* b */
    GM_STACK_CHECK(&F, 938u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t12 != ((uint8_t)0ULL)) goto mn11;
    gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t13;
    gm_b16_8 t14 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_tabb710d9mq2e t16;
    gm_f_format_ejli3qf((void *)&t16, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t17 = t16;
    if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_src7h8dni243k2v, 214, 16);
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
    gm_b16_8 t19 = t18;
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    t2 = t20;
    goto me3;
mn11:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t22 != ((uint8_t)1ULL)) goto mn21;
    gm_tngtuiuskn938 t23 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t23;
    gm_b16_8 t24 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t25;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
    gm_tabb710d9mq2e t26;
    gm_f_format_o65p921((void *)&t26, (void *)NULL, cx, NULL, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t27 = t26;
    if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src7h8dni243k2v, 215, 18);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
    gm_b16_8 t29 = t28;
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
    t2 = t30;
    goto me3;
mn21:;
ma6:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s9c60h0qendav));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(8));
    t2 = t33;
    goto me3;
mn31:;
ma7:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t35 != ((uint8_t)3ULL)) goto mn34;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t1) + 8));
    gm_l3 = t36;
    uint8_t t37 = gm_l3;
    gm_t9qkh0rsooc8d t38;
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), t37);
    gm_tabb710d9mq2e t39;
    gm_f_format_as0b1t3((void *)&t39, (void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t40 = t39;
    if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_src7h8dni243k2v, 217, 17);
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
    gm_b16_8 t42 = t41;
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
    t2 = t43;
    goto me3;
mn34:;
ma8:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t45 != ((uint8_t)4ULL)) goto mn44;
    gm_tngtuiuskn938 t46 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 0));
    F.l4 = t46;
    gm_tngtuiuskn938 t47 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 96));
    F.l5 = t47;
    gm_t6j6smj7qoctu t48;
    gm_b16_8 t49 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t48 + 0, t49);
    gm_b16_8 t50 = gm_f_show_rational_00mlc2h((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t48 + 16, t50);
    gm_tabb710d9mq2e t51;
    gm_f_format_djdpqv2((void *)&t51, (void *)NULL, cx, NULL, &t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t52 = t51;
    if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_src7h8dni243k2v, 218, 25);
    gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
    gm_b16_8 t54 = t53;
    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
    t2 = t55;
    goto me3;
mn44:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 213, 64);
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
inline __attribute__((aligned(4))) void gm_f_float_result_csnia2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0, void * a1, void * a2, const void * a3) {
    struct gm_fr_float_result_csnia2t F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint8_t gm_l4; /* negative */
    GM_STACK_CHECK(&F, 1487u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)a3)));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_tngtuiuskn938 t6 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)a3));
            gm_t0ialelq3ql1k t7;
            memset(&t7, 0, sizeof t7);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t7 + 8), t6);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t9;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; default: goto ma13; }
ma11:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t17 != ((uint8_t)0ULL)) goto mn16;
        uint8_t t18 = 0;
        void * t19 = gm_l1;
        uint8_t t20 = ((GM_LD(uint8_t, ((uint8_t *)t19) + 0) == ((uint8_t)2ULL)));
        if (t20) {
            void * t21 = gm_l2;
            uint8_t t22 = ((GM_LD(uint8_t, ((uint8_t *)t21) + 0) == ((uint8_t)2ULL)));
            t18 = t22;
        } else t18 = 0;
        t9 = t18;
        goto me10;
mn16:;
ma12:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        uint8_t t25 = 0;
        uint8_t t26 = 0;
        void * t27 = gm_l1;
        uint8_t t28 = ((GM_LD(uint8_t, ((uint8_t *)t27) + 0) == ((uint8_t)2ULL)));
        if (t28) {
            void * t29 = gm_l2;
            uint8_t t30 = (!(GM_LD(uint8_t, ((uint8_t *)t29) + 0) == ((uint8_t)2ULL)));
            t26 = t30;
        } else t26 = 0;
        if (t26) {
            void * t31 = gm_l2;
            uint8_t t32 = gm_f_is_zero_f3o1s37((void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t25 = t32;
        } else t25 = 0;
        t9 = t25;
        goto me10;
mn23:;
ma13:;
        void * t34 = gm_l1;
        uint8_t t35 = gm_f_is_neg_q1tmt2n(t34);
        void * t36 = gm_l2;
        uint8_t t37 = gm_f_is_neg_q1tmt2n(t36);
        uint8_t t38 = (!(t35 == t37));
        t9 = t38;
        goto me10;
mn33:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 190, 17);
me10:;
        gm_l4 = t9;
    }
    {
        gm_t0ialelq3ql1k t39;
        uint8_t t41 = gm_l4;
        if (!t41) goto ie40;
        gm_t0ialelq3ql1k t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)2ULL));
        gm_t0ialelq3ql1k t44 = t43;
        t39 = t44;
        goto ix40;
ie40:;
        gm_tngtuiuskn938 t46 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)a3));
        gm_t0ialelq3ql1k t47;
        memset(&t47, 0, sizeof t47);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t47 + 8), t46);
        gm_t0ialelq3ql1k t48 = t47;
        t39 = t48;
ix40:;
        uint8_t t49 = 0;
        F.ret = t39;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_neg_q1tmt2n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_neg_q1tmt2n F;
    void * gm_l0; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t11 != ((uint8_t)2ULL)) goto mn10;
    t2 = ((uint8_t)1);
    goto me3;
mn10:;
ma5:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    gm_tbci5ospk5nnn t14 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t14;
    uint8_t t15 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    t2 = t15;
    goto me3;
mn12:;
ma6:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    gm_tngtuiuskn938 t18 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t18;
    uint8_t t19 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    t2 = t19;
    goto me3;
mn16:;
ma7:;
    t2 = ((uint8_t)0);
    goto me3;
mn20:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 199, 44);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_zero_f3o1s37(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_negate_fohael7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_negate_fohael7 F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 3655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t4t568o7qnv02 t2;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t12 != ((uint8_t)0ULL)) goto mn11;
    gm_tbci5ospk5nnn t13 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t1) + 8));
    F.l1 = t13;
    gm_tbci5ospk5nnn t14;
    gm_f_neg_agd5c5k((void *)&t14, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t15;
    memset(&t15, 0, sizeof t15);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t15 + 8), t14);
    gm_t4t568o7qnv02 t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t16 + 8), t15);
    t2 = t16;
    goto me3;
mn11:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t18 != ((uint8_t)1ULL)) goto mn17;
    gm_tngtuiuskn938 t19 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t1) + 8));
    F.l2 = t19;
    gm_t0ialelq3ql1k t20;
    uint8_t t22 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (!t22) goto ie21;
    gm_t0ialelq3ql1k t24;
    memset(&t24, 0, sizeof t24);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
    gm_t0ialelq3ql1k t25 = t24;
    t20 = t25;
    goto ix21;
ie21:;
    gm_tngtuiuskn938 t27;
    gm_f_neg_0u9uqg6((void *)&t27, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t28;
    memset(&t28, 0, sizeof t28);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t28 + 8), t27);
    gm_t0ialelq3ql1k t29 = t28;
    t20 = t29;
ix21:;
    gm_t4t568o7qnv02 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t30 + 8), t20);
    t2 = t30;
    goto me3;
mn17:;
ma6:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    gm_tngtuiuskn938 t33;
    gm_f_zero_9r6o7sj((void *)&t33, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t34;
    memset(&t34, 0, sizeof t34);
    GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)1ULL));
    GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t34 + 8), t33);
    gm_t4t568o7qnv02 t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t35 + 8), t34);
    t2 = t35;
    goto me3;
mn31:;
ma7:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t37 != ((uint8_t)3ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_stpkldc5dmf71));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(32));
    gm_t4t568o7qnv02 t39;
    memset(&t39, 0, sizeof t39);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)1ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
    t2 = t39;
    goto me3;
mn36:;
ma8:;
    uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t1) + 0));
    if (t41 != ((uint8_t)4ULL)) goto mn40;
    gm_tngtuiuskn938 t42 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 0));
    F.l3 = t42;
    gm_tngtuiuskn938 t43 = GM_LD(gm_tngtuiuskn938, ((((uint8_t *)t1) + 8) + 96));
    F.l4 = t43;
    gm_tu15p0hi2qlv6 t44;
    gm_f_neg_0u9uqg6((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t44 + 0)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_neg_0u9uqg6((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t44 + 96)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t0ialelq3ql1k t45;
    GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)4ULL));
    GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t45 + 8), t44);
    gm_t4t568o7qnv02 t46;
    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t46 + 8), t45);
    t2 = t46;
    goto me3;
mn40:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 51, 65);
me3:;
    F.ret = t2;
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
extern __attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_rational_bits_u60jbai_c(gm_tngtuiuskn938 a0) { return gm_f_rational_bits_u60jbai(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_int_ub6npub F;
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        uint64_t t3 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        uint8_t t4 = (t3 <= ((uint64_t)512ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t7 = t6;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t10 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        gm_t4ce0jd3tdro4 t11;
        GM_ST(uint64_t, ((uint8_t *)&t11 + 0), t10);
        gm_tabb710d9mq2e t12;
        gm_f_format_5k1v0hr((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_src7h8dni243k2v, 68, 12);
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
        gm_b16_8 t15 = t14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        uint8_t t17 = 0;
        F.ret = t16;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rational_00mlc2h(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rational_00mlc2h F;
    GM_STACK_CHECK(&F, 211u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)a0)));
        if (!t3) goto ie2;
        {
            gm_b16_8 t5 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t7;
        gm_b16_8 t8 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
        gm_b16_8 t9 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_d801g52((void *)&t10, (void *)NULL, cx, NULL, &t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src7h8dni243k2v, 223, 12);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        uint8_t t15 = 0;
        F.ret = t14;
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
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_try_untyped_fj0h8i5 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* lit */
    void * gm_l4; /* lit */
    void * gm_l6; /* inner */
    gm_t08olq97rscn3 gm_l7; /* id */
    uint64_t gm_l10; /* d */
    void * gm_l12; /* base */
    gm_t08olq97rscn3 gm_l13; /* name */
    uint64_t gm_l14; /* m */
    uint64_t gm_l15; /* d */
    void * gm_l17; /* operand */
    gm_b16_8 gm_l21; /* m */
    gm_ts6oqppjh0ba3 gm_l22; /* op */
    void * gm_l23; /* lhs */
    void * gm_l24; /* rhs */
    gm_t9qkh0rsooc8d gm_l25; /* f */
    gm_t9qkh0rsooc8d gm_l26; /* f */
    gm_t9qkh0rsooc8d gm_l27; /* f */
    gm_t9qkh0rsooc8d gm_l28; /* f */
    gm_b16_8 gm_l34; /* m */
    GM_STACK_CHECK(&F, 18365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t16;
        {
            void * t19 = gm_l3;
            uint8_t t20;
            if ((((((uint8_t *)t19) + 48))[8] != 0)) goto mn22;
            t20 = ((uint8_t)0);
            goto me21;
mn22:;
            t20 = ((uint8_t)1);
            goto me21;
mn23:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 233, 16);
me21:;
            if (!t20) goto ie18;
            {
                gm_tkv1vap90vht2 t25;
                memset(&t25, 0, sizeof t25);
                uint8_t t26 = 0;
                F.ret = t25;
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t27 = gm_l3;
            gm_tbci5ospk5nnn t28 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t27) + 0));
            gm_t0ialelq3ql1k t29;
            memset(&t29, 0, sizeof t29);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t29 + 8), t28);
            gm_t5vbkdq81qfs7 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t30 + 8), t29);
            gm_tkv1vap90vht2 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 212, 0);
            GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t31), t30);
            ((uint8_t *)&t31)[208] = 1;
            uint8_t t32 = 0;
            F.ret = t31;
            goto gm_fret;
        }
        goto me3;
mn14:;
ma5:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)1ULL)) goto mn33;
        void * t35 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t35;
        {
            void * t38 = gm_l4;
            uint8_t t39;
            if ((((((uint8_t *)t38) + 96))[1] != 0)) goto mn41;
            t39 = ((uint8_t)0);
            goto me40;
mn41:;
            t39 = ((uint8_t)1);
            goto me40;
mn42:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 237, 16);
me40:;
            if (!t39) goto ie37;
            {
                gm_tkv1vap90vht2 t44;
                memset(&t44, 0, sizeof t44);
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            void * t46 = gm_l4;
            gm_tngtuiuskn938 t47 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t46) + 0));
            F.l5 = t47;
        }
        {
            void * t49 = gm_l4;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t49) + 98));
            if (!t50) goto ie48;
            {
                gm_tu15p0hi2qlv6 t52;
                gm_f_zero_9r6o7sj((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&t52 + 0)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memcpy((uint8_t *)&t52 + 96, ((uint8_t *)&F.l5), 96);
                gm_t0ialelq3ql1k t53;
                GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tu15p0hi2qlv6, ((uint8_t *)&t53 + 8), t52);
                gm_t5vbkdq81qfs7 t54;
                GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t54 + 8), t53);
                gm_tkv1vap90vht2 t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t55), t54);
                ((uint8_t *)&t55)[208] = 1;
                uint8_t t56 = 0;
                F.ret = t55;
                goto gm_fret;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            gm_tngtuiuskn938 t57 = F.l5;
            gm_t0ialelq3ql1k t58;
            memset(&t58, 0, sizeof t58);
            GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t58 + 8), t57);
            gm_t5vbkdq81qfs7 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t59 + 8), t58);
            gm_tkv1vap90vht2 t60;
            GM_ST(uint8_t, (uint8_t *)&t60 + 209, 0);
            GM_ST(uint16_t, (uint8_t *)&t60 + 210, 0);
            GM_ST(uint32_t, (uint8_t *)&t60 + 212, 0);
            GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t60), t59);
            ((uint8_t *)&t60)[208] = 1;
            uint8_t t61 = 0;
            F.ret = t60;
            goto gm_fret;
        }
        goto me3;
mn33:;
ma6:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)9ULL)) goto mn62;
        void * t64 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t64;
        void * t65 = gm_l0;
        uint64_t t66 = gm_l1;
        void * t67 = gm_l6;
        gm_f_try_untyped_fj0h8i5((void *)&(*(gm_tkv1vap90vht2 *)gm_out), (void *)NULL, cx, NULL, t65, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn62:;
ma7:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)6ULL)) goto mn68;
        gm_t08olq97rscn3 t70 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t70;
        {
            void * t73 = gm_l0;
            gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tk1gen3hgkm51 t75;
            gm_f_local_const_idpokjh((void *)&t75, t73, t74);
            gm_tk1gen3hgkm51 t76 = t75;
            if (!((((uint8_t *)&t76))[224] != 0)) goto ie72;
            gm_tb07pp6njg0bk t77 = GM_LD(gm_tb07pp6njg0bk, ((uint8_t *)&t76));
            F.l8 = t77;
            {
                uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 16) + 0));
                if (t80 != ((uint8_t)0ULL)) goto ie79;
                gm_t0ialelq3ql1k t81 = GM_LD(gm_t0ialelq3ql1k, ((((uint8_t *)&F.l8) + 16) + 8));
                F.l9 = t81;
                {
                    gm_t0ialelq3ql1k t83 = F.l9;
                    gm_t5vbkdq81qfs7 t84;
                    GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t84 + 8), t83);
                    gm_tkv1vap90vht2 t85;
                    GM_ST(uint8_t, (uint8_t *)&t85 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t85 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t85 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t85), t84);
                    ((uint8_t *)&t85)[208] = 1;
                    uint8_t t86 = 0;
                    F.ret = t85;
                    goto gm_fret;
                }
                goto ix79;
ie79:;
                {
                    gm_tkv1vap90vht2 t88;
                    memset(&t88, 0, sizeof t88);
                    uint8_t t89 = 0;
                    F.ret = t88;
                    goto gm_fret;
                }
ix79:;
            }
            goto ix72;
ie72:;
ix72:;
        }
        {
            void * t90 = gm_l0;
            uint64_t t91 = gm_l1;
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 0));
            gm_tf5srh4mp4b3i t93 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t94 = t93;
            uint64_t t95;
            if (((((uint8_t *)&t94))[8] != 0)) {
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                t95 = t96;
            } else {
                gm_tkv1vap90vht2 t97;
                memset(&t97, 0, sizeof t97);
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
            }
            gm_l10 = t95;
        }
        {
            void * t100 = gm_l0;
            uint64_t t101 = gm_l10;
            gm_t6qooom8qa2do t102;
            gm_f_resolve_decl_pus042v((void *)&t102, (void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t103 = t102;
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&t103) + 0));
            if (t104 != ((uint8_t)8ULL)) goto ie99;
            gm_t0ialelq3ql1k t105 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t103) + 8));
            F.l11 = t105;
            {
                gm_t0ialelq3ql1k t107 = F.l11;
                gm_t5vbkdq81qfs7 t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t108 + 8), t107);
                gm_tkv1vap90vht2 t109;
                GM_ST(uint8_t, (uint8_t *)&t109 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t109 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t109 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t109), t108);
                ((uint8_t *)&t109)[208] = 1;
                uint8_t t110 = 0;
                F.ret = t109;
                goto gm_fret;
            }
            goto ix99;
ie99:;
            {
                gm_tkv1vap90vht2 t112;
                memset(&t112, 0, sizeof t112);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
ix99:;
        }
        goto me3;
mn68:;
ma8:;
        uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t115 != ((uint8_t)22ULL)) goto mn114;
        void * t116 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l12 = t116;
        gm_t08olq97rscn3 t117 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l13 = t117;
        {
            void * t119 = gm_l0;
            uint64_t t120 = gm_l1;
            void * t121 = gm_l12;
            gm_tf5srh4mp4b3i t122 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t119, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t123 = t122;
            uint64_t t124;
            if (((((uint8_t *)&t123))[8] != 0)) {
                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&t123) + 0));
                t124 = t125;
            } else {
                gm_tkv1vap90vht2 t126;
                memset(&t126, 0, sizeof t126);
                uint8_t t127 = 0;
                F.ret = t126;
                goto gm_fret;
            }
            gm_l14 = t124;
        }
        {
            void * t128 = gm_l0;
            void * t129 = GM_LD(void *, (((uint8_t *)t128) + 0));
            uint64_t t130 = gm_l14;
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 0));
            gm_tf5srh4mp4b3i t132 = gm_f_lookup_59juqoo(t129, t130, t131);
            gm_tf5srh4mp4b3i t133 = t132;
            uint64_t t134;
            if (((((uint8_t *)&t133))[8] != 0)) {
                uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&t133) + 0));
                t134 = t135;
            } else {
                gm_tkv1vap90vht2 t136;
                memset(&t136, 0, sizeof t136);
                uint8_t t137 = 0;
                F.ret = t136;
                goto gm_fret;
            }
            gm_l15 = t134;
        }
        {
            void * t139 = gm_l0;
            uint64_t t140 = gm_l15;
            uint64_t t141 = gm_l1;
            uint8_t t142 = gm_f_decl_visible_bd168ti(t139, t140, t141);
            uint8_t t143 = (uint8_t)!t142;
            if (!t143) goto ie138;
            {
                gm_tkv1vap90vht2 t145;
                memset(&t145, 0, sizeof t145);
                uint8_t t146 = 0;
                F.ret = t145;
                goto gm_fret;
            }
            goto ix138;
ie138:;
ix138:;
        }
        {
            void * t147 = gm_l0;
            uint64_t t148 = gm_l15;
            uint64_t t149 = gm_l1;
            gm_b0_1 t150 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t147, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t152 = gm_l0;
            uint64_t t153 = gm_l15;
            gm_t6qooom8qa2do t154;
            gm_f_resolve_decl_pus042v((void *)&t154, (void *)NULL, cx, NULL, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6qooom8qa2do t155 = t154;
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t155) + 0));
            if (t156 != ((uint8_t)8ULL)) goto ie151;
            gm_t0ialelq3ql1k t157 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t155) + 8));
            F.l16 = t157;
            {
                gm_t0ialelq3ql1k t159 = F.l16;
                gm_t5vbkdq81qfs7 t160;
                GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t160 + 8), t159);
                gm_tkv1vap90vht2 t161;
                GM_ST(uint8_t, (uint8_t *)&t161 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t161 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t161 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t161), t160);
                ((uint8_t *)&t161)[208] = 1;
                uint8_t t162 = 0;
                F.ret = t161;
                goto gm_fret;
            }
            goto ix151;
ie151:;
            {
                gm_tkv1vap90vht2 t164;
                memset(&t164, 0, sizeof t164);
                uint8_t t165 = 0;
                F.ret = t164;
                goto gm_fret;
            }
ix151:;
        }
        goto me3;
mn114:;
ma9:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t167 != ((uint8_t)12ULL)) goto mn166;
        uint8_t t168 = GM_LD(uint8_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
        if (t168 != ((uint8_t)0ULL)) goto mn166;
        void * t169 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l17 = t169;
        {
            void * t171 = gm_l0;
            uint64_t t172 = gm_l1;
            void * t173 = gm_l17;
            gm_tkv1vap90vht2 t174;
            gm_f_try_untyped_fj0h8i5((void *)&t174, (void *)NULL, cx, NULL, t171, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t175 = t174;
            gm_t5vbkdq81qfs7 t176;
            if (((((uint8_t *)&t175))[208] != 0)) {
                gm_t5vbkdq81qfs7 t177 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t175) + 0));
                t176 = t177;
            } else {
                gm_tkv1vap90vht2 t178;
                memset(&t178, 0, sizeof t178);
                uint8_t t179 = 0;
                F.ret = t178;
                goto gm_fret;
            }
            F.l18 = t176;
        }
        {
            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
            if (t181 != ((uint8_t)0ULL)) goto ie180;
            gm_t0ialelq3ql1k t182 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l18) + 8));
            F.l19 = t182;
            {
                void * t184 = (void *)((uint8_t *)&F.l19);
                gm_t4t568o7qnv02 t185;
                gm_f_negate_fohael7((void *)&t185, (void *)NULL, cx, NULL, t184);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4t568o7qnv02 t186 = t185;
                uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&t186) + 0));
                if (t189 != ((uint8_t)0ULL)) goto mn188;
                gm_t0ialelq3ql1k t190 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t186) + 8));
                F.l20 = t190;
                gm_t0ialelq3ql1k t191 = F.l20;
                gm_t5vbkdq81qfs7 t192;
                GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t192 + 8), t191);
                gm_tkv1vap90vht2 t193;
                GM_ST(uint8_t, (uint8_t *)&t193 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t193 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t193 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t193), t192);
                ((uint8_t *)&t193)[208] = 1;
                uint8_t t194 = 0;
                F.ret = t193;
                goto gm_fret;
                goto me187;
mn188:;
                uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&t186) + 0));
                if (t196 != ((uint8_t)1ULL)) goto mn195;
                gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 8));
                gm_l21 = t197;
                {
                    void * t200 = gm_l0;
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)t200) + 2536));
                    uint8_t t202 = (uint8_t)!t201;
                    if (!t202) goto ie199;
                    {
                        void * t204 = gm_l0;
                        void * t205 = gm_l2;
                        gm_tl874t2atnlh3 t206 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t205) + 72));
                        gm_b16_8 t207 = gm_l21;
                        gm_b0_1 t208 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t204, t206, t207);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix199;
ie199:;
ix199:;
                }
                {
                    gm_t5vbkdq81qfs7 t209;
                    memset(&t209, 0, sizeof t209);
                    GM_ST(uint8_t, ((uint8_t *)&t209 + 0), ((uint8_t)1ULL));
                    gm_tkv1vap90vht2 t210;
                    GM_ST(uint8_t, (uint8_t *)&t210 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t210 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t210 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t210), t209);
                    ((uint8_t *)&t210)[208] = 1;
                    uint8_t t211 = 0;
                    F.ret = t210;
                    goto gm_fret;
                }
                goto me187;
mn195:;
                gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 264, 13);
me187:;
            }
            goto ix180;
ie180:;
            {
                gm_t5vbkdq81qfs7 t213;
                memset(&t213, 0, sizeof t213);
                GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t214;
                GM_ST(uint8_t, (uint8_t *)&t214 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t214 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t214 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t214), t213);
                ((uint8_t *)&t214)[208] = 1;
                uint8_t t215 = 0;
                F.ret = t214;
                goto gm_fret;
            }
ix180:;
        }
        goto me3;
mn166:;
ma10:;
        uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t217 != ((uint8_t)13ULL)) goto mn216;
        gm_ts6oqppjh0ba3 t218 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l22 = t218;
        void * t219 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l23 = t219;
        void * t220 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l24 = t220;
        {
            gm_ts6oqppjh0ba3 t223 = gm_l22;
            gm_ts6oqppjh0ba3 t224;
            GM_ST(uint8_t, (uint8_t *)&t224 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t224 + 0), ((uint8_t)20ULL));
            gm_ts6oqppjh0ba3 t225;
            GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)21ULL));
            gm_ts6oqppjh0ba3 t226;
            GM_ST(uint8_t, (uint8_t *)&t226 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)14ULL));
            gm_ts6oqppjh0ba3 t227;
            GM_ST(uint8_t, (uint8_t *)&t227 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)15ULL));
            gm_ts6oqppjh0ba3 t228;
            GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t229;
            GM_ST(uint8_t, (uint8_t *)&t229 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)11ULL));
            gm_tvqq0vnt1mu0q t230;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 0), t224);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 2), t225);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 4), t226);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 6), t227);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 8), t228);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t230 + 10), t229);
            gm_tvqq0vnt1mu0q t231 = t230;
            uint8_t t232 = 0;
            gm_ts6oqppjh0ba3 t233 = t223;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 0, ((uint8_t *)&t233))) t232 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 2, ((uint8_t *)&t233))) t232 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 4, ((uint8_t *)&t233))) t232 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 6, ((uint8_t *)&t233))) t232 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 8, ((uint8_t *)&t233))) t232 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t231) + 10, ((uint8_t *)&t233))) t232 = 1;
            uint8_t t234 = (uint8_t)(t232);
            if (!t234) goto ie222;
            {
                gm_tkv1vap90vht2 t236;
                memset(&t236, 0, sizeof t236);
                uint8_t t237 = 0;
                F.ret = t236;
                goto gm_fret;
            }
            goto ix222;
ie222:;
ix222:;
        }
        {
            uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t242 != ((uint8_t)0ULL)) goto pa241;
            gm_t9qkh0rsooc8d t243 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t243;
            goto po240;
pa241:;
            uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t245 != ((uint8_t)1ULL)) goto pa244;
            gm_t9qkh0rsooc8d t246 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t246;
            goto po240;
pa244:;
            uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t248 != ((uint8_t)2ULL)) goto pa247;
            gm_t9qkh0rsooc8d t249 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t249;
            goto po240;
pa247:;
            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            if (t251 != ((uint8_t)3ULL)) goto pa250;
            gm_t9qkh0rsooc8d t252 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l22) + 1));
            gm_l25 = t252;
            goto po240;
pa250:;
            goto mn239;
po240:;
            uint8_t t254 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l25) + 0) == ((uint8_t)0ULL)));
            if (!t254) goto ie253;
            {
                gm_tkv1vap90vht2 t256;
                memset(&t256, 0, sizeof t256);
                uint8_t t257 = 0;
                F.ret = t256;
                goto gm_fret;
            }
            goto ix253;
ie253:;
ix253:;
            goto me238;
mn239:;
            goto me238;
mn258:;
            gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 275, 13);
me238:;
        }
        {
            void * t260 = gm_l0;
            uint64_t t261 = gm_l1;
            void * t262 = gm_l23;
            gm_tkv1vap90vht2 t263;
            gm_f_try_untyped_fj0h8i5((void *)&t263, (void *)NULL, cx, NULL, t260, t261, t262);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t264 = t263;
            gm_t5vbkdq81qfs7 t265;
            if (((((uint8_t *)&t264))[208] != 0)) {
                gm_t5vbkdq81qfs7 t266 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t264) + 0));
                t265 = t266;
            } else {
                gm_tkv1vap90vht2 t267;
                memset(&t267, 0, sizeof t267);
                uint8_t t268 = 0;
                F.ret = t267;
                goto gm_fret;
            }
            F.l29 = t265;
        }
        {
            void * t269 = gm_l0;
            uint64_t t270 = gm_l1;
            void * t271 = gm_l24;
            gm_tkv1vap90vht2 t272;
            gm_f_try_untyped_fj0h8i5((void *)&t272, (void *)NULL, cx, NULL, t269, t270, t271);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkv1vap90vht2 t273 = t272;
            gm_t5vbkdq81qfs7 t274;
            if (((((uint8_t *)&t273))[208] != 0)) {
                gm_t5vbkdq81qfs7 t275 = GM_LD(gm_t5vbkdq81qfs7, (((uint8_t *)&t273) + 0));
                t274 = t275;
            } else {
                gm_tkv1vap90vht2 t276;
                memset(&t276, 0, sizeof t276);
                uint8_t t277 = 0;
                F.ret = t276;
                goto gm_fret;
            }
            F.l30 = t274;
        }
        {
            uint8_t t279 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 0));
            if (t279 != ((uint8_t)0ULL)) goto ie278;
            gm_t0ialelq3ql1k t280 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l29) + 8));
            F.l31 = t280;
            {
                uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&F.l30) + 0));
                if (t283 != ((uint8_t)0ULL)) goto ie282;
                gm_t0ialelq3ql1k t284 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l30) + 8));
                F.l32 = t284;
                {
                    gm_ts6oqppjh0ba3 t286 = gm_l22;
                    void * t287 = (void *)((uint8_t *)&F.l31);
                    void * t288 = (void *)((uint8_t *)&F.l32);
                    gm_t4t568o7qnv02 t289;
                    gm_f_binary_9sb7899((void *)&t289, (void *)NULL, cx, NULL, t286, t287, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t4t568o7qnv02 t290 = t289;
                    uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&t290) + 0));
                    if (t293 != ((uint8_t)0ULL)) goto mn292;
                    gm_t0ialelq3ql1k t294 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&t290) + 8));
                    F.l33 = t294;
                    gm_t0ialelq3ql1k t295 = F.l33;
                    gm_t5vbkdq81qfs7 t296;
                    GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t296 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&t296 + 8), t295);
                    gm_tkv1vap90vht2 t297;
                    GM_ST(uint8_t, (uint8_t *)&t297 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t297 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t297 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t297), t296);
                    ((uint8_t *)&t297)[208] = 1;
                    uint8_t t298 = 0;
                    F.ret = t297;
                    goto gm_fret;
                    goto me291;
mn292:;
                    uint8_t t300 = GM_LD(uint8_t, (((uint8_t *)&t290) + 0));
                    if (t300 != ((uint8_t)1ULL)) goto mn299;
                    gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&t290) + 8));
                    gm_l34 = t301;
                    {
                        void * t304 = gm_l0;
                        uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)t304) + 2536));
                        uint8_t t306 = (uint8_t)!t305;
                        if (!t306) goto ie303;
                        {
                            void * t308 = gm_l0;
                            void * t309 = gm_l2;
                            gm_tl874t2atnlh3 t310 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t309) + 72));
                            gm_b16_8 t311 = gm_l34;
                            gm_b0_1 t312 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t308, t310, t311);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto ix303;
ie303:;
ix303:;
                    }
                    {
                        gm_t5vbkdq81qfs7 t313;
                        memset(&t313, 0, sizeof t313);
                        GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)1ULL));
                        gm_tkv1vap90vht2 t314;
                        GM_ST(uint8_t, (uint8_t *)&t314 + 209, 0);
                        GM_ST(uint16_t, (uint8_t *)&t314 + 210, 0);
                        GM_ST(uint32_t, (uint8_t *)&t314 + 212, 0);
                        GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t314), t313);
                        ((uint8_t *)&t314)[208] = 1;
                        uint8_t t315 = 0;
                        F.ret = t314;
                        goto gm_fret;
                    }
                    goto me291;
mn299:;
                    gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 283, 13);
me291:;
                }
                goto ix282;
ie282:;
                {
                    gm_t5vbkdq81qfs7 t317;
                    memset(&t317, 0, sizeof t317);
                    GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)1ULL));
                    gm_tkv1vap90vht2 t318;
                    GM_ST(uint8_t, (uint8_t *)&t318 + 209, 0);
                    GM_ST(uint16_t, (uint8_t *)&t318 + 210, 0);
                    GM_ST(uint32_t, (uint8_t *)&t318 + 212, 0);
                    GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t318), t317);
                    ((uint8_t *)&t318)[208] = 1;
                    uint8_t t319 = 0;
                    F.ret = t318;
                    goto gm_fret;
                }
ix282:;
            }
            goto ix278;
ie278:;
            {
                gm_t5vbkdq81qfs7 t321;
                memset(&t321, 0, sizeof t321);
                GM_ST(uint8_t, ((uint8_t *)&t321 + 0), ((uint8_t)1ULL));
                gm_tkv1vap90vht2 t322;
                GM_ST(uint8_t, (uint8_t *)&t322 + 209, 0);
                GM_ST(uint16_t, (uint8_t *)&t322 + 210, 0);
                GM_ST(uint32_t, (uint8_t *)&t322 + 212, 0);
                GM_ST(gm_t5vbkdq81qfs7, ((uint8_t *)&t322), t321);
                ((uint8_t *)&t322)[208] = 1;
                uint8_t t323 = 0;
                F.ret = t322;
                goto gm_fret;
            }
ix278:;
        }
        goto me3;
mn216:;
ma11:;
        gm_tkv1vap90vht2 t325;
        memset(&t325, 0, sizeof t325);
        uint8_t t326 = 0;
        F.ret = t325;
        goto gm_fret;
        goto me3;
mn324:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src7h8dni243k2v, 231, 5);
me3:;
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
