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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint8_t v1; } f1; } gm_tum2strs1ckr2;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tj2m9dbh1ekv6 v1; } f1; } gm_tij245bdr7emd;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_tum2strs1ckr2 f0; uint8_t f1; uint8_t pe[3]; } gm_ts06tb4mldv43;
_Static_assert(sizeof(gm_tum2strs1ckr2) == 8 && _Alignof(gm_tum2strs1ckr2) == 4 && offsetof(gm_tum2strs1ckr2, f0) == 0 && offsetof(gm_tum2strs1ckr2, f1) == 4, "layout of `Escape`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tij245bdr7emd) == 24 && _Alignof(gm_tij245bdr7emd) == 8 && offsetof(gm_tij245bdr7emd, f0) == 0 && offsetof(gm_tij245bdr7emd, f1) == 8, "layout of `string!IntSuffix`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts06tb4mldv43) == 12 && _Alignof(gm_ts06tb4mldv43) == 4 && offsetof(gm_ts06tb4mldv43, f0) == 0 && offsetof(gm_ts06tb4mldv43, f1) == 8, "layout of `?Escape`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tbci5ospk5nnn f0; gm_t46lmulm8gaj3 f1; uint8_t f2; uint8_t pe[3]; } gm_ta42iqot1auae;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_ta42iqot1auae) == 64 && _Alignof(gm_ta42iqot1auae) == 8 && offsetof(gm_ta42iqot1auae, f0) == 0 && offsetof(gm_ta42iqot1auae, f1) == 48 && offsetof(gm_ta42iqot1auae, f2) == 60, "layout of `IntLit`");
typedef struct { gm_tngtuiuskn938 f0; gm_taari1s0m51a7 f1; uint8_t f2; uint8_t pe[5]; } gm_tghmb53v8qi1e;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tghmb53v8qi1e) == 104 && _Alignof(gm_tghmb53v8qi1e) == 8 && offsetof(gm_tghmb53v8qi1e, f0) == 0 && offsetof(gm_tghmb53v8qi1e, f1) == 96 && offsetof(gm_tghmb53v8qi1e, f2) == 98, "layout of `FloatLit`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_all_digits_e77c2k1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_exponent_qp4713f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_emit_char_1ips8a1 {
    gm_frame h;
    gm_ta42iqot1auae l3; /* lit */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_suffix_lh8qbg0 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_float_value_ip18r0p {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* digits */
    gm_tbci5ospk5nnn l18; /* mantissa */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_fraction_follows_ivbj04h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_imag_width_9gtosab {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_int_suffix_j594l00 {
    gm_frame h;
    gm_tij245bdr7emd ret;
};
struct gm_fr_int_value_cnmrbr4 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_hex_digit_9lads51 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_imag_suffix_46gu23m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_char_4qk4vjp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_escape_7pnp2v0 {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_lex_field_index_pf905g6 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_number_9j9sdfi {
    gm_frame h;
    gm_t4gut2c9fj1kt l22; /* d */
    gm_tngtuiuskn938 l24; /* value */
    gm_tbci5ospk5nnn l25; /* value */
    gm_b0_1 ret;
};
struct gm_fr_lex_raw_string_uf6bd4g {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_lex_string_bcdbdjm {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* bytes */
    gm_b0_1 ret;
};
struct gm_fr_lex_unicode_escape_ns81oou {
    gm_frame h;
    gm_ts06tb4mldv43 ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_parse_valid_9fjef3j {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_push_utf8_bjq6ubs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_radix_name_5kts8t0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_digits_jhpsv6j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_scan_exponent_20o8k8o {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_skip_to_closing_quote_qmb6ii3 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_unterminated_at_line_end_v8hbf1f {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint32_t gm_f_digit_value_8ogbrqp(uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4);
__attribute__((aligned(4))) void gm_f_format_16f92bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_1kivsu4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_3rm05av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_51h4888(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_78j0ecp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b6aggpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_convcvp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ek7crui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eue6enu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g6sh9k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ikusttp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ufd866t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tm98ap4112hee a0);
__attribute__((aligned(4))) void gm_f_format_v5s6luj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_j2spmhj(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_q1fs5ps(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_parse_fbta82r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_scaled_ufcgnm9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1, int64_t a2);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern uint8_t gm_cobksqr67t4[8] __attribute__((aligned(8)));
extern uint8_t gm_k2kjji37v2m[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmv1ea52os[40] __attribute__((aligned(8)));
extern uint8_t gm_khsmm7kmq9o[40] __attribute__((aligned(8)));
extern uint8_t gm_knasprnpdnf[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s15p9kmrsfogp[2];
extern const uint8_t gm_s32vm0u5vgiu5[18];
extern const uint8_t gm_s4hf5j4eu72mv[66];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s5frmv5tebja4[50];
extern const uint8_t gm_s5k9ll19r7m4g[4];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_sae23ckujbpp9[107];
extern const uint8_t gm_safnb563bt6ue[12];
extern const uint8_t gm_sajhc6og5bv8v[59];
extern const uint8_t gm_saoa4m7dh2vl3[55];
extern const uint8_t gm_sb6jpc12sa7uj[4];
extern const uint8_t gm_sc80lo6tf645h[31];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_se4629k8oa1uo[2];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_sfd839bbgsa7s[63];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg550i4fp569j[55];
extern const uint8_t gm_sh6hkhojsmti9[48];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shq8kvmdjhmfg[4];
extern const uint8_t gm_sikg061j3im7q[2];
extern const uint8_t gm_sil0do0eri7f7[44];
extern const uint8_t gm_sjblck9bpb6r8[28];
extern const uint8_t gm_skmai4uvseje8[24];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smit2phoauonm[61];
extern const uint8_t gm_sovc43d45he5a[21];
extern const uint8_t gm_sq9ngpqr0m7ek[31];
extern const uint8_t gm_sqhghq789nvm0[8];
extern const uint8_t gm_sqj9769nr8pm0[48];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4c97p4hek1p[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srnee0q49ttns[32];
extern const uint8_t gm_su30pqbigkn5m[6];
extern const uint8_t gm_su7ft5rc766n4[3];
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
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_b4v8vul F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    uint64_t t6 = gm_l1;
    uint64_t t7 = (uintptr_t)t6;
    gm_tqs5p4n2tq19v t8;
    if (t7 < t5) {
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)t4 + t7 * 1));
    gm_tqs5p4n2tq19v t10;
    GM_ST(uint8_t, ((uint8_t *)&t10), t9);
    ((uint8_t *)&t10)[1] = 1;
        t8 = t10;
    } else {
    gm_tqs5p4n2tq19v t11;
    memset(&t11, 0, sizeof t11);
        t8 = t11;
    }
    F.ret = t8;
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
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_current_char_ctktsl2 F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
    gm_tb31k5l794t7p t5 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t2, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
gm_fprop:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_error_p5r0d56 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = gm_l0;
        uint64_t t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
        gm_b16_8 t8 = gm_l3;
        gm_b0_1 t9 = gm_f_report_eardf07((void *)NULL, cx, NULL, t3, t7, t8);
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
inline __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2) {
    struct gm_fr_int_value_cnmrbr4 F;
    void * gm_l0; /* lx */
    gm_t2lqu8jsj511d gm_l1; /* digits */
    uint32_t gm_l2; /* radix */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 8) + 8);
        void * t4 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t2) + 8)));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
        uint64_t t6 = (uintptr_t)t5;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t8 = (uintptr_t)t7;
        if (t6 > t8 || t8 > (uintptr_t)t3) gm_trap_slice(t6, t8, t3, gm_srct4c97p4hek1p, 226, 24);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + t6 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t6));
        uint32_t t10 = gm_l2;
        gm_f_parse_valid_9fjef3j((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t9, t10);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_9lads51(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_9lads51 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 17u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    uint8_t t3 = gm_l0;
    uint8_t t4 = gm_f_is_digit_2hoggnj(t3);
    if (!t4) {
        uint8_t t5 = 0;
        uint8_t t6 = gm_l0;
        uint8_t t7 = (t6 >= ((uint8_t)97ULL));
        if (t7) {
            uint8_t t8 = gm_l0;
            uint8_t t9 = (t8 <= ((uint8_t)102ULL));
            t5 = t9;
        } else t5 = 0;
        t2 = t5;
    } else t2 = 1;
    if (!t2) {
        uint8_t t10 = 0;
        uint8_t t11 = gm_l0;
        uint8_t t12 = (t11 >= ((uint8_t)65ULL));
        if (t12) {
            uint8_t t13 = gm_l0;
            uint8_t t14 = (t13 <= ((uint8_t)70ULL));
            t10 = t14;
        } else t10 = 0;
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lone_cr_ktb0jvm F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 198, 22);
        uint64_t t9 = r7;
        gm_b16_8 t10 = GM_LD(gm_b16_8, gm_c644mb9o5bq);
        gm_b0_1 t11 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t2, t4, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
        uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 199, 5);
        uint64_t t17 = r15;
        GM_ST(uint64_t, (((uint8_t *)t12) + 24), t17);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    uint64_t t8 = (uintptr_t)t7;
    gm_tqs5p4n2tq19v t9;
    if (t8 < t5) {
    uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)t4 + t8 * 1));
    gm_tqs5p4n2tq19v t11;
    GM_ST(uint8_t, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
        t9 = t11;
    } else {
    gm_tqs5p4n2tq19v t12;
    memset(&t12, 0, sizeof t12);
        t9 = t12;
    }
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_at_74p8lkq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)t1) + 8));
    gm_b16_8 t3 = t2;
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t3)));
    uint64_t t5 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t3) + 8);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    uint64_t t8 = gm_l1;
    uint64_t r9; bool o10 = __builtin_add_overflow(t7, t8, &r9);
    if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t11 = r9;
    uint64_t t12 = (uintptr_t)t11;
    gm_tqs5p4n2tq19v t13;
    if (t12 < t5) {
    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)t4 + t12 * 1));
    gm_tqs5p4n2tq19v t15;
    GM_ST(uint8_t, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[1] = 1;
        t13 = t15;
    } else {
    gm_tqs5p4n2tq19v t16;
    memset(&t16, 0, sizeof t16);
        t13 = t16;
    }
    F.ret = t13;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_radix_name_5kts8t0 F;
    uint32_t gm_l0; /* radix */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t1 = gm_l0;
    uint32_t t2 = t1;
    gm_b16_8 t3;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint32_t)2ULL))) goto mn5;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s8e72gdumihb1));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(6));
    t3 = t7;
    goto me4;
mn5:;
    uint32_t t9 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t9 == ((uint32_t)8ULL))) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_su30pqbigkn5m));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
    t3 = t10;
    goto me4;
mn8:;
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&t2));
    if (!(t12 == ((uint32_t)10ULL))) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sqhghq789nvm0));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
    t3 = t13;
    goto me4;
mn11:;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_safnb563bt6ue));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(11));
    t3 = t15;
    goto me4;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 507, 43);
me4:;
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t1 = gm_l0;
    uint64_t t2 = gm_l1;
    uint32_t t3 = (uint32_t)t2;
    uint64_t t4 = gm_l2;
    uint32_t t5 = (uint32_t)t4;
    gm_tl874t2atnlh3 t6;
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t1);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 4), t3);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_tg7cev6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)t1) + 0));
    uint64_t t3 = gm_l1;
    uint64_t t4 = gm_l2;
    gm_tl874t2atnlh3 t5 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t2, t3, t4));
    F.ret = t5;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) uint8_t gm_f_all_digits_e77c2k1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_digits_e77c2k1 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l1 = t3;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l2;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l2;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l3 = t17;
                    {
                        uint8_t t20 = gm_l3;
                        uint8_t t21 = gm_f_is_digit_2hoggnj(t20);
                        uint8_t t22 = (uint8_t)!t21;
                        if (!t22) goto ie19;
                        {
                            uint8_t t24 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 515, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t26 = gm_l2;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l2 = t29;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t30 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_exponent_qp4713f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_exponent_qp4713f F;
    void * gm_l0; /* lx */
    gm_b16_8 gm_l1; /* markers */
    uint8_t gm_l2; /* m */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_f_peek_72bostn(t2);
        gm_tqs5p4n2tq19v t4 = t3;
        uint8_t t5;
        if (((((uint8_t *)&t4))[1] != 0)) {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint8_t t9 = gm_l2;
        gm_b16_8 t10 = gm_l1;
        gm_b16_8 t11 = t10;
        uint8_t t12 = 0;
        uint8_t t13 = t9;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t11) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t11))) + i * 1, ((uint8_t *)&t13))) { t12 = 1; break; }
        uint8_t t14 = (uint8_t)(!t12);
        if (!t14) goto ie8;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        gm_l3 = ((uint64_t)1ULL);
    }
    {
        void * t18 = gm_l0;
        uint64_t t19 = gm_l3;
        gm_tqs5p4n2tq19v t20 = gm_f_peek_at_74p8lkq(t18, t19);
        gm_tqs5p4n2tq19v t21;
        GM_ST(uint8_t, ((uint8_t *)&t21), ((uint8_t)43ULL));
        ((uint8_t *)&t21)[1] = 1;
        gm_tqs5p4n2tq19v t22;
        GM_ST(uint8_t, ((uint8_t *)&t22), ((uint8_t)45ULL));
        ((uint8_t *)&t22)[1] = 1;
        gm_t5r6k0fdcfsb3 t23;
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t23 + 0), t21);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t23 + 2), t22);
        gm_t5r6k0fdcfsb3 t24 = t23;
        uint8_t t25 = 0;
        gm_tqs5p4n2tq19v t26 = t20;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t24) + 0, ((uint8_t *)&t26))) t25 = 1;
        if (gm_eqb44cb3cf1n(((uint8_t *)&t24) + 2, ((uint8_t *)&t26))) t25 = 1;
        uint8_t t27 = (uint8_t)(t25);
        if (!t27) goto ie17;
        {
            uint64_t t29 = gm_l3;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            if (o31) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 192, 38);
            uint64_t t32 = r30;
            gm_l3 = t32;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t33 = gm_l0;
        uint64_t t34 = gm_l3;
        gm_tqs5p4n2tq19v t35 = gm_f_peek_at_74p8lkq(t33, t34);
        gm_tqs5p4n2tq19v t36 = t35;
        uint8_t t37;
        if (((((uint8_t *)&t36))[1] != 0)) {
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t36) + 0));
            t37 = t38;
        } else {
            uint8_t t39 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l4 = t37;
    }
    {
        uint8_t t40 = gm_l4;
        uint8_t t41 = gm_f_is_digit_2hoggnj(t40);
        uint8_t t42 = 0;
        F.ret = t41;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_char_1ips8a1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2) {
    struct gm_fr_emit_char_1ips8a1 F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* value */
    uint64_t gm_l2; /* lo */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        memset((uint8_t *)&F.l3 + 61, 0, 3);
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3 + 0)), (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t46lmulm8gaj3 t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&F.l3 + 48, t4);
        GM_ST(uint8_t, (uint8_t *)&F.l3 + 60, ((uint8_t)1));
    }
    {
        void * t5 = gm_l0;
        gm_tq7s98ihonkju t6 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &(*(gm_ta42iqot1auae *)((uint8_t *)&F.l3)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdmv1ea52os)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srct4c97p4hek1p, 321, 18);
        void * t8 = GM_LD(void *, (((uint8_t *)&t7) + 8));
        gm_t3fcbuehh3bs2 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t9 + 8), t8);
        uint64_t t10 = gm_l2;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 24));
        gm_b0_1 t13 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t5, &t9, t10, t12);
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_float_suffix_lh8qbg0(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_suffix_lh8qbg0 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shnig5tqtdedi, 3) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sm9e81djuncnu, 3) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sqqajgod2bb4o, 3) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    gm_taari1s0m51a7 t13;
    memset(&t13, 0, sizeof t13);
    t1 = t13;
    goto me2;
mn12:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 556, 50);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_float_value_ip18r0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t2lqu8jsj511d a2, const void * a3, int64_t a4) {
    struct gm_fr_float_value_ip18r0p F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    gm_t2lqu8jsj511d gm_l2; /* int_part */
    gm_tc70a9dbbmgkg gm_l3; /* frac_part */
    int64_t gm_l4; /* exp */
    gm_t2lqu8jsj511d gm_l5; /* frac */
    gm_b16_8 gm_l6; /* frac_text */
    int64_t gm_l7; /* frac_digits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l10; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* c */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint8_t gm_l17; /* c */
    GM_STACK_CHECK(&F, 788u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    {
        gm_tc70a9dbbmgkg t2 = gm_l3;
        gm_tc70a9dbbmgkg t3 = t2;
        gm_t2lqu8jsj511d t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_t2lqu8jsj511d t5 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_t2lqu8jsj511d t6;
            GM_ST(uint64_t, ((uint8_t *)&t6 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t6 + 8), ((uint64_t)0ULL));
            t4 = t6;
        }
        gm_l5 = t4;
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)t7) + 8) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t7) + 8)));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        uint64_t t11 = (uintptr_t)t10;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t13 = (uintptr_t)t12;
        if (t11 > t13 || t13 > (uintptr_t)t8) gm_trap_slice(t11, t13, t8, gm_srct4c97p4hek1p, 238, 18);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t9 + t11 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t11));
        gm_l6 = t14;
    }
    {
        gm_l7 = ((int64_t)0LL);
    }
    {
        {
            gm_b16_8 t16 = gm_l6;
            gm_l8 = t16;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l9;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l9;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t24 * 1));
                    uint8_t t27 = t26;
                    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t27));
                    gm_l10 = t30;
                    {
                        uint8_t t33 = gm_l10;
                        uint8_t t34 = (!(t33 == ((uint8_t)95ULL)));
                        if (!t34) goto ie32;
                        {
                            int64_t t36 = gm_l7;
                            int64_t r37; bool o38 = __builtin_add_overflow(t36, ((int64_t)1LL), &r37);
                            if (o38) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 241, 27);
                            int64_t t39 = r37;
                            gm_l7 = t39;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 240, 5);
me28:;
                }
lc17:;
                {
                    uint64_t t41 = gm_l9;
                    uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
                    uint64_t t44 = r42;
                    gm_l9 = t44;
                }
            }
lb17:;
        }
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uintptr_t, (((uint8_t *)t46) + 8) + 8);
            void * t48 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t46) + 8)));
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
            uint64_t t50 = (uintptr_t)t49;
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t52 = (uintptr_t)t51;
            if (t50 > t52 || t52 > (uintptr_t)t47) gm_trap_slice(t50, t52, t47, gm_srct4c97p4hek1p, 244, 14);
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t48 + t50 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t50));
            gm_l12 = t53;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t55 = gm_l13;
                uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t57 = (t55 < t56);
                if (!t57) goto lb54;
                {
                    uint64_t t60 = gm_l13;
                    uint64_t t61 = (uintptr_t)t60;
                    uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t61 * 1));
                    uint8_t t64 = t63;
                    uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)&t64));
                    gm_l14 = t67;
                    {
                        uint8_t t70 = gm_l14;
                        uint8_t t71 = (!(t70 == ((uint8_t)95ULL)));
                        if (!t71) goto ie69;
                        {
                            void * t73 = (void *)((uint8_t *)&F.l11);
                            uint8_t t74 = gm_l14;
                            gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t73, t74);
                            gm_tftb1odc739t5 t76 = t75;
                            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srct4c97p4hek1p, 245, 27);
                            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                        }
                        goto ix69;
ie69:;
ix69:;
                    }
                    goto me65;
mn66:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 244, 5);
me65:;
                }
lc54:;
                {
                    uint64_t t79 = gm_l13;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l13 = t82;
                }
            }
lb54:;
        }
    }
    {
        {
            gm_b16_8 t84 = gm_l6;
            gm_l15 = t84;
        }
        {
            gm_l16 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t86 = gm_l16;
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t88 = (t86 < t87);
                if (!t88) goto lb85;
                {
                    uint64_t t91 = gm_l16;
                    uint64_t t92 = (uintptr_t)t91;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t94 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t92 * 1));
                    uint8_t t95 = t94;
                    uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)&t95));
                    gm_l17 = t98;
                    {
                        uint8_t t101 = gm_l17;
                        uint8_t t102 = (!(t101 == ((uint8_t)95ULL)));
                        if (!t102) goto ie100;
                        {
                            void * t104 = (void *)((uint8_t *)&F.l11);
                            uint8_t t105 = gm_l17;
                            gm_tftb1odc739t5 t106 = gm_f_push_t83rg2m(t104, t105);
                            gm_tftb1odc739t5 t107 = t106;
                            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srct4c97p4hek1p, 248, 27);
                            gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
                        }
                        goto ix100;
ie100:;
ix100:;
                    }
                    goto me96;
mn97:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 247, 5);
me96:;
                }
lc85:;
                {
                    uint64_t t110 = gm_l16;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l16 = t113;
                }
            }
lb85:;
        }
    }
    {
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t115 = t114;
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        uint32_t t117 = gm_l1;
        gm_f_parse_valid_9fjef3j((void *)&F.l18, (void *)NULL, cx, NULL, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t119 = gm_l1;
        uint8_t t120 = ((t119 == ((uint32_t)16ULL)));
        if (!t120) goto ie118;
        {
            int64_t t122 = gm_l4;
            int64_t t123 = gm_l7;
            int64_t r124; bool o125 = __builtin_mul_overflow(((int64_t)4LL), t123, &r124);
            if (o125) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 252, 61);
            int64_t t126 = r124;
            int64_t r127; bool o128 = __builtin_sub_overflow(t122, t126, &r127);
            if (o128) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 252, 55);
            int64_t t129 = r127;
            gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), ((uint32_t)2ULL), t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix118;
ie118:;
ix118:;
    }
    {
        int64_t t130 = gm_l4;
        int64_t t131 = gm_l7;
        int64_t r132; bool o133 = __builtin_sub_overflow(t130, t131, &r132);
        if (o133) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 253, 39);
        int64_t t134 = r132;
        gm_f_scaled_ufcgnm9((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), ((uint32_t)10ULL), t134);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fraction_follows_ivbj04h(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fraction_follows_ivbj04h F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* first_is_digit */
    uint64_t gm_l5; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 211u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t2;
        void * t4 = gm_l0;
        uint64_t t5 = gm_l2;
        gm_tqs5p4n2tq19v t6 = gm_f_byte_b4v8vul(t4, t5);
        gm_tqs5p4n2tq19v t7 = t6;
        if (!((((uint8_t *)&t7))[1] != 0)) goto ie3;
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
        gm_l3 = t8;
        uint8_t t10 = gm_l3;
        uint8_t t11 = gm_f_is_digit_2hoggnj(t10);
        uint8_t t12 = t11;
        t2 = t12;
        goto ix3;
ie3:;
        uint8_t t14 = ((uint8_t)0);
        t2 = t14;
ix3:;
        gm_l4 = t2;
    }
    {
        uint32_t t16 = gm_l1;
        uint8_t t17 = ((t16 == ((uint32_t)10ULL)));
        if (!t17) goto ie15;
        {
            uint8_t t19 = gm_l4;
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix15;
ie15:;
ix15:;
    }
    {
        uint64_t t21 = gm_l2;
        gm_l5 = t21;
    }
    {
        for (;;) {
            uint64_t t23 = gm_l5;
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)t24) + 8) + 8);
            uint8_t t26 = (t23 < t25);
            if (!t26) goto lb22;
            uint8_t t27 = 0;
            uint64_t t28 = gm_l5;
            uint64_t t29 = (uintptr_t)t28;
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)t30) + 8) + 8);
            if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srct4c97p4hek1p, 148, 50);
            uint8_t t32 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t30) + 8)) + t29 * 1));
            uint8_t t33 = gm_f_is_hex_digit_9lads51(t32);
            if (!t33) {
                uint64_t t34 = gm_l5;
                uint64_t t35 = (uintptr_t)t34;
                void * t36 = gm_l0;
                uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 8) + 8);
                if (t35 >= (uintptr_t)(t37)) gm_trap_index(t35, t37, gm_srct4c97p4hek1p, 148, 64);
                uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 8)) + t35 * 1));
                uint8_t t39 = ((t38 == ((uint8_t)95ULL)));
                t27 = t39;
            } else t27 = 1;
            if (!t27) goto lb22;
            {
                uint64_t t42 = gm_l5;
                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 148, 84);
                uint64_t t45 = r43;
                gm_l5 = t45;
            }
lc22:;
        }
lb22:;
    }
    {
        uint8_t t46 = 0;
        uint8_t t47 = gm_l4;
        if (!t47) {
            void * t48 = gm_l0;
            uint64_t t49 = gm_l5;
            gm_tqs5p4n2tq19v t50 = gm_f_byte_b4v8vul(t48, t49);
            gm_tqs5p4n2tq19v t51;
            GM_ST(uint8_t, ((uint8_t *)&t51), ((uint8_t)112ULL));
            ((uint8_t *)&t51)[1] = 1;
            gm_tqs5p4n2tq19v t52 = t50;
            gm_tqs5p4n2tq19v t53 = t51;
            uint8_t t54 = (gm_eqb44cb3cf1n(((uint8_t *)&t52), ((uint8_t *)&t53)));
            t46 = t54;
        } else t46 = 1;
        uint8_t t55 = 0;
        F.ret = t46;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_imag_width_9gtosab(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_imag_width_9gtosab F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 33u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 t1;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sb6jpc12sa7uj, 3) == 0)) goto mn3;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t5;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[1] = 1;
    t1 = t5;
    goto me2;
mn3:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_shq8kvmdjhmfg, 3) == 0)) goto mn6;
    gm_t9qkh0rsooc8d t7;
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1ULL));
    gm_taari1s0m51a7 t8;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t8), t7);
    ((uint8_t *)&t8)[1] = 1;
    t1 = t8;
    goto me2;
mn6:;
    if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s5k9ll19r7m4g, 3) == 0)) goto mn9;
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)2ULL));
    gm_taari1s0m51a7 t11;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
    ((uint8_t *)&t11)[1] = 1;
    t1 = t11;
    goto me2;
mn9:;
    gm_taari1s0m51a7 t13;
    memset(&t13, 0, sizeof t13);
    t1 = t13;
    goto me2;
mn12:;
    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 549, 48);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_suffix_j594l00(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_int_suffix_j594l00 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* digits */
    uint32_t gm_l2; /* width */
    uint8_t gm_l3; /* valid */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    uint32_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 1105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s6groq37ram3h, 5) == 0));
        if (!t3) goto ie2;
        {
            gm_tj2m9dbh1ekv6 t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
            gm_tij245bdr7emd t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
            ((uint8_t *)&t6)[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t6 + 8), t5);
            gm_tij245bdr7emd t7 = t6;
            uint8_t t8 = ((((uint8_t *)&t7))[0] != 0);
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t10 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_s0i3i86nphpsh, 5) == 0));
        if (!t10) goto ie9;
        {
            gm_tj2m9dbh1ekv6 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)3ULL));
            gm_tij245bdr7emd t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
            ((uint8_t *)&t13)[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t13 + 8), t12);
            gm_tij245bdr7emd t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t17 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
        if (t18 > (uintptr_t)t16 || (uintptr_t)t16 > (uintptr_t)t16) gm_trap_slice(t18, (uintptr_t)t16, t16, gm_srct4c97p4hek1p, 525, 15);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t17 + t18 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)((uintptr_t)t16 - t18));
        gm_l1 = t19;
    }
    {
        uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srct4c97p4hek1p, 526, 8);
        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t21 * 1));
        gm_tqs5p4n2tq19v t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t24 + 1), ((uint8_t)105ULL));
        gm_tqs5p4n2tq19v t25 = t24;
        uint8_t t26 = 0;
        uint8_t t27 = t23;
        if (gm_eqjmccqdb90f(((uint8_t *)&t25) + 0, ((uint8_t *)&t27))) t26 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t25) + 1, ((uint8_t *)&t27))) t26 = 1;
        uint8_t t28 = (uint8_t)(t26);
        if (!t28) goto ie20;
        uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t30 = (t29 > ((uint64_t)0ULL));
        if (!t30) goto ie20;
        gm_b16_8 t31 = gm_l1;
        uint8_t t32 = gm_f_all_digits_e77c2k1(t31);
        if (!t32) goto ie20;
        {
            gm_l2 = ((uint32_t)0ULL);
        }
        {
            uint8_t t34 = 0;
            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t36 = (t35 <= ((uint64_t)3ULL));
            if (t36) {
                uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_srct4c97p4hek1p, 528, 38);
                uint8_t t39 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t37 * 1));
                uint8_t t40 = (!(t39 == ((uint8_t)48ULL)));
                t34 = t40;
            } else t34 = 0;
            gm_l3 = t34;
        }
        {
            uint8_t t42 = gm_l3;
            if (!t42) goto ie41;
            {
                {
                    gm_b16_8 t45 = gm_l1;
                    gm_l4 = t45;
                }
                {
                    gm_l5 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t47 = gm_l5;
                        uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t49 = (t47 < t48);
                        if (!t49) goto lb46;
                        {
                            uint64_t t52 = gm_l5;
                            uint64_t t53 = (uintptr_t)t52;
                            uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            uint8_t t55 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t53 * 1));
                            uint8_t t56 = t55;
                            uint8_t t59 = GM_LD(uint8_t, ((uint8_t *)&t56));
                            gm_l6 = t59;
                            {
                                uint32_t t61 = gm_l2;
                                uint32_t r62; bool o63 = __builtin_mul_overflow(t61, ((uint32_t)10ULL), &r62);
                                if (o63) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 530, 39);
                                uint32_t t64 = r62;
                                uint8_t t65 = gm_l6;
                                uint8_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint8_t)48ULL), &r66);
                                if (o67) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 530, 56);
                                uint8_t t68 = r66;
                                uint32_t t69 = (uint32_t)t68;
                                uint32_t r70; bool o71 = __builtin_add_overflow(t64, t69, &r70);
                                if (o71) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 530, 39);
                                uint32_t t72 = r70;
                                gm_l2 = t72;
                            }
                            goto me57;
mn58:;
                            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 530, 13);
me57:;
                        }
lc46:;
                        {
                            uint64_t t74 = gm_l5;
                            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                            uint64_t t77 = r75;
                            gm_l5 = t77;
                        }
                    }
lb46:;
                }
            }
            {
                {
                    uint32_t t79 = gm_l2;
                    gm_l7 = t79;
                }
                uint8_t t80 = 0;
                uint32_t t81 = gm_l7;
                uint8_t t82 = (((uint32_t)1ULL) <= t81);
                if (t82) {
                    uint32_t t83 = gm_l7;
                    uint8_t t84 = (t83 <= ((uint32_t)128ULL));
                    t80 = t84;
                } else t80 = 0;
                uint8_t t85 = t80;
                gm_l3 = t85;
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            uint8_t t87 = gm_l3;
            uint8_t t88 = (uint8_t)!t87;
            if (!t88) goto ie86;
            {
                gm_b16_8 t90 = gm_l0;
                gm_tnpifv20irnph t91;
                GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
                gm_tabb710d9mq2e t92;
                gm_f_format_1kivsu4((void *)&t92, (void *)NULL, cx, NULL, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t93 = t92;
                if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 8, gm_srct4c97p4hek1p, 534, 21);
                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t93) + 8));
                gm_b16_8 t95 = t94;
                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                gm_tij245bdr7emd t97;
                GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                ((uint8_t *)&t97)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t97 + 8), t96);
                gm_tij245bdr7emd t98 = t97;
                uint8_t t99 = ((((uint8_t *)&t98))[0] != 0);
                F.ret = t97;
                goto gm_fret;
            }
            goto ix86;
ie86:;
ix86:;
        }
        {
            uint64_t t101 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srct4c97p4hek1p, 536, 12);
            uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t101 * 1));
            uint8_t t104 = ((t103 == ((uint8_t)117ULL)));
            if (!t104) goto ie100;
            {
                uint32_t t106 = gm_l2;
                gm_tj2m9dbh1ekv6 t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0ULL));
                GM_ST(uint32_t, ((uint8_t *)&t107 + 4), t106);
                gm_tij245bdr7emd t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t108 + 16, 0);
                ((uint8_t *)&t108)[0] = 0;
                GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t108 + 8), t107);
                gm_tij245bdr7emd t109 = t108;
                uint8_t t110 = ((((uint8_t *)&t109))[0] != 0);
                F.ret = t108;
                goto gm_fret;
            }
            goto ix100;
ie100:;
ix100:;
        }
        {
            uint32_t t111 = gm_l2;
            gm_tj2m9dbh1ekv6 t112;
            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t112 + 4), t111);
            gm_tij245bdr7emd t113;
            GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t113 + 16, 0);
            ((uint8_t *)&t113)[0] = 0;
            GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t113 + 8), t112);
            gm_tij245bdr7emd t114 = t113;
            uint8_t t115 = ((((uint8_t *)&t114))[0] != 0);
            F.ret = t113;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        gm_b16_8 t117 = gm_l0;
        gm_taari1s0m51a7 t118 = gm_f_float_suffix_lh8qbg0(t117);
        gm_taari1s0m51a7 t119 = t118;
        uint8_t t120;
        if (((((uint8_t *)&t119))[1] != 0)) goto mn122;
        t120 = ((uint8_t)0);
        goto me121;
mn122:;
        t120 = ((uint8_t)1);
        goto me121;
mn123:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 539, 8);
me121:;
        if (!t120) goto ie116;
        {
            gm_b16_8 t125 = gm_l0;
            gm_tnpifv20irnph t126;
            GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t125);
            gm_tabb710d9mq2e t127;
            gm_f_format_3rm05av((void *)&t127, (void *)NULL, cx, NULL, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t128 = t127;
            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 8, gm_srct4c97p4hek1p, 540, 17);
            gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&t128) + 8));
            gm_b16_8 t130 = t129;
            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
            gm_tij245bdr7emd t132;
            GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
            ((uint8_t *)&t132)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t132 + 8), t131);
            gm_tij245bdr7emd t133 = t132;
            uint8_t t134 = ((((uint8_t *)&t133))[0] != 0);
            F.ret = t132;
            goto gm_fret;
        }
        goto ix116;
ie116:;
ix116:;
    }
    {
        gm_b16_8 t135 = gm_l0;
        gm_tnpifv20irnph t136;
        GM_ST(gm_b16_8, ((uint8_t *)&t136 + 0), t135);
        gm_tabb710d9mq2e t137;
        gm_f_format_16f92bp((void *)&t137, (void *)NULL, cx, NULL, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t138 = t137;
        if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 8, gm_srct4c97p4hek1p, 542, 13);
        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&t138) + 8));
        gm_b16_8 t140 = t139;
        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
        gm_tij245bdr7emd t142;
        GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
        ((uint8_t *)&t142)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t142 + 8), t141);
        gm_tij245bdr7emd t143 = t142;
        uint8_t t144 = ((((uint8_t *)&t143))[0] != 0);
        F.ret = t142;
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
extern __attribute__((aligned(4))) void gm_f_int_value_cnmrbr4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t2lqu8jsj511d a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_imag_suffix_46gu23m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_imag_suffix_46gu23m F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 244u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_t0uuhaljsf410 t2;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sikg061j3im7q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sb6jpc12sa7uj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t4);
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_shq8kvmdjhmfg));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 32, t5);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s5k9ll19r7m4g));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 48, t6);
    gm_t0uuhaljsf410 t7 = t2;
    uint8_t t8 = 0;
    gm_b16_8 t9 = t1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 0, ((uint8_t *)&t9))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 16, ((uint8_t *)&t9))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 32, ((uint8_t *)&t9))) t8 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t7) + 48, ((uint8_t *)&t9))) t8 = 1;
    uint8_t t10 = (uint8_t)(t8);
    F.ret = t10;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_char_4qk4vjp F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint32_t gm_l2; /* value */
    uint8_t gm_l3; /* b */
    uint32_t gm_l4; /* c */
    uint8_t gm_l5; /* x */
    gm_tb31k5l794t7p gm_l6; /* c */
    GM_STACK_CHECK(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 261, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        gm_l2 = ((uint32_t)0ULL);
    }
    {
        void * t10 = gm_l0;
        gm_tqs5p4n2tq19v t11 = gm_f_peek_72bostn(t10);
        gm_tqs5p4n2tq19v t12 = t11;
        uint8_t t13;
        if (((((uint8_t *)&t12))[1] != 0)) {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            {
                void * t16 = gm_l0;
                uint64_t t17 = gm_l1;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 24));
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sq9ngpqr0m7ek));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(30));
                gm_b0_1 t21 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t16, t17, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t22 = gm_l0;
                uint64_t t23 = gm_l1;
                gm_b0_1 t24 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t22, ((uint32_t)0ULL), t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t25;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        gm_l3 = t13;
    }
    {
        uint8_t t28 = gm_l3;
        gm_tqs5p4n2tq19v t29;
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)10ULL));
        GM_ST(uint8_t, ((uint8_t *)&t29 + 1), ((uint8_t)13ULL));
        gm_tqs5p4n2tq19v t30 = t29;
        uint8_t t31 = 0;
        uint8_t t32 = t28;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 0, ((uint8_t *)&t32))) t31 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t30) + 1, ((uint8_t *)&t32))) t31 = 1;
        uint8_t t33 = (uint8_t)(t31);
        if (!t33) goto ie27;
        {
            void * t35 = gm_l0;
            uint64_t t36 = gm_l1;
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 24));
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sq9ngpqr0m7ek));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(30));
            gm_b0_1 t40 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t35, t36, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t41 = gm_l0;
            uint64_t t42 = gm_l1;
            gm_b0_1 t43 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t41, ((uint32_t)0ULL), t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t44;
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        uint8_t t47 = gm_l3;
        uint8_t t48 = ((t47 == ((uint8_t)39ULL)));
        if (!t48) goto ie46;
        {
            void * t50 = gm_l0;
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 24));
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 274, 9);
            uint64_t t55 = r53;
            GM_ST(uint64_t, (((uint8_t *)t50) + 24), t55);
        }
        {
            void * t56 = gm_l0;
            uint64_t t57 = gm_l1;
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 24));
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_skmai4uvseje8));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(23));
            gm_b0_1 t61 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t56, t57, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t62 = gm_l0;
            uint64_t t63 = gm_l1;
            gm_b0_1 t64 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t62, ((uint32_t)0ULL), t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t65;
            uint8_t t66 = 0;
            F.ret = t65;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        uint8_t t68 = gm_l3;
        uint8_t t69 = ((t68 == ((uint8_t)92ULL)));
        if (!t69) goto ie67;
        {
            void * t71 = gm_l0;
            gm_ts06tb4mldv43 t72 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ts06tb4mldv43 t73 = t72;
            if (!((((uint8_t *)&t73))[8] != 0)) goto mn75;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
            if (t76 != ((uint8_t)0ULL)) goto mn75;
            uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)&t73) + 4));
            gm_l4 = t77;
            {
                uint32_t t79 = gm_l4;
                gm_l2 = t79;
            }
            goto me74;
mn75:;
            if (!((((uint8_t *)&t73))[8] != 0)) goto mn80;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
            if (t81 != ((uint8_t)1ULL)) goto mn80;
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t73) + 4));
            gm_l5 = t82;
            {
                uint8_t t84 = gm_l5;
                uint32_t t85 = (uint32_t)t84;
                gm_l2 = t85;
            }
            goto me74;
mn80:;
            if (((((uint8_t *)&t73))[8] != 0)) goto mn86;
            {
                {
                    void * t89 = gm_l0;
                    uint8_t t90 = gm_f_skip_to_closing_quote_qmb6ii3(t89);
                }
            }
            {
                void * t91 = gm_l0;
                uint64_t t92 = gm_l1;
                gm_b0_1 t93 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t91, ((uint32_t)0ULL), t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto me74;
mn86:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 280, 9);
me74:;
        }
        goto ix67;
ie67:;
        {
            void * t97 = gm_l0;
            gm_tb31k5l794t7p t98 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t97));
            gm_l6 = t98;
        }
        {
            void * t99 = gm_l0;
            void * t100 = gm_l0;
            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t100) + 24));
            uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t r103; bool o104 = __builtin_add_overflow(t101, t102, &r103);
            if (o104) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 291, 9);
            uint64_t t105 = r103;
            GM_ST(uint64_t, (((uint8_t *)t99) + 24), t105);
        }
        {
            uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
            gm_l2 = t106;
        }
ix67:;
    }
    {
        void * t108 = gm_l0;
        gm_tqs5p4n2tq19v t109 = gm_f_peek_72bostn(t108);
        gm_tqs5p4n2tq19v t110;
        GM_ST(uint8_t, ((uint8_t *)&t110), ((uint8_t)39ULL));
        ((uint8_t *)&t110)[1] = 1;
        gm_tqs5p4n2tq19v t111 = t109;
        gm_tqs5p4n2tq19v t112 = t110;
        uint8_t t113 = (gm_eqb44cb3cf1n(((uint8_t *)&t111), ((uint8_t *)&t112)));
        if (!t113) goto ie107;
        {
            void * t115 = gm_l0;
            void * t116 = gm_l0;
            uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)t116) + 24));
            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
            if (o119) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 295, 9);
            uint64_t t120 = r118;
            GM_ST(uint64_t, (((uint8_t *)t115) + 24), t120);
        }
        goto ix107;
ie107:;
        void * t122 = gm_l0;
        uint8_t t123 = gm_f_skip_to_closing_quote_qmb6ii3(t122);
        if (!t123) goto ie121;
        {
            void * t125 = gm_l0;
            uint64_t t126 = gm_l1;
            void * t127 = gm_l0;
            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t127) + 24));
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_saoa4m7dh2vl3));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(54));
            gm_b0_1 t130 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t125, t126, t128, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix121;
ie121:;
        {
            void * t132 = gm_l0;
            uint64_t t133 = gm_l1;
            void * t134 = gm_l0;
            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)t134) + 24));
            gm_b16_8 t136;
            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sq9ngpqr0m7ek));
            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(30));
            gm_b0_1 t137 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t132, t133, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix121:;
ix107:;
    }
    {
        void * t138 = gm_l0;
        uint32_t t139 = gm_l2;
        uint64_t t140 = gm_l1;
        gm_b0_1 t141 = gm_f_emit_char_1ips8a1((void *)NULL, cx, NULL, t138, t139, t140);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_escape_7pnp2v0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_escape_7pnp2v0 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* b */
    gm_tqs5p4n2tq19v gm_l3; /* simple */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* hi */
    uint8_t gm_l6; /* low */
    gm_tb31k5l794t7p gm_l7; /* c */
    GM_STACK_CHECK(&F, 946u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
        uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
        if (o8) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 331, 5);
        uint64_t t9 = r7;
        GM_ST(uint64_t, (((uint8_t *)t4) + 24), t9);
    }
    {
        void * t10 = gm_l0;
        gm_tqs5p4n2tq19v t11 = gm_f_peek_72bostn(t10);
        gm_tqs5p4n2tq19v t12 = t11;
        uint8_t t13;
        if (((((uint8_t *)&t12))[1] != 0)) {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
            t13 = t14;
        } else {
            gm_ts06tb4mldv43 t15;
            memset(&t15, 0, sizeof t15);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l2 = t13;
    }
    {
        uint8_t t17 = gm_l2;
        uint8_t t18 = t17;
        gm_tqs5p4n2tq19v t19;
        uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t22 == ((uint8_t)110ULL))) goto mn21;
        gm_tqs5p4n2tq19v t23;
        GM_ST(uint8_t, ((uint8_t *)&t23), ((uint8_t)10ULL));
        ((uint8_t *)&t23)[1] = 1;
        t19 = t23;
        goto me20;
mn21:;
        uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t25 == ((uint8_t)114ULL))) goto mn24;
        gm_tqs5p4n2tq19v t26;
        GM_ST(uint8_t, ((uint8_t *)&t26), ((uint8_t)13ULL));
        ((uint8_t *)&t26)[1] = 1;
        t19 = t26;
        goto me20;
mn24:;
        uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t28 == ((uint8_t)116ULL))) goto mn27;
        gm_tqs5p4n2tq19v t29;
        GM_ST(uint8_t, ((uint8_t *)&t29), ((uint8_t)9ULL));
        ((uint8_t *)&t29)[1] = 1;
        t19 = t29;
        goto me20;
mn27:;
        uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t31 == ((uint8_t)48ULL))) goto mn30;
        gm_tqs5p4n2tq19v t32;
        GM_ST(uint8_t, ((uint8_t *)&t32), ((uint8_t)0ULL));
        ((uint8_t *)&t32)[1] = 1;
        t19 = t32;
        goto me20;
mn30:;
        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t36 == ((uint8_t)92ULL))) goto pa35;
        goto po34;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t38 == ((uint8_t)34ULL))) goto pa37;
        goto po34;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t18));
        if (!(t40 == ((uint8_t)39ULL))) goto pa39;
        goto po34;
pa39:;
        goto mn33;
po34:;
        uint8_t t41 = gm_l2;
        gm_tqs5p4n2tq19v t42;
        GM_ST(uint8_t, ((uint8_t *)&t42), t41);
        ((uint8_t *)&t42)[1] = 1;
        t19 = t42;
        goto me20;
mn33:;
        gm_tqs5p4n2tq19v t44;
        memset(&t44, 0, sizeof t44);
        t19 = t44;
        goto me20;
mn43:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 333, 19);
me20:;
        gm_l3 = t19;
    }
    {
        if (!((((uint8_t *)&gm_l3))[1] != 0)) goto ie45;
        uint8_t t46 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l4 = t46;
        {
            void * t48 = gm_l0;
            void * t49 = gm_l0;
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 24));
            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
            if (o52) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 342, 9);
            uint64_t t53 = r51;
            GM_ST(uint64_t, (((uint8_t *)t48) + 24), t53);
        }
        {
            uint8_t t54 = gm_l4;
            uint32_t t55 = (uint32_t)t54;
            gm_tum2strs1ckr2 t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
            GM_ST(uint32_t, ((uint8_t *)&t56 + 4), t55);
            gm_ts06tb4mldv43 t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 10, 0);
            GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t57), t56);
            ((uint8_t *)&t57)[8] = 1;
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        uint8_t t59 = gm_l2;
        uint8_t t60 = t59;
        uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)&t60));
        if (!(t63 == ((uint8_t)120ULL))) goto mn62;
        {
            void * t65 = gm_l0;
            void * t66 = gm_l0;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 24));
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
            if (o69) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 347, 13);
            uint64_t t70 = r68;
            GM_ST(uint64_t, (((uint8_t *)t65) + 24), t70);
        }
        {
            void * t71 = gm_l0;
            gm_tqs5p4n2tq19v t72 = gm_f_peek_72bostn(t71);
            gm_tqs5p4n2tq19v t73 = t72;
            uint8_t t74;
            if (((((uint8_t *)&t73))[1] != 0)) {
                uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
                t74 = t75;
            } else {
                t74 = ((uint8_t)0ULL);
            }
            gm_l5 = t74;
        }
        {
            void * t76 = gm_l0;
            gm_tqs5p4n2tq19v t77 = gm_f_peek_at_74p8lkq(t76, ((uint64_t)1ULL));
            gm_tqs5p4n2tq19v t78 = t77;
            uint8_t t79;
            if (((((uint8_t *)&t78))[1] != 0)) {
                uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
                t79 = t80;
            } else {
                t79 = ((uint8_t)0ULL);
            }
            gm_l6 = t79;
        }
        {
            uint8_t t82 = gm_l5;
            uint8_t t83 = gm_f_is_hex_digit_9lads51(t82);
            if (!t83) goto ie81;
            uint8_t t84 = gm_l6;
            uint8_t t85 = gm_f_is_hex_digit_9lads51(t84);
            if (!t85) goto ie81;
            {
                void * t87 = gm_l0;
                void * t88 = gm_l0;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 24));
                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)2ULL), &r90);
                if (o91) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 351, 17);
                uint64_t t92 = r90;
                GM_ST(uint64_t, (((uint8_t *)t87) + 24), t92);
            }
            {
                uint8_t t93 = gm_l5;
                uint32_t t94 = gm_f_digit_value_8ogbrqp(t93);
                uint32_t r95; bool o96 = __builtin_mul_overflow(t94, ((uint32_t)16ULL), &r95);
                if (o96) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 352, 31);
                uint32_t t97 = r95;
                uint8_t t98 = gm_l6;
                uint32_t t99 = gm_f_digit_value_8ogbrqp(t98);
                uint32_t r100; bool o101 = __builtin_add_overflow(t97, t99, &r100);
                if (o101) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 352, 31);
                uint32_t t102 = r100;
                uint8_t t103 = (uint8_t)t102;
                gm_tum2strs1ckr2 t104;
                GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                GM_ST(uint8_t, (uint8_t *)&t104 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t104 + 6, 0);
                GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)1ULL));
                GM_ST(uint8_t, ((uint8_t *)&t104 + 4), t103);
                gm_ts06tb4mldv43 t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t105), t104);
                ((uint8_t *)&t105)[8] = 1;
                uint8_t t106 = 0;
                F.ret = t105;
                goto gm_fret;
            }
            goto ix81;
ie81:;
ix81:;
        }
        {
            for (;;) {
                void * t108 = gm_l0;
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 24));
                uint64_t t110 = gm_l1;
                uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)4ULL), &r111);
                if (o112) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 28);
                uint64_t t113 = r111;
                uint8_t t114 = (t109 < t113);
                if (!t114) goto lb107;
                void * t115 = gm_l0;
                uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)t115) + 24));
                void * t117 = gm_l0;
                uint64_t t118 = GM_LD(uintptr_t, (((uint8_t *)t117) + 8) + 8);
                uint8_t t119 = (t116 < t118);
                if (!t119) goto lb107;
                void * t120 = gm_l0;
                uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)t120) + 24));
                uint64_t t122 = (uintptr_t)t121;
                void * t123 = gm_l0;
                uint64_t t124 = GM_LD(uintptr_t, (((uint8_t *)t123) + 8) + 8);
                if (t122 >= (uintptr_t)(t124)) gm_trap_index(t122, t124, gm_srct4c97p4hek1p, 354, 82);
                uint8_t t125 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t123) + 8)) + t122 * 1));
                uint8_t t126 = gm_f_is_hex_digit_9lads51(t125);
                if (!t126) goto lb107;
                {
                    void * t129 = gm_l0;
                    void * t130 = gm_l0;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)t130) + 24));
                    uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                    if (o133) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 354, 100);
                    uint64_t t134 = r132;
                    GM_ST(uint64_t, (((uint8_t *)t129) + 24), t134);
                }
lc107:;
            }
lb107:;
        }
        {
            void * t135 = gm_l0;
            uint64_t t136 = gm_l1;
            void * t137 = gm_l0;
            uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)t137) + 24));
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_smit2phoauonm));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(60));
            gm_b0_1 t140 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t135, t136, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t141;
            memset(&t141, 0, sizeof t141);
            uint8_t t142 = 0;
            F.ret = t141;
            goto gm_fret;
        }
        goto me61;
mn62:;
        uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)&t60));
        if (!(t144 == ((uint8_t)117ULL))) goto mn143;
        {
            void * t146 = gm_l0;
            void * t147 = gm_l0;
            uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)t147) + 24));
            uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
            if (o150) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 359, 13);
            uint64_t t151 = r149;
            GM_ST(uint64_t, (((uint8_t *)t146) + 24), t151);
        }
        {
            void * t152 = gm_l0;
            uint64_t t153 = gm_l1;
            gm_ts06tb4mldv43 t154 = gm_f_lex_unicode_escape_ns81oou((void *)NULL, cx, NULL, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t155 = 0;
            F.ret = t154;
            goto gm_fret;
        }
        goto me61;
mn143:;
        uint8_t t159 = GM_LD(uint8_t, ((uint8_t *)&t60));
        if (!(t159 == ((uint8_t)10ULL))) goto pa158;
        goto po157;
pa158:;
        uint8_t t161 = GM_LD(uint8_t, ((uint8_t *)&t60));
        if (!(t161 == ((uint8_t)13ULL))) goto pa160;
        goto po157;
pa160:;
        goto mn156;
po157:;
        gm_ts06tb4mldv43 t162;
        memset(&t162, 0, sizeof t162);
        uint8_t t163 = 0;
        F.ret = t162;
        goto gm_fret;
        goto me61;
mn156:;
        {
            void * t166 = gm_l0;
            gm_tb31k5l794t7p t167 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t166));
            gm_l7 = t167;
        }
        {
            void * t168 = gm_l0;
            void * t169 = gm_l0;
            uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)t169) + 24));
            uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t r172; bool o173 = __builtin_add_overflow(t170, t171, &r172);
            if (o173) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 365, 13);
            uint64_t t174 = r172;
            GM_ST(uint64_t, (((uint8_t *)t168) + 24), t174);
        }
        {
            void * t175 = gm_l0;
            uint64_t t176 = gm_l1;
            void * t177 = gm_l0;
            uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)t177) + 24));
            void * t179 = gm_l0;
            uint64_t t180 = GM_LD(uintptr_t, (((uint8_t *)t179) + 8) + 8);
            void * t181 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t179) + 8)));
            uint64_t t182 = gm_l1;
            uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
            if (o184) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 366, 86);
            uint64_t t185 = r183;
            uint64_t t186 = (uintptr_t)t185;
            void * t187 = gm_l0;
            uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)t187) + 24));
            uint64_t t189 = (uintptr_t)t188;
            if (t186 > t189 || t189 > (uintptr_t)t180) gm_trap_slice(t186, t189, t180, gm_srct4c97p4hek1p, 366, 79);
            gm_b16_8 t190;
            GM_ST(void *, (uint8_t *)&t190, (void *)(((uint8_t *)t181 + t186 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(t189 - t186));
            gm_tnpifv20irnph t191;
            GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t190);
            gm_tabb710d9mq2e t192;
            gm_f_format_eue6enu((void *)&t192, (void *)NULL, cx, NULL, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t193 = t192;
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_srct4c97p4hek1p, 366, 34);
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
            gm_b16_8 t195 = t194;
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            gm_b0_1 t197 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t175, t176, t178, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t198;
            memset(&t198, 0, sizeof t198);
            uint8_t t199 = 0;
            F.ret = t198;
            goto gm_fret;
        }
        goto me61;
mn164:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 345, 5);
me61:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lex_field_index_pf905g6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_field_index_pf905g6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        for (;;) {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 8) + 8);
            uint8_t t7 = (t4 < t6);
            if (!t7) goto lb2;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 24));
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 8) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srct4c97p4hek1p, 154, 50);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 8)) + t10 * 1));
            uint8_t t14 = gm_f_is_digit_2hoggnj(t13);
            if (!t14) goto lb2;
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 24));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 154, 68);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 24), t22);
            }
lc2:;
        }
lb2:;
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
        uint64_t t26 = gm_l1;
        uint64_t r27; bool o28 = __builtin_sub_overflow(t25, t26, &r27);
        if (o28) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 155, 8);
        uint64_t t29 = r27;
        uint8_t t30 = (t29 > ((uint64_t)1ULL));
        if (!t30) goto ie23;
        uint64_t t31 = gm_l1;
        uint64_t t32 = (uintptr_t)t31;
        void * t33 = gm_l0;
        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 8) + 8);
        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_srct4c97p4hek1p, 155, 28);
        uint8_t t35 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 8)) + t32 * 1));
        uint8_t t36 = ((t35 == ((uint8_t)48ULL)));
        if (!t36) goto ie23;
        {
            void * t38 = gm_l0;
            uint64_t t39 = gm_l1;
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 24));
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sqj9769nr8pm0));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(47));
            gm_b0_1 t43 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t38, t39, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t44 = gm_l0;
        uint64_t t45 = gm_l1;
        void * t46 = gm_l0;
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 24));
        gm_t2lqu8jsj511d t48;
        GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t45);
        GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
        gm_f_int_value_cnmrbr4((void *)&F.l2, (void *)NULL, cx, NULL, t44, t48, ((uint32_t)10ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        gm_ta42iqot1auae t50;
        memset((uint8_t *)&t50 + 61, 0, 3);
        memcpy((uint8_t *)&t50 + 0, ((uint8_t *)&F.l2), 48);
        gm_t46lmulm8gaj3 t51;
        memset(&t51, 0, sizeof t51);
        GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&t50 + 48, t51);
        GM_ST(uint8_t, (uint8_t *)&t50 + 60, ((uint8_t)0));
        gm_tq7s98ihonkju t52 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t50, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khsmm7kmq9o)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t53 = t52;
        if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srct4c97p4hek1p, 159, 18);
        void * t54 = GM_LD(void *, (((uint8_t *)&t53) + 8));
        gm_t3fcbuehh3bs2 t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)3ULL));
        GM_ST(void *, ((uint8_t *)&t55 + 8), t54);
        uint64_t t56 = gm_l1;
        void * t57 = gm_l0;
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 24));
        gm_b0_1 t59 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t49, &t55, t56, t58);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_number_9j9sdfi F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* n */
    gm_th2lqbo77pvsc gm_l3; /* err */
    gm_th2lqbo77pvsc gm_l4; /* prefix_fix */
    uint32_t gm_l5; /* radix */
    uint8_t gm_l6; /* c */
    uint8_t gm_l7; /* lower */
    gm_b16_8 gm_l8; /* letter */
    gm_t2lqu8jsj511d gm_l9; /* int_part */
    gm_tc70a9dbbmgkg gm_l10; /* frac_part */
    gm_t913bb735pvt5 gm_l11; /* exp */
    uint8_t gm_l12; /* float_by_suffix */
    uint64_t gm_l13; /* at */
    uint8_t gm_l14; /* is_float */
    gm_b16_8 gm_l15; /* suffix */
    uint8_t gm_l16; /* imag */
    gm_t46lmulm8gaj3 gm_l17; /* int_sfx */
    gm_taari1s0m51a7 gm_l18; /* float_sfx */
    gm_b16_8 gm_l19; /* message */
    gm_tj2m9dbh1ekv6 gm_l20; /* s */
    gm_b16_8 gm_l21; /* message */
    gm_b16_8 gm_l23; /* p */
    GM_STACK_CHECK(&F, 3325u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
        gm_l2 = t5;
    }
    {
        uint64_t t7 = gm_l2;
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie6;
        uint64_t t9 = gm_l2;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 33, 34);
        uint64_t t12 = r10;
        uint64_t t13 = (uintptr_t)t12;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 40) + 0) + 8);
        if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_srct4c97p4hek1p, 33, 18);
        gm_t3fcbuehh3bs2 t16 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 40) + 0)) + t13 * 48) + 0));
        gm_t3fcbuehh3bs2 t17 = t16;
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)58ULL));
        gm_t9qkh0rsooc8d t19 = t18;
        uint8_t t20 = (((GM_LD(uint8_t, ((uint8_t *)&t17) + 0) == ((uint8_t)8ULL)) && gm_eqv3tj1pu43g((((uint8_t *)&t17) + 8), ((uint8_t *)&t19))));
        if (!t20) goto ie6;
        {
            void * t22 = gm_l0;
            uint64_t t23 = gm_l1;
            gm_b0_1 t24 = gm_f_lex_field_index_pf905g6((void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t25;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        gm_th2lqbo77pvsc t27;
        memset(&t27, 0, sizeof t27);
        gm_l3 = t27;
    }
    {
        gm_th2lqbo77pvsc t28;
        memset(&t28, 0, sizeof t28);
        gm_l4 = t28;
    }
    {
        gm_l5 = ((uint32_t)10ULL);
    }
    {
        uint64_t t30 = gm_l1;
        uint64_t t31 = (uintptr_t)t30;
        void * t32 = gm_l0;
        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 8) + 8);
        if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srct4c97p4hek1p, 42, 8);
        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t32) + 8)) + t31 * 1));
        uint8_t t35 = ((t34 == ((uint8_t)48ULL)));
        if (!t35) goto ie29;
        {
            void * t38 = gm_l0;
            gm_tqs5p4n2tq19v t39 = gm_f_peek_at_74p8lkq(t38, ((uint64_t)1ULL));
            gm_tqs5p4n2tq19v t40 = t39;
            if (!((((uint8_t *)&t40))[1] != 0)) goto ie37;
            uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)&t40));
            gm_l6 = t41;
            {
                uint8_t t44 = gm_l6;
                gm_t4q2b5ibsrdik t45;
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)120ULL));
                GM_ST(uint8_t, ((uint8_t *)&t45 + 1), ((uint8_t)111ULL));
                GM_ST(uint8_t, ((uint8_t *)&t45 + 2), ((uint8_t)98ULL));
                GM_ST(uint8_t, ((uint8_t *)&t45 + 3), ((uint8_t)88ULL));
                GM_ST(uint8_t, ((uint8_t *)&t45 + 4), ((uint8_t)79ULL));
                GM_ST(uint8_t, ((uint8_t *)&t45 + 5), ((uint8_t)66ULL));
                gm_t4q2b5ibsrdik t46 = t45;
                uint8_t t47 = 0;
                uint8_t t48 = t44;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 0, ((uint8_t *)&t48))) t47 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 1, ((uint8_t *)&t48))) t47 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 2, ((uint8_t *)&t48))) t47 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 3, ((uint8_t *)&t48))) t47 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 4, ((uint8_t *)&t48))) t47 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t46) + 5, ((uint8_t *)&t48))) t47 = 1;
                uint8_t t49 = (uint8_t)(t47);
                if (!t49) goto ie43;
                {
                    uint8_t t51 = gm_l6;
                    uint8_t t52 = (uint8_t)(t51 | ((uint8_t)32ULL));
                    gm_l7 = t52;
                }
                {
                    uint8_t t54 = gm_l6;
                    uint8_t t55 = (t54 < ((uint8_t)97ULL));
                    if (!t55) goto ie53;
                    {
                        uint8_t t57 = gm_l7;
                        uint8_t t58 = t57;
                        gm_b16_8 t59;
                        uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)&t58));
                        if (!(t62 == ((uint8_t)120ULL))) goto mn61;
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_se4629k8oa1uo));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
                        t59 = t63;
                        goto me60;
mn61:;
                        uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)&t58));
                        if (!(t65 == ((uint8_t)111ULL))) goto mn64;
                        gm_b16_8 t66;
                        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_se0f2jka437t4));
                        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
                        t59 = t66;
                        goto me60;
mn64:;
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s50pmptf3idds));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
                        t59 = t68;
                        goto me60;
mn67:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 47, 31);
me60:;
                        gm_l8 = t59;
                    }
                    {
                        gm_b16_8 t69 = gm_l8;
                        gm_tnpifv20irnph t70;
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                        gm_tabb710d9mq2e t71;
                        gm_f_format_g6sh9k2((void *)&t71, (void *)NULL, cx, NULL, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t72 = t71;
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srct4c97p4hek1p, 52, 27);
                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                        gm_b16_8 t74 = t73;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        gm_th2lqbo77pvsc t76;
                        GM_ST(uint8_t, (uint8_t *)&t76 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t76 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t76 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t76), t75);
                        ((uint8_t *)&t76)[16] = 1;
                        gm_l3 = t76;
                    }
                    {
                        gm_b16_8 t77 = gm_l8;
                        gm_tnpifv20irnph t78;
                        GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t77);
                        gm_tabb710d9mq2e t79;
                        gm_f_format_78j0ecp((void *)&t79, (void *)NULL, cx, NULL, t78);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t80 = t79;
                        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_srct4c97p4hek1p, 53, 34);
                        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                        gm_b16_8 t82 = t81;
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                        gm_th2lqbo77pvsc t84;
                        GM_ST(uint8_t, (uint8_t *)&t84 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t84 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t84 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t84), t83);
                        ((uint8_t *)&t84)[16] = 1;
                        gm_l4 = t84;
                    }
                    goto ix53;
ie53:;
ix53:;
                }
                {
                    void * t85 = gm_l0;
                    void * t86 = gm_l0;
                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 24));
                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)2ULL), &r88);
                    if (o89) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 55, 17);
                    uint64_t t90 = r88;
                    GM_ST(uint64_t, (((uint8_t *)t85) + 24), t90);
                }
                {
                    uint8_t t91 = gm_l7;
                    uint8_t t92 = t91;
                    uint32_t t93;
                    uint8_t t96 = GM_LD(uint8_t, ((uint8_t *)&t92));
                    if (!(t96 == ((uint8_t)120ULL))) goto mn95;
                    t93 = ((uint32_t)16ULL);
                    goto me94;
mn95:;
                    uint8_t t98 = GM_LD(uint8_t, ((uint8_t *)&t92));
                    if (!(t98 == ((uint8_t)111ULL))) goto mn97;
                    t93 = ((uint32_t)8ULL);
                    goto me94;
mn97:;
                    t93 = ((uint32_t)2ULL);
                    goto me94;
mn99:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 56, 25);
me94:;
                    gm_l5 = t93;
                }
                goto ix43;
ie43:;
ix43:;
            }
            goto ix37;
ie37:;
ix37:;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        void * t100 = gm_l0;
        uint32_t t101 = gm_l5;
        void * t102 = (void *)((uint8_t *)&gm_l3);
        gm_t2lqu8jsj511d t103 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t100, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t103;
    }
    {
        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint8_t t107 = ((t105 == t106));
        if (!t107) goto ie104;
        uint8_t t108;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn110;
        t108 = ((uint8_t)1);
        goto me109;
mn110:;
        t108 = ((uint8_t)0);
        goto me109;
mn111:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 66, 39);
me109:;
        if (!t108) goto ie104;
        {
            gm_t6j6smj7qoctu t113;
            uint32_t t114 = gm_l5;
            gm_b16_8 t115 = gm_f_radix_name_5kts8t0(t114);
            GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t115);
            void * t116 = gm_l0;
            uint64_t t117 = GM_LD(uintptr_t, (((uint8_t *)t116) + 8) + 8);
            void * t118 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t116) + 8)));
            uint64_t t119 = gm_l1;
            uint64_t t120 = (uintptr_t)t119;
            void * t121 = gm_l0;
            uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)t121) + 24));
            uint64_t t123 = (uintptr_t)t122;
            if (t120 > t123 || t123 > (uintptr_t)t117) gm_trap_slice(t120, t123, t117, gm_srct4c97p4hek1p, 67, 78);
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(((uint8_t *)t118 + t120 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(t123 - t120));
            GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t124);
            gm_tabb710d9mq2e t125;
            gm_f_format_b6aggpq((void *)&t125, (void *)NULL, cx, NULL, &t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t126 = t125;
            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 8, gm_srct4c97p4hek1p, 67, 15);
            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&t126) + 8));
            gm_b16_8 t128 = t127;
            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
            gm_th2lqbo77pvsc t130;
            GM_ST(uint8_t, (uint8_t *)&t130 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t130 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t130 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t130), t129);
            ((uint8_t *)&t130)[16] = 1;
            gm_l3 = t130;
        }
        goto ix104;
ie104:;
ix104:;
    }
    {
        uint32_t t132 = gm_l5;
        uint8_t t133 = ((t132 == ((uint32_t)10ULL)));
        if (!t133) goto ie131;
        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint64_t r136; bool o137 = __builtin_sub_overflow(t134, t135, &r136);
        if (o137) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 69, 24);
        uint64_t t138 = r136;
        uint8_t t139 = (t138 > ((uint64_t)1ULL));
        if (!t139) goto ie131;
        uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 0));
        uint64_t t141 = (uintptr_t)t140;
        void * t142 = gm_l0;
        uint64_t t143 = GM_LD(uintptr_t, (((uint8_t *)t142) + 8) + 8);
        if (t141 >= (uintptr_t)(t143)) gm_trap_index(t141, t143, gm_srct4c97p4hek1p, 69, 58);
        uint8_t t144 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t142) + 8)) + t141 * 1));
        uint8_t t145 = ((t144 == ((uint8_t)48ULL)));
        if (!t145) goto ie131;
        uint8_t t146;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn148;
        t146 = ((uint8_t)1);
        goto me147;
mn148:;
        t146 = ((uint8_t)0);
        goto me147;
mn149:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 69, 89);
me147:;
        if (!t146) goto ie131;
        {
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sfd839bbgsa7s));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(62));
            gm_th2lqbo77pvsc t152;
            GM_ST(uint8_t, (uint8_t *)&t152 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t152 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t152 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t152), t151);
            ((uint8_t *)&t152)[16] = 1;
            gm_l3 = t152;
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        gm_tc70a9dbbmgkg t153;
        memset(&t153, 0, sizeof t153);
        gm_l10 = t153;
    }
    {
        gm_t913bb735pvt5 t154;
        memset(&t154, 0, sizeof t154);
        gm_l11 = t154;
    }
    {
        uint32_t t156 = gm_l5;
        gm_tcc1rq4n1avb2 t157;
        GM_ST(uint32_t, ((uint8_t *)&t157 + 0), ((uint32_t)10ULL));
        GM_ST(uint32_t, ((uint8_t *)&t157 + 4), ((uint32_t)16ULL));
        gm_tcc1rq4n1avb2 t158 = t157;
        uint8_t t159 = 0;
        uint32_t t160 = t156;
        if (gm_eq467d5cm1d5(((uint8_t *)&t158) + 0, ((uint8_t *)&t160))) t159 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t158) + 4, ((uint8_t *)&t160))) t159 = 1;
        uint8_t t161 = (uint8_t)(t159);
        if (!t161) goto ie155;
        void * t162 = gm_l0;
        gm_tqs5p4n2tq19v t163 = gm_f_peek_72bostn(t162);
        gm_tqs5p4n2tq19v t164;
        GM_ST(uint8_t, ((uint8_t *)&t164), ((uint8_t)46ULL));
        ((uint8_t *)&t164)[1] = 1;
        gm_tqs5p4n2tq19v t165 = t163;
        gm_tqs5p4n2tq19v t166 = t164;
        uint8_t t167 = (gm_eqb44cb3cf1n(((uint8_t *)&t165), ((uint8_t *)&t166)));
        if (!t167) goto ie155;
        void * t168 = gm_l0;
        uint32_t t169 = gm_l5;
        void * t170 = gm_l0;
        uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)t170) + 24));
        uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
        if (o173) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 76, 78);
        uint64_t t174 = r172;
        uint8_t t175 = gm_f_fraction_follows_ivbj04h(t168, t169, t174);
        if (!t175) goto ie155;
        {
            void * t177 = gm_l0;
            void * t178 = gm_l0;
            uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)t178) + 24));
            uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
            if (o181) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 77, 9);
            uint64_t t182 = r180;
            GM_ST(uint64_t, (((uint8_t *)t177) + 24), t182);
        }
        {
            void * t183 = gm_l0;
            uint32_t t184 = gm_l5;
            void * t185 = (void *)((uint8_t *)&gm_l3);
            gm_t2lqu8jsj511d t186 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t183, t184, t185);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tc70a9dbbmgkg t187;
            GM_ST(uint8_t, (uint8_t *)&t187 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t187 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t187 + 20, 0);
            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t187), t186);
            ((uint8_t *)&t187)[16] = 1;
            gm_l10 = t187;
        }
        goto ix155;
ie155:;
ix155:;
    }
    {
        uint32_t t189 = gm_l5;
        uint8_t t190 = ((t189 == ((uint32_t)10ULL)));
        if (!t190) goto ie188;
        {
            void * t193 = gm_l0;
            gm_b16_8 t194;
            GM_ST(void *, (uint8_t *)&t194, (void *)(gm_su7ft5rc766n4));
            GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(2));
            uint8_t t195 = gm_f_at_exponent_qp4713f(t193, t194);
            if (!t195) goto ie192;
            {
                void * t197 = gm_l0;
                void * t198 = (void *)((uint8_t *)&gm_l3);
                int64_t t199 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t197, t198);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t913bb735pvt5 t200;
                GM_ST(uint8_t, (uint8_t *)&t200 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t200 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t200 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t200), t199);
                ((uint8_t *)&t200)[8] = 1;
                gm_l11 = t200;
            }
            goto ix192;
ie192:;
ix192:;
        }
        goto ix188;
ie188:;
        uint32_t t202 = gm_l5;
        uint8_t t203 = ((t202 == ((uint32_t)16ULL)));
        if (!t203) goto ie201;
        {
            void * t206 = gm_l0;
            gm_b16_8 t207;
            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_s15p9kmrsfogp));
            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(1));
            uint8_t t208 = gm_f_at_exponent_qp4713f(t206, t207);
            if (!t208) goto ie205;
            {
                void * t210 = gm_l0;
                void * t211 = (void *)((uint8_t *)&gm_l3);
                int64_t t212 = gm_f_scan_exponent_20o8k8o((void *)NULL, cx, NULL, t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t913bb735pvt5 t213;
                GM_ST(uint8_t, (uint8_t *)&t213 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t213 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t213 + 12, 0);
                GM_ST(int64_t, ((uint8_t *)&t213), t212);
                ((uint8_t *)&t213)[8] = 1;
                gm_l11 = t213;
            }
            goto ix205;
ie205:;
            uint8_t t215;
            if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn217;
            t215 = ((uint8_t)0);
            goto me216;
mn217:;
            t215 = ((uint8_t)1);
            goto me216;
mn218:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 19);
me216:;
            if (!t215) goto ie214;
            uint8_t t219;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn221;
            t219 = ((uint8_t)1);
            goto me220;
mn221:;
            t219 = ((uint8_t)0);
            goto me220;
mn222:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 85, 45);
me220:;
            if (!t219) goto ie214;
            {
                gm_b16_8 t224;
                GM_ST(void *, (uint8_t *)&t224, (void *)(gm_s5frmv5tebja4));
                GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(49));
                gm_th2lqbo77pvsc t225;
                GM_ST(uint8_t, (uint8_t *)&t225 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t225 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t225 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t225), t224);
                ((uint8_t *)&t225)[16] = 1;
                gm_l3 = t225;
            }
            goto ix214;
ie214:;
ix214:;
ix205:;
        }
        goto ix201;
ie201:;
ix201:;
ix188:;
    }
    {
        gm_l12 = ((uint8_t)0);
    }
    {
        uint32_t t227 = gm_l5;
        uint8_t t228 = ((t227 == ((uint32_t)10ULL)));
        if (!t228) goto ie226;
        uint8_t t229;
        if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn231;
        t229 = ((uint8_t)1);
        goto me230;
mn231:;
        t229 = ((uint8_t)0);
        goto me230;
mn232:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 24);
me230:;
        if (!t229) goto ie226;
        uint8_t t233;
        if (((((uint8_t *)&gm_l11))[8] != 0)) goto mn235;
        t233 = ((uint8_t)1);
        goto me234;
mn235:;
        t233 = ((uint8_t)0);
        goto me234;
mn236:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 91, 46);
me234:;
        if (!t233) goto ie226;
        {
            void * t238 = gm_l0;
            uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)t238) + 24));
            gm_l13 = t239;
        }
        {
            void * t240 = gm_l0;
            gm_b16_8 t241 = gm_f_scan_ident_h1b2n4u(t240);
            gm_taari1s0m51a7 t242 = gm_f_float_suffix_lh8qbg0(t241);
            gm_taari1s0m51a7 t243 = t242;
            uint8_t t244;
            if (((((uint8_t *)&t243))[1] != 0)) goto mn246;
            t244 = ((uint8_t)0);
            goto me245;
mn246:;
            t244 = ((uint8_t)1);
            goto me245;
mn247:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 93, 27);
me245:;
            gm_l12 = t244;
        }
        {
            void * t248 = gm_l0;
            uint64_t t249 = gm_l13;
            GM_ST(uint64_t, (((uint8_t *)t248) + 24), t249);
        }
        goto ix226;
ie226:;
ix226:;
    }
    {
        uint8_t t250 = 0;
        uint8_t t251 = 0;
        uint8_t t252;
        if (((((uint8_t *)&gm_l10))[16] != 0)) goto mn254;
        t252 = ((uint8_t)0);
        goto me253;
mn254:;
        t252 = ((uint8_t)1);
        goto me253;
mn255:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 17);
me253:;
        if (!t252) {
            uint8_t t256;
            if (((((uint8_t *)&gm_l11))[8] != 0)) goto mn258;
            t256 = ((uint8_t)0);
            goto me257;
mn258:;
            t256 = ((uint8_t)1);
            goto me257;
mn259:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 96, 42);
me257:;
            t251 = t256;
        } else t251 = 1;
        if (!t251) {
            uint8_t t260 = gm_l12;
            t250 = t260;
        } else t250 = 1;
        gm_l14 = t250;
    }
    {
        void * t261 = gm_l0;
        gm_b16_8 t262 = gm_f_scan_ident_h1b2n4u(t261);
        gm_l15 = t262;
    }
    {
        gm_b16_8 t263 = gm_l15;
        uint8_t t264 = gm_f_is_imag_suffix_46gu23m(t263);
        gm_l16 = t264;
    }
    {
        uint8_t t266 = gm_l16;
        if (!t266) goto ie265;
        {
            uint32_t t269 = gm_l5;
            uint8_t t270 = (!(t269 == ((uint32_t)10ULL)));
            if (!t270) goto ie268;
            uint8_t t271;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn273;
            t271 = ((uint8_t)1);
            goto me272;
mn273:;
            t271 = ((uint8_t)0);
            goto me272;
mn274:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 103, 32);
me272:;
            if (!t271) goto ie268;
            {
                gm_b16_8 t276;
                GM_ST(void *, (uint8_t *)&t276, (void *)(gm_sc80lo6tf645h));
                GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(30));
                gm_th2lqbo77pvsc t277;
                GM_ST(uint8_t, (uint8_t *)&t277 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t277 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t277 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t277), t276);
                ((uint8_t *)&t277)[16] = 1;
                gm_l3 = t277;
            }
            goto ix268;
ie268:;
ix268:;
        }
        {
            gm_l14 = ((uint8_t)1);
        }
        goto ix265;
ie265:;
        gm_b16_8 t279 = gm_l15;
        gm_b16_8 t280;
        GM_ST(void *, (uint8_t *)&t280, (void *)(gm_sikg061j3im7q));
        GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(1));
        uint8_t t281 = gm_f_starts_with_6vvt01e(t279, t280);
        if (!t281) goto ie278;
        uint64_t t282 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
        void * t283 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)));
        uint64_t t284 = (uintptr_t)((uint64_t)1ULL);
        if (t284 > (uintptr_t)t282 || (uintptr_t)t282 > (uintptr_t)t282) gm_trap_slice(t284, (uintptr_t)t282, t282, gm_srct4c97p4hek1p, 105, 54);
        gm_b16_8 t285;
        GM_ST(void *, (uint8_t *)&t285, (void *)(((uint8_t *)t283 + t284 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)((uintptr_t)t282 - t284));
        uint8_t t286 = gm_f_all_digits_e77c2k1(t285);
        if (!t286) goto ie278;
        uint32_t t287 = gm_l5;
        uint8_t t288 = ((t287 == ((uint32_t)10ULL)));
        if (!t288) goto ie278;
        uint8_t t289;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn291;
        t289 = ((uint8_t)1);
        goto me290;
mn291:;
        t289 = ((uint8_t)0);
        goto me290;
mn292:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 105, 87);
me290:;
        if (!t289) goto ie278;
        {
            gm_b16_8 t294 = gm_l15;
            gm_tnpifv20irnph t295;
            GM_ST(gm_b16_8, ((uint8_t *)&t295 + 0), t294);
            gm_tabb710d9mq2e t296;
            gm_f_format_v5s6luj((void *)&t296, (void *)NULL, cx, NULL, t295);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t297 = t296;
            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 8, gm_srct4c97p4hek1p, 106, 15);
            gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&t297) + 8));
            gm_b16_8 t299 = t298;
            gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
            gm_th2lqbo77pvsc t301;
            GM_ST(uint8_t, (uint8_t *)&t301 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t301 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t301 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t301), t300);
            ((uint8_t *)&t301)[16] = 1;
            gm_l3 = t301;
        }
        goto ix278;
ie278:;
ix278:;
ix265:;
    }
    {
        gm_t46lmulm8gaj3 t302;
        memset(&t302, 0, sizeof t302);
        gm_l17 = t302;
    }
    {
        gm_taari1s0m51a7 t303;
        memset(&t303, 0, sizeof t303);
        gm_l18 = t303;
    }
    {
        uint64_t t305 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
        uint8_t t306 = ((t305 == ((uint64_t)0ULL)));
        if (!t306) goto ie304;
        goto ix304;
ie304:;
        uint8_t t309 = gm_l16;
        if (!t309) goto ie308;
        {
            gm_b16_8 t311 = gm_l15;
            gm_taari1s0m51a7 t312 = gm_f_imag_width_9gtosab(t311);
            gm_l18 = t312;
        }
        goto ix308;
ie308:;
        uint8_t t314 = gm_l14;
        if (!t314) goto ie313;
        {
            gm_b16_8 t316 = gm_l15;
            gm_taari1s0m51a7 t317 = gm_f_float_suffix_lh8qbg0(t316);
            gm_l18 = t317;
        }
        {
            uint8_t t319;
            if (((((uint8_t *)&gm_l18))[1] != 0)) goto mn321;
            t319 = ((uint8_t)1);
            goto me320;
mn321:;
            t319 = ((uint8_t)0);
            goto me320;
mn322:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 12);
me320:;
            if (!t319) goto ie318;
            uint8_t t323;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn325;
            t323 = ((uint8_t)1);
            goto me324;
mn325:;
            t323 = ((uint8_t)0);
            goto me324;
mn326:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 116, 34);
me324:;
            if (!t323) goto ie318;
            {
                gm_b16_8 t328 = gm_l15;
                gm_tnpifv20irnph t329;
                GM_ST(gm_b16_8, ((uint8_t *)&t329 + 0), t328);
                gm_tabb710d9mq2e t330;
                gm_f_format_convcvp((void *)&t330, (void *)NULL, cx, NULL, t329);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t331 = t330;
                if (((((uint8_t *)&t331))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t331) + 8, gm_srct4c97p4hek1p, 117, 19);
                gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&t331) + 8));
                gm_b16_8 t333 = t332;
                gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                gm_th2lqbo77pvsc t335;
                GM_ST(uint8_t, (uint8_t *)&t335 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t335 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t335 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t335), t334);
                ((uint8_t *)&t335)[16] = 1;
                gm_l3 = t335;
            }
            goto ix318;
ie318:;
ix318:;
        }
        goto ix313;
ie313:;
        {
            gm_b16_8 t337 = gm_l15;
            gm_tij245bdr7emd t338;
            gm_f_int_suffix_j594l00((void *)&t338, (void *)NULL, cx, NULL, t337);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tij245bdr7emd t339 = t338;
            if (!((((uint8_t *)&t339))[0] != 0)) goto mn341;
            gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
            gm_l19 = t342;
            {
                uint8_t t345;
                if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn347;
                t345 = ((uint8_t)1);
                goto me346;
mn347:;
                t345 = ((uint8_t)0);
                goto me346;
mn348:;
                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 122, 20);
me346:;
                if (!t345) goto ie344;
                {
                    gm_b16_8 t350 = gm_l19;
                    gm_th2lqbo77pvsc t351;
                    GM_ST(uint8_t, (uint8_t *)&t351 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t351 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t351 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t351), t350);
                    ((uint8_t *)&t351)[16] = 1;
                    gm_l3 = t351;
                }
                goto ix344;
ie344:;
ix344:;
            }
            goto me340;
mn341:;
            if (((((uint8_t *)&t339))[0] != 0)) goto mn352;
            gm_tj2m9dbh1ekv6 t353 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)&t339) + 8));
            gm_l20 = t353;
            {
                gm_tj2m9dbh1ekv6 t355 = gm_l20;
                gm_t46lmulm8gaj3 t356;
                GM_ST(uint8_t, (uint8_t *)&t356 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t356 + 10, 0);
                GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t356), t355);
                ((uint8_t *)&t356)[8] = 1;
                gm_l17 = t356;
            }
            goto me340;
mn352:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 120, 9);
me340:;
        }
ix313:;
ix308:;
ix304:;
    }
    {
        if (!((((uint8_t *)&gm_l3))[16] != 0)) goto ie357;
        gm_b16_8 t358 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l21 = t358;
        {
            void * t360 = gm_l0;
            uint64_t t361 = gm_l1;
            void * t362 = gm_l0;
            uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)t362) + 24));
            gm_tl874t2atnlh3 t364 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t360, t361, t363));
            gm_b16_8 t365 = gm_l21;
            gm_f_error_s6hn871((void *)&F.l22, (void *)NULL, cx, NULL, t364, t365);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie366;
            gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l23 = t367;
            {
                void * t369 = (void *)((uint8_t *)&F.l22);
                gm_b16_8 t370 = gm_l23;
                gm_tnpifv20irnph t371;
                GM_ST(gm_b16_8, ((uint8_t *)&t371 + 0), t370);
                gm_tabb710d9mq2e t372;
                gm_f_format_ooibcq2((void *)&t372, (void *)NULL, cx, NULL, t371);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t373 = t372;
                if (((((uint8_t *)&t373))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t373) + 8, gm_srct4c97p4hek1p, 130, 40);
                gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&t373) + 8));
                gm_b16_8 t375 = t374;
                gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                void * t377 = gm_l0;
                uint64_t t378 = gm_l1;
                uint64_t t379 = gm_l1;
                uint64_t r380; bool o381 = __builtin_add_overflow(t379, ((uint64_t)2ULL), &r380);
                if (o381) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 130, 82);
                uint64_t t382 = r380;
                gm_tl874t2atnlh3 t383 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t377, t378, t382));
                gm_b16_8 t384 = gm_l23;
                gm_b0_1 t385 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t369, t376, t383, t384);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix366;
ie366:;
ix366:;
        }
        {
            void * t386 = gm_l0;
            void * t387 = GM_LD(void *, (((uint8_t *)t386) + 32));
            gm_f_push_o4ckdr1(t387, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l22)));
        }
        goto ix357;
ie357:;
ix357:;
    }
    {
        uint8_t t389 = gm_l14;
        if (!t389) goto ie388;
        {
            void * t391 = gm_l0;
            uint32_t t392 = gm_l5;
            gm_t2lqu8jsj511d t393 = gm_l9;
            gm_tc70a9dbbmgkg t394 = gm_l10;
            gm_t913bb735pvt5 t395 = gm_l11;
            gm_t913bb735pvt5 t396 = t395;
            int64_t t397;
            if (((((uint8_t *)&t396))[8] != 0)) {
                int64_t t398 = GM_LD(int64_t, (((uint8_t *)&t396) + 0));
                t397 = t398;
            } else {
                t397 = ((int64_t)0LL);
            }
            gm_f_float_value_ip18r0p((void *)&F.l24, (void *)NULL, cx, NULL, t391, t392, t393, &t394, t397);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t399 = gm_l0;
            gm_tghmb53v8qi1e t400;
            memset((uint8_t *)&t400 + 99, 0, 5);
            memcpy((uint8_t *)&t400 + 0, ((uint8_t *)&F.l24), 96);
            gm_taari1s0m51a7 t401 = gm_l18;
            GM_ST(gm_taari1s0m51a7, (uint8_t *)&t400 + 96, t401);
            uint8_t t402 = gm_l16;
            GM_ST(uint8_t, (uint8_t *)&t400 + 98, t402);
            gm_tq7s98ihonkju t403 = gm_f_new_j2spmhj((void *)NULL, cx, NULL, &t400, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knasprnpdnf)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t404 = t403;
            if (((((uint8_t *)&t404))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t404) + 8, gm_srct4c97p4hek1p, 135, 24);
            void * t405 = GM_LD(void *, (((uint8_t *)&t404) + 8));
            gm_t3fcbuehh3bs2 t406;
            GM_ST(uint8_t, (uint8_t *)&t406 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t406 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t406 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t406 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)4ULL));
            GM_ST(void *, ((uint8_t *)&t406 + 8), t405);
            uint64_t t407 = gm_l1;
            void * t408 = gm_l0;
            uint64_t t409 = GM_LD(uint64_t, (((uint8_t *)t408) + 24));
            gm_b0_1 t410 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t399, &t406, t407, t409);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix388;
ie388:;
        {
            void * t412 = gm_l0;
            gm_t2lqu8jsj511d t413 = gm_l9;
            uint32_t t414 = gm_l5;
            gm_f_int_value_cnmrbr4((void *)&F.l25, (void *)NULL, cx, NULL, t412, t413, t414);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t415 = gm_l0;
            gm_ta42iqot1auae t416;
            memset((uint8_t *)&t416 + 61, 0, 3);
            memcpy((uint8_t *)&t416 + 0, ((uint8_t *)&F.l25), 48);
            gm_t46lmulm8gaj3 t417 = gm_l17;
            GM_ST(gm_t46lmulm8gaj3, (uint8_t *)&t416 + 48, t417);
            GM_ST(uint8_t, (uint8_t *)&t416 + 60, ((uint8_t)0));
            gm_tq7s98ihonkju t418 = gm_f_new_q1fs5ps((void *)NULL, cx, NULL, &t416, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2kjji37v2m)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t419 = t418;
            if (((((uint8_t *)&t419))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t419) + 8, gm_srct4c97p4hek1p, 138, 22);
            void * t420 = GM_LD(void *, (((uint8_t *)&t419) + 8));
            gm_t3fcbuehh3bs2 t421;
            GM_ST(uint8_t, (uint8_t *)&t421 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t421 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t421 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t421 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t421 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t421 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t421 + 8), t420);
            uint64_t t422 = gm_l1;
            void * t423 = gm_l0;
            uint64_t t424 = GM_LD(uint64_t, (((uint8_t *)t423) + 24));
            gm_b0_1 t425 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t415, &t421, t422, t424);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix388:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_raw_string_uf6bd4g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint8_t gm_l5; /* b */
    uint64_t gm_l6; /* run */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 8));
        void * t6 = gm_l0;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
        uint64_t t8 = gm_f_backtick_run_l95fb09(t5, t7);
        gm_l2 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l0;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 24));
        uint64_t t12 = gm_l2;
        uint64_t r13; bool o14 = __builtin_add_overflow(t11, t12, &r13);
        if (o14) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 473, 5);
        uint64_t t15 = r13;
        GM_ST(uint64_t, (((uint8_t *)t9) + 24), t15);
    }
    {
        void * t17 = gm_l0;
        gm_tqs5p4n2tq19v t18 = gm_f_peek_72bostn(t17);
        gm_tqs5p4n2tq19v t19;
        GM_ST(uint8_t, ((uint8_t *)&t19), ((uint8_t)10ULL));
        ((uint8_t *)&t19)[1] = 1;
        gm_tqs5p4n2tq19v t20 = t18;
        gm_tqs5p4n2tq19v t21 = t19;
        uint8_t t22 = (gm_eqb44cb3cf1n(((uint8_t *)&t20), ((uint8_t *)&t21)));
        if (!t22) goto ie16;
        {
            void * t24 = gm_l0;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 24));
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            if (o28) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 475, 9);
            uint64_t t29 = r27;
            GM_ST(uint64_t, (((uint8_t *)t24) + 24), t29);
        }
        goto ix16;
ie16:;
        void * t31 = gm_l0;
        gm_tqs5p4n2tq19v t32 = gm_f_peek_72bostn(t31);
        gm_tqs5p4n2tq19v t33;
        GM_ST(uint8_t, ((uint8_t *)&t33), ((uint8_t)13ULL));
        ((uint8_t *)&t33)[1] = 1;
        gm_tqs5p4n2tq19v t34 = t32;
        gm_tqs5p4n2tq19v t35 = t33;
        uint8_t t36 = (gm_eqb44cb3cf1n(((uint8_t *)&t34), ((uint8_t *)&t35)));
        if (!t36) goto ie30;
        void * t37 = gm_l0;
        gm_tqs5p4n2tq19v t38 = gm_f_peek_at_74p8lkq(t37, ((uint64_t)1ULL));
        gm_tqs5p4n2tq19v t39;
        GM_ST(uint8_t, ((uint8_t *)&t39), ((uint8_t)10ULL));
        ((uint8_t *)&t39)[1] = 1;
        gm_tqs5p4n2tq19v t40 = t38;
        gm_tqs5p4n2tq19v t41 = t39;
        uint8_t t42 = (gm_eqb44cb3cf1n(((uint8_t *)&t40), ((uint8_t *)&t41)));
        if (!t42) goto ie30;
        {
            void * t44 = gm_l0;
            void * t45 = gm_l0;
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 24));
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)2ULL), &r47);
            if (o48) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 477, 9);
            uint64_t t49 = r47;
            GM_ST(uint64_t, (((uint8_t *)t44) + 24), t49);
        }
        goto ix30;
ie30:;
ix30:;
ix16:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb50;
            {
                void * t53 = gm_l0;
                gm_tqs5p4n2tq19v t54 = gm_f_peek_72bostn(t53);
                gm_tqs5p4n2tq19v t55 = t54;
                uint8_t t56;
                if (((((uint8_t *)&t55))[1] != 0)) {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
                    t56 = t57;
                } else {
                    {
                        void * t59 = gm_l0;
                        uint64_t t60 = gm_l1;
                        uint64_t t61 = gm_l1;
                        uint64_t t62 = gm_l2;
                        uint64_t r63; bool o64 = __builtin_add_overflow(t61, t62, &r63);
                        if (o64) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 482, 41);
                        uint64_t t65 = r63;
                        gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t59, t60, t65));
                        gm_b16_8 t67;
                        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_srnee0q49ttns));
                        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(31));
                        gm_f_error_s6hn871((void *)&F.l4, (void *)NULL, cx, NULL, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                    }
                    {
                        void * t68 = (void *)((uint8_t *)&F.l4);
                        uint64_t t69 = gm_l2;
                        gm_t4ce0jd3tdro4 t70;
                        GM_ST(uint64_t, ((uint8_t *)&t70 + 0), t69);
                        gm_tabb710d9mq2e t71;
                        gm_f_format_ikusttp((void *)&t71, (void *)NULL, cx, NULL, t70);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_tabb710d9mq2e t72 = t71;
                        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srct4c97p4hek1p, 483, 24);
                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                        gm_b16_8 t74 = t73;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        gm_f_add_note_46v502s(t68, t75);
                    }
                    {
                        void * t76 = gm_l0;
                        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 32));
                        gm_f_push_o4ckdr1(t77, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
                    }
                    goto lb50;
                }
                gm_l5 = t56;
            }
            {
                uint8_t t79 = gm_l5;
                uint8_t t80 = ((t79 == ((uint8_t)96ULL)));
                if (!t80) goto ie78;
                {
                    void * t82 = gm_l0;
                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)t82) + 8));
                    void * t84 = gm_l0;
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 24));
                    uint64_t t86 = gm_f_backtick_run_l95fb09(t83, t85);
                    gm_l6 = t86;
                }
                {
                    void * t87 = gm_l0;
                    void * t88 = gm_l0;
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 24));
                    uint64_t t90 = gm_l6;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t89, t90, &r91);
                    if (o92) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 489, 13);
                    uint64_t t93 = r91;
                    GM_ST(uint64_t, (((uint8_t *)t87) + 24), t93);
                }
                {
                    uint64_t t95 = gm_l6;
                    uint64_t t96 = gm_l2;
                    uint8_t t97 = ((t95 == t96));
                    if (!t97) goto ie94;
                    {
                        goto lb50;
                    }
                    goto ix94;
ie94:;
ix94:;
                }
                {
                    {
                        gm_l8 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t100 = gm_l6;
                        gm_l7 = t100;
                    }
                    {
                        for (;;) {
                            uint64_t t102 = gm_l8;
                            uint64_t t103 = gm_l7;
                            uint8_t t104 = (t102 < t103);
                            if (!t104) goto lb101;
                            {
                                uint64_t t107 = gm_l8;
                                uint64_t t108 = t107;
                                {
                                    void * t112 = (void *)((uint8_t *)&F.l3);
                                    gm_tftb1odc739t5 t113 = gm_f_push_t83rg2m(t112, ((uint8_t)96ULL));
                                    gm_tftb1odc739t5 t114 = t113;
                                    if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 1, gm_srct4c97p4hek1p, 491, 31);
                                    gm_b0_1 t115 = GM_LD(gm_b0_1, (((uint8_t *)&t114) + 1));
                                }
                                goto me109;
mn110:;
                                gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 491, 13);
me109:;
                            }
lc101:;
                            {
                                uint64_t t117 = gm_l8;
                                uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                                uint64_t t120 = r118;
                                gm_l8 = t120;
                            }
                        }
lb101:;
                    }
                }
                goto ix78;
ie78:;
                uint8_t t122 = gm_l5;
                uint8_t t123 = ((t122 == ((uint8_t)13ULL)));
                if (!t123) goto ie121;
                {
                    void * t126 = gm_l0;
                    gm_tqs5p4n2tq19v t127 = gm_f_peek_at_74p8lkq(t126, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t128;
                    GM_ST(uint8_t, ((uint8_t *)&t128), ((uint8_t)10ULL));
                    ((uint8_t *)&t128)[1] = 1;
                    gm_tqs5p4n2tq19v t129 = t127;
                    gm_tqs5p4n2tq19v t130 = t128;
                    uint8_t t131 = (gm_eqb44cb3cf1n(((uint8_t *)&t129), ((uint8_t *)&t130)));
                    if (!t131) goto ie125;
                    {
                        void * t133 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t134 = gm_f_push_t83rg2m(t133, ((uint8_t)10ULL));
                        gm_tftb1odc739t5 t135 = t134;
                        if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 1, gm_srct4c97p4hek1p, 494, 17);
                        gm_b0_1 t136 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 1));
                    }
                    {
                        void * t137 = gm_l0;
                        void * t138 = gm_l0;
                        uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t138) + 24));
                        uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)2ULL), &r140);
                        if (o141) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 495, 17);
                        uint64_t t142 = r140;
                        GM_ST(uint64_t, (((uint8_t *)t137) + 24), t142);
                    }
                    goto ix125;
ie125:;
                    {
                        void * t144 = gm_l0;
                        gm_b0_1 t145 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t144);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                    }
ix125:;
                }
                goto ix121;
ie121:;
                {
                    void * t147 = (void *)((uint8_t *)&F.l3);
                    uint8_t t148 = gm_l5;
                    gm_tftb1odc739t5 t149 = gm_f_push_t83rg2m(t147, t148);
                    gm_tftb1odc739t5 t150 = t149;
                    if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srct4c97p4hek1p, 500, 13);
                    gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
                }
                {
                    void * t152 = gm_l0;
                    void * t153 = gm_l0;
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)t153) + 24));
                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                    if (o156) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 501, 13);
                    uint64_t t157 = r155;
                    GM_ST(uint64_t, (((uint8_t *)t152) + 24), t157);
                }
ix121:;
ix78:;
            }
lc50:;
        }
gm_ll51:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb50; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc50; }
        }
        goto gm_lad;
lb50:;
    }
    {
        void * t158 = gm_l0;
        gm_tibupt4dmcqai t159;
        memset((uint8_t *)&t159 + 17, 0, 7);
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t159 + 0, t160);
        gm_t9qkh0rsooc8d t161;
        GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t159 + 16, t161);
        gm_t3fcbuehh3bs2 t162;
        GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t162 + 8), t159);
        uint64_t t163 = gm_l1;
        void * t164 = gm_l0;
        uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 24));
        gm_b0_1 t166 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t158, &t162, t163, t165);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_lex_string_bcdbdjm F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* c */
    uint8_t gm_l6; /* x */
    GM_STACK_CHECK(&F, 512u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 425, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 24), t7);
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb8;
            {
                void * t11 = gm_l0;
                gm_tqs5p4n2tq19v t12 = gm_f_peek_72bostn(t11);
                gm_tqs5p4n2tq19v t13 = t12;
                uint8_t t14;
                if (((((uint8_t *)&t13))[1] != 0)) {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
                    t14 = t15;
                } else {
                    {
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l1;
                        void * t19 = gm_l0;
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
                        gm_b16_8 t21;
                        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sjblck9bpb6r8));
                        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(27));
                        gm_b0_1 t22 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t17, t18, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto lb8;
                }
                gm_l4 = t14;
            }
            {
                uint8_t t24 = gm_l4;
                uint8_t t25 = ((t24 == ((uint8_t)34ULL)));
                if (!t25) goto ie23;
                {
                    void * t27 = gm_l0;
                    void * t28 = gm_l0;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 24));
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    if (o31) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 433, 13);
                    uint64_t t32 = r30;
                    GM_ST(uint64_t, (((uint8_t *)t27) + 24), t32);
                }
                {
                    goto lb8;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                uint8_t t34 = gm_l4;
                uint8_t t35 = ((t34 == ((uint8_t)10ULL)));
                if (!t35) goto ie33;
                {
                    void * t37 = gm_l0;
                    uint64_t t38 = gm_l1;
                    gm_b0_1 t39 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t37, t38);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                }
                {
                    goto lb8;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                uint8_t t41 = gm_l4;
                uint8_t t42 = ((t41 == ((uint8_t)13ULL)));
                if (!t42) goto ie40;
                {
                    void * t45 = gm_l0;
                    gm_tqs5p4n2tq19v t46 = gm_f_peek_at_74p8lkq(t45, ((uint64_t)1ULL));
                    gm_tqs5p4n2tq19v t47;
                    GM_ST(uint8_t, ((uint8_t *)&t47), ((uint8_t)10ULL));
                    ((uint8_t *)&t47)[1] = 1;
                    gm_tqs5p4n2tq19v t48 = t46;
                    gm_tqs5p4n2tq19v t49 = t47;
                    uint8_t t50 = (gm_eqb44cb3cf1n(((uint8_t *)&t48), ((uint8_t *)&t49)));
                    if (!t50) goto ie44;
                    {
                        void * t52 = gm_l0;
                        uint64_t t53 = gm_l1;
                        gm_b0_1 t54 = gm_f_unterminated_at_line_end_v8hbf1f((void *)NULL, cx, NULL, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        goto lb8;
                    }
                    goto ix44;
ie44:;
ix44:;
                }
                {
                    void * t55 = gm_l0;
                    gm_b0_1 t56 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t55);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                }
                goto ix40;
ie40:;
                uint8_t t58 = gm_l4;
                uint8_t t59 = ((t58 == ((uint8_t)92ULL)));
                if (!t59) goto ie57;
                {
                    void * t61 = gm_l0;
                    gm_ts06tb4mldv43 t62 = gm_f_lex_escape_7pnp2v0((void *)NULL, cx, NULL, t61);
                    if (gm_unw.kind) {
                        goto gm_ll9;
                    }
                    gm_ts06tb4mldv43 t63 = t62;
                    if (!((((uint8_t *)&t63))[8] != 0)) goto mn65;
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
                    if (t66 != ((uint8_t)0ULL)) goto mn65;
                    uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&t63) + 4));
                    gm_l5 = t67;
                    void * t68 = (void *)((uint8_t *)&F.l3);
                    uint32_t t69 = gm_l5;
                    gm_f_push_utf8_bjq6ubs(t68, t69);
                    goto me64;
mn65:;
                    if (!((((uint8_t *)&t63))[8] != 0)) goto mn70;
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
                    if (t71 != ((uint8_t)1ULL)) goto mn70;
                    uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t63) + 4));
                    gm_l6 = t72;
                    void * t73 = (void *)((uint8_t *)&F.l3);
                    uint8_t t74 = gm_l6;
                    gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t73, t74);
                    gm_tftb1odc739t5 t76 = t75;
                    if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srct4c97p4hek1p, 449, 29);
                    gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                    goto me64;
mn70:;
                    if (((((uint8_t *)&t63))[8] != 0)) goto mn78;
                    goto me64;
mn78:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 447, 13);
me64:;
                }
                goto ix57;
ie57:;
                {
                    void * t81 = (void *)((uint8_t *)&F.l3);
                    uint8_t t82 = gm_l4;
                    gm_tftb1odc739t5 t83 = gm_f_push_t83rg2m(t81, t82);
                    gm_tftb1odc739t5 t84 = t83;
                    if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srct4c97p4hek1p, 453, 13);
                    gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                }
                {
                    void * t86 = gm_l0;
                    void * t87 = gm_l0;
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 24));
                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                    if (o90) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 454, 13);
                    uint64_t t91 = r89;
                    GM_ST(uint64_t, (((uint8_t *)t86) + 24), t91);
                }
ix57:;
ix40:;
            }
lc8:;
        }
gm_ll9:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        uint8_t t93 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
        if (!t93) goto ie92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t95 = t94;
        uint8_t t96 = 0;
        uint8_t t97 = ((uint8_t)0ULL);
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t95) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t95))) + i * 1, ((uint8_t *)&t97))) { t96 = 1; break; }
        uint8_t t98 = (uint8_t)(t96);
        if (!t98) goto ie92;
        {
            void * t100 = gm_l0;
            uint64_t t101 = gm_l1;
            void * t102 = gm_l0;
            uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 24));
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sil0do0eri7f7));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(43));
            gm_b0_1 t105 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t100, t101, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix92;
ie92:;
ix92:;
    }
    {
        void * t106 = gm_l0;
        gm_tibupt4dmcqai t107;
        memset((uint8_t *)&t107 + 17, 0, 7);
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t107 + 0, t108);
        gm_t9qkh0rsooc8d t109 = gm_l2;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t107 + 16, t109);
        gm_t3fcbuehh3bs2 t110;
        GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)5ULL));
        GM_ST(gm_tibupt4dmcqai, ((uint8_t *)&t110 + 8), t107);
        uint64_t t111 = gm_l1;
        void * t112 = gm_l0;
        uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t112) + 24));
        gm_b0_1 t114 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t106, &t110, t111, t113);
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
__attribute__((aligned(4))) gm_ts06tb4mldv43 gm_f_lex_unicode_escape_ns81oou(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_lex_unicode_escape_ns81oou F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* digits */
    uint32_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    uint32_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* why */
    GM_STACK_CHECK(&F, 1046u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_tqs5p4n2tq19v t4 = gm_f_peek_72bostn(t3);
        gm_tqs5p4n2tq19v t5;
        GM_ST(uint8_t, ((uint8_t *)&t5), ((uint8_t)123ULL));
        ((uint8_t *)&t5)[1] = 1;
        gm_tqs5p4n2tq19v t6 = t4;
        gm_tqs5p4n2tq19v t7 = t5;
        uint8_t t8 = (!gm_eqb44cb3cf1n(((uint8_t *)&t6), ((uint8_t *)&t7)));
        if (!t8) goto ie2;
        {
            void * t10 = gm_l0;
            uint64_t t11 = gm_l1;
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 24));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sh6hkhojsmti9));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(47));
            gm_b0_1 t15 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t10, t11, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 24));
        uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 378, 5);
        uint64_t t23 = r21;
        GM_ST(uint64_t, (((uint8_t *)t18) + 24), t23);
    }
    {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
        gm_l2 = t25;
    }
    {
        for (;;) {
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 24));
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uintptr_t, (((uint8_t *)t29) + 8) + 8);
            uint8_t t31 = (t28 < t30);
            if (!t31) goto lb26;
            void * t32 = gm_l0;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 24));
            uint64_t t34 = (uintptr_t)t33;
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 8) + 8);
            if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srct4c97p4hek1p, 380, 54);
            uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 8)) + t34 * 1));
            uint8_t t38 = gm_f_is_hex_digit_9lads51(t37);
            if (!t38) goto lb26;
            {
                void * t41 = gm_l0;
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 24));
                uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                if (o45) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 380, 72);
                uint64_t t46 = r44;
                GM_ST(uint64_t, (((uint8_t *)t41) + 24), t46);
            }
lc26:;
        }
lb26:;
    }
    {
        void * t47 = gm_l0;
        uint64_t t48 = GM_LD(uintptr_t, (((uint8_t *)t47) + 8) + 8);
        void * t49 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t47) + 8)));
        uint64_t t50 = gm_l2;
        uint64_t t51 = (uintptr_t)t50;
        void * t52 = gm_l0;
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 24));
        uint64_t t54 = (uintptr_t)t53;
        if (t51 > t54 || t54 > (uintptr_t)t48) gm_trap_slice(t51, t54, t48, gm_srct4c97p4hek1p, 381, 15);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t49 + t51 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54 - t51));
        gm_l3 = t55;
    }
    {
        void * t57 = gm_l0;
        gm_tqs5p4n2tq19v t58 = gm_f_peek_72bostn(t57);
        gm_tqs5p4n2tq19v t59;
        GM_ST(uint8_t, ((uint8_t *)&t59), ((uint8_t)125ULL));
        ((uint8_t *)&t59)[1] = 1;
        gm_tqs5p4n2tq19v t60 = t58;
        gm_tqs5p4n2tq19v t61 = t59;
        uint8_t t62 = (!gm_eqb44cb3cf1n(((uint8_t *)&t60), ((uint8_t *)&t61)));
        if (!t62) goto ie56;
        {
            void * t64 = gm_l0;
            uint64_t t65 = gm_l1;
            void * t66 = gm_l0;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 24));
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s4hf5j4eu72mv));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(65));
            gm_b0_1 t69 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t64, t65, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t70;
            memset(&t70, 0, sizeof t70);
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        void * t72 = gm_l0;
        void * t73 = gm_l0;
        uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 24));
        uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
        if (o76) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 386, 5);
        uint64_t t77 = r75;
        GM_ST(uint64_t, (((uint8_t *)t72) + 24), t77);
    }
    {
        uint8_t t79 = 0;
        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t81 = ((t80 == ((uint64_t)0ULL)));
        if (!t81) {
            uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t83 = (t82 > ((uint64_t)6ULL));
            t79 = t83;
        } else t79 = 1;
        if (!t79) goto ie78;
        {
            void * t85 = gm_l0;
            uint64_t t86 = gm_l1;
            void * t87 = gm_l0;
            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)t87) + 24));
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sajhc6og5bv8v));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(58));
            gm_b0_1 t90 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t85, t86, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t91;
            memset(&t91, 0, sizeof t91);
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        gm_l4 = ((uint32_t)0ULL);
    }
    {
        {
            gm_b16_8 t94 = gm_l3;
            gm_l5 = t94;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t96 = gm_l6;
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t98 = (t96 < t97);
                if (!t98) goto lb95;
                {
                    uint64_t t101 = gm_l6;
                    uint64_t t102 = (uintptr_t)t101;
                    uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t104 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t102 * 1));
                    uint8_t t105 = t104;
                    uint8_t t108 = GM_LD(uint8_t, ((uint8_t *)&t105));
                    gm_l7 = t108;
                    {
                        uint32_t t110 = gm_l4;
                        uint32_t r111; bool o112 = __builtin_mul_overflow(t110, ((uint32_t)16ULL), &r111);
                        if (o112) gm_trap("integer overflow in `*`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t113 = r111;
                        uint8_t t114 = gm_l7;
                        uint32_t t115 = gm_f_digit_value_8ogbrqp(t114);
                        uint32_t r116; bool o117 = __builtin_add_overflow(t113, t115, &r116);
                        if (o117) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 392, 31);
                        uint32_t t118 = r116;
                        gm_l4 = t118;
                    }
                    goto me106;
mn107:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 392, 5);
me106:;
                }
lc95:;
                {
                    uint64_t t120 = gm_l6;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l6 = t123;
                }
            }
lb95:;
        }
    }
    {
        uint8_t t125 = 0;
        uint32_t t126 = gm_l4;
        uint8_t t127 = (t126 > ((uint32_t)1114111ULL));
        if (!t127) {
            {
                uint32_t t129 = gm_l4;
                gm_l8 = t129;
            }
            uint8_t t130 = 0;
            uint32_t t131 = gm_l8;
            uint8_t t132 = (((uint32_t)55296ULL) <= t131);
            if (t132) {
                uint32_t t133 = gm_l8;
                uint8_t t134 = (t133 <= ((uint32_t)57343ULL));
                t130 = t134;
            } else t130 = 0;
            uint8_t t135 = t130;
            t125 = t135;
        } else t125 = 1;
        if (!t125) goto ie124;
        {
            gm_b16_8 t137;
            uint32_t t139 = gm_l4;
            uint8_t t140 = (t139 > ((uint32_t)1114111ULL));
            if (!t140) goto ie138;
            gm_b16_8 t142;
            GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sovc43d45he5a));
            GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(20));
            gm_b16_8 t143 = t142;
            t137 = t143;
            goto ix138;
ie138:;
            gm_b16_8 t145;
            GM_ST(void *, (uint8_t *)&t145, (void *)(gm_s32vm0u5vgiu5));
            GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(17));
            gm_b16_8 t146 = t145;
            t137 = t146;
ix138:;
            gm_l9 = t137;
        }
        {
            void * t147 = gm_l0;
            uint64_t t148 = gm_l1;
            void * t149 = gm_l0;
            uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 24));
            gm_t6j6smj7qoctu t151;
            gm_b16_8 t152 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t151 + 0, t152);
            gm_b16_8 t153 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t151 + 16, t153);
            gm_tabb710d9mq2e t154;
            gm_f_format_ek7crui((void *)&t154, (void *)NULL, cx, NULL, &t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t155 = t154;
            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 8, gm_srct4c97p4hek1p, 395, 30);
            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&t155) + 8));
            gm_b16_8 t157 = t156;
            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
            gm_b0_1 t159 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t147, t148, t150, t158);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_ts06tb4mldv43 t160;
            memset(&t160, 0, sizeof t160);
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
        goto ix124;
ie124:;
ix124:;
    }
    {
        uint32_t t162 = gm_l4;
        gm_tum2strs1ckr2 t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t163 + 4), t162);
        gm_ts06tb4mldv43 t164;
        GM_ST(uint8_t, (uint8_t *)&t164 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t164 + 10, 0);
        GM_ST(gm_tum2strs1ckr2, ((uint8_t *)&t164), t163);
        ((uint8_t *)&t164)[8] = 1;
        uint8_t t165 = 0;
        F.ret = t164;
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
inline __attribute__((aligned(4))) void gm_f_parse_valid_9fjef3j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1) {
    struct gm_fr_parse_valid_9fjef3j F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l1; /* radix */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
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
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        uint8_t t20 = gm_l4;
                        uint8_t t21 = (!(t20 == ((uint8_t)95ULL)));
                        if (!t21) goto ie19;
                        uint8_t t22 = gm_l4;
                        uint32_t t23 = gm_f_digit_value_8ogbrqp(t22);
                        uint32_t t24 = gm_l1;
                        uint8_t t25 = (t23 >= t24);
                        if (!t25) goto ie19;
                        {
                            gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            goto gm_fdone;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 230, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t28 = gm_l3;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l3 = t31;
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
        gm_b16_8 t32 = gm_l0;
        uint32_t t33 = gm_l1;
        gm_f_parse_fbta82r((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t32, t33);
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
__attribute__((aligned(4))) void gm_f_push_utf8_bjq6ubs(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_utf8_bjq6ubs F;
    void * gm_l0; /* out */
    uint32_t gm_l1; /* c */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t3 = gm_l1;
        uint8_t t4 = (t3 < ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            uint32_t t7 = gm_l1;
            uint8_t t8 = (uint8_t)t7;
            gm_tftb1odc739t5 t9 = gm_f_push_t83rg2m(t6, t8);
            gm_tftb1odc739t5 t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srct4c97p4hek1p, 404, 9);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix2;
ie2:;
        uint32_t t13 = gm_l1;
        uint8_t t14 = (t13 < ((uint32_t)2048ULL));
        if (!t14) goto ie12;
        {
            void * t16 = gm_l0;
            uint32_t t17 = gm_l1;
            unsigned __int128 n18 = (unsigned __int128)((uint64_t)6ULL);
            if (n18 >= 32) gm_trap_shift((uint64_t)n18, "u32", gm_srct4c97p4hek1p, 406, 27);
            uint32_t t19 = (uint32_t)(t17 >> (int)n18);
            uint32_t t20 = (uint32_t)(((uint32_t)192ULL) | t19);
            uint8_t t21 = (uint8_t)t20;
            gm_tftb1odc739t5 t22 = gm_f_push_t83rg2m(t16, t21);
            gm_tftb1odc739t5 t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srct4c97p4hek1p, 406, 9);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        {
            void * t25 = gm_l0;
            uint32_t t26 = gm_l1;
            uint32_t t27 = (uint32_t)(t26 & ((uint32_t)63ULL));
            uint32_t t28 = (uint32_t)(((uint32_t)128ULL) | t27);
            uint8_t t29 = (uint8_t)t28;
            gm_tftb1odc739t5 t30 = gm_f_push_t83rg2m(t25, t29);
            gm_tftb1odc739t5 t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srct4c97p4hek1p, 407, 9);
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
        }
        goto ix12;
ie12:;
        uint32_t t34 = gm_l1;
        uint8_t t35 = (t34 < ((uint32_t)65536ULL));
        if (!t35) goto ie33;
        {
            void * t37 = gm_l0;
            uint32_t t38 = gm_l1;
            unsigned __int128 n39 = (unsigned __int128)((uint64_t)12ULL);
            if (n39 >= 32) gm_trap_shift((uint64_t)n39, "u32", gm_srct4c97p4hek1p, 409, 27);
            uint32_t t40 = (uint32_t)(t38 >> (int)n39);
            uint32_t t41 = (uint32_t)(((uint32_t)224ULL) | t40);
            uint8_t t42 = (uint8_t)t41;
            gm_tftb1odc739t5 t43 = gm_f_push_t83rg2m(t37, t42);
            gm_tftb1odc739t5 t44 = t43;
            if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 1, gm_srct4c97p4hek1p, 409, 9);
            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t44) + 1));
        }
        {
            void * t46 = gm_l0;
            uint32_t t47 = gm_l1;
            unsigned __int128 n48 = (unsigned __int128)((uint64_t)6ULL);
            if (n48 >= 32) gm_trap_shift((uint64_t)n48, "u32", gm_srct4c97p4hek1p, 410, 28);
            uint32_t t49 = (uint32_t)(t47 >> (int)n48);
            uint32_t t50 = (uint32_t)(t49 & ((uint32_t)63ULL));
            uint32_t t51 = (uint32_t)(((uint32_t)128ULL) | t50);
            uint8_t t52 = (uint8_t)t51;
            gm_tftb1odc739t5 t53 = gm_f_push_t83rg2m(t46, t52);
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srct4c97p4hek1p, 410, 9);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        {
            void * t56 = gm_l0;
            uint32_t t57 = gm_l1;
            uint32_t t58 = (uint32_t)(t57 & ((uint32_t)63ULL));
            uint32_t t59 = (uint32_t)(((uint32_t)128ULL) | t58);
            uint8_t t60 = (uint8_t)t59;
            gm_tftb1odc739t5 t61 = gm_f_push_t83rg2m(t56, t60);
            gm_tftb1odc739t5 t62 = t61;
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srct4c97p4hek1p, 411, 9);
            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        }
        goto ix33;
ie33:;
        {
            void * t65 = gm_l0;
            uint32_t t66 = gm_l1;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)18ULL);
            if (n67 >= 32) gm_trap_shift((uint64_t)n67, "u32", gm_srct4c97p4hek1p, 413, 27);
            uint32_t t68 = (uint32_t)(t66 >> (int)n67);
            uint32_t t69 = (uint32_t)(((uint32_t)240ULL) | t68);
            uint8_t t70 = (uint8_t)t69;
            gm_tftb1odc739t5 t71 = gm_f_push_t83rg2m(t65, t70);
            gm_tftb1odc739t5 t72 = t71;
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srct4c97p4hek1p, 413, 9);
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
        }
        {
            void * t74 = gm_l0;
            uint32_t t75 = gm_l1;
            unsigned __int128 n76 = (unsigned __int128)((uint64_t)12ULL);
            if (n76 >= 32) gm_trap_shift((uint64_t)n76, "u32", gm_srct4c97p4hek1p, 414, 28);
            uint32_t t77 = (uint32_t)(t75 >> (int)n76);
            uint32_t t78 = (uint32_t)(t77 & ((uint32_t)63ULL));
            uint32_t t79 = (uint32_t)(((uint32_t)128ULL) | t78);
            uint8_t t80 = (uint8_t)t79;
            gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t74, t80);
            gm_tftb1odc739t5 t82 = t81;
            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srct4c97p4hek1p, 414, 9);
            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
        }
        {
            void * t84 = gm_l0;
            uint32_t t85 = gm_l1;
            unsigned __int128 n86 = (unsigned __int128)((uint64_t)6ULL);
            if (n86 >= 32) gm_trap_shift((uint64_t)n86, "u32", gm_srct4c97p4hek1p, 415, 28);
            uint32_t t87 = (uint32_t)(t85 >> (int)n86);
            uint32_t t88 = (uint32_t)(t87 & ((uint32_t)63ULL));
            uint32_t t89 = (uint32_t)(((uint32_t)128ULL) | t88);
            uint8_t t90 = (uint8_t)t89;
            gm_tftb1odc739t5 t91 = gm_f_push_t83rg2m(t84, t90);
            gm_tftb1odc739t5 t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srct4c97p4hek1p, 415, 9);
            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
        }
        {
            void * t94 = gm_l0;
            uint32_t t95 = gm_l1;
            uint32_t t96 = (uint32_t)(t95 & ((uint32_t)63ULL));
            uint32_t t97 = (uint32_t)(((uint32_t)128ULL) | t96);
            uint8_t t98 = (uint8_t)t97;
            gm_tftb1odc739t5 t99 = gm_f_push_t83rg2m(t94, t98);
            gm_tftb1odc739t5 t100 = t99;
            if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srct4c97p4hek1p, 416, 9);
            gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
        }
ix33:;
ix12:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_radix_name_5kts8t0(uint32_t a0);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_scan_digits_jhpsv6j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_scan_digits_jhpsv6j F;
    void * gm_l0; /* lx */
    uint32_t gm_l1; /* radix */
    void * gm_l2; /* err */
    uint64_t gm_l3; /* start */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* ok */
    gm_b16_8 gm_l6; /* text */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 872u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l3 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            gm_tqs5p4n2tq19v t6 = gm_f_peek_72bostn(t5);
            gm_tqs5p4n2tq19v t7 = t6;
            if (!((((uint8_t *)&t7))[1] != 0)) goto lb4;
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&t7));
            gm_l4 = t8;
            {
                uint8_t t11;
                uint32_t t13 = gm_l1;
                uint8_t t14 = ((t13 == ((uint32_t)16ULL)));
                if (!t14) goto ie12;
                uint8_t t16 = gm_l4;
                uint8_t t17 = gm_f_is_hex_digit_9lads51(t16);
                uint8_t t18 = t17;
                t11 = t18;
                goto ix12;
ie12:;
                uint8_t t20 = gm_l4;
                uint8_t t21 = gm_f_is_digit_2hoggnj(t20);
                uint8_t t22 = t21;
                t11 = t22;
ix12:;
                gm_l5 = t11;
            }
            {
                uint8_t t24 = 0;
                uint8_t t25 = gm_l5;
                if (!t25) {
                    uint8_t t26 = gm_l4;
                    uint8_t t27 = ((t26 == ((uint8_t)95ULL)));
                    t24 = t27;
                } else t24 = 1;
                uint8_t t28 = (uint8_t)!t24;
                if (!t28) goto ie23;
                {
                    goto lb4;
                }
                goto ix23;
ie23:;
ix23:;
            }
            {
                void * t30 = gm_l0;
                void * t31 = gm_l0;
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 170, 9);
                uint64_t t35 = r33;
                GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
            }
lc4:;
        }
lb4:;
    }
    {
        void * t36 = gm_l0;
        uint64_t t37 = GM_LD(uintptr_t, (((uint8_t *)t36) + 8) + 8);
        void * t38 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t36) + 8)));
        uint64_t t39 = gm_l3;
        uint64_t t40 = (uintptr_t)t39;
        void * t41 = gm_l0;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 24));
        uint64_t t43 = (uintptr_t)t42;
        if (t40 > t43 || t43 > (uintptr_t)t37) gm_trap_slice(t40, t43, t37, gm_srct4c97p4hek1p, 172, 13);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t38 + t40 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(t43 - t40));
        gm_l6 = t44;
    }
    {
        void * t46 = gm_l2;
        uint8_t t47;
        if (((((uint8_t *)t46))[16] != 0)) goto mn49;
        t47 = ((uint8_t)1);
        goto me48;
mn49:;
        t47 = ((uint8_t)0);
        goto me48;
mn50:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 173, 8);
me48:;
        if (!t47) goto ie45;
        uint8_t t51 = 0;
        uint8_t t52 = 0;
        gm_b16_8 t53 = gm_l6;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sfdbfrv46lihd));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
        uint8_t t55 = gm_f_starts_with_6vvt01e(t53, t54);
        if (!t55) {
            gm_b16_8 t56 = gm_l6;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
            uint8_t t58 = gm_f_ends_with_biriu5l(t56, t57);
            t52 = t58;
        } else t52 = 1;
        if (!t52) {
            gm_b16_8 t59 = gm_l6;
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sebojpbfjfgaf));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
            uint8_t t61 = gm_f_contains_gkki4kq(t59, t60);
            t51 = t61;
        } else t51 = 1;
        if (!t51) goto ie45;
        {
            void * t63 = gm_l2;
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sg550i4fp569j));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(54));
            gm_th2lqbo77pvsc t65;
            GM_ST(uint8_t, (uint8_t *)&t65 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t65 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t65 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t65), t64);
            ((uint8_t *)&t65)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)t63), t65);
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        void * t67 = gm_l2;
        uint8_t t68;
        if (((((uint8_t *)t67))[16] != 0)) goto mn70;
        t68 = ((uint8_t)1);
        goto me69;
mn70:;
        t68 = ((uint8_t)0);
        goto me69;
mn71:;
        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 176, 8);
me69:;
        if (!t68) goto ie66;
        {
            {
                gm_b16_8 t74 = gm_l6;
                gm_l7 = t74;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t76 = gm_l8;
                    uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t78 = (t76 < t77);
                    if (!t78) goto lb75;
                    {
                        uint64_t t81 = gm_l8;
                        uint64_t t82 = gm_l8;
                        uint64_t t83 = (uintptr_t)t82;
                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t85 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t83 * 1));
                        gm_tf5srh4mp4b3i t86;
                        GM_ST(uint8_t, (uint8_t *)&t86 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t86 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t86 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t81);
                        GM_ST(uint8_t, ((uint8_t *)&t86 + 8), t85);
                        gm_tf5srh4mp4b3i t87 = t86;
                        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&t87) + 0));
                        gm_l9 = t90;
                        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t87) + 8));
                        gm_l10 = t91;
                        {
                            uint8_t t94 = gm_l10;
                            uint8_t t95 = (!(t94 == ((uint8_t)95ULL)));
                            if (!t95) goto ie93;
                            uint8_t t96 = gm_l10;
                            uint32_t t97 = gm_f_digit_value_8ogbrqp(t96);
                            uint32_t t98 = gm_l1;
                            uint8_t t99 = (t97 >= t98);
                            if (!t99) goto ie93;
                            {
                                void * t101 = gm_l2;
                                uint8_t *t102 = ((uint8_t *)t101);
                                gm_t6j6smj7qoctu t103;
                                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                void * t105 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
                                uint64_t t106 = gm_l9;
                                uint64_t t107 = (uintptr_t)t106;
                                uint64_t t108 = gm_l9;
                                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                if (o110) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 179, 79);
                                uint64_t t111 = r109;
                                uint64_t t112 = (uintptr_t)t111;
                                if (t107 > t112 || t112 > (uintptr_t)t104) gm_trap_slice(t107, t112, t104, gm_srct4c97p4hek1p, 179, 71);
                                gm_b16_8 t113;
                                GM_ST(void *, (uint8_t *)&t113, (void *)(((uint8_t *)t105 + t107 * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(t112 - t107));
                                GM_ST(gm_b16_8, (uint8_t *)&t103 + 0, t113);
                                uint32_t t114 = gm_l1;
                                gm_b16_8 t115 = gm_f_radix_name_5kts8t0(t114);
                                GM_ST(gm_b16_8, (uint8_t *)&t103 + 16, t115);
                                gm_tabb710d9mq2e t116;
                                gm_f_format_51h4888((void *)&t116, (void *)NULL, cx, NULL, &t103);
                                if (gm_unw.kind) {
                                    goto gm_ll79;
                                }
                                gm_tabb710d9mq2e t117 = t116;
                                if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_srct4c97p4hek1p, 179, 24);
                                gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
                                gm_b16_8 t119 = t118;
                                gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                                gm_th2lqbo77pvsc t121;
                                GM_ST(uint8_t, (uint8_t *)&t121 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t121 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t121 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t121), t120);
                                ((uint8_t *)&t121)[16] = 1;
                                GM_ST(gm_th2lqbo77pvsc, t102, t121);
                            }
                            {
                                goto lb75;
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me88;
mn89:;
                        gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 177, 9);
me88:;
                    }
lc75:;
                    {
                        uint64_t t123 = gm_l8;
                        uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                        uint64_t t126 = r124;
                        gm_l8 = t126;
                    }
                }
gm_ll79:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
                }
                goto gm_lad;
lb75:;
            }
        }
        goto ix66;
ie66:;
ix66:;
    }
    {
        uint64_t t127 = gm_l3;
        void * t128 = gm_l0;
        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 24));
        gm_t2lqu8jsj511d t130;
        GM_ST(uint64_t, ((uint8_t *)&t130 + 0), t127);
        GM_ST(uint64_t, ((uint8_t *)&t130 + 8), t129);
        uint8_t t131 = 0;
        F.ret = t130;
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
__attribute__((aligned(4))) int64_t gm_f_scan_exponent_20o8k8o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_scan_exponent_20o8k8o F;
    void * gm_l0; /* lx */
    void * gm_l1; /* err */
    uint8_t gm_l2; /* negative */
    gm_t2lqu8jsj511d gm_l3; /* digits */
    int64_t gm_l4; /* value */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* c */
    GM_STACK_CHECK(&F, 664u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 199, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 24), t7);
    }
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        void * t9 = gm_l0;
        gm_tqs5p4n2tq19v t10 = gm_f_peek_72bostn(t9);
        gm_tqs5p4n2tq19v t11;
        GM_ST(uint8_t, ((uint8_t *)&t11), ((uint8_t)45ULL));
        ((uint8_t *)&t11)[1] = 1;
        gm_tqs5p4n2tq19v t12 = t10;
        gm_tqs5p4n2tq19v t13 = t11;
        uint8_t t14 = (gm_eqb44cb3cf1n(((uint8_t *)&t12), ((uint8_t *)&t13)));
        if (!t14) goto ie8;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 24));
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            if (o20) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 202, 9);
            uint64_t t21 = r19;
            GM_ST(uint64_t, (((uint8_t *)t16) + 24), t21);
        }
        {
            gm_l2 = ((uint8_t)1);
        }
        goto ix8;
ie8:;
        void * t23 = gm_l0;
        gm_tqs5p4n2tq19v t24 = gm_f_peek_72bostn(t23);
        gm_tqs5p4n2tq19v t25;
        GM_ST(uint8_t, ((uint8_t *)&t25), ((uint8_t)43ULL));
        ((uint8_t *)&t25)[1] = 1;
        gm_tqs5p4n2tq19v t26 = t24;
        gm_tqs5p4n2tq19v t27 = t25;
        uint8_t t28 = (gm_eqb44cb3cf1n(((uint8_t *)&t26), ((uint8_t *)&t27)));
        if (!t28) goto ie22;
        {
            void * t30 = gm_l0;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            if (o34) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 205, 9);
            uint64_t t35 = r33;
            GM_ST(uint64_t, (((uint8_t *)t30) + 24), t35);
        }
        goto ix22;
ie22:;
ix22:;
ix8:;
    }
    {
        void * t36 = gm_l0;
        void * t37 = gm_l1;
        gm_t2lqu8jsj511d t38 = gm_f_scan_digits_jhpsv6j((void *)NULL, cx, NULL, t36, ((uint32_t)10ULL), t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t38;
    }
    {
        gm_l4 = ((int64_t)0LL);
    }
    {
        {
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uintptr_t, (((uint8_t *)t40) + 8) + 8);
            void * t42 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t40) + 8)));
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t44 = (uintptr_t)t43;
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t46 = (uintptr_t)t45;
            if (t44 > t46 || t46 > (uintptr_t)t41) gm_trap_slice(t44, t46, t41, gm_srct4c97p4hek1p, 209, 14);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t42 + t44 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t44));
            gm_l5 = t47;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l6;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l6;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t57 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t55 * 1));
                    uint8_t t58 = t57;
                    uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)&t58));
                    gm_l7 = t61;
                    {
                        uint8_t t64 = gm_l7;
                        uint8_t t65 = (!(t64 == ((uint8_t)95ULL)));
                        if (!t65) goto ie63;
                        {
                            int64_t t67 = gm_l4;
                            int64_t r68; bool o69 = __builtin_mul_overflow(t67, ((int64_t)10LL), &r68);
                            int64_t t70 = o69 ? (((t67 < 0) == (((int64_t)10LL) < 0)) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r68;
                            uint8_t t71 = gm_l7;
                            uint8_t r72; bool o73 = __builtin_sub_overflow(t71, ((uint8_t)48ULL), &r72);
                            if (o73) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 211, 41);
                            uint8_t t74 = r72;
                            int64_t t75 = (int64_t)t74;
                            int64_t r76; bool o77 = __builtin_add_overflow(t70, t75, &r76);
                            int64_t t78 = o77 ? ((t75 > 0) ? ((int64_t)9223372036854775807LL) : ((int64_t)(-9223372036854775807LL - 1))) : r76;
                            int64_t t79 = GM_LD(int64_t, gm_cobksqr67t4);
                            int64_t r80; bool o81 = __builtin_add_overflow(t79, ((int64_t)1LL), &r80);
                            if (o81) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 211, 55);
                            int64_t t82 = r80;
                            int64_t t83 = (t78 < t82 ? t78 : t82);
                            gm_l4 = t83;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    goto me59;
mn60:;
                    gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 209, 5);
me59:;
                }
lc48:;
                {
                    uint64_t t85 = gm_l6;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l6 = t88;
                }
            }
lb48:;
        }
    }
    {
        int64_t t90 = gm_l4;
        int64_t t91 = GM_LD(int64_t, gm_cobksqr67t4);
        uint8_t t92 = (t90 > t91);
        if (!t92) goto ie89;
        {
            void * t95 = gm_l1;
            uint8_t t96;
            if (((((uint8_t *)t95))[16] != 0)) goto mn98;
            t96 = ((uint8_t)1);
            goto me97;
mn98:;
            t96 = ((uint8_t)0);
            goto me97;
mn99:;
            gm_trap("no match arm matched the scrutinee", gm_srct4c97p4hek1p, 215, 12);
me97:;
            if (!t96) goto ie94;
            {
                void * t101 = gm_l1;
                uint8_t *t102 = ((uint8_t *)t101);
                int64_t t103 = GM_LD(int64_t, gm_cobksqr67t4);
                gm_tm98ap4112hee t104;
                GM_ST(int64_t, ((uint8_t *)&t104 + 0), t103);
                gm_tabb710d9mq2e t105;
                gm_f_format_ufd866t((void *)&t105, (void *)NULL, cx, NULL, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t106 = t105;
                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_srct4c97p4hek1p, 216, 20);
                gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
                gm_b16_8 t108 = t107;
                gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                gm_th2lqbo77pvsc t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t110 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t110), t109);
                ((uint8_t *)&t110)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, t102, t110);
            }
            goto ix94;
ie94:;
ix94:;
        }
        {
            gm_l4 = ((int64_t)0LL);
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        int64_t t111;
        uint8_t t113 = gm_l2;
        if (!t113) goto ie112;
        int64_t t115 = gm_l4;
        if (t115 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srct4c97p4hek1p, 220, 26);
        int64_t t116 = (int64_t)((uint64_t)0 - (uint64_t)t115);
        int64_t t117 = t116;
        t111 = t117;
        goto ix112;
ie112:;
        int64_t t119 = gm_l4;
        int64_t t120 = t119;
        t111 = t120;
ix112:;
        uint8_t t121 = 0;
        F.ret = t111;
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
inline __attribute__((aligned(4))) uint8_t gm_f_skip_to_closing_quote_qmb6ii3(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_to_closing_quote_qmb6ii3 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 24));
        gm_l1 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = gm_l1;
            gm_tqs5p4n2tq19v t7 = gm_f_byte_b4v8vul(t5, t6);
            gm_tqs5p4n2tq19v t8 = t7;
            if (!((((uint8_t *)&t8))[1] != 0)) goto lb4;
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)&t8));
            gm_l2 = t9;
            {
                uint8_t t13 = gm_l2;
                uint8_t t14 = ((t13 == ((uint8_t)39ULL)));
                if (!t14) goto ie12;
                {
                    void * t16 = gm_l0;
                    uint64_t t17 = gm_l1;
                    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                    if (o19) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 310, 22);
                    uint64_t t20 = r18;
                    GM_ST(uint64_t, (((uint8_t *)t16) + 24), t20);
                }
                {
                    uint8_t t21 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix12;
ie12:;
ix12:;
            }
            {
                uint8_t t23 = gm_l2;
                gm_tqs5p4n2tq19v t24;
                GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)10ULL));
                GM_ST(uint8_t, ((uint8_t *)&t24 + 1), ((uint8_t)13ULL));
                gm_tqs5p4n2tq19v t25 = t24;
                uint8_t t26 = 0;
                uint8_t t27 = t23;
                if (gm_eqjmccqdb90f(((uint8_t *)&t25) + 0, ((uint8_t *)&t27))) t26 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t25) + 1, ((uint8_t *)&t27))) t26 = 1;
                uint8_t t28 = (uint8_t)(t26);
                if (!t28) goto ie22;
                {
                    goto lb4;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                uint64_t t30 = gm_l1;
                uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                if (o32) gm_trap("integer overflow in `+`", gm_srct4c97p4hek1p, 314, 9);
                uint64_t t33 = r31;
                gm_l1 = t33;
            }
lc4:;
        }
lb4:;
    }
    {
        uint8_t t34 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unterminated_at_line_end_v8hbf1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_unterminated_at_line_end_v8hbf1f F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
        gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t2, t3, t5));
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sjblck9bpb6r8));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(27));
        gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sae23ckujbpp9));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(106));
        gm_f_add_note_46v502s(t8, t9);
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 32));
        gm_f_push_o4ckdr1(t11, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l2)));
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
