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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
typedef struct gm_csa3m9feqhg846 { uint64_t f0; gm_csguankko4lmhd f1; uint64_t f2; } gm_csa3m9feqhg846;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_t89vrq05jgle5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; uint64_t f2; } gm_tismvprs8r13f;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tte9sujb6sb62;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; gm_b16_8 f5; } gm_ttq7m9ctqad93;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_csa3m9feqhg846) == 24 && _Alignof(gm_csa3m9feqhg846) == 8 && offsetof(gm_csa3m9feqhg846, f0) == 0 && offsetof(gm_csa3m9feqhg846, f1) == 8 && offsetof(gm_csa3m9feqhg846, f2) == 16, "layout of `VecParts`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_t89vrq05jgle5) == 72 && _Alignof(gm_t89vrq05jgle5) == 8 && offsetof(gm_t89vrq05jgle5, f0) == 0 && offsetof(gm_t89vrq05jgle5, f1) == 16 && offsetof(gm_t89vrq05jgle5, f2) == 32 && offsetof(gm_t89vrq05jgle5, f3) == 48 && offsetof(gm_t89vrq05jgle5, f4) == 56, "layout of `(string, string, string, usize, string)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tismvprs8r13f) == 24 && _Alignof(gm_tismvprs8r13f) == 8 && offsetof(gm_tismvprs8r13f, f0) == 0 && offsetof(gm_tismvprs8r13f, f1) == 8 && offsetof(gm_tismvprs8r13f, f2) == 16, "layout of `VecParts`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tte9sujb6sb62) == 72 && _Alignof(gm_tte9sujb6sb62) == 8 && offsetof(gm_tte9sujb6sb62, f0) == 0 && offsetof(gm_tte9sujb6sb62, f1) == 16 && offsetof(gm_tte9sujb6sb62, f2) == 24 && offsetof(gm_tte9sujb6sb62, f3) == 40 && offsetof(gm_tte9sujb6sb62, f4) == 56 && offsetof(gm_tte9sujb6sb62, f5) == 64, "layout of `(string, u64, string, string, u64, u64)`");
_Static_assert(sizeof(gm_ttq7m9ctqad93) == 80 && _Alignof(gm_ttq7m9ctqad93) == 8 && offsetof(gm_ttq7m9ctqad93, f0) == 0 && offsetof(gm_ttq7m9ctqad93, f1) == 16 && offsetof(gm_ttq7m9ctqad93, f2) == 24 && offsetof(gm_ttq7m9ctqad93, f3) == 40 && offsetof(gm_ttq7m9ctqad93, f4) == 48 && offsetof(gm_ttq7m9ctqad93, f5) == 64, "layout of `(string, u64, string, u64, string, string)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; gm_tngtuiuskn938 v2; gm_tu15p0hi2qlv6 v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; gm_tntn2tf15puib v53; gm_t9i38ih07bojq v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_t5brlu7fal70j;
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t5brlu7fal70j) == 200 && _Alignof(gm_t5brlu7fal70j) == 8 && offsetof(gm_t5brlu7fal70j, f0) == 0 && offsetof(gm_t5brlu7fal70j, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t5brlu7fal70j f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tfpf0s89fpb3r;
_Static_assert(sizeof(gm_tfpf0s89fpb3r) == 224 && _Alignof(gm_tfpf0s89fpb3r) == 8 && offsetof(gm_tfpf0s89fpb3r, f0) == 0 && offsetof(gm_tfpf0s89fpb3r, f1) == 8 && offsetof(gm_tfpf0s89fpb3r, f2) == 208, "layout of `Expr`");
struct gm_fr_fn_b1j5nho {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_dahlogl {
    gm_frame h;
    gm_tfpf0s89fpb3r l2; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_kvivfpi {
    gm_frame h;
    gm_tfpf0s89fpb3r l3; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_tk14v5n {
    gm_frame h;
    gm_tfpf0s89fpb3r l2; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_blend_2oerv54 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_compare_mask_1e833o5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_compare_vrcged3 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_convert_94b6r5u {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_lane_at_i7st991 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_lane_bytes_c5pmsp6 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vec_lane_loop_a3gvooh {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_load_8m1nglt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_load_store_ng9gfpc {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_mask_test_9mgimk6 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_minmax_abs_4le8bpj {
    gm_frame h;
    void * l0; /* fg */
    gm_tvdg94bk7aa22 l2; /* op */
    gm_t88rcucg1b7uk l7; /* lane */
    uint64_t l8; /* bytes */
    gm_b16_8 l12; /* pa */
    gm_th2lqbo77pvsc l13; /* pb */
    gm_b16_8 l15; /* loc */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_parts_srl0rr9 {
    gm_frame h;
    gm_tismvprs8r13f ret;
};
struct gm_fr_vec_reduce_m5gdqn2 {
    gm_frame h;
    gm_tfpf0s89fpb3r l12; /* at */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_select_3f37tvf {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_shuffle_j6jmffl {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* idx */
    gm_tbci5ospk5nnn l12; /* i */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_store_6d0dm94 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_ty_adod8ur {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vector_binary_etnngue {
    gm_frame h;
    void * l0; /* fg */
    void * l1; /* e */
    gm_ts6oqppjh0ba3 l2; /* op */
    gm_t88rcucg1b7uk l8; /* lane */
    uint64_t l9; /* bytes */
    gm_b16_8 l16; /* b */
    gm_b16_8 l17; /* pa */
    gm_b16_8 l20; /* pa */
    gm_b16_8 l21; /* pb */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vector_builtin_a1f1ati {
    gm_frame h;
    gm_t6j4d6ei14c2t ret;
};
struct gm_fr_vector_unary_h7ar7ml {
    gm_frame h;
    void * l0; /* fg */
    void * l1; /* e */
    gm_t88rcucg1b7uk l7; /* lane */
    uint64_t l8; /* bytes */
    gm_b16_8 l10; /* pv */
    gm_t5o0tpv5jka2l ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fn_b1j5nho(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_dahlogl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_kvivfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_tk14v5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_21ahlbb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_27c89bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_61d2klb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_f89fec3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h9otq43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
__attribute__((aligned(4))) void gm_f_format_hjgrmth(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jo1gjr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mdlbfc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mnm0p9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nhrpa25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_njau13r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nvhh8d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t4gbkjg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vmsom9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4bjepb3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4ht2lgi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5476lg2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6cjbh5j(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6mfrmej(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7cnqmuh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e7j1ug(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ar0alse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_c0cfb71(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_di76emp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gjfdarm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_nac2kse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ou0dv81(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_pai11tb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_sk93pav(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ssnaj62(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_blend_2oerv54(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_mask_1e833o5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_vec_compare_vrcged3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vec_convert_94b6r5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_lane_at_i7st991(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint64_t gm_f_vec_lane_bytes_c5pmsp6(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vec_lane_loop_a3gvooh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_8m1nglt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vec_load_store_ng9gfpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vec_mask_test_9mgimk6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, void * a3);
__attribute__((aligned(4))) void gm_f_vec_minmax_abs_4le8bpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vec_parts_srl0rr9(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_csa3m9feqhg846 gm_f_vec_parts_srl0rr9_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vec_reduce_m5gdqn2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, void * a3);
__attribute__((aligned(4))) void gm_f_vec_select_3f37tvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vec_shuffle_j6jmffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_store_6d0dm94(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_ty_adod8ur(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tqs5p4n2tq19v a3);
__attribute__((aligned(4))) void gm_f_vector_binary_etnngue(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_vector_builtin_a1f1ati(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_vector_unary_h7ar7ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s0p6vt35dv9nv[6];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s9d990kf69gae[7];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sb22ajpcscr7v[41];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sjmccqdb90fnl[3];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_snd30h4tliigb[12];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src7hmnlrtmirgt[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_svaet5ugb1fj3[22];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) void gm_f_fn_b1j5nho(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b1j5nho F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* pbv */
    gm_b16_8 gm_l3; /* y */
    gm_b16_8 gm_l4; /* f */
    gm_tuglhjjmj2g58 gm_l5; /* ii */
    gm_b16_8 gm_l6; /* min */
    GM_STACK_CHECK(&F, 1191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l8));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l12));
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t7;
    }
    {
        if (!((((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l13))[16] != 0)) goto ie8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l13));
        gm_l2 = t9;
        {
            void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
            uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l8));
            gm_b16_8 t14 = gm_l2;
            gm_b16_8 t15 = gm_l0;
            gm_b16_8 t16 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t11, t12, t13, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t16;
        }
        {
            gm_b16_8 t17;
            uint8_t t19 = ((GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l2) + 0) == ((uint8_t)53ULL)));
            if (!t19) goto ie18;
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s12ng38ve3205));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
            gm_b16_8 t22 = t21;
            t17 = t22;
            goto ix18;
ie18:;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sgjc0ebf56hf4));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(7));
            gm_b16_8 t25 = t24;
            t17 = t25;
ix18:;
            gm_l4 = t17;
        }
        {
            void * t26 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
            gm_tfeab279c16rr t28;
            gm_b16_8 t29 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
            gm_b16_8 t30 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
            gm_b16_8 t31 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t28 + 32, t31);
            gm_tabb710d9mq2e t32;
            gm_f_format_lqnh3lt((void *)&t32, (void *)NULL, cx, NULL, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 8, gm_src7hmnlrtmirgt, 350, 44);
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&t33) + 8));
            gm_b16_8 t35 = t34;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_b16_8 t37 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t26, t27, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 32, 0);
            ((uint8_t *)&t38)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
            gm_t5o0tpv5jka2l t39 = t38;
            uint8_t t40 = ((((uint8_t *)&t39))[0] != 0);
            F.ret = t38;
            goto gm_fret;
        }
        goto ix8;
ie8:;
ix8:;
    }
    {
        void * t41 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
        gm_tqcc6dmlne11b t43;
        gm_f_int_info_2tcdf1n((void *)&t43, (void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqcc6dmlne11b t44 = t43;
        if (!((((uint8_t *)&t44))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 352, 19);
        gm_tuglhjjmj2g58 t45 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t44));
        gm_l5 = t45;
    }
    {
        void * t46 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
        gm_tuglhjjmj2g58 t48 = gm_l5;
        gm_tbci5ospk5nnn t49;
        gm_f_ii_min_lku7pdi((void *)&t49, (void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t50 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t46, t47, &t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t50;
    }
    {
        void * t51 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
        gm_tfeab279c16rr t52;
        gm_b16_8 t53 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
        gm_b16_8 t54 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t54);
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l15));
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 32, t55);
        gm_b0_1 t56 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t51, &t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tfeab279c16rr t57;
        gm_b16_8 t58 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
        gm_b16_8 t59 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
        gm_b16_8 t60 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t57 + 32, t60);
        gm_tabb710d9mq2e t61;
        gm_f_format_iqjnutf((void *)&t61, (void *)NULL, cx, NULL, &t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t62 = t61;
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 8, gm_src7hmnlrtmirgt, 355, 20);
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
        gm_b16_8 t64 = t63;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        gm_t5o0tpv5jka2l t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 32, 0);
        ((uint8_t *)&t66)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
        gm_t5o0tpv5jka2l t67 = t66;
        uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
        F.ret = t66;
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
inline __attribute__((aligned(4))) void gm_f_fn_dahlogl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dahlogl F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* y */
    GM_STACK_CHECK(&F, 628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l7));
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l8));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l10));
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t7;
    }
    {
        memset((uint8_t *)&F.l2 + 4, 0, 4);
        memset((uint8_t *)&F.l2 + 220, 0, 4);
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l7));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l2 + 0, t8);
        gm_t5brlu7fal70j t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t5brlu7fal70j, (uint8_t *)&F.l2 + 8, t9);
        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 208));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l2 + 208, t11);
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l0));
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_t9qkh0rsooc8d t14;
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
        gm_ts6oqppjh0ba3 t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_h7ar7ml *)((&F.h)->up))->l7));
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_b16_8 t18 = gm_l1;
        gm_t5o0tpv5jka2l t19;
        gm_f_int_binary_kp3u7i8((void *)&t19, (void *)NULL, cx, NULL, t12, t13, t15, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t20 = t19;
        if (((((uint8_t *)&t20))[0] != 0)) {
            gm_t08olq97rscn3 t21 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t20) + 8));
            gm_t5o0tpv5jka2l t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
            ((uint8_t *)&t22)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t22 + 8), t21);
            F.ret = t22;
            goto gm_fret;
        }
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
        gm_t5o0tpv5jka2l t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t24 + 32, 0);
        ((uint8_t *)&t24)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
        gm_t5o0tpv5jka2l t25 = t24;
        uint8_t t26 = ((((uint8_t *)&t25))[0] != 0);
        F.ret = t24;
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
inline __attribute__((aligned(4))) void gm_f_fn_kvivfpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kvivfpi F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* y */
    GM_STACK_CHECK(&F, 711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l9));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l20));
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t7;
    }
    {
        void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l9));
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l21));
        gm_b16_8 t12 = gm_l0;
        gm_b16_8 t13 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t8, t9, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t13;
    }
    {
        memset((uint8_t *)&F.l3 + 4, 0, 4);
        memset((uint8_t *)&F.l3 + 220, 0, 4);
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l3 + 0, t14);
        gm_t5brlu7fal70j t15;
        memset(&t15, 0, sizeof t15);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t5brlu7fal70j, (uint8_t *)&F.l3 + 8, t15);
        void * t16 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t16) + 208));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l3 + 208, t17);
    }
    {
        void * t18 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l0));
        void * t19 = (void *)((uint8_t *)&F.l3);
        gm_ts6oqppjh0ba3 t20 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l2));
        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        gm_b16_8 t22 = gm_l1;
        gm_b16_8 t23 = gm_l2;
        gm_t5o0tpv5jka2l t24;
        gm_f_int_binary_kp3u7i8((void *)&t24, (void *)NULL, cx, NULL, t18, t19, t20, t21, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) {
            gm_t08olq97rscn3 t26 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t25) + 8));
            gm_t5o0tpv5jka2l t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t27 + 8), t26);
            F.ret = t27;
            goto gm_fret;
        }
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_t5o0tpv5jka2l t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t29 + 32, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
        gm_t5o0tpv5jka2l t30 = t29;
        uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
        F.ret = t29;
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
inline __attribute__((aligned(4))) void gm_f_fn_tk14v5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tk14v5n F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    GM_STACK_CHECK(&F, 627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l9));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l17));
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t7;
    }
    {
        memset((uint8_t *)&F.l2 + 4, 0, 4);
        memset((uint8_t *)&F.l2 + 220, 0, 4);
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l2 + 0, t8);
        gm_t5brlu7fal70j t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t5brlu7fal70j, (uint8_t *)&F.l2 + 8, t9);
        void * t10 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l1));
        gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t10) + 208));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l2 + 208, t11);
    }
    {
        void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l0));
        void * t13 = (void *)((uint8_t *)&F.l2);
        gm_ts6oqppjh0ba3 t14 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l2));
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l8));
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_etnngue *)((&F.h)->up))->l16));
        gm_t5o0tpv5jka2l t18;
        gm_f_int_binary_kp3u7i8((void *)&t18, (void *)NULL, cx, NULL, t12, t13, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t19) + 8));
            gm_t5o0tpv5jka2l t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t21 + 8), t20);
            F.ret = t21;
            goto gm_fret;
        }
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_t5o0tpv5jka2l t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 32, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
        gm_t5o0tpv5jka2l t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_blend_2oerv54(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_blend_2oerv54 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* b */
    gm_tismvprs8r13f gm_l5; /* vp */
    gm_b16_8 gm_l6; /* w */
    gm_b16_8 gm_l7; /* u */
    gm_b16_8 gm_l8; /* s */
    gm_b16_8 gm_l9; /* mm */
    GM_STACK_CHECK(&F, 875u);
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
        gm_f_vec_parts_srl0rr9((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_tqs5p4n2tq19v t7;
        GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)1));
        ((uint8_t *)&t7)[1] = 1;
        gm_b16_8 t8 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_tqs5p4n2tq19v t12;
        GM_ST(uint8_t, ((uint8_t *)&t12), ((uint8_t)0));
        ((uint8_t *)&t12)[1] = 1;
        gm_b16_8 t13 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t9, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t13;
    }
    {
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_tqs5p4n2tq19v t17;
        memset(&t17, 0, sizeof t17);
        gm_b16_8 t18 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t18;
    }
    {
        void * t19 = gm_l0;
        uint32_t t20 = gm_f_fresh_5kokjmg(t19);
        gm_t88rcucg1b7uk t21;
        GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
        gm_tabb710d9mq2e t22;
        gm_f_format_apgk46s((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_src7hmnlrtmirgt, 232, 11);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_l9 = t24;
    }
    {
        void * t25 = gm_l0;
        gm_tud7oovfvnf43 t26;
        gm_b16_8 t27 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
        gm_b16_8 t28 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
        gm_b16_8 t29 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 32, t29);
        gm_b16_8 t30 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 48, t30);
        gm_b16_8 t31 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t26 + 64, t31);
        gm_b0_1 t32 = gm_f_linef_di76emp((void *)NULL, cx, NULL, t25, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2d9c7aju8hp t33;
        gm_b16_8 t34 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 0, t34);
        gm_b16_8 t35 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 16, t35);
        gm_b16_8 t36 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 32, t36);
        gm_b16_8 t37 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 48, t37);
        gm_b16_8 t38 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 64, t38);
        gm_b16_8 t39 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 80, t39);
        gm_b16_8 t40 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t33 + 96, t40);
        gm_tabb710d9mq2e t41;
        gm_f_format_l9df9v3((void *)&t41, (void *)NULL, cx, NULL, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src7hmnlrtmirgt, 234, 12);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_b16_8 t44 = t43;
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_mask_1e833o5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_compare_mask_1e833o5 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* sym */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* b */
    gm_tismvprs8r13f gm_l5; /* vp */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* bool_ty */
    gm_b16_8 gm_l8; /* wide */
    gm_b16_8 gm_l9; /* m8 */
    gm_b16_8 gm_l10; /* c */
    GM_STACK_CHECK(&F, 813u);
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
        gm_f_vec_parts_srl0rr9((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 8) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 8);
        gm_l6 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t11;
    }
    {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), ((uint8_t)1));
        ((uint8_t *)&t15)[1] = 1;
        gm_b16_8 t16 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t12, t13, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t16;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t19 = gm_l7;
        gm_tqs5p4n2tq19v t20;
        memset(&t20, 0, sizeof t20);
        gm_b16_8 t21 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t17, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t21;
    }
    {
        void * t22 = gm_l0;
        uint32_t t23 = gm_f_fresh_5kokjmg(t22);
        gm_t88rcucg1b7uk t24;
        GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_3tnvb6a((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src7hmnlrtmirgt, 220, 10);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_l10 = t27;
    }
    {
        void * t28 = gm_l0;
        gm_tif3elqk01bdl t29;
        gm_b16_8 t30 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t30);
        gm_b16_8 t31 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 16, t31);
        gm_b16_8 t32 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 32, t32);
        gm_b16_8 t33 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 48, t33);
        gm_b16_8 t34 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 64, t34);
        gm_b16_8 t35 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t29 + 80, t35);
        gm_b0_1 t36 = gm_f_linef_c0cfb71((void *)NULL, cx, NULL, t28, &t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l6;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_t88rcucg1b7uk t40 = gm_l7;
        gm_t88rcucg1b7uk t41 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t38, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t42;
        gm_b16_8 t43 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        gm_b16_8 t44 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t44);
        gm_tabb710d9mq2e t45;
        gm_f_format_f89fec3((void *)&t45, (void *)NULL, cx, NULL, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src7hmnlrtmirgt, 222, 50);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_b16_8 t50 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t37, t41, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t51 = 0;
        F.ret = t50;
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
__attribute__((aligned(4))) void gm_f_vec_compare_vrcged3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2) {
    struct gm_fr_vec_compare_vrcged3 F;
    void * gm_l0; /* fg */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    gm_b16_8 gm_l2; /* args */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* va */
    gm_b16_8 gm_l7; /* vb */
    gm_b16_8 gm_l8; /* sym */
    GM_STACK_CHECK(&F, 903u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t2 >= (uintptr_t)(t3)) gm_trap_index(t2, t3, gm_src7hmnlrtmirgt, 253, 10);
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t2 * 224) + 0));
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_src7hmnlrtmirgt, 254, 23);
        void * t8 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t6 * 224);
        gm_t5o0tpv5jka2l t9;
        gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t5, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) {
            gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t10) + 8));
            gm_t5o0tpv5jka2l t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), t11);
            F.ret = t12;
            goto gm_fret;
        }
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l4 = t13;
    }
    {
        void * t14 = gm_l0;
        uint64_t t15 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src7hmnlrtmirgt, 255, 23);
        void * t17 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 224);
        gm_t5o0tpv5jka2l t18;
        gm_f_expr_porbc9t((void *)&t18, (void *)NULL, cx, NULL, t14, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) {
            gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t19) + 8));
            gm_t5o0tpv5jka2l t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            ((uint8_t *)&t21)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t21 + 8), t20);
            F.ret = t21;
            goto gm_fret;
        }
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_l5 = t22;
    }
    {
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l3;
        gm_b16_8 t25 = gm_l4;
        gm_b16_8 t26 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_t88rcucg1b7uk t28 = gm_l3;
        gm_b16_8 t29 = gm_l5;
        gm_b16_8 t30 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t30;
    }
    {
        gm_b16_8 t31;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t40) { case ((uint8_t)0ULL): goto ma38; case ((uint8_t)1ULL): goto ma38; case ((uint8_t)2ULL): goto ma38; case ((uint8_t)3ULL): goto ma38; case ((uint8_t)4ULL): goto ma38; case ((uint8_t)5ULL): goto ma38; case ((uint8_t)6ULL): goto ma38; case ((uint8_t)7ULL): goto ma38; case ((uint8_t)8ULL): goto ma38; case ((uint8_t)9ULL): goto ma38; case ((uint8_t)10ULL): goto ma38; case ((uint8_t)11ULL): goto ma38; case ((uint8_t)12ULL): goto ma38; case ((uint8_t)13ULL): goto ma38; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma38; case ((uint8_t)16ULL): goto ma38; case ((uint8_t)17ULL): goto ma38; case ((uint8_t)18ULL): goto ma38; case ((uint8_t)19ULL): goto ma38; case ((uint8_t)20ULL): goto ma38; case ((uint8_t)21ULL): goto ma38; case ((uint8_t)22ULL): goto ma38; case ((uint8_t)23ULL): goto ma38; case ((uint8_t)24ULL): goto ma38; case ((uint8_t)25ULL): goto ma38; case ((uint8_t)26ULL): goto ma38; case ((uint8_t)27ULL): goto ma38; case ((uint8_t)28ULL): goto ma38; case ((uint8_t)29ULL): goto ma38; case ((uint8_t)30ULL): goto ma38; case ((uint8_t)31ULL): goto ma38; case ((uint8_t)32ULL): goto ma38; case ((uint8_t)33ULL): goto ma38; case ((uint8_t)34ULL): goto ma38; case ((uint8_t)35ULL): goto ma38; case ((uint8_t)36ULL): goto ma38; case ((uint8_t)37ULL): goto ma38; case ((uint8_t)38ULL): goto ma38; case ((uint8_t)39ULL): goto ma38; case ((uint8_t)40ULL): goto ma33; case ((uint8_t)41ULL): goto ma34; case ((uint8_t)42ULL): goto ma35; case ((uint8_t)43ULL): goto ma36; case ((uint8_t)44ULL): goto ma37; case ((uint8_t)45ULL): goto ma38; case ((uint8_t)46ULL): goto ma38; case ((uint8_t)47ULL): goto ma38; case ((uint8_t)48ULL): goto ma38; case ((uint8_t)49ULL): goto ma38; case ((uint8_t)50ULL): goto ma38; case ((uint8_t)51ULL): goto ma38; case ((uint8_t)52ULL): goto ma38; case ((uint8_t)53ULL): goto ma38; case ((uint8_t)54ULL): goto ma38; case ((uint8_t)55ULL): goto ma38; case ((uint8_t)56ULL): goto ma38; case ((uint8_t)57ULL): goto ma38; case ((uint8_t)58ULL): goto ma38; case ((uint8_t)59ULL): goto ma38; case ((uint8_t)60ULL): goto ma38; case ((uint8_t)61ULL): goto ma38; case ((uint8_t)62ULL): goto ma38; case ((uint8_t)63ULL): goto ma38; case ((uint8_t)64ULL): goto ma38; case ((uint8_t)65ULL): goto ma38; case ((uint8_t)66ULL): goto ma38; case ((uint8_t)67ULL): goto ma38; case ((uint8_t)68ULL): goto ma38; case ((uint8_t)69ULL): goto ma38; case ((uint8_t)70ULL): goto ma38; case ((uint8_t)71ULL): goto ma38; case ((uint8_t)72ULL): goto ma38; case ((uint8_t)73ULL): goto ma38; case ((uint8_t)74ULL): goto ma38; case ((uint8_t)75ULL): goto ma38; case ((uint8_t)76ULL): goto ma38; case ((uint8_t)77ULL): goto ma38; case ((uint8_t)78ULL): goto ma38; case ((uint8_t)79ULL): goto ma38; case ((uint8_t)80ULL): goto ma38; case ((uint8_t)81ULL): goto ma38; default: goto ma38; }
ma33:;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t42 != ((uint8_t)40ULL)) goto mn41;
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sphr6fu31dtrl));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
        t31 = t43;
        goto me32;
mn41:;
ma34:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t45 != ((uint8_t)41ULL)) goto mn44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_stnlf4pvbut1c));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
        t31 = t46;
        goto me32;
mn44:;
ma35:;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t48 != ((uint8_t)42ULL)) goto mn47;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s1f440st4mnkp));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
        t31 = t49;
        goto me32;
mn47:;
ma36:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t51 != ((uint8_t)43ULL)) goto mn50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s4g3cajt15dmi));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
        t31 = t52;
        goto me32;
mn50:;
ma37:;
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t54 != ((uint8_t)44ULL)) goto mn53;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sm5be4659ulci));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
        t31 = t55;
        goto me32;
mn53:;
ma38:;
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s02ru83e3fffc));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(2));
        t31 = t57;
        goto me32;
mn56:;
ma39:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 258, 19);
me32:;
        gm_l8 = t31;
    }
    {
        void * t58 = gm_l0;
        gm_t88rcucg1b7uk t59 = gm_l3;
        gm_b16_8 t60 = gm_l8;
        gm_b16_8 t61 = gm_l6;
        gm_b16_8 t62 = gm_l7;
        gm_b16_8 t63 = gm_f_vec_compare_mask_1e833o5((void *)NULL, cx, NULL, t58, t59, t60, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t64 + 32, 0);
        ((uint8_t *)&t64)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 8), t63);
        gm_t5o0tpv5jka2l t65 = t64;
        uint8_t t66 = ((((uint8_t *)&t65))[0] != 0);
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_vec_convert_94b6r5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_vec_convert_94b6r5u F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_tismvprs8r13f gm_l3; /* vp */
    gm_b16_8 gm_l4; /* v */
    gm_b16_8 gm_l5; /* va */
    gm_b16_8 gm_l6; /* tv */
    GM_STACK_CHECK(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
        gm_f_vec_parts_srl0rr9((void *)&gm_l3, t2, GM_CONV(gm_csguankko4lmhd, t4));
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l2;
        gm_t5o0tpv5jka2l t7;
        gm_f_expr_porbc9t((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_t08olq97rscn3 t9 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t8) + 8));
            gm_t5o0tpv5jka2l t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t10 + 8), t9);
            F.ret = t10;
            goto gm_fret;
        }
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l2;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_b16_8 t15 = gm_l4;
        gm_b16_8 t16 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t12, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t16;
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
        gm_tqs5p4n2tq19v t20;
        memset(&t20, 0, sizeof t20);
        gm_b16_8 t21 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t17, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t21;
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t23) + 0));
        gm_t6j6smj7qoctu t25;
        gm_b16_8 t26 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
        gm_b16_8 t27 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
        gm_tabb710d9mq2e t28;
        gm_f_format_s8juhic((void *)&t28, (void *)NULL, cx, NULL, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src7hmnlrtmirgt, 385, 31);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_b16_8 t33 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t22, t24, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t34 + 32, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 8), t33);
        gm_t5o0tpv5jka2l t35 = t34;
        uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
        F.ret = t34;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_lane_at_i7st991(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_lane_at_i7st991 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* lane */
    uint64_t gm_l2; /* bytes */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* i */
    gm_b16_8 gm_l5; /* ce */
    GM_STACK_CHECK(&F, 381u);
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
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        gm_tttl9nsdo1iu6 t9;
        gm_b16_8 t10 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t10);
        gm_b16_8 t11 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t11);
        gm_b16_8 t12 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 32, t12);
        uint64_t t13 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t9 + 48, t13);
        gm_tabb710d9mq2e t14;
        gm_f_format_mnm0p9v((void *)&t14, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src7hmnlrtmirgt, 101, 25);
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_b16_8 t17 = t16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_b16_8 t19 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t7, t8, t18);
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
inline __attribute__((aligned(4))) uint64_t gm_f_vec_lane_bytes_c5pmsp6(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vec_lane_bytes_c5pmsp6 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* lane */
    uint16_t gm_l2; /* bits */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 223u);
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
    gm_f_kind_ghvav22((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_t53at80pfdus3 t6 = t5;
    uint64_t t7;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    switch (t13) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t15 != ((uint8_t)0ULL)) goto mn14;
    uint16_t t16 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
    gm_l2 = t16;
    uint16_t t17 = gm_l2;
    uint64_t t18 = (uint64_t)t17;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 22, 22);
    uint64_t t19 = (uint64_t)(t18 / ((uint64_t)8ULL));
    t7 = t19;
    goto me8;
mn14:;
ma10:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t21 != ((uint8_t)3ULL)) goto mn20;
    uint16_t t22 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
    gm_l3 = t22;
    uint16_t t23 = gm_l3;
    uint64_t t24 = (uint64_t)t23;
    if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 23, 21);
    uint64_t t25 = (uint64_t)(t24 / ((uint64_t)8ULL));
    t7 = t25;
    goto me8;
mn20:;
ma11:;
    t7 = ((uint64_t)1ULL);
    goto me8;
mn26:;
ma12:;
    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 21, 54);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_vec_lane_loop_a3gvooh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3) {
    struct gm_fr_vec_lane_loop_a3gvooh F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* res_t */
    uint64_t gm_l2; /* n */
    gm_closure gm_l3; /* f */
    gm_tismvprs8r13f gm_l4; /* p */
    gm_b16_8 gm_l5; /* ce */
    gm_b16_8 gm_l6; /* res */
    gm_b16_8 gm_l7; /* i */
    gm_b16_8 gm_l8; /* v */
    GM_STACK_CHECK(&F, 1025u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_f_vec_parts_srl0rr9((void *)&gm_l4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t8 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_b16_8 t11 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t12 = gm_l0;
        uint32_t t13 = gm_f_fresh_5kokjmg(t12);
        gm_t88rcucg1b7uk t14;
        GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t13);
        gm_tabb710d9mq2e t15;
        gm_f_format_c0h6o29((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src7hmnlrtmirgt, 88, 10);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l7 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_tmr5oep1578tb t19;
        gm_b16_8 t20 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
        gm_b16_8 t21 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
        uint64_t t22 = gm_l2;
        GM_ST(uint64_t, (uint8_t *)&t19 + 32, t22);
        gm_b16_8 t23 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t19 + 40, t23);
        gm_b0_1 t24 = gm_f_linef_gjfdarm((void *)NULL, cx, NULL, t18, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 224));
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 90, 5);
        uint64_t t30 = r28;
        GM_ST(uint64_t, (((uint8_t *)t25) + 224), t30);
    }
    {
        gm_closure t31 = gm_l3;
        gm_b16_8 t32 = gm_l7;
        gm_b16_8 t33 = t32;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_closure t35 = t31;
        gm_node *ccx36 = t35.cx;
        gm_node cn37; { gm_node *f = gm_find(cx, 587034433); if (f) { cn37 = *f; cn37.next = ccx36; ccx36 = &cn37; } }
        gm_node cn38; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn38 = *f; cn38.next = ccx36; ccx36 = &cn38; } }
        void * t39 = t35.code;
        gm_t5o0tpv5jka2l t40;
        if (GM_PTR_TAGGED(t39)) {
            t40 = ((gm_t5o0tpv5jka2l (*)(gm_b16_8))GM_PTR_UNTAG(t39))(t34);
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t39)((void *)&t40, t35.env, ccx36, NULL, t34);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) {
            gm_t08olq97rscn3 t42 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t41) + 8));
            gm_t5o0tpv5jka2l t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t43 + 8), t42);
            F.ret = t43;
            goto gm_fret;
        }
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
        gm_l8 = t44;
    }
    {
        void * t45 = gm_l0;
        gm_t89vrq05jgle5 t46;
        gm_b16_8 t47 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
        gm_b16_8 t48 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
        gm_b16_8 t49 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 32, t49);
        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
        GM_ST(uint64_t, (uint8_t *)&t46 + 48, t50);
        gm_b16_8 t51 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t46 + 56, t51);
        gm_b0_1 t52 = gm_f_linef_ou0dv81((void *)NULL, cx, NULL, t45, &t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l0;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 224));
        uint64_t r56; bool o57 = __builtin_sub_overflow(t55, ((uint64_t)1ULL), &r56);
        if (o57) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 93, 5);
        uint64_t t58 = r56;
        GM_ST(uint64_t, (((uint8_t *)t53) + 224), t58);
    }
    {
        void * t59 = gm_l0;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
        gm_b0_1 t61 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t62 = gm_l6;
        gm_t5o0tpv5jka2l t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t63 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t63 + 32, 0);
        ((uint8_t *)&t63)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 8), t62);
        gm_t5o0tpv5jka2l t64 = t63;
        uint8_t t65 = ((((uint8_t *)&t64))[0] != 0);
        F.ret = t63;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_8m1nglt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_load_8m1nglt F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_tismvprs8r13f gm_l3; /* p */
    gm_b16_8 gm_l4; /* vt */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 523u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_f_vec_parts_srl0rr9((void *)&gm_l3, t2, GM_CONV(gm_csguankko4lmhd, t3));
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        gm_b16_8 t8 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        gm_t88rcucg1b7uk t10 = gm_l1;
        gm_b16_8 t11 = gm_l2;
        gm_b16_8 t12 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t12;
    }
    {
        void * t13 = gm_l0;
        uint32_t t14 = gm_f_fresh_5kokjmg(t13);
        gm_t88rcucg1b7uk t15;
        GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t14);
        gm_tabb710d9mq2e t16;
        gm_f_format_1mojd22((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_src7hmnlrtmirgt, 68, 13);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_l6 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_tud7oovfvnf43 t20;
        gm_b16_8 t21 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
        gm_b16_8 t22 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
        gm_b16_8 t23 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 32, t23);
        gm_b16_8 t24 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 48, t24);
        gm_b16_8 t25 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 64, t25);
        gm_b0_1 t26 = gm_f_linef_5476lg2((void *)NULL, cx, NULL, t19, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t27 = gm_l6;
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        uint8_t t30 = 0;
        F.ret = t29;
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
__attribute__((aligned(4))) void gm_f_vec_load_store_ng9gfpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_vec_load_store_ng9gfpc F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    uint8_t gm_l4; /* is_load */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    void * gm_l6; /* sl */
    gm_tismvprs8r13f gm_l7; /* vp */
    uint64_t gm_l8; /* n */
    gm_th2lqbo77pvsc gm_l9; /* v */
    gm_b16_8 gm_l10; /* s */
    gm_b16_8 gm_l11; /* ps */
    gm_b16_8 gm_l12; /* p */
    gm_b16_8 gm_l13; /* len */
    gm_b16_8 gm_l14; /* loc */
    gm_b16_8 gm_l15; /* what */
    uint64_t gm_l16; /* size */
    gm_b16_8 gm_l17; /* vv */
    gm_b16_8 gm_l18; /* pv */
    gm_b16_8 gm_l19; /* res */
    GM_STACK_CHECK(&F, 1750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t2 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)58ULL)));
        gm_l4 = t2;
    }
    {
        gm_t88rcucg1b7uk t3;
        uint8_t t5 = gm_l4;
        if (!t5) goto ie4;
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = t8;
        t3 = t9;
        goto ix4;
ie4:;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_src7hmnlrtmirgt, 420, 38);
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 224) + 0));
        gm_t88rcucg1b7uk t14 = t13;
        t3 = t14;
ix4:;
        gm_l5 = t3;
    }
    {
        void * t15;
        uint8_t t17 = gm_l4;
        if (!t17) goto ie16;
        uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src7hmnlrtmirgt, 421, 25);
        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t19 * 224);
        void * t22 = t21;
        t15 = t22;
        goto ix16;
ie16:;
        uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src7hmnlrtmirgt, 421, 43);
        void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 224);
        void * t27 = t26;
        t15 = t27;
ix16:;
        gm_l6 = t15;
    }
    {
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l5;
        gm_f_vec_parts_srl0rr9((void *)&gm_l7, t28, GM_CONV(gm_csguankko4lmhd, t29));
    }
    {
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 0));
        gm_l8 = t30;
    }
    {
        gm_th2lqbo77pvsc t31;
        memset(&t31, 0, sizeof t31);
        gm_l9 = t31;
    }
    {
        uint8_t t33 = gm_l4;
        uint8_t t34 = (uint8_t)!t33;
        if (!t34) goto ie32;
        {
            void * t36 = gm_l0;
            uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_src7hmnlrtmirgt, 425, 39);
            void * t39 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t37 * 224);
            gm_t5o0tpv5jka2l t40;
            gm_f_expr_porbc9t((void *)&t40, (void *)NULL, cx, NULL, t36, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t41 = t40;
            if (((((uint8_t *)&t41))[0] != 0)) {
                gm_t08olq97rscn3 t42 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t41) + 8));
                gm_t5o0tpv5jka2l t43;
                GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                ((uint8_t *)&t43)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t43 + 8), t42);
                F.ret = t43;
                goto gm_fret;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
            gm_th2lqbo77pvsc t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[16] = 1;
            gm_l9 = t45;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t46 = gm_l0;
        void * t47 = gm_l6;
        gm_t5o0tpv5jka2l t48;
        gm_f_expr_porbc9t((void *)&t48, (void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) {
            gm_t08olq97rscn3 t50 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t49) + 8));
            gm_t5o0tpv5jka2l t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            ((uint8_t *)&t51)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t51 + 8), t50);
            F.ret = t51;
            goto gm_fret;
        }
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_l10 = t52;
    }
    {
        void * t53 = gm_l0;
        void * t54 = gm_l6;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
        gm_b16_8 t56 = gm_l10;
        gm_b16_8 t57 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t53, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t57;
    }
    {
        gm_b16_8 t58 = gm_l11;
        gm_b16_8 t59 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t59;
    }
    {
        void * t60 = gm_l0;
        gm_b16_8 t61 = gm_l11;
        gm_b16_8 t62 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t62;
    }
    {
        void * t64 = gm_l0;
        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 0));
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 1136) + 0));
        if (!t66) goto ie63;
        {
            void * t68 = gm_l0;
            void * t69 = gm_l1;
            gm_b16_8 t70 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t70;
        }
        {
            gm_b16_8 t71;
            uint8_t t73 = gm_l4;
            if (!t73) goto ie72;
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sqfrv3g5v9m2n));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(4));
            gm_b16_8 t76 = t75;
            t71 = t76;
            goto ix72;
ie72:;
            gm_b16_8 t78;
            GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s0ugop4l95o1j));
            GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(5));
            gm_b16_8 t79 = t78;
            t71 = t79;
ix72:;
            gm_l15 = t71;
        }
        {
            void * t80 = gm_l0;
            gm_ttq7m9ctqad93 t81;
            gm_b16_8 t82 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t82);
            uint64_t t83 = gm_l8;
            GM_ST(uint64_t, (uint8_t *)&t81 + 16, t83);
            gm_b16_8 t84 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 24, t84);
            uint64_t t85 = gm_l8;
            GM_ST(uint64_t, (uint8_t *)&t81 + 40, t85);
            gm_b16_8 t86 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 48, t86);
            gm_b16_8 t87 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t81 + 64, t87);
            gm_b0_1 t88 = gm_f_linef_7e7j1ug((void *)NULL, cx, NULL, t80, &t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        uint64_t t89 = gm_l8;
        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
        uint64_t r91; bool o92 = __builtin_mul_overflow(t89, t90, &r91);
        if (o92) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 435, 13);
        uint64_t t93 = r91;
        gm_l16 = t93;
    }
    {
        if (!((((uint8_t *)&gm_l9))[16] != 0)) goto ie94;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l17 = t95;
        {
            void * t97 = gm_l0;
            gm_t88rcucg1b7uk t98 = gm_l5;
            gm_b16_8 t99 = gm_l17;
            gm_b16_8 t100 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t100;
        }
        {
            void * t101 = gm_l0;
            gm_tftksr4op114e t102;
            gm_b16_8 t103 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
            gm_b16_8 t104 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 16, t104);
            uint64_t t105 = gm_l16;
            GM_ST(uint64_t, (uint8_t *)&t102 + 32, t105);
            gm_b0_1 t106 = gm_f_linef_4ht2lgi((void *)NULL, cx, NULL, t101, &t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t107 = gm_l0;
            gm_b16_8 t108 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 32, 0);
            ((uint8_t *)&t109)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 8), t108);
            gm_t5o0tpv5jka2l t110 = t109;
            uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
            F.ret = t109;
            goto gm_fret;
        }
        goto ix94;
ie94:;
ix94:;
    }
    {
        void * t112 = gm_l0;
        gm_t88rcucg1b7uk t113 = gm_l5;
        gm_b16_8 t114 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t112, t113);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t114;
    }
    {
        void * t115 = gm_l0;
        gm_tftksr4op114e t116;
        gm_b16_8 t117 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
        gm_b16_8 t118 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t116 + 16, t118);
        uint64_t t119 = gm_l16;
        GM_ST(uint64_t, (uint8_t *)&t116 + 32, t119);
        gm_b0_1 t120 = gm_f_linef_6cjbh5j((void *)NULL, cx, NULL, t115, &t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t121 = gm_l19;
        gm_t5o0tpv5jka2l t122;
        GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t122 + 32, 0);
        ((uint8_t *)&t122)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t122 + 8), t121);
        gm_t5o0tpv5jka2l t123 = t122;
        uint8_t t124 = ((((uint8_t *)&t123))[0] != 0);
        F.ret = t122;
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
__attribute__((aligned(4))) void gm_f_vec_mask_test_9mgimk6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, void * a3) {
    struct gm_fr_vec_mask_test_9mgimk6 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    void * gm_l3; /* x */
    uint64_t gm_l4; /* n */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* p */
    gm_b16_8 gm_l7; /* count */
    gm_b16_8 gm_l8; /* r */
    GM_STACK_CHECK(&F, 1016u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l3;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t3) + 0));
        gm_tismvprs8r13f t5;
        gm_f_vec_parts_srl0rr9((void *)&t5, t2, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tismvprs8r13f t6 = t5;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
        gm_l4 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l3;
        gm_t5o0tpv5jka2l t10;
        gm_f_expr_porbc9t((void *)&t10, (void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_t08olq97rscn3 t12 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t11) + 8));
            gm_t5o0tpv5jka2l t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t13 + 8), t12);
            F.ret = t13;
            goto gm_fret;
        }
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_l5 = t14;
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l3;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
        gm_b16_8 t18 = gm_l5;
        gm_b16_8 t19 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t15, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        gm_t88rcucg1b7uk t24 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
        gm_b16_8 t26 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t20, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_t7neromnu8t62 t28;
        uint64_t t29 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t28 + 0, t29);
        gm_b16_8 t30 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 8, t30);
        gm_b16_8 t31 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 24, t31);
        gm_b0_1 t32 = gm_f_linef_nac2kse((void *)NULL, cx, NULL, t27, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t33;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t39) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma37; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma37; case ((uint8_t)6ULL): goto ma37; case ((uint8_t)7ULL): goto ma37; case ((uint8_t)8ULL): goto ma37; case ((uint8_t)9ULL): goto ma37; case ((uint8_t)10ULL): goto ma37; case ((uint8_t)11ULL): goto ma37; case ((uint8_t)12ULL): goto ma37; case ((uint8_t)13ULL): goto ma37; case ((uint8_t)14ULL): goto ma37; case ((uint8_t)15ULL): goto ma37; case ((uint8_t)16ULL): goto ma37; case ((uint8_t)17ULL): goto ma37; case ((uint8_t)18ULL): goto ma37; case ((uint8_t)19ULL): goto ma37; case ((uint8_t)20ULL): goto ma37; case ((uint8_t)21ULL): goto ma37; case ((uint8_t)22ULL): goto ma37; case ((uint8_t)23ULL): goto ma37; case ((uint8_t)24ULL): goto ma37; case ((uint8_t)25ULL): goto ma37; case ((uint8_t)26ULL): goto ma37; case ((uint8_t)27ULL): goto ma37; case ((uint8_t)28ULL): goto ma37; case ((uint8_t)29ULL): goto ma37; case ((uint8_t)30ULL): goto ma37; case ((uint8_t)31ULL): goto ma37; case ((uint8_t)32ULL): goto ma37; case ((uint8_t)33ULL): goto ma37; case ((uint8_t)34ULL): goto ma37; case ((uint8_t)35ULL): goto ma37; case ((uint8_t)36ULL): goto ma37; case ((uint8_t)37ULL): goto ma37; case ((uint8_t)38ULL): goto ma37; case ((uint8_t)39ULL): goto ma37; case ((uint8_t)40ULL): goto ma37; case ((uint8_t)41ULL): goto ma37; case ((uint8_t)42ULL): goto ma37; case ((uint8_t)43ULL): goto ma37; case ((uint8_t)44ULL): goto ma37; case ((uint8_t)45ULL): goto ma37; case ((uint8_t)46ULL): goto ma37; case ((uint8_t)47ULL): goto ma37; case ((uint8_t)48ULL): goto ma37; case ((uint8_t)49ULL): goto ma35; case ((uint8_t)50ULL): goto ma36; case ((uint8_t)51ULL): goto ma37; case ((uint8_t)52ULL): goto ma37; case ((uint8_t)53ULL): goto ma37; case ((uint8_t)54ULL): goto ma37; case ((uint8_t)55ULL): goto ma37; case ((uint8_t)56ULL): goto ma37; case ((uint8_t)57ULL): goto ma37; case ((uint8_t)58ULL): goto ma37; case ((uint8_t)59ULL): goto ma37; case ((uint8_t)60ULL): goto ma37; case ((uint8_t)61ULL): goto ma37; case ((uint8_t)62ULL): goto ma37; case ((uint8_t)63ULL): goto ma37; case ((uint8_t)64ULL): goto ma37; case ((uint8_t)65ULL): goto ma37; case ((uint8_t)66ULL): goto ma37; case ((uint8_t)67ULL): goto ma37; case ((uint8_t)68ULL): goto ma37; case ((uint8_t)69ULL): goto ma37; case ((uint8_t)70ULL): goto ma37; case ((uint8_t)71ULL): goto ma37; case ((uint8_t)72ULL): goto ma37; case ((uint8_t)73ULL): goto ma37; case ((uint8_t)74ULL): goto ma37; case ((uint8_t)75ULL): goto ma37; case ((uint8_t)76ULL): goto ma37; case ((uint8_t)77ULL): goto ma37; case ((uint8_t)78ULL): goto ma37; case ((uint8_t)79ULL): goto ma37; case ((uint8_t)80ULL): goto ma37; case ((uint8_t)81ULL): goto ma37; default: goto ma37; }
ma35:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t41 != ((uint8_t)49ULL)) goto mn40;
        gm_t3877pk7lv748 t42;
        gm_b16_8 t43 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        uint64_t t44 = gm_l4;
        GM_ST(uint64_t, (uint8_t *)&t42 + 16, t44);
        gm_tabb710d9mq2e t45;
        gm_f_format_nvhh8d2((void *)&t45, (void *)NULL, cx, NULL, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src7hmnlrtmirgt, 309, 21);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        t33 = t49;
        goto me34;
mn40:;
ma36:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t51 != ((uint8_t)50ULL)) goto mn50;
        gm_b16_8 t52 = gm_l7;
        gm_tnpifv20irnph t53;
        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
        gm_tabb710d9mq2e t54;
        gm_f_format_vmsom9v((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src7hmnlrtmirgt, 310, 21);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        t33 = t58;
        goto me34;
mn50:;
ma37:;
        gm_b16_8 t60 = gm_l7;
        t33 = t60;
        goto me34;
mn59:;
ma38:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 308, 17);
me34:;
        gm_l8 = t33;
    }
    {
        void * t61 = gm_l0;
        void * t62 = gm_l1;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t62) + 0));
        gm_b16_8 t64 = gm_l8;
        gm_b16_8 t65 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t61, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t66;
        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t66 + 32, 0);
        ((uint8_t *)&t66)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
        gm_t5o0tpv5jka2l t67 = t66;
        uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
        F.ret = t66;
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
__attribute__((aligned(4))) void gm_f_vec_minmax_abs_4le8bpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_vec_minmax_abs_4le8bpj F;
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tismvprs8r13f gm_l5; /* vp */
    uint64_t gm_l6; /* n */
    gm_b16_8 gm_l9; /* a */
    gm_th2lqbo77pvsc gm_l10; /* b */
    uint8_t gm_l11; /* is_abs */
    gm_b16_8 gm_l14; /* bv */
    gm_b16_8 gm_l16; /* va */
    gm_b16_8 gm_l17; /* u */
    gm_b16_8 gm_l18; /* s */
    gm_b16_8 gm_l19; /* r */
    gm_b16_8 gm_l20; /* mask */
    gm_b16_8 gm_l21; /* sh */
    gm_b16_8 gm_l22; /* vb */
    gm_b16_8 gm_l23; /* sym */
    gm_b16_8 gm_l24; /* w */
    gm_b16_8 gm_l25; /* c */
    GM_STACK_CHECK(&F, 3290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = F.l0;
        gm_t88rcucg1b7uk t5 = gm_l4;
        gm_f_vec_parts_srl0rr9((void *)&gm_l5, t4, GM_CONV(gm_csguankko4lmhd, t5));
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_l6 = t6;
    }
    {
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        F.l7 = t7;
    }
    {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        F.l8 = t8;
    }
    {
        void * t9 = F.l0;
        uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src7hmnlrtmirgt, 334, 23);
        void * t12 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t10 * 224);
        gm_t5o0tpv5jka2l t13;
        gm_f_expr_porbc9t((void *)&t13, (void *)NULL, cx, NULL, t9, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) {
            gm_t08olq97rscn3 t15 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t14) + 8));
            gm_t5o0tpv5jka2l t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t16 + 8), t15);
            F.ret = t16;
            goto gm_fret;
        }
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
        gm_l9 = t17;
    }
    {
        gm_th2lqbo77pvsc t18;
        memset(&t18, 0, sizeof t18);
        gm_l10 = t18;
    }
    {
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t21 = (t20 > ((uint64_t)1ULL));
        if (!t21) goto ie19;
        {
            void * t23 = F.l0;
            uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src7hmnlrtmirgt, 336, 40);
            void * t26 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t24 * 224);
            gm_t5o0tpv5jka2l t27;
            gm_f_expr_porbc9t((void *)&t27, (void *)NULL, cx, NULL, t23, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t28 = t27;
            if (((((uint8_t *)&t28))[0] != 0)) {
                gm_t08olq97rscn3 t29 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t28) + 8));
                gm_t5o0tpv5jka2l t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                ((uint8_t *)&t30)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t30 + 8), t29);
                F.ret = t30;
                goto gm_fret;
            }
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
            gm_th2lqbo77pvsc t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
            ((uint8_t *)&t32)[16] = 1;
            gm_l10 = t32;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        uint8_t t33 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)55ULL)));
        gm_l11 = t33;
    }
    {
        uint8_t t35 = 0;
        uint8_t t36 = 0;
        uint8_t t37 = gm_l11;
        uint8_t t38 = (uint8_t)!t37;
        if (t38) {
            void * t39 = F.l0;
            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
            void * t41 = (void *)(((uint8_t *)t40) + 8);
            gm_t88rcucg1b7uk t42 = F.l7;
            uint8_t t43 = gm_f_is_float_gthejtb(t41, GM_CONV(gm_csguankko4lmhd, t42));
            t36 = t43;
        } else t36 = 0;
        if (!t36) {
            uint8_t t44 = 0;
            uint8_t t45 = 0;
            uint8_t t46 = gm_l11;
            if (t46) {
                void * t47 = F.l0;
                void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
                void * t49 = (void *)(((uint8_t *)t48) + 8);
                gm_t88rcucg1b7uk t50 = F.l7;
                uint8_t t51 = gm_f_is_integer_eo6jkc5(t49, GM_CONV(gm_csguankko4lmhd, t50));
                t45 = t51;
            } else t45 = 0;
            if (t45) {
                void * t52 = F.l0;
                void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
                uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 1136) + 0));
                t44 = t54;
            } else t44 = 0;
            t35 = t44;
        } else t35 = 1;
        if (!t35) goto ie34;
        {
            void * t56 = F.l0;
            gm_t88rcucg1b7uk t57 = gm_l4;
            gm_b16_8 t58 = gm_l9;
            gm_b16_8 t59 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t56, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l12 = t59;
        }
        {
            gm_th2lqbo77pvsc t60;
            memset(&t60, 0, sizeof t60);
            F.l13 = t60;
        }
        {
            if (!((((uint8_t *)&gm_l10))[16] != 0)) goto ie61;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l14 = t62;
            {
                void * t64 = F.l0;
                gm_t88rcucg1b7uk t65 = gm_l4;
                gm_b16_8 t66 = gm_l14;
                gm_b16_8 t67 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t64, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t68), t67);
                ((uint8_t *)&t68)[16] = 1;
                F.l13 = t68;
            }
            goto ix61;
ie61:;
ix61:;
        }
        {
            void * t69 = F.l0;
            void * t70 = gm_l1;
            gm_b16_8 t71 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l15 = t71;
        }
        {
            void * t72 = F.l0;
            gm_t88rcucg1b7uk t73 = gm_l4;
            uint64_t t74 = gm_l6;
            gm_closure t75 = ((gm_closure){(void *)gm_f_fn_b1j5nho, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t76;
            gm_f_vec_lane_loop_a3gvooh((void *)&t76, (void *)NULL, cx, NULL, t72, t73, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) {
                gm_t08olq97rscn3 t78 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t77) + 8));
                gm_t5o0tpv5jka2l t79;
                GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                ((uint8_t *)&t79)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t79 + 8), t78);
                F.ret = t79;
                goto gm_fret;
            }
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
            gm_t5o0tpv5jka2l t81;
            GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t81 + 32, 0);
            ((uint8_t *)&t81)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
            gm_t5o0tpv5jka2l t82 = t81;
            uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
            F.ret = t81;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t84 = F.l0;
        gm_t88rcucg1b7uk t85 = gm_l4;
        gm_b16_8 t86 = gm_l9;
        gm_b16_8 t87 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t84, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t87;
    }
    {
        void * t88 = F.l0;
        uint64_t t89 = gm_l6;
        gm_t88rcucg1b7uk t90 = F.l7;
        gm_tqs5p4n2tq19v t91;
        GM_ST(uint8_t, ((uint8_t *)&t91), ((uint8_t)0));
        ((uint8_t *)&t91)[1] = 1;
        gm_b16_8 t92 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t88, t89, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t92;
    }
    {
        void * t93 = F.l0;
        uint64_t t94 = gm_l6;
        gm_t88rcucg1b7uk t95 = F.l7;
        gm_tqs5p4n2tq19v t96;
        memset(&t96, 0, sizeof t96);
        gm_b16_8 t97 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t93, t94, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t97;
    }
    {
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(0));
        gm_l19 = t98;
    }
    {
        uint8_t t100 = gm_l11;
        if (!t100) goto ie99;
        void * t101 = F.l0;
        void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
        void * t103 = (void *)(((uint8_t *)t102) + 8);
        gm_t88rcucg1b7uk t104 = F.l7;
        uint8_t t105 = gm_f_is_float_gthejtb(t103, GM_CONV(gm_csguankko4lmhd, t104));
        if (!t105) goto ie99;
        {
            gm_b16_8 t107;
            uint64_t t109 = F.l8;
            uint8_t t110 = ((t109 == ((uint64_t)4ULL)));
            if (!t110) goto ie108;
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_snd30h4tliigb));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(11));
            gm_b16_8 t113 = t112;
            t107 = t113;
            goto ix108;
ie108:;
            gm_b16_8 t115;
            GM_ST(void *, (uint8_t *)&t115, (void *)(gm_svaet5ugb1fj3));
            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(21));
            gm_b16_8 t116 = t115;
            t107 = t116;
ix108:;
            gm_l20 = t107;
        }
        {
            gm_t0uuhaljsf410 t117;
            gm_b16_8 t118 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t117 + 0, t118);
            gm_b16_8 t119 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t117 + 16, t119);
            gm_b16_8 t120 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t117 + 32, t120);
            gm_b16_8 t121 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t117 + 48, t121);
            gm_tabb710d9mq2e t122;
            gm_f_format_l0pq86n((void *)&t122, (void *)NULL, cx, NULL, &t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t123 = t122;
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_src7hmnlrtmirgt, 364, 13);
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
            gm_b16_8 t125 = t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            gm_l19 = t126;
        }
        goto ix99;
ie99:;
        uint8_t t128 = gm_l11;
        if (!t128) goto ie127;
        {
            void * t130 = F.l0;
            uint32_t t131 = gm_f_fresh_5kokjmg(t130);
            gm_t88rcucg1b7uk t132;
            GM_ST(uint32_t, ((uint8_t *)&t132 + 0), t131);
            gm_tabb710d9mq2e t133;
            gm_f_format_61d2klb((void *)&t133, (void *)NULL, cx, NULL, t132);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t134 = t133;
            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_src7hmnlrtmirgt, 366, 15);
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
            gm_l21 = t135;
        }
        {
            void * t136 = F.l0;
            gm_tttl9nsdo1iu6 t137;
            gm_b16_8 t138 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
            gm_b16_8 t139 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t139);
            gm_b16_8 t140 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t137 + 32, t140);
            uint64_t t141 = F.l8;
            uint64_t r142; bool o143 = __builtin_mul_overflow(t141, ((uint64_t)8ULL), &r142);
            if (o143) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 367, 50);
            uint64_t t144 = r142;
            uint64_t r145; bool o146 = __builtin_sub_overflow(t144, ((uint64_t)1ULL), &r145);
            if (o146) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 367, 50);
            uint64_t t147 = r145;
            GM_ST(uint64_t, (uint8_t *)&t137 + 48, t147);
            gm_b0_1 t148 = gm_f_linef_7cnqmuh((void *)NULL, cx, NULL, t136, &t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tif3elqk01bdl t149;
            gm_b16_8 t150 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 0, t150);
            gm_b16_8 t151 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 16, t151);
            gm_b16_8 t152 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 32, t152);
            gm_b16_8 t153 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 48, t153);
            gm_b16_8 t154 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 64, t154);
            gm_b16_8 t155 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t149 + 80, t155);
            gm_tabb710d9mq2e t156;
            gm_f_format_adcs7to((void *)&t156, (void *)NULL, cx, NULL, &t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t157 = t156;
            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_src7hmnlrtmirgt, 368, 13);
            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
            gm_b16_8 t159 = t158;
            gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
            gm_l19 = t160;
        }
        goto ix127;
ie127:;
        {
            void * t162 = F.l0;
            gm_t88rcucg1b7uk t163 = gm_l4;
            gm_th2lqbo77pvsc t164 = gm_l10;
            gm_th2lqbo77pvsc t165 = t164;
            if (!((((uint8_t *)&t165))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 370, 30);
            gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
            gm_b16_8 t167 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t162, t163, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t167;
        }
        {
            gm_b16_8 t168;
            uint8_t t170 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)53ULL)));
            if (!t170) goto ie169;
            gm_b16_8 t172;
            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(1));
            gm_b16_8 t173 = t172;
            t168 = t173;
            goto ix169;
ie169:;
            gm_b16_8 t175;
            GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(1));
            gm_b16_8 t176 = t175;
            t168 = t176;
ix169:;
            gm_l23 = t168;
        }
        {
            void * t177 = F.l0;
            uint64_t t178 = gm_l6;
            gm_t88rcucg1b7uk t179 = F.l7;
            gm_tqs5p4n2tq19v t180;
            GM_ST(uint8_t, ((uint8_t *)&t180), ((uint8_t)1));
            ((uint8_t *)&t180)[1] = 1;
            gm_b16_8 t181 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t177, t178, t179, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t181;
        }
        {
            void * t182 = F.l0;
            uint32_t t183 = gm_f_fresh_5kokjmg(t182);
            gm_t88rcucg1b7uk t184;
            GM_ST(uint32_t, ((uint8_t *)&t184 + 0), t183);
            gm_tabb710d9mq2e t185;
            gm_f_format_3tnvb6a((void *)&t185, (void *)NULL, cx, NULL, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t186 = t185;
            if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 8, gm_src7hmnlrtmirgt, 373, 14);
            gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 8));
            gm_l25 = t187;
        }
        {
            void * t188 = F.l0;
            gm_th2d9c7aju8hp t189;
            gm_b16_8 t190 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 0, t190);
            gm_b16_8 t191 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 16, t191);
            gm_b16_8 t192 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 32, t192);
            gm_b16_8 t193 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 48, t193);
            gm_b16_8 t194 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 64, t194);
            gm_b16_8 t195 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 80, t195);
            gm_b16_8 t196 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t189 + 96, t196);
            gm_b0_1 t197 = gm_f_linef_sk93pav((void *)NULL, cx, NULL, t188, &t189);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_th2d9c7aju8hp t198;
            gm_b16_8 t199 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t199);
            gm_b16_8 t200 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 16, t200);
            gm_b16_8 t201 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 32, t201);
            gm_b16_8 t202 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 48, t202);
            gm_b16_8 t203 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 64, t203);
            gm_b16_8 t204 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 80, t204);
            gm_b16_8 t205 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 96, t205);
            gm_tabb710d9mq2e t206;
            gm_f_format_l9df9v3((void *)&t206, (void *)NULL, cx, NULL, &t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t207 = t206;
            if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_src7hmnlrtmirgt, 375, 13);
            gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
            gm_b16_8 t209 = t208;
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
            gm_l19 = t210;
        }
ix127:;
ix99:;
    }
    {
        void * t211 = F.l0;
        gm_t88rcucg1b7uk t212 = gm_l4;
        gm_b16_8 t213 = gm_l19;
        gm_b16_8 t214 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t211, t212, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t215;
        GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t215 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t215 + 32, 0);
        ((uint8_t *)&t215)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t215 + 8), t214);
        gm_t5o0tpv5jka2l t216 = t215;
        uint8_t t217 = ((((uint8_t *)&t216))[0] != 0);
        F.ret = t215;
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
inline __attribute__((aligned(4))) void gm_f_vec_parts_srl0rr9(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vec_parts_srl0rr9 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        if (!((((uint8_t *)&t8))[16] != 0)) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 28, 10);
        gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t8));
        gm_l2 = t9;
    }
    {
        memset((uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 12, 0, 4);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 0));
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
        uint64_t t14 = gm_f_vec_lane_bytes_c5pmsp6(t12, GM_CONV(gm_csguankko4lmhd, t13));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 0, t10);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 8, t11);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tismvprs8r13f *)gm_out) + 16, t14);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csa3m9feqhg846 gm_f_vec_parts_srl0rr9_c(void * a0, gm_csguankko4lmhd a1) { gm_tismvprs8r13f gm_r; gm_f_vec_parts_srl0rr9(&gm_r, a0, a1); return GM_CONV(gm_csa3m9feqhg846, gm_r); }
__attribute__((aligned(4))) void gm_f_vec_reduce_m5gdqn2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, void * a3) {
    struct gm_fr_vec_reduce_m5gdqn2 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    void * gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tismvprs8r13f gm_l5; /* vp */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* p */
    gm_b16_8 gm_l9; /* acc */
    gm_b16_8 gm_l10; /* i */
    gm_b16_8 gm_l11; /* y */
    gm_b16_8 gm_l13; /* r */
    gm_b16_8 gm_l14; /* f */
    gm_b16_8 gm_l15; /* fr */
    gm_b16_8 gm_l16; /* gt */
    GM_STACK_CHECK(&F, 2513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l3;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t88rcucg1b7uk t5 = gm_l4;
        gm_f_vec_parts_srl0rr9((void *)&gm_l5, t4, GM_CONV(gm_csguankko4lmhd, t5));
    }
    {
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l3;
        gm_t5o0tpv5jka2l t9;
        gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t10 = t9;
        if (((((uint8_t *)&t10))[0] != 0)) {
            gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t10) + 8));
            gm_t5o0tpv5jka2l t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), t11);
            F.ret = t12;
            goto gm_fret;
        }
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l7 = t13;
    }
    {
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l4;
        gm_b16_8 t16 = gm_l7;
        gm_b16_8 t17 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t17;
    }
    {
        void * t18 = gm_l0;
        gm_t88rcucg1b7uk t19 = gm_l6;
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        gm_b16_8 t21 = gm_l8;
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
        gm_b16_8 t23 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t18, t19, t20, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t23;
    }
    {
        void * t24 = gm_l0;
        uint32_t t25 = gm_f_fresh_5kokjmg(t24);
        gm_t88rcucg1b7uk t26;
        GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_c0h6o29((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_src7hmnlrtmirgt, 276, 10);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_l10 = t29;
    }
    {
        void * t30 = gm_l0;
        gm_tmr5oep1578tb t31;
        gm_b16_8 t32 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
        gm_b16_8 t33 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t33);
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        GM_ST(uint64_t, (uint8_t *)&t31 + 32, t34);
        gm_b16_8 t35 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 40, t35);
        gm_b0_1 t36 = gm_f_linef_6mfrmej((void *)NULL, cx, NULL, t30, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l0;
        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 224));
        uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
        if (o41) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 278, 5);
        uint64_t t42 = r40;
        GM_ST(uint64_t, (((uint8_t *)t37) + 224), t42);
    }
    {
        void * t43 = gm_l0;
        gm_t88rcucg1b7uk t44 = gm_l6;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        gm_b16_8 t46 = gm_l8;
        gm_b16_8 t47 = gm_l10;
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_b16_8 t50 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t43, t44, t45, t46, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t50;
    }
    {
        memset((uint8_t *)&F.l12 + 4, 0, 4);
        memset((uint8_t *)&F.l12 + 220, 0, 4);
        gm_t88rcucg1b7uk t51 = gm_l6;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&F.l12 + 0, t51);
        gm_t5brlu7fal70j t52;
        memset(&t52, 0, sizeof t52);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t5brlu7fal70j, (uint8_t *)&F.l12 + 8, t52);
        void * t53 = gm_l1;
        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 208));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&F.l12 + 208, t54);
    }
    {
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(0));
        gm_l13 = t55;
    }
    {
        uint8_t t57 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)46ULL)));
        if (!t57) goto ie56;
        {
            void * t60 = gm_l0;
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 0));
            void * t62 = (void *)(((uint8_t *)t61) + 8);
            gm_t88rcucg1b7uk t63 = gm_l6;
            uint8_t t64 = gm_f_is_float_gthejtb(t62, GM_CONV(gm_csguankko4lmhd, t63));
            if (!t64) goto ie59;
            {
                void * t66 = gm_l0;
                gm_t88rcucg1b7uk t67 = gm_l6;
                gm_t6j6smj7qoctu t68;
                gm_b16_8 t69 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
                gm_b16_8 t70 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t70);
                gm_tabb710d9mq2e t71;
                gm_f_format_s4jotpe((void *)&t71, (void *)NULL, cx, NULL, &t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t72 = t71;
                if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src7hmnlrtmirgt, 284, 30);
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                gm_b16_8 t74 = t73;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_b16_8 t76 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t66, t67, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t76;
            }
            goto ix59;
ie59:;
            {
                void * t78 = gm_l0;
                void * t79 = (void *)((uint8_t *)&F.l12);
                gm_t9qkh0rsooc8d t80;
                GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
                gm_ts6oqppjh0ba3 t81;
                GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)0ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t81 + 1), t80);
                gm_t88rcucg1b7uk t82 = gm_l6;
                gm_b16_8 t83 = gm_l9;
                gm_b16_8 t84 = gm_l11;
                gm_t5o0tpv5jka2l t85;
                gm_f_int_binary_kp3u7i8((void *)&t85, (void *)NULL, cx, NULL, t78, t79, t81, t82, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t86 = t85;
                if (((((uint8_t *)&t86))[0] != 0)) {
                    gm_t08olq97rscn3 t87 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t86) + 8));
                    gm_t5o0tpv5jka2l t88;
                    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                    ((uint8_t *)&t88)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t88 + 8), t87);
                    F.ret = t88;
                    goto gm_fret;
                }
                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
                gm_l13 = t89;
            }
ix59:;
        }
        goto ix56;
ie56:;
        void * t91 = gm_l0;
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
        void * t93 = (void *)(((uint8_t *)t92) + 8);
        gm_t88rcucg1b7uk t94 = gm_l6;
        uint8_t t95 = gm_f_is_float_gthejtb(t93, GM_CONV(gm_csguankko4lmhd, t94));
        if (!t95) goto ie90;
        {
            gm_b16_8 t97;
            uint8_t t99 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
            if (!t99) goto ie98;
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s12ng38ve3205));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(7));
            gm_b16_8 t102 = t101;
            t97 = t102;
            goto ix98;
ie98:;
            gm_b16_8 t104;
            GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sgjc0ebf56hf4));
            GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(7));
            gm_b16_8 t105 = t104;
            t97 = t105;
ix98:;
            gm_l14 = t97;
        }
        {
            void * t106 = gm_l0;
            gm_t88rcucg1b7uk t107 = gm_l6;
            gm_tfeab279c16rr t108;
            gm_b16_8 t109 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
            gm_b16_8 t110 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t110);
            gm_b16_8 t111 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t108 + 32, t111);
            gm_tabb710d9mq2e t112;
            gm_f_format_lqnh3lt((void *)&t112, (void *)NULL, cx, NULL, &t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t113 = t112;
            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_src7hmnlrtmirgt, 290, 35);
            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
            gm_b16_8 t115 = t114;
            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
            gm_b16_8 t117 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t106, t107, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t117;
        }
        {
            void * t118 = gm_l0;
            gm_t88rcucg1b7uk t119 = gm_l6;
            gm_b16_8 t120 = gm_l15;
            gm_b16_8 t121 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t118, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t121;
        }
        goto ix90;
ie90:;
        {
            gm_b16_8 t123;
            uint8_t t125 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)47ULL)));
            if (!t125) goto ie124;
            gm_b16_8 t127;
            GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(1));
            gm_b16_8 t128 = t127;
            t123 = t128;
            goto ix124;
ie124:;
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(1));
            gm_b16_8 t131 = t130;
            t123 = t131;
ix124:;
            gm_l16 = t123;
        }
        {
            void * t132 = gm_l0;
            gm_t88rcucg1b7uk t133 = gm_l6;
            gm_tud7oovfvnf43 t134;
            gm_b16_8 t135 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 0, t135);
            gm_b16_8 t136 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 16, t136);
            gm_b16_8 t137 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 32, t137);
            gm_b16_8 t138 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 48, t138);
            gm_b16_8 t139 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t134 + 64, t139);
            gm_tabb710d9mq2e t140;
            gm_f_format_eqjcikj((void *)&t140, (void *)NULL, cx, NULL, &t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t141 = t140;
            if (((((uint8_t *)&t141))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t141) + 8, gm_src7hmnlrtmirgt, 294, 26);
            gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&t141) + 8));
            gm_b16_8 t143 = t142;
            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
            gm_b16_8 t145 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t132, t133, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t145;
        }
ix90:;
ix56:;
    }
    {
        void * t146 = gm_l0;
        gm_t6j6smj7qoctu t147;
        gm_b16_8 t148 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t148);
        gm_b16_8 t149 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t149);
        gm_b0_1 t150 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t146, &t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t151 = gm_l0;
        void * t152 = gm_l0;
        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)t152) + 224));
        uint64_t r154; bool o155 = __builtin_sub_overflow(t153, ((uint64_t)1ULL), &r154);
        if (o155) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 297, 5);
        uint64_t t156 = r154;
        GM_ST(uint64_t, (((uint8_t *)t151) + 224), t156);
    }
    {
        void * t157 = gm_l0;
        gm_b16_8 t158;
        GM_ST(void *, (uint8_t *)&t158, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(1));
        gm_b0_1 t159 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t157, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t160 = gm_l9;
        gm_t5o0tpv5jka2l t161;
        GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t161 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t161 + 32, 0);
        ((uint8_t *)&t161)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t161 + 8), t160);
        gm_t5o0tpv5jka2l t162 = t161;
        uint8_t t163 = ((((uint8_t *)&t162))[0] != 0);
        F.ret = t161;
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
inline __attribute__((aligned(4))) void gm_f_vec_select_3f37tvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_vec_select_3f37tvf F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* args */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* m */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* b */
    gm_b16_8 gm_l7; /* vm */
    gm_b16_8 gm_l8; /* va */
    gm_b16_8 gm_l9; /* vb */
    gm_b16_8 gm_l10; /* r */
    GM_STACK_CHECK(&F, 1109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_src7hmnlrtmirgt, 318, 23);
        void * t7 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t5 * 224);
        gm_t5o0tpv5jka2l t8;
        gm_f_expr_porbc9t((void *)&t8, (void *)NULL, cx, NULL, t4, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) {
            gm_t08olq97rscn3 t10 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t9) + 8));
            gm_t5o0tpv5jka2l t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t11 + 8), t10);
            F.ret = t11;
            goto gm_fret;
        }
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l4 = t12;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src7hmnlrtmirgt, 319, 23);
        void * t16 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 224);
        gm_t5o0tpv5jka2l t17;
        gm_f_expr_porbc9t((void *)&t17, (void *)NULL, cx, NULL, t13, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) {
            gm_t08olq97rscn3 t19 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t18) + 8));
            gm_t5o0tpv5jka2l t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            ((uint8_t *)&t20)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), t19);
            F.ret = t20;
            goto gm_fret;
        }
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t18) + 8));
        gm_l5 = t21;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_src7hmnlrtmirgt, 320, 23);
        void * t25 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t23 * 224);
        gm_t5o0tpv5jka2l t26;
        gm_f_expr_porbc9t((void *)&t26, (void *)NULL, cx, NULL, t22, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) {
            gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t27) + 8));
            gm_t5o0tpv5jka2l t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t29 + 8), t28);
            F.ret = t29;
            goto gm_fret;
        }
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_l6 = t30;
    }
    {
        void * t31 = gm_l0;
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_src7hmnlrtmirgt, 321, 23);
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t32 * 224) + 0));
        gm_b16_8 t35 = gm_l4;
        gm_b16_8 t36 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t31, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t36;
    }
    {
        void * t37 = gm_l0;
        gm_t88rcucg1b7uk t38 = gm_l3;
        gm_b16_8 t39 = gm_l5;
        gm_b16_8 t40 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t40;
    }
    {
        void * t41 = gm_l0;
        gm_t88rcucg1b7uk t42 = gm_l3;
        gm_b16_8 t43 = gm_l6;
        gm_b16_8 t44 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t41, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t44;
    }
    {
        void * t45 = gm_l0;
        gm_t88rcucg1b7uk t46 = gm_l3;
        gm_b16_8 t47 = gm_l7;
        gm_b16_8 t48 = gm_l8;
        gm_b16_8 t49 = gm_l9;
        gm_b16_8 t50 = gm_f_vec_blend_2oerv54((void *)NULL, cx, NULL, t45, t46, t47, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t50;
    }
    {
        void * t51 = gm_l0;
        gm_t88rcucg1b7uk t52 = gm_l3;
        gm_b16_8 t53 = gm_l10;
        gm_b16_8 t54 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t51, t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 32, 0);
        ((uint8_t *)&t55)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 8), t54);
        gm_t5o0tpv5jka2l t56 = t55;
        uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
        F.ret = t55;
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
__attribute__((aligned(4))) void gm_f_vec_shuffle_j6jmffl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_vec_shuffle_j6jmffl F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* args */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tismvprs8r13f gm_l4; /* vp */
    gm_t88rcucg1b7uk gm_l5; /* lane */
    uint64_t gm_l6; /* bytes */
    uint64_t gm_l7; /* m */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* k */
    gm_b16_8 gm_l13; /* v */
    gm_b16_8 gm_l14; /* va */
    gm_b16_8 gm_l15; /* tv */
    gm_b16_8 gm_l16; /* res */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* j */
    gm_b16_8 gm_l21; /* k */
    GM_STACK_CHECK(&F, 1906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t2 >= (uintptr_t)(t3)) gm_trap_index(t2, t3, gm_src7hmnlrtmirgt, 389, 10);
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t2 * 224) + 0));
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_f_vec_parts_srl0rr9((void *)&gm_l4, t5, GM_CONV(gm_csguankko4lmhd, t6));
    }
    {
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
        gm_l5 = t7;
    }
    {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
        gm_l6 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l1;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_tismvprs8r13f t12;
        gm_f_vec_parts_srl0rr9((void *)&t12, t9, GM_CONV(gm_csguankko4lmhd, t11));
        gm_tismvprs8r13f t13 = t12;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
        gm_l7 = t14;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l10 = ((uint64_t)1ULL);
        }
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l9 = t16;
        }
        {
            for (;;) {
                uint64_t t18 = gm_l10;
                uint64_t t19 = gm_l9;
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l10;
                    uint64_t t24 = t23;
                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t24));
                    gm_l11 = t27;
                    {
                        uint64_t t30 = gm_l11;
                        uint64_t t31 = (uintptr_t)t30;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t31 >= (uintptr_t)(t32)) gm_trap_index(t31, t32, gm_src7hmnlrtmirgt, 396, 23);
                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t31 * 224) + 8) + 0));
                        if (t33 != ((uint8_t)0ULL)) goto ie29;
                        gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t31 * 224) + 8) + 8));
                        F.l12 = t34;
                        {
                            void * t36 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t37 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l12)));
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_b16_8 t38 = t37;
                            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                            gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t36, t39);
                            gm_tftb1odc739t5 t41 = t40;
                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src7hmnlrtmirgt, 397, 13);
                            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        }
                        goto ix29;
ie29:;
                        {
                            void * t44 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t45;
                            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sicgmpca63jln));
                            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
                            gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
                            gm_tftb1odc739t5 t47 = t46;
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_src7hmnlrtmirgt, 399, 13);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
ix29:;
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 395, 5);
me25:;
                }
lc17:;
                {
                    uint64_t t50 = gm_l10;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l10 = t53;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        void * t54 = gm_l0;
        uint64_t t55 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_src7hmnlrtmirgt, 402, 23);
        void * t57 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t55 * 224);
        gm_t5o0tpv5jka2l t58;
        gm_f_expr_porbc9t((void *)&t58, (void *)NULL, cx, NULL, t54, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) {
            gm_t08olq97rscn3 t60 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t59) + 8));
            gm_t5o0tpv5jka2l t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            ((uint8_t *)&t61)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t61 + 8), t60);
            F.ret = t61;
            goto gm_fret;
        }
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
        gm_l13 = t62;
    }
    {
        void * t63 = gm_l0;
        gm_t88rcucg1b7uk t64 = gm_l3;
        gm_b16_8 t65 = gm_l13;
        gm_b16_8 t66 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t63, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t66;
    }
    {
        void * t67 = gm_l0;
        uint64_t t68 = gm_l7;
        gm_t88rcucg1b7uk t69 = gm_l5;
        gm_tqs5p4n2tq19v t70;
        memset(&t70, 0, sizeof t70);
        gm_b16_8 t71 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t67, t68, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t71;
    }
    {
        void * t72 = gm_l0;
        void * t73 = gm_l1;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
        gm_b16_8 t75 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t72, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t75;
    }
    {
        void * t76 = gm_l0;
        uint32_t t77 = gm_f_fresh_5kokjmg(t76);
        gm_t88rcucg1b7uk t78;
        GM_ST(uint32_t, ((uint8_t *)&t78 + 0), t77);
        gm_tabb710d9mq2e t79;
        gm_f_format_1mojd22((void *)&t79, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t80 = t79;
        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_src7hmnlrtmirgt, 406, 10);
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
        gm_l17 = t81;
    }
    {
        void * t82 = gm_l0;
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sb22ajpcscr7v));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(40));
        gm_b0_1 t84 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t85 = gm_l0;
        gm_tud7oovfvnf43 t86;
        gm_b16_8 t87 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 0, t87);
        gm_b16_8 t88 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 16, t88);
        gm_b16_8 t89 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 32, t89);
        gm_b16_8 t90 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 48, t90);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t92 = t91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b16_8 t94;
        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t95;
        gm_f_join_sfknljb((void *)&t95, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t96 = t95;
        if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 8, gm_src7hmnlrtmirgt, 408, 77);
        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t86 + 64, t97);
        gm_b0_1 t98 = gm_f_linef_ssnaj62((void *)NULL, cx, NULL, t85, &t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t99 = gm_l0;
        gm_tfeab279c16rr t100;
        gm_b16_8 t101 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
        gm_b16_8 t102 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t102);
        gm_b16_8 t103 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 32, t103);
        gm_b0_1 t104 = gm_f_linef_ar0alse((void *)NULL, cx, NULL, t99, &t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t105 = gm_l0;
        gm_b16_8 t106;
        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s0p6vt35dv9nv));
        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(5));
        gm_b0_1 t107 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t105, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t110 = t109;
            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
            gm_l18 = t111;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t113 = gm_l19;
                uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t115 = (t113 < t114);
                if (!t115) goto lb112;
                {
                    gm_tf1bjg81u0glv t118;
                    uint64_t t119 = gm_l19;
                    GM_ST(uint64_t, (uint8_t *)&t118 + 0, t119);
                    uint64_t t120 = gm_l19;
                    uint64_t t121 = (uintptr_t)t120;
                    uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t121 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t118 + 8, t123);
                    gm_tf1bjg81u0glv t124 = t118;
                    uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&t124) + 0));
                    gm_l20 = t127;
                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
                    gm_l21 = t128;
                    {
                        void * t130 = gm_l0;
                        gm_tte9sujb6sb62 t131;
                        gm_b16_8 t132 = gm_l16;
                        GM_ST(gm_b16_8, (uint8_t *)&t131 + 0, t132);
                        uint64_t t133 = gm_l20;
                        uint64_t t134 = (uint64_t)t133;
                        uint64_t t135 = gm_l6;
                        uint64_t r136; bool o137 = __builtin_mul_overflow(t134, t135, &r136);
                        if (o137) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 412, 85);
                        uint64_t t138 = r136;
                        GM_ST(uint64_t, (uint8_t *)&t131 + 16, t138);
                        gm_b16_8 t139 = gm_l14;
                        GM_ST(gm_b16_8, (uint8_t *)&t131 + 24, t139);
                        gm_b16_8 t140 = gm_l21;
                        GM_ST(gm_b16_8, (uint8_t *)&t131 + 40, t140);
                        uint64_t t141 = gm_l6;
                        GM_ST(uint64_t, (uint8_t *)&t131 + 56, t141);
                        uint64_t t142 = gm_l6;
                        GM_ST(uint64_t, (uint8_t *)&t131 + 64, t142);
                        gm_b0_1 t143 = gm_f_linef_pai11tb((void *)NULL, cx, NULL, t130, &t131);
                        if (gm_unw.kind) {
                            goto gm_ll116;
                        }
                    }
                    goto me125;
mn126:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 411, 5);
me125:;
                }
lc112:;
                {
                    uint64_t t145 = gm_l19;
                    uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
                    uint64_t t148 = r146;
                    gm_l19 = t148;
                }
            }
gm_ll116:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb112; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc112; }
            }
            goto gm_lad;
lb112:;
        }
    }
    {
        void * t149 = gm_l0;
        gm_b16_8 t150;
        GM_ST(void *, (uint8_t *)&t150, (void *)(gm_s9d990kf69gae));
        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(6));
        gm_b0_1 t151 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t149, t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t152 = gm_l16;
        gm_t5o0tpv5jka2l t153;
        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t153 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t153 + 32, 0);
        ((uint8_t *)&t153)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t153 + 8), t152);
        gm_t5o0tpv5jka2l t154 = t153;
        uint8_t t155 = ((((uint8_t *)&t154))[0] != 0);
        F.ret = t153;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_store_6d0dm94(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_store_6d0dm94 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* x */
    gm_b16_8 gm_l3; /* res */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        uint32_t t6 = gm_f_fresh_5kokjmg(t5);
        gm_t88rcucg1b7uk t7;
        GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t6);
        gm_tabb710d9mq2e t8;
        gm_f_format_1mojd22((void *)&t8, (void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_src7hmnlrtmirgt, 76, 10);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l4 = t10;
    }
    {
        void * t11 = gm_l0;
        gm_tif3elqk01bdl t12;
        gm_b16_8 t13 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 0, t13);
        gm_b16_8 t14 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 16, t14);
        gm_b16_8 t15 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 32, t15);
        gm_b16_8 t16 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 48, t16);
        gm_b16_8 t17 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 64, t17);
        gm_b16_8 t18 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 80, t18);
        gm_b0_1 t19 = gm_f_linef_4bjepb3((void *)NULL, cx, NULL, t11, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t20 = gm_l3;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_ty_adod8ur(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tqs5p4n2tq19v a3) {
    struct gm_fr_vec_ty_adod8ur F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* lane */
    gm_tqs5p4n2tq19v gm_l3; /* twin */
    uint64_t gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* tag */
    uint8_t gm_l7; /* signed */
    uint64_t gm_l8; /* bits */
    uint16_t gm_l9; /* bits */
    uint8_t gm_l10; /* signed */
    uint16_t gm_l11; /* bits */
    GM_STACK_CHECK(&F, 1452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        uint64_t t4 = gm_f_vec_lane_bytes_c5pmsp6(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l4 = t4;
    }
    {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s9grtrvtmi962));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(7));
        gm_l5 = t5;
    }
    {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sjmccqdb90fnl));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(2));
        gm_l6 = t6;
    }
    {
        if (!((((uint8_t *)&gm_l3))[1] != 0)) goto ie7;
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l7 = t8;
        {
            uint64_t t10 = gm_l4;
            uint64_t r11; bool o12 = __builtin_mul_overflow(t10, ((uint64_t)8ULL), &r11);
            if (o12) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 39, 17);
            uint64_t t13 = r11;
            gm_l8 = t13;
        }
        {
            uint8_t t15 = gm_l7;
            if (!t15) goto ie14;
            {
                uint64_t t17 = gm_l8;
                gm_t4ce0jd3tdro4 t18;
                GM_ST(uint64_t, ((uint8_t *)&t18 + 0), t17);
                gm_tabb710d9mq2e t19;
                gm_f_format_48m8fhm((void *)&t19, (void *)NULL, cx, NULL, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t20 = t19;
                if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src7hmnlrtmirgt, 41, 17);
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
                gm_b16_8 t22 = t21;
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                gm_l5 = t23;
            }
            {
                uint64_t t24 = gm_l8;
                gm_t4ce0jd3tdro4 t25;
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t24);
                gm_tabb710d9mq2e t26;
                gm_f_format_27c89bp((void *)&t26, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t27 = t26;
                if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src7hmnlrtmirgt, 42, 19);
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
                gm_b16_8 t29 = t28;
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_l6 = t30;
            }
            goto ix14;
ie14:;
            {
                uint64_t t32 = gm_l8;
                gm_t4ce0jd3tdro4 t33;
                GM_ST(uint64_t, ((uint8_t *)&t33 + 0), t32);
                gm_tabb710d9mq2e t34;
                gm_f_format_368205k((void *)&t34, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t35 = t34;
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src7hmnlrtmirgt, 44, 17);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                gm_b16_8 t37 = t36;
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                gm_l5 = t38;
            }
            {
                uint64_t t39 = gm_l8;
                gm_t4ce0jd3tdro4 t40;
                GM_ST(uint64_t, ((uint8_t *)&t40 + 0), t39);
                gm_tabb710d9mq2e t41;
                gm_f_format_4f5imdp((void *)&t41, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t42 = t41;
                if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_src7hmnlrtmirgt, 45, 19);
                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
                gm_b16_8 t44 = t43;
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                gm_l6 = t45;
            }
ix14:;
        }
        goto ix7;
ie7:;
        {
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_l2;
            gm_t53at80pfdus3 t51;
            gm_f_kind_ghvav22((void *)&t51, t49, GM_CONV(gm_csguankko4lmhd, t50));
            gm_t53at80pfdus3 t52 = t51;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
            switch (t58) { case ((uint8_t)0ULL): goto ma55; case ((uint8_t)1ULL): goto ma56; case ((uint8_t)2ULL): goto ma56; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma56; case ((uint8_t)5ULL): goto ma56; case ((uint8_t)6ULL): goto ma56; case ((uint8_t)7ULL): goto ma56; case ((uint8_t)8ULL): goto ma56; case ((uint8_t)9ULL): goto ma56; case ((uint8_t)10ULL): goto ma56; case ((uint8_t)11ULL): goto ma56; case ((uint8_t)12ULL): goto ma56; case ((uint8_t)13ULL): goto ma56; case ((uint8_t)14ULL): goto ma56; case ((uint8_t)15ULL): goto ma56; case ((uint8_t)16ULL): goto ma56; case ((uint8_t)17ULL): goto ma56; case ((uint8_t)18ULL): goto ma56; case ((uint8_t)19ULL): goto ma56; case ((uint8_t)20ULL): goto ma56; case ((uint8_t)21ULL): goto ma56; case ((uint8_t)22ULL): goto ma56; case ((uint8_t)23ULL): goto ma56; case ((uint8_t)24ULL): goto ma56; case ((uint8_t)25ULL): goto ma56; case ((uint8_t)26ULL): goto ma56; default: goto ma56; }
ma54:;
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
            if (t60 != ((uint8_t)3ULL)) goto mn59;
            uint16_t t61 = GM_LD(uint16_t, (((uint8_t *)&t52) + 8));
            gm_l9 = t61;
            {
                gm_b16_8 t63;
                uint16_t t65 = gm_l9;
                uint8_t t66 = ((t65 == ((uint16_t)32ULL)));
                if (!t66) goto ie64;
                gm_b16_8 t68;
                GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s4cess8gum6i7));
                GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(5));
                gm_b16_8 t69 = t68;
                t63 = t69;
                goto ix64;
ie64:;
                gm_b16_8 t71;
                GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s866ofni9sb8u));
                GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(6));
                gm_b16_8 t72 = t71;
                t63 = t72;
ix64:;
                gm_l5 = t63;
            }
            {
                uint16_t t73 = gm_l9;
                gm_tu0pangbbaeec t74;
                GM_ST(uint16_t, ((uint8_t *)&t74 + 0), t73);
                gm_tabb710d9mq2e t75;
                gm_f_format_h9otq43((void *)&t75, (void *)NULL, cx, NULL, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 8, gm_src7hmnlrtmirgt, 51, 23);
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                gm_b16_8 t78 = t77;
                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                gm_l6 = t79;
            }
            goto me53;
mn59:;
ma55:;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t52) + 0));
            if (t81 != ((uint8_t)0ULL)) goto mn80;
            uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)&t52) + 8) + 0));
            gm_l10 = t82;
            uint16_t t83 = GM_LD(uint16_t, ((((uint8_t *)&t52) + 8) + 2));
            gm_l11 = t83;
            {
                gm_tvaas22dl64e0 t85;
                memset((uint8_t *)&t85 + 18, 0, 6);
                gm_b16_8 t86;
                uint8_t t88 = gm_l10;
                if (!t88) goto ie87;
                gm_b16_8 t90;
                GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(0));
                gm_b16_8 t91 = t90;
                t86 = t91;
                goto ix87;
ie87:;
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s0m9d22aen6f8));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(1));
                gm_b16_8 t94 = t93;
                t86 = t94;
ix87:;
                GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t86);
                uint16_t t95 = gm_l11;
                GM_ST(uint16_t, (uint8_t *)&t85 + 16, t95);
                gm_tabb710d9mq2e t96;
                gm_f_format_hjgrmth((void *)&t96, (void *)NULL, cx, NULL, &t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t97 = t96;
                if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_src7hmnlrtmirgt, 54, 21);
                gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
                gm_b16_8 t99 = t98;
                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                gm_l5 = t100;
            }
            {
                gm_tvaas22dl64e0 t101;
                memset((uint8_t *)&t101 + 18, 0, 6);
                gm_b16_8 t102;
                uint8_t t104 = gm_l10;
                if (!t104) goto ie103;
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sitlbs33jqqal));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(1));
                gm_b16_8 t107 = t106;
                t102 = t107;
                goto ix103;
ie103:;
                gm_b16_8 t109;
                GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s0m9d22aen6f8));
                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(1));
                gm_b16_8 t110 = t109;
                t102 = t110;
ix103:;
                GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                uint16_t t111 = gm_l11;
                GM_ST(uint16_t, (uint8_t *)&t101 + 16, t111);
                gm_tabb710d9mq2e t112;
                gm_f_format_sojmgl9((void *)&t112, (void *)NULL, cx, NULL, &t101);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t113 = t112;
                if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 8, gm_src7hmnlrtmirgt, 55, 23);
                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
                gm_b16_8 t115 = t114;
                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                gm_l6 = t116;
            }
            goto me53;
mn80:;
ma56:;
            goto me53;
mn117:;
ma57:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 48, 9);
me53:;
        }
ix7:;
    }
    {
        void * t119 = gm_l0;
        void * t120 = GM_LD(void *, (((uint8_t *)t119) + 0));
        void * t121 = (void *)(((uint8_t *)t120) + 8);
        gm_b16_8 t122 = gm_l5;
        gm_b16_8 t123 = gm_l6;
        uint64_t t124 = gm_l1;
        uint64_t t125 = gm_l4;
        gm_b16_8 t126 = gm_f_vector_typedef_vpgf6u8((void *)NULL, cx, NULL, t121, t122, t123, t124, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t127 = 0;
        F.ret = t126;
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
__attribute__((aligned(4))) void gm_f_vector_binary_etnngue(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4) {
    struct gm_fr_vector_binary_etnngue F;
    void * gm_l3; /* l */
    void * gm_l4; /* r */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_tismvprs8r13f gm_l6; /* vp */
    uint64_t gm_l7; /* n */
    uint8_t gm_l10; /* checks */
    gm_t9qkh0rsooc8d gm_l11; /* f */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_t9qkh0rsooc8d gm_l13; /* f */
    uint8_t gm_l14; /* checked */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l18; /* a */
    gm_b16_8 gm_l19; /* b */
    gm_b16_8 gm_l22; /* a */
    gm_b16_8 gm_l23; /* b */
    gm_b16_8 gm_l24; /* va */
    gm_b16_8 gm_l25; /* sym */
    uint64_t gm_l26; /* mask */
    gm_b16_8 gm_l27; /* x */
    gm_b16_8 gm_l28; /* u */
    gm_b16_8 gm_l29; /* s */
    gm_b16_8 gm_l30; /* vb */
    gm_b16_8 gm_l31; /* sym */
    gm_b16_8 gm_l32; /* x */
    gm_b16_8 gm_l33; /* u */
    gm_b16_8 gm_l34; /* s */
    GM_STACK_CHECK(&F, 3968u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = F.l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        void * t4 = F.l0;
        gm_t88rcucg1b7uk t5 = gm_l5;
        gm_f_vec_parts_srl0rr9((void *)&gm_l6, t4, GM_CONV(gm_csguankko4lmhd, t5));
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
        gm_l7 = t6;
    }
    {
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        F.l8 = t7;
    }
    {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
        F.l9 = t8;
    }
    {
        void * t9 = F.l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 1136) + 0));
        gm_l10 = t11;
    }
    {
        uint8_t t12;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma16; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma15; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; default: goto ma16; }
ma14:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t22 != ((uint8_t)0ULL)) goto pa21;
        gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 1));
        gm_l11 = t23;
        goto po20;
pa21:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t25 != ((uint8_t)1ULL)) goto pa24;
        gm_t9qkh0rsooc8d t26 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 1));
        gm_l11 = t26;
        goto po20;
pa24:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t28 != ((uint8_t)2ULL)) goto pa27;
        gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 1));
        gm_l11 = t29;
        goto po20;
pa27:;
        goto mn19;
po20:;
        uint8_t t30 = 0;
        uint8_t t31 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l11) + 0) == ((uint8_t)2ULL)));
        if (!t31) {
            uint8_t t32 = 0;
            uint8_t t33 = 0;
            uint8_t t34 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l11) + 0) == ((uint8_t)0ULL)));
            if (t34) {
                void * t35 = F.l0;
                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                void * t37 = (void *)(((uint8_t *)t36) + 8);
                gm_t88rcucg1b7uk t38 = F.l8;
                uint8_t t39 = gm_f_is_integer_eo6jkc5(t37, GM_CONV(gm_csguankko4lmhd, t38));
                t33 = t39;
            } else t33 = 0;
            if (t33) {
                uint8_t t40 = gm_l10;
                t32 = t40;
            } else t32 = 0;
            t30 = t32;
        } else t30 = 1;
        t12 = t30;
        goto me13;
mn19:;
ma15:;
        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t44 != ((uint8_t)3ULL)) goto pa43;
        goto po42;
pa43:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t46 != ((uint8_t)6ULL)) goto pa45;
        goto po42;
pa45:;
        goto mn41;
po42:;
        uint8_t t47 = gm_l10;
        t12 = t47;
        goto me13;
mn41:;
ma16:;
        t12 = ((uint8_t)0);
        goto me13;
mn48:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 145, 21);
me13:;
        gm_l14 = t12;
    }
    {
        uint8_t t50 = gm_l14;
        if (!t50) goto ie49;
        {
            uint8_t t53;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            switch (t59) { case ((uint8_t)0ULL): goto ma57; case ((uint8_t)1ULL): goto ma57; case ((uint8_t)2ULL): goto ma57; case ((uint8_t)3ULL): goto ma55; case ((uint8_t)4ULL): goto ma57; case ((uint8_t)5ULL): goto ma57; case ((uint8_t)6ULL): goto ma56; case ((uint8_t)7ULL): goto ma57; case ((uint8_t)8ULL): goto ma57; case ((uint8_t)9ULL): goto ma57; case ((uint8_t)10ULL): goto ma57; case ((uint8_t)11ULL): goto ma57; case ((uint8_t)12ULL): goto ma57; case ((uint8_t)13ULL): goto ma57; default: goto ma57; }
ma55:;
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t61 != ((uint8_t)3ULL)) goto mn60;
            t53 = ((uint8_t)1);
            goto me54;
mn60:;
ma56:;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t63 != ((uint8_t)6ULL)) goto mn62;
            t53 = ((uint8_t)1);
            goto me54;
mn62:;
ma57:;
            t53 = ((uint8_t)0);
            goto me54;
mn64:;
ma58:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 151, 12);
me54:;
            if (!t53) goto ie52;
            {
                void * t66 = F.l0;
                void * t67 = gm_l3;
                gm_t5o0tpv5jka2l t68;
                gm_f_expr_porbc9t((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t69 = t68;
                if (((((uint8_t *)&t69))[0] != 0)) {
                    gm_t08olq97rscn3 t70 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t69) + 8));
                    gm_t5o0tpv5jka2l t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                    ((uint8_t *)&t71)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t71 + 8), t70);
                    F.ret = t71;
                    goto gm_fret;
                }
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
                gm_l15 = t72;
            }
            {
                void * t73 = F.l0;
                void * t74 = gm_l4;
                gm_t5o0tpv5jka2l t75;
                gm_f_expr_porbc9t((void *)&t75, (void *)NULL, cx, NULL, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t76 = t75;
                if (((((uint8_t *)&t76))[0] != 0)) {
                    gm_t08olq97rscn3 t77 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t76) + 8));
                    gm_t5o0tpv5jka2l t78;
                    GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                    ((uint8_t *)&t78)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t78 + 8), t77);
                    F.ret = t78;
                    goto gm_fret;
                }
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 8));
                F.l16 = t79;
            }
            {
                void * t80 = F.l0;
                gm_t88rcucg1b7uk t81 = gm_l5;
                gm_b16_8 t82 = gm_l15;
                gm_b16_8 t83 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t80, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l17 = t83;
            }
            {
                void * t84 = F.l0;
                gm_t88rcucg1b7uk t85 = gm_l5;
                uint64_t t86 = gm_l7;
                gm_closure t87 = ((gm_closure){(void *)gm_f_fn_tk14v5n, (void *)&F.h, cx});
                gm_t5o0tpv5jka2l t88;
                gm_f_vec_lane_loop_a3gvooh((void *)&t88, (void *)NULL, cx, NULL, t84, t85, t86, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t89 = t88;
                if (((((uint8_t *)&t89))[0] != 0)) {
                    gm_t08olq97rscn3 t90 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t89) + 8));
                    gm_t5o0tpv5jka2l t91;
                    GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                    ((uint8_t *)&t91)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t91 + 8), t90);
                    F.ret = t91;
                    goto gm_fret;
                }
                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t89) + 8));
                gm_t5o0tpv5jka2l t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t93 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t93 + 32, 0);
                ((uint8_t *)&t93)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t93 + 8), t92);
                gm_t5o0tpv5jka2l t94 = t93;
                uint8_t t95 = ((((uint8_t *)&t94))[0] != 0);
                F.ret = t93;
                goto gm_fret;
            }
            goto ix52;
ie52:;
ix52:;
        }
        {
            void * t96 = F.l0;
            void * t97 = gm_l3;
            gm_t5o0tpv5jka2l t98;
            gm_f_expr_porbc9t((void *)&t98, (void *)NULL, cx, NULL, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t99 = t98;
            if (((((uint8_t *)&t99))[0] != 0)) {
                gm_t08olq97rscn3 t100 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t99) + 8));
                gm_t5o0tpv5jka2l t101;
                GM_ST(uint8_t, (uint8_t *)&t101 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t101 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t101 + 4, 0);
                ((uint8_t *)&t101)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t101 + 8), t100);
                F.ret = t101;
                goto gm_fret;
            }
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
            gm_l18 = t102;
        }
        {
            void * t103 = F.l0;
            void * t104 = gm_l4;
            gm_t5o0tpv5jka2l t105;
            gm_f_expr_porbc9t((void *)&t105, (void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t106 = t105;
            if (((((uint8_t *)&t106))[0] != 0)) {
                gm_t08olq97rscn3 t107 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t106) + 8));
                gm_t5o0tpv5jka2l t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                ((uint8_t *)&t108)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t108 + 8), t107);
                F.ret = t108;
                goto gm_fret;
            }
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
            gm_l19 = t109;
        }
        {
            void * t110 = F.l0;
            gm_t88rcucg1b7uk t111 = gm_l5;
            gm_b16_8 t112 = gm_l18;
            gm_b16_8 t113 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t110, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l20 = t113;
        }
        {
            void * t114 = F.l0;
            gm_t88rcucg1b7uk t115 = gm_l5;
            gm_b16_8 t116 = gm_l19;
            gm_b16_8 t117 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t114, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l21 = t117;
        }
        {
            void * t118 = F.l0;
            gm_t88rcucg1b7uk t119 = gm_l5;
            uint64_t t120 = gm_l7;
            gm_closure t121 = ((gm_closure){(void *)gm_f_fn_kvivfpi, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t122;
            gm_f_vec_lane_loop_a3gvooh((void *)&t122, (void *)NULL, cx, NULL, t118, t119, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t123 = t122;
            if (((((uint8_t *)&t123))[0] != 0)) {
                gm_t08olq97rscn3 t124 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t123) + 8));
                gm_t5o0tpv5jka2l t125;
                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                ((uint8_t *)&t125)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t125 + 8), t124);
                F.ret = t125;
                goto gm_fret;
            }
            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
            gm_t5o0tpv5jka2l t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t127 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t127 + 32, 0);
            ((uint8_t *)&t127)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t127 + 8), t126);
            gm_t5o0tpv5jka2l t128 = t127;
            uint8_t t129 = ((((uint8_t *)&t128))[0] != 0);
            F.ret = t127;
            goto gm_fret;
        }
        goto ix49;
ie49:;
ix49:;
    }
    {
        void * t130 = F.l0;
        void * t131 = gm_l3;
        gm_t5o0tpv5jka2l t132;
        gm_f_expr_porbc9t((void *)&t132, (void *)NULL, cx, NULL, t130, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t133 = t132;
        if (((((uint8_t *)&t133))[0] != 0)) {
            gm_t08olq97rscn3 t134 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t133) + 8));
            gm_t5o0tpv5jka2l t135;
            GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
            ((uint8_t *)&t135)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t135 + 8), t134);
            F.ret = t135;
            goto gm_fret;
        }
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
        gm_l22 = t136;
    }
    {
        void * t137 = F.l0;
        void * t138 = gm_l4;
        gm_t5o0tpv5jka2l t139;
        gm_f_expr_porbc9t((void *)&t139, (void *)NULL, cx, NULL, t137, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t140 = t139;
        if (((((uint8_t *)&t140))[0] != 0)) {
            gm_t08olq97rscn3 t141 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t140) + 8));
            gm_t5o0tpv5jka2l t142;
            GM_ST(uint8_t, (uint8_t *)&t142 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t142 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t142 + 4, 0);
            ((uint8_t *)&t142)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t142 + 8), t141);
            F.ret = t142;
            goto gm_fret;
        }
        gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
        gm_l23 = t143;
    }
    {
        void * t144 = F.l0;
        gm_t88rcucg1b7uk t145 = gm_l5;
        gm_b16_8 t146 = gm_l22;
        gm_b16_8 t147 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t144, t145, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t147;
    }
    {
        uint8_t t149;
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t155) { case ((uint8_t)0ULL): goto ma153; case ((uint8_t)1ULL): goto ma153; case ((uint8_t)2ULL): goto ma153; case ((uint8_t)3ULL): goto ma151; case ((uint8_t)4ULL): goto ma153; case ((uint8_t)5ULL): goto ma153; case ((uint8_t)6ULL): goto ma152; case ((uint8_t)7ULL): goto ma153; case ((uint8_t)8ULL): goto ma153; case ((uint8_t)9ULL): goto ma153; case ((uint8_t)10ULL): goto ma153; case ((uint8_t)11ULL): goto ma153; case ((uint8_t)12ULL): goto ma153; case ((uint8_t)13ULL): goto ma153; default: goto ma153; }
ma151:;
        uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t157 != ((uint8_t)3ULL)) goto mn156;
        t149 = ((uint8_t)1);
        goto me150;
mn156:;
ma152:;
        uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t159 != ((uint8_t)6ULL)) goto mn158;
        t149 = ((uint8_t)1);
        goto me150;
mn158:;
ma153:;
        t149 = ((uint8_t)0);
        goto me150;
mn160:;
ma154:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 176, 8);
me150:;
        if (!t149) goto ie148;
        {
            gm_b16_8 t162;
            uint8_t t164 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
            if (!t164) goto ie163;
            gm_b16_8 t166;
            GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s9hi985r6f61m));
            GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(2));
            gm_b16_8 t167 = t166;
            t162 = t167;
            goto ix163;
ie163:;
            gm_b16_8 t169;
            GM_ST(void *, (uint8_t *)&t169, (void *)(gm_si62ejtp0vbct));
            GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(2));
            gm_b16_8 t170 = t169;
            t162 = t170;
ix163:;
            gm_l25 = t162;
        }
        {
            uint64_t t171 = F.l9;
            uint64_t r172; bool o173 = __builtin_mul_overflow(t171, ((uint64_t)8ULL), &r172);
            if (o173) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 180, 17);
            uint64_t t174 = r172;
            uint64_t r175; bool o176 = __builtin_sub_overflow(t174, ((uint64_t)1ULL), &r175);
            if (o176) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 180, 17);
            uint64_t t177 = r175;
            gm_l26 = t177;
        }
        {
            gm_b16_8 t178;
            GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(0));
            gm_l27 = t178;
        }
        {
            uint8_t t180 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
            if (!t180) goto ie179;
            {
                gm_tftksr4op114e t182;
                gm_b16_8 t183 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t183);
                gm_b16_8 t184 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t182 + 16, t184);
                uint64_t t185 = gm_l26;
                GM_ST(uint64_t, (uint8_t *)&t182 + 32, t185);
                gm_tabb710d9mq2e t186;
                gm_f_format_t4gbkjg((void *)&t186, (void *)NULL, cx, NULL, &t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t187 = t186;
                if (((((uint8_t *)&t187))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t187) + 8, gm_src7hmnlrtmirgt, 183, 17);
                gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&t187) + 8));
                gm_b16_8 t189 = t188;
                gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                gm_l27 = t190;
            }
            goto ix179;
ie179:;
            {
                void * t192 = F.l0;
                uint64_t t193 = gm_l7;
                gm_t88rcucg1b7uk t194 = F.l8;
                gm_tqs5p4n2tq19v t195;
                GM_ST(uint8_t, ((uint8_t *)&t195), ((uint8_t)0));
                ((uint8_t *)&t195)[1] = 1;
                gm_b16_8 t196 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t192, t193, t194, t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t196;
            }
            {
                void * t197 = F.l0;
                uint64_t t198 = gm_l7;
                gm_t88rcucg1b7uk t199 = F.l8;
                gm_tqs5p4n2tq19v t200;
                memset(&t200, 0, sizeof t200);
                gm_b16_8 t201 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t197, t198, t199, t200);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l29 = t201;
            }
            {
                gm_tjftu5su6fh8e t202;
                gm_b16_8 t203 = gm_l29;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 0, t203);
                gm_b16_8 t204 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 16, t204);
                gm_b16_8 t205 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 32, t205);
                gm_b16_8 t206 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 48, t206);
                gm_b16_8 t207 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t202 + 64, t207);
                uint64_t t208 = gm_l26;
                GM_ST(uint64_t, (uint8_t *)&t202 + 80, t208);
                gm_tabb710d9mq2e t209;
                gm_f_format_cjnp8ds((void *)&t209, (void *)NULL, cx, NULL, &t202);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t210 = t209;
                if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 8, gm_src7hmnlrtmirgt, 187, 17);
                gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)&t210) + 8));
                gm_b16_8 t212 = t211;
                gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                gm_l27 = t213;
            }
ix179:;
        }
        {
            void * t214 = F.l0;
            gm_t88rcucg1b7uk t215 = gm_l5;
            gm_b16_8 t216 = gm_l27;
            gm_b16_8 t217 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t214, t215, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t218;
            GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t218 + 32, 0);
            ((uint8_t *)&t218)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t218 + 8), t217);
            gm_t5o0tpv5jka2l t219 = t218;
            uint8_t t220 = ((((uint8_t *)&t219))[0] != 0);
            F.ret = t218;
            goto gm_fret;
        }
        goto ix148;
ie148:;
ix148:;
    }
    {
        void * t221 = F.l0;
        gm_t88rcucg1b7uk t222 = gm_l5;
        gm_b16_8 t223 = gm_l23;
        gm_b16_8 t224 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t221, t222, t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t224;
    }
    {
        gm_b16_8 t225;
        uint8_t t235 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t235) { case ((uint8_t)0ULL): goto ma227; case ((uint8_t)1ULL): goto ma228; case ((uint8_t)2ULL): goto ma229; case ((uint8_t)3ULL): goto ma233; case ((uint8_t)4ULL): goto ma230; case ((uint8_t)5ULL): goto ma233; case ((uint8_t)6ULL): goto ma233; case ((uint8_t)7ULL): goto ma231; case ((uint8_t)8ULL): goto ma232; case ((uint8_t)9ULL): goto ma233; case ((uint8_t)10ULL): goto ma233; case ((uint8_t)11ULL): goto ma233; case ((uint8_t)12ULL): goto ma233; case ((uint8_t)13ULL): goto ma233; default: goto ma233; }
ma227:;
        uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t237 != ((uint8_t)0ULL)) goto mn236;
        gm_b16_8 t238;
        GM_ST(void *, (uint8_t *)&t238, (void *)(gm_sajhum9b4nr96));
        GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(1));
        t225 = t238;
        goto me226;
mn236:;
ma228:;
        uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t240 != ((uint8_t)1ULL)) goto mn239;
        gm_b16_8 t241;
        GM_ST(void *, (uint8_t *)&t241, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(1));
        t225 = t241;
        goto me226;
mn239:;
ma229:;
        uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t243 != ((uint8_t)2ULL)) goto mn242;
        gm_b16_8 t244;
        GM_ST(void *, (uint8_t *)&t244, (void *)(gm_s6s96daqmhcs9));
        GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(1));
        t225 = t244;
        goto me226;
mn242:;
ma230:;
        uint8_t t246 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t246 != ((uint8_t)4ULL)) goto mn245;
        gm_b16_8 t247;
        GM_ST(void *, (uint8_t *)&t247, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(1));
        t225 = t247;
        goto me226;
mn245:;
ma231:;
        uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t249 != ((uint8_t)7ULL)) goto mn248;
        gm_b16_8 t250;
        GM_ST(void *, (uint8_t *)&t250, (void *)(gm_smerqm8g793l6));
        GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(1));
        t225 = t250;
        goto me226;
mn248:;
ma232:;
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t252 != ((uint8_t)8ULL)) goto mn251;
        gm_b16_8 t253;
        GM_ST(void *, (uint8_t *)&t253, (void *)(gm_s2u5posfkbgnq));
        GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(1));
        t225 = t253;
        goto me226;
mn251:;
ma233:;
        gm_b16_8 t255;
        GM_ST(void *, (uint8_t *)&t255, (void *)(gm_srs1vrn2uaams));
        GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(1));
        t225 = t255;
        goto me226;
mn254:;
ma234:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 192, 19);
me226:;
        gm_l31 = t225;
    }
    {
        gm_b16_8 t256;
        GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(0));
        gm_l32 = t256;
    }
    {
        void * t258 = F.l0;
        void * t259 = GM_LD(void *, (((uint8_t *)t258) + 0));
        void * t260 = (void *)(((uint8_t *)t259) + 8);
        gm_t88rcucg1b7uk t261 = F.l8;
        uint8_t t262 = gm_f_is_integer_eo6jkc5(t260, GM_CONV(gm_csguankko4lmhd, t261));
        if (!t262) goto ie257;
        uint8_t t263;
        uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        switch (t270) { case ((uint8_t)0ULL): goto ma265; case ((uint8_t)1ULL): goto ma266; case ((uint8_t)2ULL): goto ma267; case ((uint8_t)3ULL): goto ma268; case ((uint8_t)4ULL): goto ma268; case ((uint8_t)5ULL): goto ma268; case ((uint8_t)6ULL): goto ma268; case ((uint8_t)7ULL): goto ma268; case ((uint8_t)8ULL): goto ma268; case ((uint8_t)9ULL): goto ma268; case ((uint8_t)10ULL): goto ma268; case ((uint8_t)11ULL): goto ma268; case ((uint8_t)12ULL): goto ma268; case ((uint8_t)13ULL): goto ma268; default: goto ma268; }
ma265:;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t272 != ((uint8_t)0ULL)) goto mn271;
        t263 = ((uint8_t)1);
        goto me264;
mn271:;
ma266:;
        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t274 != ((uint8_t)1ULL)) goto mn273;
        t263 = ((uint8_t)1);
        goto me264;
mn273:;
ma267:;
        uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t276 != ((uint8_t)2ULL)) goto mn275;
        t263 = ((uint8_t)1);
        goto me264;
mn275:;
ma268:;
        t263 = ((uint8_t)0);
        goto me264;
mn277:;
ma269:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 202, 37);
me264:;
        if (!t263) goto ie257;
        {
            void * t279 = F.l0;
            uint64_t t280 = gm_l7;
            gm_t88rcucg1b7uk t281 = F.l8;
            gm_tqs5p4n2tq19v t282;
            GM_ST(uint8_t, ((uint8_t *)&t282), ((uint8_t)0));
            ((uint8_t *)&t282)[1] = 1;
            gm_b16_8 t283 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t279, t280, t281, t282);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t283;
        }
        {
            void * t284 = F.l0;
            uint64_t t285 = gm_l7;
            gm_t88rcucg1b7uk t286 = F.l8;
            gm_tqs5p4n2tq19v t287;
            memset(&t287, 0, sizeof t287);
            gm_b16_8 t288 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t284, t285, t286, t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t288;
        }
        {
            gm_tif3elqk01bdl t289;
            gm_b16_8 t290 = gm_l34;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 0, t290);
            gm_b16_8 t291 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 16, t291);
            gm_b16_8 t292 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 32, t292);
            gm_b16_8 t293 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 48, t293);
            gm_b16_8 t294 = gm_l33;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 64, t294);
            gm_b16_8 t295 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t289 + 80, t295);
            gm_tabb710d9mq2e t296;
            gm_f_format_21ahlbb((void *)&t296, (void *)NULL, cx, NULL, &t289);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t297 = t296;
            if (((((uint8_t *)&t297))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t297) + 8, gm_src7hmnlrtmirgt, 206, 13);
            gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&t297) + 8));
            gm_b16_8 t299 = t298;
            gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
            gm_l32 = t300;
        }
        goto ix257;
ie257:;
        {
            gm_tfeab279c16rr t302;
            gm_b16_8 t303 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t302 + 0, t303);
            gm_b16_8 t304 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t302 + 16, t304);
            gm_b16_8 t305 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t302 + 32, t305);
            gm_tabb710d9mq2e t306;
            gm_f_format_mdlbfc1((void *)&t306, (void *)NULL, cx, NULL, &t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t307 = t306;
            if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_src7hmnlrtmirgt, 208, 13);
            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
            gm_b16_8 t309 = t308;
            gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
            gm_l32 = t310;
        }
ix257:;
    }
    {
        void * t311 = F.l0;
        gm_t88rcucg1b7uk t312 = gm_l5;
        gm_b16_8 t313 = gm_l32;
        gm_b16_8 t314 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t311, t312, t313);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t315;
        GM_ST(uint8_t, (uint8_t *)&t315 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t315 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t315 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t315 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t315 + 32, 0);
        ((uint8_t *)&t315)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t315 + 8), t314);
        gm_t5o0tpv5jka2l t316 = t315;
        uint8_t t317 = ((((uint8_t *)&t316))[0] != 0);
        F.ret = t315;
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
__attribute__((aligned(4))) void gm_f_vector_builtin_a1f1ati(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3) {
    struct gm_fr_vector_builtin_a1f1ati F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    GM_STACK_CHECK(&F, 2686u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma3; case ((uint8_t)41ULL): goto ma3; case ((uint8_t)42ULL): goto ma3; case ((uint8_t)43ULL): goto ma3; case ((uint8_t)44ULL): goto ma3; case ((uint8_t)45ULL): goto ma3; case ((uint8_t)46ULL): goto ma4; case ((uint8_t)47ULL): goto ma4; case ((uint8_t)48ULL): goto ma4; case ((uint8_t)49ULL): goto ma5; case ((uint8_t)50ULL): goto ma5; case ((uint8_t)51ULL): goto ma5; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma9; case ((uint8_t)58ULL): goto ma10; case ((uint8_t)59ULL): goto ma10; case ((uint8_t)60ULL): goto ma11; case ((uint8_t)61ULL): goto ma11; case ((uint8_t)62ULL): goto ma11; case ((uint8_t)63ULL): goto ma11; case ((uint8_t)64ULL): goto ma11; case ((uint8_t)65ULL): goto ma11; case ((uint8_t)66ULL): goto ma11; case ((uint8_t)67ULL): goto ma11; case ((uint8_t)68ULL): goto ma11; case ((uint8_t)69ULL): goto ma11; case ((uint8_t)70ULL): goto ma11; case ((uint8_t)71ULL): goto ma11; case ((uint8_t)72ULL): goto ma11; case ((uint8_t)73ULL): goto ma11; case ((uint8_t)74ULL): goto ma11; case ((uint8_t)75ULL): goto ma11; case ((uint8_t)76ULL): goto ma11; case ((uint8_t)77ULL): goto ma11; case ((uint8_t)78ULL): goto ma11; case ((uint8_t)79ULL): goto ma11; case ((uint8_t)80ULL): goto ma11; case ((uint8_t)81ULL): goto ma11; default: goto ma11; }
ma3:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t17 != ((uint8_t)40ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t19 != ((uint8_t)41ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t21 != ((uint8_t)42ULL)) goto pa20;
        goto po15;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t23 != ((uint8_t)43ULL)) goto pa22;
        goto po15;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t25 != ((uint8_t)44ULL)) goto pa24;
        goto po15;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t27 != ((uint8_t)45ULL)) goto pa26;
        goto po15;
pa26:;
        goto mn14;
po15:;
        void * t28 = gm_l0;
        gm_tvdg94bk7aa22 t29 = gm_l2;
        gm_b16_8 t30 = gm_l3;
        gm_t5o0tpv5jka2l t31;
        gm_f_vec_compare_vrcged3((void *)&t31, (void *)NULL, cx, NULL, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) {
            gm_t08olq97rscn3 t33 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t32) + 8));
            gm_t6j4d6ei14c2t t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
            ((uint8_t *)&t34)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t34 + 8), t33);
            F.ret = t34;
            goto gm_fret;
        }
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_th2lqbo77pvsc t36;
        GM_ST(uint8_t, (uint8_t *)&t36 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t36 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t36 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t36), t35);
        ((uint8_t *)&t36)[16] = 1;
        gm_t6j4d6ei14c2t t37;
        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t37 + 32, 0);
        ((uint8_t *)&t37)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t37 + 8), t36);
        gm_t6j4d6ei14c2t t38 = t37;
        uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
        F.ret = t37;
        goto gm_fret;
        goto me2;
mn14:;
ma4:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t43 != ((uint8_t)46ULL)) goto pa42;
        goto po41;
pa42:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t45 != ((uint8_t)47ULL)) goto pa44;
        goto po41;
pa44:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t47 != ((uint8_t)48ULL)) goto pa46;
        goto po41;
pa46:;
        goto mn40;
po41:;
        void * t48 = gm_l0;
        void * t49 = gm_l1;
        gm_tvdg94bk7aa22 t50 = gm_l2;
        uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t51 >= (uintptr_t)(t52)) gm_trap_index(t51, t52, gm_src7hmnlrtmirgt, 241, 80);
        void * t53 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t51 * 224);
        gm_t5o0tpv5jka2l t54;
        gm_f_vec_reduce_m5gdqn2((void *)&t54, (void *)NULL, cx, NULL, t48, t49, t50, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) {
            gm_t08olq97rscn3 t56 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t55) + 8));
            gm_t6j4d6ei14c2t t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            ((uint8_t *)&t57)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57 + 8), t56);
            F.ret = t57;
            goto gm_fret;
        }
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_th2lqbo77pvsc t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t59 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t59), t58);
        ((uint8_t *)&t59)[16] = 1;
        gm_t6j4d6ei14c2t t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t60 + 32, 0);
        ((uint8_t *)&t60)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t60 + 8), t59);
        gm_t6j4d6ei14c2t t61 = t60;
        uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
        F.ret = t60;
        goto gm_fret;
        goto me2;
mn40:;
ma5:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t66 != ((uint8_t)49ULL)) goto pa65;
        goto po64;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t68 != ((uint8_t)50ULL)) goto pa67;
        goto po64;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t70 != ((uint8_t)51ULL)) goto pa69;
        goto po64;
pa69:;
        goto mn63;
po64:;
        void * t71 = gm_l0;
        void * t72 = gm_l1;
        gm_tvdg94bk7aa22 t73 = gm_l2;
        uint64_t t74 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_src7hmnlrtmirgt, 242, 79);
        void * t76 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t74 * 224);
        gm_t5o0tpv5jka2l t77;
        gm_f_vec_mask_test_9mgimk6((void *)&t77, (void *)NULL, cx, NULL, t71, t72, t73, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t78 = t77;
        if (((((uint8_t *)&t78))[0] != 0)) {
            gm_t08olq97rscn3 t79 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t78) + 8));
            gm_t6j4d6ei14c2t t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            ((uint8_t *)&t80)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t80 + 8), t79);
            F.ret = t80;
            goto gm_fret;
        }
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
        gm_th2lqbo77pvsc t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t82), t81);
        ((uint8_t *)&t82)[16] = 1;
        gm_t6j4d6ei14c2t t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t83 + 32, 0);
        ((uint8_t *)&t83)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t83 + 8), t82);
        gm_t6j4d6ei14c2t t84 = t83;
        uint8_t t85 = ((((uint8_t *)&t84))[0] != 0);
        F.ret = t83;
        goto gm_fret;
        goto me2;
mn63:;
ma6:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t87 != ((uint8_t)52ULL)) goto mn86;
        void * t88 = gm_l0;
        void * t89 = gm_l1;
        gm_b16_8 t90 = gm_l3;
        gm_t5o0tpv5jka2l t91;
        gm_f_vec_select_3f37tvf((void *)&t91, (void *)NULL, cx, NULL, t88, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) {
            gm_t08olq97rscn3 t93 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t92) + 8));
            gm_t6j4d6ei14c2t t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            ((uint8_t *)&t94)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t94 + 8), t93);
            F.ret = t94;
            goto gm_fret;
        }
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
        gm_th2lqbo77pvsc t96;
        GM_ST(uint8_t, (uint8_t *)&t96 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t96 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t96 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t96), t95);
        ((uint8_t *)&t96)[16] = 1;
        gm_t6j4d6ei14c2t t97;
        GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t97 + 32, 0);
        ((uint8_t *)&t97)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t97 + 8), t96);
        gm_t6j4d6ei14c2t t98 = t97;
        uint8_t t99 = ((((uint8_t *)&t98))[0] != 0);
        F.ret = t97;
        goto gm_fret;
        goto me2;
mn86:;
ma7:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t103 != ((uint8_t)53ULL)) goto pa102;
        goto po101;
pa102:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t105 != ((uint8_t)54ULL)) goto pa104;
        goto po101;
pa104:;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t107 != ((uint8_t)55ULL)) goto pa106;
        goto po101;
pa106:;
        goto mn100;
po101:;
        void * t108 = gm_l0;
        void * t109 = gm_l1;
        gm_tvdg94bk7aa22 t110 = gm_l2;
        gm_b16_8 t111 = gm_l3;
        gm_t5o0tpv5jka2l t112;
        gm_f_vec_minmax_abs_4le8bpj((void *)&t112, (void *)NULL, cx, NULL, t108, t109, t110, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t113 = t112;
        if (((((uint8_t *)&t113))[0] != 0)) {
            gm_t08olq97rscn3 t114 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t113) + 8));
            gm_t6j4d6ei14c2t t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            ((uint8_t *)&t115)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t115 + 8), t114);
            F.ret = t115;
            goto gm_fret;
        }
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t113) + 8));
        gm_th2lqbo77pvsc t117;
        GM_ST(uint8_t, (uint8_t *)&t117 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t117 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t117 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t117), t116);
        ((uint8_t *)&t117)[16] = 1;
        gm_t6j4d6ei14c2t t118;
        GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t118 + 32, 0);
        ((uint8_t *)&t118)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t118 + 8), t117);
        gm_t6j4d6ei14c2t t119 = t118;
        uint8_t t120 = ((((uint8_t *)&t119))[0] != 0);
        F.ret = t118;
        goto gm_fret;
        goto me2;
mn100:;
ma8:;
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t122 != ((uint8_t)56ULL)) goto mn121;
        void * t123 = gm_l0;
        void * t124 = gm_l1;
        uint64_t t125 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t126 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t125 >= (uintptr_t)(t126)) gm_trap_index(t125, t126, gm_src7hmnlrtmirgt, 245, 55);
        void * t127 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t125 * 224);
        gm_t5o0tpv5jka2l t128;
        gm_f_vec_convert_94b6r5u((void *)&t128, (void *)NULL, cx, NULL, t123, t124, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t129 = t128;
        if (((((uint8_t *)&t129))[0] != 0)) {
            gm_t08olq97rscn3 t130 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t129) + 8));
            gm_t6j4d6ei14c2t t131;
            GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
            ((uint8_t *)&t131)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t131 + 8), t130);
            F.ret = t131;
            goto gm_fret;
        }
        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t129) + 8));
        gm_th2lqbo77pvsc t133;
        GM_ST(uint8_t, (uint8_t *)&t133 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t133 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t133 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t133), t132);
        ((uint8_t *)&t133)[16] = 1;
        gm_t6j4d6ei14c2t t134;
        GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t134 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t134 + 32, 0);
        ((uint8_t *)&t134)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t134 + 8), t133);
        gm_t6j4d6ei14c2t t135 = t134;
        uint8_t t136 = ((((uint8_t *)&t135))[0] != 0);
        F.ret = t134;
        goto gm_fret;
        goto me2;
mn121:;
ma9:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t138 != ((uint8_t)57ULL)) goto mn137;
        void * t139 = gm_l0;
        void * t140 = gm_l1;
        gm_b16_8 t141 = gm_l3;
        gm_t5o0tpv5jka2l t142;
        gm_f_vec_shuffle_j6jmffl((void *)&t142, (void *)NULL, cx, NULL, t139, t140, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t143 = t142;
        if (((((uint8_t *)&t143))[0] != 0)) {
            gm_t08olq97rscn3 t144 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t143) + 8));
            gm_t6j4d6ei14c2t t145;
            GM_ST(uint8_t, (uint8_t *)&t145 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t145 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t145 + 4, 0);
            ((uint8_t *)&t145)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t145 + 8), t144);
            F.ret = t145;
            goto gm_fret;
        }
        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
        gm_th2lqbo77pvsc t147;
        GM_ST(uint8_t, (uint8_t *)&t147 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t147 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t147 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t147), t146);
        ((uint8_t *)&t147)[16] = 1;
        gm_t6j4d6ei14c2t t148;
        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t148 + 32, 0);
        ((uint8_t *)&t148)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t148 + 8), t147);
        gm_t6j4d6ei14c2t t149 = t148;
        uint8_t t150 = ((((uint8_t *)&t149))[0] != 0);
        F.ret = t148;
        goto gm_fret;
        goto me2;
mn137:;
ma10:;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t154 != ((uint8_t)58ULL)) goto pa153;
        goto po152;
pa153:;
        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t156 != ((uint8_t)59ULL)) goto pa155;
        goto po152;
pa155:;
        goto mn151;
po152:;
        void * t157 = gm_l0;
        void * t158 = gm_l1;
        gm_tvdg94bk7aa22 t159 = gm_l2;
        gm_b16_8 t160 = gm_l3;
        gm_t5o0tpv5jka2l t161;
        gm_f_vec_load_store_ng9gfpc((void *)&t161, (void *)NULL, cx, NULL, t157, t158, t159, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t162 = t161;
        if (((((uint8_t *)&t162))[0] != 0)) {
            gm_t08olq97rscn3 t163 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t162) + 8));
            gm_t6j4d6ei14c2t t164;
            GM_ST(uint8_t, (uint8_t *)&t164 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t164 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t164 + 4, 0);
            ((uint8_t *)&t164)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t164 + 8), t163);
            F.ret = t164;
            goto gm_fret;
        }
        gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
        gm_th2lqbo77pvsc t166;
        GM_ST(uint8_t, (uint8_t *)&t166 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t166 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t166 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t166), t165);
        ((uint8_t *)&t166)[16] = 1;
        gm_t6j4d6ei14c2t t167;
        GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t167 + 32, 0);
        ((uint8_t *)&t167)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t167 + 8), t166);
        gm_t6j4d6ei14c2t t168 = t167;
        uint8_t t169 = ((((uint8_t *)&t168))[0] != 0);
        F.ret = t167;
        goto gm_fret;
        goto me2;
mn151:;
ma11:;
        gm_th2lqbo77pvsc t171;
        memset(&t171, 0, sizeof t171);
        gm_t6j4d6ei14c2t t172;
        GM_ST(uint8_t, (uint8_t *)&t172 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t172 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t172 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t172 + 32, 0);
        ((uint8_t *)&t172)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t172 + 8), t171);
        gm_t6j4d6ei14c2t t173 = t172;
        uint8_t t174 = ((((uint8_t *)&t173))[0] != 0);
        F.ret = t172;
        goto gm_fret;
        goto me2;
mn170:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 239, 5);
me2:;
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
__attribute__((aligned(4))) void gm_f_vector_unary_h7ar7ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_vector_unary_h7ar7ml F;
    gm_t9qkh0rsooc8d gm_l2; /* op */
    void * gm_l3; /* x */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_tismvprs8r13f gm_l5; /* vp */
    uint64_t gm_l6; /* n */
    gm_b16_8 gm_l9; /* v */
    gm_b16_8 gm_l11; /* a */
    gm_b16_8 gm_l12; /* r */
    gm_b16_8 gm_l13; /* u */
    gm_b16_8 gm_l14; /* s */
    GM_STACK_CHECK(&F, 1777u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = F.l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = F.l0;
        gm_t88rcucg1b7uk t5 = gm_l4;
        gm_f_vec_parts_srl0rr9((void *)&gm_l5, t4, GM_CONV(gm_csguankko4lmhd, t5));
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 0));
        gm_l6 = t6;
    }
    {
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
        F.l7 = t7;
    }
    {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
        F.l8 = t8;
    }
    {
        void * t9 = F.l0;
        void * t10 = gm_l3;
        gm_t5o0tpv5jka2l t11;
        gm_f_expr_porbc9t((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t12 = t11;
        if (((((uint8_t *)&t12))[0] != 0)) {
            gm_t08olq97rscn3 t13 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t12) + 8));
            gm_t5o0tpv5jka2l t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), t13);
            F.ret = t14;
            goto gm_fret;
        }
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
        gm_l9 = t15;
    }
    {
        uint8_t t17 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t17) goto ie16;
        void * t18 = F.l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_t88rcucg1b7uk t21 = F.l7;
        uint8_t t22 = gm_f_is_integer_eo6jkc5(t20, GM_CONV(gm_csguankko4lmhd, t21));
        if (!t22) goto ie16;
        void * t23 = F.l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 1136) + 0));
        if (!t25) goto ie16;
        {
            void * t27 = F.l0;
            gm_t88rcucg1b7uk t28 = gm_l4;
            gm_b16_8 t29 = gm_l9;
            gm_b16_8 t30 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t30;
        }
        {
            void * t31 = F.l0;
            gm_t88rcucg1b7uk t32 = gm_l4;
            uint64_t t33 = gm_l6;
            gm_closure t34 = ((gm_closure){(void *)gm_f_fn_dahlogl, (void *)&F.h, cx});
            gm_t5o0tpv5jka2l t35;
            gm_f_vec_lane_loop_a3gvooh((void *)&t35, (void *)NULL, cx, NULL, t31, t32, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) {
                gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t36) + 8));
                gm_t5o0tpv5jka2l t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                ((uint8_t *)&t38)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), t37);
                F.ret = t38;
                goto gm_fret;
            }
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_t5o0tpv5jka2l t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t40 + 32, 0);
            ((uint8_t *)&t40)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t40 + 8), t39);
            gm_t5o0tpv5jka2l t41 = t40;
            uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
            F.ret = t40;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t43 = F.l0;
        gm_t88rcucg1b7uk t44 = gm_l4;
        gm_b16_8 t45 = gm_l9;
        gm_b16_8 t46 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t43, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t46;
    }
    {
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(0));
        gm_l12 = t47;
    }
    {
        uint8_t t49 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        if (!t49) goto ie48;
        {
            void * t52 = F.l0;
            void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
            void * t54 = (void *)(((uint8_t *)t53) + 8);
            gm_t88rcucg1b7uk t55 = F.l7;
            uint8_t t56 = gm_f_is_float_gthejtb(t54, GM_CONV(gm_csguankko4lmhd, t55));
            if (!t56) goto ie51;
            {
                gm_b16_8 t58 = gm_l11;
                gm_tnpifv20irnph t59;
                GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
                gm_tabb710d9mq2e t60;
                gm_f_format_dlemvkb((void *)&t60, (void *)NULL, cx, NULL, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t61 = t60;
                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_src7hmnlrtmirgt, 124, 17);
                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
                gm_b16_8 t63 = t62;
                gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
                gm_l12 = t64;
            }
            goto ix51;
ie51:;
            {
                void * t66 = F.l0;
                uint64_t t67 = gm_l6;
                gm_t88rcucg1b7uk t68 = F.l7;
                gm_tqs5p4n2tq19v t69;
                GM_ST(uint8_t, ((uint8_t *)&t69), ((uint8_t)0));
                ((uint8_t *)&t69)[1] = 1;
                gm_b16_8 t70 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t66, t67, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t70;
            }
            {
                void * t71 = F.l0;
                uint64_t t72 = gm_l6;
                gm_t88rcucg1b7uk t73 = F.l7;
                gm_tqs5p4n2tq19v t74;
                memset(&t74, 0, sizeof t74);
                gm_b16_8 t75 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t71, t72, t73, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t75;
            }
            {
                gm_t0uuhaljsf410 t76;
                gm_b16_8 t77 = gm_l14;
                GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t77);
                gm_b16_8 t78 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t76 + 16, t78);
                gm_b16_8 t79 = gm_l13;
                GM_ST(gm_b16_8, (uint8_t *)&t76 + 32, t79);
                gm_b16_8 t80 = gm_l11;
                GM_ST(gm_b16_8, (uint8_t *)&t76 + 48, t80);
                gm_tabb710d9mq2e t81;
                gm_f_format_jo1gjr5((void *)&t81, (void *)NULL, cx, NULL, &t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t82 = t81;
                if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_src7hmnlrtmirgt, 128, 17);
                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                gm_b16_8 t84 = t83;
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                gm_l12 = t85;
            }
ix51:;
        }
        goto ix48;
ie48:;
        void * t87 = F.l0;
        void * t88 = GM_LD(void *, (((uint8_t *)t87) + 0));
        void * t89 = (void *)(((uint8_t *)t88) + 8);
        gm_t88rcucg1b7uk t90 = F.l7;
        gm_t53at80pfdus3 t91;
        gm_f_kind_ghvav22((void *)&t91, t89, GM_CONV(gm_csguankko4lmhd, t90));
        gm_t53at80pfdus3 t92 = t91;
        uint8_t t93 = ((GM_LD(uint8_t, ((uint8_t *)&t92) + 0) == ((uint8_t)4ULL)));
        if (!t93) goto ie86;
        {
            gm_b16_8 t95 = gm_l11;
            gm_tnpifv20irnph t96;
            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t95);
            gm_tabb710d9mq2e t97;
            gm_f_format_njau13r((void *)&t97, (void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t98 = t97;
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src7hmnlrtmirgt, 131, 13);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = t99;
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_l12 = t101;
        }
        goto ix86;
ie86:;
        {
            gm_b16_8 t103 = gm_l11;
            gm_tnpifv20irnph t104;
            GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
            gm_tabb710d9mq2e t105;
            gm_f_format_nhrpa25((void *)&t105, (void *)NULL, cx, NULL, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t106 = t105;
            if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_src7hmnlrtmirgt, 133, 13);
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
            gm_b16_8 t108 = t107;
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            gm_l12 = t109;
        }
ix86:;
ix48:;
    }
    {
        void * t110 = F.l0;
        gm_t88rcucg1b7uk t111 = gm_l4;
        gm_b16_8 t112 = gm_l12;
        gm_b16_8 t113 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t110, t111, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t114 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t114 + 32, 0);
        ((uint8_t *)&t114)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t113);
        gm_t5o0tpv5jka2l t115 = t114;
        uint8_t t116 = ((((uint8_t *)&t115))[0] != 0);
        F.ret = t114;
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
