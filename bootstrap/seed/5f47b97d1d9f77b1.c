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
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk7kkm8e69l5f;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t88rcucg1b7uk f2; uint32_t f3; gm_t88rcucg1b7uk f4; uint8_t pe[4]; } gm_t0h5lk09e9p5r;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; uint64_t f2; unsigned __int128 f3; uint64_t f4; uint64_t f5; uint64_t f6; void * f7; } gm_t4nomjfe3hlif;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tk7kkm8e69l5f) == 24 && _Alignof(gm_tk7kkm8e69l5f) == 8 && offsetof(gm_tk7kkm8e69l5f, f0) == 0 && offsetof(gm_tk7kkm8e69l5f, f1) == 8 && offsetof(gm_tk7kkm8e69l5f, f2) == 16, "layout of `Deferred`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
_Static_assert(sizeof(gm_t0h5lk09e9p5r) == 40 && _Alignof(gm_t0h5lk09e9p5r) == 8 && offsetof(gm_t0h5lk09e9p5r, f0) == 0 && offsetof(gm_t0h5lk09e9p5r, f1) == 16 && offsetof(gm_t0h5lk09e9p5r, f2) == 24 && offsetof(gm_t0h5lk09e9p5r, f3) == 28 && offsetof(gm_t0h5lk09e9p5r, f4) == 32, "layout of `(host: []u8, addr: u64, whole: Ty, off: u32, fty: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4nomjfe3hlif) == 64 && _Alignof(gm_t4nomjfe3hlif) == 16 && offsetof(gm_t4nomjfe3hlif, f0) == 0 && offsetof(gm_t4nomjfe3hlif, f1) == 4 && offsetof(gm_t4nomjfe3hlif, f2) == 8 && offsetof(gm_t4nomjfe3hlif, f3) == 16 && offsetof(gm_t4nomjfe3hlif, f4) == 32 && offsetof(gm_t4nomjfe3hlif, f5) == 40 && offsetof(gm_t4nomjfe3hlif, f6) == 48 && offsetof(gm_t4nomjfe3hlif, f7) == 56, "layout of `(variant: u32, tag_ty: Ty, tag_size: u64, tag: u128, tag_w: u64, off: u64, size: u64, payload: ?^Pat)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; uint64_t v1; uint64_t v2; gm_t0h5lk09e9p5r v3; } f1; } gm_tou12vuc19e0s;
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { gm_tou12vuc19e0s f0; uint8_t f1; uint8_t pe[7]; } gm_tae0la6qgkh3h;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tou12vuc19e0s v1; } f1; } gm_tl1r7a2fn1ev6;
_Static_assert(sizeof(gm_tou12vuc19e0s) == 48 && _Alignof(gm_tou12vuc19e0s) == 8 && offsetof(gm_tou12vuc19e0s, f0) == 0 && offsetof(gm_tou12vuc19e0s, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_tae0la6qgkh3h) == 56 && _Alignof(gm_tae0la6qgkh3h) == 8 && offsetof(gm_tae0la6qgkh3h, f0) == 0 && offsetof(gm_tae0la6qgkh3h, f1) == 48, "layout of `?Place`");
_Static_assert(sizeof(gm_tl1r7a2fn1ev6) == 56 && _Alignof(gm_tl1r7a2fn1ev6) == 8 && offsetof(gm_tl1r7a2fn1ev6, f0) == 0 && offsetof(gm_tl1r7a2fn1ev6, f1) == 8, "layout of `Flow!Place`");
struct gm_fr_block_5b89ev3 {
    gm_frame h;
    gm_t2k9dbsv5ff58 l6; /* st */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_block_defers_2qpdkkn {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* defers */
    gm_t2k9dbsv5ff58 l8; /* st */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_conds_30ust77 {
    gm_frame h;
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_empty_slice_6h1imen_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lopbqdt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_errdefers_run_kn2msce {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_exec_other_3ogmhtp {
    gm_frame h;
    gm_t2k9dbsv5ff58 l2; /* st */
    gm_tou12vuc19e0s l26; /* p */
    gm_trqfvmjtt4sij l30; /* ps */
    gm_tou12vuc19e0s l41; /* p */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_exec_stmt_4jh6tlr {
    gm_frame h;
    gm_t2k9dbsv5ff58 l2; /* st */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_lexically_same_e45n6np {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_ib5k4gb_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mgqfh8v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_o51qq3k {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_loop__3bo7tki {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_loop_body_js59b5a {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_match__tg8e3g3 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_match_pat_og55a3h {
    gm_frame h;
    gm_t59juu943t8sl l9; /* r */
    gm_t4nomjfe3hlif l20; /* x */
    gm_t1ec8br9ddkav ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_push_2v345ri_2 {
    gm_frame h;
    gm_tae0la6qgkh3h l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_aaqnel1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_afk8dfq_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_0ui7njc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s8usk1j_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t387o75 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_res_is_err_8jbgndp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_run_defers_buhs35d {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_set_local_node_he6tesa {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_truncate_9n3opb7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) void gm_f_block_5b89ev3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_block_defers_2qpdkkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_cond_v8r7708(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_conds_30ust77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_deinit_2q1sifg(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_6u0ld99(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lopbqdt(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint8_t gm_f_errdefers_run_kn2msce(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_errdefers_run_kn2msce_c(void * a0, gm_t76123gfet73g a1);
__attribute__((aligned(4))) void gm_f_ev_25jfdar(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_ev_discard_8c4a8jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) void gm_f_ew_c5kil07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_exec_other_3ogmhtp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2);
__attribute__((aligned(4))) void gm_f_exec_stmt_4jh6tlr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_kill_temps_8sgbc55(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_lexically_same_e45n6np(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_loop__3bo7tki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_loop_body_js59b5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) void gm_f_match__tg8e3g3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_match_pat_og55a3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_place_47rfqe5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aaqnel1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t387o75(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_run_defers_buhs35d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) void gm_f_set_local_node_he6tesa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3);
__attribute__((aligned(4))) void gm_f_set_local_ug1vgfu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_truncate_9n3opb7(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write_place_pd5r6up(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s9h40dg66q82b[58];
extern const uint8_t gm_sqnu8fg3ikbeg[35];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcph0vopkouaai[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6h1imen_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6h1imen_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lopbqdt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lopbqdt F;
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d3uqusp_2();
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
inline __attribute__((aligned(4))) void gm_f_list_mgqfh8v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mgqfh8v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6h1imen_2();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3) {
    struct gm_fr_local_addr_o51qq3k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* fr */
    uint32_t gm_l3; /* l */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = gm_f_us_cuv01fp(t3);
        gm_l4 = t4;
    }
    {
        uint64_t t6 = gm_l4;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l2;
        uint64_t t9 = GM_LD(uintptr_t, (((uint8_t *)t8) + 64) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_src6hq50r4rk7e6, 193, 8);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t8) + 64)) + t7 * 8));
        uint8_t t11 = (!(t10 == ((uint64_t)0ULL)));
        if (!t11) goto ie5;
        {
            uint64_t t13 = gm_l4;
            uint64_t t14 = (uintptr_t)t13;
            void * t15 = gm_l2;
            uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 64) + 8);
            if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_src6hq50r4rk7e6, 193, 36);
            uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 64)) + t14 * 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8, t17);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l1;
        void * t20 = gm_l2;
        uint32_t t21 = gm_l3;
        gm_f_local_alloc_qm9q4fo((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t18, t19, t20, t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2v345ri_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2v345ri_2 F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_6h1imen_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_s8usk1j_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tae0la6qgkh3h t21 = F.l1;
        GM_ST(gm_tae0la6qgkh3h, ((uint8_t *)t20 + t18 * 56), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aaqnel1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_aaqnel1 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_lopbqdt();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_t387o75(t10, ((uint64_t)1ULL), t12);
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
        gm_tiilmfd16v3fc t21 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a1));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)t20 + t18 * 24), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_afk8dfq_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_afk8dfq_2 F;
    void * gm_l0; /* l */
    gm_tk7kkm8e69l5f gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_d3uqusp_2();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0ui7njc_2(t10, ((uint64_t)1ULL), t12);
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
        gm_tk7kkm8e69l5f t21 = GM_LD(gm_tk7kkm8e69l5f, ((uint8_t *)a1));
        GM_ST(gm_tk7kkm8e69l5f, ((uint8_t *)t20 + t18 * 24), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0ui7njc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0ui7njc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s8usk1j_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s8usk1j_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t387o75(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t387o75 F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_9n3opb7(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_9n3opb7 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
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
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 329, 13);
                uint64_t t12 = gm_l2;
                uint8_t t13 = (uint8_t)t12;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1), t13);
            }
            {
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 330, 13);
                uint64_t t16 = gm_l2;
                unsigned __int128 n17 = (unsigned __int128)((uint64_t)8ULL);
                if (n17 >= 64) gm_trap_shift((uint64_t)n17, "u64", gm_srcnqtpfma6qe4a, 330, 21);
                uint64_t t18 = (uint64_t)(t16 >> (int)n17);
                uint8_t t19 = (uint8_t)t18;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1), t19);
            }
            {
                uint64_t t20 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_srcnqtpfma6qe4a, 331, 13);
                uint64_t t22 = gm_l2;
                unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
                if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 331, 21);
                uint64_t t24 = (uint64_t)(t22 >> (int)n23);
                uint8_t t25 = (uint8_t)t24;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t20 * 1), t25);
            }
            {
                uint64_t t26 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcnqtpfma6qe4a, 332, 13);
                uint64_t t28 = gm_l2;
                unsigned __int128 n29 = (unsigned __int128)((uint64_t)24ULL);
                if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcnqtpfma6qe4a, 332, 21);
                uint64_t t30 = (uint64_t)(t28 >> (int)n29);
                uint8_t t31 = (uint8_t)t30;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t26 * 1), t31);
            }
            {
                uint64_t t32 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 333, 13);
                uint64_t t34 = gm_l2;
                unsigned __int128 n35 = (unsigned __int128)((uint64_t)32ULL);
                if (n35 >= 64) gm_trap_shift((uint64_t)n35, "u64", gm_srcnqtpfma6qe4a, 333, 21);
                uint64_t t36 = (uint64_t)(t34 >> (int)n35);
                uint8_t t37 = (uint8_t)t36;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1), t37);
            }
            {
                uint64_t t38 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_srcnqtpfma6qe4a, 334, 13);
                uint64_t t40 = gm_l2;
                unsigned __int128 n41 = (unsigned __int128)((uint64_t)40ULL);
                if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 334, 21);
                uint64_t t42 = (uint64_t)(t40 >> (int)n41);
                uint8_t t43 = (uint8_t)t42;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t38 * 1), t43);
            }
            {
                uint64_t t44 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcnqtpfma6qe4a, 335, 13);
                uint64_t t46 = gm_l2;
                unsigned __int128 n47 = (unsigned __int128)((uint64_t)48ULL);
                if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srcnqtpfma6qe4a, 335, 21);
                uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                uint8_t t49 = (uint8_t)t48;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t44 * 1), t49);
            }
            {
                uint64_t t50 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 336, 13);
                uint64_t t52 = gm_l2;
                unsigned __int128 n53 = (unsigned __int128)((uint64_t)56ULL);
                if (n53 >= 64) gm_trap_shift((uint64_t)n53, "u64", gm_srcnqtpfma6qe4a, 336, 21);
                uint64_t t54 = (uint64_t)(t52 >> (int)n53);
                uint8_t t55 = (uint8_t)t54;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1), t55);
            }
            {
                gm_b0_1 t56;
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t60 = ((t59 == ((uint64_t)1ULL)));
            if (!t60) goto ie58;
            {
                uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcnqtpfma6qe4a, 340, 13);
                uint64_t t64 = gm_l2;
                uint8_t t65 = (uint8_t)t64;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t62 * 1), t65);
            }
            {
                gm_b0_1 t66;
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix58;
ie58:;
ix58:;
        }
        {
            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t70 = ((t69 == ((uint64_t)4ULL)));
            if (!t70) goto ie68;
            {
                uint64_t t72 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t72 >= (uintptr_t)(t73)) gm_trap_index(t72, t73, gm_srcnqtpfma6qe4a, 344, 13);
                uint64_t t74 = gm_l2;
                uint8_t t75 = (uint8_t)t74;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t72 * 1), t75);
            }
            {
                uint64_t t76 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcnqtpfma6qe4a, 345, 13);
                uint64_t t78 = gm_l2;
                unsigned __int128 n79 = (unsigned __int128)((uint64_t)8ULL);
                if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 345, 21);
                uint64_t t80 = (uint64_t)(t78 >> (int)n79);
                uint8_t t81 = (uint8_t)t80;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t76 * 1), t81);
            }
            {
                uint64_t t82 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 346, 13);
                uint64_t t84 = gm_l2;
                unsigned __int128 n85 = (unsigned __int128)((uint64_t)16ULL);
                if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcnqtpfma6qe4a, 346, 21);
                uint64_t t86 = (uint64_t)(t84 >> (int)n85);
                uint8_t t87 = (uint8_t)t86;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1), t87);
            }
            {
                uint64_t t88 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t88 >= (uintptr_t)(t89)) gm_trap_index(t88, t89, gm_srcnqtpfma6qe4a, 347, 13);
                uint64_t t90 = gm_l2;
                unsigned __int128 n91 = (unsigned __int128)((uint64_t)24ULL);
                if (n91 >= 64) gm_trap_shift((uint64_t)n91, "u64", gm_srcnqtpfma6qe4a, 347, 21);
                uint64_t t92 = (uint64_t)(t90 >> (int)n91);
                uint8_t t93 = (uint8_t)t92;
                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t88 * 1), t93);
            }
            {
                gm_b0_1 t94;
                uint8_t t95 = 0;
                F.ret = t94;
                goto gm_fret;
            }
            goto ix68;
ie68:;
ix68:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t96 = gm_l0;
        gm_b16_8 t97 = gm_l1;
        uint64_t t98 = gm_l2;
        unsigned __int128 t99 = (unsigned __int128)t98;
        gm_f_put_uint_cddin99(t96, t97, t99);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_block_5b89ev3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_block_5b89ev3 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l7; /* t */
    GM_STACK_CHECK(&F, 419u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 24));
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            {
                void * t8 = gm_l2;
                gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 0));
                gm_l4 = t9;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t13 = (t11 < t12);
                    if (!t13) goto lb10;
                    {
                        uint64_t t16 = gm_l5;
                        uint64_t t17 = (uintptr_t)t16;
                        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_t2k9dbsv5ff58 t21 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t17 * 40));
                        F.l6 = t21;
                        {
                            void * t23 = gm_l0;
                            gm_tbl8il92l97ut t24 = gm_l1;
                            gm_tcfa6l7reoosn t25;
                            gm_f_exec_stmt_4jh6tlr((void *)&t25, (void *)NULL, cx, NULL, t23, t24, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l6)));
                            if (gm_unw.kind) {
                                goto gm_ll14;
                            }
                            if (((((uint8_t *)&t25))[0] != 0)) {
                                gm_tcfa6l7reoosn t26;
                                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                                ((uint8_t *)&t26)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t26 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t25) + 8)));
                                F.ret = t26;
                                goto gm_fret;
                            }
                            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 8));
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 13, 9);
me19:;
                    }
lc10:;
                    {
                        uint64_t t29 = gm_l5;
                        uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                        uint64_t t32 = r30;
                        gm_l5 = t32;
                    }
                }
gm_ll14:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
                }
                goto gm_lad;
lb10:;
            }
        }
        {
            void * t34 = gm_l2;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t35)) != 0)) goto ie33;
            void * t36 = GM_LD(void *, ((uint8_t *)&t35));
            gm_l7 = t36;
            {
                void * t38 = gm_l0;
                gm_tbl8il92l97ut t39 = gm_l1;
                void * t40 = gm_l7;
                gm_b16_8 t41 = gm_l3;
                gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t38, t39, t40, t41);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_b0_1 t42;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t42);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t43 = gm_l0;
        gm_tbl8il92l97ut t44 = gm_l1;
        void * t45 = gm_l2;
        gm_b16_8 t46 = gm_l3;
        gm_f_block_defers_2qpdkkn((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t43, t44, t45, t46);
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
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_block_defers_2qpdkkn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_block_defers_2qpdkkn F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    gm_t07lf02ct4q61 gm_l5; /* failed */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l9; /* is_err */
    void * gm_l10; /* body */
    gm_t76123gfet73g gm_l11; /* f */
    void * gm_l12; /* t */
    gm_t76123gfet73g gm_l13; /* f */
    gm_t76123gfet73g gm_l14; /* f */
    gm_tcfa6l7reoosn gm_l15; /* r */
    GM_STACK_CHECK(&F, 717u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_f_list_ib5k4gb_2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_t07lf02ct4q61 t2;
        memset(&t2, 0, sizeof t2);
        gm_l5 = t2;
    }
    {
        {
            void * t4 = gm_l2;
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
            gm_l6 = t5;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l7;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l7;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t2k9dbsv5ff58 t17 = GM_LD(gm_t2k9dbsv5ff58, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t13 * 40));
                    F.l8 = t17;
                    {
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                        if (t20 != ((uint8_t)6ULL)) goto ie19;
                        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                        gm_l9 = t21;
                        void * t22 = GM_LD(void *, ((((uint8_t *)&F.l8) + 8) + 8));
                        gm_l10 = t22;
                        {
                            void * t24 = (void *)((uint8_t *)&F.l4);
                            gm_tk7kkm8e69l5f t25;
                            memset((uint8_t *)&t25 + 1, 0, 7);
                            memset((uint8_t *)&t25 + 20, 0, 4);
                            uint8_t t26 = gm_l9;
                            GM_ST(uint8_t, (uint8_t *)&t25 + 0, t26);
                            void * t27 = gm_l10;
                            GM_ST(void *, (uint8_t *)&t25 + 8, t27);
                            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
                            GM_ST(uint32_t, (uint8_t *)&t25 + 16, t28);
                            gm_tftb1odc739t5 t29 = gm_f_push_afk8dfq_2(t24, &t25);
                            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcph0vopkouaai, 27, 13);
                            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                        }
                        {
                            goto lc6;
                        }
                        goto ix19;
ie19:;
ix19:;
                    }
                    {
                        void * t31 = gm_l0;
                        gm_tbl8il92l97ut t32 = gm_l1;
                        gm_tcfa6l7reoosn t33;
                        gm_f_exec_stmt_4jh6tlr((void *)&t33, (void *)NULL, cx, NULL, t31, t32, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l8)));
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_b0_1 t34;
                        if (!((((uint8_t *)&t33))[0] != 0)) {
                            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 8));
                            t34 = t35;
                        } else {
                            gm_l11 = (*(gm_t76123gfet73g *)(((uint8_t *)&t33) + 8));
                            {
                                const uint8_t *t37 = ((uint8_t *)&gm_l11);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 28, 0);
                                ((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)))[24] = 1;
                                memmove((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 0, t37, 24);
                            }
                            goto lb6;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 25, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t39 = gm_l7;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l7 = t42;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        uint8_t t44;
        if (((((uint8_t *)&gm_l5))[24] != 0)) goto mn46;
        t44 = ((uint8_t)1);
        goto me45;
mn46:;
        t44 = ((uint8_t)0);
        goto me45;
mn47:;
        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 35, 8);
me45:;
        if (!t44) goto ie43;
        {
            void * t50 = gm_l2;
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t51)) != 0)) goto ie49;
            void * t52 = GM_LD(void *, ((uint8_t *)&t51));
            gm_l12 = t52;
            {
                void * t54 = gm_l0;
                gm_tbl8il92l97ut t55 = gm_l1;
                void * t56 = gm_l12;
                gm_b16_8 t57 = gm_l3;
                gm_tcfa6l7reoosn t58;
                gm_f_ev_25jfdar((void *)&t58, (void *)NULL, cx, NULL, t54, t55, t56, t57);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t59;
                if (!((((uint8_t *)&t58))[0] != 0)) {
                    gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 8));
                    t59 = t60;
                } else {
                    gm_l13 = (*(gm_t76123gfet73g *)(((uint8_t *)&t58) + 8));
                    {
                        const uint8_t *t62 = ((uint8_t *)&gm_l13);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 28, 0);
                        ((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)))[24] = 1;
                        memmove((uint8_t *)&(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)) + 0, t62, 24);
                    }
                    t59 = GM_ZERO(gm_b0_1);
                }
            }
            goto ix49;
ie49:;
ix49:;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t65 = ((t64 == ((uint64_t)0ULL)));
        if (!t65) goto ie63;
        {
            if (!((((uint8_t *)&gm_l5))[24] != 0)) goto ie67;
            gm_t76123gfet73g t68 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l5));
            gm_l14 = t68;
            {
                const uint8_t *t70 = ((uint8_t *)&gm_l14);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t70, 24);
                goto gm_fdone;
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            gm_b0_1 t71;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t71);
            goto gm_fdone;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        void * t72 = gm_l0;
        gm_tbl8il92l97ut t73 = gm_l1;
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        gm_f_run_defers_buhs35d((void *)&gm_l15, (void *)NULL, cx, NULL, t72, t73, t75, &(*(gm_t07lf02ct4q61 *)((uint8_t *)&gm_l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t76 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_6u0ld99(t76);
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l15), 32);
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
inline __attribute__((aligned(4))) void gm_f_conds_30ust77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2) {
    struct gm_fr_conds_30ust77 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l2; /* parts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* e */
    void * gm_l7; /* pat */
    void * gm_l8; /* value */
    gm_b16_8 gm_l9; /* b */
    GM_STACK_CHECK(&F, 622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        void * t20 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l6 = t20;
                        void * t22 = gm_l0;
                        gm_tbl8il92l97ut t23 = gm_l1;
                        void * t24 = gm_l6;
                        gm_t833u93mr4iob t25;
                        gm_f_ew_c5kil07((void *)&t25, (void *)NULL, cx, NULL, t22, t23, t24);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (((((uint8_t *)&t25))[0] != 0)) {
                            gm_t1ec8br9ddkav t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                            ((uint8_t *)&t26)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t26 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t25) + 8)));
                            F.ret = t26;
                            goto gm_fret;
                        }
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&t25) + 8));
                        uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                        if (!t28) goto ie21;
                        {
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                            goto gm_fdone;
                        }
                        goto ix21;
ie21:;
ix21:;
                        goto me17;
mn18:;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t31 != ((uint8_t)1ULL)) goto mn30;
                        void * t32 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t32;
                        void * t33 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t33;
                        {
                            void * t35 = gm_l0;
                            void * t36 = gm_l8;
                            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 104));
                            gm_b16_8 t38 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t35, t37);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_l9 = t38;
                        }
                        {
                            void * t39 = gm_l0;
                            gm_tbl8il92l97ut t40 = gm_l1;
                            void * t41 = gm_l8;
                            gm_b16_8 t42 = gm_l9;
                            gm_tcfa6l7reoosn t43;
                            gm_f_ev_25jfdar((void *)&t43, (void *)NULL, cx, NULL, t39, t40, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t43))[0] != 0)) {
                                gm_t1ec8br9ddkav t44;
                                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                                ((uint8_t *)&t44)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t44 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t43) + 8)));
                                F.ret = t44;
                                goto gm_fret;
                            }
                            gm_b0_1 t45 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 8));
                        }
                        {
                            void * t47 = gm_l0;
                            gm_tbl8il92l97ut t48 = gm_l1;
                            void * t49 = gm_l7;
                            gm_b16_8 t50 = gm_l9;
                            gm_t1ec8br9ddkav t51;
                            gm_f_match_pat_og55a3h((void *)&t51, (void *)NULL, cx, NULL, t47, t48, t49, t50);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            if (((((uint8_t *)&t51))[0] != 0)) {
                                gm_t1ec8br9ddkav t52;
                                GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                                ((uint8_t *)&t52)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t52 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t51) + 8)));
                                F.ret = t52;
                                goto gm_fret;
                            }
                            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t51) + 8));
                            uint8_t t54 = (uint8_t)!t53;
                            if (!t54) goto ie46;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                goto gm_fdone;
                            }
                            goto ix46;
ie46:;
ix46:;
                        }
                        goto me17;
mn30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 189, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 188, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t57 = gm_l4;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l4 = t60;
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
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_errdefers_run_kn2msce(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_errdefers_run_kn2msce F;
    void * gm_l0; /* fr */
    gm_t76123gfet73g gm_l1; /* flow */
    uint64_t gm_l2; /* frame */
    uint8_t gm_l3; /* is_err */
    uint64_t gm_l4; /* frame */
    uint64_t gm_l5; /* frame */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma3; case ((uint8_t)5ULL): goto ma3; case ((uint8_t)6ULL): goto ma6; default: goto ma7; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t12 != ((uint8_t)4ULL)) goto pa11;
    goto po10;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t14 != ((uint8_t)5ULL)) goto pa13;
    goto po10;
pa13:;
    goto mn9;
po10:;
    t1 = ((uint8_t)0);
    goto me2;
mn9:;
ma4:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 0));
    gm_l2 = t17;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)a1) + 8) + 8));
    gm_l3 = t18;
    uint8_t t19;
    void * t21 = gm_l0;
    uint64_t t22 = gm_l2;
    uint8_t t23 = gm_f_lexically_same_e45n6np(t21, t22);
    if (!t23) goto ie20;
    uint8_t t25 = gm_l3;
    uint8_t t26 = t25;
    t19 = t26;
    goto ix20;
ie20:;
    uint8_t t28 = ((uint8_t)1);
    t19 = t28;
ix20:;
    t1 = t19;
    goto me2;
mn15:;
ma5:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t32 != ((uint8_t)0ULL)) goto pa31;
    uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 0));
    gm_l4 = t33;
    goto po30;
pa31:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t35 != ((uint8_t)1ULL)) goto pa34;
    uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 0));
    gm_l4 = t36;
    goto po30;
pa34:;
    goto mn29;
po30:;
    void * t37 = gm_l0;
    uint64_t t38 = gm_l4;
    uint8_t t39 = gm_f_lexically_same_e45n6np(t37, t38);
    uint8_t t40 = (uint8_t)!t39;
    t1 = t40;
    goto me2;
mn29:;
ma6:;
    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t44 != ((uint8_t)3ULL)) goto pa43;
    goto po42;
pa43:;
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    if (t46 != ((uint8_t)6ULL)) goto pa45;
    goto po42;
pa45:;
    goto mn41;
po42:;
    t1 = ((uint8_t)1);
    goto me2;
mn41:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 61, 56);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_errdefers_run_kn2msce_c(void * a0, gm_t76123gfet73g a1) { return gm_f_errdefers_run_kn2msce(a0, &a1); }
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_exec_other_3ogmhtp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2) {
    struct gm_fr_exec_other_3ogmhtp F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l3; /* e */
    uint32_t gm_l4; /* l */
    void * gm_l5; /* value */
    uint32_t gm_l6; /* l */
    uint64_t gm_l7; /* off */
    uint64_t gm_l8; /* size */
    void * gm_l9; /* init */
    void * gm_l10; /* fr */
    uint64_t gm_l11; /* i */
    void * gm_l12; /* e */
    uint64_t gm_l13; /* o */
    gm_b16_8 gm_l14; /* b */
    gm_b16_8 gm_l15; /* locals */
    void * gm_l16; /* init */
    gm_b16_8 gm_l17; /* b */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t9805qtonbsag gm_l21; /* l */
    uint32_t gm_l22; /* x */
    gm_b16_8 gm_l23; /* fb */
    void * gm_l24; /* place_ */
    void * gm_l25; /* value */
    gm_b16_8 gm_l27; /* b */
    gm_b16_8 gm_l28; /* places */
    void * gm_l29; /* value */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    void * gm_l33; /* x */
    void * gm_l34; /* pe */
    gm_b16_8 gm_l35; /* b */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    void * gm_l39; /* x */
    void * gm_l40; /* pe */
    gm_b16_8 gm_l42; /* fb */
    GM_STACK_CHECK(&F, 3535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma10; default: goto ma11; }
ma4:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t14 != ((uint8_t)5ULL)) goto mn13;
            void * t15 = GM_LD(void *, (((uint8_t *)a2) + 8));
            gm_l3 = t15;
            void * t16 = gm_l0;
            gm_tbl8il92l97ut t17 = gm_l1;
            void * t18 = gm_l3;
            gm_tcfa6l7reoosn t19;
            gm_f_ev_discard_8c4a8jg((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) {
                gm_tcfa6l7reoosn t20;
                GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                ((uint8_t *)&t20)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t20 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t19) + 8)));
                F.ret = t20;
                goto gm_fret;
            }
            gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 8));
            goto me3;
mn13:;
ma5:;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t23 != ((uint8_t)2ULL)) goto mn22;
            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 0));
            gm_l4 = t24;
            void * t25 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 8));
            gm_l5 = t25;
            void * t26 = gm_l0;
            gm_tbl8il92l97ut t27 = gm_l1;
            uint32_t t28 = gm_l4;
            void * t29 = gm_l5;
            gm_tcfa6l7reoosn t30;
            gm_f_set_local_node_he6tesa((void *)&t30, (void *)NULL, cx, NULL, t26, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t30))[0] != 0)) {
                gm_tcfa6l7reoosn t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t31 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t30) + 8)));
                F.ret = t31;
                goto gm_fret;
            }
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 8));
            goto me3;
mn22:;
ma6:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t34 != ((uint8_t)0ULL)) goto mn33;
            uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 0));
            gm_l6 = t35;
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
            gm_l7 = t36;
            uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 16));
            gm_l8 = t37;
            void * t38 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 24));
            gm_l9 = t38;
            {
                void * t40 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                gm_l10 = t40;
            }
            {
                uint32_t t41 = gm_l6;
                uint64_t t42 = (uint64_t)t41;
                uint64_t t43 = gm_f_us_cuv01fp(t42);
                gm_l11 = t43;
            }
            {
                void * t45 = gm_l9;
                if (!(GM_LD(void *, ((uint8_t *)&t45)) != 0)) goto ie44;
                void * t46 = GM_LD(void *, ((uint8_t *)&t45));
                gm_l12 = t46;
                {
                    uint64_t t49 = gm_l11;
                    uint64_t t50 = (uintptr_t)t49;
                    void * t51 = gm_l10;
                    uint64_t t52 = GM_LD(uintptr_t, (((uint8_t *)t51) + 64) + 8);
                    if (t50 >= (uintptr_t)(t52)) gm_trap_index(t50, t52, gm_srcph0vopkouaai, 134, 20);
                    uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t51) + 64)) + t50 * 8));
                    uint8_t t54 = ((t53 == ((uint64_t)0ULL)));
                    if (!t54) goto ie48;
                    uint64_t t55 = gm_l11;
                    uint64_t t56 = (uintptr_t)t55;
                    void * t57 = gm_l10;
                    void * t58 = GM_LD(void *, (((uint8_t *)t57) + 16));
                    uint64_t t59 = GM_LD(uintptr_t, (((uint8_t *)t58) + 120) + 8);
                    if (t56 >= (uintptr_t)(t59)) gm_trap_index(t56, t59, gm_srcph0vopkouaai, 134, 43);
                    uint8_t t60 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t58) + 120)) + t56 * 1));
                    uint8_t t61 = (uint8_t)!t60;
                    if (!t61) goto ie48;
                    {
                        uint64_t t63 = gm_l7;
                        uint64_t t64 = gm_f_us_cuv01fp(t63);
                        gm_l13 = t64;
                    }
                    {
                        void * t65 = gm_l0;
                        gm_tbl8il92l97ut t66 = gm_l1;
                        void * t67 = gm_l12;
                        void * t68 = gm_l10;
                        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 48));
                        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&t69) + 8);
                        void * t71 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t69)));
                        uint64_t t72 = gm_l13;
                        uint64_t t73 = (uintptr_t)t72;
                        uint64_t t74 = gm_l13;
                        uint64_t t75 = gm_l8;
                        uint64_t t76 = gm_f_us_cuv01fp(t75);
                        uint64_t r77; bool o78 = __builtin_add_overflow(t74, t76, &r77);
                        if (o78) gm_trap("integer overflow in `+`", gm_srcph0vopkouaai, 136, 48);
                        uint64_t t79 = r77;
                        uint64_t t80 = (uintptr_t)t79;
                        if (t73 > t80 || t80 > (uintptr_t)t70) gm_trap_slice(t73, t80, t70, gm_srcph0vopkouaai, 136, 36);
                        gm_b16_8 t81;
                        GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t71 + t73 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80 - t73));
                        gm_tcfa6l7reoosn t82;
                        gm_f_ev_25jfdar((void *)&t82, (void *)NULL, cx, NULL, t65, t66, t67, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t82))[0] != 0)) {
                            gm_tcfa6l7reoosn t83;
                            GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                            ((uint8_t *)&t83)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t83 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t82) + 8)));
                            F.ret = t83;
                            goto gm_fret;
                        }
                        gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 8));
                    }
                    {
                        gm_b0_1 t85;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t85);
                        goto gm_fdone;
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                {
                    void * t86 = gm_l0;
                    uint64_t t87 = gm_l8;
                    gm_b16_8 t88 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t88;
                }
                {
                    void * t89 = gm_l0;
                    gm_tbl8il92l97ut t90 = gm_l1;
                    void * t91 = gm_l12;
                    gm_b16_8 t92 = gm_l14;
                    gm_tcfa6l7reoosn t93;
                    gm_f_ev_25jfdar((void *)&t93, (void *)NULL, cx, NULL, t89, t90, t91, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t93))[0] != 0)) {
                        gm_tcfa6l7reoosn t94;
                        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                        ((uint8_t *)&t94)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t94 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t93) + 8)));
                        F.ret = t94;
                        goto gm_fret;
                    }
                    gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 8));
                }
                {
                    void * t96 = gm_l0;
                    void * t97 = gm_l12;
                    gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t97) + 112));
                    void * t99 = gm_l10;
                    uint32_t t100 = gm_l6;
                    gm_b16_8 t101 = gm_l14;
                    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                    gm_tcfa6l7reoosn t103;
                    gm_f_set_local_ug1vgfu((void *)&t103, (void *)NULL, cx, NULL, t96, t98, t99, t100, t102);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t103))[0] != 0)) {
                        gm_tcfa6l7reoosn t104;
                        GM_ST(uint8_t, (uint8_t *)&t104 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t104 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t104 + 4, 0);
                        ((uint8_t *)&t104)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t104 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t103) + 8)));
                        F.ret = t104;
                        goto gm_fret;
                    }
                    gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 8));
                }
                goto ix44;
ie44:;
                uint64_t t107 = gm_l11;
                uint64_t t108 = (uintptr_t)t107;
                void * t109 = gm_l10;
                void * t110 = GM_LD(void *, (((uint8_t *)t109) + 16));
                uint64_t t111 = GM_LD(uintptr_t, (((uint8_t *)t110) + 120) + 8);
                if (t108 >= (uintptr_t)(t111)) gm_trap_index(t108, t111, gm_srcph0vopkouaai, 142, 23);
                uint8_t t112 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t110) + 120)) + t108 * 1));
                if (!t112) goto ie106;
                {
                    {
                        void * t115 = gm_l0;
                        gm_tl874t2atnlh3 t116 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                        void * t117 = gm_l10;
                        uint32_t t118 = gm_l6;
                        gm_t833u93mr4iob t119;
                        gm_f_local_addr_o51qq3k((void *)&t119, (void *)NULL, cx, NULL, t115, t116, t117, t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
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
                        uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&t119) + 8));
                    }
                }
                goto ix106;
ie106:;
ix106:;
ix44:;
            }
            goto me3;
mn33:;
ma7:;
            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t123 != ((uint8_t)1ULL)) goto mn122;
            gm_b16_8 t124 = GM_LD(gm_b16_8, ((((uint8_t *)a2) + 8) + 0));
            gm_l15 = t124;
            void * t125 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 16));
            gm_l16 = t125;
            {
                void * t127 = gm_l0;
                void * t128 = gm_l16;
                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t128) + 104));
                gm_b16_8 t130 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t127, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t130;
            }
            {
                void * t131 = gm_l0;
                gm_tbl8il92l97ut t132 = gm_l1;
                void * t133 = gm_l16;
                gm_b16_8 t134 = gm_l17;
                gm_tcfa6l7reoosn t135;
                gm_f_ev_25jfdar((void *)&t135, (void *)NULL, cx, NULL, t131, t132, t133, t134);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t135))[0] != 0)) {
                    gm_tcfa6l7reoosn t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    ((uint8_t *)&t136)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t136 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t135) + 8)));
                    F.ret = t136;
                    goto gm_fret;
                }
                gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t135) + 8));
            }
            {
                {
                    gm_b16_8 t139 = gm_l15;
                    gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                    gm_l18 = t140;
                }
                {
                    gm_l19 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t142 = gm_l19;
                        uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        uint8_t t144 = (t142 < t143);
                        if (!t144) goto lb141;
                        {
                            uint64_t t147 = gm_l19;
                            uint64_t t148 = gm_l19;
                            uint64_t t149 = (uintptr_t)t148;
                            uint64_t t150 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                            gm_t9805qtonbsag t151 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t149 * 8));
                            gm_tp0pp2cmr6tvd t152;
                            GM_ST(uint64_t, ((uint8_t *)&t152 + 0), t147);
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t152 + 8), t151);
                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&t152) + 0));
                            gm_l20 = t155;
                            gm_t9805qtonbsag t156 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t152) + 8));
                            gm_l21 = t156;
                            {
                                if (!((((uint8_t *)&gm_l21))[4] != 0)) goto ie158;
                                uint32_t t159 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                                gm_l22 = t159;
                                {
                                    void * t161 = gm_l0;
                                    void * t162 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                                    void * t163 = GM_LD(void *, (((uint8_t *)t162) + 16));
                                    gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)t163) + 88));
                                    gm_b16_8 t165 = t164;
                                    uint32_t t166 = gm_l22;
                                    uint64_t t167 = (uint64_t)t166;
                                    uint64_t t168 = gm_f_us_cuv01fp(t167);
                                    uint64_t t169 = (uintptr_t)t168;
                                    uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&t165) + 8);
                                    if (t169 >= (uintptr_t)(t170)) gm_trap_index(t169, t170, gm_srcph0vopkouaai, 151, 33);
                                    uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t165)) + t169 * 8));
                                    gm_b16_8 t172 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t161, t171);
                                    if (gm_unw.kind) {
                                        goto gm_ll145;
                                    }
                                    gm_l23 = t172;
                                }
                                {
                                    void * t173 = gm_l0;
                                    void * t174 = gm_l16;
                                    gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t174) + 96));
                                    gm_b16_8 t176 = gm_l17;
                                    uint64_t t177 = gm_l20;
                                    uint32_t t178 = (uint32_t)t177;
                                    gm_b16_8 t179 = gm_l23;
                                    gm_b0_1 t180 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t173, t175, t176, t178, t179);
                                    if (gm_unw.kind) {
                                        goto gm_ll145;
                                    }
                                }
                                {
                                    void * t181 = gm_l0;
                                    void * t182 = gm_l16;
                                    gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t182) + 112));
                                    void * t184 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                                    uint32_t t185 = gm_l22;
                                    gm_b16_8 t186 = gm_l23;
                                    gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                                    gm_tcfa6l7reoosn t188;
                                    gm_f_set_local_ug1vgfu((void *)&t188, (void *)NULL, cx, NULL, t181, t183, t184, t185, t187);
                                    if (gm_unw.kind) {
                                        goto gm_ll145;
                                    }
                                    if (((((uint8_t *)&t188))[0] != 0)) {
                                        gm_tcfa6l7reoosn t189;
                                        GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                        ((uint8_t *)&t189)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t189 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t188) + 8)));
                                        F.ret = t189;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t190 = GM_LD(gm_b0_1, (((uint8_t *)&t188) + 8));
                                }
                                goto ix158;
ie158:;
ix158:;
                            }
                            goto me153;
mn154:;
                            gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 149, 13);
me153:;
                        }
lc141:;
                        {
                            uint64_t t192 = gm_l19;
                            uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                            uint64_t t195 = r193;
                            gm_l19 = t195;
                        }
                    }
gm_ll145:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb141; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc141; }
                    }
                    goto gm_lad;
lb141:;
                }
            }
            goto me3;
mn122:;
ma8:;
            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t197 != ((uint8_t)3ULL)) goto mn196;
            void * t198 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 0));
            gm_l24 = t198;
            void * t199 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 8));
            gm_l25 = t199;
            {
                void * t201 = gm_l0;
                gm_tbl8il92l97ut t202 = gm_l1;
                void * t203 = gm_l24;
                gm_tl1r7a2fn1ev6 t204;
                gm_f_place_47rfqe5((void *)&t204, (void *)NULL, cx, NULL, t201, t202, t203, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t204))[0] != 0)) {
                    gm_tcfa6l7reoosn t205;
                    GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                    ((uint8_t *)&t205)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t205 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t204) + 8)));
                    F.ret = t205;
                    goto gm_fret;
                }
                F.l26 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t204) + 8));
            }
            {
                void * t206 = gm_l0;
                void * t207 = gm_l25;
                uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)t207) + 104));
                gm_b16_8 t209 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t206, t208);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t209;
            }
            {
                void * t210 = gm_l0;
                gm_tbl8il92l97ut t211 = gm_l1;
                void * t212 = gm_l25;
                gm_b16_8 t213 = gm_l27;
                gm_tcfa6l7reoosn t214;
                gm_f_ev_25jfdar((void *)&t214, (void *)NULL, cx, NULL, t210, t211, t212, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t214))[0] != 0)) {
                    gm_tcfa6l7reoosn t215;
                    GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
                    ((uint8_t *)&t215)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t215 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t214) + 8)));
                    F.ret = t215;
                    goto gm_fret;
                }
                gm_b0_1 t216 = GM_LD(gm_b0_1, (((uint8_t *)&t214) + 8));
            }
            {
                void * t217 = gm_l0;
                void * t218 = gm_l24;
                gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t218) + 112));
                void * t220 = gm_l24;
                gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t220) + 96));
                gm_b16_8 t222 = gm_l27;
                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                gm_tcfa6l7reoosn t224;
                gm_f_write_place_pd5r6up((void *)&t224, (void *)NULL, cx, NULL, t217, t219, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l26)), t221, t223);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t224))[0] != 0)) {
                    gm_tcfa6l7reoosn t225;
                    GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                    ((uint8_t *)&t225)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t225 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t224) + 8)));
                    F.ret = t225;
                    goto gm_fret;
                }
                gm_b0_1 t226 = GM_LD(gm_b0_1, (((uint8_t *)&t224) + 8));
            }
            goto me3;
mn196:;
ma9:;
            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t228 != ((uint8_t)4ULL)) goto mn227;
            gm_b16_8 t229 = GM_LD(gm_b16_8, ((((uint8_t *)a2) + 8) + 0));
            gm_l28 = t229;
            void * t230 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 16));
            gm_l29 = t230;
            {
                gm_f_list_mgqfh8v_2((void *)&F.l30, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t233 = gm_l28;
                    gm_l31 = t233;
                }
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t235 = gm_l32;
                        uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        uint8_t t237 = (t235 < t236);
                        if (!t237) goto lb234;
                        {
                            uint64_t t240 = gm_l32;
                            uint64_t t241 = (uintptr_t)t240;
                            uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                            void * t243 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t241 * 8));
                            void * t246 = GM_LD(void *, ((uint8_t *)&t243));
                            gm_l33 = t246;
                            {
                                void * t249 = gm_l33;
                                if (!(GM_LD(void *, ((uint8_t *)&t249)) != 0)) goto ie248;
                                void * t250 = GM_LD(void *, ((uint8_t *)&t249));
                                gm_l34 = t250;
                                {
                                    void * t252 = (void *)((uint8_t *)&F.l30);
                                    void * t253 = gm_l0;
                                    gm_tbl8il92l97ut t254 = gm_l1;
                                    void * t255 = gm_l34;
                                    gm_tl1r7a2fn1ev6 t256;
                                    gm_f_place_47rfqe5((void *)&t256, (void *)NULL, cx, NULL, t253, t254, t255, ((uint8_t)0));
                                    if (gm_unw.kind) {
                                        goto gm_ll238;
                                    }
                                    if (((((uint8_t *)&t256))[0] != 0)) {
                                        gm_tcfa6l7reoosn t257;
                                        GM_ST(uint8_t, (uint8_t *)&t257 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t257 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t257 + 4, 0);
                                        ((uint8_t *)&t257)[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t257 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t256) + 8)));
                                        F.ret = t257;
                                        goto gm_fret;
                                    }
                                    gm_tae0la6qgkh3h t258;
                                    GM_ST(uint8_t, (uint8_t *)&t258 + 49, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t258 + 50, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t258 + 52, 0);
                                    GM_ST(gm_tou12vuc19e0s, ((uint8_t *)&t258), (*(gm_tou12vuc19e0s *)(((uint8_t *)&t256) + 8)));
                                    ((uint8_t *)&t258)[48] = 1;
                                    gm_tftb1odc739t5 t259 = gm_f_push_2v345ri_2(t252, &t258);
                                    if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srcph0vopkouaai, 166, 30);
                                    gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
                                }
                                goto ix248;
ie248:;
                                {
                                    void * t262 = (void *)((uint8_t *)&F.l30);
                                    gm_tae0la6qgkh3h t263;
                                    memset(&t263, 0, sizeof t263);
                                    gm_tftb1odc739t5 t264 = gm_f_push_2v345ri_2(t262, &t263);
                                    if (((((uint8_t *)&t264))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t264) + 1, gm_srcph0vopkouaai, 166, 75);
                                    gm_b0_1 t265 = GM_LD(gm_b0_1, (((uint8_t *)&t264) + 1));
                                }
ix248:;
                            }
                            goto me244;
mn245:;
                            gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 165, 13);
me244:;
                        }
lc234:;
                        {
                            uint64_t t267 = gm_l32;
                            uint64_t r268; bool o269 = __builtin_add_overflow(t267, ((uint64_t)1ULL), &r268);
                            uint64_t t270 = r268;
                            gm_l32 = t270;
                        }
                    }
gm_ll238:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb234; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc234; }
                    }
                    goto gm_lad;
lb234:;
                }
            }
            {
                void * t271 = gm_l0;
                void * t272 = gm_l29;
                uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)t272) + 104));
                gm_b16_8 t274 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t271, t273);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t274;
            }
            {
                void * t275 = gm_l0;
                gm_tbl8il92l97ut t276 = gm_l1;
                void * t277 = gm_l29;
                gm_b16_8 t278 = gm_l35;
                gm_tcfa6l7reoosn t279;
                gm_f_ev_25jfdar((void *)&t279, (void *)NULL, cx, NULL, t275, t276, t277, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t279))[0] != 0)) {
                    gm_tcfa6l7reoosn t280;
                    GM_ST(uint8_t, (uint8_t *)&t280 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t280 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t280 + 4, 0);
                    ((uint8_t *)&t280)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t280 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t279) + 8)));
                    F.ret = t280;
                    goto gm_fret;
                }
                gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t279) + 8));
            }
            {
                {
                    gm_b16_8 t283 = gm_l28;
                    gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
                    gm_l36 = t284;
                }
                {
                    gm_l37 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t286 = gm_l37;
                        uint64_t t287 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        uint8_t t288 = (t286 < t287);
                        if (!t288) goto lb285;
                        {
                            uint64_t t291 = gm_l37;
                            uint64_t t292 = gm_l37;
                            uint64_t t293 = (uintptr_t)t292;
                            uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                            void * t295 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t293 * 8));
                            gm_t22qobfhrnoif t296;
                            GM_ST(uint64_t, ((uint8_t *)&t296 + 0), t291);
                            GM_ST(void *, ((uint8_t *)&t296 + 8), t295);
                            uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&t296) + 0));
                            gm_l38 = t299;
                            void * t300 = GM_LD(void *, (((uint8_t *)&t296) + 8));
                            gm_l39 = t300;
                            {
                                void * t302 = gm_l39;
                                void * t303;
                                if ((GM_LD(void *, ((uint8_t *)&t302)) != 0)) {
                                    void * t304 = GM_LD(void *, (((uint8_t *)&t302) + 0));
                                    t303 = t304;
                                } else {
                                    goto lc285;
                                }
                                gm_l40 = t303;
                            }
                            {
                                uint64_t t305 = gm_l38;
                                uint64_t t306 = (uintptr_t)t305;
                                uint64_t t307 = GM_LD(uintptr_t, (((uint8_t *)&F.l30) + 0) + 8);
                                if (t306 >= (uintptr_t)(t307)) gm_trap_index(t306, t307, gm_srcph0vopkouaai, 172, 22);
                                gm_tae0la6qgkh3h t308 = GM_LD(gm_tae0la6qgkh3h, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l30) + 0)) + t306 * 56));
                                gm_tou12vuc19e0s t309;
                                if (((((uint8_t *)&t308))[48] != 0)) {
                                    t309 = (*(gm_tou12vuc19e0s *)(((uint8_t *)&t308) + 0));
                                } else {
                                    goto lc285;
                                }
                                F.l41 = t309;
                            }
                            {
                                void * t310 = gm_l0;
                                void * t311 = gm_l40;
                                uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)t311) + 104));
                                gm_b16_8 t313 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t310, t312);
                                if (gm_unw.kind) {
                                    goto gm_ll289;
                                }
                                gm_l42 = t313;
                            }
                            {
                                void * t314 = gm_l0;
                                void * t315 = gm_l29;
                                gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t315) + 96));
                                gm_b16_8 t317 = gm_l35;
                                uint64_t t318 = gm_l38;
                                uint32_t t319 = (uint32_t)t318;
                                gm_b16_8 t320 = gm_l42;
                                gm_b0_1 t321 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t314, t316, t317, t319, t320);
                                if (gm_unw.kind) {
                                    goto gm_ll289;
                                }
                            }
                            {
                                void * t322 = gm_l0;
                                void * t323 = gm_l40;
                                gm_tl874t2atnlh3 t324 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t323) + 112));
                                void * t325 = gm_l40;
                                gm_t88rcucg1b7uk t326 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t325) + 96));
                                gm_b16_8 t327 = gm_l42;
                                gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
                                gm_tcfa6l7reoosn t329;
                                gm_f_write_place_pd5r6up((void *)&t329, (void *)NULL, cx, NULL, t322, t324, &(*(gm_tou12vuc19e0s *)((uint8_t *)&F.l41)), t326, t328);
                                if (gm_unw.kind) {
                                    goto gm_ll289;
                                }
                                if (((((uint8_t *)&t329))[0] != 0)) {
                                    gm_tcfa6l7reoosn t330;
                                    GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
                                    ((uint8_t *)&t330)[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t330 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t329) + 8)));
                                    F.ret = t330;
                                    goto gm_fret;
                                }
                                gm_b0_1 t331 = GM_LD(gm_b0_1, (((uint8_t *)&t329) + 8));
                            }
                            goto me297;
mn298:;
                            gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 170, 13);
me297:;
                        }
lc285:;
                        {
                            uint64_t t333 = gm_l37;
                            uint64_t r334; bool o335 = __builtin_add_overflow(t333, ((uint64_t)1ULL), &r334);
                            uint64_t t336 = r334;
                            gm_l37 = t336;
                        }
                    }
gm_ll289:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb285; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc285; }
                    }
                    goto gm_lad;
lb285:;
                }
            }
            {
                void * t337 = (void *)((uint8_t *)&F.l30);
                gm_f_deinit_2q1sifg(t337);
            }
            goto me3;
mn227:;
ma10:;
            uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t339 != ((uint8_t)6ULL)) goto mn338;
            goto me3;
mn338:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 126, 5);
me3:;
        }
    }
    gm_b0_1 t341;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t341);
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
inline __attribute__((aligned(4))) void gm_f_exec_stmt_4jh6tlr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, const void * a2) {
    struct gm_fr_exec_stmt_4jh6tlr F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint64_t gm_l3; /* tm */
    gm_t2lqu8jsj511d gm_l4; /* mk */
    void * gm_l5; /* e */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* value */
    gm_tcfa6l7reoosn gm_l8; /* r */
    GM_STACK_CHECK(&F, 448u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 416) + 0) + 8);
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_t2lqu8jsj511d t5 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t4));
        gm_l4 = t5;
    }
    {
        gm_tcfa6l7reoosn t6;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t14 != ((uint8_t)5ULL)) goto mn13;
        void * t15 = GM_LD(void *, (((uint8_t *)a2) + 8));
        gm_l5 = t15;
        void * t16 = gm_l0;
        gm_tbl8il92l97ut t17 = gm_l1;
        void * t18 = gm_l5;
        gm_tcfa6l7reoosn t19;
        gm_f_ev_discard_8c4a8jg((void *)&t19, (void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t6 = t19;
        goto me7;
mn13:;
ma9:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t21 != ((uint8_t)2ULL)) goto mn20;
        uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 0));
        gm_l6 = t22;
        void * t23 = GM_LD(void *, ((((uint8_t *)a2) + 8) + 8));
        gm_l7 = t23;
        void * t24 = gm_l0;
        gm_tbl8il92l97ut t25 = gm_l1;
        uint32_t t26 = gm_l6;
        void * t27 = gm_l7;
        gm_tcfa6l7reoosn t28;
        gm_f_set_local_node_he6tesa((void *)&t28, (void *)NULL, cx, NULL, t24, t25, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t6 = t28;
        goto me7;
mn20:;
ma10:;
        void * t30 = gm_l0;
        gm_tbl8il92l97ut t31 = gm_l1;
        gm_tcfa6l7reoosn t32;
        gm_f_exec_other_3ogmhtp((void *)&t32, (void *)NULL, cx, NULL, t30, t31, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t6 = t32;
        goto me7;
mn29:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 98, 10);
me7:;
        gm_l8 = t6;
    }
    {
        void * t34 = gm_l0;
        uint64_t t35 = GM_LD(uintptr_t, ((((uint8_t *)t34) + 416) + 0) + 8);
        uint64_t t36 = gm_l3;
        uint8_t t37 = (t35 > t36);
        if (!t37) goto ie33;
        {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l3;
            gm_b0_1 t41 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t42 = gm_l0;
        gm_t2lqu8jsj511d t43 = gm_l4;
        gm_f_release_jn5kqdi(t42, GM_CONV(gm_csc7lc6jlkqkuq, t43));
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l8), 32);
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
inline __attribute__((aligned(4))) uint8_t gm_f_lexically_same_e45n6np(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lexically_same_e45n6np F;
    void * gm_l0; /* fr */
    uint64_t gm_l1; /* target */
    void * gm_l2; /* cur */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t7 = gm_l2;
                uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 0));
                uint64_t t9 = gm_l1;
                uint8_t t10 = ((t8 == t9));
                if (!t10) goto ie6;
                {
                    uint8_t t12 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                goto ix6;
ie6:;
ix6:;
            }
            {
                void * t14 = gm_l2;
                uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 240));
                uint8_t t16 = (uint8_t)!t15;
                if (!t16) goto ie13;
                {
                    uint8_t t18 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                goto ix13;
ie13:;
ix13:;
            }
            {
                void * t19 = gm_l2;
                void * t20 = GM_LD(void *, (((uint8_t *)t19) + 40));
                void * t21;
                if ((GM_LD(void *, ((uint8_t *)&t20)) != 0)) {
                    void * t22 = GM_LD(void *, (((uint8_t *)&t20) + 0));
                    t21 = t22;
                } else {
                    uint8_t t23 = 0;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
                gm_l2 = t21;
            }
lc3:;
        }
lb3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_loop__3bo7tki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5, gm_b16_8 a6) {
    struct gm_fr_loop__3bo7tki F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l3; /* cond */
    void * gm_l4; /* body */
    void * gm_l5; /* step */
    gm_b16_8 gm_l6; /* dst */
    void * gm_l7; /* fr */
    uint64_t gm_l8; /* depth */
    gm_tcfa6l7reoosn gm_l9; /* r */
    gm_t76123gfet73g gm_l10; /* f */
    uint64_t gm_l11; /* frame */
    uint32_t gm_l12; /* l */
    GM_STACK_CHECK(&F, 425u);
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
            void * t3 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            gm_l7 = t3;
        }
        {
            void * t4 = gm_l7;
            void * t5 = (void *)(((uint8_t *)t4) + 136);
            gm_tiilmfd16v3fc t6;
            memset((uint8_t *)&t6 + 4, 0, 4);
            uint32_t t7 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t6 + 0, t7);
            gm_b16_8 t8 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 8, t8);
            gm_tftb1odc739t5 t9 = gm_f_push_aaqnel1(t5, &t6);
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcph0vopkouaai, 291, 5);
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        }
        {
            void * t11 = gm_l7;
            uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 136) + 0) + 8);
            gm_l8 = t12;
        }
        {
            void * t13 = gm_l0;
            gm_tbl8il92l97ut t14 = gm_l1;
            uint32_t t15 = gm_l2;
            gm_b16_8 t16 = gm_l3;
            void * t17 = gm_l4;
            void * t18 = gm_l5;
            gm_f_loop_body_js59b5a((void *)&gm_l9, (void *)NULL, cx, NULL, t13, t14, t15, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t19 = gm_l7;
            void * t20 = (void *)(((uint8_t *)t19) + 136);
            uint64_t t21 = gm_l8;
            uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
            if (o23) gm_trap("integer overflow in `-`", gm_srcph0vopkouaai, 294, 24);
            uint64_t t24 = r22;
            gm_f_truncate_9n3opb7(t20, t24);
        }
        {
            gm_tcfa6l7reoosn t25 = gm_l9;
            gm_b0_1 t26;
            if (!((((uint8_t *)&t25))[0] != 0)) {
                gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 8));
                t26 = t27;
            } else {
                gm_l10 = (*(gm_t76123gfet73g *)(((uint8_t *)&t25) + 8));
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    if (t30 != ((uint8_t)0ULL)) goto ie29;
                    uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)&gm_l10) + 8) + 0));
                    gm_l11 = t31;
                    uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 8) + 8));
                    gm_l12 = t32;
                    uint64_t t33 = gm_l11;
                    void * t34 = gm_l7;
                    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 0));
                    uint8_t t36 = ((t33 == t35));
                    if (!t36) goto ie29;
                    uint32_t t37 = gm_l12;
                    uint32_t t38 = gm_l2;
                    uint8_t t39 = ((t37 == t38));
                    if (!t39) goto ie29;
                    {
                        gm_b0_1 t41;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t41);
                        goto gm_fdone;
                    }
                    goto ix29;
ie29:;
ix29:;
                }
                const uint8_t *t42 = ((uint8_t *)&gm_l10);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t42, 24);
                goto gm_fdone;
            }
        }
    }
    gm_b0_1 t43;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t43);
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
__attribute__((aligned(4))) void gm_f_loop_body_js59b5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5) {
    struct gm_fr_loop_body_js59b5a F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l3; /* cond */
    void * gm_l4; /* body */
    void * gm_l5; /* step */
    void * gm_l6; /* fr */
    uint64_t gm_l7; /* tm */
    gm_t2lqu8jsj511d gm_l8; /* mk */
    gm_t76123gfet73g gm_l9; /* f */
    uint64_t gm_l10; /* frame */
    uint32_t gm_l11; /* l */
    void * gm_l12; /* st */
    GM_STACK_CHECK(&F, 743u);
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
        void * t2 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        gm_l6 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 416) + 0) + 8);
                gm_l7 = t7;
            }
            {
                void * t8 = gm_l0;
                gm_t2lqu8jsj511d t9 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t8));
                gm_l8 = t9;
            }
            {
                void * t11 = gm_l0;
                uint64_t t12 = GM_LD(uintptr_t, (((((uint8_t *)t11) + 1656) + 0) + 0) + 8);
                uint8_t t13 = (t12 > ((uint64_t)1ULL));
                if (!t13) goto ie10;
                {
                    void * t15 = gm_l0;
                    gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                    gm_tcfa6l7reoosn t17;
                    gm_f_tick_6bsfu3n((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t17))[0] != 0)) {
                        gm_tcfa6l7reoosn t18;
                        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                        ((uint8_t *)&t18)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t18 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t17) + 8)));
                        F.ret = t18;
                        goto gm_fret;
                    }
                    gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 8));
                }
                goto ix10;
ie10:;
ix10:;
            }
            {
                void * t21 = gm_l0;
                gm_tbl8il92l97ut t22 = gm_l1;
                gm_b16_8 t23 = gm_l3;
                gm_t1ec8br9ddkav t24;
                gm_f_conds_30ust77((void *)&t24, (void *)NULL, cx, NULL, t21, t22, t23);
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                if (((((uint8_t *)&t24))[0] != 0)) {
                    gm_tcfa6l7reoosn t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t25 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t24) + 8)));
                    F.ret = t25;
                    goto gm_fret;
                }
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t24) + 8));
                uint8_t t27 = (uint8_t)!t26;
                if (!t27) goto ie20;
                {
                    void * t29 = gm_l0;
                    uint64_t t30 = gm_l7;
                    gm_b0_1 t31 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                }
                {
                    void * t32 = gm_l0;
                    gm_t2lqu8jsj511d t33 = gm_l8;
                    gm_f_release_jn5kqdi(t32, GM_CONV(gm_csc7lc6jlkqkuq, t33));
                }
                {
                    gm_b0_1 t34;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t34);
                    goto gm_fdone;
                }
                goto ix20;
ie20:;
ix20:;
            }
            {
                void * t35 = gm_l0;
                gm_tbl8il92l97ut t36 = gm_l1;
                void * t37 = gm_l4;
                gm_b16_8 t38 = gm_f_empty_slice_ijtp9lo();
                gm_tcfa6l7reoosn t39;
                gm_f_block_5b89ev3((void *)&t39, (void *)NULL, cx, NULL, t35, t36, t37, t38);
                if (gm_unw.kind) {
                    goto gm_ll4;
                }
                gm_b0_1 t40;
                if (!((((uint8_t *)&t39))[0] != 0)) {
                    gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 8));
                    t40 = t41;
                } else {
                    gm_l9 = (*(gm_t76123gfet73g *)(((uint8_t *)&t39) + 8));
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    if (t44 != ((uint8_t)1ULL)) goto ie43;
                    uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)&gm_l9) + 8) + 0));
                    gm_l10 = t45;
                    uint32_t t46 = GM_LD(uint32_t, ((((uint8_t *)&gm_l9) + 8) + 8));
                    gm_l11 = t46;
                    uint64_t t47 = gm_l10;
                    void * t48 = gm_l6;
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 0));
                    uint8_t t50 = ((t47 == t49));
                    if (!t50) goto ie43;
                    uint32_t t51 = gm_l11;
                    uint32_t t52 = gm_l2;
                    uint8_t t53 = ((t51 == t52));
                    if (!t53) goto ie43;
                    goto ix43;
ie43:;
                    const uint8_t *t56 = ((uint8_t *)&gm_l9);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t56, 24);
                    goto gm_fdone;
ix43:;
                    gm_b0_1 t57 = GM_ZERO(gm_b0_1);
                    t40 = t57;
                }
            }
            {
                void * t59 = gm_l5;
                if (!(GM_LD(void *, ((uint8_t *)&t59)) != 0)) goto ie58;
                void * t60 = GM_LD(void *, ((uint8_t *)&t59));
                gm_l12 = t60;
                {
                    void * t62 = gm_l0;
                    gm_tbl8il92l97ut t63 = gm_l1;
                    void * t64 = gm_l12;
                    gm_tcfa6l7reoosn t65;
                    gm_f_ev_discard_8c4a8jg((void *)&t65, (void *)NULL, cx, NULL, t62, t63, t64);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    if (((((uint8_t *)&t65))[0] != 0)) {
                        gm_tcfa6l7reoosn t66;
                        GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                        ((uint8_t *)&t66)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t66 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t65) + 8)));
                        F.ret = t66;
                        goto gm_fret;
                    }
                    gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 8));
                }
                goto ix58;
ie58:;
ix58:;
            }
            {
                void * t69 = gm_l0;
                uint64_t t70 = GM_LD(uintptr_t, ((((uint8_t *)t69) + 416) + 0) + 8);
                uint64_t t71 = gm_l7;
                uint8_t t72 = (t70 > t71);
                if (!t72) goto ie68;
                {
                    void * t74 = gm_l0;
                    uint64_t t75 = gm_l7;
                    gm_b0_1 t76 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t74, t75);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                }
                goto ix68;
ie68:;
ix68:;
            }
            {
                void * t77 = gm_l0;
                gm_t2lqu8jsj511d t78 = gm_l8;
                gm_f_release_jn5kqdi(t77, GM_CONV(gm_csc7lc6jlkqkuq, t78));
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
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
inline __attribute__((aligned(4))) void gm_f_match__tg8e3g3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_match__tg8e3g3 F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    void * gm_l4; /* n */
    gm_b16_8 gm_l5; /* dst */
    gm_b16_8 gm_l6; /* b */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l9; /* arm */
    void * gm_l10; /* g */
    GM_STACK_CHECK(&F, 737u);
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
        void * t3 = gm_l2;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 104));
        gm_b16_8 t5 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t2, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t5;
    }
    {
        void * t6 = gm_l0;
        gm_tbl8il92l97ut t7 = gm_l1;
        void * t8 = gm_l2;
        gm_b16_8 t9 = gm_l6;
        gm_tcfa6l7reoosn t10;
        gm_f_ev_25jfdar((void *)&t10, (void *)NULL, cx, NULL, t6, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) {
            gm_tcfa6l7reoosn t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            ((uint8_t *)&t11)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t11 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t10) + 8)));
            F.ret = t11;
            goto gm_fret;
        }
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 8));
    }
    {
        {
            gm_b16_8 t14 = gm_l3;
            gm_l7 = t14;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l8;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l8;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_tbbqefuhv2r8h t26 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t22 * 24));
                    gm_l9 = t26;
                    {
                        void * t29 = gm_l0;
                        gm_tbl8il92l97ut t30 = gm_l1;
                        void * t31 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
                        gm_b16_8 t32 = gm_l6;
                        gm_t1ec8br9ddkav t33;
                        gm_f_match_pat_og55a3h((void *)&t33, (void *)NULL, cx, NULL, t29, t30, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        if (((((uint8_t *)&t33))[0] != 0)) {
                            gm_tcfa6l7reoosn t34;
                            GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                            ((uint8_t *)&t34)[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t34 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t33) + 8)));
                            F.ret = t34;
                            goto gm_fret;
                        }
                        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t33) + 8));
                        uint8_t t36 = (uint8_t)!t35;
                        if (!t36) goto ie28;
                        {
                            goto lc15;
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    {
                        void * t39 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                        if (!(GM_LD(void *, ((uint8_t *)&t39)) != 0)) goto ie38;
                        void * t40 = GM_LD(void *, ((uint8_t *)&t39));
                        gm_l10 = t40;
                        {
                            void * t43 = gm_l0;
                            gm_tbl8il92l97ut t44 = gm_l1;
                            void * t45 = gm_l10;
                            gm_t1ec8br9ddkav t46;
                            gm_f_cond_v8r7708((void *)&t46, (void *)NULL, cx, NULL, t43, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                            if (((((uint8_t *)&t46))[0] != 0)) {
                                gm_tcfa6l7reoosn t47;
                                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                                ((uint8_t *)&t47)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t47 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t46) + 8)));
                                F.ret = t47;
                                goto gm_fret;
                            }
                            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t46) + 8));
                            uint8_t t49 = (uint8_t)!t48;
                            if (!t49) goto ie42;
                            {
                                goto lc15;
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        goto ix38;
ie38:;
ix38:;
                    }
                    {
                        void * t51 = gm_l0;
                        gm_tbl8il92l97ut t52 = gm_l1;
                        void * t53 = GM_LD(void *, (((uint8_t *)&gm_l9) + 16));
                        gm_b16_8 t54 = gm_l5;
                        gm_f_ev_25jfdar((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t51, t52, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        goto gm_fdone;
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 275, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t56 = gm_l8;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l8 = t59;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
        }
    }
    {
        void * t60 = gm_l0;
        void * t61 = gm_l4;
        gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t61) + 112));
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sqnu8fg3ikbeg));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(34));
        gm_t76123gfet73g t64;
        gm_f_trap_qh84l9d((void *)&t64, (void *)NULL, cx, NULL, t60, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t64);
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
__attribute__((aligned(4))) void gm_f_match_pat_og55a3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_match_pat_og55a3h F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    void * gm_l2; /* pat */
    gm_b16_8 gm_l3; /* b */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* l */
    void * gm_l6; /* e */
    gm_b16_8 gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* inner */
    unsigned __int128 gm_l10; /* v */
    uint8_t gm_l11; /* signed */
    unsigned __int128 gm_l12; /* lo */
    unsigned __int128 gm_l13; /* hi */
    __int128 gm_l14; /* x */
    uint8_t gm_l15; /* ok_hi */
    uint8_t gm_l16; /* ok_hi */
    void * gm_l17; /* p */
    void * gm_l18; /* p */
    void * gm_l19; /* p */
    gm_b16_8 gm_l21; /* tb */
    void * gm_l22; /* p */
    uint64_t gm_l23; /* o */
    gm_b16_8 gm_l24; /* alts */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    void * gm_l27; /* p */
    gm_b16_8 gm_l28; /* fields */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l31; /* f */
    gm_b16_8 gm_l32; /* fb */
    GM_STACK_CHECK(&F, 3059u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l2;
        gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 144));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l2;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma15; default: goto ma17; }
ma6:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t20 != ((uint8_t)0ULL)) goto mn19;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
        goto gm_fdone;
        goto me5;
mn19:;
ma7:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        uint32_t t23 = GM_LD(uint32_t, ((((uint8_t *)t4) + 16) + 16));
        gm_l5 = t23;
        {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l4;
            void * t27 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
            uint32_t t28 = gm_l5;
            gm_b16_8 t29 = gm_l3;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_tcfa6l7reoosn t31;
            gm_f_set_local_ug1vgfu((void *)&t31, (void *)NULL, cx, NULL, t25, t26, t27, t28, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t31))[0] != 0)) {
                gm_t1ec8br9ddkav t32;
                GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
                ((uint8_t *)&t32)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t32 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t31) + 8)));
                F.ret = t32;
                goto gm_fret;
            }
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 8));
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
            goto gm_fdone;
        }
        goto me5;
mn21:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t35 != ((uint8_t)2ULL)) goto mn34;
        void * t36 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l6 = t36;
        {
            void * t38 = gm_l0;
            void * t39 = gm_l6;
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 104));
            gm_b16_8 t41 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t38, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t41;
        }
        {
            void * t42 = gm_l0;
            gm_tbl8il92l97ut t43 = gm_l1;
            void * t44 = gm_l6;
            gm_b16_8 t45 = gm_l7;
            gm_tcfa6l7reoosn t46;
            gm_f_ev_25jfdar((void *)&t46, (void *)NULL, cx, NULL, t42, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t46))[0] != 0)) {
                gm_t1ec8br9ddkav t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                ((uint8_t *)&t47)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t47 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t46) + 8)));
                F.ret = t47;
                goto gm_fret;
            }
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 8));
        }
        {
            void * t50 = gm_l6;
            gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + 96));
            void * t52 = gm_l2;
            gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 0));
            uint8_t t54 = (!gm_eq9m6mvtmgde(((uint8_t *)&t51), ((uint8_t *)&t53)));
            if (!t54) goto ie49;
            void * t55 = gm_l0;
            void * t56 = gm_l2;
            gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t56) + 0));
            gm_t53at80pfdus3 t58;
            gm_f_kind_htv6093((void *)&t58, t55, GM_CONV(gm_csguankko4lmhd, t57));
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t58) + 0));
            if (t59 != ((uint8_t)13ULL)) goto ie49;
            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t58) + 8));
            gm_l8 = t60;
            gm_t88rcucg1b7uk t61 = gm_l8;
            void * t62 = gm_l6;
            gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t62) + 96));
            uint8_t t64 = (gm_eq9m6mvtmgde(((uint8_t *)&t61), ((uint8_t *)&t63)));
            if (!t64) goto ie49;
            {
                void * t67 = gm_l0;
                void * t68 = gm_l2;
                gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
                gm_b16_8 t70 = gm_l3;
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                uint8_t t72 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t67, t69, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t73 = (uint8_t)!t72;
                if (!t73) goto ie66;
                {
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix66;
ie66:;
ix66:;
            }
            {
                void * t75 = gm_l0;
                gm_tl874t2atnlh3 t76 = gm_l4;
                void * t77 = gm_l6;
                gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t77) + 96));
                void * t79 = gm_l0;
                void * t80 = gm_l2;
                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
                gm_b16_8 t82 = gm_l3;
                gm_b16_8 t83 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t79, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t84 = gm_l7;
                gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t75, t76, t78, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix49;
ie49:;
ix49:;
        }
        {
            void * t85 = gm_l0;
            gm_tl874t2atnlh3 t86 = gm_l4;
            void * t87 = gm_l2;
            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + 0));
            gm_b16_8 t89 = gm_l3;
            gm_b16_8 t90 = gm_l7;
            gm_f_equal_e5vf82h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t85, t86, t88, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn34:;
ma9:;
        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t92 != ((uint8_t)3ULL)) goto mn91;
        gm_t59juu943t8sl t93 = GM_LD(gm_t59juu943t8sl, ((((uint8_t *)t4) + 16) + 16));
        F.l9 = t93;
        {
            void * t95 = gm_l0;
            void * t96 = gm_l2;
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 0));
            gm_b16_8 t98 = gm_l3;
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            unsigned __int128 t100 = gm_f_get_int_pg7p4gc(t95, GM_CONV(gm_csguankko4lmhd, t97), t99);
            gm_l10 = t100;
        }
        {
            void * t101 = gm_l0;
            void * t102 = gm_l2;
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 0));
            gm_tg5lql6itufk7 t104 = gm_f_int_info_ulcevdv(t101, GM_CONV(gm_csguankko4lmhd, t103));
            gm_ttovhfee8mf28 t105;
            if (((((uint8_t *)&t104))[8] != 0)) {
                gm_ttovhfee8mf28 t106 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t104) + 0));
                t105 = t106;
            } else {
                gm_ttovhfee8mf28 t107;
                GM_ST(uint8_t, (uint8_t *)&t107 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t107 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0));
                GM_ST(uint32_t, ((uint8_t *)&t107 + 4), ((uint32_t)0ULL));
                t105 = t107;
            }
            uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
            gm_l11 = t108;
        }
        {
            unsigned __int128 t109 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l9) + 0)));
            gm_l12 = t109;
        }
        {
            unsigned __int128 t110 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l9) + 48)));
            gm_l13 = t110;
        }
        {
            uint8_t t112 = gm_l11;
            if (!t112) goto ie111;
            {
                unsigned __int128 t114 = gm_l10;
                __int128 t115;
                memset(&t115, 0, sizeof t115); memcpy(&t115, ((uint8_t *)&t114), sizeof t115 < 16 ? sizeof t115 : 16);
                gm_l14 = t115;
            }
            {
                uint8_t t116;
                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 96));
                if (!t118) goto ie117;
                __int128 t120 = gm_l14;
                unsigned __int128 t121 = gm_l13;
                __int128 t122;
                memset(&t122, 0, sizeof t122); memcpy(&t122, ((uint8_t *)&t121), sizeof t122 < 16 ? sizeof t122 : 16);
                uint8_t t123 = (t120 <= t122);
                uint8_t t124 = t123;
                t116 = t124;
                goto ix117;
ie117:;
                __int128 t126 = gm_l14;
                unsigned __int128 t127 = gm_l13;
                __int128 t128;
                memset(&t128, 0, sizeof t128); memcpy(&t128, ((uint8_t *)&t127), sizeof t128 < 16 ? sizeof t128 : 16);
                uint8_t t129 = (t126 < t128);
                uint8_t t130 = t129;
                t116 = t130;
ix117:;
                gm_l15 = t116;
            }
            {
                uint8_t t131 = 0;
                __int128 t132 = gm_l14;
                unsigned __int128 t133 = gm_l12;
                __int128 t134;
                memset(&t134, 0, sizeof t134); memcpy(&t134, ((uint8_t *)&t133), sizeof t134 < 16 ? sizeof t134 : 16);
                uint8_t t135 = (t132 >= t134);
                if (t135) {
                    uint8_t t136 = gm_l15;
                    t131 = t136;
                } else t131 = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t131);
                goto gm_fdone;
            }
            goto ix111;
ie111:;
ix111:;
        }
        {
            uint8_t t137;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 96));
            if (!t139) goto ie138;
            unsigned __int128 t141 = gm_l10;
            unsigned __int128 t142 = gm_l13;
            uint8_t t143 = (t141 <= t142);
            uint8_t t144 = t143;
            t137 = t144;
            goto ix138;
ie138:;
            unsigned __int128 t146 = gm_l10;
            unsigned __int128 t147 = gm_l13;
            uint8_t t148 = (t146 < t147);
            uint8_t t149 = t148;
            t137 = t149;
ix138:;
            gm_l16 = t137;
        }
        {
            uint8_t t150 = 0;
            unsigned __int128 t151 = gm_l10;
            unsigned __int128 t152 = gm_l12;
            uint8_t t153 = (t151 >= t152);
            if (t153) {
                uint8_t t154 = gm_l16;
                t150 = t154;
            } else t150 = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t150);
            goto gm_fdone;
        }
        goto me5;
mn91:;
ma10:;
        uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t156 != ((uint8_t)4ULL)) goto mn155;
        void * t157 = gm_l0;
        void * t158 = gm_l2;
        gm_t88rcucg1b7uk t159 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t158) + 0));
        gm_b16_8 t160 = gm_l3;
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        uint8_t t162 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t157, t159, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t163 = (uint8_t)!t162;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, t163);
        goto gm_fdone;
        goto me5;
mn155:;
ma11:;
        uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t165 != ((uint8_t)5ULL)) goto mn164;
        void * t166 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l17 = t166;
        {
            void * t169 = gm_l0;
            void * t170 = gm_l2;
            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t170) + 0));
            gm_b16_8 t172 = gm_l3;
            gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
            uint8_t t174 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t169, t171, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t175 = (uint8_t)!t174;
            if (!t175) goto ie168;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix168;
ie168:;
ix168:;
        }
        {
            void * t177 = gm_l0;
            gm_tbl8il92l97ut t178 = gm_l1;
            void * t179 = gm_l17;
            void * t180 = gm_l0;
            void * t181 = gm_l2;
            gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t181) + 0));
            gm_b16_8 t183 = gm_l3;
            gm_b16_8 t184 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t180, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_match_pat_og55a3h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t177, t178, t179, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn164:;
ma12:;
        uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t186 != ((uint8_t)6ULL)) goto mn185;
        void * t187 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l18 = t187;
        {
            void * t190 = gm_l0;
            void * t191 = gm_l2;
            gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t191) + 0));
            gm_b16_8 t193 = gm_l3;
            gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
            uint8_t t195 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t190, t192, t194);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t195) goto ie189;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix189;
ie189:;
ix189:;
        }
        {
            void * t197 = gm_l0;
            gm_tbl8il92l97ut t198 = gm_l1;
            void * t199 = gm_l18;
            void * t200 = gm_l0;
            void * t201 = gm_l2;
            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t201) + 0));
            gm_b16_8 t203 = gm_l3;
            gm_b16_8 t204 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t200, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_match_pat_og55a3h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t197, t198, t199, t204);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn185:;
ma13:;
        uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t206 != ((uint8_t)7ULL)) goto mn205;
        void * t207 = GM_LD(void *, ((((uint8_t *)t4) + 16) + 16));
        gm_l19 = t207;
        {
            void * t210 = gm_l0;
            void * t211 = gm_l2;
            gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t211) + 0));
            gm_b16_8 t213 = gm_l3;
            gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
            uint8_t t215 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t210, t212, t214);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t216 = (uint8_t)!t215;
            if (!t216) goto ie209;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix209;
ie209:;
ix209:;
        }
        {
            void * t218 = gm_l0;
            gm_tbl8il92l97ut t219 = gm_l1;
            void * t220 = gm_l19;
            void * t221 = gm_l0;
            void * t222 = gm_l2;
            gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t222) + 0));
            gm_b16_8 t224 = gm_l3;
            gm_b16_8 t225 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t221, t223, t224);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_match_pat_og55a3h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t218, t219, t220, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn205:;
ma14:;
        uint8_t t227 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t227 != ((uint8_t)8ULL)) goto mn226;
        gm_t4nomjfe3hlif t228 = GM_LD(gm_t4nomjfe3hlif, ((((uint8_t *)t4) + 16) + 16));
        F.l20 = t228;
        {
            gm_b16_8 t230 = gm_l3;
            uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&t230) + 8);
            void * t232 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t230)));
            uint64_t t233 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
            uint64_t t235 = gm_f_us_cuv01fp(t234);
            uint64_t t236 = (uintptr_t)t235;
            if (t233 > t236 || t236 > (uintptr_t)t231) gm_trap_slice(t233, t236, t231, gm_srcph0vopkouaai, 245, 19);
            gm_b16_8 t237;
            GM_ST(void *, (uint8_t *)&t237, (void *)(((uint8_t *)t232 + t233 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(t236 - t233));
            gm_l21 = t237;
        }
        {
            uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
            uint8_t t240 = (t239 <= ((uint64_t)8ULL));
            if (!t240) goto ie238;
            {
                void * t243 = gm_l0;
                gm_b16_8 t244 = gm_l21;
                gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                uint64_t t246 = gm_f_rd_word_8flcq6o(t243, t245);
                uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 32));
                uint8_t t248 = (!(t246 == t247));
                if (!t248) goto ie242;
                {
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                    goto gm_fdone;
                }
                goto ix242;
ie242:;
ix242:;
            }
            goto ix238;
ie238:;
            void * t251 = gm_l0;
            gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l20) + 4));
            gm_b16_8 t253 = gm_l21;
            gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
            unsigned __int128 t255 = gm_f_get_int_pg7p4gc(t251, GM_CONV(gm_csguankko4lmhd, t252), t254);
            unsigned __int128 t256 = GM_LD(unsigned __int128, (((uint8_t *)&F.l20) + 16));
            uint8_t t257 = (!(t255 == t256));
            if (!t257) goto ie250;
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                goto gm_fdone;
            }
            goto ix250;
ie250:;
ix250:;
ix238:;
        }
        {
            void * t259 = GM_LD(void *, (((uint8_t *)&F.l20) + 56));
            void * t260;
            if ((GM_LD(void *, ((uint8_t *)&t259)) != 0)) {
                void * t261 = GM_LD(void *, (((uint8_t *)&t259) + 0));
                t260 = t261;
            } else {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                goto gm_fdone;
            }
            gm_l22 = t260;
        }
        {
            uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 40));
            uint64_t t263 = gm_f_us_cuv01fp(t262);
            gm_l23 = t263;
        }
        {
            void * t264 = gm_l0;
            gm_tbl8il92l97ut t265 = gm_l1;
            void * t266 = gm_l22;
            gm_b16_8 t267 = gm_l3;
            uint64_t t268 = GM_LD(uintptr_t, ((uint8_t *)&t267) + 8);
            void * t269 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t267)));
            uint64_t t270 = gm_l23;
            uint64_t t271 = (uintptr_t)t270;
            uint64_t t272 = gm_l23;
            uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 48));
            uint64_t t274 = gm_f_us_cuv01fp(t273);
            uint64_t r275; bool o276 = __builtin_add_overflow(t272, t274, &r275);
            if (o276) gm_trap("integer overflow in `+`", gm_srcph0vopkouaai, 253, 43);
            uint64_t t277 = r275;
            uint64_t t278 = (uintptr_t)t277;
            if (t271 > t278 || t278 > (uintptr_t)t268) gm_trap_slice(t271, t278, t268, gm_srcph0vopkouaai, 253, 38);
            gm_b16_8 t279;
            GM_ST(void *, (uint8_t *)&t279, (void *)(((uint8_t *)t269 + t271 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(t278 - t271));
            gm_f_match_pat_og55a3h((void *)&(*(gm_t1ec8br9ddkav *)gm_out), (void *)NULL, cx, NULL, t264, t265, t266, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me5;
mn226:;
ma15:;
        uint8_t t281 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t281 != ((uint8_t)10ULL)) goto mn280;
        gm_b16_8 t282 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 16) + 16));
        gm_l24 = t282;
        {
            {
                gm_b16_8 t285 = gm_l24;
                gm_l25 = t285;
            }
            {
                gm_l26 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t287 = gm_l26;
                    uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                    uint8_t t289 = (t287 < t288);
                    if (!t289) goto lb286;
                    {
                        uint64_t t292 = gm_l26;
                        uint64_t t293 = (uintptr_t)t292;
                        uint64_t t294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l25) + 8);
                        void * t295 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l25)) + t293 * 8));
                        void * t298 = GM_LD(void *, ((uint8_t *)&t295));
                        gm_l27 = t298;
                        {
                            void * t301 = gm_l0;
                            gm_tbl8il92l97ut t302 = gm_l1;
                            void * t303 = gm_l27;
                            gm_b16_8 t304 = gm_l3;
                            gm_t1ec8br9ddkav t305;
                            gm_f_match_pat_og55a3h((void *)&t305, (void *)NULL, cx, NULL, t301, t302, t303, t304);
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                            if (((((uint8_t *)&t305))[0] != 0)) {
                                gm_t1ec8br9ddkav t306;
                                GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
                                ((uint8_t *)&t306)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t306 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t305) + 8)));
                                F.ret = t306;
                                goto gm_fret;
                            }
                            uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&t305) + 8));
                            if (!t307) goto ie300;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
                                goto gm_fdone;
                            }
                            goto ix300;
ie300:;
ix300:;
                        }
                        goto me296;
mn297:;
                        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 256, 13);
me296:;
                    }
lc286:;
                    {
                        uint64_t t310 = gm_l26;
                        uint64_t r311; bool o312 = __builtin_add_overflow(t310, ((uint64_t)1ULL), &r311);
                        uint64_t t313 = r311;
                        gm_l26 = t313;
                    }
                }
gm_ll290:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb286; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc286; }
                }
                goto gm_lad;
lb286:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
            goto gm_fdone;
        }
        goto me5;
mn280:;
ma16:;
        uint8_t t315 = GM_LD(uint8_t, ((((uint8_t *)t4) + 16) + 0));
        if (t315 != ((uint8_t)9ULL)) goto mn314;
        gm_b16_8 t316 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 16) + 16));
        gm_l28 = t316;
        {
            {
                gm_b16_8 t319 = gm_l28;
                gm_l29 = t319;
            }
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t321 = gm_l30;
                    uint64_t t322 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    uint8_t t323 = (t321 < t322);
                    if (!t323) goto lb320;
                    {
                        uint64_t t326 = gm_l30;
                        uint64_t t327 = (uintptr_t)t326;
                        uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        gm_ttggq7ng4kp3p t331 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t327 * 16));
                        gm_l31 = t331;
                        {
                            void * t333 = gm_l0;
                            void * t334 = gm_l0;
                            void * t335 = GM_LD(void *, (((uint8_t *)&gm_l31) + 8));
                            gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t335) + 0));
                            uint64_t t337 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t334, t336);
                            if (gm_unw.kind) {
                                goto gm_ll324;
                            }
                            gm_b16_8 t338 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t333, t337);
                            if (gm_unw.kind) {
                                goto gm_ll324;
                            }
                            gm_l32 = t338;
                        }
                        {
                            void * t339 = gm_l0;
                            void * t340 = gm_l2;
                            gm_t88rcucg1b7uk t341 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t340) + 0));
                            gm_b16_8 t342 = gm_l3;
                            uint32_t t343 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 0));
                            gm_b16_8 t344 = gm_l32;
                            gm_b0_1 t345 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t339, t341, t342, t343, t344);
                            if (gm_unw.kind) {
                                goto gm_ll324;
                            }
                        }
                        {
                            void * t347 = gm_l0;
                            gm_tbl8il92l97ut t348 = gm_l1;
                            void * t349 = GM_LD(void *, (((uint8_t *)&gm_l31) + 8));
                            gm_b16_8 t350 = gm_l32;
                            gm_t1ec8br9ddkav t351;
                            gm_f_match_pat_og55a3h((void *)&t351, (void *)NULL, cx, NULL, t347, t348, t349, t350);
                            if (gm_unw.kind) {
                                goto gm_ll324;
                            }
                            if (((((uint8_t *)&t351))[0] != 0)) {
                                gm_t1ec8br9ddkav t352;
                                GM_ST(uint8_t, (uint8_t *)&t352 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
                                ((uint8_t *)&t352)[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t352 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t351) + 8)));
                                F.ret = t352;
                                goto gm_fret;
                            }
                            uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&t351) + 8));
                            uint8_t t354 = (uint8_t)!t353;
                            if (!t354) goto ie346;
                            {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)0));
                                goto gm_fdone;
                            }
                            goto ix346;
ie346:;
ix346:;
                        }
                        goto me329;
mn330:;
                        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 262, 13);
me329:;
                    }
lc320:;
                    {
                        uint64_t t357 = gm_l30;
                        uint64_t r358; bool o359 = __builtin_add_overflow(t357, ((uint64_t)1ULL), &r358);
                        uint64_t t360 = r358;
                        gm_l30 = t360;
                    }
                }
gm_ll324:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb320; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc320; }
                }
                goto gm_lad;
lb320:;
            }
        }
        {
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out))[0] = 0;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1ec8br9ddkav *)gm_out) + 8, ((uint8_t)1));
            goto gm_fdone;
        }
        goto me5;
mn314:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 203, 5);
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
__attribute__((aligned(4))) void gm_f_run_defers_buhs35d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_run_defers_buhs35d F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    gm_b16_8 gm_l2; /* defers */
    gm_t07lf02ct4q61 gm_l3; /* failed */
    uint8_t gm_l4; /* run_err */
    gm_t76123gfet73g gm_l5; /* f */
    uint64_t gm_l6; /* i */
    gm_tk7kkm8e69l5f gm_l7; /* d */
    uint64_t gm_l8; /* tm */
    gm_t2lqu8jsj511d gm_l9; /* mk */
    gm_tcfa6l7reoosn gm_l10; /* r */
    gm_t76123gfet73g gm_l11; /* f */
    gm_t76123gfet73g gm_l12; /* f */
    GM_STACK_CHECK(&F, 614u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    {
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            if (!((((uint8_t *)&gm_l3))[24] != 0)) goto ie3;
            gm_t76123gfet73g t4 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l3));
            gm_l5 = t4;
            {
                uint8_t t7;
                uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                if (t10 != ((uint8_t)4ULL)) goto mn9;
                t7 = ((uint8_t)1);
                goto me8;
mn9:;
                t7 = ((uint8_t)0);
                goto me8;
mn11:;
                gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 73, 12);
me8:;
                if (!t7) goto ie6;
                {
                    const uint8_t *t13 = ((uint8_t *)&gm_l5);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t13, 24);
                    goto gm_fdone;
                }
                goto ix6;
ie6:;
ix6:;
            }
            {
                void * t14 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
                uint8_t t15 = gm_f_errdefers_run_kn2msce(t14, &(*(gm_t76123gfet73g *)((uint8_t *)&gm_l5)));
                gm_l4 = t15;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l6 = t16;
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint8_t t19 = (t18 > ((uint64_t)0ULL));
                if (!t19) goto lb17;
                {
                    uint64_t t22 = gm_l6;
                    uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                    if (o24) gm_trap("integer overflow in `-`", gm_srcph0vopkouaai, 78, 9);
                    uint64_t t25 = r23;
                    gm_l6 = t25;
                }
                {
                    uint64_t t26 = gm_l6;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcph0vopkouaai, 79, 14);
                    gm_tk7kkm8e69l5f t29 = GM_LD(gm_tk7kkm8e69l5f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t27 * 24));
                    gm_l7 = t29;
                }
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    if (!t31) goto ie30;
                    uint8_t t32 = gm_l4;
                    uint8_t t33 = (uint8_t)!t32;
                    if (!t33) goto ie30;
                    {
                        goto lc17;
                    }
                    goto ix30;
ie30:;
ix30:;
                }
                {
                    void * t35 = gm_l0;
                    uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 416) + 0) + 8);
                    gm_l8 = t36;
                }
                {
                    void * t37 = gm_l0;
                    gm_t2lqu8jsj511d t38 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t37));
                    gm_l9 = t38;
                }
                {
                    void * t39 = gm_l0;
                    gm_tbl8il92l97ut t40 = gm_l1;
                    void * t41 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                    gm_f_ev_discard_8c4a8jg((void *)&gm_l10, (void *)NULL, cx, NULL, t39, t40, t41);
                    if (gm_unw.kind) {
                        goto gm_ll20;
                    }
                }
                {
                    void * t42 = gm_l0;
                    uint64_t t43 = gm_l8;
                    gm_b0_1 t44 = gm_f_kill_temps_8sgbc55((void *)NULL, cx, NULL, t42, t43);
                    if (gm_unw.kind) {
                        goto gm_ll20;
                    }
                }
                {
                    void * t45 = gm_l0;
                    gm_t2lqu8jsj511d t46 = gm_l9;
                    gm_f_release_jn5kqdi(t45, GM_CONV(gm_csc7lc6jlkqkuq, t46));
                }
                {
                    gm_tcfa6l7reoosn t47 = gm_l10;
                    gm_b0_1 t48;
                    if (!((((uint8_t *)&t47))[0] != 0)) {
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 8));
                        t48 = t49;
                    } else {
                        gm_l11 = (*(gm_t76123gfet73g *)(((uint8_t *)&t47) + 8));
                        {
                            uint8_t t52;
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                            if (t55 != ((uint8_t)4ULL)) goto mn54;
                            t52 = ((uint8_t)1);
                            goto me53;
mn54:;
                            t52 = ((uint8_t)0);
                            goto me53;
mn56:;
                            gm_trap("no match arm matched the scrutinee", gm_srcph0vopkouaai, 87, 16);
me53:;
                            if (!t52) goto ie51;
                            {
                                const uint8_t *t58 = ((uint8_t *)&gm_l11);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t58, 24);
                                goto gm_fdone;
                            }
                            goto ix51;
ie51:;
ix51:;
                        }
                        void * t59 = gm_l0;
                        void * t60 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 112));
                        gm_b16_8 t62;
                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s9h40dg66q82b));
                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(57));
                        gm_t76123gfet73g t63;
                        gm_f_trap_qh84l9d((void *)&t63, (void *)NULL, cx, NULL, t59, t61, t62);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t63);
                        goto gm_fdone;
                    }
                }
lc17:;
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
        {
            if (!((((uint8_t *)&gm_l3))[24] != 0)) goto ie64;
            gm_t76123gfet73g t65 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l3));
            gm_l12 = t65;
            {
                const uint8_t *t67 = ((uint8_t *)&gm_l12);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                memmove((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t67, 24);
                goto gm_fdone;
            }
            goto ix64;
ie64:;
ix64:;
        }
    }
    gm_b0_1 t68;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t68);
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
__attribute__((aligned(4))) void gm_f_set_local_node_he6tesa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, uint32_t a2, void * a3) {
    struct gm_fr_set_local_node_he6tesa F;
    void * gm_l0; /* m */
    gm_tbl8il92l97ut gm_l1; /* a */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* value */
    void * gm_l4; /* fr */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* v */
    uint64_t gm_l7; /* off */
    gm_b16_8 gm_l8; /* b */
    GM_STACK_CHECK(&F, 732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = GM_LD(void *, (((uint8_t *)&gm_l1) + 0));
        gm_l4 = t2;
    }
    {
        uint32_t t3 = gm_l2;
        uint64_t t4 = (uint64_t)t3;
        uint64_t t5 = gm_f_us_cuv01fp(t4);
        gm_l5 = t5;
    }
    {
        void * t7 = gm_l3;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 104));
        uint8_t t9 = (t8 <= ((uint64_t)8ULL));
        if (!t9) goto ie6;
        uint64_t t10 = gm_l5;
        uint64_t t11 = (uintptr_t)t10;
        void * t12 = gm_l4;
        uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 64) + 8);
        if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcph0vopkouaai, 112, 28);
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 64)) + t11 * 8));
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        if (!t15) goto ie6;
        uint64_t t16 = gm_l5;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l4;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 16));
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 120) + 8);
        if (t17 >= (uintptr_t)(t20)) gm_trap_index(t17, t20, gm_srcph0vopkouaai, 112, 51);
        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 120)) + t17 * 1));
        uint8_t t22 = (uint8_t)!t21;
        if (!t22) goto ie6;
        {
            void * t24 = gm_l0;
            gm_tbl8il92l97ut t25 = gm_l1;
            void * t26 = gm_l3;
            gm_t833u93mr4iob t27;
            gm_f_ew_c5kil07((void *)&t27, (void *)NULL, cx, NULL, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_tcfa6l7reoosn t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t27) + 8)));
                F.ret = t28;
                goto gm_fret;
            }
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t27) + 8));
            gm_l6 = t29;
        }
        {
            uint64_t t30 = gm_l5;
            uint64_t t31 = (uintptr_t)t30;
            void * t32 = gm_l4;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 16));
            uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 72) + 8);
            if (t31 >= (uintptr_t)(t34)) gm_trap_index(t31, t34, gm_srcph0vopkouaai, 114, 19);
            uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 72)) + t31 * 8));
            uint64_t t36 = gm_f_us_cuv01fp(t35);
            gm_l7 = t36;
        }
        {
            void * t37 = gm_l0;
            void * t38 = gm_l4;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 48));
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&t39) + 8);
            void * t41 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t39)));
            uint64_t t42 = gm_l7;
            uint64_t t43 = (uintptr_t)t42;
            uint64_t t44 = gm_l7;
            void * t45 = gm_l3;
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 104));
            uint64_t t47 = gm_f_us_cuv01fp(t46);
            uint64_t r48; bool o49 = __builtin_add_overflow(t44, t47, &r48);
            if (o49) gm_trap("integer overflow in `+`", gm_srcph0vopkouaai, 115, 33);
            uint64_t t50 = r48;
            uint64_t t51 = (uintptr_t)t50;
            if (t43 > t51 || t51 > (uintptr_t)t40) gm_trap_slice(t43, t51, t40, gm_srcph0vopkouaai, 115, 19);
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t41 + t43 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t43));
            uint64_t t53 = gm_l6;
            gm_f_wr_word_npte9fs(t37, t52, t53);
        }
        {
            gm_b0_1 t54;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t54);
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t55 = gm_l0;
        void * t56 = gm_l3;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 104));
        gm_b16_8 t58 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t58;
    }
    {
        void * t59 = gm_l0;
        gm_tbl8il92l97ut t60 = gm_l1;
        void * t61 = gm_l3;
        gm_b16_8 t62 = gm_l8;
        gm_tcfa6l7reoosn t63;
        gm_f_ev_25jfdar((void *)&t63, (void *)NULL, cx, NULL, t59, t60, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t63))[0] != 0)) {
            gm_tcfa6l7reoosn t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t64 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t63) + 8)));
            F.ret = t64;
            goto gm_fret;
        }
        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 8));
    }
    {
        void * t66 = gm_l0;
        void * t67 = gm_l3;
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 112));
        void * t69 = gm_l4;
        uint32_t t70 = gm_l2;
        gm_b16_8 t71 = gm_l8;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        gm_f_set_local_ug1vgfu((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t66, t68, t69, t70, t72);
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
