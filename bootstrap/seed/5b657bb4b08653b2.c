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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_bool_ajhjgeg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lane_error_72hoonn {
    gm_frame h;
    void * ret;
};
struct gm_fr_lane_needs_a6gcpil {
    gm_frame h;
    void * ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mask_ty_cj7ln0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
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
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_binary_bicu7u0 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_construction_fhm2mm5 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    void * ret;
};
struct gm_fr_vector_conversion_32a9cuf {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_load_ssdvomk {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_method_hgn4sb1 {
    gm_frame h;
    gm_tc3oht4ksth9d l16; /* xe */
    void * ret;
};
struct gm_fr_vector_shuffle_5ime7a7 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* idx */
    gm_t7te2tdpebbt4 l12; /* a */
    gm_tc3oht4ksth9d l13; /* e */
    gm_tbci5ospk5nnn l17; /* i */
    gm_tbci5ospk5nnn l23; /* i */
    gm_trqfvmjtt4sij l26; /* all */
    gm_t69m56meqefs8 l29; /* i */
    void * ret;
};
struct gm_fr_vector_unary_2mbe97t {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8eujoej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gsgjkse(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jm4dd7q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_lane_type_ok_rp8uq1p(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vector_shape_error_3invhhe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1t08grtrn18m[9];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s6lddausbiitu[9];
extern const uint8_t gm_s8guqcjcce9oc[22];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_scqrlin5l437v[9];
extern const uint8_t gm_sf1qp7r4ktsd1[11];
extern const uint8_t gm_sfmov9pb6t5lt[14];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_sioam4mlrjuar[52];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_skdalh0o0hret[59];
extern const uint8_t gm_skftljk77e6mu[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slu3lsfv09uua[25];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_smgfld86ipv3f[58];
extern const uint8_t gm_snc5ns3akncuq[6];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7u9cfdjh7s0n[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st4t0ufodvddp[59];
extern const uint8_t gm_sut92jpq3kk88[11];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_svgoe41piv341[7];
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
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bool_ajhjgeg F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)4ULL)));
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)26ULL)));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t8 != ((uint8_t)3ULL)) goto mn7;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
me6:;
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_tb7u19loo3j87 t3;
    gm_f_vector_parts_crrfopm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_tb7u19loo3j87 t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[16] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
me6:;
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_mask_ty_cj7ln0g F;
    void * gm_l0; /* tys */
    uint64_t gm_l1; /* len */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t1, t2, t4);
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 176u);
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
    gm_tq7s98ihonkju t5 = t4;
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_error_72hoonn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* opn */
    gm_b16_8 gm_l2; /* what */
    gm_t88rcucg1b7uk gm_l3; /* vt */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 333u);
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
        gm_tl874t2atnlh3 t3 = gm_l4;
        gm_tfeab279c16rr t4;
        gm_b16_8 t5 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l3;
        gm_b16_8 t9 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t9);
        gm_tabb710d9mq2e t10;
        gm_f_format_ima6g5u((void *)&t10, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src7u9cfdjh7s0n, 124, 20);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_b0_1 t15 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t16 = gm_l0;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
        gm_tl874t2atnlh3 t18 = gm_l4;
        void * t19 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = 0;
        F.ret = t19;
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
inline __attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_needs_a6gcpil F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* what */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* lane */
    GM_STACK_CHECK(&F, 493u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_tb7u19loo3j87 t6;
        gm_f_vector_parts_crrfopm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_tb7u19loo3j87 t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) gm_trap("unwrap of null", gm_src7u9cfdjh7s0n, 118, 13);
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t7));
        gm_tlcm4lvu3g1a8 t9 = t8;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t9) + 8));
        gm_l5 = t10;
    }
    {
        void * t11 = gm_l0;
        gm_tl874t2atnlh3 t12 = gm_l4;
        gm_t0uuhaljsf410 t13;
        gm_b16_8 t14 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 0, t14);
        gm_b16_8 t15 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 16, t15);
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 32, t18);
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l5;
        gm_b16_8 t21 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 48, t21);
        gm_tabb710d9mq2e t22;
        gm_f_format_0gvqkke((void *)&t22, (void *)NULL, cx, NULL, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_src7u9cfdjh7s0n, 119, 20);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_b16_8 t25 = t24;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        gm_b0_1 t27 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t11, t12, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        gm_tl874t2atnlh3 t30 = gm_l4;
        void * t31 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t29, t30);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_binary_bicu7u0 F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* vt */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    gm_t88rcucg1b7uk gm_l8; /* lane */
    gm_b16_8 gm_l9; /* opn */
    void * gm_l10; /* r */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_ts6oqppjh0ba3 gm_l13; /* top */
    void * gm_l14; /* r */
    gm_t88rcucg1b7uk gm_l15; /* rt */
    gm_b16_8 gm_l16; /* hint */
    uint8_t gm_l17; /* lane_int */
    uint8_t gm_l18; /* lane_float */
    uint8_t gm_l19; /* numeric */
    gm_b16_8 gm_l20; /* m */
    gm_t9qkh0rsooc8d gm_l21; /* f */
    gm_t9qkh0rsooc8d gm_l22; /* f */
    gm_t9qkh0rsooc8d gm_l23; /* f */
    gm_ts6oqppjh0ba3 gm_l24; /* top */
    gm_ts6oqppjh0ba3 gm_l25; /* top */
    GM_STACK_CHECK(&F, 3510u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = gm_l6;
        gm_tb7u19loo3j87 t14;
        gm_f_vector_parts_crrfopm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_tb7u19loo3j87 t15 = t14;
        gm_tlcm4lvu3g1a8 t16;
        if (((((uint8_t *)&t15))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t17 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l4;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l7 = t16;
    }
    {
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l8 = t23;
    }
    {
        gm_ts6oqppjh0ba3 t24 = gm_l1;
        gm_b16_8 t25 = gm_f_binop_text_0ljfc0r(t24);
        gm_l9 = t25;
    }
    {
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma29; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma29; case ((uint8_t)7ULL): goto ma30; case ((uint8_t)8ULL): goto ma30; case ((uint8_t)9ULL): goto ma30; case ((uint8_t)10ULL): goto ma28; case ((uint8_t)11ULL): goto ma28; case ((uint8_t)12ULL): goto ma30; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma28; case ((uint8_t)15ULL): goto ma28; case ((uint8_t)16ULL): goto ma30; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma27; default: goto ma30; }
ma27:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t36 != ((uint8_t)20ULL)) goto pa35;
        goto po34;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t38 != ((uint8_t)21ULL)) goto pa37;
        goto po34;
pa37:;
        goto mn33;
po34:;
        {
            {
                void * t41 = gm_l0;
                void * t42 = gm_l3;
                gm_tkn42u7na3t81 t43;
                memset(&t43, 0, sizeof t43);
                void * t44 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t41, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t45 = gm_l0;
            gm_tl874t2atnlh3 t46 = gm_l4;
            gm_b16_8 t47 = gm_l9;
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_tabb710d9mq2e t49;
            gm_f_format_frnomph((void *)&t49, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_src7u9cfdjh7s0n, 37, 28);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
            gm_b16_8 t52 = t51;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_b0_1 t54 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t45, t46, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t55 = gm_l0;
            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
            gm_tl874t2atnlh3 t57 = gm_l4;
            void * t58 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = 0;
            F.ret = t58;
            goto gm_fret;
        }
        goto me26;
mn33:;
ma28:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t63 != ((uint8_t)14ULL)) goto pa62;
        goto po61;
pa62:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t65 != ((uint8_t)15ULL)) goto pa64;
        goto po61;
pa64:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t67 != ((uint8_t)10ULL)) goto pa66;
        goto po61;
pa66:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t69 != ((uint8_t)11ULL)) goto pa68;
        goto po61;
pa68:;
        goto mn60;
po61:;
        {
            {
                void * t72 = gm_l0;
                void * t73 = gm_l3;
                gm_tkn42u7na3t81 t74;
                memset(&t74, 0, sizeof t74);
                void * t75 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t72, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t76 = gm_l0;
            gm_tl874t2atnlh3 t77 = gm_l4;
            gm_b16_8 t78 = gm_l9;
            gm_tnpifv20irnph t79;
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
            gm_tabb710d9mq2e t80;
            gm_f_format_7r64gap((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_src7u9cfdjh7s0n, 42, 28);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_b16_8 t83 = t82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_b0_1 t85 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t76, t77, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t86 = gm_l0;
            void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
            gm_tl874t2atnlh3 t88 = gm_l4;
            void * t89 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto me26;
mn60:;
ma29:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t94 != ((uint8_t)3ULL)) goto pa93;
        goto po92;
pa93:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t96 != ((uint8_t)6ULL)) goto pa95;
        goto po92;
pa95:;
        goto mn91;
po92:;
        {
            void * t98 = gm_l0;
            void * t99 = gm_l3;
            gm_tkn42u7na3t81 t100;
            memset(&t100, 0, sizeof t100);
            void * t101 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t101;
        }
        {
            void * t103 = gm_l5;
            gm_t88rcucg1b7uk t104 = gm_l8;
            uint8_t t105 = gm_f_is_integer_8d4f9ek(t103, GM_CONV(gm_csguankko4lmhd, t104));
            uint8_t t106 = (uint8_t)!t105;
            if (!t106) goto ie102;
            {
                void * t108 = gm_l0;
                gm_tl874t2atnlh3 t109 = gm_l4;
                gm_t6j6smj7qoctu t110;
                gm_b16_8 t111 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t111);
                void * t112 = gm_l0;
                gm_t88rcucg1b7uk t113 = gm_l6;
                gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t114);
                gm_tabb710d9mq2e t115;
                gm_f_format_q2uenf3((void *)&t115, (void *)NULL, cx, NULL, &t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t116 = t115;
                if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 8, gm_src7u9cfdjh7s0n, 48, 32);
                gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&t116) + 8));
                gm_b16_8 t118 = t117;
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                gm_b0_1 t120 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t108, t109, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t121 = gm_l0;
                void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
                gm_tl874t2atnlh3 t123 = gm_l4;
                void * t124 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t122, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t125 = 0;
                F.ret = t124;
                goto gm_fret;
            }
            goto ix102;
ie102:;
ix102:;
        }
        {
            gm_ts6oqppjh0ba3 t127 = gm_l1;
            gm_ts6oqppjh0ba3 t128 = t127;
            gm_t9qkh0rsooc8d t129;
            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)3ULL));
            gm_t9qkh0rsooc8d t130 = t129;
            uint8_t t131 = (((GM_LD(uint8_t, ((uint8_t *)&t128) + 0) == ((uint8_t)3ULL)) && gm_eqehhdtpeti1((((uint8_t *)&t128) + 1), ((uint8_t *)&t130))));
            if (!t131) goto ie126;
            {
                void * t133 = gm_l0;
                gm_tl874t2atnlh3 t134 = gm_l4;
                gm_b16_8 t135;
                GM_ST(void *, (uint8_t *)&t135, (void *)(gm_st4t0ufodvddp));
                GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(58));
                gm_b0_1 t136 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t133, t134, t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t137 = gm_l0;
                void * t138 = GM_LD(void *, (((uint8_t *)t137) + 0));
                gm_tl874t2atnlh3 t139 = gm_l4;
                void * t140 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t138, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t141 = 0;
                F.ret = t140;
                goto gm_fret;
            }
            goto ix126;
ie126:;
ix126:;
        }
        {
            void * t143 = gm_l0;
            void * t144 = (void *)(((uint8_t *)t143) + 96);
            void * t145 = gm_l5;
            void * t146 = gm_l10;
            gm_t88rcucg1b7uk t147 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t146) + 0));
            gm_t9qkh0rsooc8d t148;
            GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)1ULL));
            uint8_t t149 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t144, t145, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t150 = (uint8_t)!t149;
            if (!t150) goto ie142;
            void * t151 = gm_l5;
            void * t152 = gm_l10;
            gm_t88rcucg1b7uk t153 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t152) + 0));
            uint8_t t154 = gm_f_is_error_8k27gd1(t151, GM_CONV(gm_csguankko4lmhd, t153));
            uint8_t t155 = (uint8_t)!t154;
            if (!t155) goto ie142;
            {
                void * t157 = gm_l0;
                void * t158 = (void *)(((uint8_t *)t157) + 96);
                void * t159 = gm_l0;
                void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
                void * t161 = gm_l10;
                gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 0));
                gm_t88rcucg1b7uk t163 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t158, t160, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t163;
            }
            {
                void * t164 = gm_l0;
                void * t165 = gm_l10;
                gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 72));
                void * t167 = gm_l0;
                gm_t88rcucg1b7uk t168 = gm_l11;
                gm_b16_8 t169 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t167, t168);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t170;
                GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t169);
                gm_tabb710d9mq2e t171;
                gm_f_format_pdashts((void *)&t171, (void *)NULL, cx, NULL, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t172 = t171;
                if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 8, gm_src7u9cfdjh7s0n, 57, 34);
                gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&t172) + 8));
                gm_b16_8 t174 = t173;
                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                gm_b0_1 t176 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t164, t166, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix142;
ie142:;
ix142:;
        }
        {
            gm_ts6oqppjh0ba3 t177;
            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t179 != ((uint8_t)3ULL)) goto ie178;
            gm_t9qkh0rsooc8d t180 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l12 = t180;
            gm_t9qkh0rsooc8d t182 = gm_l12;
            gm_ts6oqppjh0ba3 t183;
            GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t183 + 1), t182);
            gm_ts6oqppjh0ba3 t184 = t183;
            t177 = t184;
            goto ix178;
ie178:;
            gm_ts6oqppjh0ba3 t186;
            GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)6ULL));
            gm_ts6oqppjh0ba3 t187 = t186;
            t177 = t187;
ix178:;
            gm_l13 = t177;
        }
        {
            gm_t88rcucg1b7uk t188 = gm_l6;
            gm_trl9d7m1k5lbd t189;
            memset((uint8_t *)&t189 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t190 = gm_l13;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t189 + 0, t190);
            void * t191 = gm_l2;
            GM_ST(void *, (uint8_t *)&t189 + 8, t191);
            void * t192 = gm_l10;
            GM_ST(void *, (uint8_t *)&t189 + 16, t192);
            gm_teti411eujvf4 t193;
            memset(&t193, 0, sizeof t193);
            GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t193 + 8), t189);
            gm_tl874t2atnlh3 t194 = gm_l4;
            void * t195 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t188, &t193, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t196 = 0;
            F.ret = t195;
            goto gm_fret;
        }
        goto me26;
mn91:;
ma30:;
        goto me26;
mn197:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 34, 5);
me26:;
    }
    {
        void * t199 = gm_l0;
        void * t200 = gm_l3;
        gm_t88rcucg1b7uk t201 = gm_l6;
        gm_tkn42u7na3t81 t202;
        GM_ST(uint8_t, (uint8_t *)&t202 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t202 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t202), t201);
        ((uint8_t *)&t202)[4] = 1;
        void * t203 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t199, t200, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t203;
    }
    {
        void * t204 = gm_l0;
        void * t205 = (void *)(((uint8_t *)t204) + 96);
        void * t206 = gm_l0;
        void * t207 = GM_LD(void *, (((uint8_t *)t206) + 0));
        void * t208 = gm_l14;
        gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t208) + 0));
        gm_t88rcucg1b7uk t210 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t205, t207, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t210;
    }
    {
        void * t212 = gm_l5;
        gm_t88rcucg1b7uk t213 = gm_l15;
        uint8_t t214 = gm_f_is_error_8k27gd1(t212, GM_CONV(gm_csguankko4lmhd, t213));
        if (!t214) goto ie211;
        {
            void * t216 = gm_l0;
            void * t217 = GM_LD(void *, (((uint8_t *)t216) + 0));
            gm_tl874t2atnlh3 t218 = gm_l4;
            void * t219 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t217, t218);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t220 = 0;
            F.ret = t219;
            goto gm_fret;
        }
        goto ix211;
ie211:;
ix211:;
    }
    {
        gm_t88rcucg1b7uk t222 = gm_l15;
        gm_t88rcucg1b7uk t223 = gm_l6;
        gm_t88rcucg1b7uk t224 = t222;
        gm_t88rcucg1b7uk t225 = t223;
        uint8_t t226 = (!gm_eq9m6mvtmgde(((uint8_t *)&t224), ((uint8_t *)&t225)));
        if (!t226) goto ie221;
        {
            gm_b16_8 t228;
            void * t230 = gm_l5;
            gm_t88rcucg1b7uk t231 = gm_l15;
            uint8_t t232 = gm_f_is_vector_gkq6b88(t230, GM_CONV(gm_csguankko4lmhd, t231));
            if (!t232) goto ie229;
            gm_b16_8 t234;
            GM_ST(void *, (uint8_t *)&t234, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(0));
            gm_b16_8 t235 = t234;
            t228 = t235;
            goto ix229;
ie229:;
            gm_b16_8 t237;
            GM_ST(void *, (uint8_t *)&t237, (void *)(gm_smgfld86ipv3f));
            GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(57));
            gm_b16_8 t238 = t237;
            t228 = t238;
ix229:;
            gm_l16 = t228;
        }
        {
            void * t239 = gm_l0;
            gm_tl874t2atnlh3 t240 = gm_l4;
            gm_t0uuhaljsf410 t241;
            gm_b16_8 t242 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 0, t242);
            void * t243 = gm_l0;
            gm_t88rcucg1b7uk t244 = gm_l6;
            gm_b16_8 t245 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t243, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 16, t245);
            void * t246 = gm_l0;
            gm_t88rcucg1b7uk t247 = gm_l15;
            gm_b16_8 t248 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t246, t247);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 32, t248);
            gm_b16_8 t249 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t241 + 48, t249);
            gm_tabb710d9mq2e t250;
            gm_f_format_pepv3ej((void *)&t250, (void *)NULL, cx, NULL, &t241);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t251 = t250;
            if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 8, gm_src7u9cfdjh7s0n, 69, 24);
            gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&t251) + 8));
            gm_b16_8 t253 = t252;
            gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
            gm_b0_1 t255 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t239, t240, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t256 = gm_l0;
            void * t257 = GM_LD(void *, (((uint8_t *)t256) + 0));
            gm_tl874t2atnlh3 t258 = gm_l4;
            void * t259 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t260 = 0;
            F.ret = t259;
            goto gm_fret;
        }
        goto ix221;
ie221:;
ix221:;
    }
    {
        void * t261 = gm_l5;
        gm_t88rcucg1b7uk t262 = gm_l8;
        uint8_t t263 = gm_f_is_integer_8d4f9ek(t261, GM_CONV(gm_csguankko4lmhd, t262));
        gm_l17 = t263;
    }
    {
        void * t264 = gm_l5;
        gm_t88rcucg1b7uk t265 = gm_l8;
        uint8_t t266 = gm_f_is_float_3jk89qu(t264, GM_CONV(gm_csguankko4lmhd, t265));
        gm_l18 = t266;
    }
    {
        uint8_t t267 = 0;
        uint8_t t268 = gm_l17;
        if (!t268) {
            uint8_t t269 = gm_l18;
            t267 = t269;
        } else t267 = 1;
        gm_l19 = t267;
    }
    {
        uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t277) { case ((uint8_t)0ULL): goto ma272; case ((uint8_t)1ULL): goto ma272; case ((uint8_t)2ULL): goto ma272; case ((uint8_t)3ULL): goto ma275; case ((uint8_t)4ULL): goto ma273; case ((uint8_t)5ULL): goto ma275; case ((uint8_t)6ULL): goto ma275; case ((uint8_t)7ULL): goto ma274; case ((uint8_t)8ULL): goto ma274; case ((uint8_t)9ULL): goto ma274; case ((uint8_t)10ULL): goto ma275; case ((uint8_t)11ULL): goto ma275; case ((uint8_t)12ULL): goto ma275; case ((uint8_t)13ULL): goto ma275; case ((uint8_t)14ULL): goto ma275; case ((uint8_t)15ULL): goto ma275; case ((uint8_t)16ULL): goto ma271; case ((uint8_t)17ULL): goto ma271; case ((uint8_t)18ULL): goto ma271; case ((uint8_t)19ULL): goto ma271; case ((uint8_t)20ULL): goto ma275; case ((uint8_t)21ULL): goto ma275; default: goto ma275; }
ma271:;
        uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t281 != ((uint8_t)16ULL)) goto pa280;
        goto po279;
pa280:;
        uint8_t t283 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t283 != ((uint8_t)17ULL)) goto pa282;
        goto po279;
pa282:;
        uint8_t t285 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t285 != ((uint8_t)18ULL)) goto pa284;
        goto po279;
pa284:;
        uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t287 != ((uint8_t)19ULL)) goto pa286;
        goto po279;
pa286:;
        goto mn278;
po279:;
        {
            gm_b16_8 t289;
            uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t296) { case ((uint8_t)0ULL): goto ma294; case ((uint8_t)1ULL): goto ma294; case ((uint8_t)2ULL): goto ma294; case ((uint8_t)3ULL): goto ma294; case ((uint8_t)4ULL): goto ma294; case ((uint8_t)5ULL): goto ma294; case ((uint8_t)6ULL): goto ma294; case ((uint8_t)7ULL): goto ma294; case ((uint8_t)8ULL): goto ma294; case ((uint8_t)9ULL): goto ma294; case ((uint8_t)10ULL): goto ma294; case ((uint8_t)11ULL): goto ma294; case ((uint8_t)12ULL): goto ma294; case ((uint8_t)13ULL): goto ma294; case ((uint8_t)14ULL): goto ma294; case ((uint8_t)15ULL): goto ma294; case ((uint8_t)16ULL): goto ma291; case ((uint8_t)17ULL): goto ma292; case ((uint8_t)18ULL): goto ma293; case ((uint8_t)19ULL): goto ma294; case ((uint8_t)20ULL): goto ma294; case ((uint8_t)21ULL): goto ma294; default: goto ma294; }
ma291:;
            uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t298 != ((uint8_t)16ULL)) goto mn297;
            gm_b16_8 t299;
            GM_ST(void *, (uint8_t *)&t299, (void *)(gm_sh1to1i0u67lu));
            GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(8));
            t289 = t299;
            goto me290;
mn297:;
ma292:;
            uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t301 != ((uint8_t)17ULL)) goto mn300;
            gm_b16_8 t302;
            GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sv8pauo1bhltl));
            GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(8));
            t289 = t302;
            goto me290;
mn300:;
ma293:;
            uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t304 != ((uint8_t)18ULL)) goto mn303;
            gm_b16_8 t305;
            GM_ST(void *, (uint8_t *)&t305, (void *)(gm_s1t08grtrn18m));
            GM_ST(uintptr_t, (uint8_t *)&t305 + 8, (uintptr_t)(8));
            t289 = t305;
            goto me290;
mn303:;
ma294:;
            gm_b16_8 t307;
            GM_ST(void *, (uint8_t *)&t307, (void *)(gm_scqrlin5l437v));
            GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(8));
            t289 = t307;
            goto me290;
mn306:;
ma295:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 77, 25);
me290:;
            gm_l20 = t289;
        }
        {
            void * t308 = gm_l0;
            gm_tl874t2atnlh3 t309 = gm_l4;
            gm_b16_8 t310 = gm_l20;
            gm_tnpifv20irnph t311;
            GM_ST(gm_b16_8, ((uint8_t *)&t311 + 0), t310);
            gm_tabb710d9mq2e t312;
            gm_f_format_l2fmok5((void *)&t312, (void *)NULL, cx, NULL, t311);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t313 = t312;
            if (((((uint8_t *)&t313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t313) + 8, gm_src7u9cfdjh7s0n, 83, 28);
            gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&t313) + 8));
            gm_b16_8 t315 = t314;
            gm_b16_8 t316 = GM_LD(gm_b16_8, ((uint8_t *)&t315));
            gm_b0_1 t317 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t308, t309, t316);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t318 = gm_l0;
            void * t319 = GM_LD(void *, (((uint8_t *)t318) + 0));
            gm_tl874t2atnlh3 t320 = gm_l4;
            void * t321 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t319, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t322 = 0;
            F.ret = t321;
            goto gm_fret;
        }
        goto me270;
mn278:;
ma272:;
        uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t326 != ((uint8_t)0ULL)) goto pa325;
        gm_t9qkh0rsooc8d t327 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t327;
        goto po324;
pa325:;
        uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t329 != ((uint8_t)1ULL)) goto pa328;
        gm_t9qkh0rsooc8d t330 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t330;
        goto po324;
pa328:;
        uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t332 != ((uint8_t)2ULL)) goto pa331;
        gm_t9qkh0rsooc8d t333 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l21 = t333;
        goto po324;
pa331:;
        goto mn323;
po324:;
        {
            uint8_t t336 = gm_l19;
            uint8_t t337 = (uint8_t)!t336;
            if (!t337) goto ie335;
            {
                void * t339 = gm_l0;
                gm_b16_8 t340 = gm_l9;
                gm_b16_8 t341;
                GM_ST(void *, (uint8_t *)&t341, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t341 + 8, (uintptr_t)(13));
                gm_t88rcucg1b7uk t342 = gm_l6;
                gm_tl874t2atnlh3 t343 = gm_l4;
                void * t344 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t339, t340, t341, t342, t343);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t345 = 0;
                F.ret = t344;
                goto gm_fret;
            }
            goto ix335;
ie335:;
ix335:;
        }
        {
            uint8_t t347 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l21) + 0) == ((uint8_t)3ULL)));
            if (!t347) goto ie346;
            {
                void * t349 = gm_l0;
                gm_tl874t2atnlh3 t350 = gm_l4;
                gm_b16_8 t351 = gm_l9;
                gm_tnpifv20irnph t352;
                GM_ST(gm_b16_8, ((uint8_t *)&t352 + 0), t351);
                gm_tabb710d9mq2e t353;
                gm_f_format_l035o11((void *)&t353, (void *)NULL, cx, NULL, t352);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t354 = t353;
                if (((((uint8_t *)&t354))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t354) + 8, gm_src7u9cfdjh7s0n, 89, 32);
                gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&t354) + 8));
                gm_b16_8 t356 = t355;
                gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)&t356));
                gm_b0_1 t358 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t349, t350, t357);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t359 = gm_l0;
                void * t360 = GM_LD(void *, (((uint8_t *)t359) + 0));
                gm_tl874t2atnlh3 t361 = gm_l4;
                void * t362 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t360, t361);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t363 = 0;
                F.ret = t362;
                goto gm_fret;
            }
            goto ix346;
ie346:;
ix346:;
        }
        {
            uint8_t t365 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l21) + 0) == ((uint8_t)0ULL)));
            if (!t365) goto ie364;
            uint8_t t366 = gm_l17;
            uint8_t t367 = (uint8_t)!t366;
            if (!t367) goto ie364;
            {
                void * t369 = gm_l0;
                gm_b16_8 t370 = gm_l9;
                gm_b16_8 t371;
                GM_ST(void *, (uint8_t *)&t371, (void *)(gm_spj0hqsg585et));
                GM_ST(uintptr_t, (uint8_t *)&t371 + 8, (uintptr_t)(13));
                gm_t88rcucg1b7uk t372 = gm_l6;
                gm_tl874t2atnlh3 t373 = gm_l4;
                void * t374 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t369, t370, t371, t372, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t375 = 0;
                F.ret = t374;
                goto gm_fret;
            }
            goto ix364;
ie364:;
ix364:;
        }
        {
            gm_ts6oqppjh0ba3 t376;
            uint8_t t382 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t382) { case ((uint8_t)0ULL): goto ma378; case ((uint8_t)1ULL): goto ma379; case ((uint8_t)2ULL): goto ma380; case ((uint8_t)3ULL): goto ma380; case ((uint8_t)4ULL): goto ma380; case ((uint8_t)5ULL): goto ma380; case ((uint8_t)6ULL): goto ma380; case ((uint8_t)7ULL): goto ma380; case ((uint8_t)8ULL): goto ma380; case ((uint8_t)9ULL): goto ma380; case ((uint8_t)10ULL): goto ma380; case ((uint8_t)11ULL): goto ma380; case ((uint8_t)12ULL): goto ma380; case ((uint8_t)13ULL): goto ma380; case ((uint8_t)14ULL): goto ma380; case ((uint8_t)15ULL): goto ma380; case ((uint8_t)16ULL): goto ma380; case ((uint8_t)17ULL): goto ma380; case ((uint8_t)18ULL): goto ma380; case ((uint8_t)19ULL): goto ma380; case ((uint8_t)20ULL): goto ma380; case ((uint8_t)21ULL): goto ma380; default: goto ma380; }
ma378:;
            uint8_t t384 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t384 != ((uint8_t)0ULL)) goto mn383;
            gm_t9qkh0rsooc8d t385 = gm_l21;
            gm_ts6oqppjh0ba3 t386;
            GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t386 + 1), t385);
            t376 = t386;
            goto me377;
mn383:;
ma379:;
            uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t388 != ((uint8_t)1ULL)) goto mn387;
            gm_t9qkh0rsooc8d t389 = gm_l21;
            gm_ts6oqppjh0ba3 t390;
            GM_ST(uint8_t, ((uint8_t *)&t390 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t390 + 1), t389);
            t376 = t390;
            goto me377;
mn387:;
ma380:;
            gm_t9qkh0rsooc8d t392 = gm_l21;
            gm_ts6oqppjh0ba3 t393;
            GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t393 + 1), t392);
            t376 = t393;
            goto me377;
mn391:;
ma381:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 93, 30);
me377:;
            gm_l24 = t376;
        }
        {
            gm_t88rcucg1b7uk t394 = gm_l6;
            gm_trl9d7m1k5lbd t395;
            memset((uint8_t *)&t395 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t396 = gm_l24;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t395 + 0, t396);
            void * t397 = gm_l2;
            GM_ST(void *, (uint8_t *)&t395 + 8, t397);
            void * t398 = gm_l14;
            GM_ST(void *, (uint8_t *)&t395 + 16, t398);
            gm_teti411eujvf4 t399;
            memset(&t399, 0, sizeof t399);
            GM_ST(uint8_t, ((uint8_t *)&t399 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t399 + 8), t395);
            gm_tl874t2atnlh3 t400 = gm_l4;
            void * t401 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t394, &t399, t400);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t402 = 0;
            F.ret = t401;
            goto gm_fret;
        }
        goto me270;
mn323:;
ma273:;
        uint8_t t404 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t404 != ((uint8_t)4ULL)) goto mn403;
        uint8_t t406 = gm_l18;
        if (!t406) goto ie405;
        {
            gm_t88rcucg1b7uk t408 = gm_l6;
            gm_trl9d7m1k5lbd t409;
            memset((uint8_t *)&t409 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t410;
            GM_ST(uint8_t, (uint8_t *)&t410 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t410 + 0), ((uint8_t)4ULL));
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t409 + 0, t410);
            void * t411 = gm_l2;
            GM_ST(void *, (uint8_t *)&t409 + 8, t411);
            void * t412 = gm_l14;
            GM_ST(void *, (uint8_t *)&t409 + 16, t412);
            gm_teti411eujvf4 t413;
            memset(&t413, 0, sizeof t413);
            GM_ST(uint8_t, ((uint8_t *)&t413 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t413 + 8), t409);
            gm_tl874t2atnlh3 t414 = gm_l4;
            void * t415 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t408, &t413, t414);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t416 = 0;
            F.ret = t415;
            goto gm_fret;
        }
        goto ix405;
ie405:;
ix405:;
        goto me270;
mn403:;
ma274:;
        uint8_t t420 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t420 != ((uint8_t)7ULL)) goto pa419;
        goto po418;
pa419:;
        uint8_t t422 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t422 != ((uint8_t)8ULL)) goto pa421;
        goto po418;
pa421:;
        uint8_t t424 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t424 != ((uint8_t)9ULL)) goto pa423;
        goto po418;
pa423:;
        goto mn417;
po418:;
        {
            uint8_t t427 = gm_l18;
            if (!t427) goto ie426;
            {
                void * t429 = gm_l0;
                gm_b16_8 t430 = gm_l9;
                gm_b16_8 t431;
                GM_ST(void *, (uint8_t *)&t431, (void *)(gm_slu3lsfv09uua));
                GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(24));
                gm_t88rcucg1b7uk t432 = gm_l6;
                gm_tl874t2atnlh3 t433 = gm_l4;
                void * t434 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t429, t430, t431, t432, t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t435 = 0;
                F.ret = t434;
                goto gm_fret;
            }
            goto ix426;
ie426:;
ix426:;
        }
        {
            gm_ts6oqppjh0ba3 t436;
            uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t442) { case ((uint8_t)0ULL): goto ma440; case ((uint8_t)1ULL): goto ma440; case ((uint8_t)2ULL): goto ma440; case ((uint8_t)3ULL): goto ma440; case ((uint8_t)4ULL): goto ma440; case ((uint8_t)5ULL): goto ma440; case ((uint8_t)6ULL): goto ma440; case ((uint8_t)7ULL): goto ma438; case ((uint8_t)8ULL): goto ma439; case ((uint8_t)9ULL): goto ma440; case ((uint8_t)10ULL): goto ma440; case ((uint8_t)11ULL): goto ma440; case ((uint8_t)12ULL): goto ma440; case ((uint8_t)13ULL): goto ma440; case ((uint8_t)14ULL): goto ma440; case ((uint8_t)15ULL): goto ma440; case ((uint8_t)16ULL): goto ma440; case ((uint8_t)17ULL): goto ma440; case ((uint8_t)18ULL): goto ma440; case ((uint8_t)19ULL): goto ma440; case ((uint8_t)20ULL): goto ma440; case ((uint8_t)21ULL): goto ma440; default: goto ma440; }
ma438:;
            uint8_t t444 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t444 != ((uint8_t)7ULL)) goto mn443;
            gm_ts6oqppjh0ba3 t445;
            GM_ST(uint8_t, (uint8_t *)&t445 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)7ULL));
            t436 = t445;
            goto me437;
mn443:;
ma439:;
            uint8_t t447 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t447 != ((uint8_t)8ULL)) goto mn446;
            gm_ts6oqppjh0ba3 t448;
            GM_ST(uint8_t, (uint8_t *)&t448 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t448 + 0), ((uint8_t)8ULL));
            t436 = t448;
            goto me437;
mn446:;
ma440:;
            gm_ts6oqppjh0ba3 t450;
            GM_ST(uint8_t, (uint8_t *)&t450 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t450 + 0), ((uint8_t)9ULL));
            t436 = t450;
            goto me437;
mn449:;
ma441:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 103, 30);
me437:;
            gm_l25 = t436;
        }
        {
            gm_t88rcucg1b7uk t451 = gm_l6;
            gm_trl9d7m1k5lbd t452;
            memset((uint8_t *)&t452 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t453 = gm_l25;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t452 + 0, t453);
            void * t454 = gm_l2;
            GM_ST(void *, (uint8_t *)&t452 + 8, t454);
            void * t455 = gm_l14;
            GM_ST(void *, (uint8_t *)&t452 + 16, t455);
            gm_teti411eujvf4 t456;
            memset(&t456, 0, sizeof t456);
            GM_ST(uint8_t, ((uint8_t *)&t456 + 0), ((uint8_t)30ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t456 + 8), t452);
            gm_tl874t2atnlh3 t457 = gm_l4;
            void * t458 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t451, &t456, t457);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t459 = 0;
            F.ret = t458;
            goto gm_fret;
        }
        goto me270;
mn417:;
ma275:;
        goto me270;
mn460:;
ma276:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 75, 5);
me270:;
    }
    {
        void * t462 = gm_l0;
        gm_tl874t2atnlh3 t463 = gm_l4;
        gm_t6j6smj7qoctu t464;
        gm_b16_8 t465 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t464 + 0, t465);
        void * t466 = gm_l0;
        gm_t88rcucg1b7uk t467 = gm_l8;
        gm_b16_8 t468 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t466, t467);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t464 + 16, t468);
        gm_tabb710d9mq2e t469;
        gm_f_format_qu5usm7((void *)&t469, (void *)NULL, cx, NULL, &t464);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t470 = t469;
        if (((((uint8_t *)&t470))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t470) + 8, gm_src7u9cfdjh7s0n, 113, 20);
        gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&t470) + 8));
        gm_b16_8 t472 = t471;
        gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)&t472));
        gm_b0_1 t474 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t462, t463, t473);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t475 = gm_l0;
        void * t476 = GM_LD(void *, (((uint8_t *)t475) + 0));
        gm_tl874t2atnlh3 t477 = gm_l4;
        void * t478 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t476, t477);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t479 = 0;
        F.ret = t478;
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
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_construction_fhm2mm5 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* attrs */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* st */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* tys */
    gm_tlcm4lvu3g1a8 gm_l13; /* p */
    gm_tlcm4lvu3g1a8 gm_l14; /* p */
    GM_STACK_CHECK(&F, 1346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        void * t3 = gm_f_construction_call_ktqtrpo(t2);
        void * t4 = t3;
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            gm_tl874t2atnlh3 t9 = gm_l3;
            void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        gm_l4 = t5;
    }
    {
        void * t13 = gm_l4;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 8) + 0));
        if (t14 != ((uint8_t)19ULL)) goto ie12;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((((uint8_t *)t13) + 8) + 8) + 8));
        gm_l5 = t15;
        {
            {
                gm_b16_8 t18 = gm_l1;
                gm_l6 = t18;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t20 = gm_l7;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t22 = (t20 < t21);
                    if (!t22) goto lb19;
                    {
                        uint64_t t25 = gm_l7;
                        uint64_t t26 = (uintptr_t)t25;
                        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tkacitepg5fk0 t30 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t26 * 72));
                        F.l8 = t30;
                        {
                            uint8_t t33 = (((GM_LD(uintptr_t, ((((uint8_t *)&F.l8) + 0) + 0) + 8) == 3) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l8) + 0) + 0)), gm_sqes10usq4q5e, 3) == 0));
                            if (!t33) goto ie32;
                            {
                                void * t35 = gm_l0;
                                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                                void * t37 = gm_l0;
                                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
                                void * t39 = gm_l2;
                                void * t40 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t39);
                                if (gm_unw.kind) {
                                    goto gm_ll23;
                                }
                                gm_tl874t2atnlh3 t41 = gm_l3;
                                gm_t88rcucg1b7uk t42 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t36, t38, t40, t41);
                                if (gm_unw.kind) {
                                    goto gm_ll23;
                                }
                                gm_l9 = t42;
                            }
                            {
                                void * t43 = gm_l0;
                                void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                                gm_t88rcucg1b7uk t45 = gm_l9;
                                gm_t8cc1mtbrhlpt t46;
                                gm_f_soa_info_pq4q43l((void *)&t46, t44, GM_CONV(gm_csguankko4lmhd, t45));
                                gm_t8cc1mtbrhlpt t47 = t46;
                                gm_tjpqvfd72o62u t48;
                                if (((((uint8_t *)&t47))[32] != 0)) {
                                    gm_tjpqvfd72o62u t49 = GM_LD(gm_tjpqvfd72o62u, (((uint8_t *)&t47) + 0));
                                    t48 = t49;
                                } else {
                                    void * t50 = gm_l0;
                                    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                                    gm_tl874t2atnlh3 t52 = gm_l3;
                                    void * t53 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t51, t52);
                                    if (gm_unw.kind) {
                                        goto gm_ll23;
                                    }
                                    uint8_t t54 = 0;
                                    F.ret = t53;
                                    goto gm_fret;
                                }
                                gm_l10 = t48;
                            }
                            {
                                void * t55 = gm_l0;
                                gm_t88rcucg1b7uk t56 = gm_l9;
                                gm_b16_8 t57 = gm_l5;
                                gm_b16_8 t58 = t57;
                                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                                gm_tl874t2atnlh3 t60 = gm_l3;
                                void * t61 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t55, t56, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t59, t60);
                                if (gm_unw.kind) {
                                    goto gm_ll23;
                                }
                                uint8_t t62 = 0;
                                F.ret = t61;
                                goto gm_fret;
                            }
                            goto ix32;
ie32:;
ix32:;
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 12, 5);
me28:;
                    }
lc19:;
                    {
                        uint64_t t64 = gm_l7;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l7 = t67;
                    }
                }
gm_ll23:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
                }
                goto gm_lad;
lb19:;
            }
        }
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            void * t70 = gm_l0;
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 8));
            gm_b16_8 t72 = gm_l1;
            void * t73 = gm_l2;
            void * t74 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t75 = gm_l3;
            gm_t88rcucg1b7uk t76 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t69, t71, t72, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t76;
        }
        {
            void * t77 = gm_l0;
            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
            void * t79 = (void *)(((uint8_t *)t78) + 8);
            gm_l12 = t79;
        }
        {
            void * t81 = gm_l12;
            gm_t88rcucg1b7uk t82 = gm_l11;
            gm_tb7u19loo3j87 t83;
            gm_f_vector_parts_crrfopm((void *)&t83, t81, GM_CONV(gm_csguankko4lmhd, t82));
            gm_tb7u19loo3j87 t84 = t83;
            if (!((((uint8_t *)&t84))[16] != 0)) goto ie80;
            gm_tlcm4lvu3g1a8 t85 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t84));
            gm_l13 = t85;
            {
                void * t87 = gm_l0;
                gm_t88rcucg1b7uk t88 = gm_l11;
                gm_tkn42u7na3t81 t89;
                GM_ST(uint8_t, (uint8_t *)&t89 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t89 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89), t88);
                ((uint8_t *)&t89)[4] = 1;
                uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 0));
                gm_tf5srh4mp4b3i t91;
                GM_ST(uint8_t, (uint8_t *)&t91 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t91 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t91 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t91), t90);
                ((uint8_t *)&t91)[8] = 1;
                gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t93 = gm_l5;
                gm_b16_8 t94 = t93;
                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                gm_tl874t2atnlh3 t96 = gm_l3;
                void * t97 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t87, t89, t91, t92, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t98 = 0;
                F.ret = t97;
                goto gm_fret;
            }
            goto ix80;
ie80:;
ix80:;
        }
        {
            void * t100 = gm_l12;
            gm_t88rcucg1b7uk t101 = gm_l11;
            gm_tb7u19loo3j87 t102;
            gm_f_math_vector_parts_49c7865((void *)&t102, t100, GM_CONV(gm_csguankko4lmhd, t101));
            gm_tb7u19loo3j87 t103 = t102;
            if (!((((uint8_t *)&t103))[16] != 0)) goto ie99;
            gm_tlcm4lvu3g1a8 t104 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t103));
            gm_l14 = t104;
            {
                void * t106 = gm_l0;
                gm_t88rcucg1b7uk t107 = gm_l11;
                gm_tkn42u7na3t81 t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108), t107);
                ((uint8_t *)&t108)[4] = 1;
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 0));
                gm_tf5srh4mp4b3i t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t110), t109);
                ((uint8_t *)&t110)[8] = 1;
                gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                gm_b16_8 t112 = gm_l5;
                gm_b16_8 t113 = t112;
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                gm_tl874t2atnlh3 t115 = gm_l3;
                void * t116 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t106, t108, t110, t111, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t117 = 0;
                F.ret = t116;
                goto gm_fret;
            }
            goto ix99;
ie99:;
ix99:;
        }
        {
            void * t118 = gm_l0;
            void * t119 = GM_LD(void *, (((uint8_t *)t118) + 0));
            gm_tl874t2atnlh3 t120 = gm_l3;
            void * t121 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t122 = 0;
            F.ret = t121;
            goto gm_fret;
        }
        goto ix12;
ie12:;
        {
            void * t124 = gm_l0;
            void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
            gm_tl874t2atnlh3 t126 = gm_l3;
            void * t127 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t125, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t128 = 0;
            F.ret = t127;
            goto gm_fret;
        }
ix12:;
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
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_conversion_32a9cuf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    gm_tn3r4rvlh0rsv gm_l12; /* <temporary array> */
    gm_b16_8 gm_l13; /* why */
    GM_STACK_CHECK(&F, 1832u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        gm_t88rcucg1b7uk t14 = gm_l1;
        uint8_t t15 = gm_f_is_vector_gkq6b88(t13, GM_CONV(gm_csguankko4lmhd, t14));
        uint8_t t16 = (uint8_t)!t15;
        if (!t16) goto ie12;
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l5;
        uint8_t t19 = gm_f_is_vector_gkq6b88(t17, GM_CONV(gm_csguankko4lmhd, t18));
        uint8_t t20 = (uint8_t)!t19;
        if (!t20) goto ie12;
        {
            uint8_t t22 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l5;
        gm_t53at80pfdus3 t26;
        gm_f_kind_jcnt4hm((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
        gm_t53at80pfdus3 t27 = t26;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
        if (t28 != ((uint8_t)11ULL)) goto ie23;
        uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)&t27) + 8) + 0));
        gm_l6 = t29;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t27) + 8) + 8));
        gm_l7 = t30;
        {
            void * t33 = gm_l4;
            gm_t88rcucg1b7uk t34 = gm_l1;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            gm_t53at80pfdus3 t36 = t35;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            if (t37 != ((uint8_t)11ULL)) goto ie32;
            uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)&t36) + 8) + 0));
            gm_l8 = t38;
            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t36) + 8) + 8));
            gm_l9 = t39;
            {
                uint8_t t41 = 0;
                void * t42 = gm_l4;
                gm_t88rcucg1b7uk t43 = gm_l5;
                uint8_t t44 = gm_f_is_vector_gkq6b88(t42, GM_CONV(gm_csguankko4lmhd, t43));
                if (t44) {
                    void * t45 = gm_l4;
                    gm_t88rcucg1b7uk t46 = gm_l1;
                    uint8_t t47 = gm_f_is_vector_gkq6b88(t45, GM_CONV(gm_csguankko4lmhd, t46));
                    t41 = t47;
                } else t41 = 0;
                gm_l10 = t41;
            }
            {
                uint64_t t49 = gm_l6;
                uint64_t t50 = gm_l8;
                uint8_t t51 = ((t49 == t50));
                if (!t51) goto ie48;
                gm_t88rcucg1b7uk t52 = gm_l7;
                gm_t88rcucg1b7uk t53 = gm_l9;
                gm_t88rcucg1b7uk t54 = t52;
                gm_t88rcucg1b7uk t55 = t53;
                uint8_t t56 = (gm_eq9m6mvtmgde(((uint8_t *)&t54), ((uint8_t *)&t55)));
                if (!t56) goto ie48;
                uint8_t t57 = 0;
                uint8_t t58 = 0;
                uint8_t t59 = gm_l10;
                if (!t59) {
                    void * t60 = gm_l4;
                    gm_t88rcucg1b7uk t61 = gm_l5;
                    uint8_t t62 = gm_f_is_positional_array_6sufsk8(t60, GM_CONV(gm_csguankko4lmhd, t61));
                    t58 = t62;
                } else t58 = 1;
                if (!t58) {
                    void * t63 = gm_l4;
                    gm_t88rcucg1b7uk t64 = gm_l1;
                    uint8_t t65 = gm_f_is_positional_array_6sufsk8(t63, GM_CONV(gm_csguankko4lmhd, t64));
                    t57 = t65;
                } else t57 = 1;
                if (!t57) goto ie48;
                {
                }
                {
                    gm_tvdg94bk7aa22 t67;
                    GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t67 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)7ULL));
                    gm_t88rcucg1b7uk t68 = gm_l1;
                    {
                        void * t70 = gm_l2;
                        gm_tn3r4rvlh0rsv t71;
                        GM_ST(void *, ((uint8_t *)&t71 + 0), t70);
                        gm_l11 = t71;
                    }
                    void * t72 = ((uint8_t *)&gm_l11);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 325, 39);
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t72 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t74 = t73;
                    gm_tl874t2atnlh3 t75 = gm_l3;
                    void * t76 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t67, t68, t74, t75);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t77 = 0;
                    F.ret = t76;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
ix48:;
            }
            {
                uint8_t t79 = gm_l10;
                if (!t79) goto ie78;
                uint64_t t80 = gm_l6;
                uint64_t t81 = gm_l8;
                uint8_t t82 = ((t80 == t81));
                if (!t82) goto ie78;
                void * t83 = gm_l0;
                void * t84 = GM_LD(void *, (((uint8_t *)t83) + 0));
                gm_t88rcucg1b7uk t85 = gm_l7;
                gm_t88rcucg1b7uk t86 = gm_l9;
                uint8_t t87 = gm_f_lossless_a54o69s(t84, GM_CONV(gm_csguankko4lmhd, t85), GM_CONV(gm_csguankko4lmhd, t86));
                if (!t87) goto ie78;
                {
                }
                {
                    gm_tvdg94bk7aa22 t89;
                    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t89 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)56ULL));
                    gm_t88rcucg1b7uk t90 = gm_l1;
                    {
                        void * t92 = gm_l2;
                        gm_tn3r4rvlh0rsv t93;
                        GM_ST(void *, ((uint8_t *)&t93 + 0), t92);
                        gm_l12 = t93;
                    }
                    void * t94 = ((uint8_t *)&gm_l12);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 327, 88);
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(((uint8_t *)t94 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t96 = t95;
                    gm_tl874t2atnlh3 t97 = gm_l3;
                    void * t98 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t89, t90, t96, t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t99 = 0;
                    F.ret = t98;
                    goto gm_fret;
                }
                goto ix78;
ie78:;
ix78:;
            }
            {
                gm_b16_8 t100;
                uint8_t t102 = gm_l10;
                if (!t102) goto ie101;
                uint64_t t103 = gm_l6;
                uint64_t t104 = gm_l8;
                uint8_t t105 = ((t103 == t104));
                if (!t105) goto ie101;
                gm_t6j6smj7qoctu t107;
                void * t108 = gm_l0;
                gm_t88rcucg1b7uk t109 = gm_l7;
                gm_b16_8 t110 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t108, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t110);
                void * t111 = gm_l0;
                gm_t88rcucg1b7uk t112 = gm_l9;
                gm_b16_8 t113 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t111, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t107 + 16, t113);
                gm_tabb710d9mq2e t114;
                gm_f_format_mrlevsk((void *)&t114, (void *)NULL, cx, NULL, &t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t115 = t114;
                if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_src7u9cfdjh7s0n, 328, 40);
                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
                gm_b16_8 t117 = t116;
                gm_b16_8 t118 = t117;
                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                t100 = t119;
                goto ix101;
ie101:;
                gm_b16_8 t121;
                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(0));
                gm_b16_8 t122 = t121;
                t100 = t122;
ix101:;
                gm_l13 = t100;
            }
            {
                void * t123 = gm_l0;
                gm_tl874t2atnlh3 t124 = gm_l3;
                gm_tfeab279c16rr t125;
                void * t126 = gm_l0;
                gm_t88rcucg1b7uk t127 = gm_l5;
                gm_b16_8 t128 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t125 + 0, t128);
                void * t129 = gm_l0;
                gm_t88rcucg1b7uk t130 = gm_l1;
                gm_b16_8 t131 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t129, t130);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t125 + 16, t131);
                gm_b16_8 t132 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t125 + 32, t132);
                gm_tabb710d9mq2e t133;
                gm_f_format_d0lfl1k((void *)&t133, (void *)NULL, cx, NULL, &t125);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t134 = t133;
                if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_src7u9cfdjh7s0n, 329, 20);
                gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                gm_b16_8 t136 = t135;
                gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                gm_b0_1 t138 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t123, t124, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t139 = gm_l0;
                void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
                gm_tl874t2atnlh3 t141 = gm_l3;
                void * t142 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t143 = 0;
                F.ret = t142;
                goto gm_fret;
            }
            goto ix32;
ie32:;
            {
                {
                    void * t146 = gm_l0;
                    gm_tl874t2atnlh3 t147 = gm_l3;
                    gm_t6j6smj7qoctu t148;
                    void * t149 = gm_l0;
                    gm_t88rcucg1b7uk t150 = gm_l5;
                    gm_b16_8 t151 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t149, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t148 + 0, t151);
                    void * t152 = gm_l0;
                    gm_t88rcucg1b7uk t153 = gm_l1;
                    gm_b16_8 t154 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t152, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t148 + 16, t154);
                    gm_tabb710d9mq2e t155;
                    gm_f_format_9rhpk5t((void *)&t155, (void *)NULL, cx, NULL, &t148);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t156 = t155;
                    if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_src7u9cfdjh7s0n, 319, 24);
                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
                    gm_b16_8 t158 = t157;
                    gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                    gm_b0_1 t160 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t146, t147, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t161 = gm_l0;
                    void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
                    gm_tl874t2atnlh3 t163 = gm_l3;
                    void * t164 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t162, t163);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t165 = 0;
                    F.ret = t164;
                    goto gm_fret;
                }
            }
ix32:;
        }
        goto ix23;
ie23:;
        {
            {
                void * t168 = gm_l0;
                gm_tl874t2atnlh3 t169 = gm_l3;
                gm_t6j6smj7qoctu t170;
                void * t171 = gm_l0;
                gm_t88rcucg1b7uk t172 = gm_l5;
                gm_b16_8 t173 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t171, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t173);
                void * t174 = gm_l0;
                gm_t88rcucg1b7uk t175 = gm_l1;
                gm_b16_8 t176 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t170 + 16, t176);
                gm_tabb710d9mq2e t177;
                gm_f_format_9rhpk5t((void *)&t177, (void *)NULL, cx, NULL, &t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t178 = t177;
                if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 8, gm_src7u9cfdjh7s0n, 315, 24);
                gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&t178) + 8));
                gm_b16_8 t180 = t179;
                gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&t180));
                gm_b0_1 t182 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t168, t169, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t183 = gm_l0;
                void * t184 = GM_LD(void *, (((uint8_t *)t183) + 0));
                gm_tl874t2atnlh3 t185 = gm_l3;
                void * t186 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t184, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t187 = 0;
                F.ret = t186;
                goto gm_fret;
            }
        }
ix23:;
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
inline __attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_load_ssdvomk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tlcm4lvu3g1a8 gm_l5; /* parts */
    void * gm_l6; /* s */
    gm_tn3r4rvlh0rsv gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 493u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l4;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        gm_tlcm4lvu3g1a8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_tl874t2atnlh3 t13 = gm_l3;
            void * t14 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l5 = t9;
    }
    {
        void * t17 = gm_l0;
        gm_b16_8 t18 = gm_l2;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sqfrv3g5v9m2n));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
        gm_tl874t2atnlh3 t20 = gm_l3;
        uint8_t t21 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t17, t18, ((uint64_t)1ULL), t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie16;
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            gm_tl874t2atnlh3 t26 = gm_l3;
            void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t29 = gm_l0;
        gm_b16_8 t30 = gm_l2;
        void * t31 = gm_l4;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_t88rcucg1b7uk t33 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t31, ((uint8_t)1), t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t34 = gm_l3;
        void * t35 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t29, t30, ((uint64_t)0ULL), t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t35;
    }
    {
    }
    {
        gm_tvdg94bk7aa22 t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)58ULL));
        gm_t88rcucg1b7uk t37 = gm_l1;
        {
            void * t39 = gm_l6;
            gm_tn3r4rvlh0rsv t40;
            GM_ST(void *, ((uint8_t *)&t40 + 0), t39);
            gm_l7 = t40;
        }
        void * t41 = ((uint8_t *)&gm_l7);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 304, 34);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t43 = t42;
        gm_tl874t2atnlh3 t44 = gm_l3;
        void * t45 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t36, t37, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = 0;
        F.ret = t45;
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
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_vector_method_hgn4sb1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint8_t gm_l10; /* numeric */
    gm_tn3r4rvlh0rsv gm_l11; /* one */
    gm_tvdg94bk7aa22 gm_l12; /* op */
    void * gm_l13; /* y */
    gm_tvdg94bk7aa22 gm_l14; /* op */
    gm_t5fru0lp11n7t gm_l15; /* <temporary array> */
    void * gm_l17; /* x */
    gm_t88rcucg1b7uk gm_l18; /* xt */
    uint8_t gm_l19; /* same_lanes */
    gm_tlcm4lvu3g1a8 gm_l20; /* xp */
    void * gm_l21; /* y */
    gm_thmgeksj5pdth gm_l22; /* <temporary array> */
    void * gm_l23; /* y */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l25; /* f */
    gm_b16_8 gm_l26; /* msg */
    void * gm_l27; /* dst */
    gm_t5fru0lp11n7t gm_l28; /* <temporary array> */
    GM_STACK_CHECK(&F, 4670u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l6 = t4;
    }
    {
        void * t5 = gm_l6;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        gm_tlcm4lvu3g1a8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_tl874t2atnlh3 t13 = gm_l5;
            void * t14 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l7 = t9;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t16;
    }
    {
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t17;
    }
    {
        uint8_t t18 = 0;
        void * t19 = gm_l6;
        gm_t88rcucg1b7uk t20 = gm_l9;
        uint8_t t21 = gm_f_is_integer_8d4f9ek(t19, GM_CONV(gm_csguankko4lmhd, t20));
        if (!t21) {
            void * t22 = gm_l6;
            gm_t88rcucg1b7uk t23 = gm_l9;
            uint8_t t24 = gm_f_is_float_3jk89qu(t22, GM_CONV(gm_csguankko4lmhd, t23));
            t18 = t24;
        } else t18 = 1;
        gm_l10 = t18;
    }
    {
        void * t25 = gm_l1;
        gm_tn3r4rvlh0rsv t26;
        GM_ST(void *, ((uint8_t *)&t26 + 0), t25);
        gm_l11 = t26;
    }
    {
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sut92jpq3kk88, 10) == 0)) goto pa30;
        goto po29;
pa30:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbqoiitqtfv75, 10) == 0)) goto pa31;
        goto po29;
pa31:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sf1qp7r4ktsd1, 10) == 0)) goto pa32;
        goto po29;
pa32:;
        goto mn28;
po29:;
        {
            void * t35 = gm_l0;
            gm_b16_8 t36 = gm_l4;
            gm_b16_8 t37 = gm_l3;
            gm_tl874t2atnlh3 t38 = gm_l5;
            uint8_t t39 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t35, t36, ((uint64_t)0ULL), t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)!t39;
            if (!t40) goto ie34;
            {
                void * t42 = gm_l0;
                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                gm_tl874t2atnlh3 t44 = gm_l5;
                void * t45 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix34;
ie34:;
ix34:;
        }
        {
            uint8_t t48 = gm_l10;
            uint8_t t49 = (uint8_t)!t48;
            if (!t49) goto ie47;
            {
                void * t51 = gm_l0;
                gm_b16_8 t52 = gm_l3;
                gm_t88rcucg1b7uk t53 = gm_l2;
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t55 = gm_l5;
                void * t56 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t51, t52, t53, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            gm_tvdg94bk7aa22 t58;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sut92jpq3kk88, 10) == 0)) goto mn60;
            gm_tvdg94bk7aa22 t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)46ULL));
            t58 = t61;
            goto me59;
mn60:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbqoiitqtfv75, 10) == 0)) goto mn62;
            gm_tvdg94bk7aa22 t63;
            GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t63 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)47ULL));
            t58 = t63;
            goto me59;
mn62:;
            gm_tvdg94bk7aa22 t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)48ULL));
            t58 = t65;
            goto me59;
mn64:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 161, 31);
me59:;
            gm_l12 = t58;
        }
        {
            gm_tvdg94bk7aa22 t66 = gm_l12;
            gm_t88rcucg1b7uk t67 = gm_l9;
            void * t68 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 166, 38);
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t68 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t70 = t69;
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_tl874t2atnlh3 t72 = gm_l5;
            void * t73 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t66, t67, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto me27;
mn28:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sj7s3tnnvob4e, 8) == 0)) goto pa77;
        goto po76;
pa77:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6lddausbiitu, 8) == 0)) goto pa78;
        goto po76;
pa78:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sh1to1i0u67lu, 8) == 0)) goto pa79;
        goto po76;
pa79:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sv8pauo1bhltl, 8) == 0)) goto pa80;
        goto po76;
pa80:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s1t08grtrn18m, 8) == 0)) goto pa81;
        goto po76;
pa81:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_scqrlin5l437v, 8) == 0)) goto pa82;
        goto po76;
pa82:;
        goto mn75;
po76:;
        {
            void * t85 = gm_l0;
            gm_b16_8 t86 = gm_l4;
            gm_b16_8 t87 = gm_l3;
            gm_tl874t2atnlh3 t88 = gm_l5;
            uint8_t t89 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t85, t86, ((uint64_t)1ULL), t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = (uint8_t)!t89;
            if (!t90) goto ie84;
            {
                void * t92 = gm_l0;
                void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
                gm_tl874t2atnlh3 t94 = gm_l5;
                void * t95 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t93, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t96 = 0;
                F.ret = t95;
                goto gm_fret;
            }
            goto ix84;
ie84:;
ix84:;
        }
        {
            gm_b16_8 t98 = gm_l3;
            gm_t6j6smj7qoctu t99;
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sj7s3tnnvob4e));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t99 + 0, t100);
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s6lddausbiitu));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&t99 + 16, t101);
            gm_t6j6smj7qoctu t102 = t99;
            uint8_t t103 = 0;
            gm_b16_8 t104 = t98;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t102) + 0, ((uint8_t *)&t104))) t103 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t102) + 16, ((uint8_t *)&t104))) t103 = 1;
            uint8_t t105 = (uint8_t)(!t103);
            if (!t105) goto ie97;
            uint8_t t106 = gm_l10;
            uint8_t t107 = (uint8_t)!t106;
            if (!t107) goto ie97;
            {
                void * t109 = gm_l0;
                gm_b16_8 t110 = gm_l3;
                gm_t88rcucg1b7uk t111 = gm_l2;
                gm_b16_8 t112;
                GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t113 = gm_l5;
                void * t114 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t109, t110, t111, t112, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t115 = 0;
                F.ret = t114;
                goto gm_fret;
            }
            goto ix97;
ie97:;
ix97:;
        }
        {
            void * t116 = gm_l0;
            gm_b16_8 t117 = gm_l4;
            gm_t88rcucg1b7uk t118 = gm_l2;
            gm_tl874t2atnlh3 t119 = gm_l5;
            void * t120 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t116, t117, ((uint64_t)0ULL), t118, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t120;
        }
        {
            gm_tvdg94bk7aa22 t121;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sj7s3tnnvob4e, 8) == 0)) goto mn123;
            gm_tvdg94bk7aa22 t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)40ULL));
            t121 = t124;
            goto me122;
mn123:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s6lddausbiitu, 8) == 0)) goto mn125;
            gm_tvdg94bk7aa22 t126;
            GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t126 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)41ULL));
            t121 = t126;
            goto me122;
mn125:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sh1to1i0u67lu, 8) == 0)) goto mn127;
            gm_tvdg94bk7aa22 t128;
            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)42ULL));
            t121 = t128;
            goto me122;
mn127:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sv8pauo1bhltl, 8) == 0)) goto mn129;
            gm_tvdg94bk7aa22 t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)43ULL));
            t121 = t130;
            goto me122;
mn129:;
            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s1t08grtrn18m, 8) == 0)) goto mn131;
            gm_tvdg94bk7aa22 t132;
            GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t132 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)44ULL));
            t121 = t132;
            goto me122;
mn131:;
            gm_tvdg94bk7aa22 t134;
            GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t134 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)45ULL));
            t121 = t134;
            goto me122;
mn133:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 172, 31);
me122:;
            gm_l14 = t121;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t135 = gm_l14;
            void * t136 = gm_l6;
            uint64_t t137 = gm_l8;
            gm_t88rcucg1b7uk t138 = gm_f_mask_ty_cj7ln0g((void *)NULL, cx, NULL, t136, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t140 = gm_l1;
                void * t141 = gm_l13;
                gm_t5fru0lp11n7t t142;
                GM_ST(void *, ((uint8_t *)&t142 + 0), t140);
                GM_ST(void *, ((uint8_t *)&t142 + 8), t141);
                gm_l15 = t142;
            }
            void * t143 = ((uint8_t *)&gm_l15);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 180, 51);
            gm_b16_8 t144;
            GM_ST(void *, (uint8_t *)&t144, (void *)(((uint8_t *)t143 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t145 = t144;
            gm_tl874t2atnlh3 t146 = gm_l5;
            void * t147 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t135, t138, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        goto me27;
mn75:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0nf1rg04f3m5, 3) == 0)) goto pa151;
        goto po150;
pa151:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skftljk77e6mu, 3) == 0)) goto pa152;
        goto po150;
pa152:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_snc5ns3akncuq, 5) == 0)) goto pa153;
        goto po150;
pa153:;
        goto mn149;
po150:;
        {
            void * t156 = gm_l0;
            gm_b16_8 t157 = gm_l4;
            gm_b16_8 t158 = gm_l3;
            gm_tl874t2atnlh3 t159 = gm_l5;
            uint8_t t160 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t156, t157, ((uint64_t)0ULL), t158, t159);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t161 = (uint8_t)!t160;
            if (!t161) goto ie155;
            {
                void * t163 = gm_l0;
                void * t164 = GM_LD(void *, (((uint8_t *)t163) + 0));
                gm_tl874t2atnlh3 t165 = gm_l5;
                void * t166 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t164, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t167 = 0;
                F.ret = t166;
                goto gm_fret;
            }
            goto ix155;
ie155:;
ix155:;
        }
        {
            void * t169 = gm_l6;
            gm_t88rcucg1b7uk t170 = gm_l9;
            uint8_t t171 = gm_f_is_bool_ajhjgeg(t169, GM_CONV(gm_csguankko4lmhd, t170));
            uint8_t t172 = (uint8_t)!t171;
            if (!t172) goto ie168;
            {
                void * t174 = gm_l0;
                gm_b16_8 t175 = gm_l3;
                gm_t88rcucg1b7uk t176 = gm_l2;
                gm_b16_8 t177;
                GM_ST(void *, (uint8_t *)&t177, (void *)(gm_s8guqcjcce9oc));
                GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(21));
                gm_tl874t2atnlh3 t178 = gm_l5;
                void * t179 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t174, t175, t176, t177, t178);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t180 = 0;
                F.ret = t179;
                goto gm_fret;
            }
            goto ix168;
ie168:;
ix168:;
        }
        {
            uint8_t t182 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0nf1rg04f3m5, 3) == 0));
            if (!t182) goto ie181;
            {
                gm_tvdg94bk7aa22 t184;
                GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t184 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t184 + 0), ((uint8_t)49ULL));
                void * t185 = gm_l6;
                gm_t88rcucg1b7uk t186 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t187 = ((uint8_t *)&gm_l11);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 185, 69);
                gm_b16_8 t188;
                GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t187 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t189 = t188;
                gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                gm_tl874t2atnlh3 t191 = gm_l5;
                void * t192 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t184, t186, t190, t191);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t193 = 0;
                F.ret = t192;
                goto gm_fret;
            }
            goto ix181;
ie181:;
ix181:;
        }
        {
            uint8_t t195 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_skftljk77e6mu, 3) == 0));
            if (!t195) goto ie194;
            {
                gm_tvdg94bk7aa22 t197;
                GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t197 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)50ULL));
                void * t198 = gm_l6;
                gm_t88rcucg1b7uk t199 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t200 = ((uint8_t *)&gm_l11);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 186, 69);
                gm_b16_8 t201;
                GM_ST(void *, (uint8_t *)&t201, (void *)(((uint8_t *)t200 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t202 = t201;
                gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                gm_tl874t2atnlh3 t204 = gm_l5;
                void * t205 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t197, t199, t203, t204);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t206 = 0;
                F.ret = t205;
                goto gm_fret;
            }
            goto ix194;
ie194:;
ix194:;
        }
        {
            gm_tvdg94bk7aa22 t207;
            GM_ST(uint8_t, (uint8_t *)&t207 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t207 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t207 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t207 + 0), ((uint8_t)51ULL));
            void * t208 = gm_l6;
            gm_t88rcucg1b7uk t209 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t208);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t210 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 187, 56);
            gm_b16_8 t211;
            GM_ST(void *, (uint8_t *)&t211, (void *)(((uint8_t *)t210 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t212 = t211;
            gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
            gm_tl874t2atnlh3 t214 = gm_l5;
            void * t215 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t207, t209, t213, t214);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t216 = 0;
            F.ret = t215;
            goto gm_fret;
        }
        goto me27;
mn149:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_svgoe41piv341, 6) == 0)) goto mn217;
        {
            void * t220 = gm_l0;
            gm_b16_8 t221 = gm_l4;
            gm_b16_8 t222 = gm_l3;
            gm_tl874t2atnlh3 t223 = gm_l5;
            uint8_t t224 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t220, t221, ((uint64_t)2ULL), t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t225 = (uint8_t)!t224;
            if (!t225) goto ie219;
            {
                void * t227 = gm_l0;
                void * t228 = GM_LD(void *, (((uint8_t *)t227) + 0));
                gm_tl874t2atnlh3 t229 = gm_l5;
                void * t230 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t228, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t231 = 0;
                F.ret = t230;
                goto gm_fret;
            }
            goto ix219;
ie219:;
ix219:;
        }
        {
            void * t233 = gm_l6;
            gm_t88rcucg1b7uk t234 = gm_l9;
            uint8_t t235 = gm_f_is_bool_ajhjgeg(t233, GM_CONV(gm_csguankko4lmhd, t234));
            uint8_t t236 = (uint8_t)!t235;
            if (!t236) goto ie232;
            {
                void * t238 = gm_l0;
                gm_b16_8 t239 = gm_l3;
                gm_t88rcucg1b7uk t240 = gm_l2;
                gm_b16_8 t241;
                GM_ST(void *, (uint8_t *)&t241, (void *)(gm_s8guqcjcce9oc));
                GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(21));
                gm_tl874t2atnlh3 t242 = gm_l5;
                void * t243 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t238, t239, t240, t241, t242);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t244 = 0;
                F.ret = t243;
                goto gm_fret;
            }
            goto ix232;
ie232:;
ix232:;
        }
        {
            uint64_t t246 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t247 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t246 >= (uintptr_t)(t247)) gm_trap_index(t246, t247, gm_src7u9cfdjh7s0n, 192, 32);
            uint8_t t248 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t246 * 144) + 0) + 0));
            if (t248 != ((uint8_t)0ULL)) goto ie245;
            gm_tc3oht4ksth9d t249 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t246 * 144) + 0) + 8));
            F.l16 = t249;
            {
                void * t251 = gm_l0;
                void * t252 = (void *)((uint8_t *)&F.l16);
                gm_tkn42u7na3t81 t253;
                memset(&t253, 0, sizeof t253);
                void * t254 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t251, t252, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t254;
            }
            {
                void * t255 = gm_l0;
                void * t256 = (void *)(((uint8_t *)t255) + 96);
                void * t257 = gm_l0;
                void * t258 = GM_LD(void *, (((uint8_t *)t257) + 0));
                void * t259 = gm_l17;
                gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t259) + 0));
                gm_t88rcucg1b7uk t261 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t256, t258, t260);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t261;
            }
            {
                void * t263 = gm_l6;
                gm_t88rcucg1b7uk t264 = gm_l18;
                uint8_t t265 = gm_f_is_error_8k27gd1(t263, GM_CONV(gm_csguankko4lmhd, t264));
                if (!t265) goto ie262;
                {
                    void * t267 = gm_l0;
                    void * t268 = GM_LD(void *, (((uint8_t *)t267) + 0));
                    gm_tl874t2atnlh3 t269 = gm_l5;
                    void * t270 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t268, t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t271 = 0;
                    F.ret = t270;
                    goto gm_fret;
                }
                goto ix262;
ie262:;
ix262:;
            }
            {
                gm_l19 = ((uint8_t)0);
            }
            {
                void * t273 = gm_l6;
                gm_t88rcucg1b7uk t274 = gm_l18;
                gm_tb7u19loo3j87 t275;
                gm_f_vector_parts_crrfopm((void *)&t275, t273, GM_CONV(gm_csguankko4lmhd, t274));
                gm_tb7u19loo3j87 t276 = t275;
                if (!((((uint8_t *)&t276))[16] != 0)) goto ie272;
                gm_tlcm4lvu3g1a8 t277 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t276));
                gm_l20 = t277;
                {
                    uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 0));
                    uint64_t t280 = gm_l8;
                    uint8_t t281 = ((t279 == t280));
                    gm_l19 = t281;
                }
                goto ix272;
ie272:;
ix272:;
            }
            {
                uint8_t t283 = gm_l19;
                uint8_t t284 = (uint8_t)!t283;
                if (!t284) goto ie282;
                {
                    void * t286 = gm_l0;
                    void * t287 = gm_l17;
                    gm_tl874t2atnlh3 t288 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t287) + 72));
                    gm_t7neromnu8t62 t289;
                    uint64_t t290 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t289 + 0, t290);
                    void * t291 = gm_l0;
                    gm_t88rcucg1b7uk t292 = gm_l2;
                    gm_b16_8 t293 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t291, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t289 + 8, t293);
                    void * t294 = gm_l0;
                    gm_t88rcucg1b7uk t295 = gm_l18;
                    gm_b16_8 t296 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t294, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&t289 + 24, t296);
                    gm_tabb710d9mq2e t297;
                    gm_f_format_4nuejdf((void *)&t297, (void *)NULL, cx, NULL, &t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t298 = t297;
                    if (((((uint8_t *)&t298))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t298) + 8, gm_src7u9cfdjh7s0n, 199, 34);
                    gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&t298) + 8));
                    gm_b16_8 t300 = t299;
                    gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
                    gm_b0_1 t302 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t286, t288, t301);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t303 = gm_l0;
                    void * t304 = GM_LD(void *, (((uint8_t *)t303) + 0));
                    gm_tl874t2atnlh3 t305 = gm_l5;
                    void * t306 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t304, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t307 = 0;
                    F.ret = t306;
                    goto gm_fret;
                }
                goto ix282;
ie282:;
ix282:;
            }
            {
                void * t308 = gm_l0;
                gm_b16_8 t309 = gm_l4;
                gm_t88rcucg1b7uk t310 = gm_l18;
                gm_tl874t2atnlh3 t311 = gm_l5;
                void * t312 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t308, t309, ((uint64_t)1ULL), t310, t311);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t312;
            }
            {
            }
            {
                gm_tvdg94bk7aa22 t313;
                GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t313 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t313 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t313 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)52ULL));
                gm_t88rcucg1b7uk t314 = gm_l18;
                {
                    void * t316 = gm_l1;
                    void * t317 = gm_l17;
                    void * t318 = gm_l21;
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 0, t316);
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 8, t317);
                    GM_ST(void *, (uint8_t *)&(*(gm_thmgeksj5pdth *)((uint8_t *)&gm_l22)) + 16, t318);
                }
                void * t319 = ((uint8_t *)&gm_l22);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_src7u9cfdjh7s0n, 203, 41);
                gm_b16_8 t320;
                GM_ST(void *, (uint8_t *)&t320, (void *)(((uint8_t *)t319 + (uintptr_t)0 * 8)));
                GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t321 = t320;
                gm_tl874t2atnlh3 t322 = gm_l5;
                void * t323 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t313, t314, t321, t322);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t324 = 0;
                F.ret = t323;
                goto gm_fret;
            }
            goto ix245;
ie245:;
            {
                void * t326 = gm_l0;
                void * t327 = GM_LD(void *, (((uint8_t *)t326) + 0));
                gm_tl874t2atnlh3 t328 = gm_l5;
                void * t329 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t327, t328);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t330 = 0;
                F.ret = t329;
                goto gm_fret;
            }
ix245:;
        }
        goto me27;
mn217:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0)) goto pa333;
        goto po332;
pa333:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s2mtoab140jp6, 3) == 0)) goto pa334;
        goto po332;
pa334:;
        goto mn331;
po332:;
        {
            void * t337 = gm_l0;
            gm_b16_8 t338 = gm_l4;
            gm_b16_8 t339 = gm_l3;
            gm_tl874t2atnlh3 t340 = gm_l5;
            uint8_t t341 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t337, t338, ((uint64_t)1ULL), t339, t340);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t342 = (uint8_t)!t341;
            if (!t342) goto ie336;
            {
                void * t344 = gm_l0;
                void * t345 = GM_LD(void *, (((uint8_t *)t344) + 0));
                gm_tl874t2atnlh3 t346 = gm_l5;
                void * t347 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t345, t346);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t348 = 0;
                F.ret = t347;
                goto gm_fret;
            }
            goto ix336;
ie336:;
ix336:;
        }
        {
            uint8_t t350 = gm_l10;
            uint8_t t351 = (uint8_t)!t350;
            if (!t351) goto ie349;
            {
                void * t353 = gm_l0;
                gm_b16_8 t354 = gm_l3;
                gm_t88rcucg1b7uk t355 = gm_l2;
                gm_b16_8 t356;
                GM_ST(void *, (uint8_t *)&t356, (void *)(gm_sfmov9pb6t5lt));
                GM_ST(uintptr_t, (uint8_t *)&t356 + 8, (uintptr_t)(13));
                gm_tl874t2atnlh3 t357 = gm_l5;
                void * t358 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t353, t354, t355, t356, t357);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t359 = 0;
                F.ret = t358;
                goto gm_fret;
            }
            goto ix349;
ie349:;
ix349:;
        }
        {
            void * t360 = gm_l0;
            gm_b16_8 t361 = gm_l4;
            gm_t88rcucg1b7uk t362 = gm_l2;
            gm_tl874t2atnlh3 t363 = gm_l5;
            void * t364 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t360, t361, ((uint64_t)0ULL), t362, t363);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t364;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t365;
            uint8_t t367 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sm3o0uerjumpm, 3) == 0));
            if (!t367) goto ie366;
            gm_tvdg94bk7aa22 t369;
            GM_ST(uint8_t, (uint8_t *)&t369 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t369 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t369 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t369 + 0), ((uint8_t)53ULL));
            gm_tvdg94bk7aa22 t370 = t369;
            t365 = t370;
            goto ix366;
ie366:;
            gm_tvdg94bk7aa22 t372;
            GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t372 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)54ULL));
            gm_tvdg94bk7aa22 t373 = t372;
            t365 = t373;
ix366:;
            gm_t88rcucg1b7uk t374 = gm_l2;
            {
                void * t376 = gm_l1;
                void * t377 = gm_l23;
                gm_t5fru0lp11n7t t378;
                GM_ST(void *, ((uint8_t *)&t378 + 0), t376);
                GM_ST(void *, ((uint8_t *)&t378 + 8), t377);
                gm_l24 = t378;
            }
            void * t379 = ((uint8_t *)&gm_l24);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 209, 100);
            gm_b16_8 t380;
            GM_ST(void *, (uint8_t *)&t380, (void *)(((uint8_t *)t379 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t381 = t380;
            gm_tl874t2atnlh3 t382 = gm_l5;
            void * t383 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t365, t374, t381, t382);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t384 = 0;
            F.ret = t383;
            goto gm_fret;
        }
        goto me27;
mn331:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sbl3pbs9195ho, 3) == 0)) goto mn385;
        {
            void * t388 = gm_l0;
            gm_b16_8 t389 = gm_l4;
            gm_b16_8 t390 = gm_l3;
            gm_tl874t2atnlh3 t391 = gm_l5;
            uint8_t t392 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t388, t389, ((uint64_t)0ULL), t390, t391);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t393 = (uint8_t)!t392;
            if (!t393) goto ie387;
            {
                void * t395 = gm_l0;
                void * t396 = GM_LD(void *, (((uint8_t *)t395) + 0));
                gm_tl874t2atnlh3 t397 = gm_l5;
                void * t398 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t396, t397);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t399 = 0;
                F.ret = t398;
                goto gm_fret;
            }
            goto ix387;
ie387:;
ix387:;
        }
        {
            uint8_t t401 = 0;
            void * t402 = gm_l6;
            gm_t88rcucg1b7uk t403 = gm_l9;
            uint8_t t404 = gm_f_is_signed_integer_s66djm7(t402, GM_CONV(gm_csguankko4lmhd, t403));
            if (!t404) {
                void * t405 = gm_l6;
                gm_t88rcucg1b7uk t406 = gm_l9;
                uint8_t t407 = gm_f_is_float_3jk89qu(t405, GM_CONV(gm_csguankko4lmhd, t406));
                t401 = t407;
            } else t401 = 1;
            uint8_t t408 = (uint8_t)!t401;
            if (!t408) goto ie400;
            {
                void * t410 = gm_l0;
                gm_b16_8 t411 = gm_l3;
                gm_t88rcucg1b7uk t412 = gm_l2;
                gm_b16_8 t413;
                GM_ST(void *, (uint8_t *)&t413, (void *)(gm_salin070nim24));
                GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(29));
                gm_tl874t2atnlh3 t414 = gm_l5;
                void * t415 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t410, t411, t412, t413, t414);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t416 = 0;
                F.ret = t415;
                goto gm_fret;
            }
            goto ix400;
ie400:;
ix400:;
        }
        {
            gm_tvdg94bk7aa22 t417;
            GM_ST(uint8_t, (uint8_t *)&t417 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t417 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t417 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t417 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)55ULL));
            gm_t88rcucg1b7uk t418 = gm_l2;
            void * t419 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 214, 42);
            gm_b16_8 t420;
            GM_ST(void *, (uint8_t *)&t420, (void *)(((uint8_t *)t419 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t421 = t420;
            gm_b16_8 t422 = GM_LD(gm_b16_8, ((uint8_t *)&t421));
            gm_tl874t2atnlh3 t423 = gm_l5;
            void * t424 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t417, t418, t422, t423);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t425 = 0;
            F.ret = t424;
            goto gm_fret;
        }
        goto me27;
mn385:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 2) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sfrjcsmisriv1, 2) == 0)) goto mn426;
        {
            void * t429 = gm_l0;
            gm_b16_8 t430 = gm_l4;
            gm_b16_8 t431 = gm_l3;
            gm_tl874t2atnlh3 t432 = gm_l5;
            uint8_t t433 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t429, t430, ((uint64_t)1ULL), t431, t432);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t434 = (uint8_t)!t433;
            if (!t434) goto ie428;
            {
                void * t436 = gm_l0;
                void * t437 = GM_LD(void *, (((uint8_t *)t436) + 0));
                gm_tl874t2atnlh3 t438 = gm_l5;
                void * t439 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t437, t438);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t440 = 0;
                F.ret = t439;
                goto gm_fret;
            }
            goto ix428;
ie428:;
ix428:;
        }
        {
            void * t441 = gm_l0;
            gm_b16_8 t442 = gm_l4;
            gm_tl874t2atnlh3 t443 = gm_l5;
            gm_tkn42u7na3t81 t444 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t441, t442, ((uint64_t)0ULL), t443);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t445 = t444;
            gm_t88rcucg1b7uk t446;
            if (((((uint8_t *)&t445))[4] != 0)) {
                gm_t88rcucg1b7uk t447 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t445) + 0));
                t446 = t447;
            } else {
                void * t448 = gm_l0;
                void * t449 = GM_LD(void *, (((uint8_t *)t448) + 0));
                gm_tl874t2atnlh3 t450 = gm_l5;
                void * t451 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t449, t450);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t452 = 0;
                F.ret = t451;
                goto gm_fret;
            }
            gm_l25 = t446;
        }
        {
            uint8_t t454 = 0;
            uint8_t t455 = 0;
            void * t456 = gm_l6;
            gm_t88rcucg1b7uk t457 = gm_l25;
            uint8_t t458 = gm_f_is_float_3jk89qu(t456, GM_CONV(gm_csguankko4lmhd, t457));
            uint8_t t459 = (uint8_t)!t458;
            if (!t459) {
                void * t460 = gm_l6;
                gm_t88rcucg1b7uk t461 = gm_l25;
                uint8_t t462 = gm_f_lane_type_ok_rp8uq1p(t460, GM_CONV(gm_csguankko4lmhd, t461));
                uint8_t t463 = (uint8_t)!t462;
                t455 = t463;
            } else t455 = 1;
            if (!t455) {
                uint8_t t464 = gm_l10;
                uint8_t t465 = (uint8_t)!t464;
                t454 = t465;
            } else t454 = 1;
            if (!t454) goto ie453;
            {
                void * t467 = gm_l0;
                gm_tl874t2atnlh3 t468 = gm_l5;
                gm_t6j6smj7qoctu t469;
                void * t470 = gm_l0;
                gm_t88rcucg1b7uk t471 = gm_l9;
                gm_b16_8 t472 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t470, t471);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t469 + 0, t472);
                void * t473 = gm_l0;
                gm_t88rcucg1b7uk t474 = gm_l25;
                gm_b16_8 t475 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t473, t474);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t469 + 16, t475);
                gm_tabb710d9mq2e t476;
                gm_f_format_m1f4vhf((void *)&t476, (void *)NULL, cx, NULL, &t469);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t477 = t476;
                if (((((uint8_t *)&t477))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t477) + 8, gm_src7u9cfdjh7s0n, 220, 32);
                gm_b16_8 t478 = GM_LD(gm_b16_8, (((uint8_t *)&t477) + 8));
                gm_b16_8 t479 = t478;
                gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&t479));
                gm_b0_1 t481 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t467, t468, t480);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t482 = gm_l0;
                void * t483 = GM_LD(void *, (((uint8_t *)t482) + 0));
                gm_tl874t2atnlh3 t484 = gm_l5;
                void * t485 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t483, t484);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t486 = 0;
                F.ret = t485;
                goto gm_fret;
            }
            goto ix453;
ie453:;
ix453:;
        }
        {
            void * t488 = gm_l0;
            void * t489 = GM_LD(void *, (((uint8_t *)t488) + 0));
            uint64_t t490 = gm_l8;
            gm_t88rcucg1b7uk t491 = gm_l25;
            gm_th2lqbo77pvsc t492;
            gm_f_vector_shape_error_3invhhe((void *)&t492, (void *)NULL, cx, NULL, t489, t490, t491);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t493 = t492;
            if (!((((uint8_t *)&t493))[16] != 0)) goto ie487;
            gm_b16_8 t494 = GM_LD(gm_b16_8, ((uint8_t *)&t493));
            gm_l26 = t494;
            {
                void * t496 = gm_l0;
                gm_tl874t2atnlh3 t497 = gm_l5;
                gm_b16_8 t498 = gm_l26;
                gm_b0_1 t499 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t496, t497, t498);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t500 = gm_l0;
                void * t501 = GM_LD(void *, (((uint8_t *)t500) + 0));
                gm_tl874t2atnlh3 t502 = gm_l5;
                void * t503 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t501, t502);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t504 = 0;
                F.ret = t503;
                goto gm_fret;
            }
            goto ix487;
ie487:;
ix487:;
        }
        {
            gm_tvdg94bk7aa22 t505;
            GM_ST(uint8_t, (uint8_t *)&t505 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t505 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t505 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t505 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t505 + 0), ((uint8_t)56ULL));
            void * t506 = gm_l6;
            uint64_t t507 = gm_l8;
            gm_t88rcucg1b7uk t508 = gm_l25;
            gm_t88rcucg1b7uk t509 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t506, t507, t508);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t510 = ((uint8_t *)&gm_l11);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src7u9cfdjh7s0n, 227, 60);
            gm_b16_8 t511;
            GM_ST(void *, (uint8_t *)&t511, (void *)(((uint8_t *)t510 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t511 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t512 = t511;
            gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
            gm_tl874t2atnlh3 t514 = gm_l5;
            void * t515 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t505, t509, t513, t514);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t516 = 0;
            F.ret = t515;
            goto gm_fret;
        }
        goto me27;
mn426:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_salpeh0v5rqdf, 7) == 0)) goto mn517;
        void * t518 = gm_l0;
        void * t519 = gm_l1;
        gm_t88rcucg1b7uk t520 = gm_l2;
        gm_b16_8 t521 = gm_l4;
        gm_tl874t2atnlh3 t522 = gm_l5;
        void * t523 = gm_f_vector_shuffle_5ime7a7((void *)NULL, cx, NULL, t518, t519, t520, t521, t522);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t524 = 0;
        F.ret = t523;
        goto gm_fret;
        goto me27;
mn517:;
        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_s0ugop4l95o1j, 5) == 0)) goto mn525;
        {
            void * t528 = gm_l0;
            gm_b16_8 t529 = gm_l4;
            gm_b16_8 t530 = gm_l3;
            gm_tl874t2atnlh3 t531 = gm_l5;
            uint8_t t532 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t528, t529, ((uint64_t)1ULL), t530, t531);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t533 = (uint8_t)!t532;
            if (!t533) goto ie527;
            {
                void * t535 = gm_l0;
                void * t536 = GM_LD(void *, (((uint8_t *)t535) + 0));
                gm_tl874t2atnlh3 t537 = gm_l5;
                void * t538 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t536, t537);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t539 = 0;
                F.ret = t538;
                goto gm_fret;
            }
            goto ix527;
ie527:;
ix527:;
        }
        {
            void * t540 = gm_l0;
            gm_b16_8 t541 = gm_l4;
            void * t542 = gm_l6;
            gm_t88rcucg1b7uk t543 = gm_l9;
            gm_t88rcucg1b7uk t544 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t542, ((uint8_t)0), t543);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t545 = gm_l5;
            void * t546 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t540, t541, ((uint64_t)0ULL), t544, t545);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t546;
        }
        {
        }
        {
            gm_tvdg94bk7aa22 t547;
            GM_ST(uint8_t, (uint8_t *)&t547 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t547 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t547 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t547 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t547 + 0), ((uint8_t)59ULL));
            void * t548 = gm_l6;
            gm_t88rcucg1b7uk t549 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t548);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                void * t551 = gm_l1;
                void * t552 = gm_l27;
                gm_t5fru0lp11n7t t553;
                GM_ST(void *, ((uint8_t *)&t553 + 0), t551);
                GM_ST(void *, ((uint8_t *)&t553 + 8), t552);
                gm_l28 = t553;
            }
            void * t554 = ((uint8_t *)&gm_l28);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src7u9cfdjh7s0n, 233, 51);
            gm_b16_8 t555;
            GM_ST(void *, (uint8_t *)&t555, (void *)(((uint8_t *)t554 + (uintptr_t)0 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t555 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t556 = t555;
            gm_tl874t2atnlh3 t557 = gm_l5;
            void * t558 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t547, t549, t556, t557);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t559 = 0;
            F.ret = t558;
            goto gm_fret;
        }
        goto me27;
mn525:;
        goto me27;
mn560:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 157, 5);
me27:;
    }
    {
        void * t562 = gm_l0;
        gm_tl874t2atnlh3 t563 = gm_l5;
        gm_b16_8 t564 = gm_l3;
        gm_tnpifv20irnph t565;
        GM_ST(gm_b16_8, ((uint8_t *)&t565 + 0), t564);
        gm_tabb710d9mq2e t566;
        gm_f_format_gsgjkse((void *)&t566, (void *)NULL, cx, NULL, t565);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t567 = t566;
        if (((((uint8_t *)&t567))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t567) + 8, gm_src7u9cfdjh7s0n, 237, 20);
        gm_b16_8 t568 = GM_LD(gm_b16_8, (((uint8_t *)&t567) + 8));
        gm_b16_8 t569 = t568;
        gm_b16_8 t570 = GM_LD(gm_b16_8, ((uint8_t *)&t569));
        gm_b0_1 t571 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t562, t563, t570);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t572 = gm_l0;
        void * t573 = GM_LD(void *, (((uint8_t *)t572) + 0));
        gm_tl874t2atnlh3 t574 = gm_l5;
        void * t575 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t573, t574);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t576 = 0;
        F.ret = t575;
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
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_shuffle_5ime7a7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    uint64_t gm_l7; /* len */
    gm_t88rcucg1b7uk gm_l8; /* usize_t */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l14; /* v */
    gm_tf5srh4mp4b3i gm_l15; /* k */
    uint8_t gm_l16; /* int_ok */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_tl874t2atnlh3 gm_l19; /* at */
    void * gm_l20; /* cell */
    unsigned __int128 gm_l21; /* n */
    uint64_t gm_l22; /* x */
    uint64_t gm_l24; /* m */
    gm_b16_8 gm_l25; /* msg */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    GM_STACK_CHECK(&F, 3056u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l5;
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        gm_tlcm4lvu3g1a8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t10 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            gm_tl874t2atnlh3 t13 = gm_l4;
            void * t14 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        gm_l6 = t9;
    }
    {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
        gm_l7 = t16;
    }
    {
        void * t17 = gm_l5;
        gm_t88rcucg1b7uk t18 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t20 = gm_l3;
            gm_l10 = t20;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l11;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l11;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t7te2tdpebbt4 t32 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t28 * 144));
                    F.l12 = t32;
                    {
                        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 0) + 0));
                        if (t35 != ((uint8_t)0ULL)) goto ie34;
                        gm_tc3oht4ksth9d t36 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l12) + 0) + 8));
                        F.l13 = t36;
                        {
                            void * t38 = gm_l0;
                            void * t39 = (void *)((uint8_t *)&F.l13);
                            gm_t88rcucg1b7uk t40 = gm_l8;
                            gm_tkn42u7na3t81 t41;
                            GM_ST(uint8_t, (uint8_t *)&t41 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t41 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41), t40);
                            ((uint8_t *)&t41)[4] = 1;
                            void * t42 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t38, t39, t41);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_l14 = t42;
                        }
                        {
                            gm_tf5srh4mp4b3i t43;
                            memset(&t43, 0, sizeof t43);
                            gm_l15 = t43;
                        }
                        {
                            gm_l16 = ((uint8_t)0);
                        }
                        {
                            void * t45 = gm_l14;
                            uint8_t t46;
                            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            if (t49 != ((uint8_t)0ULL)) goto mn48;
                            t46 = ((uint8_t)1);
                            goto me47;
mn48:;
                            t46 = ((uint8_t)0);
                            goto me47;
mn50:;
                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 12);
me47:;
                            if (!t46) goto ie44;
                            {
                                void * t52 = gm_l0;
                                void * t53 = (void *)(((uint8_t *)t52) + 96);
                                void * t54 = gm_l5;
                                void * t55 = gm_l14;
                                gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t55) + 0));
                                gm_t88rcucg1b7uk t57 = gm_l8;
                                gm_t9qkh0rsooc8d t58 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t53, t54, t56, t57);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_t9qkh0rsooc8d t59 = t58;
                                uint8_t t60;
                                if (!((((uint8_t *)&t59))[0] != 0)) goto mn62;
                                t60 = ((uint8_t)1);
                                goto me61;
mn62:;
                                t60 = ((uint8_t)0);
                                goto me61;
mn63:;
                                gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 46);
me61:;
                                uint8_t t64 = (uint8_t)!t60;
                                gm_l16 = t64;
                            }
                            goto ix44;
ie44:;
ix44:;
                        }
                        {
                            uint8_t t66 = gm_l16;
                            if (!t66) goto ie65;
                            {
                                void * t69 = gm_l14;
                                uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t69) + 8) + 0));
                                if (t70 != ((uint8_t)0ULL)) goto ie68;
                                gm_tbci5ospk5nnn t71 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t69) + 8) + 8));
                                F.l17 = t71;
                                {
                                    gm_tf5srh4mp4b3i t73 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
                                    gm_l15 = t73;
                                }
                                goto ix68;
ie68:;
                                {
                                    void * t75 = gm_l0;
                                    void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
                                    gm_tl874t2atnlh3 t77 = gm_l4;
                                    void * t78 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t76, t77);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    uint8_t t79 = 0;
                                    F.ret = t78;
                                    goto gm_fret;
                                }
ix68:;
                            }
                            goto ix65;
ie65:;
                            void * t81 = gm_l5;
                            void * t82 = gm_l14;
                            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t82) + 0));
                            uint8_t t84 = gm_f_is_error_8k27gd1(t81, GM_CONV(gm_csguankko4lmhd, t83));
                            if (!t84) goto ie80;
                            {
                                void * t86 = gm_l0;
                                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                                gm_tl874t2atnlh3 t88 = gm_l4;
                                void * t89 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t87, t88);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                uint8_t t90 = 0;
                                F.ret = t89;
                                goto gm_fret;
                            }
                            goto ix80;
ie80:;
                            void * t92 = gm_l0;
                            void * t93 = (void *)(((uint8_t *)t92) + 96);
                            void * t94 = gm_l0;
                            void * t95 = GM_LD(void *, (((uint8_t *)t94) + 0));
                            void * t96 = gm_l14;
                            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 0));
                            gm_t88rcucg1b7uk t98 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t93, t95, t97);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                            gm_t88rcucg1b7uk t99 = gm_l8;
                            gm_t88rcucg1b7uk t100 = t98;
                            gm_t88rcucg1b7uk t101 = t99;
                            uint8_t t102 = (!gm_eq9m6mvtmgde(((uint8_t *)&t100), ((uint8_t *)&t101)));
                            if (!t102) goto ie91;
                            {
                                void * t104 = gm_l0;
                                void * t105 = (void *)(((uint8_t *)t104) + 96);
                                void * t106 = gm_l0;
                                void * t107 = GM_LD(void *, (((uint8_t *)t106) + 0));
                                void * t108 = gm_l14;
                                gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t108) + 0));
                                gm_t88rcucg1b7uk t110 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t105, t107, t109);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_l18 = t110;
                            }
                            {
                                void * t111 = gm_l0;
                                void * t112 = gm_l14;
                                gm_tl874t2atnlh3 t113 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t112) + 72));
                                void * t114 = gm_l0;
                                gm_t88rcucg1b7uk t115 = gm_l18;
                                gm_b16_8 t116 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t114, t115);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_tnpifv20irnph t117;
                                GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t116);
                                gm_tabb710d9mq2e t118;
                                gm_f_format_jrngvku((void *)&t118, (void *)NULL, cx, NULL, t117);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_tabb710d9mq2e t119 = t118;
                                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_src7u9cfdjh7s0n, 265, 30);
                                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                                gm_b16_8 t121 = t120;
                                gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                                gm_b0_1 t123 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t111, t113, t122);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                            }
                            {
                                void * t124 = gm_l0;
                                void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
                                gm_tl874t2atnlh3 t126 = gm_l4;
                                void * t127 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t125, t126);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                uint8_t t128 = 0;
                                F.ret = t127;
                                goto gm_fret;
                            }
                            goto ix91;
ie91:;
                            void * t130 = gm_l14;
                            uint8_t t131 = GM_LD(uint8_t, ((((uint8_t *)t130) + 8) + 0));
                            if (t131 != ((uint8_t)14ULL)) goto ie129;
                            gm_tl874t2atnlh3 t132 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t130) + 8) + 8) + 16));
                            gm_l19 = t132;
                            {
                                void * t135 = gm_l0;
                                void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                                gm_tl874t2atnlh3 t137 = gm_l19;
                                void * t138 = gm_l14;
                                gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t138) + 0));
                                void * t140 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t136, t137, t139);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                void * t141 = t140;
                                if (!(GM_LD(void *, ((uint8_t *)&t141)) != 0)) goto ie134;
                                void * t142 = GM_LD(void *, ((uint8_t *)&t141));
                                gm_l20 = t142;
                                {
                                    void * t145 = gm_l20;
                                    uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t145) + 0) + 0));
                                    if (t146 != ((uint8_t)0ULL)) goto ie144;
                                    unsigned __int128 t147 = GM_LD(unsigned __int128, ((((uint8_t *)t145) + 0) + 16));
                                    gm_l21 = t147;
                                    {
                                        unsigned __int128 t149 = gm_l21;
                                        gm_tf5srh4mp4b3i t150;
                                        uint64_t t151 = (uint64_t)t149;
                                        gm_tf5srh4mp4b3i t152;
                                        GM_ST(uint8_t, (uint8_t *)&t152 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t152 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t152 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t152), t151);
                                        ((uint8_t *)&t152)[8] = 1;
                                        gm_tf5srh4mp4b3i t153;
                                        memset(&t153, 0, sizeof t153);
                                        t150 = ((unsigned __int128)(t149) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t152 : t153;
                                        gm_l15 = t150;
                                    }
                                    goto ix144;
ie144:;
ix144:;
                                }
                                goto ix134;
ie134:;
ix134:;
                            }
                            goto ix129;
ie129:;
ix129:;
ix91:;
ix80:;
ix65:;
                        }
                        {
                            if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie154;
                            uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            gm_l22 = t155;
                            {
                                uint64_t t158 = gm_l22;
                                uint64_t t159 = gm_l7;
                                uint8_t t160 = (t158 >= t159);
                                if (!t160) goto ie157;
                                {
                                    void * t162 = gm_l0;
                                    void * t163 = gm_l14;
                                    gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t163) + 72));
                                    gm_tob1snuii5bme t165;
                                    uint64_t t166 = gm_l22;
                                    GM_ST(uint64_t, (uint8_t *)&t165 + 0, t166);
                                    void * t167 = gm_l0;
                                    gm_t88rcucg1b7uk t168 = gm_l2;
                                    gm_b16_8 t169 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t167, t168);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t165 + 8, t169);
                                    uint64_t t170 = gm_l7;
                                    uint64_t r171; bool o172 = __builtin_sub_overflow(t170, ((uint64_t)1ULL), &r171);
                                    if (o172) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 274, 111);
                                    uint64_t t173 = r171;
                                    GM_ST(uint64_t, (uint8_t *)&t165 + 24, t173);
                                    gm_tabb710d9mq2e t174;
                                    gm_f_format_jm4dd7q((void *)&t174, (void *)NULL, cx, NULL, &t165);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    gm_tabb710d9mq2e t175 = t174;
                                    if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 8, gm_src7u9cfdjh7s0n, 274, 34);
                                    gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t175) + 8));
                                    gm_b16_8 t177 = t176;
                                    gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                                    gm_b0_1 t179 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t162, t164, t178);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                }
                                {
                                    void * t180 = gm_l0;
                                    void * t181 = GM_LD(void *, (((uint8_t *)t180) + 0));
                                    gm_tl874t2atnlh3 t182 = gm_l4;
                                    void * t183 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t181, t182);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    uint8_t t184 = 0;
                                    F.ret = t183;
                                    goto gm_fret;
                                }
                                goto ix157;
ie157:;
ix157:;
                            }
                            {
                                void * t185 = (void *)((uint8_t *)&F.l9);
                                gm_t88rcucg1b7uk t186 = gm_l8;
                                uint64_t t187 = gm_l22;
                                gm_tbci5ospk5nnn t188;
                                gm_f_from_u64_ud13q5v((void *)&t188, (void *)NULL, cx, NULL, t187);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_teti411eujvf4 t189;
                                GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t189 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t189 + 8), t188);
                                void * t190 = gm_l14;
                                gm_tl874t2atnlh3 t191 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t190) + 72));
                                void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t186, &t189, t191);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                gm_tftb1odc739t5 t193 = gm_f_push_d943vgp_2(t185, &(*(gm_t69m56meqefs8 *)((uint8_t *)t192)));
                                gm_tftb1odc739t5 t194 = t193;
                                if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 1, gm_src7u9cfdjh7s0n, 277, 13);
                                gm_b0_1 t195 = GM_LD(gm_b0_1, (((uint8_t *)&t194) + 1));
                            }
                            goto ix154;
ie154:;
                            {
                                void * t198 = gm_l14;
                                uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t198) + 8) + 0));
                                if (t199 != ((uint8_t)0ULL)) goto ie197;
                                gm_tbci5ospk5nnn t200 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t198) + 8) + 8));
                                F.l23 = t200;
                                {
                                    void * t202 = gm_l0;
                                    void * t203 = gm_l14;
                                    gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t203) + 72));
                                    gm_tftksr4op114e t205;
                                    gm_b16_8 t206 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t205 + 0, t206);
                                    void * t207 = gm_l0;
                                    gm_t88rcucg1b7uk t208 = gm_l2;
                                    gm_b16_8 t209 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t207, t208);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t205 + 16, t209);
                                    uint64_t t210 = gm_l7;
                                    uint64_t r211; bool o212 = __builtin_sub_overflow(t210, ((uint64_t)1ULL), &r211);
                                    if (o212) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 280, 124);
                                    uint64_t t213 = r211;
                                    GM_ST(uint64_t, (uint8_t *)&t205 + 32, t213);
                                    gm_tabb710d9mq2e t214;
                                    gm_f_format_8eujoej((void *)&t214, (void *)NULL, cx, NULL, &t205);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    gm_tabb710d9mq2e t215 = t214;
                                    if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 8, gm_src7u9cfdjh7s0n, 280, 34);
                                    gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t215) + 8));
                                    gm_b16_8 t217 = t216;
                                    gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                                    gm_b0_1 t219 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t202, t204, t218);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                }
                                {
                                    void * t220 = gm_l0;
                                    void * t221 = GM_LD(void *, (((uint8_t *)t220) + 0));
                                    gm_tl874t2atnlh3 t222 = gm_l4;
                                    void * t223 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t221, t222);
                                    if (gm_unw.kind) {
                                        goto gm_ll25;
                                    }
                                    uint8_t t224 = 0;
                                    F.ret = t223;
                                    goto gm_fret;
                                }
                                goto ix197;
ie197:;
ix197:;
                            }
                            {
                                void * t225 = gm_l0;
                                void * t226 = gm_l14;
                                gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t226) + 72));
                                gm_b16_8 t228;
                                GM_ST(void *, (uint8_t *)&t228, (void *)(gm_skdalh0o0hret));
                                GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(58));
                                gm_b0_1 t229 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t225, t227, t228);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                            }
                            {
                                void * t230 = gm_l0;
                                void * t231 = GM_LD(void *, (((uint8_t *)t230) + 0));
                                gm_tl874t2atnlh3 t232 = gm_l4;
                                void * t233 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t231, t232);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                uint8_t t234 = 0;
                                F.ret = t233;
                                goto gm_fret;
                            }
ix154:;
                        }
                        goto ix34;
ie34:;
                        {
                            {
                                void * t237 = gm_l0;
                                gm_tl874t2atnlh3 t238 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                gm_b16_8 t239;
                                GM_ST(void *, (uint8_t *)&t239, (void *)(gm_sioam4mlrjuar));
                                GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(51));
                                gm_b0_1 t240 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t237, t238, t239);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                            }
                            {
                                void * t241 = gm_l0;
                                void * t242 = GM_LD(void *, (((uint8_t *)t241) + 0));
                                gm_tl874t2atnlh3 t243 = gm_l4;
                                void * t244 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t242, t243);
                                if (gm_unw.kind) {
                                    goto gm_ll25;
                                }
                                uint8_t t245 = 0;
                                F.ret = t244;
                                goto gm_fret;
                            }
                        }
ix34:;
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 249, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t247 = gm_l11;
                    uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                    uint64_t t250 = r248;
                    gm_l11 = t250;
                }
            }
gm_ll25:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
            }
            goto gm_lad;
lb21:;
        }
    }
    {
        uint64_t t251 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
        uint64_t t252 = (uint64_t)t251;
        gm_l24 = t252;
    }
    {
        void * t254 = gm_l0;
        void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
        uint64_t t256 = gm_l24;
        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_th2lqbo77pvsc t258;
        gm_f_vector_shape_error_3invhhe((void *)&t258, (void *)NULL, cx, NULL, t255, t256, t257);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t259 = t258;
        if (!((((uint8_t *)&t259))[16] != 0)) goto ie253;
        gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
        gm_l25 = t260;
        {
            void * t262 = gm_l0;
            gm_tl874t2atnlh3 t263 = gm_l4;
            gm_b16_8 t264 = gm_l25;
            gm_tnpifv20irnph t265;
            GM_ST(gm_b16_8, ((uint8_t *)&t265 + 0), t264);
            gm_tabb710d9mq2e t266;
            gm_f_format_7cgva50((void *)&t266, (void *)NULL, cx, NULL, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t267 = t266;
            if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_src7u9cfdjh7s0n, 289, 24);
            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
            gm_b16_8 t269 = t268;
            gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
            gm_b0_1 t271 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t262, t263, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t272 = gm_l0;
            void * t273 = GM_LD(void *, (((uint8_t *)t272) + 0));
            gm_tl874t2atnlh3 t274 = gm_l4;
            void * t275 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t273, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t276 = 0;
            F.ret = t275;
            goto gm_fret;
        }
        goto ix253;
ie253:;
ix253:;
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t277 = (void *)((uint8_t *)&F.l26);
        void * t278 = gm_l1;
        gm_tftb1odc739t5 t279 = gm_f_push_d943vgp_2(t277, &(*(gm_t69m56meqefs8 *)((uint8_t *)t278)));
        gm_tftb1odc739t5 t280 = t279;
        if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_src7u9cfdjh7s0n, 293, 5);
        gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
    }
    {
        {
            gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_l27 = t283;
        }
        {
            gm_l28 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t285 = gm_l28;
                uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                uint8_t t287 = (t285 < t286);
                if (!t287) goto lb284;
                {
                    uint64_t t290 = gm_l28;
                    uint64_t t291 = (uintptr_t)t290;
                    uint64_t t292 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    gm_t69m56meqefs8 t295 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t291 * 88));
                    F.l29 = t295;
                    {
                        void * t297 = (void *)((uint8_t *)&F.l26);
                        gm_tftb1odc739t5 t298 = gm_f_push_d943vgp_2(t297, &(*(gm_t69m56meqefs8 *)((uint8_t *)&F.l29)));
                        gm_tftb1odc739t5 t299 = t298;
                        if (((((uint8_t *)&t299))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t299) + 1, gm_src7u9cfdjh7s0n, 294, 26);
                        gm_b0_1 t300 = GM_LD(gm_b0_1, (((uint8_t *)&t299) + 1));
                    }
                    goto me293;
mn294:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 294, 5);
me293:;
                }
lc284:;
                {
                    uint64_t t302 = gm_l28;
                    uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                    uint64_t t305 = r303;
                    gm_l28 = t305;
                }
            }
lb284:;
        }
    }
    {
        void * t306 = gm_l5;
        uint64_t t307 = gm_l24;
        gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_t88rcucg1b7uk t309 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t306, t307, t308);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tt8o5kebou62r t310;
        memset((uint8_t *)&t310 + 12, 0, 4);
        gm_tvdg94bk7aa22 t311;
        GM_ST(uint8_t, (uint8_t *)&t311 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t311 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t311 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t311 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t311 + 0), ((uint8_t)57ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t310 + 0, t311);
        gm_b16_8 t312 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t310 + 16, t312);
        gm_teti411eujvf4 t313;
        GM_ST(uint8_t, (uint8_t *)&t313 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t313 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t313 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t313 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t313 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t313 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t313 + 8), t310);
        gm_tl874t2atnlh3 t314 = gm_l4;
        void * t315 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t309, &t313, t314);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t316 = 0;
        F.ret = t315;
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
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_unary_2mbe97t F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    gm_t88rcucg1b7uk gm_l7; /* lane */
    gm_b16_8 gm_l8; /* hint */
    gm_b16_8 gm_l9; /* s */
    GM_STACK_CHECK(&F, 1219u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t11;
    }
    {
        void * t12 = gm_l4;
        gm_t88rcucg1b7uk t13 = gm_l5;
        gm_tb7u19loo3j87 t14;
        gm_f_vector_parts_crrfopm((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        gm_tb7u19loo3j87 t15 = t14;
        gm_tlcm4lvu3g1a8 t16;
        if (((((uint8_t *)&t15))[16] != 0)) {
            gm_tlcm4lvu3g1a8 t17 = GM_LD(gm_tlcm4lvu3g1a8, (((uint8_t *)&t15) + 0));
            t16 = t17;
        } else {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            gm_tl874t2atnlh3 t20 = gm_l3;
            void * t21 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        gm_l6 = t16;
    }
    {
        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t23;
    }
    {
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t30) { case ((uint8_t)0ULL): goto ma25; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma28; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma28; case ((uint8_t)5ULL): goto ma27; default: goto ma28; }
ma25:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t32 != ((uint8_t)0ULL)) goto mn31;
        uint8_t t34 = 0;
        void * t35 = gm_l4;
        gm_t88rcucg1b7uk t36 = gm_l7;
        uint8_t t37 = gm_f_is_signed_integer_s66djm7(t35, GM_CONV(gm_csguankko4lmhd, t36));
        if (!t37) {
            void * t38 = gm_l4;
            gm_t88rcucg1b7uk t39 = gm_l7;
            uint8_t t40 = gm_f_is_float_3jk89qu(t38, GM_CONV(gm_csguankko4lmhd, t39));
            t34 = t40;
        } else t34 = 1;
        if (!t34) goto ie33;
        {
            gm_t88rcucg1b7uk t42 = gm_l5;
            gm_t9qkh0rsooc8d t43;
            GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
            void * t44 = gm_l2;
            gm_t8ghgl2jj0qha t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t45 + 0), t43);
            GM_ST(void *, ((uint8_t *)&t45 + 8), t44);
            gm_teti411eujvf4 t46;
            memset(&t46, 0, sizeof t46);
            GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t46 + 8), t45);
            gm_tl874t2atnlh3 t47 = gm_l3;
            void * t48 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t42, &t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
        goto me24;
mn31:;
ma26:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t51 != ((uint8_t)1ULL)) goto mn50;
        void * t53 = gm_l4;
        gm_t88rcucg1b7uk t54 = gm_l7;
        uint8_t t55 = gm_f_is_float_3jk89qu(t53, GM_CONV(gm_csguankko4lmhd, t54));
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie52;
        {
            gm_t88rcucg1b7uk t58 = gm_l5;
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)1ULL));
            void * t60 = gm_l2;
            gm_t8ghgl2jj0qha t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t61 + 0), t59);
            GM_ST(void *, ((uint8_t *)&t61 + 8), t60);
            gm_teti411eujvf4 t62;
            memset(&t62, 0, sizeof t62);
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)29ULL));
            GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t62 + 8), t61);
            gm_tl874t2atnlh3 t63 = gm_l3;
            void * t64 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t58, &t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t65 = 0;
            F.ret = t64;
            goto gm_fret;
        }
        goto ix52;
ie52:;
ix52:;
        goto me24;
mn50:;
ma27:;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t67 != ((uint8_t)5ULL)) goto mn66;
        {
            gm_b16_8 t69;
            void * t71 = gm_l4;
            gm_t88rcucg1b7uk t72 = gm_l7;
            uint8_t t73 = gm_f_is_bool_ajhjgeg(t71, GM_CONV(gm_csguankko4lmhd, t72));
            if (!t73) goto ie70;
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sajso5sujsruj));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(46));
            gm_b16_8 t76 = t75;
            t69 = t76;
            goto ix70;
ie70:;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(0));
            gm_b16_8 t79 = t78;
            t69 = t79;
ix70:;
            gm_l8 = t69;
        }
        {
            void * t80 = gm_l0;
            gm_tl874t2atnlh3 t81 = gm_l3;
            gm_t6j6smj7qoctu t82;
            void * t83 = gm_l0;
            gm_t88rcucg1b7uk t84 = gm_l5;
            gm_b16_8 t85 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t85);
            gm_b16_8 t86 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t86);
            gm_tabb710d9mq2e t87;
            gm_f_format_s7ci5ul((void *)&t87, (void *)NULL, cx, NULL, &t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t88 = t87;
            if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_src7u9cfdjh7s0n, 139, 28);
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
            gm_b16_8 t90 = t89;
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            gm_b0_1 t92 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t80, t81, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t93 = gm_l0;
            void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
            gm_tl874t2atnlh3 t95 = gm_l3;
            void * t96 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t97 = 0;
            F.ret = t96;
            goto gm_fret;
        }
        goto me24;
mn66:;
ma28:;
        goto me24;
mn98:;
ma29:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 134, 5);
me24:;
    }
    {
        gm_b16_8 t100;
        uint8_t t102 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (!t102) goto ie101;
        gm_b16_8 t104;
        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(1));
        gm_b16_8 t105 = t104;
        t100 = t105;
        goto ix101;
ie101:;
        gm_b16_8 t107;
        GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sa831sfid0hf3));
        GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(1));
        gm_b16_8 t108 = t107;
        t100 = t108;
ix101:;
        gm_l9 = t100;
    }
    {
        void * t109 = gm_l0;
        gm_tl874t2atnlh3 t110 = gm_l3;
        gm_t6j6smj7qoctu t111;
        gm_b16_8 t112 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 0, t112);
        void * t113 = gm_l0;
        gm_t88rcucg1b7uk t114 = gm_l5;
        gm_b16_8 t115 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t113, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t111 + 16, t115);
        gm_tabb710d9mq2e t116;
        gm_f_format_ul5ufam((void *)&t116, (void *)NULL, cx, NULL, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t117 = t116;
        if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src7u9cfdjh7s0n, 145, 20);
        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
        gm_b16_8 t119 = t118;
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t109, t110, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t122 = gm_l0;
        void * t123 = GM_LD(void *, (((uint8_t *)t122) + 0));
        gm_tl874t2atnlh3 t124 = gm_l3;
        void * t125 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t126 = 0;
        F.ret = t125;
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
